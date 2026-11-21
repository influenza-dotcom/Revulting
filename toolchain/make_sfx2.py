"""s132: second synthesized SFX batch - swings, spell schools, traps, breakage,
pick-up by item class, fizzle.  Reuses the helpers from make_sfx.py."""
import os

_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_sfx.py')).read()
exec(_src.split('# ---- UI ----')[0])  # tone/noise/mix/save helpers only
random.seed(132)

# ---- melee swings (played on every lunge) ----
for k, (lp0, lp1, f0) in enumerate([(0.05, 0.55, 700), (0.08, 0.7, 520), (0.04, 0.45, 880)]):
    save('swing%d' % (k + 1), mix(noise(0.2, 1, lp=lp0, lp_end=lp1, hp=0.35, a=0.02, d=0.05, s=0.7, r=0.08),
                                  tone(f0, 0.18, 0.15, f_end=f0 * 0.6, s=0.4, r=0.08)), 0.32)

# ---- spell schools ----
save('cast_attack', mix(noise(0.45, 1, lp=0.1, lp_end=0.8, a=0.03, s=0.7, r=0.2),
                        tone(220, 0.45, 0.6, f_end=660, shape='saw', s=0.5, r=0.2),
                        noise(0.25, 0.6, lp=0.9, hp=0.7, a=0.002, s=0.4, r=0.15), offsets=[0, 0, 0.2]), 0.45)
save('cast_heal', mix(*[tone(523 * (1.26 ** k), 0.5 - 0.05 * k, 0.7, s=0.5, r=0.3, vib=0.004) for k in range(4)],
                      offsets=[0, 0.07, 0.14, 0.21]), 0.35)
save('cast_div', mix(tone(1318, 0.8, 1, a=0.002, d=0.1, s=0.3, r=0.6),
                     tone(1975, 0.7, 0.5, a=0.002, d=0.1, s=0.25, r=0.5),
                     tone(2637, 0.6, 0.3, a=0.002, s=0.2, r=0.4)), 0.32)
save('cast_ench', mix(*[tone(1600 + 250 * ((k * 7) % 5), 0.12, 0.6, a=0.001, s=0.3, r=0.1) for k in range(7)],
                      offsets=[0.05 * k for k in range(7)]), 0.3)
save('cast_cler', mix(tone(392, 0.9, 1, s=0.45, r=0.6), tone(494, 0.9, 0.8, s=0.45, r=0.6),
                      tone(587, 0.9, 0.7, s=0.45, r=0.6), tone(784, 0.9, 0.4, s=0.4, r=0.6)), 0.3)
save('cast_escape', mix(noise(0.4, 1, lp=0.6, lp_end=0.03, hp=0.2, a=0.01, s=0.6, r=0.2),
                        tone(1400, 0.35, 0.4, f_end=300, s=0.4, r=0.2)), 0.38)
save('cast_matter', mix(tone(70, 0.5, 1, f_end=50, a=0.01, s=0.6, r=0.25),
                        noise(0.5, 0.8, lp=0.04, a=0.02, s=0.6, r=0.25),
                        noise(0.15, 0.5, lp=0.5, a=0.001, s=0.3, r=0.1), offsets=[0, 0, 0.3]), 0.5)
save('fizzle', mix(noise(0.35, 1, lp=0.9, hp=0.8, a=0.005, d=0.05, s=0.4, r=0.25),
                   tone(600, 0.3, 0.3, f_end=150, s=0.3, r=0.2)), 0.28)

# ---- traps / breakage ----
save('squeak', mix(tone(1800, 0.18, 1, f_end=2400, shape='tri', a=0.005, s=0.6, r=0.06, vib=0.03),
                   tone(1500, 0.14, 0.6, f_end=1900, shape='tri', s=0.5, r=0.05), offsets=[0, 0.12]), 0.35)
save('snap', mix(noise(0.06, 1, lp=0.95, hp=0.6, a=0.0005, d=0.01, s=0.3, r=0.04),
                 tone(1200, 0.12, 0.6, f_end=500, shape='square', a=0.0005, s=0.3, r=0.08),
                 noise(0.18, 0.5, lp=0.3, s=0.3, r=0.12), offsets=[0, 0, 0.03]), 0.6)
save('teleport', mix(tone(300, 0.6, 0.8, f_end=2400, shape='tri', a=0.05, s=0.6, r=0.2, vib=0.02),
                     noise(0.6, 0.5, lp=0.05, lp_end=0.6, hp=0.5, a=0.05, s=0.5, r=0.2)), 0.35)
save('dart_thunk', mix(noise(0.05, 1, lp=0.4, a=0.0005, s=0.3, r=0.03),
                       tone(160, 0.1, 0.7, f_end=110, a=0.0005, s=0.3, r=0.07)), 0.5)
save('glass', mix(*[tone(2500 + 900 * ((k * 3) % 7), 0.25 - 0.02 * k, 0.5, a=0.0005, s=0.25, r=0.18)
                    for k in range(8)],
                  noise(0.3, 1, lp=0.95, hp=0.8, a=0.0005, d=0.03, s=0.3, r=0.2),
                  offsets=[0.012 * k for k in range(8)] + [0]), 0.45)

# ---- picking things up, by kind ----
save('pk_potion', mix(tone(2300, 0.12, 1, a=0.0005, s=0.3, r=0.1), tone(3100, 0.1, 0.6, a=0.0005, s=0.3, r=0.08),
                      offsets=[0, 0.05]), 0.3)
save('pk_scroll', noise(0.16, 1, lp=0.5, hp=0.7, a=0.01, d=0.03, s=0.5, r=0.08), 0.28)
save('pk_weapon', mix(noise(0.08, 1, lp=0.9, hp=0.7, a=0.0005, s=0.3, r=0.06),
                      tone(1900, 0.22, 0.4, a=0.0005, s=0.25, r=0.18), tone(2850, 0.18, 0.25, s=0.2, r=0.15)), 0.32)
save('pk_armor', mix(noise(0.12, 1, lp=0.5, a=0.002, s=0.4, r=0.08), tone(420, 0.12, 0.5, s=0.3, r=0.08),
                     noise(0.1, 0.8, lp=0.5, s=0.4, r=0.07), offsets=[0, 0, 0.07]), 0.36)
save('pk_gem', mix(*[tone(3200 + 400 * k, 0.06, 0.6, a=0.0005, s=0.3, r=0.05) for k in range(3)],
                   offsets=[0, 0.04, 0.08]), 0.25)
save('pk_food', noise(0.12, 1, lp=0.25, a=0.01, d=0.03, s=0.5, r=0.06), 0.3)
