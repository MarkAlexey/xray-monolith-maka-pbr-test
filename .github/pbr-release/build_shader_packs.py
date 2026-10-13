#!/usr/bin/env python3
"""
build_shader_packs.py - build the ready-made GAMMA shader zips for a PBR-Test release.

Used by .github/workflows/pbr-build.yml (job "shader_packs"); also runs locally with the same
sources folder. Produces, in --out:

  PBR_overlay_GAMMA.zip   only the files the PBR pipeline needs, patched from GAMMA's own stack
                          (tools/make_pbr_overlay.py). Installed last, on top of GAMMA.
  PBR_shaders_GAMMA.zip   GAMMA's whole shader stack with PBR merged in
                          (tools/build_shader_pack.py). Replaces GAMMA's shader mods.

Both are built on the stack GAMMA uses: Enhanced Shaders (Atmospherics' module set) -> SSS 23.5
(+ its Beef's NVG patch) -> Atmospherics -> GAMMA's own shader overrides. Atmospherics is
required: GAMMA's weathers are tuned for its shadow.h and pbr_settings.h, and without them the
world renders far too dark.

Sources folder (the private sources repository's checkout), names are case-sensitive:

  es/ or es.zip / es.7z                       Enhanced Shaders and Color Grading 1.10
  sss/ or sss.zip / sss.7z                    Screen Space Shaders 23.5
  atmospherics/ or atmospherics.zip / .7z     Atmospherics (the full mod or just its shaders)
  beef/ or beef.zip / beef.7z                 Beef's NVGs (optional, GAMMA ships it)
  gamma/                                      GAMMA's shader overrides: one folder/zip/7z each
  gamma/order.txt                             their names, lowest priority first, one per line
                                              (# comments allowed). Required when there is
                                              more than one: their order decides which
                                              depth_of_field.ps etc. wins.
  vanilla/  (optional)                        Anomaly's unpacked shaders (any folder containing
                                              shaders/r3). Enables the DXC compile check.

Only gamedata/shaders matters (the packs are shaders-only), so the archives may be stripped
to their shaders folders to stay under GitHub's 100 MB file limit (or use Git LFS).

Compile check (with --dxc and vanilla/): every PBR-relevant shader is compiled with DXC for
both zips, against the same stack without PBR as the baseline. Only NEW errors fail the build
(SSS / ES contain a few lines DXC rejects but the game's FXC accepts). DXC is not FXC: a clean
compile catches syntax, type and missing-identifier errors, not everything FXC might reject.

Usage
  python build_shader_packs.py --sources shader-sources --out shader_packs [--dxc dxc/bin/dxc]
  --dry-run resolves the sources and prints the commands without running them.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

OVERLAY_ZIP = "PBR_overlay_GAMMA.zip"
OVERLAY_DIR = "PBR overlay - GAMMA"       # top folder inside the overlay zip (the MO2 mod name)
PACK_ZIP = "PBR_shaders_GAMMA.zip"
PACK_DIR = "PBR_shaders_GAMMA"

# Shaders the compile check builds (the ones the PBR pipeline touches, plus the CSM sun)
CHECK_SHADERS = [
    "combine_1.ps", "accum_sun.ps", "accum_sun_near.ps", "accum_sun_far.ps", "accum_sun_csm.ps",
    "accum_base.ps", "accum_omni_unshadowed.ps", "accum_indirect.ps",
    "deffer_base_bump.ps", "deffer_base_aref_bump.ps",
    "deffer_base_hud_bump.ps", "deffer_base_hud_aref_bump.ps",
    "ssfx_ssr_combine.ps",
]
# Engine defines added when SSS / ES are installed (check_shaders.py --define)
CHECK_DEFINES = ["MONOLITH_ES", "MONOLITH_SSFX_CORE"]

# ------------------------------------------------------------------------------------------

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(REPO, "tools")


def find(sources, key, required=True):
    for cand in (key, key + ".zip", key + ".7z"):
        p = os.path.join(sources, cand)
        if os.path.exists(p):
            return p
    if required:
        sys.exit(f"error: {key}/ (or {key}.zip / {key}.7z) not found in {sources}")
    return None


def gamma_layers(sources):
    d = os.path.join(sources, "gamma")
    if not os.path.isdir(d):
        print("warning: no gamma/ folder: building on Atmospherics + SSS + ES only")
        return []
    entries = sorted(e for e in os.listdir(d) if e != "order.txt" and not e.startswith("."))
    order_file = os.path.join(d, "order.txt")
    if os.path.isfile(order_file):
        with open(order_file, encoding="utf-8") as f:
            order = [l.strip() for l in f if l.strip() and not l.lstrip().startswith("#")]
        missing = [o for o in order if o not in entries]
        unlisted = [e for e in entries if e not in order]
        if missing or unlisted:
            sys.exit("error: gamma/order.txt does not match gamma/:"
                     + (f"\n  listed but missing: {missing}" if missing else "")
                     + (f"\n  present but not listed: {unlisted}" if unlisted else ""))
    elif len(entries) > 1:
        sys.exit("error: gamma/ has several overrides but no order.txt (their order decides which file wins)")
    else:
        order = entries
    return [os.path.join(d, e) for e in order]


def find_r3(root):
    for dp, dns, _ in os.walk(root):
        if os.path.basename(dp).lower() == "r3" and os.path.basename(os.path.dirname(dp)).lower() == "shaders":
            return dp
    return None


def run(cmd, dry):
    print("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    if not dry:
        subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", required=True, help="sources folder (see above)")
    ap.add_argument("--out", default="shader_packs", help="where the two zips go")
    ap.add_argument("--dxc", help="DXC binary: enables the compile check (needs vanilla/ in the sources)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    es, sss = find(a.sources, "es"), find(a.sources, "sss")
    atm = find(a.sources, "atmospherics")
    beef = find(a.sources, "beef", required=False)
    extras = gamma_layers(a.sources)
    vanilla = os.path.join(a.sources, "vanilla")
    print(f"ES: {es}\nSSS: {sss}\nAtmospherics: {atm}\nBeef's NVGs: {beef or '-'}")
    print("GAMMA overrides (lowest first):" + "".join(f"\n  {e}" for e in extras))

    py = sys.executable
    common = ["--es", es, "--sss", sss, "--atmospherics", atm, "--shaders-only"]
    if beef:
        common += ["--beef", beef]
    for e in extras:
        common += ["--extra", e]

    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="pbr_packs_")
    base = os.path.join(work, "base")
    full = os.path.join(work, PACK_DIR)
    overlay = os.path.join(work, OVERLAY_DIR)
    try:
        # 1. The full pack, 2. the same stack without PBR (overlay input and check baseline)
        run([py, os.path.join(TOOLS, "build_shader_pack.py")] + common + ["--out", full, "--zip"], a.dry_run)
        run([py, os.path.join(TOOLS, "build_shader_pack.py")] + common + ["--no-compat", "--out", base], a.dry_run)
        # 3. The overlay, patched from that stack
        run([py, os.path.join(TOOLS, "make_pbr_overlay.py"), "--layer", base, "--out", overlay, "--zip"], a.dry_run)

        # 4. Compile check: only errors the PBR changes introduce fail the build
        if a.dxc and os.path.isdir(vanilla):
            van_r3 = find_r3(vanilla)
            if not van_r3 and not a.dry_run:
                sys.exit("error: vanilla/ has no shaders/r3 folder")
            base_r3 = os.path.join(base, "gamedata", "shaders", "r3")
            check = [py, os.path.join(TOOLS, "check_shaders.py"), "--dxc", a.dxc, "--shaders"] + CHECK_SHADERS
            for d in CHECK_DEFINES:
                check += ["--define", d]
            run(check + ["--layers", van_r3 or "vanilla", os.path.join(full, "gamedata", "shaders", "r3"),
                         "--baseline-layers", van_r3 or "vanilla", base_r3], a.dry_run)
            run(check + ["--layers", van_r3 or "vanilla", base_r3, os.path.join(overlay, "gamedata", "shaders", "r3"),
                         "--baseline-layers", van_r3 or "vanilla", base_r3], a.dry_run)
        else:
            print("compile check skipped (needs --dxc and vanilla/ in the sources)")

        if not a.dry_run:
            shutil.copy(full + ".zip", os.path.join(a.out, PACK_ZIP))
            shutil.copy(overlay + ".zip", os.path.join(a.out, OVERLAY_ZIP))
            print(f"wrote {os.path.join(a.out, PACK_ZIP)}\nwrote {os.path.join(a.out, OVERLAY_ZIP)}")
    except subprocess.CalledProcessError as e:
        print(f"error: step failed (exit {e.returncode}); no zips written", file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
