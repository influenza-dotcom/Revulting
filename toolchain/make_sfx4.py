"""s136: fourth synthesized SFX batch - distant dungeon ambience, the search
pulse, and message sounds (eating, skills, intrinsics, special rooms, webs,
engraving).  Reuses the helpers from make_sfx.py."""
import os

_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_sfx.py')).read()
exec(_src.split('# ---- UI ----')[0])  # tone/noise/mix/save helpers only
random.seed(136)


def echo(x, delay=0.11, fb=0.45, taps=5):
    """cheap cave reverb: decaying repeats"""
    out = list(x) + [0.0] * n(delay * taps)
    for k in range(1, taps + 1):
        g = fb ** k
        s0 = n(delay * k)
        for i, v in enumerate(x):
            out[s0 + i] += v * g
    return out


# ---- faraway dungeon sounds (played quietly from a random direction) ----
save('far_drip', echo(mix(tone(1900, 0.05, 1, f_end=900, a=0.001, s=0.3, r=0.03),
                          tone(1500, 0.04, 0.5, f_end=800, a=0.001, s=0.3, r=0.03), offsets=[0, 0.5]),
                      0.13, 0.5, 6), 0.35)
save('far_creak', echo(tone(140, 0.7, 1, f_end=210, shape='saw', a=0.08, s=0.5, r=0.25, vib=0.04),
                       0.15, 0.35, 4), 0.3)
save('far_chain', echo(mix(*[tone(2200 + 500 * (k % 3), 0.07, 0.7, a=0.001, s=0.3, r=0.05)
                             for k in range(6)],
                           *[noise(0.05, 0.5, lp=0.9, hp=0.7, a=0.001, s=0.3, r=0.03) for k in range(6)],
                           offsets=[0.09 * k + random.uniform(0, 0.03) for k in range(6)] * 2),
                       0.12, 0.4, 4), 0.28)
save('far_rumble', mix(noise(2.2, 1, lp=0.008, lp_end=0.02, a=0.6, s=0.7, r=1.0),
                       tone(42, 2.2, 0.6, f_end=36, a=0.6, s=0.6, r=1.0)), 0.5)
save('far_howl', echo(tone(330, 1.6, 1, f_end=520, shape='tri', a=0.3, d=0.2, s=0.7, r=0.7, vib=0.02),
                      0.18, 0.4, 4), 0.25)
save('far_moan', echo(mix(tone(110, 1.8, 1, f_end=90, shape='saw', a=0.4, s=0.6, r=0.8, vib=0.03),
                          tone(165, 1.8, 0.5, f_end=130, shape='tri', a=0.5, s=0.5, r=0.8, vib=0.04)),
                      0.2, 0.35, 3), 0.28)

# ---- search pulse ----
save('search_ping', echo(mix(tone(1250, 0.25, 1, f_end=1150, a=0.002, d=0.05, s=0.3, r=0.18),
                             tone(1875, 0.2, 0.3, a=0.002, s=0.2, r=0.15)), 0.09, 0.35, 3), 0.18)

# ---- message sounds ----
save('eat_chew', mix(*[noise(0.09, 1, lp=0.3, hp=0.2, a=0.005, d=0.03, s=0.5, r=0.04) for k in range(3)],
                     offsets=[0, 0.17, 0.33]), 0.32)
save('skill_up', mix(tone(659, 0.15, 1, s=0.5, r=0.1), tone(880, 0.15, 1, s=0.5, r=0.1),
                     tone(1319, 0.4, 0.9, s=0.45, r=0.3, vib=0.006), offsets=[0, 0.1, 0.2]), 0.35)
save('gain', mix(*[tone(784 * (1.5 ** (k % 3)) * (2 if k > 2 else 1), 0.5, 0.6, a=0.01, s=0.4, r=0.35)
                   for k in range(5)],
                 noise(0.8, 0.3, lp=0.9, hp=0.85, a=0.2, s=0.3, r=0.4),
                 offsets=[0.06 * k for k in range(5)] + [0]), 0.32)
save('room_sting', mix(tone(98, 1.6, 1, shape='saw', a=0.02, s=0.6, r=1.0),
                       tone(147, 1.6, 0.7, shape='saw', a=0.02, s=0.6, r=1.0),
                       tone(208, 1.6, 0.5, shape='tri', a=0.02, s=0.5, r=1.0, vib=0.01),
                       noise(0.3, 0.6, lp=0.1, a=0.002, s=0.4, r=0.25)), 0.4)
save('web', mix(noise(0.35, 1, lp=0.6, hp=0.5, a=0.01, s=0.5, r=0.2),
                tone(400, 0.3, 0.3, f_end=250, shape='tri', s=0.4, r=0.15, vib=0.08)), 0.28)
save('scribble', mix(*[noise(0.07, 1, lp=0.8, hp=0.75, a=0.005, s=0.5, r=0.03) for k in range(5)],
                     offsets=[0.1 * k + random.uniform(0, 0.03) for k in range(5)]), 0.25)
