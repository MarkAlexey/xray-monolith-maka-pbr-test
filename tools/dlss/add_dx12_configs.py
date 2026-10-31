#!/usr/bin/env python3
"""
add_dx12_configs.py - add the "DX12" build (AnomalyDX12.exe, brodrigz's DX12 presentation bridge
with frame generation) to engine-vs2022.sln and the two projects that change, by cloning this
tree's own DX11 configurations.

Why a generator: brodrigz's commit bdefda0d adds hand-written ReleaseR4-DX12 / Release-DX12
blocks copied from his ReleaseR4 / Release. Our projects carry other settings (profiler, MT,
include paths), so his blocks would drop them. This clones ours instead. Run on his base
(bdefda0d^) it reproduces his .sln and .vcxproj configuration changes byte for byte (checked).

What it does (idempotent: a file that already has the new configuration is left alone)
  engine-vs2022.sln
    - solution configuration "DX12|x64" next to "DX11|x64";
    - every project mapped for DX11 gets the same mapping for DX12, except xrEngine and
      xrRender_R4, which map to <their DX11 configuration>-DX12 (ReleaseR4-DX12, Release-DX12).
  xrEngine.vcxproj      ReleaseR4 -> ReleaseR4-DX12, TargetName AnomalyDX11 -> AnomalyDX12
  xrRender_R4.vcxproj   Release   -> Release-DX12
    - every <ProjectConfiguration>, <PropertyGroup>, <ImportGroup>, <ItemDefinitionGroup> and
      <ItemGroup> conditioned on the source configuration is cloned right after itself;
    - one-line per-item conditions (e.g. stdafx.cpp's PrecompiledHeader Create) are cloned too;
    - XR_DX12_BRIDGE is prepended to the ClCompile PreprocessorDefinitions of the clone
      (not ResourceCompile).
  The bridge sources themselves (D3D12PresentBridge, StreamlineIntegration, FfxIntegration,
  Dlss/FrameGeneration) are separate items, excluded from every configuration but Release-DX12;
  this script does not add them.

Usage
  python tools/dlss/add_dx12_configs.py [--src src] [--dry-run]

Caveats
  - Block cloning matches the opening tag's indentation to find its closing tag; a project file
    reformatted with different indentation for open/close tags is not supported (it throws).
  - Encoding and line endings (BOM, CRLF) are kept as found.
  - Only x64 configurations exist in these projects; other platforms are ignored.
"""

import argparse
import os
import re
import sys

# ------------------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------------------
SLN = "engine-vs2022.sln"
SOURCE_SOLUTION_CONFIG = "DX11"
NEW_SOLUTION_CONFIG = "DX12"
DEFINE = "XR_DX12_BRIDGE"
# project file -> (GUID, source configuration, new configuration, (old TargetName, new) or None)
PROJECTS = {
    "xrEngine/xrEngine.vcxproj": ("{2578C6D8-660D-48AE-9322-7422F8664F06}", "ReleaseR4", "ReleaseR4-DX12",
                                  ("AnomalyDX11", "AnomalyDX12")),
    "Layers/xrRenderPC_R4/xrRender_R4.vcxproj": ("{AC9B12ED-A2D7-4337-A981-5BD8430E96D8}", "Release",
                                                 "Release-DX12", None),
}


def read(path):
    raw = open(path, "rb").read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    return raw.decode("utf-8-sig"), bom


def write(path, text, bom):
    open(path, "wb").write((b"\xef\xbb\xbf" if bom else b"") + text.encode("utf-8"))


def convert_sln(s, dx12_guids):
    out = []
    for line in s.splitlines(keepends=True):
        out.append(line)
        m = re.match(r"(\s*)" + SOURCE_SOLUTION_CONFIG + r"\|x64 = " + SOURCE_SOLUTION_CONFIG + r"\|x64(\r?\n)", line)
        if m:
            out.append(f"{m.group(1)}{NEW_SOLUTION_CONFIG}|x64 = {NEW_SOLUTION_CONFIG}|x64{m.group(2)}")
            continue
        m = re.match(r"(\s*)(\{[0-9A-F-]+\})\." + SOURCE_SOLUTION_CONFIG + r"\|x64\.(\w+\.?\d*) = ([^|]+)\|x64(\r?\n)", line)
        if m:
            ind, guid, key, cfg, nl = m.groups()
            if guid in dx12_guids:
                cfg = dx12_guids[guid]
            out.append(f"{ind}{guid}.{NEW_SOLUTION_CONFIG}|x64.{key} = {cfg}|x64{nl}")
    return "".join(out)


def clone_config(s, src, new, rename=None):
    cond = f"'$(Configuration)|$(Platform)'=='{src}|x64'"
    ncond = f"'$(Configuration)|$(Platform)'=='{new}|x64'"
    lines = s.splitlines(keepends=True)
    out, i = [], 0
    opening = re.compile(r"(\s*)<(ProjectConfiguration) Include=\"" + re.escape(src) + r"\|x64\">")
    grouped = re.compile(r"(\s*)<(PropertyGroup|ImportGroup|ItemDefinitionGroup|ItemGroup) Condition=\"" +
                         re.escape(cond) + "\"")
    while i < len(lines):
        line = lines[i]
        m = opening.match(line) or grouped.match(line)
        if m and not line.rstrip().endswith("/>"):
            indent, tag = m.groups()
            j = i
            while not lines[j].startswith(f"{indent}</{tag}>"):
                j += 1
                if j >= len(lines):
                    raise ValueError(f"no closing </{tag}> for line {i + 1}")
            block = lines[i:j + 1]
            out += block
            clone = [b.replace(cond, ncond).replace(f'Include="{src}|x64"', f'Include="{new}|x64"')
                      .replace(f"<Configuration>{src}</Configuration>", f"<Configuration>{new}</Configuration>")
                     for b in block]
            if tag == "ItemDefinitionGroup":
                in_cl = False
                for k, b in enumerate(clone):
                    in_cl = in_cl or "<ClCompile>" in b
                    if in_cl and "<PreprocessorDefinitions>" in b:
                        clone[k] = b.replace("<PreprocessorDefinitions>", f"<PreprocessorDefinitions>{DEFINE};", 1)
                    if "</ClCompile>" in b:
                        in_cl = False
                if not any(DEFINE in b for b in clone):
                    raise ValueError(f"no ClCompile PreprocessorDefinitions in the {src} ItemDefinitionGroup")
            if rename and tag == "PropertyGroup":
                clone = [b.replace(f"<TargetName>{rename[0]}</TargetName>", f"<TargetName>{rename[1]}</TargetName>")
                         for b in clone]
            out += clone
            i = j + 1
            continue
        out.append(line)
        # one-line per-item condition: <X Condition="...src...">value</X> or <X Condition="..." />
        if cond in line and re.match(r"\s*<\w+ Condition=", line) and \
                (line.rstrip().endswith("/>") or re.search(r"</\w+>\s*$", line)):
            out.append(line.replace(cond, ncond))
        i += 1
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="src", help="the src/ folder (default: src)")
    ap.add_argument("--dry-run", action="store_true", help="report what would change, write nothing")
    a = ap.parse_args()

    dx12_guids = {}
    for rel, (guid, src, new, rename) in PROJECTS.items():
        path = os.path.join(a.src, rel)
        text, bom = read(path)
        dx12_guids[guid] = new
        if f"'{new}|x64'" in text:
            print(f"{rel}: already has {new}")
            continue
        result = clone_config(text, src, new, rename)
        added = result.count(f"{new}|x64")
        print(f"{rel}: {src} -> {new} ({added} references{', TargetName ' + rename[1] if rename else ''})")
        if not a.dry_run:
            write(path, result, bom)

    path = os.path.join(a.src, SLN)
    text, bom = read(path)
    if f"{NEW_SOLUTION_CONFIG}|x64 = {NEW_SOLUTION_CONFIG}|x64" in text:
        print(f"{SLN}: already has {NEW_SOLUTION_CONFIG}")
    else:
        # the sln maps xrEngine / xrRender_R4 to their DX11 configuration; the clone is <that>-DX12
        result = convert_sln(text, dx12_guids)
        print(f"{SLN}: {result.count(NEW_SOLUTION_CONFIG + '|x64') } {NEW_SOLUTION_CONFIG} references")
        if not a.dry_run:
            write(path, result, bom)
    if a.dry_run:
        print("dry run: nothing written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
