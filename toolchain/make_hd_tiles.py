"""IMPLEMENTATION_LOG s201: double-resolution sprites (tiles_hd/).

Every sprite in gamedata/tiles is upscaled 4x with Real-ESRGAN (the general
x4plus model - it suits Vulture's pre-rendered 3D art and cleans up its
dithering) and brought down to exactly 2x with Lanczos, alpha kept.  The
game draws these when the map is zoomed in (vulture_sdl.cpp, vgm_hd_*), so a
zoomed dungeon stays sharp instead of turning into blocks.

    python make_hd_tiles.py <gamedata dir> <realesrgan-ncnn-vulkan.exe> [<work dir>]

Real-ESRGAN: https://github.com/xinntao/Real-ESRGAN (v0.2.5.0, the
realesrgan-ncnn-vulkan Windows build; runs on the GPU through Vulkan).

TRAP (found on the first run): one realesrgan-ncnn-vulkan process goes bad
after ~20 images on this machine - every later output is stripes and noise,
with any thread count or tile size.  So it runs in batches of 16, each a
fresh process, and every output is checked: shrunk back to 1x it must match
the original's colours where the original is opaque (a good one is off by
~8-16 levels - up to ~30 for heavily dithered art, whose dither the model
cleans away - a corrupt one by 40-230); failures are redone one at a time.
"""
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageChops, ImageStat

BATCH = 16
LIMIT = 40.0 # good: ~8-16, heavily dithered art 24-30 (the dither is cleaned away); ruined: 40-230


def error_of(orig, up):
    """mean colour difference of the 4x output shrunk to 1x, where opaque"""
    q = up.convert('RGBA').resize(orig.size, Image.LANCZOS)
    mask = orig.getchannel('A').point(lambda v: 255 if v > 128 else 0)
    if not mask.getbbox():
        return 0.0
    d = ImageChops.difference(orig.convert('RGB'), q.convert('RGB'))
    return sum(ImageStat.Stat(d, mask).mean) / 3.0


def upscale(exe, models, names, src, out4, tmp):
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(tmp)
    for n in names:
        shutil.copyfile(os.path.join(src, n), os.path.join(tmp, n))
    subprocess.run([exe, '-i', tmp, '-o', out4, '-n', 'realesrgan-x4plus', '-s', '4',
                    '-m', models, '-f', 'png'], capture_output=True, text=True)


def main(gamedata, exe, work=None):
    src = os.path.join(gamedata, 'tiles')
    dst = os.path.join(gamedata, 'tiles_hd')
    work = work or os.path.join(os.path.dirname(os.path.abspath(dst)), '_hd_work')
    out4 = os.path.join(work, 'x4v2')
    tmp = os.path.join(work, 'batch')
    os.makedirs(out4, exist_ok=True)
    os.makedirs(dst, exist_ok=True)
    models = os.path.join(os.path.dirname(exe), 'models')
    names = sorted(n for n in os.listdir(src) if n.lower().endswith('.png'))
    origs = {n: Image.open(os.path.join(src, n)).convert('RGBA') for n in names}

    def good(n):
        p = os.path.join(out4, n)
        return os.path.exists(p) and error_of(origs[n], Image.open(p)) <= LIMIT

    todo = [n for n in names if not good(n)]
    print('sprites %d, to upscale %d' % (len(names), len(todo)), flush=True)
    for i in range(0, len(todo), BATCH):
        upscale(exe, models, todo[i:i + BATCH], src, out4, tmp)
        if (i // BATCH) % 20 == 0:
            print('  %d / %d' % (min(i + BATCH, len(todo)), len(todo)), flush=True)
    for attempt in range(3):
        bad = [n for n in todo if not good(n)]
        print('after pass %d: %d bad' % (attempt + 1, len(bad)), flush=True)
        if not bad:
            break
        for n in bad:
            upscale(exe, models, [n], src, out4, tmp)
    bad = [n for n in names if not good(n)]
    made = 0
    for n in names:
        o = origs[n]
        w2, h2 = o.width * 2, o.height * 2
        p4 = os.path.join(out4, n)
        if n in bad:
            im = o.resize((w2, h2), Image.LANCZOS) # the fallback: smooth, never garbage
        else:
            im = Image.open(p4).convert('RGBA').resize((w2, h2), Image.LANCZOS)
        if n.startswith(('floor.', 'wall.', 'edge.')):
            # terrain tiles fit edge to edge: a soft upscaled edge would leave a
            # hairline seam between neighbours, so they keep the original's
            # hard alpha, doubled
            im.putalpha(o.getchannel('A').resize((w2, h2), Image.NEAREST))
        else:
            # a sprite's own transparent pixels stay transparent: Real-ESRGAN
            # smears a faint alpha into the empty margin - drop what the 2x of
            # the original alpha doesn't cover
            a0 = o.getchannel('A').resize((w2, h2), Image.BILINEAR)
            mask = a0.point(lambda v: 255 if v > 0 else 0)
            im.putalpha(Image.composite(im.getchannel('A'), Image.new('L', (w2, h2), 0), mask))
        im.save(os.path.join(dst, n), optimize=True)
        made += 1
    print('hd sprites written: %d (%d from the Lanczos fallback: %s)'
          % (made, len(bad), ', '.join(bad[:10])))


if __name__ == '__main__':
    main(*sys.argv[1:])
