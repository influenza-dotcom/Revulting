"""s135: third synthesized SFX batch - the "!" sting when a sleeping enemy
wakes up and spots you.  Reuses the helpers from make_sfx.py."""
import os

_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_sfx.py')).read()
exec(_src.split('# ---- UI ----')[0])  # tone/noise/mix/save helpers only
random.seed(135)

save('spotted', mix(tone(660, 0.07, 1, shape='square', a=0.001, s=0.5, r=0.03),
                    tone(1320, 0.22, 1, shape='square', a=0.001, d=0.05, s=0.45, r=0.15, vib=0.01),
                    noise(0.05, 0.6, lp=0.9, hp=0.8, a=0.0005, s=0.3, r=0.03),
                    offsets=[0, 0.07, 0.07]), 0.32)
