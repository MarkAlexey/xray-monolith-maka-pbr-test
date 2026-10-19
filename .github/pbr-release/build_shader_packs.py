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

Top layers (--top-layer DIR, repeatable, lowest first): extra shader folders applied above
GAMMA's overrides, i.e. above the whole stack. The DLSS / FSR3 branch passes the components
of the DLSS compatibility patch here (its SSS-level base, then the Atmospherics component that
matches atmospherics/), so the overlay's files are patched from the DLSS versions (for example
deffer_grass.vs keeps the DLSS grass jitter and gains the instanced-grass paths) and the full
pack contains the DLSS shaders. Install the DLSS patch first, then the overlay above it.

Usage
  python build_shader_packs.py --sources shader-sources --out shader_packs [--dxc dxc/bin/dxc]
                               [--top-layer DLSS/base --top-layer DLSS/atmos_rc7]
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


def _norm(data):
    return data.replace(b"\r\n", b"\n")


def _adapt_nv_depth(text):
    """DLSS patch's night_vision.h change, applied to another NVG header: the NVG depth blur
    loads s_position (scene-sized with DLSS / FSR3) with the display size (screen_res). Every
    s_position.Load(...) gets the scene size instead: dlss_params.yz while an upscaler runs,
    screen_res.xy otherwise. Returns None when nothing matched."""
    if b"dlss_params" in text or b"depth_size" in text:
        return text                                  # already adapted
    out, i, n = bytearray(), 0, 0
    key = b"s_position.Load("
    while True:
        j = text.find(key, i)
        if j < 0:
            out += text[i:]
            break
        k, depth = j + len(key), 1
        while k < len(text) and depth:
            depth += {40: 1, 41: -1}.get(text[k], 0)      # ( and )
            k += 1
        call = text[j:k]
        new = call.replace(b"screen_res.xy", b"(dlss_params.x > 0.5f ? dlss_params.yz : screen_res.xy)")
        n += new != call
        out += text[i:j] + new
        i = k
    if not n:
        return None
    return b'#include "dlss_contract.h" // DLSS / FSR3: scene-sized depth (build_shader_packs.py)\n' + bytes(out)


# Known top-layer changes that a line merge cannot carry onto a rewritten stack file. Used when
# git merge-file conflicts; each adapter re-applies the change's intent to the stack's winner.
TOP_LAYER_ADAPTERS = {
    "night_vision.h": _adapt_nv_depth,
}


def resolve_top_layers(lower, tops, dest, dry=False):
    """Fold the --top-layer folders onto the stack without discarding what the stack changed.

    A top layer (e.g. the DLSS compatibility patch) ships whole files made from ONE version of
    them, usually the plain mod (SSS, stock Beef's NVGs). When the stack's winning file is that
    same version, the top file simply replaces it. When the stack's winner is a different
    rewrite (GAMMA's own NVG override, a newer Atmospherics), replacing it would throw that
    rewrite away and break the shaders that need it (GAMMA's nightvision_gen_*.ps use the
    NVGParams of GAMMA's night_vision.h; the patch's stock-Beef night_vision.h has none). Then
    the top file's own changes are merged onto the stack's winner instead (git merge-file):
      base   = the version among the stack's layers closest to the top file (what it was made from)
      ours   = the stack's winner
      theirs = the top file
    A conflict stops the build (exit 1) and names the file; no guessed merge is shipped.

    Writes <dest>/gamedata/shaders/r3 (every top-layer file, resolved) and returns
    (labels, merged): {file: 'replaced'|'merged onto <layer>'} and the list of merged files.
    Caveats: only text the top layer itself changed is carried over; files present only in a top
    layer are copied as they are; the 'closest version' is picked by line diff size."""
    import difflib
    versions = {}                                      # key -> [(label, bytes)] lowest first
    for label, path in lower:
        for files in r3_files(path).values():
            for key, (rel, reader) in files.items():
                versions.setdefault(key, []).append((label, reader))
    # A later top layer's file replaces an earlier one's (e.g. the Atmospherics component over
    # the base): only the last one is resolved, always against the stack below the top layers
    latest = {}
    for tpath in tops:
        for files in r3_files(tpath).values():
            for key, (rel, reader) in files.items():
                latest[key] = (tpath, rel, reader)
    resolved, labels, merged, conflicts = {}, {}, [], []
    for key, (tpath, rel, reader) in sorted(latest.items()):
        if True:
            if True:
                top = _norm(reader())
                cands = versions.get(key, [])
                if not cands:
                    resolved[key] = (rel, top); labels[key] = "new"; continue
                winner_label, winner_reader = cands[-1]
                winner = _norm(winner_reader())
                def dist(b):
                    sm = difflib.SequenceMatcher(None, b.split(b"\n"), top.split(b"\n"), autojunk=False)
                    return sum(max(i2 - i1, j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal")
                base_label, base = min(((l, _norm(r())) for l, r in cands), key=lambda lb: dist(lb[1]))
                if base == winner:
                    resolved[key] = (rel, top); labels[key] = "replaced"; continue
                with tempfile.TemporaryDirectory() as td:
                    names = []
                    for n, data in (("ours", winner), ("base", base), ("theirs", top)):
                        fn = os.path.join(td, n); names.append(fn)
                        with open(fn, "wb") as f:
                            f.write(data)
                    r = subprocess.run(["git", "merge-file", "-p", "-L", winner_label, "-L", base_label, "-L",
                                        os.path.basename(os.path.normpath(tpath))] + names, capture_output=True)
                if r.returncode != 0:
                    adapter = TOP_LAYER_ADAPTERS.get(os.path.basename(key))
                    adapted = adapter(winner) if adapter else None
                    if adapted is None:
                        conflicts.append(f"{rel} (stack: {winner_label}, made from: {base_label})")
                        continue
                    resolved[key] = (rel, adapted); labels[key] = f"adapted onto {winner_label} (line merge conflicted)"
                    if key not in merged:
                        merged.append(key)
                    continue
                resolved[key] = (rel, r.stdout); labels[key] = f"merged onto {winner_label} (made from {base_label})"
                if key not in merged:
                    merged.append(key)
    if conflicts:
        sys.exit("error: a top layer conflicts with the stack's own changes:\n  " + "\n  ".join(conflicts))
    if not dry:
        r3 = os.path.join(dest, "gamedata", "shaders", "r3")
        for key, (rel, data) in resolved.items():
            out = os.path.join(r3, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as f:
                f.write(data)
    return labels, merged


def includers(r3_dir, headers):
    """Shader programs (.ps/.vs/.cs/.gs) in r3_dir that #include one of the given headers"""
    want = {h.lower() for h in headers}
    found = []
    for dp, _, fs in os.walk(r3_dir):
        for f in fs:
            if not f.lower().endswith((".ps", ".vs", ".cs", ".gs")):
                continue
            with open(os.path.join(dp, f), "rb") as fh:
                text = fh.read().decode("latin-1").lower()
            if any(re.search(r'#\s*include\s*"' + re.escape(h) + '"', text) for h in want):
                found.append(os.path.relpath(os.path.join(dp, f), r3_dir).replace(os.sep, "/"))
    return sorted(found)


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
    ap.add_argument("--top-layer", action="append", default=[],
                    help="extra shader folder above GAMMA's overrides (repeatable, lowest first)")
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
    for t in a.top_layer:
        if not os.path.exists(t):
            sys.exit(f"error: --top-layer {t} does not exist")
    if a.top_layer and mode != "merged":
        sys.exit("error: --top-layer needs the merged source form (one shaders/r3 per mod)")
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
            lower = ([("Beef's NVGs", src["beef"])] if src["beef"] else []) + [
                ("Enhanced Shaders", src["es"]), ("Screen Space Shaders", src["sss"]),
                ("Atmospherics", src["atmospherics"])] + [
                (f"GAMMA/{os.path.basename(e)}", e) for e in extras]
            stack, merged_top = lower, []
            if a.top_layer:
                # 0. the top layers, folded onto the stack (resolve_top_layers)
                top_dir = os.path.join(work, "top")
                labels, merged_top = resolve_top_layers(lower, a.top_layer, top_dir, a.dry_run)
                print("Top layers (" + ", ".join(a.top_layer) + "):")
                for k in sorted(labels):
                    print(f"  r3/{k}: {labels[k]}")
                stack = lower + [("top layers, resolved", top_dir)]
            # 1. the overlay, patched from the winners of exactly this stack
            cmd = [py, os.path.join(TOOLS, "make_pbr_overlay.py"), "--out", overlay]
            for _, p in stack:
                cmd += ["--layer", p]
            run(cmd + (["--dry-run"] if a.dry_run else []), False)
            # Files the top layers had to merge go into the overlay too: it is installed above the
            # top layer itself (e.g. the DLSS patch), whose unmerged copies would win otherwise
            if merged_top and not a.dry_run:
                over_r3 = os.path.join(overlay, "gamedata", "shaders", "r3")
                for key in merged_top:
                    src_f = os.path.join(top_dir, "gamedata", "shaders", "r3", key)
                    dst_f = os.path.join(over_r3, key)
                    if not os.path.exists(dst_f):
                        os.makedirs(os.path.dirname(dst_f), exist_ok=True)
                        shutil.copy(src_f, dst_f)
                        print(f"  overlay += r3/{key} (top layer merged onto the stack)")
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
            top_check = None
            if a.top_layer:
                # The top layers must not break shaders of the stack below them: compile every
                # program they ship or whose headers they change, against the stack without them
                top_r3 = os.path.join(top_dir, "gamedata", "shaders", "r3")
                low = os.path.join(work, "lower")
                write_merged(repo_layer + lower, low)
                shipped = sorted(os.path.relpath(os.path.join(dp, f), top_r3).replace(os.sep, "/")
                                 for dp, _, fs in os.walk(top_r3) for f in fs)
                progs = [f for f in shipped if f.lower().endswith((".ps", ".vs", ".cs", ".gs"))]
                progs += includers(full_r3, [f for f in shipped if f.lower().endswith(".h")])
                top_check = (sorted(set(progs)), os.path.join(low, "gamedata", "shaders", "r3"))
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
            if mode == "merged" and top_check and top_check[0]:
                tcheck = [py, os.path.join(TOOLS, "check_shaders.py"), "--dxc", a.dxc, "--shaders"] + top_check[0]
                for d in CHECK_DEFINES:
                    tcheck += ["--define", d]
                print("Top layers vs the stack below them:")
                run(tcheck + ["--layers", van_r3, full_r3, "--baseline-layers", van_r3, top_check[1]], False)
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
