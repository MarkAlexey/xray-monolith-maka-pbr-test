# STALKER Anomaly – Modded Exes with IX-Ray PBR (PBR-Test)

Automatic build of [`pbr-test-s3ds-mt-sss`](https://github.com/{{REPO}}/tree/{{SHA}}) at commit
`{{SHORT_SHA}}`, built {{DATE}} (UTC).

Demonized's Modded Exes (multithreaded build) with physically based materials ported from IX-Ray,
IX-Ray screen-space reflections (SSLR), single-pass cascaded sun shadows (CSM) and HDR reflections,
plus engine support for Screen Space Shaders. DX11 only.

> **Test build.** Made by GitHub Actions straight from the branch. Back up your `bin` and
> `db/mods` folders before installing.

## Downloads

| File | What it is |
|---|---|
| `*_latest.zip` / `*_<version>.zip` | the exes (`bin/`) and the PBR gamedata (`db/mods/00_modded_exes_gamedata.db0`) |
| `*_pdb.zip` | debug symbols; put the `.pdb` files next to the exes for readable crash logs |
| `*_tools.zip` | Python tools: SSS/ES compatibility patch generator, shader pack builder, PBR texture packer |

## Install

1. Extract the main zip into your Anomaly folder (it contains `bin/` and `db/`), replacing files.
   This replaces the regular Modded Exes: the PBR build contains everything they do, minus
   Heatvision and HDR10 output.
2. Start `AnomalyDX11.exe` (or `AnomalyDX11AVX.exe` on CPUs with AVX). PBR runs on DX11 only.
3. Delete `appdata/shaders_cache/r4` whenever you change shaders or update this build.

### With Screen Space Shaders 23.5 and/or Enhanced Shaders 1.10

SSS and ES replace shaders PBR needs, so they need patches that are generated from your own
copies of those mods (their files cannot be redistributed here):

1. From the tools zip:
   `python tools/make_pbr_compat_patch.py --es <ES zip or folder> --sss <SSS zip or folder> --out pbr_compat_patch --zip`
   It covers everything PBR needs under SSS/ES: G-buffer, lighting, ambient/reflections, weapons.
2. Load order (lowest priority first): this build's gamedata (it is an archive, so it is always
   below loose mods) → Beef's NVGs (main folder only) → ES → SSS (+ its Beef's NVG patch) →
   Atmospherics → the generated `99 - PBR compat` folders (always last).

**With Atmospherics**, install Enhanced Shaders the way Atmospherics asks: `01 Enhanced Shaders - PBR`,
`Optional - Fixed Bloom` and `Optional - shader_param controls - simple` only. Do **not** add
`02 Enhanced Color Grading`: it grades the image a second time from the same `r__color_grading`
setting and everything looks washed out.

`build_shader_pack.py` can merge all of that into one folder instead
(`--beef` / `--atmospherics` optional).

**Modpacks with their own shader stack (GAMMA):** use `make_pbr_overlay.py` instead. It patches
only the files PBR needs, from the versions your mod list actually uses, and leaves everything
else alone. Install the result last. Every tool has `--dry-run` and documents itself (`--help`).

## Making PBR textures

`<name>.dds` (albedo), `<name>_bump.dds` (R height, G normal Y, A normal X) and `<name>_bump#.dds`
(R metal, G roughness, B subsurface, A AO), all linear. Mark the base texture PBR either in its THM
(material "PBR") or, without a THM, in `<name>.ltx`:

    [texture]
    bump_mode = use
    material = pbr

`pack_pbr_textures.py` builds all three textures from separate maps, including Call of Duty
exports (gloss is inverted to roughness, packed normals are unpacked).

## Console

`r4_sslr_reflections`, `r4_sslr_intensity`, `r4_sslr_temporal`, `r4_sslr_max_dist`,
`r4_sslr_skip_ssfx_ssr`, `r4_sun_csm`, `r4_sun_csm_blend`.

Details, caveats and the full change list: [README-Claude.md](https://github.com/{{REPO}}/blob/{{SHA}}/README-Claude.md).
