/*
 * Vulture mod (IMPLEMENTATION_LOG §69): local-LLM monster chat transport.
 *
 * A tiny blocking HTTP/1.0 POST to a local Ollama server (127.0.0.1 only -
 * nothing ever leaves the machine).  Kept free of NetHack headers so
 * winsock and NetHack's macros never meet; the prompt and the game state
 * live in src/sounds.c.  A warm-up thread loads the model when a game
 * starts, so the first #chat doesn't pay the model-load time.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int llm_wsa_ready = 0;

static int llm_wsa(void)
{
	if (!llm_wsa_ready) {
		WSADATA wd;
		if (WSAStartup(MAKEWORD(2, 2), &wd) != 0)
			return 0;
		llm_wsa_ready = 1;
	}
	return 1;
}

/* POST body to http://127.0.0.1:port/path.  Copies the response BODY into
   out (NUL-terminated, truncated to outlen-1).  Returns 1 on HTTP 200. */
extern "C" int vulture_llm_post(int port, const char *path, const char *body,
                                char *out, int outlen, int timeout_ms)
{
	SOCKET s;
	struct sockaddr_in addr;
	char hdr[512];
	char *buf;
	int cap = 65536, len = 0, n, ok = 0;
	DWORD tmo = (DWORD) timeout_ms;

	if (outlen > 0)
		out[0] = '\0';
	if (!llm_wsa())
		return 0;
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET)
		return 0;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tmo, sizeof tmo);
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *) &tmo, sizeof tmo);
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short) port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* a server that isn't running refuses at once on loopback */
	if (connect(s, (struct sockaddr *) &addr, sizeof addr) != 0) {
		closesocket(s);
		return 0;
	}
	_snprintf(hdr, sizeof hdr,
	          "POST %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\n"
	          "Content-Type: application/json\r\nContent-Length: %d\r\n\r\n",
	          path, port, (int) strlen(body));
	hdr[sizeof hdr - 1] = '\0';
	if (send(s, hdr, (int) strlen(hdr), 0) <= 0
	    || send(s, body, (int) strlen(body), 0) <= 0) {
		closesocket(s);
		return 0;
	}
	buf = (char *) malloc(cap);
	if (!buf) {
		closesocket(s);
		return 0;
	}
	while ((n = recv(s, buf + len, cap - 1 - len, 0)) > 0) {
		len += n;
		if (len >= cap - 1)
			break;
	}
	closesocket(s);
	buf[len] = '\0';
#ifdef LLM_DEBUG
	fprintf(stderr, "[llm] %d bytes, WSA %d:\n%.400s\n", len, WSAGetLastError(), buf);
#endif
	if (len > 12 && !strncmp(buf, "HTTP/1.", 7) && !strncmp(buf + 9, "200", 3)) {
		char *b = strstr(buf, "\r\n\r\n");
		if (b && outlen > 0) {
			b += 4;
			strncpy(out, b, outlen - 1);
			out[outlen - 1] = '\0';
			ok = 1;
		}
	}
	free(buf);
	return ok;
}

static char llm_warm_body[512];

static DWORD WINAPI llm_warm_thread(LPVOID)
{
	char out[1024];
	vulture_llm_post(11434, "/api/generate", llm_warm_body, out, sizeof out,
	                 120000);
	return 0;
}

/* load the model in the background (keep_alive keeps it resident) */
extern "C" void vulture_llm_warmup(const char *model)
{
	HANDLE h;
	_snprintf(llm_warm_body, sizeof llm_warm_body,
	          "{\"model\":\"%s\",\"prompt\":\"\",\"keep_alive\":\"30m\"}",
	          model);
	llm_warm_body[sizeof llm_warm_body - 1] = '\0';
	h = CreateThread(NULL, 0, llm_warm_thread, NULL, 0, NULL);
	if (h)
		CloseHandle(h);
}
