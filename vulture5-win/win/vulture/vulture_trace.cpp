/*
 * Vulture mod (IMPLEMENTATION_LOG s98): startup trace + last words.
 *
 * vulture_trace(step) appends a timestamped line to vulture_log.txt.
 * vulture_install_trace() registers handlers that record WHY the process
 * ends when Windows doesn't log a crash: an unhandled SEH exception (code
 * + RVA, for addr2line on binary/NetHackV-syms.exe), an uncaught C++
 * exception (std::terminate), abort(), or a plain exit().
 * Kept free of NetHack headers so <windows.h> can't collide with them.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <exception>

static void vtr_write(const char *msg)
{
	FILE *f = fopen("vulture_log.txt", "a");
	SYSTEMTIME st;

	if (!f)
		return;
	GetLocalTime(&st);
	fprintf(f, "[trace %02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute,
	        st.wSecond, st.wMilliseconds, msg);
	fclose(f);
}

extern "C" void vulture_trace(const char *step)
{
	vtr_write(step ? step : "(null)");
}

static LONG WINAPI vtr_seh(EXCEPTION_POINTERS *ep)
{
	char buf[200];
	ULONG_PTR base = (ULONG_PTR) GetModuleHandleA(NULL);
	ULONG_PTR addr = (ULONG_PTR) ep->ExceptionRecord->ExceptionAddress;

	_snprintf(buf, sizeof buf, "CRASH: exception 0x%08lx at RVA 0x%llx",
	          (unsigned long) ep->ExceptionRecord->ExceptionCode,
	          (unsigned long long) (addr - base));
	buf[sizeof buf - 1] = 0;
	vtr_write(buf);
	return EXCEPTION_CONTINUE_SEARCH;
}

static void vtr_terminate(void)
{
	char buf[300];

	try {
		std::exception_ptr ep = std::current_exception();

		if (ep)
			std::rethrow_exception(ep);
		vtr_write("TERMINATE: std::terminate with no active exception");
	} catch (const std::exception &e) {
		_snprintf(buf, sizeof buf, "TERMINATE: uncaught C++ exception: %s", e.what());
		buf[sizeof buf - 1] = 0;
		vtr_write(buf);
	} catch (...) {
		vtr_write("TERMINATE: uncaught non-std C++ exception");
	}
	abort();
}

static void vtr_abort(int)
{
	vtr_write("ABORT: abort() called");
}

static void vtr_atexit(void)
{
	vtr_write("EXIT: process exiting via exit()/return from main");
}

extern "C" void vulture_install_trace(void)
{
	static int done = 0;

	if (done)
		return;
	done = 1;
	SetUnhandledExceptionFilter(vtr_seh);
	std::set_terminate(vtr_terminate);
	signal(SIGABRT, vtr_abort);
	atexit(vtr_atexit);
	vtr_write("startup: trace installed");
}
