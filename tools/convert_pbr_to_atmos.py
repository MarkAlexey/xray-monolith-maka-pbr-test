#!/usr/bin/env python3
"""
convert_pbr_to_atmos.py - convert PBR textures from the IX-Ray packing (LTX "material = pbr")
to the Atmospherics packing (LTX "material = 4.5"), the community standard.

    IX-Ray packing (input)                          Atmospherics packing (output)
    _bump   R = height, G = normal Y (OpenGL),      _bump   R = normal X, G = normal Y (DirectX),
            B = unused, A = normal X                        B = height, A unused (255)
    _bump#  R = metal, G = roughness,               _bump#  R = AO, G = roughness, B = metal,
            B = subsurface, A = AO                          A unused (255)
    .ltx    material = pbr                          .ltx    material = 4.5 (+ SKIN_SSS = value)

The albedo (<name>.dds) does not change and is not written.

Usage
  python convert_pbr_to_atmos.py SOURCE_DIR --out OUT_DIR --dry-run
  python convert_pbr_to_atmos.py SOURCE_DIR --out OUT_DIR
  python convert_pbr_to_atmos.py SOURCE_DIR --out OUT_DIR --bc7                    (BC7, any OS)
  python convert_pbr_to_atmos.py SOURCE_DIR --out OUT_DIR --texconv texconv.exe   (BC7, Windows)

SOURCE_DIR is searched recursively for <name>.ltx files whose [texture] section says
"material = pbr" (or 4). OUT_DIR gets <name>_bump.dds, <name>_bump#.dds and <name>.ltx at the
same relative paths; copy it over the mod (or install it as a mod above it).

Known edge cases / caveats
  - Textures described by a .thm (THM material "PBR") are not converted: the engine reads a
    texture's [texture] section only when it has no .thm. They are listed; to move one over,
    delete its .thm and give it an .ltx with "material = pbr" first (bump_mode = use).
  - Subsurface: the Atmospherics packing has no channel for it. A texture whose bump# blue is
    not empty (above BC noise, on 1%+ of it) gets SKIN_SSS = <its mean where it is set> in the
    .ltx; per-pixel variation is lost (reported when large). An existing SKIN_SSS in the .ltx is kept as is.
  - USE_DX_NORMAL_MAP = 1 in the .ltx means the map is already DirectX style: its green is kept,
    otherwise it is flipped. The option is removed (it means nothing for material = 4.5).
  - Uncompressed input (A8R8G8B8, as pack_pbr_textures.py writes): every mip level is converted
    exactly. Block-compressed input (BC1-BC7): the top level is decoded (Pillow) and the mips are
    rebuilt (box filter, normals renormalised); BC re-compression loses a little more.
  - Output DDS are uncompressed (exact) unless --bc7 (Python package etcpak: pip install etcpak;
    compresses this script's own mips) or --texconv (BC7_UNORM, never sRGB; texconv builds its
    own mips) is given. --bc7 averages under 1/255 of error but busy normal maps keep a few
    pixels off by 16+/255 (normal BC7 loss); texconv's BC7 is a little better.
  - Never writes into SOURCE_DIR. Existing files in OUT_DIR are overwritten.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

SSS_EMPTY = 8.0 / 255.0         # bump# blue at or below this = no subsurface (above BC noise)
SSS_MIN_COVERAGE = 0.01         # ...and it must cover at least this share of the texture
SSS_VARIATION_WARN = 0.10       # warn when the subsurface map's spread (std) is above this
TEXCONV_FORMAT = "BC7_UNORM"    # BC1_UNORM for size: the Atmospherics packing needs no alpha
BC7_UBER_LEVEL = 2              # --bc7 (etcpak / bc7enc) quality, 0..4; above 2 gains ~nothing

# ------------------------------------------------------------------------------------------


def read_ltx(path):
    with open(path, "r", encoding="latin-1") as f:
        return f.read()


def ltx_value(text, section, key):
    m = re.search(r"^\s*\[" + re.escape(section) + r"\][^\n]*\n(.*?)(?=^\s*\[|\Z)", text, re.M | re.S | re.I)
    if not m:
        return None
    k = re.search(r"^\s*" + re.escape(key) + r"\s*=\s*([^;\r\n]*)", m.group(1), re.M | re.I)
    return k.group(1).strip() if k else None


def is_ixray_pbr(text):
    v = ltx_value(text, "texture", "material")
    if v is None:
        return False
    if v.lower() == "pbr":
        return True
    try:
        return abs(float(v) - 4.0) < 1e-4
    except ValueError:
        return False


def converted_ltx(text, skin_sss):
    """material -> 4.5, USE_DX_NORMAL_MAP removed, SKIN_SSS added when given and absent"""
    out = re.sub(r"^(\s*material\s*=\s*)[^;\r\n]*", lambda m: m.group(1) + "4.5", text, count=1, flags=re.M | re.I)
    out = re.sub(r"^\s*USE_DX_NORMAL_MAP\s*=[^\r\n]*\r?\n?", "", out, flags=re.M | re.I)
    if skin_sss is not None and ltx_value(out, "shaders_options", "SKIN_SSS") is None:
        line = f"SKIN_SSS = {skin_sss:.2f}\n"
        if re.search(r"^\s*\[shaders_options\]", out, re.M | re.I):
            out = re.sub(r"(^\s*\[shaders_options\][^\n]*\n)", lambda m: m.group(1) + line, out, count=1, flags=re.M | re.I)
        else:
            out = out.rstrip("\r\n") + "\n\n[shaders_options]\n" + line
    return out


def read_dds(path):
    """-> (list of RGBA uint8 mip levels, exact) ; exact = every stored level was read"""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"DDS ":
        raise ValueError("not a DDS file")
    h, w = struct.unpack_from("<II", b, 12)
    mips = max(1, struct.unpack_from("<I", b, 28)[0])
    pf_flags, fourcc, bits = struct.unpack_from("<I4sI", b, 80)
    masks = struct.unpack_from("<4I", b, 92)
    if pf_flags & 0x40 and bits == 32 and masks == (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000):
        levels, o = [], 128
        for m in range(mips):
            mw, mh = max(1, w >> m), max(1, h >> m)
            n = mw * mh * 4
            if o + n > len(b):
                break
            levels.append(np.frombuffer(b, np.uint8, n, o).reshape(mh, mw, 4)[..., [2, 1, 0, 3]].copy())
            o += n
        return levels, len(levels) == mips
    im = Image.open(path)
    im.load()
    return [np.asarray(im.convert("RGBA")).copy()], False


def box_mips(level0, normal=False):
    """float RGBA level 0 -> full chain; normal=True renormalises the RG(B) vector each level"""
    levels, a = [level0], level0
    while a.shape[0] > 1 or a.shape[1] > 1:
        h, w = a.shape[:2]
        if h > 1:
            a = 0.5 * (a[0:h // 2 * 2:2] + a[1:h // 2 * 2:2])
        if w > 1:
            a = 0.5 * (a[:, 0:w // 2 * 2:2] + a[:, 1:w // 2 * 2:2])
        if normal:
            v = a[..., :2] * 2.0 - 1.0
            z = np.sqrt(np.clip(1.0 - (v ** 2).sum(-1), 0, 1))
            n = np.dstack([v, z])
            n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
            a = a.copy()
            a[..., :2] = n[..., :2] * 0.5 + 0.5
        levels.append(a)
    return levels


def write_dds_argb(path, levels_u8):
    h, w = levels_u8[0].shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
    pf = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, w * 4, 0, len(levels_u8)) + b"\0" * 44 + pf + \
        struct.pack("<IIIII", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(header)
        for m in levels_u8:
            f.write(m[..., [2, 1, 0, 3]].tobytes())


def swizzle_bump(lv, dx_normal):
    """IX-Ray bump (R height, G Y, A X) -> Atmospherics bump (R X, G Y DirectX, B height, A 255)"""
    g = lv[..., 1] if dx_normal else (255 - lv[..., 1])
    return np.dstack([lv[..., 3], g, lv[..., 0], np.full(lv.shape[:2], 255, np.uint8)]).astype(np.uint8)


def swizzle_bumpx(lv):
    """IX-Ray bump# (R metal, G rough, B sss, A AO) -> Atmospherics bump# (R AO, G rough, B metal, A 255)"""
    return np.dstack([lv[..., 3], lv[..., 1], lv[..., 0], np.full(lv.shape[:2], 255, np.uint8)]).astype(np.uint8)


def write_dds_bc7(path, levels_u8):
    """BC7_UNORM DDS (DX10 header), every given mip level compressed with etcpak (bc7enc)."""
    import etcpak
    params = etcpak.BC7CompressBlockParams()
    params.init_linear_weights()        # data maps, not colour: no perceptual weighting
    params.m_uber_level = BC7_UBER_LEVEL
    params.m_max_partitions = 64
    params.m_try_least_squares = True
    h, w = levels_u8[0].shape[:2]
    blobs = []
    for m in levels_u8:
        mh, mw = m.shape[:2]
        ph, pw = (mh + 3) // 4 * 4, (mw + 3) // 4 * 4
        if (ph, pw) != (mh, mw):  # levels below 4x4: pad by repeating the edge
            m = np.pad(m, ((0, ph - mh), (0, pw - mw), (0, 0)), mode="edge")
        blobs.append(etcpak.compress_bc7(np.ascontiguousarray(m).tobytes(), pw, ph, params))
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000   # CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT LINEARSIZE
    pf = struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, len(blobs[0]), 0, len(blobs)) + b"\0" * 44 + pf + \
        struct.pack("<IIIII", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    dx10 = struct.pack("<IIIII", 98, 3, 0, 1, 0)                  # BC7_UNORM, TEXTURE2D, array size 1
    with open(path, "wb") as f:
        f.write(header + dx10 + b"".join(blobs))


def save(path, levels_u8, texconv, bc7=False):
    if bc7:
        write_dds_bc7(path, levels_u8)
        return
    if not texconv:
        write_dds_argb(path, levels_u8)
        return
    tmp = path.replace("#", "_X_") + ".png"
    Image.fromarray(levels_u8[0], "RGBA").save(tmp)
    subprocess.run([texconv, "-f", TEXCONV_FORMAT, "-m", "0", "-y", "-nologo", "-o", os.path.dirname(path), tmp], check=True)
    os.replace(os.path.splitext(tmp)[0] + ".dds", path)
    os.remove(tmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="folder searched recursively (e.g. a mod's gamedata/textures)")
    ap.add_argument("--out", required=True, help="output folder (same relative paths)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--bc7", action="store_true", help="write BC7 DDS with etcpak (pip install etcpak)")
    g.add_argument("--texconv", help="texconv.exe: write BC7 instead of uncompressed DDS")
    ap.add_argument("--dry-run", action="store_true", help="report only, write nothing")
    a = ap.parse_args()

    src, out = os.path.abspath(a.source), os.path.abspath(a.out)
    if out == src:
        sys.exit("error: --out must not be the source folder (the inputs are never overwritten)")
    if os.path.commonpath([src, out]) == src:
        print("note: --out is inside the source folder; it is skipped while scanning")
    if a.texconv and not (shutil.which(a.texconv) or os.path.exists(a.texconv)):
        sys.exit(f"error: texconv not found: {a.texconv}")
    if a.bc7:
        try:
            import etcpak  # noqa: F401
        except ImportError:
            sys.exit("error: --bc7 needs the etcpak package: pip install etcpak")

    todo, thm_pbr, problems = [], [], []
    for root, dirs, files in os.walk(src):
        if os.path.abspath(root).startswith(out):
            dirs[:] = []
            continue
        lower = {f.lower(): f for f in files}
        for f in files:
            if not f.lower().endswith(".ltx"):
                continue
            stem = f[:-4]
            text = read_ltx(os.path.join(root, f))
            if not is_ixray_pbr(text):
                continue
            if stem.lower() + ".thm" in lower:
                thm_pbr.append(os.path.relpath(os.path.join(root, f), src))
                continue
            todo.append((root, stem, text, lower))

    print(f"{len(todo)} texture(s) with material = pbr under {src}")
    written = 0
    for root, stem, text, lower in todo:
        rel = os.path.relpath(os.path.join(root, stem), src)
        bump_name = ltx_value(text, "texture", "bump_name")
        if bump_name:
            print(f"  ! {rel}: bump_name = {bump_name} (a shared bump texture); convert that one by hand")
            problems.append(rel)
            continue
        names = {k: lower.get((stem + suf).lower()) for k, suf in (("bump", "_bump.dds"), ("bumpx", "_bump#.dds"))}
        if not all(names.values()):
            print(f"  ! {rel}: missing {', '.join(stem + s for k, s in (('bump', '_bump.dds'), ('bumpx', '_bump#.dds')) if not names[k])}")
            problems.append(rel)
            continue
        try:
            bump, bump_exact = read_dds(os.path.join(root, names["bump"]))
            bumpx, bumpx_exact = read_dds(os.path.join(root, names["bumpx"]))
        except Exception as e:  # unreadable or unsupported DDS
            print(f"  ! {rel}: cannot read ({e})")
            problems.append(rel)
            continue

        dx = (ltx_value(text, "shaders_options", "USE_DX_NORMAL_MAP") or "0").strip() not in ("0", "")
        sss = bumpx[0][..., 2].astype(np.float32) / 255.0
        skin_sss, note = None, ""
        used = sss[sss > SSS_EMPTY]
        if used.size >= SSS_MIN_COVERAGE * sss.size and ltx_value(text, "shaders_options", "SKIN_SSS") is None:
            skin_sss = float(used.mean())
            note = f", subsurface -> SKIN_SSS = {skin_sss:.2f} (on {used.size / sss.size * 100:.0f}% of the texture)"
            if used.std() > SSS_VARIATION_WARN:
                note += f" WARNING: it varies (std {used.std():.2f}), per-pixel detail is lost"

        new_bump = [swizzle_bump(l, dx) for l in bump]
        new_bumpx = [swizzle_bumpx(l) for l in bumpx]
        if not bump_exact:
            new_bump = [np.clip(m * 255 + 0.5, 0, 255).astype(np.uint8)
                        for m in box_mips(new_bump[0].astype(np.float32) / 255.0, normal=True)]
        if not bumpx_exact:
            new_bumpx = [np.clip(m * 255 + 0.5, 0, 255).astype(np.uint8)
                         for m in box_mips(new_bumpx[0].astype(np.float32) / 255.0)]
        h, w = new_bump[0].shape[:2]
        how = "exact, all mips" if bump_exact and bumpx_exact else "decoded top level, mips rebuilt"
        print(f"  {rel}: {w}x{h}, normal {'DirectX (kept)' if dx else 'OpenGL -> DirectX'}, {how}{note}")
        if a.dry_run:
            continue
        dst = os.path.join(out, os.path.relpath(root, src))
        os.makedirs(dst, exist_ok=True)
        save(os.path.join(dst, stem + "_bump.dds"), new_bump, a.texconv, a.bc7)
        save(os.path.join(dst, stem + "_bump#.dds"), new_bumpx, a.texconv, a.bc7)
        with open(os.path.join(dst, stem + ".ltx"), "w", encoding="latin-1", newline="") as f:
            f.write(converted_ltx(text, skin_sss))
        written += 1

    for t in thm_pbr:
        print(f"  skipped (has a .thm, the engine ignores its [texture] section): {t}")
    if problems:
        print(f"{len(problems)} texture(s) not converted, see '!' above")
    print("dry run: nothing written" if a.dry_run else f"wrote {written} texture(s) to {out}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
