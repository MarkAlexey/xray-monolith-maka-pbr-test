#!/usr/bin/env python3
"""
make_blood_atlas.py - pack blood masks and normal maps into the textures of the HUD blood
experiment (blood on hands / weapons), and write configs/plugins/hud_blood_atlas.ltx that lists every tile.

Input: grey masks (white = blood) laid out on a grid, and their normal maps on the same grid.
  spray   4x4 flipbook, impact splat breaking up (frame 0 dense ... 15 sparse)
  flow    4x4 flipbook, blood running down (frame 0 short ... 15 broken up)
  drops   2x2 round droplets
  smears  2x2 smear shapes
  drip    1x1 teardrop (drip head, narrow end up)
Any set can be left out. A set without a normal map gets one worked out from its mask.

Output (SIZE x SIZE each, with mipmaps), folder textures/hud_blood/ under --out:
  hud_blood_mask.dds    R = spray  G = flow  B = drops / smears / drip  A = 0 (free)
  hud_blood_nrm_a.dds   RG = spray normal XY   BA = flow normal XY
  hud_blood_nrm_b.dds   RG = drops / smears / drip normal XY   BA = 0.5 (free, flat)
  configs/plugins/hud_blood_atlas.ltx   the tile rectangles in 0..1 UV, per set (the script reads it)
  preview/*.png         with --preview: what was packed, for checking by eye

Layout of the B channel (and of nrm_b RG):  top-left quarter = drops (2x2),
top-right quarter = smears (2x2), bottom-left quarter = drip, bottom-right quarter = free.

Normal convention written: tangent-space X right (+u), Y DOWN the texture (+v), i.e. DirectX,
stored as XY * 0.5 + 0.5; Z is rebuilt in the shader. Source normals are read in the layout of
NORMAL_LAYOUT ("ag": X in alpha, Y in green, the MW4 / DXT5nm style; "rgb": a plain normal map)
and NORMAL_Y_SOURCE ("dx" or "gl", gl gets its Y flipped).

Usage
  python make_blood_atlas.py --out out_dir \\
      --spray-mask spray.png --spray-normal spray_n.dds --flow-mask flow.png --flow-normal flow_n.dds \\
      --drops-mask drops.png --drops-normal drops_n.dds --smears-mask smears.png \\
      --smears-normal smears_n.dds --drip-mask drip.dds [--preview] [--texconv texconv.exe] [--dry-run]
  --dry-run   read and check everything, print the report, write nothing
  --texconv   path to Microsoft's texconv (DirectXTex); compresses the three .dds to BC7
              (without it they are uncompressed BGRA8, about 5.6 MB each at 1024)
Needs Python 3.8+, numpy and Pillow (Pillow 9.1+ reads BC7 / BC4 .dds).

Checks it reports (warnings, nothing is changed unless a setting says so)
  - mask and normal of a set do not line up (IoU of "has blood" vs "normal not flat" per tile):
    usually a different grid or tile order in the normal map
  - shapes touching the edge of their tile (mip bleeding into the next tile at a distance)
  - flow frames whose stream is cut flat at the bottom of the tile (FLOW_CUT_FIX decides)
  - the stored mip count after texconv

Caveats
  - Masks: the alpha channel is used when the file has a real one (varies, not all 255),
    otherwise the grey level. Keep soft edges: the grey level is the blood's thickness.
  - Source normals with a broken outer pixel border (wrap-around generation, seen on the MW4 spray
    normal) get their outer EDGE_FLATTEN_PX pixels set flat.
  - Flipbooks are read row by row, left to right, top to bottom: frame 0 top-left.
  - Resampling is per tile (Lanczos), so tiles never blur into each other at the top level.
    Lower mips of an atlas still mix neighbouring tiles; the shader must clamp its mip level
    (max_lod in the ltx) and keep its UVs inside the tile (padding_uv in the ltx).
  - texconv: BC7 keeps the mips written here as long as the size is not changed; the tool reads
    the result back and warns if the mip count dropped. BC7 encoding on the CPU takes a while.
  - The DRIP_NORMAL / derived normals are an approximation (a dome from a blurred mask); use a
    real normal map with --<set>-normal when there is one.
"""

import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("error: needs numpy and Pillow (pip install numpy pillow)")

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

SIZE = 1024                         # output texture size (power of two)
TEXTURE_FOLDER = "hud_blood"        # textures\<this>\ in the game
LTX_PATH = ("configs", "plugins", "hud_blood_atlas.ltx")   # under --out; read by scripts\hud_blood.script

NORMAL_LAYOUT = "ag"                # "ag" X in alpha, Y in green | "rgb" plain normal map
NORMAL_Y_SOURCE = "dx"              # "dx" Y down | "gl" Y up (flipped on read)
EDGE_FLATTEN_PX = 2                 # outer pixels of every source normal map set flat (0 = off)

# Per set: grid (cols, rows), where it goes, and how strong its normal is (XY scale before
# renormalising: <1 flattens, the MW4 drops are very domed and blood is a thin film)
SETS = {
    #           grid    mask channel  normal tex / channels   region (x0,y0,x1,y1) in 0..1   strength
    "spray":  ((4, 4), "r",          ("nrm_a", "rg"),         (0.0, 0.0, 1.0, 1.0),           1.0),
    "flow":   ((4, 4), "g",          ("nrm_a", "ba"),         (0.0, 0.0, 1.0, 1.0),           1.0),
    "drops":  ((2, 2), "b",          ("nrm_b", "rg"),         (0.0, 0.0, 0.5, 0.5),           0.5),
    "smears": ((2, 2), "b",          ("nrm_b", "rg"),         (0.5, 0.0, 1.0, 0.5),           1.0),
    "drip":   ((1, 1), "b",          ("nrm_b", "rg"),         (0.0, 0.5, 0.5, 1.0),           1.0),
}

# Normals worked out from a mask (sets given without a normal map)
DERIVED_BLUR = 0.03                 # blur radius as a fraction of the tile (rounds the dome)
DERIVED_STRENGTH = 6.0              # slope scale of the derived dome

# Crop every tile of a set to the square around what any of its frames shows (+ margin), so the
# shapes fill their tiles: the MW4 sheets leave most of each frame empty, which would make the
# stamps in game far smaller than their tiles. The same crop for all frames of a set keeps
# flipbook frames aligned.
CROP_TO_CONTENT = True
CROP_MARGIN = 0.04                  # fraction of the source tile kept around the shapes
CROP_KEEP = 0.985                   # flipbooks (more than 4 tiles): the square holds this share of
                                    # the blood, so stray specks far out (spray sheet) are cut off
                                    # instead of shrinking every stamp. Other sets keep everything.

# Flow frames cut flat at the bottom of their tile
FLOW_CUT_FIX = "fade"               # "fade" fade the bottom FLOW_FADE of the tile | "none"
FLOW_FADE = 0.08                    # fraction of the tile height
CUT_ROW_COVER = 0.004               # a tile counts as cut when this much of its bottom row is blood

# Checks
SHAPE_THRESHOLD = 0.02              # mask value counted as blood
NORMAL_FLAT = 0.06                  # |XY| below this counts as flat
TILE_CLEAR_PX = 2                   # blood within this many px of a tile edge is cleared (mip bleed
                                    # into the next tile); the amount cleared is reported. 0 = off
IOU_DILATE_PX = 2                   # mask / normal overlap is measured on shapes grown by this much
IOU_WARN = 0.5                      # overlap below this is reported (tiles with enough blood only)
IOU_MIN_COVER = 0.005               # tiles with less blood than this are not compared
MIN_TILE_PX_AT_MAX_LOD = 8          # max_lod written so the smallest tile keeps this many px

# ------------------------------------------------------------------------------------------


def log(msg):
    print(msg)


def load_rgba(path):
    im = Image.open(path)
    im.load()
    return np.asarray(im.convert("RGBA")).astype(np.float32) / 255.0


def load_mask(path):
    a = load_rgba(path)
    alpha = a[..., 3]
    if alpha.min() < 0.99 and alpha.std() > 0.01:
        return alpha, "alpha"
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114, "grey"


def load_normal(path):
    """-> float32 HxWx2 (x, y) in -1..1, DirectX Y"""
    a = load_rgba(path)
    if NORMAL_LAYOUT == "ag":
        x, y = a[..., 3] * 2 - 1, a[..., 1] * 2 - 1
    else:
        x, y = a[..., 0] * 2 - 1, a[..., 1] * 2 - 1
    if NORMAL_Y_SOURCE == "gl":
        y = -y
    n = np.stack([x, y], -1)
    e = EDGE_FLATTEN_PX
    if e > 0:
        n[:e, :] = 0
        n[-e:, :] = 0
        n[:, :e] = 0
        n[:, -e:] = 0
    return n


def dilate(b, r):
    if r <= 0:
        return b
    out = b.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(b, dy, 0), dx, 1)
    return out


def iou(a, b):
    u = (a | b).sum()
    return (a & b).sum() / u if u else 1.0


def resize_f(ch, w, h):
    return np.asarray(Image.fromarray(ch.astype(np.float32), "F").resize((w, h), Image.LANCZOS), np.float32)


def blur(ch, radius):
    if radius < 0.5:
        return ch
    r = int(math.ceil(radius * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / radius) ** 2)
    k /= k.sum()
    p = np.pad(ch, r, mode="edge")
    p = np.apply_along_axis(lambda v: np.convolve(v, k, mode="valid"), 1, p)
    p = np.apply_along_axis(lambda v: np.convolve(v, k, mode="valid"), 0, p)
    return p


def derive_normal(mask, tile_px):
    h = np.sqrt(np.clip(blur(mask, DERIVED_BLUR * tile_px), 0, 1))
    dy, dx = np.gradient(h)
    # raised surface: x = -dh/du, DirectX y = -dh/dv
    return np.stack([-dx, -dy], -1) * DERIVED_STRENGTH * (tile_px / 256.0)


def scale_normal(n, strength):
    if strength == 1.0:
        return n
    x, y = n[..., 0] * strength, n[..., 1] * strength
    z = np.sqrt(np.clip(1 - (n[..., 0] ** 2 + n[..., 1] ** 2), 0, 1))
    l = np.sqrt(x * x + y * y + z * z) + 1e-8
    return np.stack([x / l, y / l], -1)


def clamp_xy(n):
    l = np.sqrt((n ** 2).sum(-1, keepdims=True))
    return np.where(l > 0.999, n / np.maximum(l, 1e-8) * 0.999, n)


def tiles_of(grid, region):
    """[(x0, y0, x1, y1) in output pixels] row by row"""
    cols, rows = grid
    rx0, ry0, rx1, ry1 = [int(round(v * SIZE)) for v in region]
    tw, th = (rx1 - rx0) // cols, (ry1 - ry0) // rows
    return [(rx0 + c * tw, ry0 + r * th, rx0 + (c + 1) * tw, ry0 + (r + 1) * th)
            for r in range(rows) for c in range(cols)]


def src_tiles(shape, grid):
    h, w = shape[:2]
    cols, rows = grid
    out = []
    for r in range(rows):
        for c in range(cols):
            out.append((int(round(c * w / cols)), int(round(r * h / rows)),
                        int(round((c + 1) * w / cols)), int(round((r + 1) * h / rows))))
    return out


def content_square(tiles, keep):
    """(u0, v0, u1, v1) in 0..1 of a source tile: the square around the shapes of all tiles"""
    n = 256
    px, py = np.zeros(n), np.zeros(n)
    for m in tiles:
        r = resize_f(m, n, n)
        r = np.where(r > SHAPE_THRESHOLD, r, 0)
        px += r.sum(0)
        py += r.sum(1)
    if px.sum() <= 0:
        return (0.0, 0.0, 1.0, 1.0)
    cut = (1 - keep) / 2

    def span(p):
        c = np.cumsum(p) / p.sum()
        return np.searchsorted(c, cut, side="right") / n, (np.searchsorted(c, 1 - cut - 1e-9) + 1) / n

    u0, u1 = span(px)
    v0, v1 = span(py)
    side = min(1.0, max(u1 - u0, v1 - v0) + 2 * CROP_MARGIN)
    cu, cv = (u0 + u1) / 2, (v0 + v1) / 2
    a = min(max(cu - side / 2, 0.0), 1.0 - side)
    b = min(max(cv - side / 2, 0.0), 1.0 - side)
    return (round(a, 4), round(b, 4), round(a + side, 4), round(b + side, 4))


def sub_rect(r, crop):
    a0, b0, a1, b1 = r
    w, h = a1 - a0, b1 - b0
    return (a0 + int(round(crop[0] * w)), b0 + int(round(crop[1] * h)),
            a0 + max(1, int(round(crop[2] * w))), b0 + max(1, int(round(crop[3] * h))))


def pack_set(name, cfg, mask_path, normal_path, mask_tex, nrm_tex, report):
    grid, mch, (ntex, nch), region, strength = cfg
    mask, mode = load_mask(mask_path)
    log(f"{name:7} mask   {mask_path}  {mask.shape[1]}x{mask.shape[0]} ({mode})")
    normal = None
    if normal_path:
        normal = load_normal(normal_path)
        log(f"{name:7} normal {normal_path}  {normal.shape[1]}x{normal.shape[0]} ({NORMAL_LAYOUT}, {NORMAL_Y_SOURCE})")
    else:
        log(f"{name:7} normal (worked out from the mask)")
    dst = tiles_of(grid, region)
    sm = src_tiles(mask.shape, grid)
    sn = src_tiles(normal.shape, grid) if normal is not None else None
    mi = "rgba".index(mch)
    info = {"tiles": [], "cut": [], "warn": [], "cleared": 0, "iou": []}
    keep = CROP_KEEP if len(sm) > 4 else 1.0
    crop = content_square([mask[b0:b1, a0:a1] for a0, b0, a1, b1 in sm], keep) if CROP_TO_CONTENT else (0.0, 0.0, 1.0, 1.0)
    if crop != (0.0, 0.0, 1.0, 1.0):
        log(f"        cropped to {crop[2] - crop[0]:.2f} of each source tile (CROP_TO_CONTENT)")
    for t, (x0, y0, x1, y1) in enumerate(dst):
        tw, th = x1 - x0, y1 - y0
        a0, b0, a1, b1 = sub_rect(sm[t], crop)
        m = np.clip(resize_f(mask[b0:b1, a0:a1], tw, th), 0, 1)
        if normal is not None:
            c0, d0, c1, d1 = sub_rect(sn[t], crop)
            src = normal[d0:d1, c0:c1]
            n = np.stack([resize_f(src[..., 0], tw, th), resize_f(src[..., 1], tw, th)], -1)
        else:
            n = derive_normal(m, tw)
        n = clamp_xy(scale_normal(n, strength))

        # cut flat at the bottom (flow): record where, optionally fade
        bottom = m[-1, :] > 0.5
        cut = None
        if bottom.mean() > CUT_ROW_COVER:
            xs = np.nonzero(bottom)[0]
            cut = ((xs.min() + xs.max() + 1) / 2 / tw, (xs.max() + 1 - xs.min()) / tw)
            if FLOW_CUT_FIX == "fade":
                f = max(1, int(round(FLOW_FADE * th)))
                ramp = np.ones(th, np.float32)
                ramp[-f:] = np.linspace(1, 0, f) ** 2
                m = m * ramp[:, None]
                n = n * ramp[:, None, None]
        info["cut"].append(cut)

        # clear the tile border (mip bleed)
        e = TILE_CLEAR_PX
        if e > 0:
            border = np.zeros((th, tw), bool)
            border[:e, :] = border[-e:, :] = True
            border[:, :e] = border[:, -e:] = True
            info["cleared"] += int((m[border] > SHAPE_THRESHOLD).sum())
            m[border] = 0
            n[border] = 0

        # checks
        blood = m > SHAPE_THRESHOLD
        if not blood.any():
            info["warn"].append(f"tile {t}: empty")
        if normal is not None:
            info["iou"].append((dilate(blood, IOU_DILATE_PX), dilate(np.sqrt((n ** 2).sum(-1)) > NORMAL_FLAT, IOU_DILATE_PX),
                                blood.mean()))

        mask_tex[y0:y1, x0:x1, mi] = m
        nt = nrm_tex[ntex]
        ci = "rgba".index(nch[0])
        nt[y0:y1, x0:x1, ci] = n[..., 0]
        nt[y0:y1, x0:x1, ci + 1] = n[..., 1]
        info["tiles"].append((x0 / SIZE, y0 / SIZE, x1 / SIZE, y1 / SIZE))
    # mask / normal line up: per tile, and against the next tile (a shifted order)
    if info["iou"]:
        ious, shifted = [], []
        k = len(info["iou"])
        for t, (b, n_, cover) in enumerate(info["iou"]):
            if cover < IOU_MIN_COVER:
                continue
            ious.append(iou(b, n_))
            if b.shape == info["iou"][(t + 1) % k][1].shape and k > 1:
                shifted.append(iou(b, info["iou"][(t + 1) % k][1]))
            if ious[-1] < IOU_WARN:
                info["warn"].append(f"tile {t}: mask and normal overlap only {ious[-1]:.2f}")
        if ious:
            mi_, ms_ = float(np.mean(ious)), (float(np.mean(shifted)) if shifted else 0.0)
            log(f"        mask / normal overlap {mi_:.2f} (next tile's normal: {ms_:.2f})")
            if mi_ < IOU_WARN:
                info["warn"].append("this normal map does not match this mask (another set's normal?)")
            elif shifted and ms_ >= mi_ * 0.9:
                info["warn"].append("the normal map's tiles may be in another order or grid than the mask's")
        del info["iou"]
    if info["cleared"]:
        log(f"        cleared {info['cleared']} blood px at tile edges (TILE_CLEAR_PX = {TILE_CLEAR_PX})")
    report[name] = info
    cuts = sum(1 for c in info["cut"] if c)
    log(f"        {len(dst)} tiles of {dst[0][2]-dst[0][0]}x{dst[0][3]-dst[0][1]} px"
        + (f", {cuts} cut at the bottom ({FLOW_CUT_FIX})" if cuts else ""))
    for w in info["warn"]:
        log(f"        warning: {w}")


def mips_mask(img):
    out = [img]
    while out[-1].shape[0] > 1:
        p = out[-1]
        out.append((p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2]) * 0.25)
    return out


def mips_normal(xy):
    """xy pairs in -1..1 -> chain, each level averaged as vectors and renormalised"""
    z = np.sqrt(np.clip(1 - (xy ** 2).sum(-1, keepdims=True), 0, 1))
    v = np.concatenate([xy, z], -1)
    out = []
    while True:
        l = np.sqrt((v ** 2).sum(-1, keepdims=True)) + 1e-8
        out.append((v / l)[..., :2])
        if v.shape[0] == 1:
            break
        v = (v[0::2, 0::2] + v[1::2, 0::2] + v[0::2, 1::2] + v[1::2, 1::2]) * 0.25
    return out


def write_dds(path, levels):
    """levels: list of HxWx4 float 0..1 (RGBA). Writes legacy BGRA8 with mips."""
    h, w = levels[0].shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8 | 0x20000
    pf = struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr = struct.pack("<7I", 124, flags, h, w, w * 4, 0, len(levels)) + b"\0" * 44 + pf + \
        struct.pack("<5I", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"DDS " + hdr)
        for lv in levels:
            b = np.clip(np.round(lv * 255), 0, 255).astype(np.uint8)
            f.write(b[..., [2, 1, 0, 3]].tobytes())


def dds_info(path):
    d = open(path, "rb").read(148)
    h = struct.unpack("<31I", d[4:128])
    fourcc = struct.pack("<I", h[20])
    fmt = fourcc.decode("latin1")
    if fourcc == b"DX10":
        fmt = f"DXGI {struct.unpack('<I', d[128:132])[0]}" + (" (BC7)" if struct.unpack('<I', d[128:132])[0] in (98, 99) else "")
    elif h[19] & 0x40:
        fmt = "uncompressed"
    return h[3], h[2], max(1, h[6]), fmt


def write_ltx(path, report, lods):
    smallest = min((t[2] - t[0]) * SIZE for r in report.values() for t in r["tiles"])
    max_lod = max(0, int(math.log2(smallest / MIN_TILE_PX_AT_MAX_LOD)))
    L = [f"; generated by make_blood_atlas.py - HUD blood experiment",
         f"; tiles: u0, v0, u1, v1 in 0..1 (v down). Frames run row by row.",
         "",
         "[hud_blood_textures]",
         f"mask       = {TEXTURE_FOLDER}\\hud_blood_mask      ; r spray, g flow, b drops/smears/drip, a free",
         f"normal_a   = {TEXTURE_FOLDER}\\hud_blood_nrm_a     ; rg spray xy, ba flow xy",
         f"normal_b   = {TEXTURE_FOLDER}\\hud_blood_nrm_b     ; rg drops/smears/drip xy",
         f"size       = {SIZE}",
         f"normal_y   = dx                 ; +y = down the texture (+v); z rebuilt",
         f"max_lod    = {max_lod}                  ; mip limit so tiles do not bleed",
         f"padding_uv = {0.5 / SIZE * 2 ** max_lod:.6f}       ; keep uv this far inside a tile",
         f"mips       = {lods}",
         ""]
    for name, r in report.items():
        grid, mch, (ntex, nch), _, strength = SETS[name]
        L += [f"[hud_blood_set_{name}]",
              f"channel = mask.{mch}",
              f"normal  = {ntex}.{nch}",
              f"count   = {len(r['tiles'])}"]
        if name == "drip":
            L.append("rotate  = false            ; points down (+v): narrow end up")
        elif name == "flow":
            L.append("rotate  = false            ; runs down (+v)")
        else:
            L.append("rotate  = true")
        for i, t in enumerate(r["tiles"]):
            L.append(f"tile_{i:02} = {t[0]:.6f}, {t[1]:.6f}, {t[2]:.6f}, {t[3]:.6f}")
        if any(r["cut"]):
            L.append(f"; where the stream leaves the tile bottom: u in the tile (centre), width; - = not cut")
            for i, c in enumerate(r["cut"]):
                L.append(f"cut_{i:02}  = " + (f"{c[0]:.4f}, {c[1]:.4f}" if c else "-"))
        L.append("")
    open(path, "w", newline="\r\n").write("\n".join(L))


def preview(folder, mask_tex, nrm):
    os.makedirs(folder, exist_ok=True)
    for i, c in enumerate("rgb"):
        Image.fromarray((mask_tex[..., i] * 255).astype(np.uint8)).save(os.path.join(folder, f"mask_{c}.png"))
    for name, t in nrm.items():
        for pair in ((0, "rg"), (2, "ba")):
            xy = t[..., pair[0]:pair[0] + 2]
            z = np.sqrt(np.clip(1 - (xy ** 2).sum(-1, keepdims=True), 0, 1))
            rgb = np.concatenate([xy[..., :1], -xy[..., 1:2], z], -1) * 0.5 + 0.5   # shown as OpenGL-style
            Image.fromarray((rgb * 255).astype(np.uint8)).save(os.path.join(folder, f"{name}_{pair[1]}.png"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    for s in SETS:
        ap.add_argument(f"--{s}-mask")
        ap.add_argument(f"--{s}-normal")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--texconv")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if SIZE & (SIZE - 1):
        sys.exit("error: SIZE must be a power of two")
    given = {s: (getattr(a, f"{s}_mask"), getattr(a, f"{s}_normal")) for s in SETS}
    if not any(m for m, _ in given.values()):
        sys.exit("error: no masks given")
    for s, (m, n) in given.items():
        if n and not m:
            sys.exit(f"error: --{s}-normal needs --{s}-mask")

    mask_tex = np.zeros((SIZE, SIZE, 4), np.float32)
    nrm = {"nrm_a": np.zeros((SIZE, SIZE, 4), np.float32), "nrm_b": np.zeros((SIZE, SIZE, 4), np.float32)}
    report = {}
    for s, (m, n) in given.items():
        if m:
            pack_set(s, SETS[s], m, n, mask_tex, nrm, report)

    lods = int(math.log2(SIZE)) + 1
    tex_dir = os.path.join(a.out, "textures", TEXTURE_FOLDER)
    outs = {"hud_blood_mask": mips_mask(mask_tex)}
    for name, t in nrm.items():
        ch_a, ch_b = mips_normal(t[..., 0:2]), mips_normal(t[..., 2:4])
        outs[f"hud_blood_{name}"] = [np.concatenate([x, y], -1) * 0.5 + 0.5 for x, y in zip(ch_a, ch_b)]

    if a.dry_run:
        for k in outs:
            log(f"[dry-run] would write {os.path.join(tex_dir, k + '.dds')}  {SIZE}x{SIZE}, {lods} mips")
        log(f"[dry-run] would write {os.path.join(a.out, *LTX_PATH)}")
        return 0

    os.makedirs(tex_dir, exist_ok=True)
    for k, levels in outs.items():
        p = os.path.join(tex_dir, k + ".dds")
        write_dds(p, levels)
        if a.texconv:
            tmp = tempfile.mkdtemp()
            r = subprocess.run([a.texconv, "-f", "BC7_UNORM", "-y", "-nologo", "-o", tmp, p],
                               capture_output=True, text=True)
            res = os.path.join(tmp, k + ".dds")
            if r.returncode != 0 or not os.path.exists(res):
                log(f"warning: texconv failed on {k}, kept uncompressed:\n{r.stdout}{r.stderr}")
            else:
                shutil.move(res, p)
            shutil.rmtree(tmp, ignore_errors=True)
        w, h, mips, fmt = dds_info(p)
        log(f"wrote {p}  {w}x{h}, {mips} mips, {fmt}" + ("   warning: mips were dropped" if mips < lods else ""))
    ltx = os.path.join(a.out, *LTX_PATH)
    os.makedirs(os.path.dirname(ltx), exist_ok=True)
    write_ltx(ltx, report, lods)
    log(f"wrote {ltx}")
    if a.preview:
        preview(os.path.join(a.out, "preview"), mask_tex, nrm)
        log(f"wrote previews to {os.path.join(a.out, 'preview')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
