# STALKER Anomaly – Modded Exes with IX-Ray PBR (PBR-Test)

Automatic build of [`pbr-test-s3ds-mt-sss-lass`](https://github.com/{{REPO}}/tree/{{SHA}}) at commit
`{{SHORT_SHA}}`, built {{DATE}} (UTC).

This branch adds Zira3l137's [xray-monolith-lass](https://github.com/Zira3l137/xray-monolith-lass)
work on top of the PBR build: the first-person body fix, jiggle bones (with the skeleton loader fixes
they need) and alpha-tested surfaces that honour their authored ref. It also carries TheLostInPlace's
grass rendering rework and qkff99's CPU-side render optimisations and multithreaded shadow
recording (from their `mt-consolidated-fixes` branch; no DLSS / FSR here). LASS's picture-in-picture scopes
are not included.

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
| `*_tools.zip` | Python tools: SSS/ES compatibility patch generator, PBR overlay generator, shader pack builder, PBR texture packer |
| `PBR_overlay_GAMMA.zip` | GAMMA: only the shaders PBR needs, patched from GAMMA's own stack. Install last, on top of GAMMA |
| `PBR_shaders_GAMMA.zip` | GAMMA: GAMMA's whole shader stack with PBR merged in, replacing GAMMA's shader mods |

## Install

1. Extract the main zip into your Anomaly folder (it contains `bin/` and `db/`), replacing files.
   This replaces the regular Modded Exes: the PBR build contains everything they do, minus
   Heatvision and HDR10 output.
2. Start `AnomalyDX11.exe` (or `AnomalyDX11AVX.exe` on CPUs with AVX). PBR runs on DX11 only.
3. Delete `appdata/shaders_cache/r4` whenever you change shaders or update this build.

### With GAMMA

Use **one** of the two GAMMA zips (when this release has them), not both:

- **`PBR_overlay_GAMMA.zip` (recommended):** add it as a mod in Mod Organizer and put it at the
  very bottom of the left pane (highest priority). It only replaces the files PBR needs, made from
  GAMMA's own versions, so every other GAMMA shader stays as it is. In the conflict tab every file
  of it must be "winning".
- **`PBR_shaders_GAMMA.zip`:** GAMMA's complete shader stack (Enhanced Shaders, Screen Space
  Shaders, Atmospherics, Beef's NVGs and GAMMA's shader overrides) with PBR merged in, as one
  mod above all of them.

Both are built for GAMMA's shader stack at the time of this release; if GAMMA updates its shaders,
generate your own overlay with `make_pbr_overlay.py` from the tools zip. Delete
`appdata/shaders_cache/r4` afterwards.

Credits: Screen Space Shaders by Ascii1457, Enhanced Shaders and Color Grading by KennShade,
Atmospherics, Beef's NVGs and the GAMMA shader overrides by their authors, included with
permission.

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
`r4_sslr_skip_ssfx_ssr`, `r4_sun_csm`, `r4_sun_csm_blend`, `r4_vslr_enable` (IX-Ray offscreen
reflections, off by default, needs a restart), `r4_vslr_size`, `r4_vslr_faces`, `r4_vslr_far`,
`r4_vslr_intensity`, `r4_vslr_localize` (parallax-corrected VSLR reflections, by L4U6H1N6-LUN4T1C).

From LASS (Zira3l137): `g_jiggle_enabled`, `g_jiggle_stiffness`, `g_jiggle_damping`,
`g_jiggle_max_offset`, `g_jiggle_translate_gain`, `g_jiggle_rotate_gain`, `g_jiggle_bone_length`,
`g_jiggle_world_gain`, `g_jiggle_axis` (jiggle bones; the outfit's `jiggle_*` lines win on re-equip),
`r__alpha_ref_live` and `r__alpha_dither` (delete `shaders_cache/r4` and restart after changing them).
`g_legs_spine_offset_y` now defaults to 0.

Grass (TheLostInPlace's rework): `r__detail_shadow_cull`, `r__detail_rows`, `r__detail_inst`,
`r__detail_inst_res`, `r__detail_merge`, `r__detail_occ`, `r__detail_occ_first` (all on),
`r__detail_thin` with `r__detail_thin_start` / `_keep` / `_band` (off). The instanced grass path
needs grass shaders with the `DT_INST` paths: the GAMMA zips have them; with your own SSS/ES,
run the overlay tool or the compat generator (both add them).
`r__static_screen_quad` (on): repeated screen quads from static vertex buffers.

Multithreaded shadows (qkff99, all on): `r4_mt_sun_shadows`, `r4_mt_local_shadows`, `r4_mt_rain_shadows`,
`r4_smapvis` (occlusion culling of local-light shadow casters). If shadows break or the game
crashes, set the four to 0 in `user.ltx` and report which one fixed it.
qkff99's other optimisations have no switches. `sheduler_budget_max` (5): ceiling of the per-frame
object-update budget in ms (IX-Ray value); raise it (up to 66, the old value) if NPCs react late in
busy scenes.

Details, caveats and the full change list: [README-Claude.md](https://github.com/{{REPO}}/blob/{{SHA}}/README-Claude.md).
