"""s131: synthesized UI / feedback sound effects (pure Python, no samples).
22050 Hz mono 16-bit into VultureNetHack/sound/."""
import array, math, random, wave, os

SR = 22050
OUT = r'C:\Users\dalla\Vulture\VultureNetHack\sound'
random.seed(131)


def n(sec):
    return int(sec * SR)


def env_adsr(i, total, a=0.005, d=0.05, s=0.6, r=0.1):
    t = i / SR
    T = total / SR
    if t < a:
        return t / a
    if t < a + d:
        return 1 - (1 - s) * (t - a) / d
    if t > T - r:
        return s * max(0.0, (T - t) / r)
    return s


def tone(freq, dur, amp=1.0, f_end=None, shape='sine', a=0.004, d=0.04, s=0.5, r=0.08, vib=0.0):
    N = n(dur)
    out = [0.0] * N
    ph = 0.0
    for i in range(N):
        f = freq if f_end is None else freq * (f_end / freq) ** (i / max(1, N - 1))
        if vib:
            f *= 1 + vib * math.sin(2 * math.pi * 6 * i / SR)
        ph += 2 * math.pi * f / SR
        if shape == 'sine':
            v = math.sin(ph)
        elif shape == 'tri':
            v = 2 / math.pi * math.asin(math.sin(ph))
        elif shape == 'square':
            v = 0.6 if math.sin(ph) >= 0 else -0.6
        else:  # saw
            v = ((ph / (2 * math.pi)) % 1.0) * 2 - 1
        out[i] = v * amp * env_adsr(i, N, a, d, s, r)
    return out


def noise(dur, amp=1.0, lp=0.3, hp=0.0, a=0.002, d=0.03, s=0.4, r=0.06, lp_end=None):
    N = n(dur)
    out = [0.0] * N
    y = 0.0
    yh = 0.0
    prev = 0.0
    for i in range(N):
        k = lp if lp_end is None else lp + (lp_end - lp) * i / max(1, N - 1)
        x = random.uniform(-1, 1)
        y += k * (x - y)                  # low-pass
        v = y
        if hp:
            yh = hp * (yh + v - prev)     # high-pass
            prev = v
            v = yh
        out[i] = v * amp * env_adsr(i, N, a, d, s, r)
    return out


def mix(*parts, offsets=None):
    offsets = offsets or [0.0] * len(parts)
    L = max(len(p) + n(o) for p, o in zip(parts, offsets))
    out = [0.0] * L
    for p, o in zip(parts, offsets):
        s0 = n(o)
        for i, v in enumerate(p):
            out[s0 + i] += v
    return out


def save(name, x, peak=0.6):
    m = max(1e-9, max(abs(v) for v in x))
    x = [v / m * peak for v in x]
    # short fade-out to avoid clicks
    f = min(len(x), n(0.006))
    for i in range(f):
        x[len(x) - f + i] *= 1 - i / f
    w = wave.open(os.path.join(OUT, name + '.wav'), 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(array.array('h', [int(max(-1, min(1, v)) * 32767) for v in x]).tobytes())
    w.close()
    print(name, round(len(x) / SR, 2))


# ---- UI ----
save('ui_hover', tone(2400, 0.035, 1, f_end=2600, a=0.001, d=0.01, s=0.3, r=0.02), 0.18)
save('ui_click', mix(tone(1500, 0.05, 1, f_end=900, a=0.001, d=0.02, s=0.3, r=0.03),
                     noise(0.03, 0.5, lp=0.6, hp=0.6, r=0.02)), 0.4)
save('ui_open', mix(tone(660, 0.09, 1, a=0.002, d=0.03, s=0.5, r=0.05),
                    tone(990, 0.12, 0.8, a=0.002, d=0.04, s=0.5, r=0.07), offsets=[0, 0.05]), 0.35)
save('ui_assign', mix(tone(880, 0.1, 1, s=0.5), tone(1320, 0.18, 0.9, s=0.5, r=0.12),
                      tone(1760, 0.22, 0.6, s=0.4, r=0.16), offsets=[0, 0.06, 0.12]), 0.38)
save('ui_clear', tone(700, 0.14, 1, f_end=380, shape='tri', s=0.5, r=0.08), 0.3)
save('ui_error', mix(tone(220, 0.16, 1, shape='square', s=0.6, r=0.05),
                     tone(233, 0.16, 0.8, shape='square', s=0.6, r=0.05)), 0.25)
save('ui_cancel', tone(520, 0.12, 1, f_end=300, shape='sine', s=0.4, r=0.07), 0.3)
save('ui_lock', mix(tone(500, 0.06, 1, shape='square', s=0.4, r=0.03),
                    tone(750, 0.1, 1, shape='square', s=0.4, r=0.06), offsets=[0, 0.05]), 0.25)
save('ui_zoom', mix(tone(1100, 0.05, 1, f_end=1500, a=0.001, s=0.3, r=0.03),
                    noise(0.05, 0.3, lp=0.4, hp=0.5)), 0.2)

# ---- click markers (what the hero is sent to do) ----
save('mk_move', mix(tone(1200, 0.06, 1, f_end=1800, a=0.001, s=0.3, r=0.04),
                    noise(0.08, 0.5, lp=0.15, lp_end=0.5, hp=0.3, s=0.4, r=0.06)), 0.3)
save('mk_attack', mix(noise(0.18, 1, lp=0.9, hp=0.85, a=0.001, d=0.02, s=0.4, r=0.14),
                      tone(2900, 0.2, 0.4, f_end=2500, a=0.001, s=0.3, r=0.15),
                      tone(3800, 0.16, 0.25, a=0.001, s=0.3, r=0.12)), 0.4)
save('mk_ally', mix(tone(1046, 0.25, 1, s=0.4, r=0.2), tone(1568, 0.3, 0.5, s=0.3, r=0.25)), 0.3)
save('mk_item', mix(*[tone(1800 + 300 * k, 0.09, 0.8, a=0.001, s=0.3, r=0.07) for k in range(4)],
                    offsets=[0.0, 0.04, 0.08, 0.12]), 0.3)
save('mk_door', mix(noise(0.07, 1, lp=0.12, s=0.5, r=0.05), tone(180, 0.08, 0.7, s=0.4, r=0.05),
                    noise(0.07, 0.8, lp=0.12, s=0.5, r=0.05), offsets=[0, 0, 0.11]), 0.45)
save('mk_stairs', mix(noise(0.35, 1, lp=0.05, lp_end=0.4, s=0.5, r=0.2),
                      tone(400, 0.35, 0.4, f_end=150, s=0.4, r=0.2)), 0.35)
save('mk_drink', mix(tone(300, 0.07, 1, f_end=900, a=0.001, s=0.4, r=0.04),
                     tone(420, 0.07, 0.8, f_end=1100, a=0.001, s=0.4, r=0.04), offsets=[0, 0.08]), 0.35)

# ---- aiming ----
save('aim_ready', mix(noise(0.2, 0.6, lp=0.05, lp_end=0.6, hp=0.4, s=0.6, r=0.05),
                      tone(500, 0.2, 0.5, f_end=900, s=0.4, r=0.05)), 0.3)
save('aim_fire', mix(noise(0.16, 1, lp=0.7, lp_end=0.1, hp=0.2, a=0.001, s=0.6, r=0.1),
                     tone(900, 0.12, 0.5, f_end=300, a=0.001, s=0.4, r=0.08)), 0.42)

# ---- combat / status ----
save('hit_big', mix(tone(90, 0.25, 1, f_end=45, a=0.001, d=0.05, s=0.5, r=0.15),
                    noise(0.12, 1, lp=0.8, lp_end=0.2, a=0.001, d=0.03, s=0.4, r=0.08),
                    noise(0.05, 0.7, lp=0.95, hp=0.9, a=0.0005, s=0.3, r=0.03)), 0.7)
save('status_bad', mix(tone(185, 0.6, 1, shape='saw', s=0.5, r=0.35, vib=0.01),
                       tone(196, 0.6, 0.9, shape='saw', s=0.5, r=0.35, vib=0.012),
                       tone(277, 0.6, 0.6, shape='tri', s=0.4, r=0.35)), 0.32)
save('alert', mix(tone(988, 0.08, 1, shape='tri', s=0.5, r=0.05),
                  tone(740, 0.12, 1, shape='tri', s=0.5, r=0.08), offsets=[0, 0.09]), 0.3)
save('amove_go', mix(noise(0.22, 1, lp=0.2, lp_end=0.9, hp=0.5, a=0.01, s=0.6, r=0.08),
                     tone(700, 0.2, 0.4, f_end=1400, s=0.4, r=0.06)), 0.35)
save('rest_start', mix(noise(0.6, 0.8, lp=0.05, a=0.15, d=0.1, s=0.6, r=0.3),
                       tone(330, 0.6, 0.25, a=0.15, s=0.5, r=0.3)), 0.25)
save('rest_done', mix(tone(784, 0.3, 1, s=0.4, r=0.25), tone(988, 0.35, 0.8, s=0.4, r=0.28),
                      tone(1175, 0.45, 0.6, s=0.4, r=0.35), offsets=[0, 0.1, 0.2]), 0.32)
save('rustle', noise(0.22, 1, lp=0.6, hp=0.6, a=0.02, d=0.05, s=0.5, r=0.1), 0.22)
save('card_whoosh', mix(noise(0.6, 1, lp=0.02, lp_end=0.3, a=0.2, s=0.6, r=0.3),
                        tone(220, 0.6, 0.3, f_end=330, a=0.2, s=0.5, r=0.3)), 0.3)
