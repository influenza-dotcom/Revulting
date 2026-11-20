/*
 * Vulture mod (IMPLEMENTATION_LOG s76): Discord for the mail daemon.
 *
 * Messages posted in one Discord channel arrive in the game as scrolls of
 * mail, delivered by the mail daemon; reading one lets you reply.
 *
 * Setup (done by the player, never by the game): create a Discord bot,
 * enable its "Message Content" intent, invite it to your server with
 * View Channel + Read Message History + Send Messages, and write
 *     %USERPROFILE%\NetHack\discord.txt
 * containing
 *     token=<bot token>
 *     channel=<channel id>
 *     poll=20          (optional, seconds, minimum 10)
 * The token stays in that file; it is only ever sent to discord.com.
 *
 * Transport: WinHTTP (HTTPS, built into Windows) on a background thread
 * that polls GET /channels/{id}/messages?after={last}.  The first poll
 * only records where the channel is, so old history isn't delivered.
 * Messages written by bots (including our own replies) are skipped.
 */
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tiny JSON reader (enough for Discord message objects) ---- */
struct jv {
	char t;                       /* o a s n (n = number/bool/null raw) */
	std::string s;
	std::vector<jv> a;
	std::map<std::string, jv> o;
};

static void jws(const char *&p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; }

static std::string jstr(const char *&p)
{
	std::string r;

	p++; /* opening quote */
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n': r += '\n'; break;
			case 't': r += ' '; break;
			case 'r': break;
			case 'u': {
				unsigned v = (unsigned) strtoul(std::string(p + 1, 4).c_str(), NULL, 16);
				p += 4;
				if (v == 0x2019 || v == 0x2018) r += '\'';
				else if (v == 0x201c || v == 0x201d) r += '"';
				else if (v == 0x2014 || v == 0x2013) r += '-';
				else if (v >= 32 && v < 127) r += (char) v;
				else if (v >= 0xd800 && v <= 0xdbff) { if (p[1] == '\\' && p[2] == 'u') p += 6; r += '?'; }
				else r += '?';
				break;
			}
			default: r += *p; break;
			}
			p++;
		} else {
			unsigned char c = (unsigned char) *p;

			if (c >= 0x80) {          /* UTF-8 sequence -> one '?' */
				int n = (c >= 0xf0) ? 4 : (c >= 0xe0) ? 3 : 2;
				for (int i = 0; i < n && *p; i++) p++;
				r += '?';
				continue;
			}
			r += *p++;
		}
	}
	if (*p == '"') p++;
	return r;
}

static jv jparse(const char *&p, int depth = 0)
{
	jv v;

	jws(p);
	if (depth > 40) { v.t = 'n'; return v; }
	if (*p == '{') {
		v.t = 'o';
		p++;
		jws(p);
		while (*p && *p != '}') {
			jws(p);
			if (*p != '"') break;
			std::string k = jstr(p);
			jws(p);
			if (*p == ':') p++;
			v.o[k] = jparse(p, depth + 1);
			jws(p);
			if (*p == ',') p++;
		}
		if (*p == '}') p++;
	} else if (*p == '[') {
		v.t = 'a';
		p++;
		jws(p);
		while (*p && *p != ']') {
			v.a.push_back(jparse(p, depth + 1));
			jws(p);
			if (*p == ',') p++;
			jws(p);
		}
		if (*p == ']') p++;
	} else if (*p == '"') {
		v.t = 's';
		v.s = jstr(p);
	} else {
		v.t = 'n';
		while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n')
			v.s += *p++;
	}
	return v;
}

/* ---- config + state ---- */
struct dmsg { std::string id, author, text; };

static std::string dc_token, dc_channel, dc_last;
static int dc_poll = 20;
static int dc_state = 0;               /* 0 untried, 1 running, -1 off */
static CRITICAL_SECTION dc_lock;
static std::deque<dmsg> dc_queue;

static bool dc_read_config(void)
{
	char path[MAX_PATH], line[512];
	const char *home = getenv("USERPROFILE");
	FILE *f;

	if (!home)
		return false;
	_snprintf(path, sizeof path, "%s\\NetHack\\discord.txt", home);
	path[sizeof path - 1] = 0;
	if (!(f = fopen(path, "r")))
		return false;
	while (fgets(line, sizeof line, f)) {
		char *e = line + strlen(line);

		while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
			*--e = 0;
		if (!strncmp(line, "token=", 6)) dc_token = line + 6;
		else if (!strncmp(line, "channel=", 8)) dc_channel = line + 8;
		else if (!strncmp(line, "poll=", 5)) dc_poll = atoi(line + 5);
	}
	fclose(f);
	if (dc_poll < 10) dc_poll = 10;
	return !dc_token.empty() && !dc_channel.empty();
}

/* one HTTPS request to discord.com; returns HTTP status (0 on failure) */
static int dc_http(const wchar_t *verb, const std::string &path,
                   const std::string &body, std::string &out)
{
	HINTERNET ses = NULL, con = NULL, req = NULL;
	std::wstring wpath(path.begin(), path.end());
	std::string auth = "Authorization: Bot " + dc_token
	                   + "\r\nContent-Type: application/json\r\n";
	std::wstring wauth(auth.begin(), auth.end());
	DWORD status = 0, sz = sizeof status, avail, got;
	int ok = 0;

	out.clear();
	ses = WinHttpOpen(L"DiscordBot (https://www.nethack.org, 1.0)",
	                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
	                  WINHTTP_NO_PROXY_BYPASS, 0);
	if (!ses) goto done;
	WinHttpSetTimeouts(ses, 5000, 5000, 10000, 10000);
	con = WinHttpConnect(ses, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!con) goto done;
	req = WinHttpOpenRequest(con, verb, wpath.c_str(), NULL, WINHTTP_NO_REFERER,
	                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!req) goto done;
	if (!WinHttpSendRequest(req, wauth.c_str(), (DWORD) -1L,
	                        (LPVOID) (body.empty() ? NULL : body.c_str()),
	                        (DWORD) body.size(), (DWORD) body.size(), 0))
		goto done;
	if (!WinHttpReceiveResponse(req, NULL)) goto done;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
	while (WinHttpQueryDataAvailable(req, &avail) && avail) {
		std::vector<char> buf(avail);

		if (!WinHttpReadData(req, &buf[0], avail, &got) || !got) break;
		out.append(&buf[0], got);
		if (out.size() > 1000000) break;
	}
	ok = (int) status;
done:
	if (req) WinHttpCloseHandle(req);
	if (con) WinHttpCloseHandle(con);
	if (ses) WinHttpCloseHandle(ses);
	return ok;
}

static void dc_poll_once(bool first)
{
	std::string path = "/api/v10/channels/" + dc_channel + "/messages?limit="
	                   + (first ? "1" : "20");
	std::string out;

	if (!first && !dc_last.empty())
		path += "&after=" + dc_last;
	if (dc_http(L"GET", path, "", out) != 200)
		return;
	const char *p = out.c_str();
	jv arr = jparse(p);

	if (arr.t != 'a')
		return;
	/* Discord returns newest first: walk backwards for oldest-first */
	for (int i = (int) arr.a.size() - 1; i >= 0; i--) {
		jv &m = arr.a[i];

		if (m.t != 'o' || !m.o.count("id"))
			continue;
		std::string id = m.o["id"].s;

		if (dc_last.empty() || id.size() > dc_last.size()
		    || (id.size() == dc_last.size() && id > dc_last))
			dc_last = id;
		if (first)
			continue;
		jv &a = m.o["author"];
		if (a.t == 'o' && a.o.count("bot") && a.o["bot"].s == "true")
			continue;
		dmsg d;
		d.id = id;
		d.text = m.o.count("content") ? m.o["content"].s : "";
		if (a.t == 'o') {
			if (a.o.count("global_name") && a.o["global_name"].t == 's'
			    && !a.o["global_name"].s.empty())
				d.author = a.o["global_name"].s;
			else if (a.o.count("username"))
				d.author = a.o["username"].s;
		}
		if (d.author.empty()) d.author = "someone";
		if (d.text.empty()) d.text = "(an attachment you cannot read here)";
		if (d.text.size() > 1500) d.text.resize(1500);
		EnterCriticalSection(&dc_lock);
		dc_queue.push_back(d);
		LeaveCriticalSection(&dc_lock);
	}
}

static DWORD WINAPI dc_thread(LPVOID)
{
	bool first = true;

	for (;;) {
		dc_poll_once(first);
		if (!dc_last.empty())
			first = false;
		Sleep((DWORD) dc_poll * 1000);
	}
	return 0;
}

/* starts the poller the first time; 1 if Discord mail is configured */
extern "C" int vulture_discord_enabled(void)
{
	if (dc_state == 0) {
		dc_state = -1;
		if (dc_read_config()) {
			HANDLE h;

			InitializeCriticalSection(&dc_lock);
			h = CreateThread(NULL, 0, dc_thread, NULL, 0, NULL);
			if (h) {
				CloseHandle(h);
				dc_state = 1;
			}
		}
	}
	return dc_state > 0;
}

/* next delivered message, oldest first; 1 if one was returned */
extern "C" int vulture_discord_pop(char *author, int alen, char *text, int tlen,
                                   char *msgid, int mlen)
{
	dmsg d;

	if (dc_state <= 0)
		return 0;
	EnterCriticalSection(&dc_lock);
	if (dc_queue.empty()) {
		LeaveCriticalSection(&dc_lock);
		return 0;
	}
	d = dc_queue.front();
	dc_queue.pop_front();
	LeaveCriticalSection(&dc_lock);
	_snprintf(author, alen, "%s", d.author.c_str()); author[alen - 1] = 0;
	_snprintf(text, tlen, "%s", d.text.c_str()); text[tlen - 1] = 0;
	_snprintf(msgid, mlen, "%s", d.id.c_str()); msgid[mlen - 1] = 0;
	return 1;
}

/* post a reply (as a Discord reply to msgid when given); 1 on success */
extern "C" int vulture_discord_reply(const char *msgid, const char *text)
{
	std::string body = "{\"content\":\"", out;

	if (dc_state <= 0)
		return 0;
	for (const char *p = text; *p; p++) {
		unsigned char c = (unsigned char) *p;

		if (c == '"' || c == '\\') { body += '\\'; body += (char) c; }
		else if (c >= 32 && c < 127) body += (char) c;
	}
	body += "\"";
	if (msgid && *msgid)
		body += std::string(",\"message_reference\":{\"message_id\":\"") + msgid
		        + "\",\"fail_if_not_exists\":false}";
	body += "}";
	int st = dc_http(L"POST", "/api/v10/channels/" + dc_channel + "/messages",
	                 body, out);
	return st >= 200 && st < 300;
}
