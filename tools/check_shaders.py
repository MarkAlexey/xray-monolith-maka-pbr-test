#!/usr/bin/env python3
"""
check_shaders.py - compile shader stacks with DXC and diff the errors against a baseline.

Overlays shader folders the way Mod Organizer does (later layers win), compiles a list of
shaders under several define sets, and reports errors. With --baseline it compares against
a second layer list (e.g. the previous commit's shaders) and reports only NEW errors, which
is what matters: vanilla Anomaly, SSS and ES all contain code DXC rejects but the game's FXC
accepts (e.g. SSS's [unroll(expr)] with a non-literal count).

DXC is not the game's compiler (FXC, SM 5.0). A clean DXC compile catches syntax, type and
missing-identifier errors; it does not promise FXC accepts every line.

Usage (paths are examples):
  python check_shaders.py --dxc dxc/bin/dxc \
      --layers vanilla/shaders/r3 repo/gamedata/shaders/r3 "ES/01 Enhanced Shaders - PBR/gamedata/shaders/r3" \
      --baseline-layers vanilla/shaders/r3 repo_old/gamedata/shaders/r3 "ES/.../r3" \
      --shaders combine_1.ps accum_sun.ps sslr_render.cs
  --dry-run prints the compile matrix without compiling.
  --define NAME[=VALUE] (repeatable) adds a define to every compile: the engine adds some only
  when a mod is installed (MONOLITH_ES with Enhanced Shaders, MONOLITH_SSFX_CORE with SSS).
  --show-bindings NAME[,NAME] prints which of these resources each compiled shader binds
  (e.g. s_refl, to confirm an #ifdef'd path is really compiled in).
"""

import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

# Defines the engine always injects (r4.cpp CRender::shader_compile). Without them SSS shaders
# fail on undeclared SSFX_*_QUALITY identifiers.
ENGINE_DEFINES = [
    "SM_5=1", "SSFX_MODEXE=1", "SSFX_SSR_QUALITY=2", "SSFX_WATER_QUALITY=2", "SSFX_WATER_PARALLAX=1",
    "SSFX_IL_QUALITY=16", "SSFX_AO_QUALITY=4", "SSFX_POM_REFINE=0", "SSFX_TERRA_POM_REFINE=0",
    "SSFX_SSS_DIR_QUALITY=8", "SSFX_SSS_OMNI_QUALITY=4",
]

# Define sets compiled for every shader (on top of ENGINE_DEFINES)
DEFINE_SETS = [
    [],
    ["USE_SSLR_REFLECTIONS=1"],
    ["USE_MSAA=1", "MSAA_SAMPLES=4"],
    ["USE_R2_STATIC_SUN=1", "USE_SSLR_REFLECTIONS=1"],
    ["SSAO_QUALITY=3", "USE_HDAO=1", "USE_SSLR_REFLECTIONS=1"],
]

PROFILES = {"ps": "ps_6_0", "vs": "vs_6_0", "cs": "cs_6_0", "gs": "gs_6_0", "hs": "hs_6_0", "ds": "ds_6_0"}

# ------------------------------------------------------------------------------------------


POS_RE = re.compile(r":\d+:\d+:")


def strip_pos(err):
    """'combine_1.ps:128:58: error: ...' -> 'combine_1.ps: error: ...'"""
    return POS_RE.sub(":", err, count=1)


INCLUDE_RE = re.compile(r'(#\s*include\s*")([^"]+)(")')


def overlay(dst, layers):
    """Overlay the layers into dst the way Windows sees them: file names are case-insensitive
    and #include paths may use backslashes (e.g. ES's "ACES.h" is shipped as ACES.H and
    included as "ACES_LMTs\\LMT_Contrast.h"). Every path is lowercased and every #include line
    rewritten to lowercase with forward slashes, so DXC on Linux resolves what FXC on Windows
    resolves. A later layer replaces an earlier file regardless of the case it uses."""
    for layer in layers:
        if not os.path.isdir(layer):
            sys.exit(f"error: layer not found: {layer}")
        for root, _, files in os.walk(layer):
            rel = os.path.relpath(root, layer).lower()
            os.makedirs(os.path.join(dst, rel), exist_ok=True)
            for f in files:
                src = os.path.join(root, f)
                out = os.path.join(dst, rel, f.lower())
                with open(src, "rb") as fh:
                    data = fh.read()
                text = data.decode("latin-1")
                text = INCLUDE_RE.sub(lambda m: m.group(1) + m.group(2).replace("\\", "/").lower() + m.group(3), text)
                with open(out, "wb") as fh:
                    fh.write(text.encode("latin-1"))


EXTRA_DEFINES = []  # --define


def compile_one(dxc, d, shader, defines, bindings):
    ext = shader.rsplit(".", 1)[-1]
    args = [dxc, "-T", PROFILES[ext], "-E", "main", "-HV", "2016", "-Gec"]
    for k in ENGINE_DEFINES + EXTRA_DEFINES + defines:
        args += ["-D", k]
    asm = os.path.join(d, "__out.asm")
    args += [shader, "-Fc", asm]
    r = subprocess.run(args, cwd=d, capture_output=True, text=True, errors="replace")
    # ": fatal error" (missing include, ...) counts too: it stops the compile outright
    errors = sorted({l.strip() for l in r.stderr.splitlines() if ": error" in l or ": fatal error" in l})
    bound = []
    if bindings and os.path.exists(asm):
        with open(asm, errors="replace") as f:
            names = {line.split()[1] for line in f if line.startswith("; ") and len(line.split()) > 2}
        bound = [b for b in bindings if b in names]
        os.remove(asm)
    return errors, bound


def run(dxc, layers, shaders, bindings):
    out = {}
    with tempfile.TemporaryDirectory() as d:
        overlay(d, layers)
        for sh in shaders:
            sh = sh.lower()
            if not os.path.exists(os.path.join(d, sh)):
                out[sh] = None
                continue
            for ds in DEFINE_SETS:
                out.setdefault(sh, {})[" ".join(ds) or "-"] = compile_one(dxc, d, sh, ds, bindings)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dxc", required=True)
    ap.add_argument("--layers", nargs="+", required=True, help="shader folders, lowest priority first")
    ap.add_argument("--baseline-layers", nargs="+", help="same, for the baseline to diff against")
    ap.add_argument("--shaders", nargs="+", required=True)
    ap.add_argument("--define", action="append", default=[], help="extra define for every compile, NAME or NAME=VALUE")
    ap.add_argument("--show-bindings", default="", help="comma-separated resource names to report")
    ap.add_argument("--json", help="write full results here")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    a.dxc = os.path.abspath(a.dxc)
    EXTRA_DEFINES.extend(d if "=" in d else d + "=1" for d in a.define)
    bindings = [b for b in a.show_bindings.split(",") if b]
    if a.dry_run:
        print("layers:", *a.layers, sep="\n  ")
        if a.baseline_layers:
            print("baseline:", *a.baseline_layers, sep="\n  ")
        if a.define:
            print("extra defines:", " ".join(a.define))
        for sh in a.shaders:
            for ds in DEFINE_SETS:
                print(f"  would compile {sh} [{' '.join(ds) or '-'}]")
        return 0

    cur = run(a.dxc, a.layers, a.shaders, bindings)
    base = run(a.dxc, a.baseline_layers, a.shaders, []) if a.baseline_layers else None
    new_total = 0
    for sh, sets in cur.items():
        if sets is None:
            print(f"{sh}: MISSING in layers")
            continue
        for ds, (errs, bound) in sets.items():
            # Compare without line:column: code the patch inserts above an existing error moves
            # it, and it must still count as the same (old) error. Counted, not deduplicated.
            known = collections.Counter(strip_pos(e) for e in base[sh][ds][0]) if base and base.get(sh) else collections.Counter()
            new = []
            for e in errs:
                k = strip_pos(e)
                if known[k] > 0:
                    known[k] -= 1
                else:
                    new.append(e)
            new_total += len(new)
            tag = "OK" if not errs else (f"{len(errs)} errors ({len(new)} new)" if base else f"{len(errs)} errors")
            extra = f"  binds: {' '.join(bound) or '-'}" if bindings else ""
            print(f"{sh} [{ds}]: {tag}{extra}")
            for e in (new if base else errs)[:5]:
                print("    " + e)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(cur, f, indent=1)
    print(f"\nnew errors: {new_total}" if base else "")
    return 1 if (base and new_total) else 0


if __name__ == "__main__":
    sys.exit(main())
