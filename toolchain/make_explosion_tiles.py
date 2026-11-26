"""IMPLEMENTATION_LOG s198: explosion sprites for the four types that had none.

Vulture shipped fire, frost and magic explosion sprites; dark, noxious,
muddy and wet explosions borrowed the fire ones (vulture_tiles.conf "=>"),
so a gas spore's green cloud or a water explosion looked like a fireball.
This makes each its own set from the fire sprites: the brightness of every
pixel is mapped onto the type's colour ramp (three stops, dark to bright),
the alpha is kept, so the shapes - and the fire normal maps - still fit.

    python make_explosion_tiles.py <gamedata dir>

writes tiles/explosion.<type>_<1..9>.png and tiles_n/ copies of the fire
normal maps, and points vulture_tiles.conf at them.
"""
import os
import re
import shutil
import sys

from PIL import Image

RAMPS = {
    # type: (dark, mid, bright) - RGB
    'dark': ((6, 2, 12), (70, 24, 110), (175, 120, 235)),
    'noxious': ((14, 40, 8), (80, 175, 35), (215, 255, 120)),
    'muddy': ((36, 22, 8), (125, 82, 38), (215, 178, 118)),
    'wet': ((6, 22, 70), (45, 115, 225), (205, 236, 255)),
}


def ramp(stops, v):
    lo, mid, hi = stops
    if v < 0.5:
        a, b, f = lo, mid, v / 0.5
    else:
        a, b, f = mid, hi, (v - 0.5) / 0.5
    return tuple(int(round(a[k] + (b[k] - a[k]) * f)) for k in range(3))


def recolour(src, stops):
    im = Image.open(src).convert('RGBA')
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            v = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
            v = min(1.0, max(0.0, (v - 0.12) / 0.88))  # fire is never black: stretch
            px[x, y] = ramp(stops, v) + (a,)
    return im


def main(gamedata):
    tiles = os.path.join(gamedata, 'tiles')
    tiles_n = os.path.join(gamedata, 'tiles_n')
    conf = os.path.join(gamedata, 'config', 'vulture_tiles.conf')
    for kind, stops in RAMPS.items():
        for n in range(1, 10):
            src = os.path.join(tiles, 'explosion.fiery_%d.png' % n)
            recolour(src, stops).save(os.path.join(tiles, 'explosion.%s_%d.png' % (kind, n)))
            nsrc = os.path.join(tiles_n, 'explosion.fiery_%d.png' % n)
            if os.path.exists(nsrc):
                shutil.copyfile(nsrc, os.path.join(tiles_n, 'explosion.%s_%d.png' % (kind, n)))
    raw = open(conf, 'rb').read().decode('utf-8', 'surrogateescape')
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    fiery = {}
    for ln in lines:
        m = re.match(r'explosion\.FIERY_(\d)\s+=\s+"tiles/explosion\.fiery_\d\.png"\s+(-?\d+)\s+(-?\d+)', ln)
        if m:
            fiery[m.group(1)] = (m.group(2), m.group(3))
    assert len(fiery) == 9, fiery
    out, changed = [], 0
    for ln in lines:
        m = re.match(r'explosion\.(DARK|NOXIOUS|MUDDY|WET)_(\d)(\s+)=>\s+explosion\.FIERY_\d\s*$', ln)
        if m:
            kind, n = m.group(1), m.group(2)
            xo, yo = fiery[n]
            name = 'explosion.%s_%s' % (kind, n)
            ln = '%s = "tiles/explosion.%s_%s.png"   %s   %s' % (
                name.ljust(34), kind.lower(), n, xo, yo)
            changed += 1
        out.append(ln)
    open(conf, 'wb').write(nl.join(out).encode('utf-8', 'surrogateescape'))
    print('sprites: %d, conf lines changed: %d' % (len(RAMPS) * 9, changed))


if __name__ == '__main__':
    main(sys.argv[1])
