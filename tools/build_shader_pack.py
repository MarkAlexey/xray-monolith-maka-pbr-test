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

Optional (--beef, --atmospherics), placed where they have to go:

    repo -> Beef's NVGs -> ES -> Beef's ES sky2.ps -> SSS (+ SSS's Beef patch) -> Atmospherics -> compat

    Beef's NVGs: its night_vision.h must win over the exe's copy (an older one without
      shader_param_8; SSS's NVG code and Atmospherics' sky2.ps do not compile with it).
      Its combine_1.ps, combine_2_naa.ps and pp_blur.ps are left out: SSS 23.5 already has
      Beef's NVG code in its own versions (enabled by "00 - PATCH - [ Beefs NVGs ]", added
      automatically), and Beef's copies would remove SSS's AO/IL/fog/SSR and PBR ambient
      handling from combine_1. Its sky2.ps comes from "Beef's NVGs - Patch (ES AND SSR)",
      Beef's ES-compatible version (byte-identical to the one Atmospherics ships).
    Atmospherics: ES is installed the way Atmospherics asks (ATMOSPHERICS_ES_MODULES: 01 PBR,
      Fixed Bloom, shader_param controls - simple; no "02 Enhanced Color Grading", which would
      grade the image a second time and wash it out).
      Its files: settings headers for SSS/ES, sky2.ps, shadow.h (cloud-shadow floor),
      pbr_settings.h (ES pseudo-PBR tuning). None of them touches the G-buffer, lighting,
      gbuffer_stage.h, hmodel/lmodel/pbr_brdf or SSLR, so no compat patching is needed. It
      goes after SSS so its settings headers win. Accepts the full mod or just its shaders
      folder (with or without a gamedata/ level), as a folder, .zip or .7z (needs py7zr).

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
  - Atmospherics' pbr_settings.h brightens and re-tunes ES's pseudo-PBR conversion of
    legacy textures (ALBEDO_BOOST etc.). Real PBR textures are not affected, so legacy and
    PBR materials drift apart a little; set ATMOSPHERICS_SKIP = {"pbr_settings.h"} to keep
    the ES/SSS values that pseudo-PBR was matched against.
  - --extra (repeatable): your own shader mods on top, e.g. a modpack's per-mod overrides
    (GAMMA). They go after SSS/ES/Atmospherics and before the compat patch, in the order given
    (later wins), so their settings headers (LUT layout, puddles, SSR, ...) match the textures
    the modpack ships. Each accepts a folder, .zip or .7z, with or without a gamedata/ level.
    A layer that replaces a PBR-critical file (PBR_CRITICAL) is listed after the build.
  - Build a modpack's pack on the same base stack the modpack uses. GAMMA runs on
    Atmospherics (its shadow.h cloud-shadow floor and pbr_settings.h albedo boost are what its
    weathers are tuned for), so GAMMA needs --beef and --atmospherics as well as --extra:
        --es ES --sss SSS --beef BEEF --atmospherics ATM --shaders-only \
        --extra <each GAMMA shader override> ...
    Without --atmospherics the world renders far too dark (no sun under cloud shadows, darker
    albedo) while the sky looks normal.
  - Settings headers in this pack (settings_screenspace_*.h) belong to the textures of the mod
    they come from. Installing the pack over a modpack that ships other textures (GAMMA's LUT
    atlas, for one) without --extra breaks those effects: a LUT layout mismatch remaps every
    colour (purple/green image).
  - Beef's other patch folders (Only ES / Only SSR / Arszi's) are not used: with SSS 23.5
    they are superseded, and Arszi's patch is a script for that mod only.
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

# ES modules used instead of ES_MODULES when --atmospherics is given: the set Atmospherics'
# install instructions ask for. NOT "02 Enhanced Color Grading": Atmospherics grades the image
# in its own img_corrections.h, where r__color_grading is a mild midtone tint. With "02" also
# installed, ES's ACES grading reads the same r__color_grading as a power curve
# (2 * (1 - value); Atmospherics' 0.75 gives 0.5) and the image looks washed out. Without "02"
# the "simple" module's files are not compiled at all (nothing includes them); it is listed
# because Atmospherics asks for it and it is harmless.
ATMOSPHERICS_ES_MODULES = [
    "01 Enhanced Shaders - PBR",
    "Optional - Fixed Bloom",
    "Optional - shader_param controls - simple",
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

BEEF_MODULE = "Beef's NVGs"
BEEF_ES_PATCH = "Beef's NVGs - Patch (ES AND SSR)"
BEEF_ES_PATCH_FILES = {"sky2.ps"}                   # only this file is taken from the ES patch
BEEF_SKIP = {"combine_1.ps", "combine_2_naa.ps", "pp_blur.ps", "sky2.ps"}  # SSS/ES integrate these
SSS_BEEF_PATCH = "00 - PATCH - [ Beefs NVGs ]"      # inserted after the SSS ES patch with --beef
ATMOSPHERICS_SKIP = set()                           # e.g. {"pbr_settings.h"}, see caveats

# Files the PBR / SSLR / Heatvision pipeline depends on. An --extra layer that ships one of these
# is reported: it will override the PBR version (the compat patch still wins for its own files).
PBR_CRITICAL = {
    "gbuffer_stage.h", "sload.h", "common.h", "hmodel.h", "lmodel.h", "pbr_brdf.h",
    "combine_1.ps", "deffer_base_bump.ps", "deffer_base_aref_bump.ps",
    "deffer_base_hud_bump.ps", "deffer_base_hud_aref_bump.ps",
    "accum_sun.ps", "accum_sun_near.ps", "accum_sun_far.ps", "accum_base.ps",
    "accum_omni_unshadowed.ps", "accum_indirect.ps", "screenspace_common.h", "ssfx_ssr_combine.ps",
}

INCLUDE_REPO_SHADERS = True     # this branch's gamedata/shaders (needed: ixray_brdf.h, sslr_*)
SHADERS_ONLY = False            # True = only gamedata/shaders from every layer
SKIP_NAMES = {"Thumbs.db", "desktop.ini", ".DS_Store"}
# Shader settings that describe a texture: left out of a SHADERS_ONLY pack (which ships no
# textures), so the copy that matches the installed texture stays in charge. The LUT layout
# must match lut_atlas.dds exactly (SSS 1024/32/4, Atmospherics 4096/64/17, GAMMA 4096/64/1);
# a mismatch remaps every colour. An --extra layer that ships one still wins.
TEXTURE_BOUND_SETTINGS = {"settings_screenspace_lut.h"}

# ------------------------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


class Source:
    """A zip or a folder; lists and reads files under a module prefix."""

    def __init__(self, path):
        self.path = path
        self.tmp = None
        if os.path.isfile(path) and path.lower().endswith(".7z"):
            try:
                import py7zr
            except ImportError:
                sys.exit(f"error: {path}: .7z needs py7zr (pip install py7zr) or extract it first")
            self.tmp = tempfile.mkdtemp(prefix="pbr_7z_")
            with py7zr.SevenZipFile(path) as z:
                z.extractall(self.tmp)
            self.path = path = self.tmp
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
            pre = self.root + ("" if module == "." else module.rstrip("/") + "/")
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


def gamedata_module(src):
    """(module, prefix) for a source that is one mod: its gamedata level may be missing"""
    if src.zip:
        names = [n[len(src.root):] for n in src.names]
        if any(n.lower().startswith("gamedata/") for n in names):
            return ".", ""
        return ".", "gamedata/"
    for d in os.listdir(src.path):
        if d.lower() == "gamedata":
            return ".", ""
    return ".", "gamedata/"


def keep(rel):
    parts = rel.split("/")
    if parts[-1] in SKIP_NAMES:
        return False
    if parts[0].lower() != "gamedata":
        return False                     # module-level readmes, images, bin/ etc.
    if SHADERS_ONLY and (len(parts) < 2 or parts[1].lower() != "shaders"):
        return False
    return True


def texture_bound(rel):
    return SHADERS_ONLY and rel.rsplit("/", 1)[-1].lower() in TEXTURE_BOUND_SETTINGS


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--es", required=True, help="Enhanced Shaders 1.10 zip or extracted folder")
    ap.add_argument("--sss", required=True, help="Screen Space Shaders 23.5 zip or extracted folder")
    ap.add_argument("--beef", help="Beef's NVGs zip or extracted folder (optional)")
    ap.add_argument("--atmospherics", help="Atmospherics (or just its shaders) folder/zip/7z (optional)")
    ap.add_argument("--extra", action="append", default=[], help="extra shader mod on top (repeatable, later wins)")
    ap.add_argument("--compat", help="existing compat patch folder/zip (default: generate it)")
    ap.add_argument("--no-compat", action="store_true",
                    help="leave the PBR compat patch out: the plain merged stack, e.g. as the base for make_pbr_overlay.py")
    ap.add_argument("--out", default="pbr_shader_pack")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    ap.add_argument("--shaders-only", action="store_true", help="same as SHADERS_ONLY = True")
    ap.add_argument("--dry-run", action="store_true", help="report what would be merged, write nothing")
    a = ap.parse_args()
    if a.shaders_only:
        global SHADERS_ONLY
        SHADERS_ONLY = True

    tmp = None
    if not a.compat and not a.no_compat:
        tmp = tempfile.mkdtemp(prefix="pbr_compat_")
        r = subprocess.run([sys.executable, os.path.join(HERE, "make_pbr_compat_patch.py"),
                            "--es", a.es, "--sss", a.sss, "--out", tmp], capture_output=True, text=True)
        if r.returncode:
            sys.exit("error: generating the compat patch failed:\n" + r.stderr)
        a.compat = tmp

    es, sss = Source(a.es), Source(a.sss)
    compat = Source(a.compat) if not a.no_compat else None
    # (label, source, module, prefix added to paths, file filter on the base name)
    layers = []
    if INCLUDE_REPO_SHADERS:
        layers.append(("repo", Source(REPO), ".", "", None))
    beef = Source(a.beef) if a.beef else None
    if beef:
        layers.append(("Beef/" + BEEF_MODULE, beef, BEEF_MODULE, "", lambda n: n.lower() not in BEEF_SKIP))
    es_modules = ATMOSPHERICS_ES_MODULES if a.atmospherics else ES_MODULES
    if a.atmospherics:
        print("Atmospherics: using its ES modules: " + ", ".join(es_modules))
    layers += [("ES/" + m, es, m, "", None) for m in es_modules]
    if beef:
        layers.append(("Beef/" + BEEF_ES_PATCH, beef, BEEF_ES_PATCH, "", lambda n: n.lower() in BEEF_ES_PATCH_FILES))
    sss_mods = list(SSS_MODULES)
    if beef and SSS_BEEF_PATCH not in sss_mods:
        anchor = "00 - PATCH - [ Enhanced Shaders 1.10 ]"
        sss_mods.insert(sss_mods.index(anchor) + 1 if anchor in sss_mods else 0, SSS_BEEF_PATCH)
    layers += [("SSS/" + m, sss, m, "", None) for m in sss_mods]
    if a.atmospherics:
        atm = Source(a.atmospherics)
        mod, prefix = gamedata_module(atm)
        layers.append(("Atmospherics", atm, mod, prefix, lambda n: n.lower() not in {s.lower() for s in ATMOSPHERICS_SKIP}))
    if compat is None:
        cmods = []
    elif compat.zip:
        cmods = sorted({n[len(compat.root):].split("/", 1)[0] for n in compat.names if n[len(compat.root):].startswith("99 ")})
    else:
        cmods = sorted(d for d in os.listdir(a.compat) if d.startswith("99 "))
    extra_labels = []
    for i, e in enumerate(a.extra, 1):
        src = Source(e)
        mod, prefix = gamedata_module(src)
        label = f"extra{i}/{os.path.basename(e.rstrip('/'))}"
        extra_labels.append(label)
        layers.append((label, src, mod, prefix, None))
    layers += [("compat/" + m, compat, m, "", None) for m in cmods]

    winner, history, missing = {}, {}, []
    for label, src, mod, prefix, flt in layers:
        try:
            for rel, reader in src.files(mod):
                rel = prefix + rel
                if flt and not flt(rel.rsplit("/", 1)[-1]):
                    continue
                if label == "repo" and not rel.lower().startswith("gamedata/shaders/"):
                    continue                          # the exe's other gamedata is not part of the pack
                if not keep(rel):
                    continue
                if texture_bound(rel) and not label.startswith("extra"):
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
    for label, *_ in layers:
        print(f"  {by_layer.get(label, 0):5d} winning  {label}")

    report = ["PBR shader pack - merge report", "", "Layers, lowest priority first:"]
    report += [f"  {l[0]}" for l in layers]
    report += ["", "Files provided by more than one layer (last one wins):"]
    for k in sorted(overridden):
        report.append(f"  {winner[k][0]}\n      {' -> '.join(overridden[k])}")
    critical = []
    for k, hist in history.items():
        name = k.rsplit("/", 1)[-1]
        if name in PBR_CRITICAL and any(h in extra_labels for h in hist):
            critical.append(f"{winner[k][0]}: {' -> '.join(hist)}")
    if critical:
        report += ["", "WARNING: extra layers ship PBR-critical files (check the winner is right):"]
        report += [f"  {c}" for c in sorted(critical)]
        print("WARNING: extra layers ship PBR-critical files:\n  " + "\n  ".join(sorted(critical)))
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
