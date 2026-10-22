#!/usr/bin/env python3
"""
make_hud_blood_overlay.py - build the "HUD blood (experiment)" mod: blood on the player's hands
and weapons that grows with hits, bleeding and close kills, dries, flakes off, washes off in the
rain and can be wiped off with a cloth.

It takes the HUD G-buffer shaders that win in YOUR shader setup (SSS's deffer_base_hud_bump.ps
and deffer_base_hud_aref_bump.ps, as patched by the PBR overlay), adds the blood call to them,
and bundles everything the experiment needs into one mod to put ABOVE the PBR overlay:

  gamedata/shaders/r3/hud_blood.h                  the blood shader (tools/hud_blood)
  gamedata/shaders/r3/deffer_base_hud_bump.ps      yours + #include "hud_blood.h" + the call
  gamedata/shaders/r3/deffer_base_hud_aref_bump.ps yours + the same
  gamedata/scripts/hud_blood.script                the state (hits, drying, rain, wiping, saving)
  gamedata/configs/plugins/hud_blood.ltx           all the tuning
  gamedata/configs/plugins/hud_blood_atlas.ltx     the atlas tiles (from --atlas)
  gamedata/configs/mod_system_hud_blood.ltx        "Wipe off blood" on cloths + bloody versions
  gamedata/configs/text/{eng,rus}/st_hud_blood.xml
  gamedata/textures/hud_blood/*.dds                the atlas (from --atlas)

Usage
  python make_hud_blood_overlay.py --pack PBR_overlay_GAMMA.zip [--stack PBR_shaders_GAMMA.zip] \\
      --atlas blood_atlas_out --out "HUD blood.zip" [--dry-run]
  python make_hud_blood_overlay.py --ps my/deffer_base_hud_bump.ps --ps my/deffer_base_hud_aref_bump.ps \\
      --atlas blood_atlas_out --out "HUD blood.zip"
  --pack / --stack  overlay or full pack (zip or folder) to take the HUD shaders from (--stack is
                    searched when --pack has none: an overlay only has the files PBR changes)
  --ps              your own winning HUD shader file (MO2 right pane > Data > shaders/r3 shows which
                    mod wins); repeat for both. Recommended when your SSS differs from the pack's
  --atlas           output folder of tools/make_blood_atlas.py (textures/ and configs/ inside)
  --dry-run         check the anchors and list what would be written

Needs in game
  - An exe with the shader bus 7+ and the HUD blood texture binding (branch
    pbr-test-s3ds-mt-sss-lass-blood-dlss). On another exe the shader compiles to nothing or reads
    an unbound atlas (no blood); nothing breaks.
  - SSS with its "HUD Raindrops" component: its HUD shaders pass the bind-pose position the
    stamps are placed with (I.RDrops). The tool stops on a HUD shader without it.
  - Delete appdata/shaders_cache/r4 after installing or changing the shaders.

Caveats
  - The patch looks for fixed lines (#include "sload.h", "Ne = normalize(Ne);"). If one is
    missing the tool stops and names the file; nothing is written.
  - Only the bump HUD shaders get blood; HUD models drawn with deffer_base_hud_flat.ps do not.
  - The -hq / _d variants include these two files, so they get blood too.
  - In-game test without waiting for a hit: debug console (Lua) hud_blood.add(0.8, 0.3), or the
    engine console "bus_force hud_blood 0.8 0.3 1 0.5" (spatter, smear, wet, drip; bus_release
    hud_blood to stop). hud_blood.view(1) shows the coverage in red.
"""

import argparse
import os
import re
import sys
import zipfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

MOD_FOLDER = "HUD blood (experiment)"
HERE = os.path.dirname(os.path.abspath(__file__))
BUNDLE_DIR = os.path.join(HERE, "hud_blood")          # gamedata/ tree shipped as is
HUD_SHADERS = ["gamedata/shaders/r3/deffer_base_hud_bump.ps", "gamedata/shaders/r3/deffer_base_hud_aref_bump.ps"]
ATLAS_FILES = ("gamedata/textures/hud_blood/", "gamedata/configs/plugins/hud_blood_atlas.ltx")
TAG = "// [HUD blood]"

INCLUDE_RE = re.compile(r'#include\s+"sload\.h"[^\n]*\n')
CALL_RE = re.compile(r'[ \t]*Ne\s*=\s*normalize\s*\(\s*Ne\s*\)\s*;[^\n]*\n')
ARGS = "Ne, I.position.xyz, I.RDrops.xyz, I.M1, I.M2, I.M3"
CALL_PBR = (f"#ifdef USE_PBR\n"
            f"\thud_blood_apply( S, pbr_hud, {ARGS} );\t{TAG}\n"
            f"#else\n"
            f"\t{{ float4 hb_pbr = float4( 0, 0.5, 1, 0 ); hud_blood_apply( S, hb_pbr, {ARGS} ); }}\t{TAG}\n"
            f"#endif\n")
CALL_LEGACY = f"\t{{ float4 hb_pbr = float4( 0, 0.5, 1, 0 ); hud_blood_apply( S, hb_pbr, {ARGS} ); }}\t{TAG}\n"

# ------------------------------------------------------------------------------------------


def read_files(path):
    """{lowercase path from gamedata/ on: (original path, bytes)} from a zip or a folder"""
    out = {}
    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for f in files:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, path).replace(os.sep, "/")
                i = rel.lower().find("gamedata/")
                if i < 0 and rel.lower().split("/")[0] in ("textures", "configs", "shaders", "scripts"):
                    rel, i = "gamedata/" + rel, 0
                if i >= 0:
                    with open(full, "rb") as fh:
                        out[rel[i:].lower()] = (rel[i:], fh.read())
    else:
        with zipfile.ZipFile(path) as z:
            for n in z.namelist():
                i = n.lower().find("gamedata/")
                if i >= 0 and not n.endswith("/"):
                    out[n[i:].lower()] = (n[i:], z.read(n))
    return out


def patch(name, text):
    crlf = "\r\n" in text
    t = text.replace("\r\n", "\n")
    if TAG in t:
        sys.exit(f"error: {name} already has the HUD blood patch (give the PBR overlay's file, not this mod's)")
    if "RDrops" not in t:
        sys.exit(f"error: {name} has no I.RDrops: it is not SSS's HUD Raindrops version. Install SSS's "
                 f"'HUD Raindrops' component (GAMMA has it) or give that file with --ps; nothing written")
    if len(INCLUDE_RE.findall(t)) != 1:
        sys.exit(f'error: {name}: #include "sload.h" not found once; nothing written')
    if len(CALL_RE.findall(t)) != 1:
        sys.exit(f"error: {name}: 'Ne = normalize(Ne);' not found once; nothing written")
    t = INCLUDE_RE.sub(lambda m: m.group(0) + f'#include "hud_blood.h"\t{TAG} blood on hands and weapons\n', t, count=1)
    call = CALL_PBR if "float4 pbr_hud" in t else CALL_LEGACY
    t = CALL_RE.sub(lambda m: m.group(0) + call, t, count=1)
    return t.replace("\n", "\r\n") if crlf else t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pack")
    ap.add_argument("--stack")
    ap.add_argument("--ps", action="append", default=[], help="your winning HUD shader (repeatable)")
    ap.add_argument("--atlas", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if not (a.pack or a.ps):
        sys.exit("error: give --pack (and --stack) or --ps")
    sources = {}
    for p in a.ps:
        base = os.path.basename(p).lower()
        key = next((k for k in HUD_SHADERS if k.endswith("/" + base)), None)
        if not key:
            sys.exit(f"error: --ps {p}: expected one of {', '.join(os.path.basename(k) for k in HUD_SHADERS)}")
        with open(p, "rb") as fh:
            sources[key] = (p, fh.read())
    pack = read_files(a.pack) if a.pack else {}
    stack = read_files(a.stack) if a.stack else {}
    for key in HUD_SHADERS:
        if key not in sources:
            src = pack.get(key) or stack.get(key)
            if src:
                sources[key] = (f"{a.pack if key in pack else a.stack}:{src[0]}", src[1])
    missing = [k for k in HUD_SHADERS if k not in sources]
    if missing:
        sys.exit(f"error: not found: {', '.join(missing)} (an overlay may lack them: add the full pack as --stack, or --ps)")

    files = {}
    for key, (origin, data) in sources.items():
        files[key] = patch(origin, data.decode("utf-8", "replace")).encode("utf-8")
        print(f"patched  {key}  <- {origin}")

    if not os.path.isdir(BUNDLE_DIR):
        sys.exit(f"error: {BUNDLE_DIR} missing (run the tool from the repository's tools folder)")
    for root, _, names in os.walk(BUNDLE_DIR):
        for n in names:
            full = os.path.join(root, n)
            rel = os.path.relpath(full, BUNDLE_DIR).replace(os.sep, "/")
            with open(full, "rb") as fh:
                files[rel] = fh.read()

    atlas = read_files(a.atlas)
    got = {k: v for k, v in atlas.items() if k.startswith(ATLAS_FILES[0]) or k == ATLAS_FILES[1]}
    if not any(k.endswith(".dds") for k in got) or ATLAS_FILES[1] not in got:
        sys.exit(f"error: --atlas {a.atlas}: needs textures/hud_blood/*.dds and configs/plugins/hud_blood_atlas.ltx "
                 f"(the output folder of make_blood_atlas.py)")
    for k, (orig, data) in got.items():
        files[orig] = data

    readme = (f"{MOD_FOLDER}\r\n\r\nPut this mod ABOVE the PBR overlay / full pack, delete appdata/shaders_cache/r4.\r\n"
              f"Tuning: gamedata/configs/plugins/hud_blood.ltx.\r\n\r\n" + __doc__.replace("\n", "\r\n"))
    files["README.txt"] = readme.encode("utf-8")
    for k in sorted(files):
        print(f"{'[dry-run] ' if a.dry_run else ''}{MOD_FOLDER}/{k}  ({len(files[k])} bytes)")
    if a.dry_run:
        return 0
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        for k, v in files.items():
            z.writestr(f"{MOD_FOLDER}/{k}", v)
    print("wrote", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
