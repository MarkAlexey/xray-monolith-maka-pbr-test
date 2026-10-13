# README-Claude — about the `clean-renderer`, `pbr`, `pbr-mt` and `pbr-mt-sss` branches

**The changes on this branch were made by MarkAlexey together with an AI (Claude, by
Anthropic).** The code changes, the commit messages and this document were
written by the AI. Treat it the way you would treat any large patch from a new contributor:
build it, test it, and report what breaks.

## What this branch is

`clean-renderer` is `all-in-one-vs2022-wpo` (Modded Exes, upstream commit `9b491ffb`) with the
renderer *feature* additions taken out, so the renderer goes back to behaving like Anomaly
1.5.3's own renderer. It is meant as a clean base for further renderer work (the next planned
step is a physically based shading port from
[IX-Ray](https://github.com/ixray-team/ixray-1.6-stcop)).

`clean-renderer-scopes` is `clean-renderer` plus one commit that puts the 2D and 3D shader
scopes back, so weapon packs that depend on them keep working. Everything else is the same.
Where this document says something about scopes, the note in brackets says how it differs on
`clean-renderer-scopes`.

`pbr` is `clean-renderer-scopes` plus opt-in physically based materials ported from IX-Ray. It
is described in [its own section](#the-pbr-branch) at the end; everything above that section
applies to it too.

`pbr-mt` is `pbr` merged with upstream's multithreaded build (`all-in-one-vs2022-wpo-mt`), see
[its section](#the-pbr-mt-branch).

Everything else in Modded Exes is kept: scripting extensions, gameplay and AI fixes, DLTX/DXML,
the shader bus, ImGui, the profiler, multi-monitor support, the DX11 swapchain fixes, the zoom
improvements, Mark Switch, vignette, wallmarks, GIF textures, details v4, memory and race fixes,
and so on.

## What was removed

One commit per feature, each with a detailed message:

| Commit | Removed |
|---|---|
| `Remove Screen Space Shaders engine integration` | Ascii1457's SSS engine patch (SSR, AO, IL, water, bloom, TAA, motion blur and motion vectors, fog scattering, volumetric blur, contact shadows, interactive grass, SSS rain), all `ssfx_*` console variables and shader defines, and the later Monolith edits that only existed to support SSS |
| `Remove Heatvision and silencer overheat glow` | vegeta1k95's Heatvision (heat render target, per-object hotness), the silencer glow built on the same plumbing, and the cold-zombies option |
| `Remove shader scopes (2D) and 3D shader scopes` | CrookR/Edzan's 2D shader scopes (fakescope pass, SDS zoom integration, `add_scope_radii`) and Redotix's 3D Shader Scopes (scope lens shader flag, reticle pass, `s3ds_param_1..4`). [`clean-renderer-scopes`: reverted by `Restore shader scopes (2D) and 3D shader scopes`, so both scope systems are present.] |
| `Remove HDR10 output and tonemapping` | deggua's HDR10 output (10-bit swapchain, Rec.2020/PQ, HDR bloom and lens flares, HDR tonemapping) and the shader overrides that existed only for it |

Because Hippo's *Atmospherics* depends on SSS and Enhanced Shaders rather than on its own engine
code, removing SSS also removes everything Atmospherics needs from the engine.

## What this means for players and modders

This exe is **not** a drop-in replacement for the regular Modded Exes in a modded setup.

- **Mods that will not work:** Screen Space Shaders, Atmospherics, GAMMA's shader stack, Beef's
  NVG Heatvision extension, 2D/3D shader scope addons, the HDR10 shader packs. [On
  `clean-renderer-scopes`, shader scope addons work.]
- **Console commands that no longer exist:** every `ssfx_*`, `r__heatvision`, `heat_vision_*`,
  `sil_glow_*`, `scope_*` (blur, fog, radius, factor and so on), `r__fakescope`, `r__3Dfakescope`,
  `sds_enable`, `sds_zoom_enable`, `sds_speed_enable`, `s3ds_param_1..4`, every `r4_hdr10_*`.
  Old `user.ltx` files that set them will print "unknown command" lines; that is harmless.
  [On `clean-renderer-scopes`, the `scope_*`, `r__fakescope`, `r__3Dfakescope`, `sds_*` and
  `s3ds_param_*` commands all exist.]
- **Lua functions that no longer exist:** `level.rain_wetness()`, `level.rain_hemi()`,
  `add_scope_radii()`. The `scopelense` and `dx10cullmode` shader-script methods are gone too.
  [On `clean-renderer-scopes`, `add_scope_radii()` and `scopelense` exist.]
- **Changed values:** `r2_ls_squality` is limited to 1.0 again (SSS raised it to 3.0). The
  shader bus now has eight legacy lanes (`shader_param_1..8`) instead of twelve. [On
  `clean-renderer-scopes`, it keeps all twelve, including `s3ds_param_1..4`.]
- **Modded Exes menu:** the HDR10 page is gone, and the "Shader Scopes" page is now called
  "Zoom" and only has the zoom options. [On `clean-renderer-scopes`, the "Shader Scopes" page is
  unchanged from upstream.]

## How it was checked

The AI could not run MSVC or the game, so the checks were static:

- every removed identifier was searched for across the codebase, to make sure no remaining
  code still uses it;
- every C++ file the branch touches, plus every file that includes a header the branch touches
  (590 file checks across R1–R4, xrEngine and xrGame), was syntax-checked with clang against
  the mingw-w64 Windows headers. The same files were checked on both the original commit and
  this branch, and the error lists were compared. No new errors appeared on this branch;
- the project files were checked for references to deleted sources, and the changed files for
  brace and `#if` balance.

**None of that replaces a real build and a play test.** Upstream's GitHub Actions workflow
(`.github/workflows/msbuild.yml`) builds every configuration on Windows when this branch is
pushed to a fork. Please run it, then play with vanilla Anomaly 1.5.3 gamedata before trusting
it with a mod list.

## Credits

All of the removed features belong to their authors (Ascii1457, vegeta1k95, CrookR, Edzan,
LVutner, Redotix, deggua, party_50 and others). They were removed only to get a minimal
renderer to build on, not because anything was wrong with them. The upstream Modded Exes by
themrdemonized and contributors remain the place to get them.

The PBR lighting model, its texture conventions and most of the shader math on the `pbr` branch
come from the IX-Ray team's [ixray-1.6-stcop](https://github.com/ixray-team/ixray-1.6-stcop)
(LVutner and the other IX-Ray contributors). The port only adapts it to the stock renderer.

## The `pbr` branch

`pbr` ports IX-Ray's metallic/roughness lighting model
([ixray-1.6-stcop](https://github.com/ixray-team/ixray-1.6-stcop), checked against commit
`2ba06c98`) into the DX11 renderer as an **opt-in, per-texture** feature. Textures that are not
marked as PBR are lit exactly as before, so a stock Anomaly install looks the same.

### Making a PBR texture

PBR uses IX-Ray's texture conventions, so textures made for IX-Ray work as they are:

| Texture | Channels |
|---|---|
| `name.dds` | albedo (RGB), alpha test / transparency (A), same as before |
| `name_bump.dds` | **x** = height (parallax), **y** = normal Y, **w** = normal X (z is unused) |
| `name_bump#.dds` | **x** = metalness, **y** = roughness, **z** = subsurface, **w** = ambient occlusion |

- In the texture's THM, set the material to **PBR (metallic/roughness)**. That is material value
  4, the same value IX-Ray's SDK writes, so THMs from IX-Ray projects work unchanged. Everything
  else in the THM (bump name, parallax mode, detail texture) works as usual.
- Normal maps are OpenGL style (green channel points up), as in IX-Ray. For a DirectX-style map
  (what Substance's "DirectX" preset and most game pipelines export), either flip its green
  channel or add the per-texture option `USE_DX_NORMAL_MAP` described below. A map in the wrong
  convention lights bumps and bevels from the wrong side.
- A detail texture's bump is read the PBR way only when the detail texture's own THM is also
  marked PBR; otherwise it is treated as a normal Anomaly detail bump.
- Non-metals use a fixed F0 of 0.04 (IX-Ray's `F0_BASE`). IX-Ray's optional IOR texture is not
  ported.

### Per-texture options (`.ltx`)

As in IX-Ray, a texture can have an `.ltx` file next to it with the same name, for example
`textures\wpn\wpn_pm.ltx` for `wpn_pm.dds`:

```ini
[shaders_options]
USE_DX_NORMAL_MAP = 1   ; the normal map is DirectX style (green down)
```

Every line becomes a shader define for that texture's G-buffer shader (DX11 only), and is added
to the shader's name so the shader cache keeps the combinations apart. A value of `0` leaves the
line out. `USE_DX_NORMAL_MAP` is the option the shaders here understand; others are passed
through for future use. The file is read when the game starts.

### Global options and the debug view

The top of `gamedata\shaders\r3\pbr_ixray.h` has commented-out defines that apply to every PBR
texture. Uncomment one and restart the game:

- `USE_NORMAL_HEMI_DISTRIBUTION` (IX-Ray option, off there too): the normal limits the sky
  light, so detail facing down gets darker in shade. It also darkens vertical surfaces.
- `IBL_FAKE_IRRADANCE` (IX-Ray option): always blur rough reflections with the hemisphere
  cubemap. By default that only happens when the sky cubemap has no mipmaps.
- `PBR_REFLECTION_SCALE_MAX` (default 16): limit for matching the sky reflection's brightness
  to the ambient light. Lower it if reflections of dark parts of the sky look too bright.
- `PBR_DEBUG_VIEW` 1 to 6: shows, on PBR pixels only, the world-space normal, the ambient
  occlusion, roughness, metalness, the ambient light alone or the direct light alone. The normal
  view is the quickest way to check a normal map: surfaces facing up should be green, and bumps
  should tilt the colour the same way the geometry around them does.

About normal and AO detail in shade: IX-Ray does not bake normals into the albedo. In PBR mode
(this port) the AO texture only darkens ambient light, and a normal map only shows in shade
through the ambient light's direction and the reflections, which are soft under the usual
Anomaly skies. IX-Ray's own default for Call of Pripyat content is its legacy lighting mode, which
does multiply the AO into the albedo (and has no metalness); that mode is not ported.

### What changed

Engine (`src/`):

- `ETextureParams` / `TextureDescrManager`: the PBR material type and `UsePBRTextures()`, and
  the per-texture `.ltx` shader options (`GetShaderOptions()`). PBR textures get the legacy
  material slot 1 so nothing that still reads it goes out of range.
- `uber_deffer.cpp` (DX11 only): PBR textures compile their G-buffer pixel shader with `USE_PBR`
  (and `USE_PBR_DETAIL`), with the option in the shader name so the shader cache keeps both
  variants apart. Tessellated and plain passes are both covered.
- R4 render target: a third G-buffer target, `$user$surface` (RGBA8, MSAA-aware), cleared every
  frame and bound with position and albedo. The light and combine blenders bind it as
  `s_surface`, and `combine_1` also gets the `smp_linear` sampler for blurred sky reflections.
- A new shader constant, `L_sky_color` (the weather's sky colour), used to tint sky reflections.

Shaders (`gamedata/shaders/r3/`, packed into the modded exes gamedata as usual). These are the
Anomaly 1.5.3 files with the PBR path added; the legacy code in them is unchanged:

- `gbuffer_stage.h`: the third G-buffer output. Every deferred shader writes "not PBR" into it
  unless it is a PBR texture.
- `sload.h`: IX-Ray's PBR texture decoding, and parallax reading height from the right channel.
- `deffer_base_bump.ps`, `deffer_base_aref_bump.ps`: write the PBR material.
- `pbr_ixray.h` (new): the IX-Ray BRDF (GGX, Smith, Schlick), attenuation, translucency and the
  split-sum ambient/IBL code, with credit to IX-Ray.
- `accum_base.ps`, `accum_omni_unshadowed.ps`, `accum_sun.ps`, `accum_sun_near.ps`,
  `accum_sun_far.ps`, `accum_indirect.ps`, `combine_1.ps`: a PBR branch for PBR pixels.

How it renders: PBR pixels are lit in linear space. Each light adds linear radiance (albedo and
BRDF included) to the accumulator; `combine_1` adds ambient light (the weather's hemisphere
cubemap for diffuse, the sky cubemaps blurred by roughness for reflections, scaled by SSAO and
the material AO; the reflections' brightness is matched to the hemisphere light, so metals are
as bright as everything else around them) and converts the result back to the gamma space the rest of the pipeline
(fog, tonemapping, bloom, post-processing) expects. Diffuse ambient uses the same formula as the
legacy lighting, so a matte PBR surface and a legacy one under the same sky are about equally
bright.

### Limits and known differences

- **DX11 only.** DX10 and DX9 renderers ignore the PBR flag and draw these textures the legacy
  way (with the bump channels read the legacy way, so they will look wrong there).
- Shader mods that replace any of the files listed above (Screen Space Shaders, Enhanced
  Shaders, GAMMA's shader stack and similar) will either override the PBR code or be overridden
  by it. A shader pack has to be merged with these files by hand to keep both.
- Rain does not make PBR surfaces wet, and tessellation displacement still reads the legacy
  height channel.
- Reflections sample the sky cubemaps the same way the skybox is drawn (its rotation and its
  stretched upper half) and fade to the hemisphere cubemap below the horizon. IX-Ray's
  screen-space reflections were ported later (see "IX-Ray SSLR" below); its contact shadows
  were not.
- Lighting is IX-Ray's, not a match for the legacy look: light falloff, specular highlights and
  the ambient reflections differ by design.

### How it was checked

The AI could not run MSVC or the game, so again the checks were static:

- every C++ file of the DX11 renderer project plus the changed shared files for the other
  renderers (235 file checks) were syntax-checked with clang against the mingw-w64 Windows headers,
  on `clean-renderer-scopes` and on this branch; no new errors appeared. A deliberate typo in the
  new code was caught by the same check, to confirm it covers that code;
- the changed shaders and the shaders that include them (33 shader files, in DX11, DX11 MSAA,
  DX10, static sun and PBR/non-PBR variants) were compiled with Microsoft's DXC in its legacy
  HLSL mode, before and after the change. DXC is not the compiler the game uses (the game uses
  the D3D11 FXC compiler), so this catches syntax and type errors but cannot promise FXC accepts
  every line. The compiled output was also checked to confirm the PBR code is really present in
  the DX11 shaders and absent from the DX10 ones.

**No image has been rendered with this code.** Before relying on it: build the branch, play with
vanilla gamedata and check that nothing changed, then add one IX-Ray-style PBR texture and check
it under the sun, a flashlight, at night and indoors.

## The `pbr-mt` branch

Upstream ships two builds: the normal one (`all-in-one-vs2022-wpo`, which everything above is
based on) and a multithreaded one (`all-in-one-vs2022-wpo-mt`). `pbr-mt` is `pbr` with the
multithreaded branch merged in, as one squashed commit, so it is to `pbr` what upstream's MT
build is to the normal build. `pbr` itself stays single-threaded.

The merge brings in upstream's MT branch as of commit `7b79d2be` (about 1,000 changed files:
the reworked render graph, asynchronous particles, the threaded rain update, the new HUD/camera
matrix handling, and the fixes and features that only exist there).
40 files conflicted with the feature removals and the PBR work. In most of them the MT branch had
rewritten the code, so the resolution was to take the MT version and remove the same features
from it again. The decisions worth knowing:

- **Render graph** (`r__dsgraph_*`, `CDSGraphManager`): MT's version, without the SSS water
  and HUD-mask maps. The 3D-scope pass no longer redirects an SSS render target.
- **HUD and camera matrices**: MT's `mViewHud`/`mViewCam` and `CHudInitializer` are kept. The
  previous-frame matrices (only used for SSS motion vectors) and the calls that uploaded them are
  removed, as on the other branches.
- **Rain**: MT's threaded rain update is kept, with the SSS rain behaviour removed again (wind
  gusts, the SSS drop angles and distances, the `ssfx_rain_*` settings, `level.rain_hemi()`).
  MT's own `r__rain_exp` and `r__rain_k` console variables stay.
- **Grass**: MT's detail manager is kept, without SSS grass shadows from lights, grass fade-in and
  the terrain slope limit. Grass shadows from the sun are drawn in the nearest cascade when
  `r__enable_grass_shadow` is on, which is what MT did with the SSS settings at their defaults.
- **Volumetric lights**: MT's new `accum_volumetric_lv` path is kept without the SSS distance
  fade. Sun cascade sizes stay fixed at 20/40/160 m.
- **PBR**: carried over unchanged; its edits merged without conflicts.

Everything else from the MT branch (scripts in `gamedata\scripts`, xrGame, xrCore, xrPhysics,
xrParticles and so on) comes in exactly as upstream has it.

How it was checked: every identifier the feature removals took out was searched for in the
merged tree, so that MT code which still used one could be fixed. Then the C++ files of the four
renderers and the engine, plus every xrGame file that changed or includes a changed header, were
syntax-checked with clang on upstream's MT branch and on `pbr-mt`, and the error lists compared
(see the commit message for the numbers). As with everything here, it has not been built with
MSVC or run.

## The `pbr-mt-sss` branch

`pbr-mt-sss` is `pbr-mt` with the engine side of Screen Space Shaders (SSS) put back, so that
SSS and the PBR materials can be used together. It is the MT branch as upstream has it, minus
Heatvision and HDR10, plus PBR. Heatvision and HDR10 stay removed because PBR and SSS need the
render target slots and the output path they used.

### What changed compared to `pbr-mt`

- **SSS engine support is back**, as it is on upstream's MT branch: motion vectors and the
  previous-frame matrices, the SSS render targets and blur/bloom/SMAA/SSAO passes, screen-space
  shadows for the sun and local lights, the SSS rain, grass, wind and water features, and the
  `ssfx_*` console variables and their shader defines. The grass shadow settings are back in
  the graphics options menu.
- **G-buffer layout.** The deferred pass now writes four targets:

  | Slot | Target | Contents |
  |---|---|---|
  | 0 | `rt_Position` | position, normal, material |
  | 1 | `rt_Color` | albedo, gloss |
  | 2 | `rt_Surface` | PBR: metalness, roughness, AO, PBR flag/subsurface |
  | 3 | `rt_ssfx_motion_vectors` | SSS motion vectors |

  Upstream used slot 2 for Heatvision. Pixels drawn with a legacy texture write 0 to slot 2, so
  they stay on the old lighting path.
- **Sky constants.** SSS binds `sky_color` (w = sky rotation) and PBR binds `L_sky_color` and
  `L_sky_rotation`. Both are registered and they do not affect each other.

### Shaders: the SSS compatibility patch

The shaders in this repository's `gamedata` are still the vanilla-based PBR shaders, so PBR
works without SSS. SSS 23.5 and ES 1.10 replace several of the files PBR needs
(`gbuffer_stage.h`, `combine_1.ps`, `accum_sun_near.ps`, `accum_sun_far.ps`, `accum_sun.ps`,
`accum_base.ps`, `accum_omni_unshadowed.ps`, `accum_indirect.ps`, `deffer_base_bump.ps`,
`deffer_base_aref_bump.ps`, the HUD G-buffer shaders). The generated compatibility patch
(`tools/make_pbr_compat_patch.py`, see "The SSS 23.5 / ES 1.10 compatibility patch" below)
adds the PBR code to *your* SSS/ES versions of all of them, in folders named after the
modules they override. Without it the SSS/ES files win: PBR textures write no material or
nothing reads it, so they are lit as legacy (metals turn matte white).

(Earlier versions of this README pointed to a separately distributed "SSS 23.5.1 PBR
compatibility patch". It was never published; the generated patch now covers everything.)

What the merged shaders do with PBR pixels:

- SSS screen-space shadows darken PBR surfaces for the sun and local lights, as they do legacy ones.
- With SSS AO on, its AO replaces the engine AO in the PBR ambient term, as it does for
  legacy pixels. The SSS HUD hemi boost is applied too.
- SSS's `sload.h` is kept, so PBR textures are re-decoded in the G-buffer shaders
  (`pbr_hud.h`): no POM / parallax and no detail bump on PBR textures under SSS.
- The r2 static-sun term (`USE_R2_STATIC_SUN`) is not PBR.
- SSS Indirect Light (bounce light) scales the PBR ambient diffuse by the same factor SSS
  applies to legacy surfaces (converted to linear when SSS runs without ES); reflections are
  not scaled, as in SSS. `PBR_IL_INTENSITY` in `pbr_ixray.h` (0 = off, 1 = as SSS) tunes it,
  `PBR_DEBUG_VIEW 7` shows the factor (grey = no change).
- SSS SSR used to be added on top of the PBR image-based specular. With IX-Ray SSLR on (see
  below) and the generated patch installed, SSS SSR is limited to the HUD and SSLR replaces it
  everywhere else, so reflections no longer stack.
- With Enhanced Shaders, PBR pixels skip the Enhanced Shaders lighting but are combined
  correctly: both write linear light to the accumulator.

### Unified lighting with Enhanced Shaders

With Enhanced Shaders installed, PBR and its pseudo-PBR conversion work as one lighting model
instead of two:

- Legacy textures get Enhanced Shaders' pseudo-PBR material: albedo, specular colour and
  roughness guessed from the old gloss channel and material, with Enhanced Shaders' own
  functions and `pbr_settings.h`. Mods that tune that conversion (the SSS Enhanced Shaders
  patch, "Improved gloss & wet surfaces", Atmospherics) still apply.
- Every pixel, legacy or PBR, is then lit by the IX-Ray lighting: the same direct light
  model for the sun and all local lights, and the same image-based ambient and sky
  reflections in `combine_1`. A PBR object next to a legacy one no longer looks like it comes
  from a different renderer.
- The ambient light is calibrated the way Enhanced Shaders does it (its hemisphere "ambient
  cube" and roughness-blurred reflections), so weathers made for Enhanced Shaders keep their
  brightness.
- Flora keeps Enhanced Shaders' translucency (or the SSS Flora Fixes version of it), SSS AO
  and SSS indirect light still apply, and self-illumination from Enhanced Shaders' emissive
  shaders is kept.

It switches on by itself when Enhanced Shaders is present; without Enhanced Shaders nothing
changes. To go back to Enhanced Shaders' own lighting for legacy textures, uncomment
`#define PBR_UNIFIED_LIGHTING 0` in `pbr_ixray.h`. The PBR debug views now also work on
legacy pixels (view 3 shows the converted roughness), which helps when tuning
`pbr_settings.h`.

Enhanced Shaders replaces some files PBR needs (`accum_sun.ps`, `accum_indirect.ps` and,
without SSS, the other light shaders, `combine_1.ps`, `sload.h` and `deffer_base_bump.ps`), so
the compatibility patch now has an "ES" folder for them.

### How it was checked

The C++ files of the four renderers and the engine, plus the xrGame files that changed or
include a changed header, were syntax-checked with clang on upstream's MT branch and on
`pbr-mt-sss`, and the error lists were compared (see the commit message for the numbers). The
merged shaders were compiled with DXC on top of vanilla, this branch's `gamedata` and the SSS
modules, with and without the motion-vector main file and the PBR, MSAA, static sun,
parallax, detail and debug-view variants, and compared against plain SSS on upstream's
shaders. The unified lighting was compiled the same way for vanilla, SSS alone, Enhanced
Shaders alone, Enhanced Shaders with SSS (including its Enhanced Shaders modules) and that
setup with Atmospherics and Beef's NVGs, with unified lighting on and off; the only errors
left are ones the original files have too (Enhanced Shaders' `combine_1` with the static
sun). As with everything here, it has not been built with
MSVC or run.

## IX-Ray SSLR, shared BRDF and the cubemap remap

This adds IX-Ray's screen-space local reflections (SSLR) to the DX11 renderer, makes Enhanced
Shaders' pseudo-PBR use exactly the same IX-Ray BRDF as real PBR textures, and fixes the
stretched sky/hemisphere cubemap lookups. IX-Ray SSLR replaces Screen Space Shaders' SSR for
everything it covers; SSS SSR stays only on the HUD (weapons, hands).

### SSLR

Ported from IX-Ray's `blender_sslr`, `phase_sslr` and `sslr_render/filter/temporal`
(LVutner and the IX-Ray team). Three compute passes run inside `phase_combine`, before
`combine_1`:

| Pass | Shader | Output |
|---|---|---|
| trace | `sslr_render.cs` | one GGX-VNDF-sampled ray per pixel, screen-space march + bisection, colour from last frame |
| resolve | `sslr_filter.cs` | neighbours' rays reused, weighted by this pixel's GGX lobe, depth and normal |
| temporal | `sslr_temporal.cs` | reprojected history, clamped to the 3x3 neighbourhood |

`combine_1` reads the result as `s_refl` and blends it over the cubemap reflection where the
rays hit (`sslr_apply.h`). Misses keep the existing cubemap reflection, so the IBL calibration
of PBR and Enhanced Shaders is unchanged where SSLR has nothing to say.

What differs from IX-Ray, and why:

- Anomaly's G-buffer stores linear view depth (IX-Ray stores NDC depth), so all depth tests
  were rewritten.
- No velocity buffer is needed: hits and history are reprojected with `m_previous`, which the
  engine already computes. Reflections of moving objects smear slightly.
- No blue-noise texture array exists in Anomaly; interleaved gradient noise is used.
- IX-Ray's sky fallback inside the trace is dropped (the consumer has its own).
- IX-Ray's temporal pass sampled the (-1, -1) neighbour twice; fixed to (+1, -1).
- **VSLR (IX-Ray's offscreen cubemap reflections)** is ported separately, off by default:
  see "IX-Ray VSLR (offscreen reflections)" below.

Console: `r4_sslr_reflections 0/1` (needs a game restart; `vid_restart` does not re-read renderer options), `r4_sslr_intensity`, `r4_sslr_temporal`,
`r4_sslr_max_dist`, `r4_sslr_skip_ssfx_ssr`. Compile-time settings and a debug view are in
`gamedata\shaders\r3\sslr_settings.h`. SSLR is off with MSAA. It is not traced for a scope's
second viewport, and its intensity is 0 there, so the scope picture keeps cubemap reflections. Pixels closer than `SSLR_HUD_DEPTH` (1.2 m) are treated as HUD and skipped.

### One BRDF for PBR and pseudo-PBR

`ixray_brdf.h` holds the IX-Ray BRDF (GGX, Smith, Schlick with IX-Ray's F90, Fresnel-blended
diffuse/specular, split-sum ambient). `pbr_ixray.h` now calls it, and the compatibility patch
makes Enhanced Shaders' `Lit_BRDF`/`Amb_BRDF` call it too, with IX-Ray's point/spot falloff.
ES's material guessing (`calc_albedo/specular/rough`, `pbr_settings.h`) is untouched. Settings
(`ES_IXRAY_*`) are at the top of the patched `pbr_brdf.h`.

### Cubemap remap

`ixr_cube_unfuck()` in `ixray_brdf.h` is Maka's remap (divide by the largest component,
`y * 2 - 1`) with a zero guard and an optional horizon clamp. It is used by the PBR sky and
hemisphere lookups, ES's reflection lookup, and SSS's `SSFX_calc_sky`/`SSFX_calc_env` (water
and SSR sky fallback), which used an ad-hoc perspective/altitude hack and an unremapped lookup.

### The SSS 23.5 / ES 1.10 compatibility patch (generated)

One patch, generated from your SSS/ES archives, in two parts:

| Part | Files | Purpose |
|---|---|---|
| PBR textures | `gbuffer_stage.h`, `deffer_base_bump.ps`, `deffer_base_aref_bump.ps`, `deffer_base_hud_*bump.ps`, `accum_sun*.ps`, `accum_base.ps`, `accum_omni_unshadowed.ps`, `accum_indirect.ps`, `combine_1.ps` | PBR materials written to the G-buffer and lit with the IX-Ray model under SSS / ES |
| Legacy textures and reflections | `pbr_brdf.h`, `lmodel.h`, `hmodel.h`, `screenspace_common.h`, `ssfx_ssr_combine.ps` | IX-Ray BRDF for Enhanced Shaders, SSLR, cubemap remap, HDR reflections |

Install order: ES → SSS modules → the generated `99 - PBR compat` folders (they sort in the
right order among themselves; `accum_base.ps` is patched in 02, 08 and 91, the last one you
have installed wins, as in SSS itself).

`tools/make_pbr_compat_patch.py` builds it from your own SSS and ES archives (this repository
does not redistribute them). Every edit is anchored on exact text; with a different SSS/ES
version the script refuses to write a partial patch. `--dry-run` checks the anchors only.

```
python tools/make_pbr_compat_patch.py --es Enhanced_Shaders_Color_Grading_1_10.zip ^
    --sss ScreenSpaceShaders_Update23_5.zip --out pbr_compat_patch --zip
```

Install its folders last, after ES and all SSS modules, then delete the shader cache.

### How it was checked

Not built with MSVC and not run. Shaders were compiled with DXC (legacy HLSL 2016, SM 6.0
profiles; the game uses FXC with SM 5.0, so this catches syntax, type and identifier errors but
cannot promise FXC accepts every line) using `tools/check_shaders.py`, which overlays shader
folders like Mod Organizer and diffs errors against a baseline. Three stacks, on unpacked
vanilla Anomaly 1.5.3 shaders, before and after this change, each with five define sets
(plain, SSLR, MSAA, static sun + SSLR, HDAO + SSLR) plus the engine's `SSFX_*` defines:

| Stack | Shaders | Result |
|---|---|---|
| vanilla + this repo | `combine_1`, `accum_*`, `deffer_base_bump`, `sslr_*.cs` | no errors |
| + Enhanced Shaders 1.10 + compat patch | `combine_1`, `accum_*`, `deffer_base_bump` | no new errors |
| + SSS 23.5 (main, SSR, SSR water, AO, SSS, IL, shadows, flora, gloss, ES patch) + compat patch | the above plus `ssfx_ssr_combine`, `ssfx_ssr_gloss`, `ssfx_ssr`, `ssfx_water_ssr`, `water` | no new errors |

Errors that exist before and after: Enhanced Shaders' `combine_1` with the static sun (its own
bug), and SSS's `[unroll(expr)]` with a non-literal count in `ssfx_ssr` / `ssfx_water_ssr`,
which FXC accepts and DXC does not.

The compiled output was also checked for what it binds: `combine_1` binds `s_refl` only with
`USE_SSLR_REFLECTIONS`, in all three stacks (PBR path on vanilla, Enhanced Shaders `hmodel()` with
ES and with ES + SSS); `ssfx_ssr_combine` reads `s_position` for the HUD mask only with SSLR on.

The C++ was reviewed against the existing HDAO compute path it is modelled on; it was not compiled.

## IX-Ray cascaded sun shadows (CSM)

IX-Ray renders every sun cascade into one `Texture2DArray` and lights the screen in a single
fullscreen pass that picks the cascade per pixel (`R_sun.cpp`, `accum_direct_cascade()`,
`accum_sun.ps.hlsl`). Monolith renders a cascade into the shared sun map, lights that
cascade's light-space box, and repeats, three stencil-masked passes with hard seams between
them.

`r4_sun_csm 1` (default; game restart to change) ports this as a hybrid:

- All cascades except the last are rendered into `$user$smap_csm`, a texture array with the
  same format as the legacy sun map, one slice per cascade, and lit by one fullscreen pass,
  `accum_sun_csm.ps`. That pass chooses the cascade per pixel (IX-Ray `calc_cascades`) and
  cross-fades into the next cascade near the edge (`r4_sun_csm_blend`, 0 to 0.3 of a cascade).
- The last (far) cascade still goes through `rt_smap_depth` and the installed `accum_sun_far`
  (vanilla, ES or SSS). Volumetric sun shafts read the far cascade from there, and SSS/ES tune
  the far pass, so keeping it avoids rewriting both.
- Hand-off is through the stencil, the same way the legacy cascades hand off to each other: the
  single pass zeroes the light marker on pixels it lights and discards everything outside the
  array cascades, so the far pass lights exactly the remainder.
- The lighting in `accum_sun_csm.ps` reproduces the installed near pass: IX-Ray PBR for PBR
  pixels, the Enhanced Shaders output with ES, vanilla otherwise, plus SSS's normal-offset bias
  and screen-space shadows when the engine reports SSS (new define `MONOLITH_SSFX_CORE`).
  `plight_infinity()` comes from the installed `lmodel.h`, so the compatibility patch's IX-Ray
  BRDF applies here too.
- The cloud-shadow (sunmask) matrix is now computed once per frame and shared by all cascades,
  so cloud shadows line up across cascade boundaries. Its scroll speed is scaled by the cascade
  count to keep the old speed.
- `CTexture::surface_set` (DX10/11, shared by R3 and R4) now creates a `Texture2DArray` view for
  array textures; nothing used arrays before, so other textures are unaffected.

Off automatically with MSAA, `-sunfilter`, or when `accum_sun_csm.ps` is missing. Extra memory:
one shadow map per array slice (2 x 64 MB at `r2_smap_size 4096` with 32-bit depth).
`CSM_DEBUG` and `CSM_PCF_TAPS` are at the top of `accum_sun_csm.ps`.

Not ported from IX-Ray: its cascade sizes (15/40/160 m; Monolith keeps the configurable
`ssfx_shadow_cascades`), its normal-based "far shadow" fade beyond the last cascade, and its
HUD contact shadows. Under SSS without the generated compatibility patch, PBR textures are
lit as legacy by this pass too, because SSS's `gbuffer_stage.h` replaces the PBR one; that is
the same limitation the legacy sun shaders have.

Checked: `accum_sun_csm.ps`/`.vs` compile with DXC on vanilla, vanilla + ES + compat patch,
vanilla + ES + SSS (main, AO, SSS, SSR, IL, New Shadow & Light Features, flora, gloss, ES patch)
+ compat patch, and vanilla + SSS without ES, with and without `MONOLITH_SSFX_CORE`, sunmask,
`SUN_QUALITY` 2/3 and the debug view; each binds the expected resources (`s_ssfx_sss` only with
SSS). The C++ was not compiled.

## IX-Ray VSLR (offscreen reflections)

IX-Ray 1.6's "view space local reflections" (`r4_vslr_enable`): the scene around the camera is
rendered into a small cubemap with a cheap forward shader, converted to an octahedral panorama,
and the PBR reflections read it, so metals and glossy PBR surfaces reflect the actual
surroundings (walls, trees, buildings) instead of only the sky. SSLR still wins where its rays
hit something on screen; VSLR replaces the sky cubemap behind it.

Console (renderer options need a game restart):

| Command | Default | What it does |
|---|---|---|
| `r4_vslr_enable` | 0 | 1 = on (needs a restart; off with MSAA) |
| `r4_vslr_size` | 256 | cube face size in pixels, 64-1024 (restart) |
| `r4_vslr_faces` | 2 | cube faces re-rendered per frame, 1-6; a full update takes 6 / this many frames |
| `r4_vslr_far` | 0.4 | capture distance, fraction of the weather's far plane |
| `r4_vslr_intensity` | 1 | how strongly the capture replaces the sky reflection |

How it is built (`r4_rendertarget_phase_vslr.cpp`):

- Capture before the G-buffer pass: per cube face a separate scene traversal (`GMReflect`: opaque
  geometry, no HOM, no particles, no lights) drawn with shader element 5 (`SE_R2_REFLECTIONS`),
  which `uber_reflect()` builds for the flat, alpha-tested, lightmapped, terrain, tree and model
  blenders: the geometry's own vertex shader with `deffer_vslr_*.ps` (`vslr_capture.h`), writing
  linear colour (R11G11B10F) and distance (R16F). Faces are aligned with the world axes (IX-Ray
  aligns them with the camera), so the panorama does not change with the view direction.
- Lighting in the capture: sun with the baked sun visibility of lightmapped geometry (dynamic
  objects estimate it from their hemi), sky colour x hemi, the weather's ambient, and the
  level's baked lightmap colour. PBR textures keep their metalness. No shadow map, no dynamic
  lights.
- `vslr_panorama.cs` turns the cubes into the panorama (RGBA16F, premultiplied coverage that fades
  over the last 30 % of the capture distance and with fog), then the mips are generated.
  `pbr_ixray.h` (`pbr_specular_irradiance`, `USE_OFFSCREEN_REFLECTIONS`) samples it by
  reflection direction, roughness picking the mip, as IX-Ray's `CompureSpecularIrradance` does.

Limits: one probe centred on the camera (reflections of far-away surfaces are increasingly off,
no parallax correction); objects the main view draws as LOD billboards are left out; only PBR
textures read it (legacy textures keep their cubemap reflections); not recaptured for a scope's
second viewport. Cost: `r4_vslr_faces` extra traversals and draws of the opaque scene per frame
at `r4_vslr_size`. Checked: the shaders compile with DXC on vanilla, SSS/ES/Atmospherics (both
Atmospherics versions) and the GAMMA overlay; the C++ passes `check_cpp.py` (R3 and R4) with no
new errors. Not yet run in the game.

## HDR reflections

"HDR" here means reflections that keep highlight energy: the bright sky between branches
reflects much brighter than the rest, and the tonemapper and bloom shape it, instead of
everything clipping to a flat grey "white". The output pipeline is unchanged:
`combine_1`'s `tonemap()` (vanilla and Enhanced Shaders) already takes values above 1, compresses
them with a soft curve and sends the excess to bloom. What was missing was HDR input:

- **Cubemaps** (`hdr_ibl.h`). Anomaly's sky and hemisphere cubemaps are 8-bit, so their highlight
  range is reconstructed: linear luminance above `HDR_IBL_KNEE` (0.6) follows a bounded
  inverse-Reinhard curve up to `HDR_IBL_PEAK` (6x) at white; below the knee nothing changes, the
  slope is continuous at the knee, and hue is kept. Applied to PBR/unified sky reflections in
  `pbr_ixray.h` (to the blurred average too, so the existing brightness match keeps the overall
  level and only the highlights rise), and through the compatibility patch to Enhanced Shaders'
  reflection lookups in `hmodel.h`. `HDR_IBL_ENABLE 0` turns it off.
- **SSLR** (`sslr_untonemap()`, `SSLR_HDR_DECODE`). SSLR reads `combine_1`'s tonemapped 8-bit
  output; it now inverts that exact curve (vanilla's, or Enhanced Shaders' when the engine
  reports ES with the new define `MONOLITH_ES`) and divides by the exposure (`s_tonemap`), so
  reflected surfaces keep their pre-tonemap brightness. The SSLR targets are 16-bit float and no
  longer clamp colour to 1. Both inverses were checked numerically to round-trip exactly up to
  the curve's clip point.

Limits: the cubemap range is a reconstruction, not real HDR data, so clouds clipped to white in a
sky texture get bright too; expansion happens after mip filtering, so rough reflections get less
boost than a true HDR map would give; SSLR cannot recover what the tonemap clipped (above 1.7 for
vanilla, 11.2 linear for Enhanced Shaders, times the exposure). A full IX-Ray-style HDR output
pipeline (linear float scene buffer, tonemap moved to `combine_2`) is not part of this.

## Cleanup after the ports

Fixes found by reviewing the SSLR, CSM and HDR commits as a whole:

- **Compute-shader texture bindings.** `SRVSManager.ResetDeviceState()` reset the pixel, vertex,
  geometry, hull and domain shader view caches every frame but not the compute one. Together
  with raw `CSSetShaderResources(..., nullptr)` unbinds (HDAO, and SSLR as first written),
  `RCache` believed views were still bound and skipped re-binding them: SSLR's resolve and
  temporal passes, and SSLR right after HDAO, would have read null G-buffer textures. The cache
  now resets the compute stage, and both passes unbind through the new
  `CBackend::unbind_CS_textures()`.
- **HDAO** leaked a reference to every render-target view it saved with `OMGetRenderTargets`,
  once per frame. They are released now.
- **SSLR history** validity is a `CRenderTarget` member instead of a function-local `static`,
  so it resets with the render targets on `vid_restart`. A two-frame gap (main view alternating
  with a scope's second viewport) no longer clears the history every frame.
- **SSLR in a scope's second viewport** is disabled (intensity 0) instead of showing the main
  view's reflections in the wrong places.
- **Cloud shadows**: only the wind scroll is computed once per frame; the matrix is rebuilt per
  call, because it depends on the camera of the view being lit.
- `tools/check_shaders.py` takes `--define` for stack-dependent engine defines (`MONOLITH_ES`,
  `MONOLITH_SSFX_CORE`).
- New `tools/check_cpp.py`: C++ syntax check of engine code on Linux, diffed against a baseline
  commit (details below).

### C++ syntax check (`tools/check_cpp.py`)

The engine builds only with MSVC, so until now the C++ of these ports had only been reviewed by
eye. `check_cpp.py` runs `clang++ -fsyntax-only` on a case-folded mirror of the tree (Windows
include paths are case-insensitive and use backslashes), with the mingw-w64 Windows and C++
headers, the repository's DirectX SDK headers, and small stand-ins for MSVC-only library
headers in `tools/cpp_shims/` (Concurrency Runtime, `stdext` hash containers; check-only, never
built). Each changed `.cpp` is checked at the baseline commit and at the change, under R4
(`USE_DX11`) and, for shared renderer files, R3 (`USE_DX10`), and only errors that are new are
reported. clang reports plenty of errors MSVC would not (dependent-base lookups in
`xr_resource.h` and similar); they are identical before and after and cancel out. Two
environment-only patterns are handled explicitly: `L#x` wide stringizing (`PIX_EVENT`) is
rewritten to standard `L"" #x` in the mirror, and console-command registrations are listed in
`IGNORE_PATTERNS` with the reason.

```
git submodule update --init          # optick is needed by xrCore headers
python tools/check_cpp.py --base 5dffea1 --head WORKTREE --changed
python tools/check_cpp.py --base HEAD~1 --changed --dry-run
```

Result for everything on this branch since `5dffea1` (SSLR, CSM, HDR reflections and this
cleanup): 20 file/renderer checks, no new errors. The check was shown to be sensitive by
planting a misspelt member, a wrong argument count and a type mismatch in the new code; all
three were reported. It is still not a build: no linking, templates only where instantiated,
and anything MSVC-specific clang cannot parse is invisible to it.
- Documentation: the two compatibility patches are told apart, and stale statements about SSR
  were corrected.

## SSLR ghosting fixes

In-game testing showed ghosting. All of it came from SSLR's frame-to-frame state:

| Cause | What it looked like | Fix |
|---|---|---|
| History of mirror-like reflections followed the reflecting surface | trails behind everything reflected in puddles, wet floors and glass whenever the camera moved | IX-Ray's rule: smooth reflections take their history from the virtual image of the hit point (along the view ray at view distance + hit distance), rough ones from the surface, blended by roughness (`SSLR_VIRTUAL_ROUGH_LO/HI`). A simulated wet floor gave 2.6 px of error per frame for a 5 cm strafe and 13 px for 25 cm with the old rule; the new rule is exact for flat mirrors. |
| No disocclusion test | newly revealed surfaces inherited the reflection of whatever covered them | history is used only if the surface was visible at its previous position, checked against the previous G-buffer depth (`$user$sslr_pos_old`, tolerance `SSLR_REPROJ_DEPTH_TOL_*`) |
| Reflected colour read from pixels that were covered last frame | the weapon (and moving NPCs) showing up in reflections, swaying with it | the hit point must have been visible at its previous pixel (same depth test), otherwise the ray counts as a miss and the cubemap shows |
| Scope second viewport | a second, shifting picture in reflections while aiming a scope | SSLR reflects its own copy of the last main-view frame (`$user$sslr_color`) instead of `$user$generic_temp`, which a scope overwrites every other frame |
| Loose history clamp, non-premultiplied blending | slow-fading smears and dark halos where hits meet misses | variance clipping (`SSLR_TEMPORAL_GAMMA`) inside the 3x3 min/max box; premultiplied colour in the history and in `sslr_apply.h`; history weight halves at `SSLR_TEMPORAL_MOTION_PX` of surface motion |

Also: positions are now reconstructed and projected with the engine's own
`pos_decompression_params` (exactly what `gbuffer_load_data` does) instead of `m_P`, which is
whatever projection `RCache` last had set; a stale one would move reflections and their
history around.

Cost: one more RGBA16F and one RGBA8 full-resolution target (about 12 MB at 1080p) and two
copies per frame.

If ghosting remains, isolate it in game: `r4_sslr_temporal 0` (no SSLR history: noisier, but no
SSLR trails), `r4_sslr_intensity 0` (no SSLR at all). If it is still there, it comes from Screen
Space Shaders modules (TAA, motion blur, SSR on the weapon; `r4_sslr_skip_ssfx_ssr 1` drops the
latter).

Checked: shaders compile with DXC on the vanilla, ES, ES + SSS and SSS-only stacks with no new
errors, each SSLR pass binds exactly what its blender provides; C++ syntax check: no new errors.
Not run.

## PBR weapons and hands under Screen Space Shaders

With SSS installed, the engine compiles every first-person model with SSS's own G-buffer
shaders, `deffer_base_hud_*.ps` (`ResourceManager.cpp`: `HudElement` is set for HUD models
whenever SSS is present; `blender_deffer_model.cpp` then picks `base_hud`). Those shaders never
wrote the PBR material target, so a PBR weapon was stored as a legacy pixel. Its IX-Ray
textures were decoded the legacy way (the height channel read as gloss, the normal channels
swapped) and, with unified lighting, converted by Enhanced Shaders' pseudo-PBR guess: a chrome
pistol rendered as matte white plastic. World objects were not affected.

The compatibility patch now patches SSS's two real HUD shaders, `deffer_base_hud_bump.ps` and
`deffer_base_hud_aref_bump.ps`, in both modules that ship them (`00 - MAIN FILE (MV)` and
`05 - Rain - HUD Raindrops`; the `-hq`, `_d`, `_d-hq` and `_db-hq` variants include these). They
call the new `pbr_hud.h`:

- With a PBR-aware `sload.h` (this repository's, detected by `SLOAD_HEIGHT`), the surface
  sload() decoded is used as is.
- With SSS's own `sload.h`, the normal, height, gloss and material are re-read from the IX-Ray
  texture layout (no parallax offset or detail bump on this path).
- The material is written to the G-buffer only when a PBR `gbuffer_stage.h` is installed
  (`PBR_GBUFFER`); under SSS that is the generated patch's `gbuffer_stage.h`.

Update: that fixed the G-buffer side only. SSS/ES also replace the lighting shaders
(`accum_*.ps`, `combine_1.ps`) and the world G-buffer shaders, so PBR pixels were still lit as
legacy (the Deagle stayed white in GAMMA). The patch now adds the PBR path to all of them; see
"The SSS 23.5 / ES 1.10 compatibility patch".

Checked with DXC on vanilla + ES + SSS (MV main file, HUD Raindrops) + the patch, in three
setups: SSS's own G-buffer and `sload.h` (slot 2 stays exactly SSS's output with and without
`USE_PBR`), a PBR G-buffer stand-in with SSS's `sload.h` (legacy textures write 0, PBR textures
write the material), and a PBR G-buffer with a PBR `sload.h` (same). All variants (`-hq`, `_d`,
`_d-hq`, `_db-hq`, `aref`, `USE_DX_NORMAL_MAP`) compile; the MSAA alpha-to-coverage variant fails
in unmodified SSS too (`alpha_to_coverage` undeclared).

To check a PBR weapon in game, set `PBR_DEBUG_VIEW` in `pbr_ixray.h`: 4 (metalness) should show
metal parts white; black means the pixels are still not PBR (THM material not "PBR
(metallic/roughness)", or no PBR `gbuffer_stage.h`). 3 (roughness) should be dark on polished
metal; if it is bright, the roughness channel holds gloss (smoothness) and must be inverted.

## PBR texture packer (tools/pack_pbr_textures.py)

Builds `<name>.dds`, `<name>_bump.dds` (R height, G normal Y, A normal X) and `<name>_bump#.dds`
(R metal, G roughness, B subsurface, A AO) from separate maps, including Call of Duty exports:
`--gloss` inverts gloss to roughness, `--nog` reads the packed CoD normal (R gloss, G Y, B Z, A X).
Writes uncompressed DDS with renormalised, Toksvig-widened mips, or BC through texconv. `--dry-run`
prints per-channel statistics and warns when a "roughness" map looks like gloss. See the header
for caveats (normal Y convention, height channel = 5 cm position offset, never sRGB).

## gbuffer_stage.h under SSS

`gamedata/shaders/r3/gbuffer_stage.h` in this repository is the vanilla-shader version (no motion
vectors, no TAA unjitter). SSS ships its own; the compatibility patch now generates SSS's file with
the PBR material target added (`99 - PBR compat - SSS (00 - MAIN FILE)`). The exe's copy must never
sit above SSS: SSS shaders then fail with `undeclared identifier 'ssfx_jitter'` (ssfx_taa.ps) and
lose the velocity target.

## PBR overlay for modpacks (tools/make_pbr_overlay.py)

For modpacks with their own shader stack (GAMMA: Atmospherics on top of SSS/ES, plus its own
LUT, puddles, DOF and NVG shaders). Instead of replacing the whole stack, the overlay takes the
winning version of each file the PBR pipeline depends on, adds the PBR code to *that* version
and writes only those files (`gbuffer_stage.h`, the `deffer_base_*bump.ps` G-buffer shaders,
the `accum_*.ps` lights, `combine_1.ps`, and ES's `pbr_brdf.h`/`lmodel.h`/`hmodel.h` plus SSS's
`screenspace_common.h`/`ssfx_ssr_combine.ps`), with this repository's PBR headers. Everything
else stays exactly as the modpack ships it. Install it last; regenerate it when the shader mods
below it change.

    python tools/make_pbr_overlay.py --layer <lowest mod> --layer <next> ... --out "PBR overlay" --zip

Each `--layer` is a mod folder/zip/7z or one merged copy of the whole stack, lowest priority
first; leave out the exe's gamedata and earlier PBR packs (it refuses to patch files that an
earlier pack already patched). `build_shader_pack.py --no-compat` makes such a merged copy from
the mod archives. Settings block: `PATCH_PBR_TEXTURES`, `PATCH_LEGACY_IXRAY`,
`INCLUDE_REPO_HEADERS`, `REQUIRED`.

Found while testing GAMMA: a pack built on plain SSS/ES made GAMMA's world far too dark,
because GAMMA's weathers are tuned for Atmospherics' `shadow.h` (cloud-shadow floor) and
`pbr_settings.h` (albedo boost). The overlay avoids that class of problem by never replacing
those files.

## Merged shader pack (tools/build_shader_pack.py)

Flattens this branch's shaders, ES 1.10, SSS 23.5 (module list in the settings block) and the
generated compat patch into one gamedata folder, with PACK_REPORT.txt listing every overridden
file. `--shaders-only` keeps only gamedata/shaders. The compat patch now also patches SSS module
94's own pbr_brdf.h / lmodel.h (they override the ES patch's copies in a normal SSS install).

## Beef's NVGs and Atmospherics

Neither touches a file PBR changed, so the compat patch needs nothing for them; the order does matter:
exe gamedata -> Beef's NVGs (main folder only) -> ES -> SSS (+ its Beef patch) -> Atmospherics -> compat.
The exe ships an older `night_vision.h` (no `shader_param_8`); Beef's must win or SSS's NVG code and
Atmospherics' `sky2.ps` do not compile. Beef's `combine_1`/`combine_2_naa`/`pp_blur` must lose to SSS's
(SSS 23.5 already integrates Beef's NVG). `build_shader_pack.py --beef --atmospherics` does this.

## Upstream update: MT build 871e2491 (2026.10.11)

Upstream's multithreaded branch was merged again, from `7b79d2be` to `871e2491` (154 commits),
as one squashed commit. Heatvision stays removed: upstream's new shader-bus hotness path into it
is left out, the bus lanes themselves are kept. Upstream's new THM-less texture descriptors
(`<texture>.ltx` with a `[texture]` section) can also mark PBR textures: `material = pbr` (or `4`),
plus an optional `[shaders_options]` section in the same file, e.g.

    [texture]
    bump_mode = use
    material = pbr

    [shaders_options]
    USE_DX_NORMAL_MAP = 1

## Atmospherics and ES color grading

Atmospherics asks for ES as `01 Enhanced Shaders - PBR` + `Optional - Fixed Bloom` +
`Optional - shader_param controls - simple`, without `02 Enhanced Color Grading`: it grades the image
in its own `img_corrections.h`, where `r__color_grading` is a mild midtone tint. With `02` also
installed, ES's ACES grading reads the same value as a power curve (`2 * (1 - r__color_grading)`;
Atmospherics' 0.75 gives 0.5) and the image looks washed out. `build_shader_pack.py --atmospherics`
uses Atmospherics' ES set (`ATMOSPHERICS_ES_MODULES`).

`tools/check_shaders.py` now counts fatal errors (a missing include used to pass as "OK") and folds
file names and `#include` paths the way Windows resolves them (ES ships `ACES.H` and includes
`"ACES.h"` and `"ACES_LMTs\..."`). Known DXC-only error: upstream's `fakescope.ps` declares
`SV_POSITION` twice; the game's FXC accepts it.

## Heatvision is back

vegeta1k95's Heatvision, the silencer overheat glow and the "cold zombies" option are restored from
upstream's MT build, including the shader-bus `obj_hotness` path. HDR10 stays removed.

Heat used G-buffer target 2 upstream, which holds the PBR material here, so on R4 it moved:

| Slot | Target | Contents |
|---|---|---|
| 0 | `rt_Position` | position, normal, material |
| 1 | `rt_Color` | albedo, gloss |
| 2 | `rt_Surface` | PBR material |
| 3 | `rt_ssfx_motion_vectors` | SSS motion vectors |
| 4 | `rt_Heat` | Heatvision (`s_heat`) |

The forward and combine passes bind `rt_Heat` exactly as upstream (slots 1 and 2 there; no PBR
target is bound in those passes). The render backend now tracks all 8 D3D11 slots, a five-target
`u_setrt` binds the G-buffer, and `phase_scene_end` unbinds slot 4 so the heat vision pass can read it.

Shaders write heat as `SV_Target4` under the PBR G-buffer and as `SV_Target2` otherwise (R3, which
shares the `r3` shader folder). That is in this repository's `gbuffer_stage.h` (so Heatvision now also
works without SSS) and in the SSS `gbuffer_stage.h` the compatibility patch generates, which keeps SSS's
own heat output. The heat vision post-process itself comes from a shader mod (upstream ships only a
stub `r2/heatvision.ps`); it reads `s_heat`, so it does not care which slot the heat was written to.
