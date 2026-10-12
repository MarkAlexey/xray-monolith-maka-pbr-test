#!/usr/bin/env python3
"""
make_pbr_compat_patch.py - build the PBR / IX-Ray SSLR compatibility patch for
Screen Space Shaders 23.5 and Enhanced Shaders 1.10, from YOUR copies of those mods.

Why a generator: SSS and ES belong to Ascii1457 and KennShade. This repository does not
redistribute their files; it ships the edits, and this script applies them to the archives
you downloaded. Every edit is anchored on exact text and the script refuses to write
anything if an anchor is missing (a different SSS/ES version), instead of producing a
half-patched file.

What the patch does
  Enhanced Shaders (pbr_brdf.h, lmodel.h, hmodel.h):
    - Lit_BRDF / Amb_BRDF use the IX-Ray BRDF from ixray_brdf.h (GGX + Smith + Schlick with
      IX-Ray's F90, Fresnel-blended diffuse/specular, split-sum ambient), the same math real
      PBR textures use in pbr_ixray.h. ES's pseudo-PBR material conversion (calc_albedo,
      calc_specular, calc_rough, pbr_settings.h) is untouched, so tuning mods still apply.
    - Point/spot attenuation uses IX-Ray's falloff (same as PBR textures).
    - Reflection directions go through ixr_env_dir() (the "unfuck" cubemap remap).
    - The IX-Ray SSLR result replaces the cubemap reflection where its rays hit.
    - HDR reflections: cubemap highlights above a knee are expanded (hdr_ibl.h), so bright sky
      openings reflect brighter than "white", like an HDR environment map.
  Screen Space Shaders:
    - screenspace_common.h: SSFX_calc_sky / SSFX_calc_env (water, SSR sky fallback) use the
      unfuck remap instead of SSS's perspective/altitude hack and unremapped env lookup.
    - ssfx_ssr_combine.ps: with SSLR on and ES installed, SSS SSR is suppressed on non-HUD
      pixels (IX-Ray SSLR owns them); weapons/HUD keep SSS SSR.
    - deffer_base_hud_*bump.ps (MV main file, HUD Raindrops): first-person models with PBR
      textures are written to the G-buffer as PBR (pbr_hud.h). SSS compiles every weapon and
      hand with these shaders, which never wrote the PBR material, so PBR weapons rendered as
      legacy (white plastic). Needs the SSS 23.5.1 PBR compatibility patch's gbuffer_stage.h.

Install order (Mod Organizer priority / JSGME order), after everything else:
  ES 1.10 -> SSS 23.5 modules (incl. "00 - PATCH - [ Enhanced Shaders 1.10 ]") -> this patch.
Each output folder is named after the module it overrides.

Requires the modded exe from the pbr-test-s3ds-mt-sss branch: the patched files include
ixray_brdf.h, sslr_settings.h and sslr_apply.h, which ship with the exe's gamedata.

Usage
  python make_pbr_compat_patch.py --es Enhanced_Shaders_Color_Grading_1_10.zip \
      --sss ScreenSpaceShaders_Update23_5.zip --out pbr_compat_patch [--dry-run] [--zip]
  --es / --sss also accept already-extracted folders.
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

# Defaults written into the patched pbr_brdf.h settings block (users can edit them there).
SETTINGS = {
    # 1 = ES uses the IX-Ray BRDF; 0 = original ES BRDF (patch then only adds SSLR + remap)
    "ES_IXRAY_BRDF": 1,
    # 1 = keep SSS update 19 "smooth shading" (NdotL squared). 0 = IX-Ray NdotL, matches PBR.
    "ES_IXRAY_SMOOTH_NDOTL": 0,
    # 1 = IX-Ray point/spot falloff (matches PBR textures); 0 = ES's linear falloff
    "ES_IXRAY_ATTENUATION": 1,
    # ES's calc_rough() returns gloss-derived roughness SQUARED. The unified PBR path
    # (pbr_from_legacy) feeds it to IX-Ray as perceptual roughness, so 0 here matches it.
    # 1 = take the square root first (rougher, closer to ES's own GGX look).
    "ES_IXRAY_ROUGH_IS_ALPHA": 0,
}

# Paths inside the archives (relative to the archive root)
ES_PBR = "01 Enhanced Shaders - PBR/gamedata/shaders/r3"
SSS_MAIN = "00 - MAIN FILE/gamedata/shaders/r3"
SSS_ESPATCH = "00 - PATCH - [ Enhanced Shaders 1.10 ]/gamedata/shaders/r3"
SSS_SSR = "03 - SSR/gamedata/shaders/r3"
SSS_GLOSS = "94 - IMPROVED GLOSS & WET SURFACES - [ ES Required ]/gamedata/shaders/r3"
SSS_MV = "00 - MAIN FILE (MV)/gamedata/shaders/r3"
SSS_HUDRAIN = "05 - Rain - HUD Raindrops/gamedata/shaders/r3"

# Output folder names (prefix sorts them after the modules they override)
OUT = {
    "es": "99 - PBR compat - ES 1.10 (01 Enhanced Shaders - PBR)",
    "sss_main": "99 - PBR compat - SSS (00 - MAIN FILE)",
    "sss_es": "99 - PBR compat - SSS (00 - PATCH - Enhanced Shaders 1.10)",
    "sss_ssr": "99 - PBR compat - SSS (03 - SSR)",
    "sss_gloss": "99 - PBR compat - SSS (94 - IMPROVED GLOSS & WET SURFACES)",
    # HUD Raindrops overrides the MV main file's HUD shaders; its folder sorts after the MV one
    "sss_mv": "99 - PBR compat - SSS (00 - MAIN FILE (MV))",
    "sss_hudrain": "99 - PBR compat - SSS (05 - Rain - HUD Raindrops)",
}

# ------------------------------------------------------------------------------------------
# Source access (zip or folder)
# ------------------------------------------------------------------------------------------


class Source:
    def __init__(self, path):
        self.path = path
        self.zip = zipfile.ZipFile(path) if os.path.isfile(path) else None
        if self.zip is None and not os.path.isdir(path):
            sys.exit(f"error: {path} is neither a zip nor a folder")

    def read(self, rel):
        if self.zip:
            names = {n.replace("\\", "/"): n for n in self.zip.namelist()}
            if rel not in names:
                raise KeyError(rel)
            return self.zip.read(names[rel])
        full = os.path.join(self.path, rel)
        with open(full, "rb") as f:
            return f.read()


# ------------------------------------------------------------------------------------------
# Text patching
# ------------------------------------------------------------------------------------------


class PatchError(Exception):
    pass


def decode(data):
    text = data.decode("utf-8", errors="surrogateescape")
    crlf = "\r\n" in text
    return text.replace("\r\n", "\n"), crlf


def encode(text, crlf):
    if crlf:
        text = text.replace("\n", "\r\n")
    return text.encode("utf-8", errors="surrogateescape")


def replace_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise PatchError(f"{what}: anchor found {n} times (expected 1):\n    {old.splitlines()[0]!r}")
    return text.replace(old, new)


def regex_once(text, pattern, new, what, flags=re.S):
    found = re.findall(pattern, text, flags)
    if len(found) != 1:
        raise PatchError(f"{what}: pattern matched {len(found)} times (expected 1): {pattern}")
    return re.sub(pattern, new, text, count=1, flags=flags)


BANNER = "// [PBR compat] "


def settings_block():
    lines = [
        "",
        "//=================================================================================================",
        "// [PBR compat] IX-Ray BRDF for Enhanced Shaders (generated by tools/make_pbr_compat_patch.py)",
        "// Pseudo-PBR legacy textures are shaded with the same IX-Ray BRDF as real PBR textures.",
        "//",
    ]
    doc = {
        "ES_IXRAY_BRDF": "1 = IX-Ray BRDF, 0 = original Enhanced Shaders BRDF",
        "ES_IXRAY_SMOOTH_NDOTL": "1 = NdotL^2 (SSS update 19 smooth shading), 0 = NdotL like PBR textures",
        "ES_IXRAY_ATTENUATION": "1 = IX-Ray point/spot falloff like PBR textures, 0 = ES falloff",
        "ES_IXRAY_ROUGH_IS_ALPHA": "1 = sqrt() calc_rough() before IX-Ray (rougher), 0 = as unified PBR lighting",
    }
    for k, v in SETTINGS.items():
        lines += [f"// {doc[k]}", f"#ifndef {k}", f"#\tdefine {k} {int(v)}", "#endif"]
    lines += ['#include "ixray_brdf.h"', ""]
    return "\n".join(lines) + "\n"


NEW_BRDF = r"""
//=================================================================================================
// [PBR compat] IX-Ray BRDF entry points. The original Enhanced Shaders versions are kept above as
// Lit_BRDF_ES / Amb_BRDF_ES and used when ES_IXRAY_BRDF is 0.
//
float es_ixray_rough( float rough )
{
#if ES_IXRAY_ROUGH_IS_ALPHA
	return max( 0.02, sqrt( saturate( rough ) ) );
#else
	return max( 0.02, rough );
#endif
}

// V, N, L: unit vectors, V to the eye, L to the light (Enhanced Shaders convention).
// Returns BRDF * NdotL, linear, like the original (light colour applied by the caller).
float3 Lit_BRDF( float rough, float3 albedo, float3 f0, float3 V, float3 N, float3 L )
{
#if ES_IXRAY_BRDF
	float  NdotL	= saturate( dot( N, L ) );
	float3 brdf		= ixr_brdf_vec( albedo, f0, es_ixray_rough( rough ), N, L, V );
#	if ES_IXRAY_SMOOTH_NDOTL
	return brdf * NdotL * NdotL;
#	else
	return brdf * NdotL;
#	endif
#else
	return Lit_BRDF_ES( rough, albedo, f0, V, N, L );
#endif
}

// env_d: diffuse irradiance, env_s: specular radiance (both linear, from hmodel.h)
float3 Amb_BRDF( float rough, float3 albedo, float3 f0, float3 env_d, float3 env_s, float3 V, float3 N )
{
#if ES_IXRAY_BRDF
	float NdotV = saturate( dot( N, V ) );
	return ixr_ambient( albedo, f0, es_ixray_rough( rough ), NdotV, env_d, env_s );
#else
	return Amb_BRDF_ES( rough, albedo, f0, env_d, env_s, V, N );
#endif
}
"""


def patch_pbr_brdf(text, what):
    text = replace_once(
        text,
        '#include "pbr_settings.h" //load settings files\n',
        '#include "pbr_settings.h" //load settings files\n' + settings_block(),
        what,
    )
    text = replace_once(text, "float3 Lit_BRDF(float rough,", "float3 Lit_BRDF_ES(float rough,", what)
    text = replace_once(text, "float3 Amb_BRDF(float rough,", "float3 Amb_BRDF_ES(float rough,", what)
    # Append after the last function; keep a trailing include guard intact if present
    stripped = text.rstrip()
    if stripped.endswith("#endif"):
        cut = stripped.rfind("#endif")
        return stripped[:cut] + NEW_BRDF + "\n" + stripped[cut:] + "\n"
    return stripped + "\n" + NEW_BRDF


def patch_lmodel(text, what):
    return replace_once(
        text,
        "\tatt = SRGBToLinear(att);\n",
        "#if ES_IXRAY_ATTENUATION\n"
        "\t" + BANNER + "IX-Ray GetLightAttention() falloff 2, same as PBR textures (pbr_attenuation)\n"
        "\t{\n"
        "\t\tfloat factor = rsqr * light_range_rsq;\n"
        "\t\tatt = max(1.0 - factor, 0.0);\n"
        "\t\tatt = (att * att) / (1.0 + 2.0 * factor);\n"
        "\t}\n"
        "#else\n"
        "\tatt = SRGBToLinear(att);\n"
        "#endif\n",
        what,
    )


def patch_hmodel(text, what):
    text = replace_once(
        text,
        '#include "pbr_cubemap_check.h"\n',
        '#include "pbr_cubemap_check.h"\n'
        '#include "ixray_brdf.h"\t' + BANNER + "unfuck() cubemap remap\n"
        '#include "sslr_apply.h"\t' + BANNER + "IX-Ray SSLR (no-op unless r4_sslr_reflections 1)\n"
        '#include "hdr_ibl.h"\t' + BANNER + "HDR highlights from the 8-bit cubemaps\n",
        what,
    )
    text = replace_once(
        text,
        "\tPnt = normalize(Pnt);\n",
        "\tfloat3 Pview = Pnt;\t" + BANNER + "view position for SSLR\n\tPnt = normalize(Pnt);\n",
        what,
    )
    text = regex_once(
        text,
        r"\tfloat3 vreflectRemap = vreflect;\n.*?//fake remapping[^\n]*\n",
        "\t" + BANNER + "unfuck() remap (ixray_brdf.h); same as ES's fake remap plus a zero guard\n"
        "\tfloat3 vreflectRemap = ixr_env_dir(vreflect);\n",
        what,
    )
    text = replace_once(
        text,
        "\tfloat3\te1s\t\t= env_s1.SampleLevel( smp_base, vreflectRemap, RoughMip );\n",
        "\tfloat3\te1s\t\t= env_s1.SampleLevel( smp_base, vreflectRemap, RoughMip );\n"
        "\te0s = hdr_ibl_expand_gamma(e0s);\t" + BANNER + "HDR reflections (hdr_ibl.h)\n"
        "\te1s = hdr_ibl_expand_gamma(e1s);\n",
        what,
    )
    text = replace_once(
        text,
        "\tenv_s = SRGBToLinear(env_s);",
        "\tenv_s = sslr_blend(env_s, Pview);\t" + BANNER + "IX-Ray SSLR over the cubemap reflection\n"
        "\tenv_s = SRGBToLinear(env_s);",
        what,
    )
    return text


def patch_screenspace_common(text, what):
    text = replace_once(
        text,
        '#include "check_screenspace.h"\n',
        '#include "check_screenspace.h"\n'
        '#include "ixray_brdf.h"\t' + BANNER + "unfuck() cubemap remap\n"
        "// " + BANNER.strip() + " 1 = sky/env lookups use the X-Ray cubemap remap (no stretching), 0 = SSS original\n"
        "#ifndef SSFX_IXR_CUBE_REMAP\n#\tdefine SSFX_IXR_CUBE_REMAP 1\n#endif\n",
        what,
    )
    text = regex_once(
        text,
        r"(\tdir = SSFX_yaw_vector\(dir, -sky_color\.w\); // Sky rotation\n)(.*?)(\tfloat3 sky0 = )",
        lambda m: m.group(1)
        + "#if SSFX_IXR_CUBE_REMAP\n"
        + "\tdir = ixr_cube_unfuck(dir, IXR_SKY_CLAMP_HORIZON != 0);\t" + BANNER + "unstretch the skybox cubemap\n"
        + "#else\n"
        + m.group(2)
        + "#endif\n"
        + m.group(3),
        what,
    )
    text = replace_once(
        text,
        "float3 SSFX_calc_env(float3 dir)\n{\n",
        "float3 SSFX_calc_env(float3 dir)\n{\n"
        "#if SSFX_IXR_CUBE_REMAP\n"
        "\tdir = ixr_env_dir(dir);\t" + BANNER + "unstretch the hemisphere cubemap\n"
        "#endif\n",
        what,
    )
    return text


def patch_ssr_combine(text, what):
    text = replace_once(
        text,
        '#include "screenspace_common.h"\n',
        '#include "screenspace_common.h"\n#include "sslr_settings.h"\t' + BANNER + "IX-Ray SSLR interop\n",
        what,
    )
    return replace_once(
        text,
        "\t// Scene + SSR\n",
        "#if defined(USE_SSLR_REFLECTIONS) && defined(SSFX_ENHANCED_SHADERS) && SSLR_REPLACE_SSFX_SSR\n"
        "\t" + BANNER + "IX-Ray SSLR (applied in hmodel.h) owns every non-HUD pixel; keep SSS SSR on the HUD only.\n"
        "\t// Without this, both reflections stack and surfaces look twice as reflective.\n"
        "\tif (s_position.Sample(smp_nofilter, I.tc0).z >= SSLR_HUD_DEPTH)\n"
        "\t\treturn float4(color, 1.0f);\n"
        "#endif\n\n"
        "\t// Scene + SSR\n",
        what,
    )


def patch_hud(text, what):
    """SSS first-person (HUD) G-buffer shaders: write the IX-Ray PBR material (pbr_hud.h)."""
    text = replace_once(
        text,
        '#include "sload.h"\n',
        '#include "sload.h"\n#include "pbr_hud.h"\t' + BANNER + "PBR materials on weapons and hands\n",
        what,
    )
    # Fix up the decoded surface before its normal is transformed (rain drops are added after)
    text = regex_once(
        text,
        r"(\n)([ \t]*float3[ \t]+Ne[ \t]*=[ \t]*mul)",
        lambda m: m.group(1)
        + "#ifdef USE_PBR\n"
        + "\tfloat4 pbr_hud = pbr_hud_decode(S, I.tcdh.xy);\t" + BANNER + "IX-Ray material, see pbr_hud.h\n"
        + "#endif\n"
        + m.group(2),
        what,
        flags=0,
    )
    text = regex_once(
        text,
        r"(\n)([ \t]*return[ \t]+O[ \t]*;)",
        lambda m: m.group(1)
        + "#if defined(USE_PBR) && defined(PBR_GBUFFER)\n"
        + "\tO.surface = pbr_hud_surface(pbr_hud);\t" + BANNER + "mark the pixel PBR\n"
        + "#endif\n"
        + m.group(2),
        what,
        flags=0,
    )
    return text


# (source, path in archive, output key, patch function)
JOBS = [
    ("es", ES_PBR + "/pbr_brdf.h", "es", patch_pbr_brdf),
    ("es", ES_PBR + "/lmodel.h", "es", patch_lmodel),
    ("es", ES_PBR + "/hmodel.h", "es", patch_hmodel),
    ("sss", SSS_ESPATCH + "/pbr_brdf.h", "sss_es", patch_pbr_brdf),
    ("sss", SSS_ESPATCH + "/lmodel.h", "sss_es", patch_lmodel),
    ("sss", SSS_GLOSS + "/hmodel.h", "sss_gloss", patch_hmodel),
    ("sss", SSS_MAIN + "/screenspace_common.h", "sss_main", patch_screenspace_common),
    ("sss", SSS_SSR + "/ssfx_ssr_combine.ps", "sss_ssr", patch_ssr_combine),
    # -hq, _d, _d-hq and _db-hq variants #include these two
    ("sss", SSS_MV + "/deffer_base_hud_bump.ps", "sss_mv", patch_hud),
    ("sss", SSS_MV + "/deffer_base_hud_aref_bump.ps", "sss_mv", patch_hud),
    ("sss", SSS_HUDRAIN + "/deffer_base_hud_bump.ps", "sss_hudrain", patch_hud),
    ("sss", SSS_HUDRAIN + "/deffer_base_hud_aref_bump.ps", "sss_hudrain", patch_hud),
]

README = """PBR / IX-Ray SSLR compatibility patch for SSS 23.5 + Enhanced Shaders 1.10
Generated by tools/make_pbr_compat_patch.py (xray-monolith-maka-pbr-test, pbr-test-s3ds-mt-sss).

Install each folder AFTER the module named in brackets (highest priority last).
Delete the shader cache afterwards.

Folders and what they change:
{listing}
Settings: top of pbr_brdf.h (ES_IXRAY_*), gamedata/shaders/r3/sslr_settings.h and
ixray_brdf.h (shipped with the exe). Console: r4_sslr_reflections, r4_sslr_intensity,
r4_sslr_temporal, r4_sslr_max_dist, r4_sslr_skip_ssfx_ssr.
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--es", required=True, help="Enhanced Shaders 1.10 zip or extracted folder")
    ap.add_argument("--sss", required=True, help="Screen Space Shaders 23.5 zip or extracted folder")
    ap.add_argument("--out", default="pbr_compat_patch", help="output folder")
    ap.add_argument("--zip", action="store_true", help="also write <out>.zip")
    ap.add_argument("--dry-run", action="store_true", help="check every anchor, write nothing")
    args = ap.parse_args()

    sources = {"es": Source(args.es), "sss": Source(args.sss)}
    results, errors = [], []

    for src, rel, key, fn in JOBS:
        what = f"{src}:{rel}"
        try:
            text, crlf = decode(sources[src].read(rel))
            patched = fn(text, what)
            out_rel = os.path.join(OUT[key], "gamedata", "shaders", "r3", os.path.basename(rel))
            results.append((out_rel, encode(patched, crlf), what))
        except KeyError:
            errors.append(f"{what}: file not found in the archive (different SSS/ES version?)")
        except PatchError as e:
            errors.append(str(e))

    if errors:
        print("Refusing to write a partial patch:", file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1

    listing = ""
    for out_rel, data, what in results:
        listing += f"  {out_rel}\n      from {what}\n"
    print(("[dry-run] would write:\n" if args.dry_run else "Writing:\n") + listing)
    if args.dry_run:
        return 0

    files = results + [("README.txt", README.format(listing=listing).encode(), "")]
    for out_rel, data, _ in files:
        full = os.path.join(args.out, out_rel)
        os.makedirs(os.path.dirname(full) or ".", exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)
    if args.zip:
        with zipfile.ZipFile(args.out.rstrip("/\\") + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
            for out_rel, data, _ in files:
                z.writestr(out_rel.replace(os.sep, "/"), data)
    print(f"Done: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
