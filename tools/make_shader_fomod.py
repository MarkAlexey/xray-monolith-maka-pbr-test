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
  Page 3  HUD blood        With (experiment)     | Without            (only with --blood)

HUD blood (--blood, the zip of tools/make_hud_blood_overlay.py) is installed on top of whichever
variant was picked: its HUD shaders are the variants' own deffer_base_hud_bump.ps /
deffer_base_hud_aref_bump.ps with the blood call, so the tool stops when the variants do not all
share the same two files (the blood zip would then fit only some of them).

Layout inside the zip, the same as brodrigz's DLSS / FSR3 compatibility patch zip so the two
read alike:
  README.txt                       how to install (also docs/README.txt)
  components/<id>/gamedata/...     one folder per variant: overlay, full, overlay_dlss, full_dlss
                                   (+ hud_blood with --blood)
  docs/README.txt, docs/CREDITS.txt, docs/<id>_<report>   (each variant's own README / PACK_REPORT)
  fomod/info.xml, fomod/ModuleConfig.xml
  selections/<id>.txt              which variant was picked, one line each
The chosen variant's gamedata/ lands in the mod's gamedata/. With INSTALL_DOCS on, docs/ and the
picked selections/<id>.txt are also installed into DOCS_DEST ("PBR shaders - Instructions", no game
files there), like the DLSS patch does with its instructions folder; off (default), only game files
are installed and the instructions stay in the archive.

Usage
  python make_shader_fomod.py --overlay plain/PBR_overlay_GAMMA.zip --full plain/PBR_shaders_GAMMA.zip \\
      [--dlss-overlay dlss/PBR_overlay_GAMMA.zip --dlss-full dlss/PBR_shaders_GAMMA.zip] \\
      [--blood "HUD blood.zip"] --out PBR_shaders_GAMMA_FOMOD.zip [--version 2026.10.20] [--dry-run]
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
INSTALL_DOCS = False                        # ship docs/ and install it into DOCS_DEST
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
BLOOD_ON_TEXT = ("Blood on the player's hands and weapons (experiment): grows with hits, bleeding and close kills, "
                 "dries, flakes off, washes off in the rain and can be wiped off with a cloth (\"Wipe off blood\" "
                 "on cloths).\n\nNeeds SSS's HUD Raindrops component (GAMMA has it). Tuning: "
                 "gamedata/configs/plugins/hud_blood.ltx. Installed with this mod, above the PBR shaders.")
BLOOD_OFF_TEXT = "No HUD blood. The first-person shaders stay exactly as the PBR shaders ship them."
HUD_SHADERS = ("gamedata/shaders/r3/deffer_base_hud_bump.ps", "gamedata/shaders/r3/deffer_base_hud_aref_bump.ps")
BLOOD_ID = "hud_blood"

DLSS_OFF_TEXT = ("Plain GAMMA shaders, no DLSS / FSR3 shaders. Upscaling stays off; picking DLSS or FSR 3 "
                 "in the menu shows which shader is missing.")

README = """PBR SHADERS (GAMMA) - {version}
{website}

INSTALL
-------
1. Install the PBR-Test binaries (bin/ and db/ of the release zip) into the Anomaly folder.
2. Install this ZIP through MO2:
   - Page 1: Overlay (recommended) or Full pack.
{dlss_line}{blood_line}3. Overlay: enable this mod at the very bottom of MO2's left pane (highest priority,
   above every shader mod). Every file of it must show as winning.
   Full pack: enable it above GAMMA's shader mods; they can stay enabled below it.
{dlss_steps}4. Clear the shader cache (delete appdata/shaders_cache/r4) and start the game.

UPDATING OR CHANGING OPTIONS
---------------------------
Reinstall this archive when changing the install type, the DLSS / FSR 3 or the HUD blood choice.
Choose REPLACE in MO2 so files from your old selections are removed.

OTHER SHADER STACKS
-------------------
Built for GAMMA's shader stack as it was at release time. With other shader mods than GAMMA's,
generate your own overlay with tools/make_pbr_overlay.py (tools zip of the release).

IF IT DOES NOT WORK
-------------------
Check the install type, MO2 left-pane priority and that the shader cache was cleared first.
If it still fails, share your full appdata/logs/xray_<username>.log, enabled modlist and
installer choices when asking for help.

The variants' own reports are in docs/:
{reports}
"""

CREDITS = """PBR SHADERS (GAMMA) CREDITS
============================
MarkAlexey (Maka), Zirael and the PBR-Test contributors - PBR pipeline and its GAMMA integration
{website}

IX-Ray Team - the PBR pipeline it is ported from (LVutner, ForserX, Drombeys, mnelenpridumivat,
v2v3v4, NewKidHere and the IX-Ray contributors)
https://github.com/ixray-team/ixray-1.6-stcop

Ascii1457 - Screen Space Shaders
https://www.moddb.com/addons/screen-space-shaders

KennShade - Enhanced Shaders

Hippobot - Atmospherics
https://www.moddb.com/mods/stalker-anomaly/addons/hippos-shaders-and-atmospherics

theRealBeef and the contributors credited in night_vision.h - Beef's NVGs
https://www.moddb.com/addons/beefs-shader-based-nvgs-v10

indiesunpraiser - Better Beef's NVGs
https://www.moddb.com/mods/stalker-anomaly/addons/better-beefs-nvg-indiesunpraiser/

bepis (brodrigz) - DLSS / FSR3 compatibility patch; OGSR team and xrSimpAI - original DLSS
implementation
https://github.com/brodrigz/xray-monolith

GAMMA and its shader overrides, GSC Game World, Anomaly, themrdemonized/xray-monolith and fork
contributors
https://github.com/themrdemonized/xray-monolith

Original author comments and ownership remain with their respective authors; their credits are
retained in the files.
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


def plugin(name, text, flags, kind, files=""):
    fl = "".join(f'\n                <flag name="{escape(k)}">{escape(v)}</flag>' for k, v in flags.items())
    return (f'\n            <plugin name="{escape(name)}">'
            f'\n              <description>{escape(text)}</description>'
            + (f'\n              <files>{files}\n              </files>' if files else '') +
            f'\n              <conditionFlags>{fl}\n              </conditionFlags>'
            f'\n              <typeDescriptor>\n                <type name="{kind}" />\n              </typeDescriptor>'
            f'\n            </plugin>')


def step(name, group, kind, plugins):
    return (f'\n    <installStep name="{escape(name)}">\n      <optionalFileGroups order="Explicit">'
            f'\n        <group name="{escape(group)}" type="{kind}">\n          <plugins order="Explicit">'
            + "".join(plugins) +
            '\n          </plugins>\n        </group>\n      </optionalFileGroups>\n    </installStep>')


def module_config(variants, with_dlss, with_blood, version):
    steps = [step("1. Install type", "Pick one", "SelectExactlyOne", [
        plugin("Overlay (recommended)", OVERLAY_TEXT, {"type": "overlay"}, "Recommended"),
        plugin("Full pack", FULL_TEXT, {"type": "full"}, "Optional")])]
    if with_dlss:
        steps.append(step("2. DLSS / FSR 3", "Upscaling shaders", "SelectExactlyOne", [
            plugin("With DLSS / FSR 3 (recommended)", DLSS_ON_TEXT, {"dlss": "on"}, "Recommended"),
            plugin("Without DLSS / FSR 3", DLSS_OFF_TEXT, {"dlss": "off"}, "Optional")]))
    if with_blood:
        steps.append(step(f"{len(steps) + 1}. HUD blood", "Blood on hands and weapons", "SelectExactlyOne", [
            plugin("With HUD blood (experiment)", BLOOD_ON_TEXT, {"blood": "on"}, "Recommended"),
            plugin("Without HUD blood", BLOOD_OFF_TEXT, {"blood": "off"}, "Optional")]))
    patterns = []
    for vid in variants:
        typ, dlss, _ = VARIANTS[vid]
        deps = f'\n          <flagDependency flag="type" value="{typ}" />'
        if with_dlss:
            deps += f'\n          <flagDependency flag="dlss" value="{dlss}" />'
        files = f'\n          <folder source="components\\{vid}\\gamedata" destination="gamedata" priority="0" />'
        if INSTALL_DOCS:
            files += (f'\n          <file source="selections\\{vid}.txt" '
                      f'destination="{escape(DOCS_DEST)}\\Selected variant.txt" priority="0" />')
        patterns.append(f'\n      <pattern>\n        <dependencies operator="And">{deps}\n        </dependencies>'
                        f'\n        <files>{files}\n        </files>\n      </pattern>')
    if with_blood:
        # Above the variant (priority 1): its two HUD shaders replace the variant's
        patterns.append('\n      <pattern>\n        <dependencies operator="And">'
                        '\n          <flagDependency flag="blood" value="on" />\n        </dependencies>'
                        f'\n        <files>\n          <folder source="components\\{BLOOD_ID}\\gamedata" '
                        'destination="gamedata" priority="1" />\n        </files>\n      </pattern>')
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" '
            'xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">'
            f'\n  <moduleName>{escape(MOD_NAME)} - {escape(version)}</moduleName>'
            + (f'\n  <requiredInstallFiles>\n    <folder source="docs" destination="{escape(DOCS_DEST)}" priority="0" />'
               '\n  </requiredInstallFiles>' if INSTALL_DOCS else '') +
            f'\n  <installSteps order="Explicit">{"".join(steps)}\n  </installSteps>'
            f'\n  <conditionalFileInstalls>\n    <patterns>{"".join(patterns)}\n    </patterns>\n  </conditionalFileInstalls>'
            '\n</config>\n')


def info_xml(version, with_blood):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<fomod>'
            f'\n  <Name>{escape(MOD_NAME)}</Name>\n  <Author>{escape(AUTHOR)}</Author>'
            f'\n  <Version>{escape(version)}</Version>'
            '\n  <Description>PBR shader files for GAMMA: overlay or full pack, with or without the DLSS / FSR3 shaders'
            + (', optional HUD blood (experiment)' if with_blood else '') + '.</Description>'
            f'\n  <Website>{escape(WEBSITE)}</Website>\n</fomod>\n')


def selection_text(vid, version):
    _, _, label = VARIANTS[vid]
    return f"Selected: {label}.\nBuilt: {version}\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--full", required=True)
    ap.add_argument("--dlss-overlay")
    ap.add_argument("--dlss-full")
    ap.add_argument("--blood", help="HUD blood zip (tools/make_hud_blood_overlay.py): adds the HUD blood page")
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

    blood = None
    if a.blood:
        blood, _ = read_pack(a.blood)
        # The blood zip patches the variants' HUD shaders: they must all be the same two files
        for key in HUD_SHADERS:
            seen = {vid: next((d for k, d in files.items() if k.lower() == key), None) for vid, (files, _) in packs.items()}
            if len(set(seen.values())) != 1 or None in seen.values():
                sys.exit(f"error: --blood: the variants do not share one {key.split('/')[-1]} "
                         f"({', '.join(v for v, d in seen.items() if d is None) or 'they differ'}); "
                         f"the blood zip fits only the variant it was built from")
            if not any(k.lower() == key for k in blood):
                sys.exit(f"error: --blood {a.blood} has no {key}: not a make_hud_blood_overlay.py zip")
        game = [k for k in blood if k.lower().startswith("gamedata/")]
        docs = [k for k in blood if not k.lower().startswith("gamedata/")]
        print(f"{BLOOD_ID:13} {'HUD blood (experiment)':24} {len(game):4} game files, docs: {', '.join(docs) or '-'}  <- {a.blood}")
        reports += [f"  {BLOOD_ID}_{d}" for d in docs]

    dlss_line = "   - Page 2: With DLSS / FSR 3 (recommended) or Without.\n" if with_dlss else ""
    dlss_steps = ("   Overlay with DLSS / FSR 3: install brodrigz's DLSS / FSR3 compatibility patch first\n"
                  "   (its Better Beef's NVGs option only if you have that mod) and put this mod ABOVE it.\n"
                  "   The full pack already has the DLSS shaders; the patch is optional there.\n"
                  "   In game: Settings > Video > Basic: MSAA Off, then DLSS or FSR 3, Apply.\n") if with_dlss else ""
    blood_line = (f"   - Page {3 if with_dlss else 2}: With HUD blood (experiment) or Without.\n") if blood else ""
    readme = README.format(version=a.version, website=WEBSITE, dlss_line=dlss_line, blood_line=blood_line,
                           dlss_steps=dlss_steps,
                           reports="\n".join(reports) or "  (none)")
    credits = CREDITS.format(website=WEBSITE)
    if a.dry_run:
        print("[dry-run] would write", a.out)
        return 0

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    crlf = lambda text: text.replace("\r\n", "\n").replace("\n", "\r\n")
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        for vid, (files, _) in packs.items():
            for rel, data in files.items():
                if rel.lower().startswith("gamedata/"):
                    z.writestr(f"components/{vid}/{rel}", data)
        for rel, data in (blood or {}).items():
            if rel.lower().startswith("gamedata/"):
                z.writestr(f"components/{BLOOD_ID}/{rel}", data)
        z.writestr("docs/CREDITS.txt", crlf(credits))
        z.writestr("docs/README.txt", crlf(readme))
        for vid, (files, _) in packs.items():
            for rel, data in files.items():
                if not rel.lower().startswith("gamedata/"):
                    z.writestr(f"docs/{vid}_{rel}", data)
        for rel, data in (blood or {}).items():
            if not rel.lower().startswith("gamedata/"):
                z.writestr(f"docs/{BLOOD_ID}_{rel}", data)
        z.writestr("fomod/info.xml", crlf(info_xml(a.version, bool(blood))))
        z.writestr("fomod/ModuleConfig.xml", crlf(module_config(list(sources), with_dlss, bool(blood), a.version)))
        z.writestr("README.txt", crlf(readme))
        for vid in packs:
            z.writestr(f"selections/{vid}.txt", crlf(selection_text(vid, a.version)))
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
