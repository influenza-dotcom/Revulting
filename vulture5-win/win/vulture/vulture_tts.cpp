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

	if (!text || !*text || !tts_init())
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
	if (tts_state > 0)
		tts_voice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
}
