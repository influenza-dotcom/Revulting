"""s137: stealth / sleep feedback sounds - the silent kill, the backstab, a
soft cue for spotting a sleeper, and the "?" of a monster woken by noise.
Reuses the helpers from make_sfx.py."""
import os

_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_sfx.py')).read()
exec(_src.split('# ---- UI ----')[0])  # tone/noise/mix/save helpers only
random.seed(137)

# a dark blade "shing", a low heartbeat thump, and a hushed shimmer tail
save('silent_kill', mix(noise(0.25, 1, lp=0.95, hp=0.85, a=0.001, d=0.04, s=0.5, r=0.18),
                        tone(3200, 0.5, 0.35, f_end=2900, a=0.001, s=0.3, r=0.4, vib=0.004),
                        tone(55, 0.35, 1, f_end=38, a=0.002, d=0.08, s=0.5, r=0.2),
                        *[tone(880 * (1.5 ** k), 0.9, 0.18, a=0.08, s=0.4, r=0.6) for k in range(3)],
                        offsets=[0, 0, 0.05, 0.18, 0.24, 0.3]), 0.5)
save('backstab', mix(noise(0.12, 1, lp=0.9, hp=0.7, a=0.001, s=0.4, r=0.08),
                     tone(140, 0.22, 0.9, f_end=80, a=0.001, d=0.05, s=0.4, r=0.12),
                     tone(2400, 0.25, 0.3, f_end=2100, a=0.001, s=0.3, r=0.2)), 0.5)
save('sleeper_spot', mix(noise(0.5, 0.6, lp=0.04, a=0.15, s=0.5, r=0.3),
                         tone(587, 0.6, 0.35, a=0.12, s=0.4, r=0.35),
                         tone(880, 0.6, 0.2, a=0.18, s=0.35, r=0.35)), 0.2)
save('wake_q', mix(tone(520, 0.1, 1, shape='tri', a=0.002, s=0.5, r=0.05),
                   tone(700, 0.2, 1, f_end=820, shape='tri', a=0.002, s=0.5, r=0.12),
                   offsets=[0, 0.1]), 0.28)
