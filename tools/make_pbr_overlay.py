#!/usr/bin/env python3
"""
make_pbr_overlay.py - a small "PBR overlay" mod with ONLY the shader files the PBR pipeline
needs, patched from whatever versions YOUR mod list actually uses.

Why: a merged shader pack (build_shader_pack.py) replaces every shader of SSS / ES /
Atmospherics / your modpack with its own copy, so it has to be rebuilt to match each modpack
(GAMMA runs on Atmospherics, has its own LUT, puddles, DOF, NVG shaders, ...). The overlay does
the opposite: it takes your existing shader stack as it is, finds the winning version of each
file the PBR pipeline depends on, adds the PBR code to THAT version, and writes only those
files. Everything else stays exactly as your mods ship it.

    Install the overlay LAST (highest priority in Mod Organizer, above every shader mod).
    Regenerate it whenever you add, remove or reorder a shader mod below it.

Files it can patch (the same edits as make_pbr_compat_patch.py, applied to the winner):
  PBR textures (PATCH_PBR_TEXTURES)
    gbuffer_stage.h                    PBR material target (2), Heatvision moved to target 4
    deffer_base_bump.ps, deffer_base_aref_bump.ps,
    deffer_base_hud_bump.ps, deffer_base_hud_aref_bump.ps
                                       write the PBR material (pbr_hud.h)
    accum_sun.ps, accum_sun_near.ps, accum_sun_far.ps, accum_base.ps,
    accum_omni_unshadowed.ps, accum_indirect.ps
                                       IX-Ray PBR lighting for PBR pixels
    combine_1.ps                       IX-Ray ambient / reflections for PBR pixels
  Legacy textures, SSLR and reflections (PATCH_LEGACY_IXRAY)
    pbr_brdf.h, lmodel.h, hmodel.h     IX-Ray BRDF for Enhanced Shaders' pseudo-PBR, SSLR,
                                       cubemap remap, HDR reflections
    screenspace_common.h               SSS sky/env lookups through the cubemap remap
    ssfx_ssr_combine.ps                SSS SSR off where IX-Ray SSLR owns the pixel

Per file, one of:
  patched     the winning version was patched and goes into the overlay
  exe         no mod overrides it: the exe's own PBR version is used, nothing to do
  done        the winner already has the PBR code (from this repository's shaders)
  stale       the winner was patched by an earlier PBR pack / compat patch. Take that pack
              out of the layers (the overlay replaces it) and run again
  FAILED      a REQUIRED file whose winner the patch anchors do not match (a modpack rewrote
              it): the run stops
  skipped     the same for an optional file: it is left out with a warning and the mod's own
              version stays. Expected for lmodel.h / hmodel.h under Atmospherics 2026, which
              brings its own BRDF (the IX-Ray BRDF for legacy textures does not apply there;
              PBR textures still get IX-Ray lighting)

Usage
  python make_pbr_overlay.py --layer <lowest mod> --layer <next> ... --out "PBR overlay" [--zip]
  python make_pbr_overlay.py --layer merged_shaders_folder --out "PBR overlay" --dry-run
  Each --layer is a folder, .zip or .7z: a mod (with or without gamedata/), a shaders folder,
  or one already-merged copy of your whole shader stack. Lowest priority first, like Mod
  Organizer's left pane top to bottom. Leave out the exe's gamedata and earlier PBR packs.
  --repo <path> is this repository (default: the folder above tools/), for the PBR headers.

Caveats
  - The overlay is only as right as the layer list: a mod you forget to list can still win in
    the game and silently undo a patched file. Check Mod Organizer's conflict tab: every overlay
    file must be "winning".
  - Headers only this repository ships (pbr_ixray.h, pbr_hud.h, ixray_brdf.h, sslr_*.h,
    hdr_ibl.h) are copied in when INCLUDE_REPO_HEADERS is on, so the overlay matches the PBR
    code it was made with. They are also in the exe's gamedata archive: if you update the exe
    later, regenerate the overlay (or turn the option off and rely on the exe's copies).
  - Under SSS's sload.h, PBR textures get no POM / parallax and no detail bump (see pbr_hud.h);
    the r2 static-sun term is not applied to PBR pixels. SSS Indirect Light is, on the ambient
    diffuse (PBR_IL_INTENSITY in pbr_ixray.h).
"""

import argparse
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_pbr_compat_patch as compat   # noqa: E402  (patch functions, BANNER)
from build_shader_pack import Source      # noqa: E402  (folder / zip / 7z reader)

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

# Patch the files PBR textures need (G-buffer, lighting, ambient). Without them PBR textures
# render as legacy surfaces (metals come out matte white).
PATCH_PBR_TEXTURES = True

# Also patch Enhanced Shaders' BRDF / SSS's reflection files (IX-Ray BRDF for legacy textures,
# IX-Ray SSLR, cubemap remap, HDR reflections). Off = only PBR textures change; legacy textures
# keep their mods' look exactly.
PATCH_LEGACY_IXRAY = True

# Copy this repository's PBR-only headers into the overlay (see the caveat above).
INCLUDE_REPO_HEADERS = True
REPO_HEADERS = ["pbr_ixray.h", "pbr_hud.h", "ixray_brdf.h", "hdr_ibl.h", "sslr_apply.h", "sslr_settings.h",
                "vslr_common.h"]

# Files whose failure stops the run: without them PBR textures are wrong. Any other file that
# fails is left out with a warning.
REQUIRED = {"gbuffer_stage.h", "deffer_base_bump.ps", "deffer_base_aref_bump.ps",
            "deffer_base_hud_bump.ps", "deffer_base_hud_aref_bump.ps", "combine_1.ps",
            "accum_sun_near.ps", "accum_sun_far.ps", "accum_base.ps"}

# ------------------------------------------------------------------------------------------

PBR_FILES = {
    "gbuffer_stage.h": compat.patch_gbuffer_stage,
    "deffer_base_bump.ps": compat.patch_hud,
    "deffer_base_aref_bump.ps": compat.patch_hud,
    "deffer_base_hud_bump.ps": compat.patch_hud,
    "deffer_base_hud_aref_bump.ps": compat.patch_hud,
    "accum_sun.ps": compat.make_light_patch("sun"),
    "accum_sun_near.ps": compat.make_light_patch("sun"),
    "accum_sun_far.ps": compat.make_light_patch("sun"),
    "accum_base.ps": compat.make_light_patch("local"),
    "accum_omni_unshadowed.ps": compat.make_light_patch("omni"),
    "accum_indirect.ps": compat.make_light_patch("indirect"),
    "combine_1.ps": compat.patch_combine,
}
LEGACY_FILES = {
    "pbr_brdf.h": compat.patch_pbr_brdf,
    "lmodel.h": compat.patch_lmodel,
    "hmodel.h": compat.patch_hmodel,
    "screenspace_common.h": compat.patch_screenspace_common,
    "ssfx_ssr_combine.ps": compat.patch_ssr_combine,
}

# A winner containing one of these already has PBR code written by this repository
# (the exe's own shaders): nothing to patch.
NATIVE_MARKERS = ('#include "pbr_ixray.h"', "#define PBR_GBUFFER", "ES_IXRAY_BRDF", "ixr_env_dir(")

R3 = re.compile(r"(?:^|/)shaders/r3/(.+)$", re.I)

README = """PBR overlay - generated by tools/make_pbr_overlay.py
(xray-monolith-maka-pbr-test, branch pbr-test-s3ds-mt-sss)

Install LAST: highest priority in Mod Organizer, above every shader mod. Every file in it
must show as "winning" in Mod Organizer's conflict tab. Delete appdata/shaders_cache/r4.
Regenerate it whenever you add, remove or reorder a shader mod below it.

Made from these layers (lowest priority first):
{layers}
Files:
{files}"""


def load_layer(path):
    """{lower-case path under shaders/r3: (path, reader)} for one layer"""
    src = Source(path)
    files, loose = {}, {}
    for rel, reader in src.files("."):
        m = R3.search(rel)
        if m:
            files[m.group(1).lower()] = (m.group(1), reader)
        elif "/" not in rel:
            loose[rel.lower()] = (rel, reader)     # a bare r3 folder
    return files or loose


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--layer", action="append", required=True,
                    help="shader mod / folder / zip / 7z, lowest priority first (repeatable)")
    ap.add_argument("--out", default="PBR overlay", help="output mod folder")
    ap.add_argument("--repo", default=os.path.dirname(HERE), help="this repository (for the PBR headers)")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    ap.add_argument("--dry-run", action="store_true", help="report what would be patched, write nothing")
    a = ap.parse_args()

    winner = {}
    for i, path in enumerate(a.layer, 1):
        label = "%d: %s" % (i, os.path.basename(path.rstrip("/\\")))
        layer = load_layer(path)
        if not layer:
            sys.exit(f"error: no shaders/r3 files in {path}")
        for key, (rel, reader) in layer.items():
            winner[key] = (rel, reader, label)

    jobs = {}
    if PATCH_PBR_TEXTURES:
        jobs.update(PBR_FILES)
    if PATCH_LEGACY_IXRAY:
        jobs.update(LEGACY_FILES)

    out, report, failed, stale = [], [], [], []
    for name, fn in jobs.items():
        if name not in winner:
            report.append(f"  exe      {name}")
            continue
        rel, reader, label = winner[name]
        text, crlf = compat.decode(reader())
        if compat.BANNER in text:
            stale.append(f"{name} (from {label})")
            report.append(f"  stale    {name}  <- {label}")
            continue
        if any(m in text for m in NATIVE_MARKERS):
            report.append(f"  done     {name}  <- {label}")
            continue
        try:
            patched = fn(text, f"{label}:{name}")
        except compat.PatchError as e:
            failed.append((name, str(e)))
            report.append(f"  {'FAILED ' if name in REQUIRED else 'skipped'}  {name}  <- {label}")
            continue
        out.append((rel, compat.encode(patched, crlf)))
        report.append(f"  patched  {name}  <- {label}")

    if INCLUDE_REPO_HEADERS and out:
        r3 = os.path.join(a.repo, "gamedata", "shaders", "r3")
        for h in REPO_HEADERS:
            p = os.path.join(r3, h)
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    out.append((h, f.read()))
                report.append(f"  header   {h}  <- repository")
            else:
                print(f"warning: {p} not found, the exe's copy of {h} will be used", file=sys.stderr)

    print("\n".join(report))
    hard = [(n, e) for n, e in failed if n in REQUIRED]
    for n, e in failed:
        print(f"{'error' if n in REQUIRED else 'warning'}: {e}", file=sys.stderr)
    if stale:
        print("error: already patched by an earlier PBR pack / compat patch: " + ", ".join(stale) +
              "\n  take that pack out of the layers (this overlay replaces it) and run again", file=sys.stderr)
    if hard or stale:
        return 1
    if a.dry_run:
        print("[dry-run] nothing written")
        return 0
    if not out:
        print("nothing to patch: no mod overrides the PBR files")
        return 0

    files = [(os.path.join("gamedata", "shaders", "r3", rel), data) for rel, data in out]
    files.append(("README.txt", README.format(
        layers="".join(f"  {p}\n" for p in a.layer), files="\n".join(report) + "\n").encode()))
    for rel, data in files:
        full = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)
    if a.zip:
        top = os.path.basename(a.out.rstrip("/\\"))
        with zipfile.ZipFile(a.out.rstrip("/\\") + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
            for rel, data in files:
                z.writestr(f"{top}/{rel}".replace(os.sep, "/"), data)
    print(f"wrote {a.out} ({len(out)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
