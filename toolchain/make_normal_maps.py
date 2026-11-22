"""Generate a normal map for every Vulture tile (tiles/ -> tiles_n/).

Each output pixel's RGB is the surface normal in screen space
(x right, y down, z toward the viewer), encoded as (n + 1) / 2 * 255, and its
alpha is the tile's own alpha.  The game lights the map per pixel from these.

Height model per tile family:
  floor / edge   fine texture only (high-passed brightness): stones, cracks
                 and mortar get relief, the tile itself stays flat
  wall           the same at a coarser scale (bricks, blocks)
  everything     texture plus a rounded bevel from the silhouette, so
  else           monsters, items, statues and features read as volumes

Usage: python make_normal_maps.py <tiles dir> <out dir> [--only PREFIX ...]
Needs numpy, Pillow, SciPy.
"""
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

FAMILY = {
    'floor': dict(fine=0.8, coarse=5.0, bevel=0.0, strength=5.0),
    'edge': dict(fine=0.8, coarse=5.0, bevel=0.0, strength=5.0),
    'wall': dict(fine=0.9, coarse=7.0, bevel=0.25, strength=4.5),
}
SPRITE = dict(fine=0.7, coarse=3.5, bevel=0.6, strength=4.0)
SKIP = ('cursor.',)  # UI only


def height_of(rgba, fam):
    a = rgba[..., 3] / 255.0
    lum = (0.30 * rgba[..., 0] + 0.59 * rgba[..., 1] + 0.11 * rgba[..., 2]) / 255.0
    solid = a > 0.5
    # brightness detail: a little smoothing, minus the broad shading the
    # artist painted in (that is lighting, not shape)
    # both blurs weighted by alpha, so the transparent area outside a tile
    # never reads as a groove along its edge (that drew a grid on floors)
    fine = ndimage.gaussian_filter(lum * a, fam['fine']) / (ndimage.gaussian_filter(a, fam['fine']) + 1e-4)
    broad = ndimage.gaussian_filter(lum * a, fam['coarse'])
    wsum = ndimage.gaussian_filter(a, fam['coarse']) + 1e-4
    detail = (fine - broad / wsum) * solid
    h = detail
    if fam['bevel'] > 0 and solid.any():
        # rounded edges: distance to the silhouette, eased like a dome rim
        d = ndimage.distance_transform_edt(solid)
        r = max(3.0, min(9.0, 0.12 * min(rgba.shape[0], rgba.shape[1])))
        rim = np.sqrt(np.clip(d / r, 0.0, 1.0))
        h = h + fam['bevel'] * 0.35 * rim
    return h, a


def normals_of(h, a, strength):
    gx = ndimage.sobel(h, axis=1) / 8.0
    gy = ndimage.sobel(h, axis=0) / 8.0
    nx, ny, nz = -gx * strength * 8.0, -gy * strength * 8.0, np.ones_like(h)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    n = np.stack([nx * inv, ny * inv, nz * inv], axis=-1)
    n[a <= 0.02] = (0.0, 0.0, 1.0)
    return n


def make(src, dst):
    name = os.path.basename(src)
    fam = FAMILY.get(name.split('.', 1)[0], SPRITE)
    img = Image.open(src).convert('RGBA')
    rgba = np.asarray(img, dtype=np.float64)
    h, a = height_of(rgba, fam)
    n = normals_of(h, a, fam['strength'])
    out = np.empty(rgba.shape, dtype=np.uint8)
    out[..., :3] = np.clip((n + 1.0) * 0.5 * 255.0 + 0.5, 0, 255).astype(np.uint8)
    out[..., 3] = rgba[..., 3].astype(np.uint8)
    Image.fromarray(out, 'RGBA').save(dst, optimize=True)


def main():
    src_dir, out_dir = sys.argv[1], sys.argv[2]
    only = sys.argv[sys.argv.index('--only') + 1:] if '--only' in sys.argv else None
    os.makedirs(out_dir, exist_ok=True)
    names = sorted(f for f in os.listdir(src_dir) if f.lower().endswith('.png'))
    done = 0
    for f in names:
        if f.startswith(SKIP) or (only and not f.startswith(tuple(only))):
            continue
        make(os.path.join(src_dir, f), os.path.join(out_dir, f))
        done += 1
    print('normal maps:', done)


if __name__ == '__main__':
    main()
