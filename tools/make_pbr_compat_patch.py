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
    - gbuffer_stage.h: SSS's G-buffer layout (motion vectors in target 3, TAA unjitter) with
      the PBR material target in target 2 and Heatvision moved to target 4. Without it the PBR material
      is never written under SSS (PBR_GBUFFER undefined), and the exe's own gbuffer_stage.h
      must NOT be used over SSS: it lacks ssfx_jitter / velocity and breaks SSS's shaders.
    - screenspace_common.h: SSFX_calc_sky / SSFX_calc_env (water, SSR sky fallback) use the
      unfuck remap instead of SSS's perspective/altitude hack and unremapped env lookup.
    - ssfx_ssr_combine.ps: with SSLR on and ES installed, SSS SSR is suppressed on non-HUD
      pixels (IX-Ray SSLR owns them); weapons/HUD keep SSS SSR.
    - deffer_base_hud_*bump.ps (MV main file, HUD Raindrops): first-person models with PBR
      textures are written to the G-buffer as PBR (pbr_hud.h). SSS compiles every weapon and
      hand with these shaders, which never wrote the PBR material, so PBR weapons rendered as
      legacy (white plastic). Needs the patched gbuffer_stage.h above.
  World PBR under SSS / ES (these files replace the exe's PBR versions, so without the edits
  PBR textures wrote their material and nothing read it: lit as legacy, metals white):
    - deffer_base_bump.ps, deffer_base_aref_bump.ps (ES 01 PBR, SSS MV main file): write the
      PBR material like the HUD shaders (pbr_hud.h). Under SSS's sload.h: no POM / detail
      bump on PBR textures.
    - accum_sun_near/far.ps, accum_sun.ps, accum_base.ps, accum_omni_unshadowed.ps,
      accum_indirect.ps (ES, SSS main file, 02 SSS, 08 Shadow & Light, 91 Flora): PBR pixels
      take the IX-Ray lighting (pbr_ixray.h), with SSS's screen-space/contact shadows and
      lightmaps folded in; legacy pixels keep SSS's / ES's lighting.
    - combine_1.ps (ES, SSS main file, SSS ES patch): PBR pixels get the IX-Ray ambient (IBL,
      SSLR, HDR reflections) with SSS AO, the HUD hemi boost and SSS Indirect Light on the
      ambient diffuse (PBR_IL_INTENSITY); PBR_DEBUG_VIEW works (7 = IL factor).
      Not applied to PBR pixels: the r2 static-sun term.
  Instanced grass (exes with TheLostInPlace's grass rework, r__detail_inst and later):
    - deffer_grass.vs (ES 01 PBR, SSS 00 MV / 09 / 10 / 91) and deffer_detail_s_flat.vs (SSS
      00 MV): DT_INST / DT_MERGE / DT_OCC / DT_THIN paths, the edits TheLostInPlace made to
      SSS's copies (the generator reproduces their files). The exe's deffer_grass_inst*.vs
      wrappers include these; without them the engine keeps the stock grass path.

Install order (Mod Organizer priority / JSGME order), after everything else:
  ES 1.10 -> SSS 23.5 modules (incl. "00 - PATCH - [ Enhanced Shaders 1.10 ]") -> this patch.
With Beef's NVGs and Atmospherics (neither needs PBR patching, but the order matters):
  exe gamedata -> Beef's NVGs (main folder only) -> ES -> SSS (+ "00 - PATCH - [ Beefs NVGs ]")
  -> Atmospherics -> this patch.
  Beef's night_vision.h must win over the exe's older copy, or SSS's NVG code and
  Atmospherics' sky2.ps fail with "undeclared identifier 'shader_param_8'". Beef's own
  combine_1/combine_2_naa/pp_blur must lose to SSS's, which already contain Beef's NVG code;
  don't install Beef's "Patch (...)" folders on top of SSS.
Each output folder is named after the module it overrides.

Requires the modded exe from the pbr-test-s3ds-mt-sss branch: the patched files include
ixray_brdf.h, sslr_settings.h and sslr_apply.h, which ship with the exe's gamedata.

Usage
  python make_pbr_compat_patch.py --es Enhanced_Shaders_Color_Grading_1_10.zip \
      --sss ScreenSpaceShaders_Update23_5.zip --out pbr_compat_patch [--dry-run] [--zip]
  --es / --sss also accept already-extracted folders.
"""

import argparse
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
SSS_SSS = "02 - SSS/gamedata/shaders/r3"
SSS_SHADOWS = "08 - New Shadow & Light Features/gamedata/shaders/r3"
SSS_FLORA = "91 - FLORA FIXES & IMPROVEMENTS/gamedata/shaders/r3"
SSS_INTGRASS = "09 - Interactive Grass/gamedata/shaders/r3"
SSS_WIND = "10 - Wind/gamedata/shaders/r3"

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
    # accum_base.ps ships in 02, 08 and 91 (91 wins in SSS's own order, and so do these folders)
    "sss_sss": "99 - PBR compat - SSS (02 - SSS)",
    "sss_shadows": "99 - PBR compat - SSS (08 - New Shadow & Light Features)",
    "sss_flora": "99 - PBR compat - SSS (91 - FLORA FIXES & IMPROVEMENTS)",
    "sss_intgrass": "99 - PBR compat - SSS (09 - Interactive Grass)",
    "sss_wind": "99 - PBR compat - SSS (10 - Wind)",
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


# PBR lighting for SSS's / ES's own light shaders. These replace the exe's PBR versions
# (gamedata/shaders/r3/accum_*.ps, which only exist for the vanilla shader set), so without this
# a PBR pixel wrote its material and nothing read it: it was lit as a legacy surface.
#
# Same code as the exe's versions (pbr_ixray.h), with two differences:
#  - pbr_load(), not pbr_load_unified(): only textures marked PBR take the IX-Ray path; legacy
#    pixels keep SSS's / ES's lighting (which the patches above already move to the IX-Ray BRDF).
#  - Inserted right before the shader's final return, after SSS has folded its screen-space
#    shadows, contact shadows, lightmaps and shadow fading into the shadow factor, so PBR pixels
#    get all of them.
# PBR pixels write linear radiance and no separate specular (alpha 0); combine_1 adds the ambient.

LIGHT_KINDS = {
    "sun": "pbr_light_sun( pbr_m, gbd.P, _N.xyz, Ldynamic_dir.xyz, Ldynamic_color.rgb ) * pbr_to_linear( {shadow} )",
    "local": "pbr_light_local( pbr_m, gbd.P, _N.xyz, Ldynamic_pos, Ldynamic_color.rgb ) * {shadow} * pbr_to_linear( lightmap.rgb )",
    "omni": "pbr_light_local( pbr_m, gbd.P, _N.xyz, Ldynamic_pos, Ldynamic_color.rgb )",
    "indirect": "pbr_to_linear( Ldynamic_color.xyz ) * att * light * hemi * pbr_m.diffuse",
}

# Where the final return construct starts, most specific first (SSS, then ES)
LIGHT_RETURN_ANCHORS = [
    # comment lines may sit between the #ifdef and the return (Atmospherics 2026 accum_sun_far.ps)
    r"\n(?=#ifdef SSFX_ENHANCED_SHADERS[^\n]*\n(?:[ \t]*//[^\n]*\n)*[ \t]*return)",
    r"\n(?=[ \t]*float3 result[ \t]*=)",
    r"\n(?=[ \t]*return[ \t]+float4\([ \t]*SRGBToLinear\(Ldynamic_color)",
]


def add_pbr_include(text, what):
    if '#include "pbr_ixray.h"' in text:
        return text
    for anchor in ('#include "hmodel.h"\n', '#include "lmodel.h"\n', '#include "common.h"\n'):
        if text.count(anchor) == 1:
            return text.replace(anchor, anchor + '#include "pbr_ixray.h"\t' + BANNER + "IX-Ray PBR lighting\n")
    raise PatchError(f"{what}: no include anchor for pbr_ixray.h")


def make_light_patch(kind):
    def patch(text, what):
        text = add_pbr_include(text, what)
        for anchor in LIGHT_RETURN_ANCHORS:
            hits = [m.start() for m in re.finditer(anchor, text)]
            if hits:
                pos = hits[-1]
                break
        else:
            raise PatchError(f"{what}: final return not found")
        loads = list(re.finditer(r"gbuffer_load_data\(\s*(GLD_P\([^;]*?\))\s*\)", text[:pos]))
        if not loads:
            raise PatchError(f"{what}: gbuffer_load_data( GLD_P(...) ) not found")
        gld = loads[-1].group(1)
        # SSS's far cascade folds clouds and shadows into "shadows"; everything else uses "s"
        shadow = "shadows" if (kind == "sun" and re.search(r"\bshadows[ \t]*\*=[ \t]*s\b", text[:pos])) else "s"
        expr = LIGHT_KINDS[kind].format(shadow=shadow)
        block = (
            "\n#ifdef PBR_GBUFFER\n"
            "\t" + BANNER + "IX-Ray PBR pixel: linear radiance, no separate specular (pbr_ixray.h)\n"
            f"\tpbr_material pbr_m = pbr_load( {gld}, gbd.C );\n"
            "\t[branch] if( pbr_m.enabled )\n"
            f"\t\treturn float4( {expr}, 0 );\n"
            "#endif\n"
        )
        return text[:pos] + block + text[pos:]
    return patch


def il_factor(text):
    """HLSL computing float3 pbr_il, the SSS Indirect Light factor for the PBR ambient diffuse
    (see patch_combine). 1 when the file has no SSS IL code (ES alone, vanilla)."""
    blend = "\t\tpbr_il = lerp( (float3)1.0, pbr_il, PBR_IL_INTENSITY );\n"
    es = "lerp( (float3)1.0f, IL_sample * ( 30.0f * ssfx_il_setup.y ), IL_sample )"
    gamma = "pbr_to_linear( lerp( (float3)1.0f, IL_sample * ( 7.5f * ssfx_il_setup.y ), IL_sample ) )"
    head = "\t\tfloat3 pbr_il = 1.0;\n#ifdef SSFX_INDIRECT_LIGHT\n"
    if "IL_sample" not in text or "SSFX_INDIRECT_LIGHT" not in text:
        return "\t\tfloat3 pbr_il = 1.0;\t// no SSS Indirect Light in this combine_1\n"
    if "7.5f * ssfx_il_setup.y" in text and "30.0f * ssfx_il_setup.y" in text:
        # SSS main file: both paths, picked by SSFX_ENHANCED_SHADERS like SSS does
        return (head + "#\tifdef SSFX_ENHANCED_SHADERS\n\t\tpbr_il = " + es + ";\n#\telse\n"
                "\t\tpbr_il = " + gamma + ";\n#\tendif\n" + blend + "#endif\n")
    if "30.0f * ssfx_il_setup.y" in text:
        return head + "\t\tpbr_il = " + es + ";\n" + blend + "#endif\n"
    raise PatchError("combine_1: SSS Indirect Light code found but not its hdiffuse factor (different SSS version?)")


def patch_combine(text, what):
    """combine_1: IX-Ray ambient (IBL, SSLR, HDR reflections) for PBR pixels.

    Replaces the colour SSS / ES computed for PBR pixels, right before fog, with
    pbr_to_gamma( accumulated linear light + pbr_ambient ). Uses SSS AO when SSFX_AO is on (else
    the engine SSAO), times the material AO, and SSS's HUD hemi boost where the file has it.

    SSS Indirect Light (IL): SSS multiplies the legacy ambient diffuse by a factor from its IL
    buffer (bounce light from nearby lit surfaces). The same factor scales the PBR ambient
    diffuse (pbr_ambient_ex's diffuse_scale), so PBR surfaces pick up the same bounce colour.
    Reflections are not scaled, as in SSS. SSS applies the factor in two spaces:
      - with Enhanced Shaders (ES patch's combine_1, or SSFX_ENHANCED_SHADERS in the main file)
        to linear light, factor 30: used as is;
      - without ES to gamma-space light, factor 7.5: converted with pbr_to_linear(), which is
        what scaling a gamma value by f does to the linear one.
    PBR_IL_INTENSITY (pbr_ixray.h, default 1) blends it: 0 = off, >1 = stronger.
    Not applied to PBR pixels: the static-sun (r2 static lighting) term.
    """
    text = add_pbr_include(text, what)
    hemi = "lerp( N.w, hud_hemi, HudMask )" if ("hud_hemi" in text and "HudMask" in text) else "N.w"
    ao = ("\n#ifdef SSFX_AO\n\t\tfloat pbr_occ = ssfx_ao.Sample( smp_linear, I.tc0 ).w;\n#else\n"
          "\t\tfloat pbr_occ = occ.x;\n#endif\n") if "ssfx_ao" in text else "\t\tfloat pbr_occ = occ.x;\n"
    il = il_factor(text)
    block = (
        "#ifdef PBR_GBUFFER\n"
        "#ifndef PBR_IL_INTENSITY\n#\tdefine PBR_IL_INTENSITY 1.0\t// older pbr_ixray.h without the option\n#endif\n"
        "\t" + BANNER + "IX-Ray PBR pixel: ambient (IBL) + the accumulated linear light, back to gamma\n"
        "\tpbr_material pbr_m = pbr_load( GLD_P(I.tc0, I.pos2d, ISAMPLE), D.rgb );\n"
        "\t[branch] if( pbr_m.enabled )\n"
        "\t{\n"
        + ao + il +
        f"\t\tfloat3 pbr_amb = pbr_ambient_ex( pbr_m, P.xyz, N.xyz, {hemi}, pbr_occ * pbr_m.ao, pbr_il );\n"
        "\t\tcolor = pbr_to_gamma( L.rgb + pbr_amb );\n"
        "#ifdef PBR_DEBUG_VIEW\n"
        "\t\t// pbr_ixray.h: 1 normal, 2 AO, 3 roughness, 4 metalness, 5 ambient only, 6 direct only,\n"
        "\t\t// 7 SSS indirect light factor (grey 0.5 = no change, brighter = more bounce light)\n"
        "\t\tfloat4 pbr_dbg = gbuffer_load_surface( GLD_P(I.tc0, I.pos2d, ISAMPLE) );\n"
        "#\tif PBR_DEBUG_VIEW == 1\n\t\tcolor = mul( m_inv_V, normalize( N.xyz ) ) * 0.5 + 0.5;\n"
        "#\telif PBR_DEBUG_VIEW == 2\n\t\tcolor = pbr_occ * pbr_m.ao;\n"
        "#\telif PBR_DEBUG_VIEW == 3\n\t\tcolor = pbr_dbg.y;\n"
        "#\telif PBR_DEBUG_VIEW == 4\n\t\tcolor = pbr_dbg.x;\n"
        "#\telif PBR_DEBUG_VIEW == 5\n\t\tcolor = pbr_to_gamma( pbr_amb );\n"
        "#\telif PBR_DEBUG_VIEW == 6\n\t\tcolor = pbr_to_gamma( L.rgb );\n"
        "#\telif PBR_DEBUG_VIEW == 7\n\t\tcolor = saturate( pbr_il * 0.5 );\n"
        "#\tendif\n"
        "#endif\n"
        "\t}\n"
        "#endif\n\n"
    )
    return regex_once(text, r"\n([ \t]*// here should be distance fog)", lambda m: "\n" + block + m.group(1), what, flags=0)


def patch_hud(text, what):
    """SSS first-person (HUD) G-buffer shaders: write the IX-Ray PBR material (pbr_hud.h)."""
    text = replace_once(
        text,
        '#include "sload.h"\n',
        '#include "sload.h"\n#include "pbr_hud.h"\t' + BANNER + "PBR materials on weapons and hands\n",
        what,
    )
    # Fix up the decoded surface before its normal is first COPIED. Old SSS shaders copy it in
    # "float3 Ne = mul(..., S.normal)"; SSS/Atmospherics HUD shaders since HUD_NORMAL_STRENGTH
    # copy it earlier, into "float3 hud_nmap = float3(S.normal.xy * ...)", and only use that
    # copy afterwards. Anchoring on "float3 Ne" there left PBR weapons and hands with the
    # legacy-decoded normal (IX-Ray keeps the normal in G/A, legacy reads A/B/G): sideways
    # normals, almost no sun, reflections from the wrong direction. So the decode goes in
    # front of the first declaration that reads S.normal, whatever its name. In-place edits
    # before it (SSS's "S.normal.xy *= 0.7" on world surfaces) are overwritten by the decode,
    # as before. The match is the first one after the last sload(), so it is outside the
    # MSAA alpha-test branches that declare S.
    text = text.replace("\r\n", "\n") if "\r\n" in text else text
    decl = re.compile(r"\n([ \t]*float[234]?[ \t]+\w+[ \t]*=[^;\n]*\bS\.normal\b)")
    last_sload = max(text.rfind("sload"), 0)
    m = decl.search(text, last_sload)
    if not m:
        raise PatchError(f"{what}: no declaration reading S.normal after sload()")
    insert = ("\n#ifdef USE_PBR\n"
              "\tfloat4 pbr_hud = pbr_hud_decode(S, I.tcdh.xy);\t" + BANNER + "IX-Ray material, see pbr_hud.h\n"
              "#endif")
    text = text[:m.start()] + insert + text[m.start():]
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


GBUFFER_PBR_DECL = """
// Monolith: IX-Ray PBR. On DX11 (SM_5) the G-buffer has a third target, s_surface:
//   x = metalness, y = roughness, z = ambient occlusion,
//   w = 0 for legacy pixels, 0.5 + 0.5 * subsurface for PBR pixels.
// Every shader that writes f_deffer through pack_gbuffer() writes zero there, so only
// textures marked "PBR (metallic/roughness)" in their THM switch to the PBR lighting model.
// It takes render target 2; SSS motion vectors stay in target 3 and Heatvision moves from
// target 2 to target 4 (the PBR exes bind it there).
#if defined(SM_5) && !defined(PBR_DISABLE)
#	define PBR_GBUFFER
#endif

#ifdef PBR_GBUFFER
#	ifdef USE_MSAA
		Texture2DMS<float4> s_surface;
#	else
		Texture2D s_surface;
#	endif
#endif
"""

GBUFFER_LOAD_SURFACE = """
#ifdef PBR_GBUFFER
//Unpacking stage for the PBR material target
float4 gbuffer_load_surface( float2 tc : TEXCOORD, float2 pos2d, uint iSample )
{
#ifndef USE_MSAA
	return s_surface.Sample( smp_nofilter, tc );
#else
	return s_surface.Load( int3( pos2d, 0 ), iSample );
#endif
}
#endif
"""


def patch_gbuffer_stage(text, what):
    """SSS's gbuffer_stage.h (motion vectors, TAA unjitter, Heatvision) + the PBR material target.

    The exe's own gamedata/shaders/r3/gbuffer_stage.h is written for vanilla shaders: no
    screenspace_mvectors.h, no velocity target, no TAA unjitter. SSS replaces that file with
    its own, so with SSS installed the PBR edits have to be applied to SSS's copy. Using the
    exe's copy over SSS breaks every SSS shader that needs ssfx_jitter or O.velocity
    (first one in the log: ssfx_taa.ps, "undeclared identifier 'ssfx_jitter'").

    Targets with the PBR G-buffer (DX11 / R4): 0 position, 1 albedo, 2 PBR material,
    3 SSS motion vectors, 4 Heatvision. Upstream (and R3, which shares the r3 shader folder and
    has no PBR) keeps Heatvision in target 2, so H moves to target 4 only under PBR_GBUFFER.
    """
    text = replace_once(text, '#include "screenspace_mvectors.h"\n',
                        '#include "screenspace_mvectors.h"\n' + GBUFFER_PBR_DECL, what)
    text = regex_once(text, r"[ \t]*float4[ \t]+H[ \t]*:[ \t]*SV_Target2;[^\n]*\n",
                      "#ifdef PBR_GBUFFER\n"
                      "\tfloat4 surface : SV_Target2; //X - Metalness, Y - Roughness, Z - AO, W - PBR flag/SSS (0 = legacy)\n"
                      "\tfloat4 H : SV_Target4; // HeatVision (target 2 holds the PBR material on the PBR exes)\n"
                      "#else\n"
                      "\tfloat4 H : SV_Target2; // HeatVision\n"
                      "#endif\n", what, flags=0)
    # Heatvision output stays exactly as SSS writes it (L_hotness); only the PBR target is added
    text = regex_once(text, r"([ \t]*// HeatVision\n[ \t]*if \(L_hotness\.x > 0\.0\)\n)",
                      lambda m: "#ifdef PBR_GBUFFER\n\tres.surface = float4( 0.0, 0.0, 0.0, 0.0 ); "
                                "// legacy lighting; PBR shaders overwrite it\n#endif\n\n" + m.group(1),
                      what, flags=0)
    text = regex_once(text, r"(\n)(gbuffer_data gbuffer_load_data\( float2 tc : TEXCOORD, float2 pos2d \)\n)",
                      lambda m: m.group(1) + GBUFFER_LOAD_SURFACE.lstrip("\n") + "\n" + m.group(2), what, flags=0)
    return text


# ------------------------------------------------------------------------------------------
# Instanced grass (TheLostInPlace's grass rework, r__detail_inst and later)
# ------------------------------------------------------------------------------------------
# The engine draws a grass object instanced only when its vertex shader has "<vs>_inst.vs"
# (and _inst_merge, _inst_merge_occ, ..._thin) variants that read the instance buffers
# (dt_inst.h: dt_rows, dt_draw, dt_verts, dt_idx). The variants are two-line wrappers shipped
# with the exe; they define DT_INST / DT_MERGE / DT_OCC / DT_THIN and #include the stack's
# grass shader, so THAT shader needs the DT_* paths. This adds them, the same edits
# TheLostInPlace made to SSS 23.5's deffer_grass.vs / deffer_detail_s_flat.vs:
#   - array[61*4] (and SSS's exdata[61]) become the instance buffers under DT_INST; SSS's
#     per-instance terrain normal / alpha moves to dt_ex (t11), which the engine fills from
#     the same data;
#   - main() takes SV_VertexID / SV_InstanceID under DT_MERGE / DT_INST;
#   - the row fetch reads dt_row(n, k) instead of array[i + k];
#   - DT_THIN scales far instances down and drops the ones whose scale reaches 0.
# Without DT_* defined the shader compiles exactly as before (the stock constant-array path).
# A shader whose fetch block does not look like stock / SSS / ES (a rewritten grass shader)
# fails the anchors and is left alone: the engine then logs "[DT-INST] ... old" and keeps the
# stock path for it.

DT_DECL_INST = '#ifdef DT_INST\n#include "dt_inst.h"\n'
DT_DECL_EX = 'Buffer<uint4>\tdt_ex\t: register(t11);\t// terrain normal and grass alpha per instance\n'
DT_FETCH_INST = ("#ifdef DT_INST\n#ifdef DT_MERGE\n\tuint\tn;\n\tv_detail\tv\t= dt_vertex(vid, iid, n);\n"
                 "#ifdef\tDT_OCC\n\tn\t= dt_idx[n];\n#endif\n#else\n\tuint\tn\t= dt_inst(iid);\n#endif\n"
                 "\tfloat4  m0 \t= dt_row(n, 0);\n\tfloat4  m1 \t= dt_row(n, 1);\n\tfloat4  m2 \t= dt_row(n, 2);\n"
                 "\tfloat4  c0 \t= asfloat(dt_rows[n * 4 + 3]);\n")
DT_FETCH_EX = "\n\tfloat4 data = asfloat(dt_ex[n]);\n"
DT_THIN_SCALE = ("\n#ifdef\tDT_THIN\n\tfloat\tthin\t= dt_thin_scale(float3(m0.w, m1.w, m2.w));\n"
                 "\tm0.xyz\t*= thin;\n\tm1.xyz\t*= thin;\n\tm2.xyz\t*= thin;\n#endif\n")
DT_THIN_DROP = "#ifdef\tDT_THIN\n\tif (thin <= 0)\n\t\tO.hpos\t= float4(0, 0, -1, 1);\n#endif\n"


def patch_grass_inst(text, what):
    """deffer_grass.vs (SSS / ES) and deffer_detail_s_flat.vs (SSS): add the DT_INST paths."""
    if "DT_INST" in text:
        raise PatchError(f"{what}: already has the DT_INST paths")
    # SSS's per-instance terrain normal / alpha
    ex = re.search(r"^[ \t]*(?:uniform[ \t]+)?float4[ \t]+exdata[ \t]*\[[ \t]*61[ \t]*\][ \t]*;[^\n]*\n", text, re.M)
    if ex:
        text = text[:ex.start()] + "#ifndef DT_INST\n" + ex.group(0) + "#endif\n" + text[ex.end():]
    # the constant array -> instance buffers
    text = regex_once(text, r"^([ \t]*(?:uniform[ \t]+)?float4[ \t]+array[ \t]*\[[ \t]*61[ \t]*\*[ \t]*4[ \t]*\][ \t]*;[^\n]*\n)",
                      lambda m: DT_DECL_INST + (DT_DECL_EX if ex else "") + "#else\n" + m.group(1) + "#endif\n",
                      what, flags=re.M)
    # entry point
    text = regex_once(text, r"^(v2p_\w+)([ \t]+)main[ \t]*\([ \t]*v_detail[ \t]+v[ \t]*\)[ \t]*\n",
                      lambda m: (f"#if defined(DT_MERGE)\n{m.group(1)}{m.group(2)}main (uint vid : SV_VertexID, uint iid : SV_InstanceID)\n"
                                 f"#elif defined(DT_INST)\n{m.group(1)}{m.group(2)}main (v_detail v, uint iid : SV_InstanceID)\n"
                                 f"#else\n{m.group(0)}#endif\n"), what, flags=re.M)
    # row fetch (+ SSS's exdata fetch right after it)
    fetch = (r"((?:^[ \t]*// index[ \t]*\n)?^[ \t]*int[ \t]+i[ \t]*=[ \t]*v\.misc\.w[ \t]*;[^\n]*\n"
             r"(?:^[ \t]*float4[ \t]+m[012][ \t]*=[ \t]*array[ \t]*\[[ \t]*i[ \t]*\+[ \t]*[012][ \t]*\][ \t]*;[^\n]*\n){3}"
             r"^[ \t]*float4[ \t]+c0[ \t]*=[ \t]*array[ \t]*\[[ \t]*i[ \t]*\+[ \t]*3[ \t]*\][ \t]*;[^\n]*\n"
             r"(?:[ \t]*\n^[ \t]*float4[ \t]+data[ \t]*=[ \t]*exdata[^\n]*\n)?)")
    def fetch_sub(m):
        has_ex = "exdata" in m.group(1)
        if has_ex != bool(ex):
            raise PatchError(f"{what}: exdata is declared but not fetched after the rows (or the other way round)")
        return DT_FETCH_INST + (DT_FETCH_EX if has_ex else "") + "#else\n" + m.group(1) + "#endif\n" + DT_THIN_SCALE
    text = regex_once(text, fetch, fetch_sub, what, flags=re.M)
    # drop thinned-out instances right where the clip position is written
    text = regex_once(text, r"^([ \t]*O\.hpos[ \t]*=[^\n]*\n)", lambda m: m.group(1) + DT_THIN_DROP, what, flags=re.M)
    return text


# (source, path in archive, output key, patch function)
JOBS = [
    ("sss", SSS_MAIN + "/gbuffer_stage.h", "sss_main", patch_gbuffer_stage),
    ("es", ES_PBR + "/pbr_brdf.h", "es", patch_pbr_brdf),
    ("es", ES_PBR + "/lmodel.h", "es", patch_lmodel),
    ("es", ES_PBR + "/hmodel.h", "es", patch_hmodel),
    ("sss", SSS_ESPATCH + "/pbr_brdf.h", "sss_es", patch_pbr_brdf),
    ("sss", SSS_ESPATCH + "/lmodel.h", "sss_es", patch_lmodel),
    # 94 ships its own pbr_brdf.h / lmodel.h too and, installed after the ES patch, they win:
    # patch them as well, or the compat versions above would silently drop 94's changes
    ("sss", SSS_GLOSS + "/pbr_brdf.h", "sss_gloss", patch_pbr_brdf),
    ("sss", SSS_GLOSS + "/lmodel.h", "sss_gloss", patch_lmodel),
    ("sss", SSS_GLOSS + "/hmodel.h", "sss_gloss", patch_hmodel),
    ("sss", SSS_MAIN + "/screenspace_common.h", "sss_main", patch_screenspace_common),
    ("sss", SSS_SSR + "/ssfx_ssr_combine.ps", "sss_ssr", patch_ssr_combine),
    # -hq, _d, _d-hq and _db-hq variants #include these two
    ("sss", SSS_MV + "/deffer_base_hud_bump.ps", "sss_mv", patch_hud),
    ("sss", SSS_MV + "/deffer_base_hud_aref_bump.ps", "sss_mv", patch_hud),
    ("sss", SSS_HUDRAIN + "/deffer_base_hud_bump.ps", "sss_hudrain", patch_hud),
    ("sss", SSS_HUDRAIN + "/deffer_base_hud_aref_bump.ps", "sss_hudrain", patch_hud),
    # World PBR under SSS / ES: G-buffer (same decode as the HUD shaders) and lighting
    ("es", ES_PBR + "/deffer_base_bump.ps", "es", patch_hud),
    ("sss", SSS_MV + "/deffer_base_bump.ps", "sss_mv", patch_hud),
    ("sss", SSS_MV + "/deffer_base_aref_bump.ps", "sss_mv", patch_hud),
    ("es", ES_PBR + "/accum_sun.ps", "es", make_light_patch("sun")),
    ("es", ES_PBR + "/accum_sun_near.ps", "es", make_light_patch("sun")),
    ("es", ES_PBR + "/accum_sun_far.ps", "es", make_light_patch("sun")),
    ("es", ES_PBR + "/accum_base.ps", "es", make_light_patch("local")),
    ("es", ES_PBR + "/accum_omni_unshadowed.ps", "es", make_light_patch("omni")),
    ("es", ES_PBR + "/accum_indirect.ps", "es", make_light_patch("indirect")),
    ("es", ES_PBR + "/combine_1.ps", "es", patch_combine),
    ("sss", SSS_MAIN + "/accum_sun_near.ps", "sss_main", make_light_patch("sun")),
    ("sss", SSS_MAIN + "/accum_sun_far.ps", "sss_main", make_light_patch("sun")),
    ("sss", SSS_MAIN + "/combine_1.ps", "sss_main", patch_combine),
    ("sss", SSS_ESPATCH + "/combine_1.ps", "sss_es", patch_combine),
    ("sss", SSS_SSS + "/accum_base.ps", "sss_sss", make_light_patch("local")),
    ("sss", SSS_SHADOWS + "/accum_base.ps", "sss_shadows", make_light_patch("local")),
    ("sss", SSS_FLORA + "/accum_base.ps", "sss_flora", make_light_patch("local")),
    ("sss", SSS_FLORA + "/accum_omni_unshadowed.ps", "sss_flora", make_light_patch("omni")),
    # Instanced grass (needs the exes with TheLostInPlace's grass rework, r__detail_inst):
    # deffer_grass.vs ships in 00 MV, 09, 10 and 91 (91 wins in SSS's order), s_flat in 00 MV
    ("es", ES_PBR + "/deffer_grass.vs", "es", patch_grass_inst),
    ("sss", SSS_MV + "/deffer_grass.vs", "sss_mv", patch_grass_inst),
    ("sss", SSS_MV + "/deffer_detail_s_flat.vs", "sss_mv", patch_grass_inst),
    ("sss", SSS_INTGRASS + "/deffer_grass.vs", "sss_intgrass", patch_grass_inst),
    ("sss", SSS_WIND + "/deffer_grass.vs", "sss_wind", patch_grass_inst),
    ("sss", SSS_FLORA + "/deffer_grass.vs", "sss_flora", patch_grass_inst),
]

README = """PBR / IX-Ray SSLR compatibility patch for SSS 23.5 + Enhanced Shaders 1.10
Generated by tools/make_pbr_compat_patch.py (xray-monolith-maka-pbr-test, pbr-test-s3ds-mt-sss).

Install each folder AFTER the module named in brackets (highest priority last).
Delete the shader cache afterwards.

With Beef's NVGs and Atmospherics:
  exe gamedata -> Beef's NVGs (main folder only, none of its Patch folders) -> ES ->
  SSS (+ "00 - PATCH - [ Beefs NVGs ]") -> Atmospherics -> these folders.
  Beef's night_vision.h must be above the exe's gamedata; SSS must be above Beef's NVGs.

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
