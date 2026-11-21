"""s119: death screams, voiced with Piper (LibriTTS speakers), then shaped:
trimmed, faded out, normalised; creature cries pitched up (small) or down
(big) by resampling.  Writes 22050 Hz mono 16-bit WAVs into sound/."""
import json, os, struct, subprocess, wave, array, math

PIPER = r'C:\Users\dalla\Vulture\VultureNetHack\piper'
MODEL = PIPER + r'\voices\en_US-libritts_r-medium.onnx'
SOUND = r'C:\Users\dalla\Vulture\VultureNetHack\sound'
TMP = os.path.join(os.environ.get('TEMP', '.'), 'revulting_screams_tmp')
os.makedirs(TMP, exist_ok=True)

# (output name, speaker, text, length_scale, pitch factor)
JOBS = [
    ('scream_m1', 380, 'Aaaaaargh!', 1.6, 1.0),
    ('scream_m2', 90, 'Nooooo!', 1.7, 1.0),
    ('scream_m3', 500, 'Gaaah... urgh.', 1.4, 1.0),
    ('scream_m4', 50, 'Aaagh!', 1.5, 0.95),
    ('scream_f1', 260, 'Aaaaaah!', 1.6, 1.0),
    ('scream_f2', 0, 'Nooooo!', 1.7, 1.0),
    ('scream_f3', 80, 'Aiieeee!', 1.5, 1.0),
    ('scream_f4', 440, 'Ahhh... ugh.', 1.4, 1.0),
    ('scream_s1', 110, 'Eeeeek!', 1.3, 1.45),
    ('scream_s2', 30, 'Yiiip!', 1.2, 1.55),
    ('scream_s3', 70, 'Skreeee!', 1.3, 1.4),
    ('scream_l1', 380, 'Rrraaaaawr!', 1.8, 0.62),
    ('scream_l2', 400, 'Hrrooooaaargh!', 1.8, 0.58),
    ('scream_l3', 50, 'Graaaaaagh!', 1.7, 0.66),
]


def piper(text, speaker, ls, out):
    cfg = {'text': text, 'speaker_id': speaker}
    p = subprocess.run([PIPER + r'\piper.exe', '--model', MODEL, '--json-input',
                        '--length_scale', str(ls), '--output_file', out, '--quiet'],
                       cwd=PIPER, input=json.dumps(cfg) + '\n', text=True,
                       capture_output=True, timeout=120)
    if p.returncode or not os.path.exists(out):
        raise SystemExit('piper failed: ' + p.stderr[-400:])


def read(path):
    w = wave.open(path, 'rb')
    sr, n = w.getframerate(), w.getnframes()
    x = array.array('h', w.readframes(n))
    w.close()
    return sr, [v / 32768.0 for v in x]


def resample(x, ratio):
    """ratio > 1 = shorter + higher"""
    out, i = [], 0.0
    while i < len(x) - 1:
        k = int(i)
        f = i - k
        out.append(x[k] * (1 - f) + x[k + 1] * f)
        i += ratio
    return out


def shape(x, sr):
    # trim silence
    thr = 0.02
    a = next((i for i, v in enumerate(x) if abs(v) > thr), 0)
    b = len(x) - next((i for i, v in enumerate(reversed(x)) if abs(v) > thr), 0)
    x = x[max(0, a - int(0.01 * sr)):min(len(x), b + int(0.05 * sr))]
    # gentle saturation for grit, normalise, fade out the last 25%
    x = [math.tanh(v * 2.2) for v in x]
    peak = max(abs(v) for v in x) or 1.0
    x = [v / peak * 0.92 for v in x]
    x = x[:int(1.6 * sr)]
    n = len(x)
    fo = int(n * 0.25)
    for i in range(fo):
        x[n - fo + i] *= 1.0 - i / fo
    return x


def write(path, x, sr):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(sr)
    w.writeframes(array.array('h', [int(max(-1, min(1, v)) * 32767) for v in x]).tobytes())
    w.close()


for name, spk, text, ls, pitch in JOBS:
    raw = os.path.join(TMP, name + '_raw.wav')
    piper(text, spk, ls, raw)
    sr, x = read(raw)
    if pitch != 1.0:
        x = resample(x, pitch)
    if sr != 22050:
        x = resample(x, sr / 22050.0)
    x = shape(x, 22050)
    write(os.path.join(SOUND, name + '.wav'), x, 22050)
    print(name, round(len(x) / 22050.0, 2), 's')
