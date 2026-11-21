/*
 * Vulture mod (IMPLEMENTATION_LOG s116): Discord Rich Presence.
 *
 * Shows what you're doing on your Discord profile / friends list:
 *   "Hilda the Valkyrie - XL 5"  /  "Gnomish Mines 4 - HP 30/42"
 * plus time played.  Talks to the running Discord client over its local
 * IPC pipe (\\.\pipe\discord-ipc-N) - no SDK, no network from the game.
 *
 * Needs a Discord "application" ID: the number in discord_app_id.txt in
 * the game folder (or the REVULTING_DISCORD_APP env var).  No ID, or no
 * Discord running: silently does nothing.  All pipe work happens on a
 * background thread; the game only hands over the latest text.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>

static CRITICAL_SECTION pr_lock;
static HANDLE pr_wake = NULL;
static std::string pr_details, pr_state;
static bool pr_dirty = false, pr_inited = false, pr_disabled = false;
static char pr_appid[32];
static long long pr_start = 0;

static void pr_json_str(std::string &out, const std::string &s)
{
	size_t i;

	out += '"';
	for (i = 0; i < s.size() && i < 120; i++) {
		unsigned char c = (unsigned char) s[i];

		if (c == '"' || c == '\\') {
			out += '\\';
			out += (char) c;
		} else if (c < 0x20 || c >= 0x7f) {
			out += ' '; /* keep it plain ASCII */
		} else {
			out += (char) c;
		}
	}
	out += '"';
}

static bool pr_write_frame(HANDLE h, int op, const std::string &json)
{
	DWORD wrote;
	unsigned char hdr[8];
	unsigned int len = (unsigned int) json.size();

	memcpy(hdr, &op, 4);
	memcpy(hdr + 4, &len, 4);
	return WriteFile(h, hdr, 8, &wrote, NULL) && wrote == 8
	       && WriteFile(h, json.data(), len, &wrote, NULL) && wrote == len;
}

/* drain anything Discord sent back (we don't need the replies) */
static void pr_drain(HANDLE h)
{
	DWORD avail = 0, got;
	char buf[2048];

	while (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) && avail > 0) {
		if (!ReadFile(h, buf, avail > sizeof buf ? sizeof buf : avail, &got, NULL)
		    || !got)
			break;
		avail = 0;
	}
}

static HANDLE pr_connect(void)
{
	char name[64];
	int i;

	for (i = 0; i < 10; i++) {
		HANDLE h;

		snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", i);
		h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		                OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			std::string hello = std::string("{\"v\":1,\"client_id\":\"") + pr_appid + "\"}";

			if (pr_write_frame(h, 0, hello)) {
				Sleep(300);
				pr_drain(h);
				return h;
			}
			CloseHandle(h);
		}
	}
	return INVALID_HANDLE_VALUE;
}

static DWORD WINAPI pr_worker(LPVOID)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD last_send = 0, last_try = 0;
	int nonce = 1;

	for (;;) {
		std::string details, state, json;
		bool dirty;
		DWORD now;

		WaitForSingleObject(pr_wake, 2000);
		now = GetTickCount();
		EnterCriticalSection(&pr_lock);
		dirty = pr_dirty;
		details = pr_details, state = pr_state;
		LeaveCriticalSection(&pr_lock);
		if (h != INVALID_HANDLE_VALUE)
			pr_drain(h);
		if (!dirty)
			continue;
		/* Discord allows ~5 updates per 20 s: keep to one per 15 s */
		if (last_send && now - last_send < 15000)
			continue;
		if (h == INVALID_HANDLE_VALUE) {
			if (last_try && now - last_try < 20000)
				continue;
			last_try = now;
			h = pr_connect();
			if (h == INVALID_HANDLE_VALUE)
				continue; /* Discord not running: try again later */
		}
		json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":";
		json += std::to_string((unsigned long) GetCurrentProcessId());
		json += ",\"activity\":{\"details\":";
		pr_json_str(json, details);
		json += ",\"state\":";
		pr_json_str(json, state);
		json += ",\"timestamps\":{\"start\":" + std::to_string(pr_start) + "}";
		json += ",\"assets\":{\"large_image\":\"revulting\",\"large_text\":\"Revulting - NetHack 5 + Vulture\"}";
		json += "}},\"nonce\":\"" + std::to_string(nonce++) + "\"}";
		if (!pr_write_frame(h, 1, json)) {
			CloseHandle(h); /* Discord closed: reconnect next time */
			h = INVALID_HANDLE_VALUE;
			continue;
		}
		last_send = now;
		EnterCriticalSection(&pr_lock);
		if (pr_details == details && pr_state == state)
			pr_dirty = false;
		LeaveCriticalSection(&pr_lock);
	}
	return 0;
}

static bool pr_init(void)
{
	char path[MAX_PATH], *slash;
	const char *env;
	FILE *fp;
	int i, n = 0;

	if (pr_inited)
		return !pr_disabled;
	pr_inited = true;
	pr_disabled = true;
	pr_appid[0] = 0;
	if ((env = getenv("REVULTING_DISCORD_APP")) != NULL && *env) {
		for (i = 0; env[i] && n < 30; i++)
			if (env[i] >= '0' && env[i] <= '9')
				pr_appid[n++] = env[i];
	} else if (GetModuleFileNameA(NULL, path, sizeof path)) {
		if ((slash = strrchr(path, '\\')) != NULL)
			*slash = 0;
		strncat(path, "\\discord_app_id.txt", sizeof path - strlen(path) - 1);
		if ((fp = fopen(path, "r")) != NULL) {
			int c;

			while ((c = fgetc(fp)) != EOF && n < 30) {
				if (c >= '0' && c <= '9')
					pr_appid[n++] = (char) c;
				else if (n > 0)
					break; /* first run of digits only */
			}
			fclose(fp);
		}
	}
	pr_appid[n] = 0;
	if (n < 10)
		return false; /* no (plausible) application ID: feature off */
	pr_start = (long long) time(NULL);
	InitializeCriticalSection(&pr_lock);
	pr_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!pr_wake || !CreateThread(NULL, 0, pr_worker, NULL, 0, NULL))
		return false;
	pr_disabled = false;
	return true;
}

/* the game hands over its current status; cheap, call it every turn */
extern "C" void vulture_presence(const char *details, const char *state)
{
	if (!pr_init())
		return;
	EnterCriticalSection(&pr_lock);
	if (pr_details != (details ? details : "") || pr_state != (state ? state : "")) {
		pr_details = details ? details : "";
		pr_state = state ? state : "";
		pr_dirty = true;
		SetEvent(pr_wake);
	}
	LeaveCriticalSection(&pr_lock);
}
