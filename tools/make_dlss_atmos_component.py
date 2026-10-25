#!/usr/bin/env python3
"""
make_dlss_atmos_component.py - add DLSS / FSR3 support for a newer Atmospherics to brodrigz's
DLSS compatibility patch (the zip in .github/pbr-release/dlss/).

The patch's "base" component holds DLSS versions of SSS 23.5 files and its "atmos_rc7"
component DLSS versions of Atmospherics 2.69 RC7 files. Installed below a newer Atmospherics (as
the patch must be, it wins every file both ship), those copies replace the newer Atmospherics'
own shaders: its rewritten AO / indirect light / SSR, which its combine_1 expects in a new format
(IL with depth stored alongside it, AO depth-validated history, its own SSR output). This tool
takes YOUR Atmospherics files, applies brodrigz's DLSS edits to them, and writes the result as new
components plus installer options, so the patch keeps Atmospherics' shaders and only adds DLSS.

brodrigz's edits (the same in every file he patched, read off his 0.5 components):
  - #include "dlss_contract.h" at the top
  - every history lookup "tc - Motion_Vectors.xy" goes through dlss_reproject() (the previous
    frame was rendered with another jitter offset); reprojection offsets used as "tc - offset"
    get "- dlss_history_offset()"
  - terrain (deffer_impl_flat.ps) writes motion vectors (O.velocity), which DLSS needs
  - Better Beef's NVGs: combine_2_naa.ps leaves the NVG lens out of the LUT

Components written (id = --id, default atmos_2026_10):
  <id>                    deffer_impl_flat.ps, ssfx_sss_ext.ps, ssfx_ao.ps  (always)
  <id>_ssr                ssfx_ssr.ps        (only with Atmospherics' own SSR, ATMOS_SSR 1)
  <id>_il                 ssfx_il.ps         (only with Atmospherics' own indirect light, ATMOS_IL 1)
  better_beefs_<id>       combine_2_naa.ps   (with Better Beef's NVGs)
A file your Atmospherics does not ship is left out (its component then has fewer files; a
component left empty is not written and its installer option is not offered).
The installer (fomod/ModuleConfig.xml) gets the new Atmospherics on page 2 and, when it is
picked, a page "2b" with Atmospherics' SSR / indirect light choices (they must match what you
picked in Atmospherics' own installer).

Usage
  python make_dlss_atmos_component.py --atmos Atmospherics.7z|folder \\
      --patch-zip dlss-binaries-GAMMA-compatibility-patch-0.5.zip --out-zip patched.zip \\
      [--id atmos_2026_10] [--label "Atmospherics (October 2026 update)"] [--dry-run]
  --dry-run checks every anchor and lists the files, writes nothing.

Caveats
  - The anchors are the exact reprojection lines of the Atmospherics this was written for. A
    later Atmospherics that changes them stops the tool with the file and line it expected
    (nothing is written); a new history lookup it does not know about is NOT found by it: check
    "Motion_Vectors.xy" in a newer ssfx_*.ps by hand.
  - --check-against (a patch component, e.g. atmos_rc7) compares the files produced for the
    files that did not change between that Atmospherics and yours; a mismatch is reported.
  - Atmospherics' "SSR / IL from SSS" choices: then its installer does not ship ssfx_ssr.ps /
    ssfx_il.ps, the base component's SSS versions apply, and the 2b options must stay off.
"""

import argparse
import io
import os
import re
import sys
import zipfile

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------

DEFAULT_ID = "atmos_2026_10"
DEFAULT_LABEL = "Atmospherics, October 2026 update (Atmospherics SSR / IL options)"
INCLUDE = '#include "dlss_contract.h"\n'

# file -> (component suffix, [(regex, replacement, expected count)], add the contract include)
RULES = {
    "deffer_impl_flat.ps": ("", [
        (r'(#include "common\.h"\n)', r'\1#include "screenspace_mvectors.h"\n', 1),
        (r'\n(  return O;\n\}\s*)$',
         "\n  // Preserve Atmospherics material/puddle shading while supplying temporal motion.\n"
         "  O.velocity = ssfx_mv_calc(I.vel_curr, I.vel_prev, 0, 0);\n\\1", 1),
    ], False),
    "ssfx_sss_ext.ps": ("", [
        (r"s_prev_pos\.Sample\( smp_linear, \(tc - Motion_Vectors\.xy\) \)",
         "s_prev_pos.Sample( smp_linear, dlss_reproject(tc - Motion_Vectors.xy) )", 1),
        (r"any\(\(tc - Motion_Vectors\.xy\) < 0\) \+ any\(\(tc - Motion_Vectors\.xy\) > 1\.0\)",
         "any(dlss_reproject(tc - Motion_Vectors.xy) < 0) + any(dlss_reproject(tc - Motion_Vectors.xy) > 1.0)", 1),
        (r"sss_image\.Sample\(smp_linear, \(I\.tc0 \+ rnd\) - Motion_Vectors\.xy\)",
         "sss_image.Sample(smp_linear, dlss_reproject((I.tc0 + rnd) - Motion_Vectors.xy))", 1),
    ], True),
    "ssfx_ao.ps": ("", [
        (r"(float2\s+ptc\s*=\s*)tc\s*-\s*Motion_Vectors\.xy\s*;", r"\1dlss_reproject(tc - Motion_Vectors.xy);", 1),
    ], True),
    "ssfx_il.ps": ("_il", [
        (r"(float2\s+ptc\s*=\s*)tc\s*-\s*Motion_Vectors\.xy\s*;", r"\1dlss_reproject(tc - Motion_Vectors.xy);", 1),
    ], True),
    "ssfx_ssr.ps": ("_ssr", [
        (r"(uv_rp\s*=\s*lerp\(\s*Motion_Vectors\.xy\s*,\s*uv_rp\s*,\s*HUD_mask\s*\))\s*;", r"\1 - dlss_history_offset();", 1),
        (r"s_prev_pos\.Sample\(\s*smp_linear\s*,\s*tc\s*-\s*Motion_Vectors\.xy\s*\)",
         "s_prev_pos.Sample(smp_linear, dlss_reproject(tc - Motion_Vectors.xy))", 1),
        # the velocity penalty compares the reflection's motion with the surface's: undo the
        # jitter offset just added to uv_rp so it is not counted as motion
        (r"length\(\s*uv_rp\s*-\s*Motion_Vectors\.xy\s*\)", "length(uv_rp + dlss_history_offset() - Motion_Vectors.xy)", 1),
    ], True),
    "combine_2_naa.ps": ("better_beefs", [
        (r"\n([ \t]*)img = ssfx_lut_pp\(img\);\n",
         "\n#ifdef SSFX_BEEFS_NVG\n"
         "\\1// Better Beef's NVGs (indiesunpraiser): preserve NVG colours inside the lens.\n"
         "\\1// Keep Atmospherics clarity, tonemapping and dithering around this condition.\n"
         "\\1if (!(lua_param_nvg_generation > 0.0f && compute_lens_mask(aspect_ratio_correction(I.Tex0), lua_param_nvg_num_tubes) == 1.0f))\n"
         "\\1    img = ssfx_lut_pp(img);\n"
         "#else\n"
         "\\1img = ssfx_lut_pp(img);\n"
         "#endif\n", 1),
    ], False),
}
# a history lookup the rules above do not cover would be missed: these must not remain
LOOKUP = re.compile(r"(\btc|\(I\.tc0 \+ rnd\)|\(tc \+ rnd\))\s*-\s*Motion_Vectors\.xy")


def leftovers(t):
    """history lookups not wrapped in dlss_reproject() (and not inside "ptc = dlss_reproject(...)")"""
    return [m.group(0) for m in LOOKUP.finditer(t)
            if not t[max(0, m.start() - 16):m.start()].rstrip("( ").endswith("dlss_reproject")]

# ------------------------------------------------------------------------------------------


def read_atmos(path):
    """{file name: text} of the shaders/r3 files of an Atmospherics folder / zip / 7z"""
    out = {}

    def take(name, data):
        n = name.replace("\\", "/")
        if "/shaders/r3/" in "/" + n.lower() and os.path.basename(n) in RULES:
            out[os.path.basename(n)] = data.decode("utf-8", "replace")

    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for f in files:
                full = os.path.join(root, f)
                with open(full, "rb") as fh:
                    take(os.path.relpath(full, path), fh.read())
    elif path.lower().endswith(".7z"):
        try:
            import py7zr
        except ImportError:
            sys.exit("error: a .7z needs py7zr (pip install py7zr), or extract it first")
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            with py7zr.SevenZipFile(path) as z:
                z.extractall(tmp)
            return read_atmos(tmp)
    else:
        with zipfile.ZipFile(path) as z:
            for n in z.namelist():
                if not n.endswith("/"):
                    take(n, z.read(n))
    return out


def port(name, text):
    suffix, edits, include = RULES[name]
    crlf = "\r\n" in text
    t = text.replace("\r\n", "\n")
    if "dlss_contract.h" in t or "dlss_reproject" in t:
        sys.exit(f"error: {name} already has DLSS edits (give Atmospherics' own file); nothing written")
    for rx, rep, n in edits:
        found = len(re.findall(rx, t))
        if found != n:
            sys.exit(f"error: {name}: expected {n} match(es) of {rx!r}, found {found}; nothing written")
        t = re.sub(rx, rep, t, count=n)
    if include:
        t = INCLUDE + t
    left = leftovers(t)
    if left:
        sys.exit(f"error: {name}: history lookups the rules do not cover: {left}; nothing written")
    return suffix, (t.replace("\n", "\r\n") if crlf else t)


def plugin_xml(name, desc, files, flag=None, kind="Optional", indent="            "):
    fl = "".join(f'\n{indent}    {f}' for f in files)
    cf = (f"\n{indent}  <conditionFlags>\n{indent}    <flag name=\"{flag}\">On</flag>\n{indent}  </conditionFlags>"
          if flag else "")
    return (f'{indent}<plugin name="{name}">\n{indent}  <description>{desc}</description>\n'
            f'{indent}  <files>{fl}\n{indent}  </files>{cf}\n'
            f'{indent}  <typeDescriptor>\n{indent}    <type name="{kind}" />\n{indent}  </typeDescriptor>\n'
            f'{indent}</plugin>\n')


def patch_fomod(xml, cid, label, comps):
    flag = f"dlss_{cid}"
    files = [f'<folder source="components\\{cid}\\gamedata" destination="gamedata" priority="100" />',
             f'<file source="selections\\{cid}.txt" destination="DLSS Compatibility - Instructions\\Selected Atmospherics.txt" priority="0" />']
    desc = (f"Choose this for {label}.\n\nOn the next page, tick the Atmospherics SSR / indirect light options you "
            "picked in Atmospherics' own installer.\n\nKeep only one Atmospherics version enabled.")
    p = plugin_xml(label, desc, files, flag)
    anchor = "          </plugins>\n        </group>\n      </optionalFileGroups>\n    </installStep>\n    <installStep name=\"3."
    if xml.count(anchor) != 1:
        sys.exit("error: fomod: the end of page 2 (before page 3) not found; nothing written")
    xml = xml.replace(anchor, p + anchor, 1)
    sub = []
    if f"{cid}_ssr" in comps:
        sub.append(plugin_xml("Atmospherics SSR (ATMOS_SSR 1)",
                              "Tick if Atmospherics' installer uses Atmospherics' own screen-space reflections.",
                              [f'<folder source="components\\{cid}_ssr\\gamedata" destination="gamedata" priority="110" />'],
                              kind="Recommended", indent="              "))
    if f"{cid}_il" in comps:
        sub.append(plugin_xml("Atmospherics indirect light (ATMOS_IL 1)",
                              "Tick if Atmospherics' installer uses Atmospherics' own indirect light.",
                              [f'<folder source="components\\{cid}_il\\gamedata" destination="gamedata" priority="110" />'],
                              kind="Recommended", indent="              "))
    if sub:
        step = ('    <installStep name="2b. Atmospherics rendering choices">\n'
                f'      <visible>\n        <flagDependency flag="{flag}" value="On" />\n      </visible>\n'
                '      <optionalFileGroups order="Explicit">\n'
                '        <group name="Match Atmospherics\' own installer" type="SelectAny">\n'
                '          <plugins order="Explicit">\n' + "".join(sub) +
                '          </plugins>\n        </group>\n      </optionalFileGroups>\n    </installStep>\n')
        a3 = '    <installStep name="3.'
        xml = xml.replace(a3, step + a3, 1)
    if f"better_beefs_{cid}" in comps:
        pat = ("      <pattern>\n        <dependencies operator=\"And\">\n"
               f"          <flagDependency flag=\"{flag}\" value=\"On\" />\n"
               "          <flagDependency flag=\"dlss_better_beefs\" value=\"On\" />\n        </dependencies>\n"
               f"        <files>\n          <folder source=\"components\\better_beefs_{cid}\\gamedata\" destination=\"gamedata\" priority=\"300\" />\n"
               "        </files>\n      </pattern>\n")
        if xml.count("    </patterns>") != 1:
            sys.exit("error: fomod: </patterns> not found; nothing written")
        xml = xml.replace("    </patterns>", pat + "    </patterns>", 1)
    return xml


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--atmos", required=True)
    ap.add_argument("--patch-zip", required=True)
    ap.add_argument("--out-zip", required=True)
    ap.add_argument("--id", default=DEFAULT_ID)
    ap.add_argument("--label", default=DEFAULT_LABEL)
    ap.add_argument("--check-against", help="patch component to compare unchanged files with (e.g. atmos_rc7)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if not re.match(r"^[a-z0-9_]+$", a.id):
        sys.exit("error: --id: lowercase letters, digits and _ only")

    src = read_atmos(a.atmos)
    if not src:
        sys.exit(f"error: {a.atmos}: none of {', '.join(RULES)} found under shaders/r3")
    zin = zipfile.ZipFile(a.patch_zip)
    names = zin.namelist()
    if any(n.startswith(f"components/{a.id}/") for n in names):
        sys.exit(f"error: the patch already has components/{a.id}; use another --id")

    comps = {}
    for name in RULES:
        if name not in src:
            print(f"  -        {name}: not in this Atmospherics, left out")
            continue
        suffix, text = port(name, src[name])
        cid = (f"better_beefs_{a.id}" if suffix == "better_beefs" else a.id + suffix)
        comps.setdefault(cid, {})[name] = text
        print(f"  ported   {name}  -> components/{cid}")
    if a.id not in comps:
        sys.exit(f"error: no file for the main component {a.id}; nothing written")

    if a.check_against:
        for name, text in comps.get(a.id, {}).items():
            ref = f"components/{a.check_against}/gamedata/shaders/r3/{name}"
            if ref in names:
                r = zin.read(ref).decode("utf-8", "replace").replace("\r\n", "\n").rstrip()
                same = r == text.replace("\r\n", "\n").rstrip()
                print(f"  check    {name} vs {a.check_against}: {'identical' if same else 'differs (expected if Atmospherics changed the file)'}")

    xml = zin.read("fomod/ModuleConfig.xml").decode("utf-8")
    xml = patch_fomod(xml, a.id, a.label, comps)
    sel = f"Selected: {a.label}.\r\nPatch: 0.5 + PBR-Test {a.id} (tools/make_dlss_atmos_component.py)\r\n"
    readme_add = (f"\r\n{a.label.upper()}\r\n" + "-" * len(a.label) + "\r\n"
                  f"Added by the PBR-Test build (tools/make_dlss_atmos_component.py): brodrigz's DLSS edits\r\n"
                  f"applied to that Atmospherics' own AO / SSS / terrain shaders and, if you use them, its own\r\n"
                  f"SSR and indirect light (page 2b of the installer: match Atmospherics' own choices).\r\n")
    if a.dry_run:
        print(f"[dry-run] would write {a.out_zip}: {', '.join(sorted(comps))}, selections/{a.id}.txt, fomod, README")
        return 0
    os.makedirs(os.path.dirname(os.path.abspath(a.out_zip)) or ".", exist_ok=True)
    with zipfile.ZipFile(a.out_zip, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            data = zin.read(info)
            if info.filename == "fomod/ModuleConfig.xml":
                data = xml.encode("utf-8")
            elif info.filename == "README.txt":
                data = data + readme_add.encode("utf-8")
            zout.writestr(info, data)
        for cid, files in comps.items():
            for name, text in files.items():
                zout.writestr(f"components/{cid}/gamedata/shaders/r3/{name}", text.encode("utf-8"))
        zout.writestr(f"selections/{a.id}.txt", sel)
    print("wrote", a.out_zip)
    return 0


if __name__ == "__main__":
    sys.exit(main())
