#!/usr/bin/env python3
"""
build_shader_packs.py - build the ready-made GAMMA shader zips for a PBR-Test release.

Used by .github/workflows/pbr-build.yml (job "shader_packs"); also runs locally with the same
sources folder. Produces, in --out:

  PBR_overlay_GAMMA.zip   only the files the PBR pipeline needs, patched from GAMMA's own stack
                          (tools/make_pbr_overlay.py). Installed last, on top of GAMMA.
  PBR_shaders_GAMMA.zip   GAMMA's whole shader stack with PBR merged in. Replaces GAMMA's
                          shader mods.

The stack, lowest priority first (the order GAMMA installs them in):
  Beef's NVGs -> Enhanced Shaders -> Screen Space Shaders -> Atmospherics -> GAMMA's overrides
Atmospherics is required: GAMMA's weathers are tuned for its shadow.h / pbr_settings.h, and
without them the world renders far too dark.

Sources folder (the private sources repository's checkout), names are case-sensitive:

  es/   (or es.zip / es.7z)                    Enhanced Shaders and Color Grading 1.10
  sss/  (or sss.zip / sss.7z)                  Screen Space Shaders 23.5
  atmospherics/ or atmos/ (or .zip / .7z)      Atmospherics
  beef/ (or beef.zip / beef.7z)                Beef's NVGs (optional, GAMMA ships it)
  gamma/                                       GAMMA's shader overrides: one folder/zip/7z each
  gamma/order.txt                              their names, lowest priority first, one per line
                                               (# comments allowed); required when there is
                                               more than one (their order decides which
                                               depth_of_field.ps etc. wins)
  vanilla/ (optional)                          Anomaly's unpacked shaders (any folder with
                                               shaders/r3): enables the DXC compile check

Each of es/, sss/, atmos/, beef/ can be given in one of two forms; all four must use the same:
  merged   ONE shaders/r3 folder each, the files as they are installed (e.g. copied from a
           working install, every module already merged in its order). The stack is then
           exactly what is in these folders. Recommended: it is what your game really runs.
  modules  the mods as downloaded, with their module folders ("00 - MAIN FILE", "01 Enhanced
           Shaders - PBR", ...). Modules are picked by tools/build_shader_pack.py's lists
           (Atmospherics' ES module set, every SSS module, Beef's main folder + ES patch).
Only gamedata/shaders matters (the packs are shaders-only), so strip everything else to stay
under GitHub's 100 MB file limit (or use Git LFS).

Compile check (with --dxc and vanilla/): every PBR-relevant shader is compiled with DXC, with
PBR and against the same stack without it as the baseline. Only NEW errors fail the build
(SSS / ES contain a few lines DXC rejects but the game's FXC accepts). DXC is not FXC: a clean
compile catches syntax, type and missing-identifier errors, not everything FXC might reject.

Usage
  python build_shader_packs.py --sources shader-sources --out shader_packs [--dxc dxc/bin/dxc]
  --dry-run resolves the sources and shows what would be built, writing nothing.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

OVERLAY_ZIP = "PBR_overlay_GAMMA.zip"
OVERLAY_DIR = "PBR overlay - GAMMA"       # top folder inside the overlay zip (the MO2 mod name)
PACK_ZIP = "PBR_shaders_GAMMA.zip"
PACK_DIR = "PBR_shaders_GAMMA"

# Accepted names per source, first match wins
SOURCE_NAMES = {
    "es": ["es"],
    "sss": ["sss"],
    "atmospherics": ["atmospherics", "atmos"],
    "beef": ["beef"],
}

# Merged form: put this repository's own shaders (the exe's gamedata) at the bottom of the
# full pack, as build_shader_pack.py does, so it carries one consistent set of PBR headers.
INCLUDE_REPO_SHADERS = True

# Shaders the compile check builds (the ones the PBR pipeline touches, plus the CSM sun)
CHECK_SHADERS = [
    "combine_1.ps", "accum_sun.ps", "accum_sun_near.ps", "accum_sun_far.ps", "accum_sun_csm.ps",
    "accum_base.ps", "accum_omni_unshadowed.ps", "accum_indirect.ps",
    "deffer_base_bump.ps", "deffer_base_aref_bump.ps",
    "deffer_base_hud_bump.ps", "deffer_base_hud_aref_bump.ps",
    "ssfx_ssr_combine.ps",
    # Instanced grass: the stack's grass shaders with the DT_INST paths (overlay), every variant
    "deffer_grass.vs", "deffer_grass_inst.vs", "deffer_grass_inst_merge.vs", "deffer_grass_inst_merge_occ.vs",
    "deffer_grass_inst_merge_thin.vs", "deffer_grass_inst_merge_occ_thin.vs",
    "deffer_detail_s_flat.vs", "deffer_detail_s_flat_inst.vs", "deffer_detail_s_flat_inst_merge_occ_thin.vs",
    # IX-Ray VSLR capture / panorama (engine-owned, compiled against the stack's sload.h)
    "deffer_vslr_bump.ps", "deffer_vslr_lmh_aref_bump.ps", "vslr_panorama.cs",
]
# Engine defines added when SSS / ES are installed (check_shaders.py --define)
CHECK_DEFINES = ["MONOLITH_ES", "MONOLITH_SSFX_CORE"]

# ------------------------------------------------------------------------------------------

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(REPO, "tools")
sys.path.insert(0, TOOLS)
from build_shader_pack import Source      # noqa: E402  (folder / zip / 7z reader)

R3 = re.compile(r"^(.*?)shaders/r3/(.+)$", re.I)


def find(sources, key, required=True):
    for name in SOURCE_NAMES[key]:
        for cand in (name, name + ".zip", name + ".7z"):
            p = os.path.join(sources, cand)
            if os.path.exists(p):
                return p
    if required:
        names = " / ".join(SOURCE_NAMES[key])
        sys.exit(f"error: {names} (folder, .zip or .7z) not found in {sources}")
    return None


def r3_files(path):
    """{shaders/r3 roots: {lower-case relative path: (relative path, reader)}} of a source"""
    roots = {}
    for rel, reader in Source(path).files("."):
        m = R3.match(rel)
        if m:
            roots.setdefault(m.group(1).lower(), {})[m.group(2).lower()] = (m.group(2), reader)
    return roots


def form(path):
    """'merged' (one shaders/r3 folder) or 'modules' (several)"""
    roots = r3_files(path)
    if not roots:
        sys.exit(f"error: no shaders/r3 files in {path}")
    return "merged" if len(roots) == 1 else "modules"


def gamma_layers(sources):
    d = os.path.join(sources, "gamma")
    if not os.path.isdir(d):
        print("WARNING: no gamma/ folder: building without GAMMA's overrides. Its LUT layout differs"
              " from SSS's and Atmospherics': without gamma/ the full pack turns GAMMA's colours wrong.")
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
    for dp, _, _ in os.walk(root):
        if os.path.basename(dp).lower() == "r3" and os.path.basename(os.path.dirname(dp)).lower() == "shaders":
            return dp
    return None


def run(cmd, dry):
    print("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    if not dry:
        subprocess.run(cmd, check=True)


def write_merged(layers, dest):
    """Merge shaders/r3 of every (label, path) layer, later wins, Windows-style
    case-insensitive. Writes <dest>/gamedata/shaders/r3 and returns {file: [labels]}."""
    winner, history = {}, {}
    for label, path in layers:
        for files in r3_files(path).values():
            for key, (rel, reader) in files.items():
                winner[key] = (rel, reader)
                history.setdefault(key, []).append(label)
    r3 = os.path.join(dest, "gamedata", "shaders", "r3")
    for key, (rel, reader) in winner.items():
        out = os.path.join(r3, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(reader())
    return history


def zip_dir(src, zip_path, top):
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for dp, _, fs in os.walk(src):
            for f in fs:
                full = os.path.join(dp, f)
                z.write(full, os.path.join(top, os.path.relpath(full, src)).replace(os.sep, "/"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", required=True, help="sources folder (see above)")
    ap.add_argument("--out", default="shader_packs", help="where the two zips go")
    ap.add_argument("--dxc", help="DXC binary: enables the compile check (needs vanilla/ in the sources)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    src = {"es": find(a.sources, "es"), "sss": find(a.sources, "sss"),
           "atmospherics": find(a.sources, "atmospherics"), "beef": find(a.sources, "beef", required=False)}
    forms = {k: form(p) for k, p in src.items() if p}
    if len(set(forms.values())) != 1:
        sys.exit("error: es/, sss/, atmospherics/ and beef/ must all be merged (one shaders/r3 each)"
                 " or all be the mods with their module folders:\n  "
                 + "\n  ".join(f"{k}: {v}" for k, v in forms.items()))
    mode = next(iter(forms.values()))
    extras = gamma_layers(a.sources)
    vanilla = os.path.join(a.sources, "vanilla")
    van_r3 = find_r3(vanilla) if os.path.isdir(vanilla) else None
    print(f"Form: {mode}")
    for k in ("beef", "es", "sss", "atmospherics"):
        print(f"{k}: {src[k] or '-'}")
    print("GAMMA overrides (lowest first):" + "".join(f"\n  {e}" for e in extras))
    print(f"Compile check: {'on' if a.dxc and van_r3 else 'off (needs --dxc and vanilla/ with shaders/r3)'}")

    py = sys.executable
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="pbr_packs_")
    base = os.path.join(work, "base")
    full = os.path.join(work, PACK_DIR)
    overlay = os.path.join(work, OVERLAY_DIR)
    try:
        if mode == "merged":
            stack = ([("Beef's NVGs", src["beef"])] if src["beef"] else []) + [
                ("Enhanced Shaders", src["es"]), ("Screen Space Shaders", src["sss"]),
                ("Atmospherics", src["atmospherics"])] + [
                (f"GAMMA/{os.path.basename(e)}", e) for e in extras]
            # 1. the overlay, patched from the winners of exactly this stack
            cmd = [py, os.path.join(TOOLS, "make_pbr_overlay.py"), "--out", overlay]
            for _, p in stack:
                cmd += ["--layer", p]
            run(cmd + (["--dry-run"] if a.dry_run else []), False)
            if a.dry_run:
                print("[dry-run] nothing written")
                return 0
            # 2. the full pack = (repo shaders) + the stack + the overlay; 3. the baseline
            repo_layer = [("repo", os.path.join(REPO, "gamedata"))] if INCLUDE_REPO_SHADERS else []
            history = write_merged(repo_layer + stack + [("PBR overlay", overlay)], full)
            write_merged(repo_layer + stack, base)
            report = ["PBR shader pack (GAMMA) - merge report", "", "Layers, lowest priority first:"]
            report += [f"  {l}" for l, _ in repo_layer + stack] + ["  PBR overlay", "",
                                                                    "Files provided by more than one layer (last one wins):"]
            report += [f"  r3/{k}\n      {' -> '.join(v)}" for k, v in sorted(history.items()) if len(v) > 1]
            with open(os.path.join(full, "PACK_REPORT.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(report) + "\n")
            zip_dir(full, full + ".zip", PACK_DIR)
            zip_dir(overlay, overlay + ".zip", OVERLAY_DIR)
            full_r3 = os.path.join(full, "gamedata", "shaders", "r3")
            checks = [[full_r3]]
        else:
            common = ["--es", src["es"], "--sss", src["sss"], "--atmospherics", src["atmospherics"], "--shaders-only"]
            if src["beef"]:
                common += ["--beef", src["beef"]]
            for e in extras:
                common += ["--extra", e]
            run([py, os.path.join(TOOLS, "build_shader_pack.py")] + common + ["--out", full, "--zip"], a.dry_run)
            run([py, os.path.join(TOOLS, "build_shader_pack.py")] + common + ["--no-compat", "--out", base], a.dry_run)
            run([py, os.path.join(TOOLS, "make_pbr_overlay.py"), "--layer", base, "--out", overlay, "--zip"], a.dry_run)
            if a.dry_run:
                print("[dry-run] nothing written")
                return 0
            full_r3 = os.path.join(full, "gamedata", "shaders", "r3")
            checks = [[full_r3], [os.path.join(base, "gamedata", "shaders", "r3"),
                                  os.path.join(overlay, "gamedata", "shaders", "r3")]]

        # Compile check: only errors the PBR changes introduce fail the build
        if a.dxc and van_r3:
            base_r3 = os.path.join(base, "gamedata", "shaders", "r3")
            check = [py, os.path.join(TOOLS, "check_shaders.py"), "--dxc", a.dxc, "--shaders"] + CHECK_SHADERS
            for d in CHECK_DEFINES:
                check += ["--define", d]
            for layers in checks:
                run(check + ["--layers", van_r3] + layers + ["--baseline-layers", van_r3, base_r3], False)
        else:
            print("compile check skipped")

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
