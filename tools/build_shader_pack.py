#!/usr/bin/env python3
"""
build_shader_pack.py - merge this branch's shaders, Enhanced Shaders 1.10, Screen Space
Shaders 23.5 and the PBR compatibility patch into ONE ready-to-install gamedata folder.

The result is what Mod Organizer would show as the final, winning files with these mods in
the right order, flattened into a single mod:

    1. this repository's gamedata/shaders  (IX-Ray PBR, SSLR, CSM, ixray_brdf.h, ...)
    2. Enhanced Shaders modules             (ES_MODULES below)
    3. Screen Space Shaders modules         (SSS_MODULES below, in that order)
    4. PBR compat patch                     (generated from YOUR ES/SSS copies by
                                             make_pbr_compat_patch.py, or --compat)

Later layers win. Every file that more than one layer provides is listed in
PACK_REPORT.txt with the layer that won, so you can check nothing was lost.

Usage
  python build_shader_pack.py --es Enhanced_Shaders_1_10.zip --sss SSS_Update23_5.zip \
      --out pbr_shader_pack --zip [--dry-run]
  --es / --sss accept zips or extracted folders. --compat <folder|zip> uses an existing
  compat patch instead of generating one.

Install: the pack replaces the separate ES, SSS and compat mods. Disable those (and keep
the exe's gamedata BELOW the pack, it may be anywhere below), then delete
appdata/shaders_cache/r4. To change SSS settings use MCM as before: the pack keeps SSS's
scripts and configs (unless SHADERS_ONLY).

Caveats
  - The module selection below is a guess at a "full" install. Mutually exclusive SSS options
    (SSR Water vs its ICE version, Detectors+ DAR vs Vanilla) and add-on patches that need
    other mods (Beefs NVGs, Laser+ BAS, Detectors+) are off by default; switch them on here
    if you use them. Order inside SSS_MODULES matters: HUD Raindrops must come after
    "00 - MAIN FILE (MV)", the ES patch after the main files.
  - SHADERS_ONLY = True drops SSS's textures, scripts and configs. SSS then still needs its
    own mod installed BELOW the pack for those (shaders from the pack win).
  - The pack contains files from SSS (Ascii1457) and ES (KennShade). Fine for your own
    install; ask the authors before sharing it publicly.
  - Changing module selection in the pack means rebuilding it; re-run this script.
"""

import argparse
import io
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

ES_MODULES = [
    "01 Enhanced Shaders - PBR",              # the compat patch is built for this variant
    "02 Enhanced Color Grading",
    # "Optional - Fixed Bloom",
    # "Optional - Corrected Cubemaps",         # not needed with the unfuck() remap
    # "Optional - shader_param controls",
]

SSS_MODULES = [
    "00 - MAIN FILE",
    "00 - MAIN FILE (MV)",
    "00 - PATCH - [ Enhanced Shaders 1.10 ]",
    # "00 - PATCH - [ Beefs NVGs ]",           # only with Beef's NVGs
    "01 - AO",
    "02 - SSS",
    "03 - SSR",
    "04 - SSR Water",                          # or "04 - SSR Water - [ ICE VERSION ]", not both
    "05 - Rain",
    "05 - Rain - Footsteps",
    "05 - Rain - HUD Raindrops",
    "05 - Rain - Puddles",
    "06 - Weapons DOF",
    "07 - Indirect Light",
    "08 - New Shadow & Light Features",
    "09 - Interactive Grass",
    "10 - Wind",
    # "10 - Wind - [OPTIONAL] Vanilla Emission",  # skip with a weather addon that sets wind
    "11 - Terrain Upgrade",
    "50 - PostProcess - LUT",
    "51 - PostProcess - BLOOM",
    "52 - PostProcess - Motion Blur",
    "53 - PostProcess - TAA",
    "90 - SKY DEBANDING",
    "91 - FLORA FIXES & IMPROVEMENTS",
    "92 - FOG",
    "93 - BLOOD DECALS",
    "94 - IMPROVED GLOSS & WET SURFACES - [ ES Required ]",
    "95 - PARALLAX",
    "96 - GLASS",
    # "990 - ( EXTRA ) Soft Pulse",
    # "991 - ( EXTRA ) Laser+ [ BAS ]",
    # "992 - ( EXTRA ) Detectors+ [ DAR ]",
    # "992 - ( EXTRA ) Detectors+ [ Vanilla ]",
]

INCLUDE_REPO_SHADERS = True     # this branch's gamedata/shaders (needed: ixray_brdf.h, sslr_*)
SHADERS_ONLY = False            # True = only gamedata/shaders from every layer
SKIP_NAMES = {"Thumbs.db", "desktop.ini", ".DS_Store"}

# ------------------------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


class Source:
    """A zip or a folder; lists and reads files under a module prefix."""

    def __init__(self, path):
        self.path = path
        if os.path.isfile(path):
            self.zip = zipfile.ZipFile(path)
            self.names = {n.replace("\\", "/"): n for n in self.zip.namelist() if not n.endswith("/")}
            # tolerate an extra top folder inside the zip
            tops = {n.split("/", 1)[0] for n in self.names}
            self.root = ""
            if len(tops) == 1:
                only = next(iter(tops))
                if not only.lower().startswith("gamedata") and all("/" in n for n in self.names):
                    self.root = only + "/"
        elif os.path.isdir(path):
            self.zip = None
        else:
            sys.exit(f"error: {path} is neither a zip nor a folder")

    def files(self, module):
        """Yield (path relative to the module, reader) for every file of the module"""
        if self.zip:
            pre = self.root + module.rstrip("/") + "/"
            found = False
            for n, real in self.names.items():
                if n.startswith(pre):
                    found = True
                    yield n[len(pre):], (lambda real=real: self.zip.read(real))
            if not found:
                raise KeyError(module)
        else:
            base = os.path.join(self.path, module)
            if not os.path.isdir(base):
                raise KeyError(module)
            for dp, _, fns in os.walk(base):
                for fn in fns:
                    full = os.path.join(dp, fn)
                    yield os.path.relpath(full, base).replace(os.sep, "/"), (lambda full=full: open(full, "rb").read())


def keep(rel):
    parts = rel.split("/")
    if parts[-1] in SKIP_NAMES:
        return False
    if parts[0].lower() != "gamedata":
        return False                     # module-level readmes, images, bin/ etc.
    if SHADERS_ONLY and (len(parts) < 2 or parts[1].lower() != "shaders"):
        return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--es", required=True, help="Enhanced Shaders 1.10 zip or extracted folder")
    ap.add_argument("--sss", required=True, help="Screen Space Shaders 23.5 zip or extracted folder")
    ap.add_argument("--compat", help="existing compat patch folder/zip (default: generate it)")
    ap.add_argument("--out", default="pbr_shader_pack")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    ap.add_argument("--shaders-only", action="store_true", help="same as SHADERS_ONLY = True")
    ap.add_argument("--dry-run", action="store_true", help="report what would be merged, write nothing")
    a = ap.parse_args()
    if a.shaders_only:
        global SHADERS_ONLY
        SHADERS_ONLY = True

    tmp = None
    if not a.compat:
        tmp = tempfile.mkdtemp(prefix="pbr_compat_")
        r = subprocess.run([sys.executable, os.path.join(HERE, "make_pbr_compat_patch.py"),
                            "--es", a.es, "--sss", a.sss, "--out", tmp], capture_output=True, text=True)
        if r.returncode:
            sys.exit("error: generating the compat patch failed:\n" + r.stderr)
        a.compat = tmp

    es, sss, compat = Source(a.es), Source(a.sss), Source(a.compat)
    layers = []    # (label, source, module)
    if INCLUDE_REPO_SHADERS:
        layers.append(("repo", Source(REPO), "."))
    layers += [("ES/" + m, es, m) for m in ES_MODULES]
    layers += [("SSS/" + m, sss, m) for m in SSS_MODULES]
    if compat.zip:
        cmods = sorted({n[len(compat.root):].split("/", 1)[0] for n in compat.names if n[len(compat.root):].startswith("99 ")})
    else:
        cmods = sorted(d for d in os.listdir(a.compat) if d.startswith("99 "))
    layers += [("compat/" + m, compat, m) for m in cmods]

    winner, history, missing = {}, {}, []
    for label, src, mod in layers:
        try:
            for rel, reader in src.files(mod):
                if label == "repo" and not rel.lower().startswith("gamedata/shaders/"):
                    continue                          # the exe's other gamedata is not part of the pack
                if not keep(rel):
                    continue
                key = rel.lower()                     # Windows: case-insensitive paths
                history.setdefault(key, []).append(label)
                winner[key] = (rel, reader, label)
        except KeyError:
            missing.append(label)
    if missing:
        print("error: modules not found (different archive layout or version?):\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1

    overridden = {k: v for k, v in history.items() if len(v) > 1}
    by_layer = {}
    for rel, _, label in winner.values():
        by_layer[label] = by_layer.get(label, 0) + 1
    print(f"{len(winner)} files from {len(layers)} layers; {len(overridden)} provided by more than one layer")
    for label, _, _ in layers:
        print(f"  {by_layer.get(label, 0):5d} winning  {label}")

    report = ["PBR shader pack - merge report", "", "Layers, lowest priority first:"]
    report += [f"  {l}" for l, _, _ in layers]
    report += ["", "Files provided by more than one layer (last one wins):"]
    for k in sorted(overridden):
        report.append(f"  {winner[k][0]}\n      {' -> '.join(overridden[k])}")
    report_text = "\n".join(report) + "\n"

    if a.dry_run:
        print("[dry-run] nothing written. Overrides that matter for PBR:")
        for k in sorted(overridden):
            if "compat/" in overridden[k][-1]:
                print(f"  {winner[k][0]}: {' -> '.join(overridden[k])}")
        return 0

    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    for rel, reader, _ in winner.values():
        full = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(reader())
    with open(os.path.join(a.out, "PACK_REPORT.txt"), "w", encoding="utf-8") as f:
        f.write(report_text)
    if a.zip:
        zpath = a.out.rstrip("/\\") + ".zip"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            for dp, _, fns in os.walk(a.out):
                for fn in fns:
                    full = os.path.join(dp, fn)
                    z.write(full, os.path.relpath(full, a.out))
        print("wrote", zpath)
    print("wrote", a.out)
    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
