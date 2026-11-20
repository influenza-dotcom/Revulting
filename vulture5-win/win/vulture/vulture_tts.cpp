/*
 * Vulture mod (IMPLEMENTATION_LOG s75): text-to-speech for spoken lines.
 *
 * Windows SAPI 5 (the voices installed with Windows - here "Microsoft
 * David" and "Microsoft Zira").  5.0.0's own windsound_verbal() is a stub,
 * so the core calls vulture_tts_speak() directly for verbalize()d lines
 * when OPTIONS=voices is on.  Speaking is asynchronous and queued; the
 * game never waits for it.  Gender picks the voice, pitch/rate give each
 * speaker its own sound (SAPI XML <pitch>/<rate>).
 */
#include <initguid.h>
#include <windows.h>
#include <sapi.h>
#include <stdio.h>
#include <string.h>


/*
 * IMPLEMENTATION_LOG s87: Piper neural voices.
 *
 * If <game folder>\piper\piper.exe and its voices are present, spoken lines
 * use Piper (rhasspy/piper, MIT; voices from rhasspy/piper-voices) instead
 * of SAPI.  One piper.exe per voice runs in the background with
 * --output_dir: each line written to its stdin becomes a WAV whose path it
 * prints.  A worker thread feeds the queue, loads each WAV, resamples it
 * for the speaker's pitch (small creatures higher, big ones lower), and
 * plays it on a reserved mixer channel - one line after another.  The game
 * never waits.  Any failure falls back to SAPI.
 */
#include <SDL.h>
#include <SDL_mixer.h>
#include <deque>
#include <string>
#ifdef PP_DEBUG
#define PPD(...) fprintf(stderr, __VA_ARGS__)
#else
#define PPD(...) ((void) 0)
#endif

struct pp_req {
	std::string text;
	int voice;          /* 0 male, 1 female, 2 gruff */
	double factor;      /* playback rate: >1 higher/faster */
};

static const char *const pp_voice_file[3] = {
	"en_US-ryan-medium.onnx", "en_US-amy-medium.onnx",
	"en_GB-northern_english_male-medium.onnx"
};
static int pp_state = 0;                 /* 0 untried, 1 ok, -1 unavailable */
static char pp_dir[MAX_PATH], pp_out[MAX_PATH];
static HANDLE pp_in[3], pp_outp[3];      /* child stdin (write), stdout (read) */
static bool pp_started[3];
static std::deque<pp_req> pp_queue;
static CRITICAL_SECTION pp_lock;
static HANDLE pp_wake;

static bool pp_start_voice(int v)
{
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	HANDLE in_r, in_w, out_r, out_w;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char cmd[2048];

	if (pp_started[v])
		return true;
	if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0))
		return false;
	SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = in_r;
	si.hStdOutput = out_w;
	/* a windowed game has no stderr to hand down: send piper's logging to NUL */
	si.hStdError = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
	                           OPEN_EXISTING, 0, NULL);
	_snprintf(cmd, sizeof cmd,
	          "\"%s\\piper.exe\" --model \"%s\\voices\\%s\" --output_dir \"%s\" --quiet",
	          pp_dir, pp_dir, pp_voice_file[v], pp_out);
	cmd[sizeof cmd - 1] = 0;
	if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
	                    pp_dir, &si, &pi)) {
		CloseHandle(in_r); CloseHandle(in_w); CloseHandle(out_r); CloseHandle(out_w);
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(in_r);
	CloseHandle(out_w);
	if (si.hStdError != INVALID_HANDLE_VALUE)
		CloseHandle(si.hStdError);
	pp_in[v] = in_w;
	pp_outp[v] = out_r;
	pp_started[v] = true;
	return true;
}

/* one line of the child's stdout (the WAV path) */
static bool pp_read_line(HANDLE h, std::string &line)
{
	char c;
	DWORD got;

	line.clear();
	for (;;) {
		if (!ReadFile(h, &c, 1, &got, NULL) || got == 0)
			return false;
		if (c == '\n')
			break;
		if (c != '\r')
			line += c;
		if (line.size() > 1000)
			return false;
	}
	return true;
}

/* resample an interleaved S16 buffer by `factor` (linear) */
static Uint8 *pp_resample(const Uint8 *src, Uint32 len, int chans, double factor,
                          Uint32 *outlen)
{
	const Sint16 *s = (const Sint16 *) src;
	Uint32 frames = len / (2 * chans), oframes, i;
	Sint16 *o;
	int c;

	if (factor <= 0.0)
		factor = 1.0;
	oframes = (Uint32) (frames / factor);
	o = (Sint16 *) malloc((size_t) oframes * chans * 2 + 4);
	if (!o)
		return NULL;
	for (i = 0; i < oframes; i++) {
		double pos = i * factor;
		Uint32 a = (Uint32) pos, b = a + 1 < frames ? a + 1 : a;
		double f = pos - a;

		for (c = 0; c < chans; c++)
			o[i * chans + c] = (Sint16) (s[a * chans + c] * (1.0 - f)
			                             + s[b * chans + c] * f);
	}
	*outlen = oframes * chans * 2;
	return (Uint8 *) o;
}

static DWORD WINAPI pp_worker(LPVOID)
{
	Mix_Chunk *last = NULL;
	Uint8 *lastbuf = NULL;

	for (;;) {
		pp_req r;
		std::string path, line;
		DWORD wrote;
		Mix_Chunk *wav;
		int freq, chans;
		Uint16 fmt;

		WaitForSingleObject(pp_wake, 500);
		EnterCriticalSection(&pp_lock);
		if (pp_queue.empty()) {
			LeaveCriticalSection(&pp_lock);
			continue;
		}
		r = pp_queue.front();
		pp_queue.pop_front();
		LeaveCriticalSection(&pp_lock);

		if (!pp_start_voice(r.voice)) {
			PPD("pp: start voice %d failed (%lu)\n", r.voice, GetLastError());
			continue;
		}
		line = r.text + "\n";
		if (!WriteFile(pp_in[r.voice], line.c_str(), (DWORD) line.size(), &wrote, NULL)
		    || !pp_read_line(pp_outp[r.voice], path)) {
			PPD("pp: write/read failed (%lu)\n", GetLastError());
			continue;
		}
		PPD("pp: wav %s\n", path.c_str());
		/* piper prints the path a moment before the file is complete:
		   retry for up to ~2 s */
		wav = NULL;
		for (int tries = 0; tries < 100 && !wav; tries++) {
			wav = Mix_LoadWAV(path.c_str());
			if (!wav)
				Sleep(20);
		}
		DeleteFileA(path.c_str());
		if (!wav || !Mix_QuerySpec(&freq, &fmt, &chans) || fmt != AUDIO_S16SYS) {
			PPD("pp: load/spec failed wav=%p fmt=%x %s\n", (void *) wav, fmt, Mix_GetError());
			if (wav)
				Mix_FreeChunk(wav);
			continue;
		}
		{
			Uint32 olen = 0;
			Uint8 *buf = pp_resample(wav->abuf, wav->alen, chans, r.factor, &olen);
			Mix_Chunk *ch;

			Mix_FreeChunk(wav);
			if (!buf)
				continue;
			ch = Mix_QuickLoad_RAW(buf, olen);
			if (!ch) {
				free(buf);
				continue;
			}
			/* one line after another on the reserved speech channel */
			while (Mix_Playing(0))
				Sleep(20);
			if (last) {
				Mix_FreeChunk(last); /* QuickLoad chunk: buffer is ours */
				free(lastbuf);
			}
			last = ch, lastbuf = buf;
			Mix_PlayChannel(0, ch, 0);
		}
	}
	return 0;
}

static bool pp_init(void)
{
	char exe[MAX_PATH], *slash;
	char probe[MAX_PATH + 64];
	const char *tmp = getenv("TEMP");
	int v;

	if (pp_state)
		return pp_state > 0;
	pp_state = -1;
	if (!GetModuleFileNameA(NULL, exe, sizeof exe))
		return false;
	if ((slash = strrchr(exe, '\\')) != NULL)
		*slash = 0;
	_snprintf(pp_dir, sizeof pp_dir, "%s\\piper", exe);
	pp_dir[sizeof pp_dir - 1] = 0;
	_snprintf(probe, sizeof probe, "%s\\piper.exe", pp_dir);
	if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) {
		PPD("pp: no %s\n", probe);
		return false;
	}
	for (v = 0; v < 3; v++) {
		_snprintf(probe, sizeof probe, "%s\\voices\\%s", pp_dir, pp_voice_file[v]);
		if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES)
			return false;
	}
	_snprintf(pp_out, sizeof pp_out, "%s\\revulting_tts", tmp ? tmp : exe);
	pp_out[sizeof pp_out - 1] = 0;
	CreateDirectoryA(pp_out, NULL);
	{
		int freq, chans;
		Uint16 fmt;

		if (!Mix_QuerySpec(&freq, &fmt, &chans))
			return false; /* no audio device: nothing to speak with */
	}
	Mix_ReserveChannels(1); /* channel 0 is speech from now on */
	InitializeCriticalSection(&pp_lock);
	pp_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!pp_wake || !CreateThread(NULL, 0, pp_worker, NULL, 0, NULL))
		return false;
	pp_state = 1;
	return true;
}

/* queue a line; false if Piper isn't available (caller falls back) */
static bool pp_speak(const char *text, int female, int pitch)
{
	pp_req r;

	if (!pp_init())
		return false;
	r.text = text;
	for (size_t i = 0; i < r.text.size(); i++)
		if (r.text[i] == '\n' || r.text[i] == '\r')
			r.text[i] = ' ';
	r.voice = pitch <= -6 ? 2 : (female ? 1 : 0);
	r.factor = 1.0 + pitch * 0.025;          /* -10..10 -> 0.75..1.25 */
	if (r.voice == 2 && r.factor < 0.85)
		r.factor = 0.85;                      /* the gruff voice is low already */
	EnterCriticalSection(&pp_lock);
	if (pp_queue.size() < 6)                  /* don't build a backlog */
		pp_queue.push_back(r);
	LeaveCriticalSection(&pp_lock);
	SetEvent(pp_wake);
	return true;
}

static ISpVoice *tts_voice = NULL;
static ISpObjectToken *tts_tok[2] = { NULL, NULL }; /* [0] male, [1] female */
static int tts_state = 0; /* 0 untried, 1 ready, -1 unavailable */

static ISpObjectToken *tts_find(const wchar_t *attrs)
{
	ISpObjectTokenCategory *cat = NULL;
	IEnumSpObjectTokens *en = NULL;
	ISpObjectToken *tok = NULL;

	if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
	                            IID_ISpObjectTokenCategory, (void **) &cat)))
		return NULL;
	if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))
	    && SUCCEEDED(cat->EnumTokens(attrs, NULL, &en)) && en) {
		ULONG got = 0;

		if (FAILED(en->Next(1, &tok, &got)) || got != 1)
			tok = NULL;
		en->Release();
	}
	cat->Release();
	return tok;
}

static int tts_init(void)
{
	if (tts_state)
		return tts_state > 0;
	tts_state = -1;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); /* S_FALSE if already */
	if (FAILED(CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice,
	                            (void **) &tts_voice)) || !tts_voice)
		return 0;
	tts_tok[0] = tts_find(L"Gender=Male");
	tts_tok[1] = tts_find(L"Gender=Female");
	tts_state = 1;
	return 1;
}

/* text: plain ASCII; pitch -10..10, rate -10..10 */
extern "C" void vulture_tts_speak(const char *text, int female, int pitch,
                                  int rate)
{
	wchar_t buf[2048];
	int n, i;

	if (!text || !*text)
		return;
	if (pp_speak(text, female, pitch)) /* s87: Piper first */
		return;
	if (!tts_init())
		return;
	if (tts_tok[female ? 1 : 0])
		tts_voice->SetVoice(tts_tok[female ? 1 : 0]);
	if (pitch < -10) pitch = -10;
	if (pitch > 10) pitch = 10;
	if (rate < -10) rate = -10;
	if (rate > 10) rate = 10;
	n = swprintf(buf, 200, L"<pitch absmiddle=\"%d\"><rate absspeed=\"%d\">",
	             pitch, rate);
	if (n < 0)
		return;
	for (i = 0; text[i] && n < 1900; i++) {
		unsigned char c = (unsigned char) text[i];

		if (c == '&')
			n += swprintf(buf + n, 8, L"&amp;");
		else if (c == '<')
			n += swprintf(buf + n, 8, L"&lt;");
		else if (c == '>')
			n += swprintf(buf + n, 8, L"&gt;");
		else if (c >= 32 && c < 127)
			buf[n++] = (wchar_t) c;
	}
	buf[n] = 0;
	wcscat(buf, L"</rate></pitch>");
	tts_voice->Speak(buf, SPF_ASYNC | SPF_IS_XML, NULL);
}

/* stop talking (level change, quit) */
extern "C" void vulture_tts_stop(void)
{
	if (pp_state > 0) { /* s87 */
		EnterCriticalSection(&pp_lock);
		pp_queue.clear();
		LeaveCriticalSection(&pp_lock);
		Mix_HaltChannel(0);
	}
	if (tts_state > 0)
		tts_voice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
}
