#!/usr/bin/env python3
"""
suggest_hud_reflect.py - pick HUD_REFLECT / WORLD_REFLECT for PBR textures from their roughness,
and write it into each texture's .ltx ([shaders_options], see gamedata/shaders/r3/pbr_hud.h).

How the option works (pbr_hud.h): for PBR textures the shader moves the roughness towards 1,
    rough' = lerp( 1, rough, k ) = 1 - k * ( 1 - rough )
so the k that brings a texture's typical roughness to a target is
    k = ( 1 - target ) / ( 1 - rough )
This tool measures the roughness of each _bump# (green channel; R metalness, B subsurface,
A AO), takes a robust statistic of it (the median by default), and computes that k.

Why the default target is 0.86: in the IX-Ray environment BRDF (ixray_brdf.h,
ixr_env_brdf_approx) the share of the ambient that is sky reflection for a non-metal falls
quickly up to a roughness of about 0.85 and is nearly flat after it (glancing view: 0.12 at
rough 0.61, 0.068 at 0.86, 0.055 at 1.0). Rougher than that only flattens the highlights;
the reflection cannot get much weaker by roughness alone (F0 stays 0.04 in metal / rough).

Usage
  python suggest_hud_reflect.py <texture or _bump# .dds | folder> [...] [--preset skin]
      [--target 0.86] [--stat p50] [--world] [--sss 0.8] [--write] [--force] [--dry-run]

  Each argument is a texture (femme_arm_right.dds), its _bump# (femme_arm_right_bump#.dds) or a
  folder (searched recursively for *_bump#.dds). Exported _bump# copies renamed by a download
  ("x_bump.dds" with the PBR layout) can be passed with --bump-file.
  Without --write it only prints the suggestion (same as --dry-run). With --write it adds or
  replaces the line in <texture>.ltx, keeping every other line; a value already in the file is
  kept unless --force.

Presets (target roughness after the option): skin 0.86, cloth 0.90, leather 0.70, rubber 0.80,
plastic 0.60. --target overrides the preset.

Requirements: numpy, texture2ddecoder (pip install numpy texture2ddecoder) for BC1 / BC3 / BC4 /
BC5 / BC7 textures; uncompressed RGBA8 / BGRA8 DDS need numpy only.

Caveats
  - Only for PBR textures (THM material "PBR" or .ltx material = pbr). On a legacy texture the
    _bump# is an error-correction map and the result means nothing; the tool cannot tell the
    two apart from pixels alone (it warns when green looks like a normal channel, ~0.5 flat).
  - The statistic covers the whole texture, including UV space no triangle uses. On sparse
    atlases the empty space (often a flat colour) can skew it: --stat p50 is the most robust,
    --mask-alpha ignores texels whose AO (alpha) is exactly 0 or 1 when that marks empty space.
  - It reads one mip near --mip-size (default 256) for speed; roughness is smooth enough that
    this barely changes the statistic.
  - The value only changes roughness. If a texture still looks mirror-like at the suggested k,
    the reflection is not this ambient term (screen-space reflections or a direct highlight):
    lowering k further will not fix it.
  - X-Ray .ltx files are written with CRLF line ends and ';' comments, like the game's own.
"""

import argparse
import os
import re
import struct
import sys

import numpy as np

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

PRESETS = {"skin": 0.86, "cloth": 0.90, "leather": 0.70, "rubber": 0.80, "plastic": 0.60}
DEFAULT_PRESET = "skin"
DEFAULT_STAT = "p50"            # p50 (median), mean, p25, p75
DEFAULT_MIP_SIZE = 256          # read the mip closest to this size
K_MIN, K_MAX = 0.0, 1.0         # clamp of the written value
DECIMALS = 2
LTX_SECTION = "shaders_options"

# DXGI formats -> (decoder name in texture2ddecoder, bytes per 4x4 block) or "raw" for 32 bpp
DXGI = {
    71: ("decode_bc1", 8), 72: ("decode_bc1", 8),
    77: ("decode_bc3", 16), 78: ("decode_bc3", 16),
    80: ("decode_bc4", 8), 81: ("decode_bc4", 8),
    83: ("decode_bc5", 16), 84: ("decode_bc5", 16),
    98: ("decode_bc7", 16), 99: ("decode_bc7", 16),
    28: "raw_rgba", 29: "raw_rgba", 87: "raw_bgra", 91: "raw_bgra",
}
FOURCC = {b"DXT1": ("decode_bc1", 8), b"DXT5": ("decode_bc3", 16), b"ATI1": ("decode_bc4", 8),
          b"BC4U": ("decode_bc4", 8), b"ATI2": ("decode_bc5", 16), b"BC5U": ("decode_bc5", 16)}

# ------------------------------------------------------------------------------------------


def read_dds(path, mip_size):
    """RGBA float32 [0,1] of the mip closest to mip_size"""
    d = open(path, "rb").read()
    if d[:4] != b"DDS ":
        raise ValueError("not a DDS file")
    h = struct.unpack("<31I", d[4:128])
    height, width, mips = h[2], h[3], max(1, h[6])
    pf_flags, fourcc, bits = h[19], d[84:88], h[20]
    off = 128
    fmt = None
    if fourcc == b"DX10":
        dxgi = struct.unpack("<I", d[128:132])[0]
        off = 148
        fmt = DXGI.get(dxgi)
        if fmt is None:
            raise ValueError(f"DXGI format {dxgi} not supported")
    elif pf_flags & 0x4:
        fmt = FOURCC.get(fourcc)
        if fmt is None:
            raise ValueError(f"FourCC {fourcc!r} not supported")
    elif pf_flags & 0x40 and bits == 32:
        masks = h[21:25]
        fmt = "raw_rgba" if masks[0] == 0xFF else "raw_bgra"
    else:
        raise ValueError("unsupported pixel format")

    def level_size(w, hh):
        if isinstance(fmt, tuple):
            return max(1, (w + 3) // 4) * max(1, (hh + 3) // 4) * fmt[1]
        return w * hh * 4

    w, hh, level = width, height, 0
    while level + 1 < mips and max(w, hh) // 2 >= mip_size:
        off += level_size(w, hh)
        w, hh, level = max(1, w // 2), max(1, hh // 2), level + 1
    data = d[off:off + level_size(w, hh)]
    if isinstance(fmt, tuple):
        try:
            import texture2ddecoder as t2d
        except ImportError:
            raise ValueError("block-compressed texture: pip install texture2ddecoder")
        bgra = np.frombuffer(getattr(t2d, fmt[0])(data, w, hh), np.uint8).reshape(hh, w, 4)
        rgba = bgra[..., [2, 1, 0, 3]]
    else:
        px = np.frombuffer(data, np.uint8).reshape(hh, w, 4)
        rgba = px if fmt == "raw_rgba" else px[..., [2, 1, 0, 3]]
    return rgba.astype(np.float32) / 255.0, (width, height, w, hh)


def stat(v, how):
    return {"p50": np.median(v), "mean": v.mean(), "p25": np.percentile(v, 25),
            "p75": np.percentile(v, 75)}[how]


def texture_base(path):
    """femme_arm_right(_bump#).dds -> folder/femme_arm_right"""
    b = re.sub(r"\.dds$", "", path, flags=re.I)
    return re.sub(r"_bump#$", "", b, flags=re.I)


def collect(args):
    out = []
    for p in args:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in sorted(files):
                    if f.lower().endswith("_bump#.dds"):
                        out.append((texture_base(os.path.join(root, f)), os.path.join(root, f)))
        else:
            base = texture_base(p)
            bump = base + "_bump#.dds"
            out.append((base, bump if os.path.exists(bump) else p))
    return out


def update_ltx(text, key, value, force):
    """add / replace key in [shaders_options] of an .ltx text; returns (new text, old value or None, changed)"""
    lines = text.replace("\r\n", "\n").split("\n") if text else []
    sec_start = next((i for i, l in enumerate(lines) if re.match(rf"\s*\[{LTX_SECTION}\]", l, re.I)), None)
    newline = f"{key} = {value}"
    old = None
    if sec_start is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines += [f"[{LTX_SECTION}]", newline]
        return "\r\n".join(lines).rstrip("\r\n") + "\r\n", None, True
    end = next((i for i in range(sec_start + 1, len(lines)) if re.match(r"\s*\[", lines[i])), len(lines))
    for i in range(sec_start + 1, end):
        m = re.match(rf"\s*{key}\s*=\s*([^;\s]+)", lines[i], re.I)
        if m:
            old = m.group(1)
            if not force:
                return text, old, False
            lines[i] = newline
            break
    else:
        ins = end
        while ins > sec_start + 1 and not lines[ins - 1].strip():
            ins -= 1
        lines.insert(ins, newline)
    return "\r\n".join(lines).rstrip("\r\n") + "\r\n", old, True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--bump-file", action="append", default=[],
                    help="TEXTURE=BUMPFILE: use BUMPFILE as TEXTURE's _bump# (renamed copies)")
    ap.add_argument("--preset", choices=sorted(PRESETS), default=DEFAULT_PRESET)
    ap.add_argument("--target", type=float, help="target roughness (overrides --preset)")
    ap.add_argument("--stat", choices=["p50", "mean", "p25", "p75"], default=DEFAULT_STAT)
    ap.add_argument("--mip-size", type=int, default=DEFAULT_MIP_SIZE)
    ap.add_argument("--mask-alpha", action="store_true", help="ignore texels whose alpha (AO) is exactly 0 or 1")
    ap.add_argument("--world", action="store_true", help="write WORLD_REFLECT instead of HUD_REFLECT")
    ap.add_argument("--sss", type=float, help="also write SKIN_SSS = value when the subsurface channel is empty")
    ap.add_argument("--write", action="store_true", help="write the .ltx files (default: only print)")
    ap.add_argument("--force", action="store_true", help="replace values already in the .ltx")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    target = a.target if a.target is not None else PRESETS[a.preset]
    if not 0.0 < target < 1.0:
        sys.exit("error: --target must be between 0 and 1")
    key = "WORLD_REFLECT" if a.world else "HUD_REFLECT"
    jobs = collect(a.paths)
    for spec in a.bump_file:
        if "=" not in spec:
            sys.exit("error: --bump-file TEXTURE=BUMPFILE")
        tex, bump = spec.split("=", 1)
        jobs.append((texture_base(tex), bump))
    if not jobs:
        sys.exit("error: nothing to do (give textures, _bump# files or folders)")
    write = a.write and not a.dry_run
    print(f"target roughness {target:.2f} ({'--target' if a.target is not None else a.preset}), statistic {a.stat}, "
          f"{'writing' if write else 'dry run (nothing written)'}")

    bad = 0
    for base, bump in jobs:
        name = os.path.basename(base)
        try:
            img, (w0, h0, w, h) = read_dds(bump, a.mip_size)
        except (OSError, ValueError) as e:
            print(f"  {name}: {bump}: {e}")
            bad += 1
            continue
        metal, rough, sss, ao = (img[..., i].ravel() for i in range(4))
        keep = np.ones_like(rough, bool)
        if a.mask_alpha:
            keep = (ao > 0.0) & (ao < 1.0)
            if keep.mean() < 0.05:
                keep[:] = True
        r = float(stat(rough[keep], a.stat))
        k = float(np.clip((1.0 - target) / max(1.0 - r, 1e-4), K_MIN, K_MAX))
        k = round(k, DECIMALS)
        after = 1.0 - k * (1.0 - r)
        notes = []
        if r >= target:
            notes.append("already at least as rough as the target: 1 (no change)")
        if abs(np.median(rough) - 0.5) < 0.02 and rough.std() < 0.05:
            notes.append("green is a flat ~0.5: is this a legacy _bump# (normal map)? check the texture is PBR")
        if metal.mean() > 0.5:
            notes.append(f"mostly metal ({metal.mean():.2f}): roughness controls a metal's reflection, k may be too strong")
        sss_empty = float(np.percentile(sss, 95)) < 0.02
        print(f"  {name}: {w0}x{h0} (read {w}x{h}), roughness {a.stat} {r:.3f} (p5 {np.percentile(rough, 5):.3f}, "
              f"p95 {np.percentile(rough, 95):.3f}) -> {key} = {k:.{DECIMALS}f} (roughness becomes ~{after:.2f})"
              f"; subsurface {'empty' if sss_empty else f'median {np.median(sss):.2f}'}")
        for n in notes:
            print(f"      note: {n}")

        ltx = base + ".ltx"
        edits = [(key, f"{k:.{DECIMALS}f}")]
        if a.sss is not None and sss_empty:
            edits.append(("SKIN_SSS", f"{min(max(a.sss, 0.0), 1.0):.2f}"))
        text = open(ltx, encoding="cp1251", errors="replace").read() if os.path.exists(ltx) else ""
        any_change = False
        for ek, ev in edits:
            text, old, changed = update_ltx(text, ek, ev, a.force)
            if not changed:
                print(f"      {os.path.basename(ltx)}: {ek} = {old} already set, kept (--force to replace)")
                continue
            any_change = True
            what = f"replace {ek} = {old} with {ev}" if old else f"add {ek} = {ev}"
            print(f"      {os.path.basename(ltx)}: {'' if write else '[dry-run] would '}{what}")
        if write and any_change:
            with open(ltx, "w", encoding="cp1251", newline="") as f:
                f.write(text)
    print("restart the game after writing (the value is part of the shader name, no cache clear needed)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
