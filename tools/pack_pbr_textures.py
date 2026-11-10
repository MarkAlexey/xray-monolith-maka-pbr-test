#!/usr/bin/env python3
"""
pack_pbr_textures.py - build X-Ray PBR textures (<name>.dds, <name>_bump.dds, <name>_bump#.dds
and <name>.ltx) from separate PBR maps, e.g. Substance Painter or Call of Duty (IW) exports.

Two packings (--layout), both read by the engine (gamedata/shaders/r3/sload.h, USE_PBR):

  atmos (default) - the community standard, LTX "material = 4.5" (Atmospherics, Hippobot).
  Renders the same with Atmospherics' own shaders and with the PBR shaders.
    <name>.dds        RGB albedo, A = alpha test / transparency (opaque: white or no alpha)
    <name>_bump.dds   R = normal X, G = normal Y (DirectX style), B = height (0 = none), A unused
    <name>_bump#.dds  R = ambient occlusion, G = roughness, B = metalness, A unused
    <name>.ltx        [texture] material = 4.5, bump_mode = use
                      (+ [shaders_options] SKIN_SSS = value with --skin-sss: no SSS channel)

  ixray - the older IX-Ray packing, LTX "material = pbr" or THM material "PBR".
    <name>.dds        RGB albedo, A = alpha test / transparency
    <name>_bump.dds   R = height (parallax, 0 = none), G = normal Y (OpenGL), B = unused, A = normal X
    <name>_bump#.dds  R = metalness, G = roughness, B = subsurface, A = ambient occlusion

All data is linear (never sRGB).

Inputs (PNG/TGA/BMP/TIFF, anything Pillow reads; all the same size):
  --albedo     colour map (CoD: _c)
  --metal      metalness, grey
  --gloss      gloss / smoothness, grey (CoD), inverted to roughness   } one of these, or
  --roughness  roughness, grey                                         } the packed normal's R
  --normal     RGB normal map (R = X, G = Y, B = Z), or
  --nog        CoD packed normal: R = gloss, G = normal Y, B = normal Z, A = normal X
  --ao, --height          optional, grey
  --sss                   optional, grey (ixray only; atmos has no channel: use --skin-sss)
  --skin-sss VALUE        atmos: one subsurface value for the whole texture (skin, ears, leaves)
  --input-normal opengl|directx   convention of --normal / --nog (default opengl, as
                          Substance's default export and CoD); converted to what --layout needs

Outputs go to --out (default ./packed):
  uncompressed DDS (A8R8G8B8 with full mip chain, written by this script; exact data, large),
  or block-compressed DDS through Microsoft texconv (--texconv), plus PNG previews with --png.

Usage
  python pack_pbr_textures.py --name wpn_deagle_mw --albedo deagle_c.png --metal deagle_m.png \
      --nog deagle_nog.png --dry-run
  python pack_pbr_textures.py --name wpn_deagle_mw --albedo c.png --metal m.png \
      --gloss g.png --normal n.png --out gamedata/textures/wpn --texconv texconv.exe

Known edge cases / caveats
  - "Roughness" exports of CoD materials are usually GLOSS: polished parts are bright. The dry
    run prints the map's mean over metal pixels and warns when it looks like gloss; check the
    result in game with PBR_DEBUG_VIEW 3 (roughness): polished metal must be dark.
  - Gloss -> roughness is 1 - gloss by default. IW's own gloss curve is not public;
    ROUGHNESS_GAMMA adjusts the result if highlights look too tight or too broad.
  - Normal Y convention: say what the INPUT is with --input-normal; the output is converted
    (atmos: DirectX, as Atmospherics reads it; ixray: OpenGL). ixray only: --dx-normal keeps a
    DirectX input unconverted and writes USE_DX_NORMAL_MAP = 1 instead. Wrong convention =
    bevels lit from below.
  - Height: without a height map R is 0. Do not leave gloss there: the engine offsets the lit
    position by height * 5 cm (def_virtualh).
  - Mipmaps: box-filtered; normals are renormalised per level and roughness is widened with
    normal variance (Toksvig-style) so distant metal does not sparkle.
  - texconv: always *_UNORM, never *_SRGB (sRGB would be gamma-converted twice). atmos: BC7
    for all three (BC1 for size: the packing needs no alpha); ixray: BC3 for both bump
    textures (X in alpha). texconv builds its own mips (no renormalisation, no Toksvig).
  - BC1 / BC5 must never be used for ixray bump textures (alpha / blue channels are data).
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

ROUGHNESS_GAMMA = 1.0             # roughness = roughness ** gamma (1 = unchanged)
ROUGHNESS_MIN = 0.02              # the shader clamps to this anyway
FLIP_GREEN = False                # also flip the input's normal Y (same as --input-normal directx)
DEFAULT_AO = 1.0                  # when no --ao is given
DEFAULT_SSS = 0.0                 # when no --sss is given
DEFAULT_HEIGHT = 0.0              # when no --height is given (0 = no parallax, no offset)
TOKSVIG_MIPS = True               # widen roughness in lower mips by normal variance
GLOSS_WARNING_MEAN = 0.50         # warn if a "roughness" map averages above this on metal pixels
TEXCONV_FORMATS = {
    "atmos": {"base": "BC7_UNORM", "bump": "BC7_UNORM", "bumpx": "BC7_UNORM"},   # BC1_UNORM for size
    "ixray": {"base": "BC7_UNORM", "bump": "BC3_UNORM", "bumpx": "BC3_UNORM"},
}

# ------------------------------------------------------------------------------------------


def load(path, mode):
    """mode 'L': a grey map as float [0,1]. Colour files use their red channel (grey maps saved
    as RGB have R = G = B; a luminance conversion would be the same up to rounding)."""
    im = Image.open(path)
    if mode == "L":
        if im.mode in ("I;16", "I;16B", "I"):
            return np.asarray(im, dtype=np.float32) / 65535.0
        if im.mode == "L":
            return np.asarray(im, dtype=np.float32) / 255.0
        return np.asarray(im.convert("RGB"), dtype=np.float32)[..., 0] / 255.0
    return np.asarray(im.convert(mode), dtype=np.float32) / 255.0


def decode_normal(rgb_or_xy, directx_input=False):
    """-> unit normals, OpenGL convention (Y up) unless the input is kept as is (ixray --dx-normal)"""
    n = rgb_or_xy * 2.0 - 1.0
    if n.shape[-1] == 2:
        z = np.sqrt(np.clip(1.0 - (n ** 2).sum(-1), 0, 1))
        n = np.dstack([n, z])
    if FLIP_GREEN != directx_input:
        n[..., 1] = -n[..., 1]
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    return n


def mip_chain(level0, kind):
    """kind: 'color' (box), 'normal' (box + renormalise), returns list of arrays"""
    levels = [level0]
    a = level0
    while a.shape[0] > 1 or a.shape[1] > 1:
        h, w = a.shape[:2]
        h2, w2 = max(1, h // 2), max(1, w // 2)
        a = a[: h2 * 2 if h > 1 else 1, : w2 * 2 if w > 1 else 1]
        if h > 1:
            a = 0.5 * (a[0::2] + a[1::2])
        if w > 1:
            a = 0.5 * (a[:, 0::2] + a[:, 1::2])
        levels.append(a)
    if kind == "normal":
        levels = [l / np.maximum(np.linalg.norm(l, axis=-1, keepdims=True), 1e-6) for l in levels]
    return levels


def write_dds_argb(path, mips_rgba):
    """Uncompressed DDS, legacy A8R8G8B8 (B,G,R,A bytes), full mip chain. Linear, no sRGB."""
    h, w = mips_rgba[0].shape[:2]
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT = 0x1, 0x2, 0x4, 0x8, 0x1000, 0x20000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT
    pf = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = 0x1000 | 0x8 | 0x400000  # TEXTURE | COMPLEX | MIPMAP
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, w * 4, 0, len(mips_rgba)) + b"\0" * 44 + pf + \
        struct.pack("<IIIII", caps, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(header)
        for m in mips_rgba:
            u8 = (np.clip(m, 0, 1) * 255.0 + 0.5).astype(np.uint8)
            f.write(u8[..., [2, 1, 0, 3]].tobytes())   # BGRA


def stats(name, a, mask):
    v = a[mask] if mask is not None and mask.any() else a.ravel()
    return f"{name:10s} mean {v.mean():.2f}  p5 {np.percentile(v, 5):.2f}  p95 {np.percentile(v, 95):.2f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="output base name, e.g. wpn_deagle_mw")
    ap.add_argument("--albedo", required=True)
    ap.add_argument("--metal", required=True)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--gloss")
    g.add_argument("--roughness")
    n = ap.add_mutually_exclusive_group(required=True)
    n.add_argument("--normal")
    n.add_argument("--nog", help="CoD packed: R gloss, G normal Y, B normal Z, A normal X")
    ap.add_argument("--ao")
    ap.add_argument("--height")
    ap.add_argument("--sss", help="ixray only: subsurface map")
    ap.add_argument("--skin-sss", type=float, help="atmos: one subsurface value 0..1 for the whole texture")
    ap.add_argument("--layout", choices=("atmos", "ixray"), default="atmos",
                    help="atmos: material = 4.5, the standard (default); ixray: material = pbr")
    ap.add_argument("--input-normal", choices=("opengl", "directx"), default="opengl",
                    help="normal map convention of --normal / --nog (default opengl)")
    ap.add_argument("--keep-albedo-alpha", action="store_true", help="keep the albedo's alpha (alpha test)")
    ap.add_argument("--dx-normal", action="store_true", help="ixray only: keep a DirectX normal map as is and write USE_DX_NORMAL_MAP = 1")
    ap.add_argument("--out", default="packed")
    ap.add_argument("--png", action="store_true", help="also write PNG previews of the packed textures")
    ap.add_argument("--texconv", help="path to texconv.exe: write block-compressed DDS instead")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if not (a.gloss or a.roughness or a.nog):
        sys.exit("error: give --gloss, --roughness, or --nog (its red channel is gloss)")
    atmos = a.layout == "atmos"
    if atmos and a.sss:
        sys.exit("error: the atmos packing has no subsurface channel; use --skin-sss VALUE (one value for the texture)")
    if atmos and a.dx_normal:
        sys.exit("error: --dx-normal is for --layout ixray; with atmos say --input-normal directx")
    if not atmos and a.skin_sss is not None:
        sys.exit("error: --skin-sss is for --layout atmos; the ixray packing takes a --sss map")
    if a.skin_sss is not None and not 0.0 <= a.skin_sss <= 1.0:
        sys.exit("error: --skin-sss must be 0..1")
    # internal normals: OpenGL, except ixray --dx-normal (kept DirectX, the .ltx says so)
    dx_in = a.input_normal == "directx" and not a.dx_normal

    alb_img = Image.open(a.albedo)
    size = alb_img.size
    alb = np.asarray(alb_img.convert("RGBA"), dtype=np.float32) / 255.0
    met = load(a.metal, "L")

    if a.nog:
        nog = np.asarray(Image.open(a.nog).convert("RGBA"), dtype=np.float32) / 255.0
        gloss = nog[..., 0]
        N = decode_normal(np.dstack([nog[..., 3], nog[..., 1], nog[..., 2]]), dx_in)
        rough_src = "gloss (packed R)" if not (a.gloss or a.roughness) else None
    else:
        N = decode_normal(np.asarray(Image.open(a.normal).convert("RGB"), dtype=np.float32) / 255.0, dx_in)
        gloss = None
        rough_src = None

    if a.roughness:
        rough = load(a.roughness, "L")
        rough_src = "roughness map"
    else:
        if a.gloss:
            gloss = load(a.gloss, "L")
            rough_src = "gloss map"
        rough = 1.0 - gloss
    rough = np.clip(rough, 0, 1) ** ROUGHNESS_GAMMA
    rough = np.maximum(rough, ROUGHNESS_MIN)

    ao = load(a.ao, "L") if a.ao else np.full(met.shape, DEFAULT_AO, np.float32)
    height = load(a.height, "L") if a.height else np.full(met.shape, DEFAULT_HEIGHT, np.float32)
    sss = load(a.sss, "L") if a.sss else np.full(met.shape, DEFAULT_SSS, np.float32)

    shapes = {k: v.shape[:2] for k, v in dict(albedo=alb, metal=met, rough=rough, normal=N, ao=ao, height=height, sss=sss).items()}
    if len(set(shapes.values())) != 1:
        sys.exit(f"error: input sizes differ: {shapes}")
    h, w = met.shape
    if (w % 4) or (h % 4):
        print(f"WARNING: {w}x{h} is not a multiple of 4; block compression (texconv) will pad or fail")

    metal_mask = met > 0.5
    print(f"{a.name}: {w}x{h}, {a.layout} packing, roughness from {rough_src}, normal from "
          f"{'packed CoD texture' if a.nog else a.normal} ({a.input_normal}{', FLIP_GREEN' if FLIP_GREEN else ''})")
    print(f"metal pixels: {metal_mask.mean() * 100:.0f}%")
    print("  " + stats("metal", met, None))
    print("  " + stats("roughness", rough, metal_mask) + "   (over metal pixels)")
    print("  " + stats("ao", ao, None))
    if a.roughness:
        m = load(a.roughness, "L")[metal_mask]
        if m.size and m.mean() > GLOSS_WARNING_MEAN:
            print(f"WARNING: the roughness map averages {m.mean():.2f} on metal (rough metal). Exports labelled 'roughness'"
                  " from CoD are usually gloss; if polished parts are bright in it, pass it as --gloss.")
    nl = np.linalg.norm(N, axis=-1)
    print(f"  normal     mean length {nl.mean():.3f}, mean Z {N[..., 2].mean():.3f}")

    write_ltx = atmos or a.dx_normal
    outs = [f"{a.name}.dds", f"{a.name}_bump.dds", f"{a.name}_bump#.dds"] + ([f"{a.name}.ltx"] if write_ltx else [])
    print("would write" if a.dry_run else "writing", "->", a.out + ":", ", ".join(outs),
          "(texconv BC)" if a.texconv else "(uncompressed A8R8G8B8 + mips)")
    if a.dry_run:
        return 0

    os.makedirs(a.out, exist_ok=True)
    base = alb.copy()
    if not a.keep_albedo_alpha:
        base[..., 3] = 1.0

    # Mip chains: normals renormalised; roughness widened by the lost normal variance
    n_mips = mip_chain(N, "normal")
    raw_mips = mip_chain(N, "color")            # unnormalised average -> length < 1 where normals vary
    r_mips = mip_chain(rough, "color")
    if TOKSVIG_MIPS:
        out = []
        for r, raw in zip(r_mips, raw_mips):
            ln = np.clip(np.linalg.norm(raw, axis=-1), 1e-3, 1.0)
            a2 = (r * r) ** 2                    # alpha^2, alpha = roughness^2
            var = (1.0 - ln) / ln                # Toksvig
            out.append(np.clip(np.sqrt(np.sqrt(np.clip(a2 + var, 0, 1))), ROUGHNESS_MIN, 1.0))
        r_mips = out
    others = [mip_chain(x, "color") for x in (met, sss, ao, height)]

    if atmos:
        # R = X, G = Y DirectX style (internal normals are OpenGL: flip), B = height, A unused
        one = [np.ones_like(hm) for hm in others[3]]
        bump_m = [np.dstack([nm[..., 0] * 0.5 + 0.5, -nm[..., 1] * 0.5 + 0.5, hm, o])
                  for nm, hm, o in zip(n_mips, others[3], one)]
        bumpx_m = [np.dstack([am, rm, mm, o]) for mm, rm, am, o in zip(others[0], r_mips, others[2], one)]
    else:
        bump_m = [np.dstack([hm, nm[..., 1] * 0.5 + 0.5, np.zeros_like(hm), nm[..., 0] * 0.5 + 0.5])
                  for nm, hm in zip(n_mips, others[3])]
        bumpx_m = [np.dstack([mm, rm, sm, am]) for mm, rm, sm, am in zip(others[0], r_mips, others[1], others[2])]
    base_m = mip_chain(base, "color")

    targets = {"base": (f"{a.name}.dds", base_m), "bump": (f"{a.name}_bump.dds", bump_m), "bumpx": (f"{a.name}_bump#.dds", bumpx_m)}
    for key, (fname, mips) in targets.items():
        path = os.path.join(a.out, fname)
        if a.texconv:
            if not shutil.which(a.texconv) and not os.path.exists(a.texconv):
                sys.exit(f"error: texconv not found: {a.texconv}")
            tmp = os.path.join(a.out, fname.replace("#", "_X_") + ".png")
            Image.fromarray((np.clip(mips[0], 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(tmp)
            subprocess.run([a.texconv, "-f", TEXCONV_FORMATS[a.layout][key], "-m", "0", "-y", "-nologo", "-o", a.out, tmp], check=True)
            produced = os.path.join(a.out, os.path.splitext(os.path.basename(tmp))[0] + ".dds")
            os.replace(produced, path)
            os.remove(tmp)
        else:
            write_dds_argb(path, mips)
        if a.png:
            Image.fromarray((np.clip(mips[0], 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(
                os.path.join(a.out, fname.replace("#", "_X_") + ".preview.png"))
        print("  wrote", path)
    if atmos:
        ltx = "[texture]\nmaterial  = 4.5\nbump_mode = use\n"
        if a.skin_sss is not None:
            ltx += f"\n[shaders_options]\nSKIN_SSS = {a.skin_sss:g}\n"
    elif a.dx_normal:
        ltx = "[shaders_options]\nUSE_DX_NORMAL_MAP = 1\n"
    if write_ltx:
        with open(os.path.join(a.out, f"{a.name}.ltx"), "w") as f:
            f.write(ltx)
        print("  wrote", os.path.join(a.out, f"{a.name}.ltx"))
    if not atmos:
        print("Now set the base texture's THM: material 'PBR (metallic/roughness)', bump name", f"{a.name}_bump")
    return 0


if __name__ == "__main__":
    sys.exit(main())
