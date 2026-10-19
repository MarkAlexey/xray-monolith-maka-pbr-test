#!/usr/bin/env python3
"""
make_shader_fomod.py - wrap the GAMMA shader packs into ONE Mod Organizer installer (FOMOD).

build_shader_packs.py makes two zips per shader stack:
  PBR_overlay_GAMMA.zip   only the files PBR needs, patched from GAMMA's own stack (installed last)
  PBR_shaders_GAMMA.zip   GAMMA's whole shader stack with PBR merged in (replaces GAMMA's shader mods)
On a DLSS / FSR3 branch it makes them twice: once for the plain GAMMA stack and once with
brodrigz's DLSS compatibility patch folded on top (--top-layer). Picking the wrong one of four
zips is easy; this tool puts them in one FOMOD that asks two questions and installs exactly one:

  Page 1  Install type     Overlay (recommended) | Full pack
  Page 2  DLSS / FSR 3     With DLSS / FSR 3     | Without            (only with --dlss-* zips)

Layout inside the zip (what Mod Organizer sees):
  fomod/info.xml, fomod/ModuleConfig.xml
  docs/                          README.txt plus each variant's own README / PACK_REPORT
  variants/<id>/gamedata/...     one folder per variant: overlay, full, overlay_dlss, full_dlss
The chosen variant's gamedata/ lands in the mod's gamedata/; docs/ is always installed into
"PBR shaders - Instructions" (no game files there).

Usage
  python make_shader_fomod.py --overlay plain/PBR_overlay_GAMMA.zip --full plain/PBR_shaders_GAMMA.zip \\
      [--dlss-overlay dlss/PBR_overlay_GAMMA.zip --dlss-full dlss/PBR_shaders_GAMMA.zip] \\
      --out PBR_shaders_GAMMA_FOMOD.zip [--version 2026.10.20] [--dry-run]
  --dry-run lists what would go into the installer and writes nothing.

Caveats
  - The variants are built for GAMMA's shader stack as it was in the sources repository when the
    release was built. A different Atmospherics or NVG mod than GAMMA's needs a rebuilt overlay
    (tools/make_pbr_overlay.py), not this installer.
  - "With DLSS / FSR 3" is built with brodrigz's 0.5 patch and the Atmospherics component the
    release was built with (DLSS_PATCH_COMPONENTS in the workflow). The overlay needs that patch
    installed below it; its Better Beef's NVGs option is for that mod only (GAMMA ships its own
    NVG files, and that option replaces combine_2_naa.ps). The full pack already contains the patch's
    shaders (its menu script and strings are in the engine's db0), so the patch is optional there.
  - Mod Organizer remembers the last choices; reinstalling with "Replace" removes files of a
    previous choice, "Merge" keeps them (pick Replace).
"""

import argparse
import datetime
import os
import sys
import zipfile
from xml.sax.saxutils import escape

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

MOD_NAME = "PBR shaders (GAMMA)"
AUTHOR = "MarkAlexey / xray-monolith-maka-pbr-test"
WEBSITE = "https://github.com/MarkAlexey/xray-monolith-maka-pbr-test"
DOCS_DEST = "PBR shaders - Instructions"     # folder in the mod for the docs (not game files)

# Variant id -> (install type flag, dlss flag, label)
VARIANTS = {
    "overlay":      ("overlay", "off", "Overlay"),
    "full":         ("full",    "off", "Full pack"),
    "overlay_dlss": ("overlay", "on",  "Overlay, DLSS / FSR 3"),
    "full_dlss":    ("full",    "on",  "Full pack, DLSS / FSR 3"),
}

OVERLAY_TEXT = ("Only the shader files PBR needs, patched from GAMMA's own versions. Every other GAMMA "
                "shader stays as it is.\n\nPut this mod at the very bottom of Mod Organizer's left pane "
                "(highest priority, above every shader mod). Every file of it must show as winning.")
FULL_TEXT = ("GAMMA's complete shader stack (Enhanced Shaders, Screen Space Shaders, Atmospherics, "
             "Beef's NVGs, GAMMA's shader overrides) with PBR merged in, as one mod.\n\nPut it above "
             "all of them; they can stay enabled below it or be disabled.")
DLSS_ON_TEXT = ("PBR merged with brodrigz's DLSS / FSR3 compatibility patch 0.5 (attached to this release).\n\n"
                "Overlay: install that patch first (its \"Better Beef's NVGs\" option only if you have that mod) and put this "
                "mod ABOVE it. The files both change (night_vision.h, deffer_impl_flat.ps, the grass shader) "
                "are merged here, so the patch's own copies must lose to these.\n\n"
                "Full pack: the DLSS shaders are already inside; the patch can stay below it or be left out.")
DLSS_OFF_TEXT = ("Plain GAMMA shaders, no DLSS / FSR3 shaders. Upscaling stays off; picking DLSS or FSR 3 "
                 "in the menu shows which shader is missing.")

README = """PBR shaders (GAMMA) - installer {version}
{website}

One installer for the PBR shader files on GAMMA. It asks:
  1. Install type: Overlay (recommended) or Full pack.
{dlss_line}
Then:
  - Overlay: put this mod at the very bottom of the left pane (highest priority).
  - Full pack: put it above GAMMA's shader mods.
  - Delete appdata/shaders_cache/r4 and start the game.
{dlss_steps}
Changing your answers: reinstall this archive in Mod Organizer and pick REPLACE.

Built for GAMMA's shader stack at release time. With other shader mods than GAMMA's, generate
your own overlay with tools/make_pbr_overlay.py (tools zip of the release).

The variants' own reports are next to this file:
{reports}
"""

# ------------------------------------------------------------------------------------------


def read_pack(path):
    """{relative path under the pack's top folder: bytes} and the top folder name"""
    out, top = {}, None
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            parts = info.filename.split("/", 1)
            if len(parts) != 2:
                continue
            top = top or parts[0]
            out[parts[1]] = z.read(info)
    if not any(k.lower().startswith("gamedata/") for k in out):
        sys.exit(f"error: {path} has no gamedata/ under its top folder")
    return out, top


def plugin(name, text, flags, kind):
    fl = "".join(f'<flag name="{escape(k)}">{escape(v)}</flag>' for k, v in flags.items())
    return (f'<plugin name="{escape(name)}"><description>{escape(text)}</description>'
            f'<conditionFlags>{fl}</conditionFlags><typeDescriptor><type name="{kind}"/></typeDescriptor></plugin>')


def module_config(variants, with_dlss):
    steps = [
        '<installStep name="1. Install type"><optionalFileGroups order="Explicit">'
        '<group name="Pick one" type="SelectExactlyOne"><plugins order="Explicit">'
        + plugin("Overlay (recommended)", OVERLAY_TEXT, {"type": "overlay"}, "Recommended")
        + plugin("Full pack", FULL_TEXT, {"type": "full"}, "Optional")
        + "</plugins></group></optionalFileGroups></installStep>"]
    if with_dlss:
        steps.append(
            '<installStep name="2. DLSS / FSR 3"><optionalFileGroups order="Explicit">'
            '<group name="Upscaling shaders" type="SelectExactlyOne"><plugins order="Explicit">'
            + plugin("With DLSS / FSR 3 (recommended)", DLSS_ON_TEXT, {"dlss": "on"}, "Recommended")
            + plugin("Without DLSS / FSR 3", DLSS_OFF_TEXT, {"dlss": "off"}, "Optional")
            + "</plugins></group></optionalFileGroups></installStep>")
    patterns = []
    for vid in variants:
        typ, dlss, _ = VARIANTS[vid]
        deps = f'<flagDependency flag="type" value="{typ}"/>'
        if with_dlss:
            deps += f'<flagDependency flag="dlss" value="{dlss}"/>'
        patterns.append(f'<pattern><dependencies operator="And">{deps}</dependencies><files>'
                        f'<folder source="variants\\{vid}\\gamedata" destination="gamedata" priority="0"/>'
                        f'</files></pattern>')
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" '
            'xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">'
            f'<moduleName>{escape(MOD_NAME)}</moduleName>'
            f'<requiredInstallFiles><folder source="docs" destination="{escape(DOCS_DEST)}" priority="0"/></requiredInstallFiles>'
            f'<installSteps order="Explicit">{"".join(steps)}</installSteps>'
            f'<conditionalFileInstalls><patterns>{"".join(patterns)}</patterns></conditionalFileInstalls>'
            '</config>\n')


def info_xml(version):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<fomod>'
            f'<Name>{escape(MOD_NAME)}</Name><Author>{escape(AUTHOR)}</Author>'
            f'<Version>{escape(version)}</Version><Website>{escape(WEBSITE)}</Website>'
            '<Description>PBR shader files for GAMMA: overlay or full pack, with or without the DLSS / FSR3 shaders.</Description>'
            '</fomod>\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--full", required=True)
    ap.add_argument("--dlss-overlay")
    ap.add_argument("--dlss-full")
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", default=datetime.date.today().strftime("%Y.%m.%d"))
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if bool(a.dlss_overlay) != bool(a.dlss_full):
        sys.exit("error: give both --dlss-overlay and --dlss-full, or neither")
    sources = {"overlay": a.overlay, "full": a.full}
    if a.dlss_overlay:
        sources.update(overlay_dlss=a.dlss_overlay, full_dlss=a.dlss_full)
    with_dlss = "overlay_dlss" in sources

    packs = {vid: read_pack(p) for vid, p in sources.items()}
    reports = []
    for vid, (files, top) in packs.items():
        game = [k for k in files if k.lower().startswith("gamedata/")]
        docs = [k for k in files if not k.lower().startswith("gamedata/")]
        print(f"{vid:13} {VARIANTS[vid][2]:24} {len(game):4} game files, docs: {', '.join(docs) or '-'}  <- {sources[vid]}")
        reports += [f"  {vid}_{d}" for d in docs]

    dlss_line = "  2. DLSS / FSR 3: with or without the upscaling shaders.\n" if with_dlss else ""
    dlss_steps = ("  - Overlay with DLSS / FSR 3: install brodrigz's DLSS patch first (its Better Beef's\n"
                  "    NVGs option only if you have that mod), then this mod ABOVE it. The full pack does not need the patch.\n"
                  "  - In game: MSAA Off, then DLSS or FSR 3 in Settings > Video > Basic.\n") if with_dlss else ""
    readme = README.format(version=a.version, website=WEBSITE, dlss_line=dlss_line, dlss_steps=dlss_steps,
                           reports="\n".join(reports) or "  (none)")
    if a.dry_run:
        print("[dry-run] would write", a.out)
        return 0

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("fomod/info.xml", info_xml(a.version))
        z.writestr("fomod/ModuleConfig.xml", module_config(list(sources), with_dlss))
        z.writestr("docs/README.txt", readme.replace("\n", "\r\n"))
        for vid, (files, _) in packs.items():
            for rel, data in files.items():
                if rel.lower().startswith("gamedata/"):
                    z.writestr(f"variants/{vid}/{rel}", data)
                else:
                    z.writestr(f"docs/{vid}_{rel}", data)
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
