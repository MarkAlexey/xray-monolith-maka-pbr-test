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
HUD_REFLECT       = 0.25 ; first-person models (hands, weapons): a quarter of the reflection
WORLD_REFLECT     = 0.6  ; everything else using this texture: 60% of the reflection
```

Every line becomes a shader define for that texture's G-buffer shader (DX11 only), and is added
to the shader's name so the shader cache keeps the combinations apart. A value of `0` leaves the
line out. The file is read when the game starts. The options the shaders here understand:

- `USE_DX_NORMAL_MAP`: see above.
- `HUD_REFLECT` and `WORLD_REFLECT` (0..1): tone down the reflection of a texture that looks too
  wet, such as bare skin or leather gloves. 1 is unchanged. Legacy textures get their gloss scaled,
  PBR textures get rougher (`lerp(1, roughness, k)`), so highlights spread and dim. `HUD_REFLECT`
  works in SSS's first-person shaders (`deffer_base_hud_*`), `WORLD_REFLECT` in the world ones
  (`deffer_base_bump` / `deffer_base_aref_bump`: this repository's, and SSS's / ES's through the
  compatibility patch and the overlay), so hands can be toned down without changing the NPCs
  that share the texture, or the other way around. Without SSS first-person models are drawn
  with the world shaders and read `WORLD_REFLECT`. Terrain, detail objects (grass) and the flat
  (no bump map) shaders read neither.

Other options are passed through for future use.

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
| `r4_vslr_localize` | 2 | localized (parallax-corrected) lookup for sharp reflections: search steps 1-4, 0 = plain direction lookup as in IX-Ray |

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

Localized lookup (by L4U6H1N6-LUN4T1C, "Approximate Ray-Tracing on the GPU with Distance
Impostors", Szirmay-Kalos, Aszodi, Lazanyi and Premecz, 2005): the panorama pass also stores, per
direction, the distance from the capture point to the captured surface (R16F, no mips), and
`pbr_ixray.h` searches along the reflected ray from the shaded pixel for where it meets that
surface (false-position iteration, `r4_vslr_localize` steps), then reads the panorama in the
direction of the hit. Nearby walls then stay in place in reflections instead of sliding across
them. Used for sharp reflections only (faded out between roughness 0.3 and 0.6, where the blur
hides the parallax). With `r4_vslr_faces` below 6 the faces come from different frames; their
distances are re-measured from the current capture point in the panorama pass, so the distance
field stays consistent while moving (directions are not re-projected: small offsets only).

Limits: one probe centred on the camera (localized lookup only for sharp reflections, and only as
good as the captured distances); objects the main view draws as LOD billboards are left out; only PBR
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

## The `pbr-test-s3ds-mt-sss-lass` branch: LASS first person, jiggle bones, alpha test

This branch is `pbr-test-s3ds-mt-sss` plus Zira3l137's work from
[xray-monolith-lass](https://github.com/Zira3l137/xray-monolith-lass): the first-person body fix,
jiggle bones, the honoured alpha-test ref, and the skeleton loader fixes jiggle bones need. LASS's
picture-in-picture scopes (every `pip:` commit) are **not** in it; they come later. Every commit
keeps Zira3l137 as its author; the ones that had to be adapted to this branch say so in their
message.

| LASS commit | Here | What it does |
|---|---|---|
| `16a4613b` first person view actor body mesh handling | ported | The first-person body is a second, never-animated instance of the actor visual, so `Bone_Calculate()` on it welded the spine sub-tree to its parent. The spine offset is now a plain translation of the copied pose (`shift_bone_subtree_y`). `g_legs_spine_offset_y` defaults to 0. Upstream has since split the legs into a main and a shadow model and copies bones by name, so only the spine part was needed. |
| `485a1cf9` + `300f21bc` jiggle bones spring controller | as is | `CBoneSpringController` (`bone_spring.cpp/.h`): spring-damped secondary motion on configured bones, driven by `bctCustom` callbacks on the actor visual, so first person, third person and shadows all follow. |
| `5ebca8e5` + `300f21bc` honor alpha ref authored value | ported | Alpha-tested shaders clip against the ref their pass sets (`m_AlphaRef`) instead of the fixed `def_aref` 200/255, and can dither the threshold for TAA (`aref_patch.h` rewrites the served `common_defines.h` / `common_iostructs.h` at compile time). `uber_deffer` keeps the PBR texture options; only the two alpha switches of LASS's console block were taken. |
| `0e62cbb0`, `48d57c34`, `7d49bce0`, `95b3a100`, `9f38a37a`, `bbd0b362` skeleton fixes | ported, one commit | An outfit model with extra bones loaded through stock actor `.omf` files: the loader now reads the file's own tracks (it used to read one per model bone and run off the end), every visual owns its partition bound to its own skeleton (it used to share the first model's bone indices: heap corruption), bones without a track hold their bind pose instead of snapping to their parent, and the two HUD hand models agree on their partition layout. |
| `d9ce2032` reclaim orphaned shared-memory blocks | as is | Animation key blocks dropped by discarded motion sets are freed at load boundaries. |

### Jiggle bones

Bones are listed in the outfit's section, or in `[actor]` for every outfit:

```ini
jiggle_bones = bip01_l_boob, bip01_r_boob   ; the default when neither section has the line
jiggle_stiffness = 140      ; spring constant: higher = snappier
jiggle_damping = 11         ; lower = more overshoot
jiggle_max_offset = 0.035   ; metres, hard clamp
jiggle_translate_gain = 1.0
jiggle_rotate_gain = 1.6
jiggle_bone_length = 0.075  ; virtual length that turns lag into a swing angle
jiggle_world_gain = 0.9     ; jump / landing response
jiggle_axis = 2             ; bone-local "length" axis: 0 = i, 1 = j, 2 = k
```

The same names exist as `g_jiggle_*` console commands (plus `g_jiggle_enabled`) for tuning live;
the outfit's values win again on the next re-equip. A visual without any of the listed bones
costs nothing.

### Alpha test

| Command | Default | Effect |
|---|---|---|
| `r__alpha_ref_live` | 1 | `def_aref` reads the pass's ref (`m_AlphaRef`); 0 = the old fixed 200/255 |
| `r__alpha_dither` | 1 | hashed threshold per pixel so TAA resolves soft edges (not with MSAA) |

Both change the shaders that get compiled: delete `appdata/shaders_cache/r4` after changing them
and restart. Model and tree blenders now pass their authored ref (`oAREF`); grass, terrain-aref
and branch passes keep 200, the old value.

### Caveats

- The skeleton fixes log a lot on purpose: every model bone without a motion track prints a
  `[MODEL-BIND] ... holding bind pose` line when the model loads, so an intentional extra bone
  can be told from a broken export. A model whose motions cannot bind at all still stops with
  the stock assert, which now points at those log lines. (In LASS that case opens an in-game
  exit prompt; that is part of its `pip:` model-loader work and is not here.)
- Atmospherics already hashes its alpha test (`hashed_alpha.h`) around `def_aref`; with
  `r__alpha_dither 1` the two stack. If foliage edges look noisier than before under
  Atmospherics/GAMMA, try `r__alpha_dither 0`.
- A pixel shader the dither cannot be added to (its input is not named `I`, or is not one of
  `p_flat` / `p_bumped` / `p_shadow_direct_aref`) is compiled again without it, automatically.

### How it was checked

- `tools/check_cpp.py --base pbr-test-s3ds-mt-sss --changed`: the remaining "new" errors are the
  known clang environment noise (`xr_vector` not resolving as `std::vector`, class hierarchies
  not resolving in xrGame, both present for the same constructs in unchanged code).
- The alpha-ref header rewrite was reproduced in Python and every alpha-tested pixel shader of
  vanilla + this repository and of the GAMMA stack (SSS, ES, Atmospherics, Beef's NVGs, GAMMA's
  overrides) was compiled with DXC, plain alias and dithered: no new errors, VSLR capture shaders
  included.
- Not run in the game: that, and the MSVC build, are CI's and yours.

### Grass rework (TheLostInPlace) and static screen quads

Also on this branch: TheLostInPlace's grass rework from
[xray-monolith `fps-clean`](https://github.com/TheLostInPlace/xray-monolith/tree/fps-clean), all eleven
commits with their author kept (`ae374bd2`, the LUT/DOF change on that branch, is not included).
Only the console lines of `r__skip_unused_passes` had to be dropped while porting (that switch
belongs to `b26ede47`, which is not part of this port); every grass source and shader is
byte-identical to `fps-clean`.

| Command | Default | What it does |
|---|---|---|
| `r__detail_shadow_cull` | 1 | grass shadow entries are culled per sun cascade / light by slot sphere |
| `r__detail_rows` | 1 | per-slot instance rows copied four lanes at once straight into the constant buffer |
| `r__detail_inst` | 1 | grass instances uploaded once per frame and drawn instanced |
| `r__detail_inst_res` | 1 | instance rows stay resident on the GPU (compute copy of changed slots only) |
| `r__detail_merge` | 1 | instanced draws of the same object merged |
| `r__detail_occ` | 1 | grass spans tested against the live depth buffer, drawn indirect |
| `r__detail_occ_first` | 1 | near spans drawn first (they become occluders for the rest) |
| `r__detail_thin` | 0 | far merged grass thinned by a per-instance hash (`_start`, `_keep`, `_band`) |

**Under SSS / ES / GAMMA:** the instanced path (`r__detail_inst` and everything after it) only
engages for a grass shader that has `<vs>_inst*.vs` variants. SSS, ES and GAMMA draw grass with their
own `deffer_grass.vs` (from `details_blend.s`) and SSS replaces `deffer_detail_s_flat.vs`, so:

- `gamedata/shaders/r3/deffer_grass_inst*.vs` (TheLostInPlace): two-line wrappers that define
  `DT_INST` / `DT_MERGE` / `DT_OCC` / `DT_THIN` and include the stack's `deffer_grass.vs`;
- the stack's `deffer_grass.vs` / `deffer_detail_s_flat.vs` get the matching `DT_*` paths from the
  overlay tool (`PATCH_GRASS_INST`) and the compat generator (`patch_grass_inst`): the edits
  TheLostInPlace made to SSS 23.5's copies, applied to whichever copy wins. Run on SSS's own files
  the generator reproduces TheLostInPlace's shaders exactly. SSS's per-instance terrain normal and
  alpha (`exdata[61]`) move to the `dt_ex` buffer the engine fills; ES's grass has none.
- The GAMMA zips from CI carry the patched shaders; with an unpatched stack the engine logs
  `[DT-INST] ... old` once and keeps the stock path (the shadow cull, the faster row copy and the loop
  changes still apply).

Checked: the GAMMA zips were built locally with the CI script; every grass variant compiles (no new
DXC errors) and binds `dt_rows` (+ `dt_ex` for SSS, + `dt_verts` / `dt_idx` for merge / occ); ES's
patched grass compiles and binds the same minus `dt_ex`.

Full-screen quad (IX-Ray `1eea3317`, LVutner): `xrRender/FullscreenQuad.h` keeps the full-screen
post-process quad in an immutable vertex buffer, and the passes draw it with
`draw_fullscreen_quad(C)` instead of writing the same four vertices into the dynamic buffer on every
draw. It replaces the earlier generic quad cache (`r__static_screen_quad`, `ScreenQuad.h`), which was
more machinery than the gain was worth.
- The quad is exactly what the passes wrote: (0,h) (0,0) (w,h) (w,0) at full size, z = `EPS_S`,
  w = 1, UV 0..1, `FVF::F_TL` + `QuadIB`. One buffer per vertex colour (black, white, transparent
  black) and screen size the passes draw at, at most 8.
- 42 of the 76 4-vertex passes were converted by a script that only touched sites whose effective
  values on the DX10/11 branch are exactly that quad: SSS SSR (two of five), SSS, SSS ext, AO
  (four of five), fog scattering, motion blur, TAA, volumetric blur, water waves, bloom, rain, DOF,
  LUT, NVG / fake scope / heatvision, gasmask, SMAA, pp bloom. The blur chain (half/quarter/eighth
  size), the `1/ScaleFactor` SSR/AO/IL passes, the half-UV water blur, the light accumulation and
  stencil quads (EPS offsets, clip-space corners) keep their own dynamic quads. Unused locals of the
  converted passes (`Offset`, `d_Z`, `p0/p1`, ...) were removed with them.
- DX9 (R2) uses half-texel UVs that depend on the size: there `draw_fullscreen_quad` still writes
  the dynamic buffer every draw, with the same vertices as before.
- DLSS / FSR 3 branch: the passes size their quad with `RenderScreenWidth()` / `RenderScreenHeight()`
  (the render resolution inside the upscaled scene, the display size after it), so the R4 target
  hands that size to the helper; render and display size each get their own buffers. Only sites
  sized with `RenderScreen*()` were converted there (the same 42).
- `reset_light_marker` keeps its dynamic quad and binds the recording context's own stream on a
  qkff99 worker (`RenderContextId()`), as before.
IX-Ray `c3f4bb38` (wallmark creation) was already in upstream Monolith.

Checked: the 18 new grass shaders compile with DXC on vanilla + this repository and on the GAMMA stack;
`check_cpp.py` reports only the known clang noise (`xr_vector` / `xr_resource` templates) for the
grass sources and no new errors for the screen quad change. Not run in the game.

### CPU optimisations from qkff99 (`codex/mt-consolidated-fixes`)

Step 1 of porting [qkff99's branch](https://github.com/qkff99/xray-monolith/tree/codex/mt-consolidated-fixes)
(same upstream base, 871e2491): the CPU-side render optimisations and their follow-up fixes, 30
commits cherry-picked with the author kept (`-x` lines point at the original hashes). The
multithreaded shadow recording (sun / local / rain on D3D11 deferred contexts) and DLSS / FSR3 are
later steps and are not in here. None of these commits change the picture on purpose, and none
adds a switch except the scheduler ceiling below.

What is in:

| Area | Commits (qk) | What changes |
|---|---|---|
| Backend | `78644d3d` `06901db2` `eb837055` | unchanged typed constants are not rewritten; only changed sampler ranges are bound; constant destinations no shader reads are skipped |
| HOM (occlusion) | `3bd6b024` `6ff4cf53` `a1dd29b6` | coarse-to-fine conservative depth tests, SSE2 depth mip reduction, occluder sort distances computed once |
| Frustums / sectors | `34dacfd8` `ef64b89f` `cef67662` | union tests stop after full containment; exact duplicate sector frustums rejected; the camera frustum gets an eye plane, so boxes behind the camera are rejected before portal traversal |
| Spatial DB | `8771d6ac` `d20000a8` | items removed by stored index; the three sun cascades query the spatial tree in one walk (a revision counter falls back to the per-cascade query if anything moved in between) |
| Statics | `3c136ce4` `2f03c49a` `ffd980a5` | adjacent visible immutable static ranges drawn as one batch, also in shadow maps; partially visible LOD groups cull their children through the active frusta |
| Trees | `0cf028e7` `cc64b692` `f08e9e92` | world/view matrices and shared constants reused while they match; compatible shadow index buffers shared |
| Skinning | `959ed410` `92d7fd11` | packed bone rows (current + previous frame) cached, compatible arrays written in bulk |
| Shadows | `f5a8a210` `8719e633` `b58e9661` | sun cascade casters pruned against the light-space envelope of the visible receivers; skipped for the far cascade when volumetric sunshafts need the full map; empty caster volumes bounded by the cascade sides |
| Misc | `ba2c5b84` `b04cf435` `3de02c68` `a831399e` | WVP reuse, particle sprites culled before quad expansion, nearest pending grass slots in one pass, offscreen PDA map textures rejected |
| Fixes | `93f06621` `b6dff5de` `93f81892` | API adaptation; load-time batch builders allowed on the PPL level loader; texture ownership kept across asynchronous loads (no use-after-free when a texture dies while queued) |
| Scheduler | `80fd4778` (IX-Ray `cf2f5f9c`) | adaptive object-update budget clamped to 1..`sheduler_budget_max` ms, smoother adaptation |

Adapted while porting:

- **Our CSM.** The single spatial walk (`d20000a8`) runs after all three cascades are prepared and
  before the one-time `clear_smap_csm()`; cascades still render into the array slices.
- **Receiver pruning** works with the grass rework: a grass slot widens the receiver envelope only
  when it was actually visible (`DetailManager.cpp`, `slot_visible`).
- **Grass culling** keeps the sphere test (with the new eye plane) instead of qk's slot box test,
  so wind-swayed blades at the screen edge cannot pop.
- **No `Device.mmThreadID` here**: the load-time builders check `!RImplementation.b_loaded`
  (qk's own follow-up `b6dff5de`); qk's main-thread assert crashed when the level loaded on PPL.
- `PROF_TAG` (Optick tag, no-op in normal builds) added to `profiler.h`; the rest of qk's Optick
  rework is not ported.
- `RenderViewPreparation.cpp` hunks dropped (view preparation is not ported).

| Command | Default | What it does |
|---|---|---|
| `sheduler_budget_max` | 5 | ceiling of the per-frame scheduler budget in ms (IX-Ray value). 66 = the old Monolith ceiling |

Not taken from qk's branch: R1/R2 3DSS, the Lua GC / frame pacing rework, startup speedups, their
fullscreen quad (ours is `FullscreenQuad.h`), their grass constant changes, view preparation,
the DLSS-related diagnostics, and `1db923f1` (it reverts demonized's `R*R` SSA formula, which moves
LOD and discard distances game-wide, and changes far terrain / LQ static materials).

Caveats:

- qkff99's commits are agent-written; most say build and runtime validation were deferred. The
  later ones in their branch fix earlier ones, and those fixes are included, but nothing here has
  been run in the game yet.
- With `sheduler_budget_max 5`, a very busy scene (many NPCs, A-Life spikes) spreads object updates
  over more frames: higher FPS, but AI can react a little later. Raise it if NPCs feel sluggish.
- The receiver pruning and the eye plane only remove work that cannot reach the screen; if a shadow
  goes missing at the edge of the view, that is where to look first (`f5a8a210`).

Checked: `tools/check_cpp.py --base 80bf4167 --changed` (R3, R4, xrGame, xrEngine units): every new
error is the known clang noise (`xr_vector` / `vector<bool>` shims, `xr_resource` dependent bases,
UI class hierarchy); no undeclared names or signature mismatches. Not built with MSVC or run.

### Multithreaded shadow recording (qkff99, step 2)

qkff99's `79a9a1a6` and `2ae9c457` (plus the `vis_update` line of `6631dc22`), adapted. Shadow maps
are recorded on D3D11 deferred contexts by worker threads, each with its own backend (`RCache` is
now the active context's backend), constant buffers, state and sampler caches, and the owner
thread replays the command lists in the original order:

- **Sun**: three workers, one per cascade. The static part of each cascade is captured on its worker
  while the owner draws the G-buffer; the middle and far cascades record their static casters early.
  Dynamic casters are captured on the owner (Lua/renderable callbacks keep their order), skeletal
  poses are frozen into snapshots, then each cascade records.
- **Local lights**: four workers. Spatial queries overlap scene work, each shadow atlas records on a
  lane, and lights whose lighting shaders are safe also record their accumulation (`accum_spot` /
  `accum_point`) in the same list. OGSR's `smapvis` (one occlusion query per static caster, hides
  casters that never reach the shadow map) runs with it.
- **Rain**: one worker records the rain-wetness depth map.

Anything a worker cannot do safely stays on the owner, packet by packet: a shader with UAVs or
unknown resources, a constant binder not marked safe (`R_constant_setup::parallel_safe()`, false by
default, so every binder added by PBR / SSS / ES / scripts keeps the owner path), translucent
shadows, volumetric lights, GI, HUD lights.

| Command | Default | What it does |
|---|---|---|
| `r4_mt_sun_shadows` | 1 | sun cascades recorded on workers |
| `r4_mt_local_shadows` | 1 | local-light shadows (and safe light accumulation) recorded on workers |
| `r4_smapvis` | 1 | occlusion-query culling of static local-light shadow casters (also works with `r4_mt_local_shadows 0`) |
| `r4_mt_rain_shadows` | 1 | rain-wetness depth map recorded on a worker |

Adapted while porting:

- **CSM.** With `r4_sun_csm 1` the near cascades record straight into their slice of the shadow-map
  array (no extra memory), the far cascade into `rt_smap_depth`; the one-pass CSM lighting is
  unchanged. With `r4_sun_csm 0`, qkff99's layout: two extra private shadow maps (2 × 64 MB at
  `r2_smap_size 4096`) for cascades 0 and 1.
- **Grass.** The grass rework uploads instance rows, runs compute copies and indirect draws on the
  immediate context, so grass shadows are never recorded on a worker: the owner draws them after
  each cascade's / light's list with the grass rework's own code (sun frustum cull, per-light
  path). Lights with grass shadows keep owner lighting.
- **Static screen quads** stay owner-only; a worker resetting the light-marker stencil writes its own
  dynamic quad.
- No upscaler: qkff99's DLSS hooks (render size, jitter, scene depth) are replaced by the plain
  screen size and `HW.pBaseZB`. The sampler cache has no DLSS variants; anisotropy / LOD bias
  changes resynchronise the workers.
- Not ported: view preparation, qkff99's grass recording (`RenderShadow`), the upscaled light
  occlusion and visibility diagnostics (`c922790d`), and `6631dc22`'s change of the
  `r2_exp_donttest_uns` default.

Caveats:

- Untested in game. qkff99 measured lower mean and p95 frame times on their build; the CSM, grass
  and screen-quad paths above are new and nobody has run them yet.
- Each switch can be turned off on its own at any time; with all four at 0 the renderer takes the
  previous code paths (the only difference left is that `RCache` and the state caches are looked up
  through the active context, a thread-local read).
- `r4_smapvis` hides a static caster only after a query saw zero samples, and resets when the light
  moves or its static list changes; if a local-light shadow ever goes missing, try `r4_smapvis 0`.
- Workers are 8 extra threads (3 sun, 4 local, 1 rain) that sleep when idle.

Checked: `tools/check_cpp.py` over every R4 translation unit (228 files, so the `RCache` /
`StateManager` macro change is covered everywhere) and the shared files for R3: only the known
clang noise (`xr_vector` / `vector<bool>` shims, MSVC-only dependent template names). Not built
with MSVC or run.

## The `pbr-test-s3ds-mt-sss-lass-dlss` branch: DLSS / FSR 3

`pbr-test-s3ds-mt-sss-lass` plus NVIDIA DLSS and AMD FSR 3 super resolution: qkff99's port
(`bb6988ad`, `de29c960` and the occlusion fix of `c922790d`) of
[brodrigz's xray-monolith 0.5](https://github.com/brodrigz/xray-monolith) (`854dd279`), which is
based on OGSR's DLSS implementation (OGSR team, xrSimpAI). Kept as its own branch so the LASS
branch stays upscaler-free.

How it works: the scene renders at a lower resolution (`RenderScreenWidth()` / `Height()` while
`dlss::BeginScene()` .. `EndScene()` is active) with a per-frame sub-pixel jitter. After the scene
the upscaler (DLSS through NGX, or FSR 3.1 compiled into the exe) reconstructs it at display
resolution from colour, depth and SSS's motion vectors; post-processing, the PDA screen and the UI
draw at display resolution afterwards.

Requirements (the engine checks them and otherwise renders natively with a red on-screen notice):
Screen Space Shaders 23.5 (motion vectors), brodrigz's shader compatibility patch (it provides
`dlss_*.ps` and the jitter-aware SSS files), MSAA off; DLSS needs an RTX GPU and
`nvngx_dlss.dll` (shipped in `bin/`), FSR 3 needs feature level 11.1.

| Command | Default | What it does |
|---|---|---|
| `r_upscaler` | dlss | `off`, `dlss`, `fsr3` |
| `r_dlss_quality` | off | `off`, `dlaa`, `ultra_quality`, `quality`, `balanced`, `performance`, `ultra_performance` |
| `r_dlss_preset` | default | DLSS model: `default`, `j`, `k`, `l`, `m` |
| `r_fsr3_quality` | quality | `native_aa`, `ultra_quality`, `quality`, `balanced`, `performance`, `ultra_performance` |
| `r_dlss_sharpness` | 0 | CAS sharpening after either upscaler, 0..1, applies immediately |

Method / quality changes need `vid_restart` (the menu's Apply does it). The menu (brodrigz's
`modxml_dlss_options.script`) has everything except `ultra_quality`, which is console-only.

Adapted to this fork:

- **PBR passes at scene resolution**: SSLR's targets (they copy `rt_Generic_0` / `rt_Position`)
  and its dispatch, the one-pass CSM sun quad, VSLR's restore of the main viewport, MT local-light
  volumetrics. The static screen-quad cache needs nothing (quads at the new size are new keys).
- **MT shadows**: recording contexts get the frame's scene size and jitter for position
  decompression (`RenderContext::Synchronize`, inside the scene); the recorded light accumulator
  binds the scene depth; the DLSS shader constants are parallel-safe. The sampler cache is
  qkff99's variant cache (the upscaler's mip bias switches per scene without recreating samplers)
  with the workers' copy.
- **No HDR10** in this tree: the upscalers are created for LDR colour, second-viewport and PDA UI
  targets stay at display size.
- **Light occlusion queries** draw depth-only (`c922790d`): the display-size back buffer with the
  scene-size depth buffer made them fail with an upscaler.
- **Grass**: the DLSS patch's `deffer_grass.vs` (uniform jitter, previous wind for motion vectors)
  gets the instanced-grass paths in the GAMMA overlay; the engine already sets `wave_prev` /
  `dir2D_prev` for every grass path.
- **The DLSS patch on top of GAMMA's own overrides** (`build_shader_packs.py --top-layer`): the
  patch ships whole files made from plain SSS / Atmospherics / stock Beef's NVGs. Where GAMMA
  replaced one of them with its own rewrite, the patch's copy is not just dropped on top: its
  changes are line-merged onto GAMMA's file (`deffer_impl_flat.ps` keeps GAMMA's terrain puddles),
  or, when a line merge conflicts, re-applied by a known adapter (`night_vision.h`: GAMMA's
  "nvg dof blur" NVGs keep their `NVGParams`, and the depth blur reads `s_position` at scene size).
  Those files are added to the overlay too, since the overlay sits above the patch. A second DXC
  pass compiles every shader the patch ships or whose headers it changes against the stack without
  it; any new error fails the build. The patch's plain `night_vision.h` over GAMMA's NVG shaders is
  exactly what broke `nightvision_gen_1.ps` (`unrecognized identifier 'NVGParams'`).

Shaders: the DLSS shaders are **not** in this build's db0 (only the menu script and its strings
are), so a missing patch is reported instead of half-working. CI attaches brodrigz's GAMMA
compatibility patch 0.5 unchanged, downloads `nvngx_dlss.dll` 310.9.1 from NVIDIA's repository
(SHA-256 pinned) into `bin/`, and builds the GAMMA shader packs with the patch's `base` and
Atmospherics components on top (`build_shader_packs.py --top-layer`, `DLSS_PATCH_COMPONENTS` in the
workflow). The packs target the October 2026 Atmospherics update only: `atmos_2026_10`
(its AO, indirect light, SSR, SSS and terrain shaders), which `tools/make_dlss_atmos_component.py`
added to the patch from that Atmospherics' own shaders with brodrigz's DLSS edits. Its SSR and IL
files used to be optional installer ticks (page "2b"); left unticked, the base component's SSS
`ssfx_ssr.ps` stayed and failed to compile against Atmospherics' `screenspace_reflections.h`
(`SSFX_ScreenSpaceReflections: cannot convert output parameter from 'float4' to 'float'`), so
they are now always part of `atmos_2026_10` (`atmos_rc7` for 2.69
RC7.x and `base` alone for 2.68.93 remain possible values).

Caveats:

- Untested in game. qkff99 and brodrigz tested their builds; the PBR-specific parts above are new.
- SSLR's temporal history is reprojected without removing the jitter, so reflections may be a
  little softer with an upscaler (`r4_sslr_temporal 0` to compare).
- PBR without SSS cannot use either upscaler (no motion vectors).

Checked: `tools/check_cpp.py` over every R4 translation unit including `Dlss/`: only the known
clang noise. The GAMMA zips built with the DLSS components compile with DXC with no new errors;
the overlay's grass shader carries both the DLSS changes and the `DT_INST` paths. Not built with
MSVC or run.

### One shader installer (FOMOD) instead of loose GAMMA zips

Four GAMMA zips (overlay / full pack, each with and without the DLSS patch merged in) were too easy
to mix up, so the release now ships one Mod Organizer installer, `PBR_shaders_GAMMA_FOMOD.zip`,
made by `tools/make_shader_fomod.py` from `build_shader_packs.py`'s zips:

- Page 1, install type: **Overlay** (recommended, highest priority) or **Full pack** (above GAMMA's
  shader mods).
- Page 2, DLSS / FSR 3 (only on `-dlss` branches): **With** (packs built with `--top-layer` and the
  patch's `DLSS_PATCH_COMPONENTS`) or **Without** (plain GAMMA stack). The overlay variant needs
  brodrigz's patch installed below it; the full pack contains the DLSS shaders itself (the menu
  script and strings are in the db0), so the patch is optional there.
- Inside: `fomod/` and `variants/{overlay,full,overlay_dlss,full_dlss}/gamedata/`: game files
  only. The installer used to also install a `docs/` folder (a README plus each variant's README /
  PACK_REPORT) as "PBR shaders - Instructions" in the mod folder; that is off now
  (`INSTALL_DOCS` in `tools/make_shader_fomod.py` brings it back). brodrigz's patch stays a
  separate, unchanged download.

CI: the shader job runs `build_shader_packs.py` once into `shader_packs/plain` and, on `-dlss`
branches, once more with the top layers into `shader_packs/dlss`, then wraps them; only the FOMOD
is uploaded and released. The tool is in the tools zip too (`--dry-run` lists the variants).

Checked: built locally from the same sources as CI; `fomod/ModuleConfig.xml` validates against
the ModConfig 5.0 schema (with and without the DLSS page), each pattern's source folder holds the
variant's files (24 / 277 / 26 / 282), and the DLSS full pack carries the patch's 12 untouched
shaders plus the 3 merged ones (`deffer_impl_flat.ps`, `deffer_grass.vs`, `night_vision.h`).
Not yet installed through Mod Organizer itself.

### Profiled DX11 build in CI (Optick)

CI builds a third configuration, upstream's `ProfiledDX11`, next to `DX11` and `DX11-AVX`. It is the
DX11 exe with `XRCORE_PROFILER` defined, so the engine's Optick markers (`PROF_EVENT`, `PROF_FRAME`,
qkff99's `PROF_TAG` counters: sun / local-light shadow workers, HOM, grass cache) are compiled in
and the Optick GUI can connect to the game. The release gets it as `*_profiled.zip`:
`bin/AnomalyDX11_Profiled.exe` (renamed, so it sits next to the normal exe) and its PDB in
`symbols/` under the name the linker embedded (`AnomalyDX11.pdb`, kept away from the release PDB).
The leg is `continue-on-error`: if it fails, the release still ships without it. Optick itself is
upstream's (submodule `src/3rd party/optick-git`, bombomby/optick `8abd28d`); only `PROF_TAG` and
the markers in the ported code came from qkff99.

Checked: with the profiler switched on (`XRCORE_PROFILER PROFILER_OPTICK`), `check_cpp.py` on the 39
R4 translation units with `PROF_TAG` / `PROF_EVENT_DYNAMIC` or our own markers reports no new errors
against the profiler-off build. The packaging step was run locally with and without the profiled
artifact. Not built with MSVC yet.

## The `pbr-test-s3ds-mt-sss-lass-skin-dlss` branch: skin subsurface scattering (experiment)

Screen-space subsurface scattering for first-person PBR skin (hands), after Jorge Jimenez's
separable SSS (Jimenez et al. 2015, "Separable Subsurface Scattering"). Light reaching the skin
spreads under it before it comes back out, red the furthest: shadow edges on the hands turn soft
and warm instead of hard and grey. Specular highlights stay sharp. Based on the DLSS branch; its CI release is
`pbr-test-lass-skin-dlss-latest`.

How it works (`gamedata/shaders/r3/skin_sss.h` has the details):

1. The light passes (`pbr_accum()` in `pbr_ixray.h`, used by this repository's `accum_*.ps` and by
   the compatibility patch for SSS's / ES's / Atmospherics' light shaders) store skin pixels in the
   light accumulator as diffuse irradiance (albedo not yet applied) in rgb and the specular
   luminance, made negative, in alpha. The negative alpha marks the pixel as skin. Every other
   pixel is stored exactly as before. No extra render target in the light passes, so mods'
   light shaders keep working.
2. `r4_rendertarget_phase_skin_sss.cpp` runs two full-screen passes before `combine_1`:
   horizontal (`skin_sss_blur_h.ps`, accumulator to an RGBA16F target with the depth in alpha),
   then vertical (`skin_sss_blur_v.ps`, to an R11G11B10F target). Each skin pixel reads
   `SKIN_SSS_TAPS` neighbours (default 17) along the skin diffusion profile; neighbours that are
   not skin, or much further away in depth (fingers in front of the palm), are replaced by the
   centre colour. Pixels that are not skin return after one or two loads.
3. `combine_1` (`skin_sss_resolve()`) rebuilds the skin's light: blurred irradiance x albedo +
   specular, the specular colour taken from the unblurred light.

Which pixels: PBR pixels with a subsurface value on first-person models (view depth below 1.2 m,
SSLR's HUD test). The subsurface value is the `_bump#` blue channel. For a skin texture whose
blue channel is empty, set it in the texture's `.ltx`:

```
[shaders_options]
SKIN_SSS = 0.8     ; 0..1, replaces the bump# blue channel; also scales the scatter width
```

Restart the game after editing the `.ltx`. The value also drives the sun's wrap-around
translucency, which PBR skin already had.

Console:

| Command | Default | What it does |
|---|---|---|
| `r4_skin_sss 0/1` | 1 | On / off. Needs a restart (changes the light and combine shaders). Off with MSAA. |
| `r4_skin_sss_width` | 0.012 | Scatter width in metres at subsurface 1 (red spreads this far, blue less). |
| `r4_skin_sss_strength` | 1 | 0 = unblurred (to compare), 1 = full scattering. |
| `r4_skin_sss_debug 0..3` | 0 | 1 = skin mask (red tint = subsurface value), 2 = blurred light only, 3 = what the scattering changed (x 4). |
| `r4_skin_sss_timer 0/1` | 0 | Logs the GPU time of both passes every 120 frames (D3D11 timestamp queries). |

Shader settings (top of `skin_sss.h`, restart to apply): `SKIN_SSS_TAPS` 11 / 17 / 25,
`SKIN_SSS_HUD_DEPTH`, `SKIN_SSS_MIN`, `SKIN_SSS_DEPTH_FADE_START` / `_END`. The kernels come from
`tools/make_skin_sss_kernel.py` (Jimenez's profile; `FALLOFF` / `STRENGTH` per colour channel at
its top); its 25-tap kernel matches the values in Jimenez's demo.

Why R11G11B10F only for the result: accumulating the skin's light into a separate R11G11B10F
target with hardware blending would need a second render target in every light pass, including
the mods' light shaders. The accumulator's alpha already gives room for the split (the legacy
pipeline keeps its specular there the same way), so the light passes write nothing extra. The
R11G11B10F target holds the blurred result that `combine_1` reads once (32 bits a pixel instead
of 64).

Caveats:

- Untested in game. Shaders compile with DXC on the vanilla stack and on SSS + ES + the October
  2026 Atmospherics with the generated overlay (no new errors, with and without
  `USE_SKIN_SSS`); the engine files pass `tools/check_cpp.py` with no new errors.
- Only direct light scatters. The ambient (IBL) diffuse and the r2 static sun are added in
  `combine_1` after the blur.
- The specular colour of a skin pixel comes from its total diffuse light: exact for one light,
  a mix with several differently coloured lights on the same pixel.
- A skin pixel that no light pass reaches at all is not marked: it is neither blurred nor used as
  a neighbour. Shadowed pixels are still marked (the sun pass covers the screen).
- Each light adds a tiny marker (1e-4) to the stored specular; one is removed again, so N lights
  on a pixel leave (N - 1) x 1e-4 of extra specular.
- World geometry closer than 1.2 m with a subsurface value is treated as first-person too.
- The packs and the overlay must be regenerated with this branch's tools (CI does). Light shaders
  from an older overlay write skin pixels the old way (unmarked): they then simply get no
  scattering, since `combine_1` and the blur only resolve marked pixels.

### Skin diffuse ramp (separate patch on top of skin SSS)

Branch `pbr-test-s3ds-mt-sss-lass-skin-ramp-dlss`, shaders only (`pbr_ixray.h`). For PBR pixels
with a subsurface value, the diffuse N.L of every light is replaced by a ramp that softens and
tints the shadow line, in the light passes themselves (no blur pass: also for world skin, with
MSAA, and with `r4_skin_sss 0`). `PBR_SKIN_RAMP` at the top of the ramp block:

- `0` off (N.L as before).
- `1` (default) ProcRamp: N.L blurred by a Gaussian of width `PBR_SKIN_RAMP_WIDTH` x subsurface
  (pre-integrated skin, Penner style); the light wrapping past the shadow line is faded and
  tinted by Beer-Lambert absorption, `PBR_SKIN_RAMP_ABSORB` (low red: red reaches furthest).
- `2` LVutner's `sss()`: N.L plus a fixed warm band at the shadow line (`PBR_SKIN_RAMP_LV_IR`,
  `PBR_SKIN_RAMP_LV_TINT`). Cheaper and stronger; it adds light on the lit side too, up to about
  +0.13 at N.L 0.5.

Both take the signed N.L and are not clamped (the wrap lives at N.L < 0); the sun's IX-Ray
wrap-around translucency is skipped for those pixels so the light does not wrap twice. Caveats:
shadow maps still cut the wrap where they darken the far side; with the screen-space blur on as
well, the two overlap at the shadow line (lower the width); the r2 static sun is not ramped.

### Pieces from IX-Ray's develop branch (separate patch on top of the skin ramp)

Branch `pbr-test-s3ds-mt-sss-lass-skin-ixdev-dlss` (local), shaders only:

- **Skin transmission** (`pbr_ixray.h`, `PBR_SKIN_TRANSMIT`, from IX-Ray develop `d97c480b`,
  after Barre-Brisebois & Bouchard / Green): view-dependent light through thin parts for PBR
  pixels with a subsurface value, `pow(saturate(dot(-V, L - N * 0.3)), 4) * 0.25`, tinted
  (1, 0.35, 0.2), scaled by the subsurface value, for every light (IX-Ray: sun only). With the
  ramp off the sun also gets IX-Ray's wrap term at the shadow line in place of 1.6's
  translucency. No thickness map; shadow maps still cut it on the self-shadowed side.
- **Translucency glow fix** (`PBR_TRANSLUCENCY_GLOW_FIX`, develop `c83074a7`):
  `saturate(1.5 * x)` instead of `saturate(3.5 * x + 0.1)`, whose 0.1 floor lit the far side of
  every subsurface pixel. Weaker overall.
- **SSLR resolve weights** (`sslr_settings.h`, develop `454d9ecd`, Stachowiak's stochastic SSR
  ray reuse): reused neighbour rays weighted by D x G x N.L instead of D x N.H
  (`SSLR_RESOLVE_BRDF`), and by 1 / (1 + luminance) against fireflies (`SSLR_RESOLVE_FIREFLY`);
  the Hi-Z trace of that commit is not ported.

Each has a 0 setting that restores the previous behaviour.

## Hi-Z traced SSLR and a reactive mask for DLSS / FSR 3 (experiment, local)

Branch `pbr-test-s3ds-mt-sss-lass-hiz-reactive-dlss`: everything in testing stacked together
(skin SSS, skin ramp, the IX-Ray develop shader pieces, the loading patch) plus these two.

### Hi-Z traced SSLR (`r4_sslr_hiz`, needs a restart)

After Stachowiak 2015 ("Stochastic Screen-Space Reflections") and IX-Ray develop `454d9ecd`.
World rays walk a min-depth pyramid instead of 24 fixed steps, skipping empty space a whole
cell at a time, so long rays find thin or distant hits the fixed steps jump over.

- Pyramid (`sslr_hiz_copy.cs`, `sslr_hiz_reduce.cs`): R32F, full mip chain at scene resolution,
  built each frame before the trace. It stores -1 / view depth: linear along a screen-space ray
  (the trace interpolates exactly that), nearest surface = smallest. Sky and first-person pixels
  are farthest, so rays pass behind the weapon as with the march.
- Trace (`sslr_render.cs`, `sslr_hiz_trace`): in level-0 pixel units, the cell of a pixel at
  level n being pixel >> n clamped to the last cell. IX-Ray scales the UV by each level's size,
  which on odd-sized levels puts some pixels into the neighbouring cell and misses their
  surfaces; found and fixed with a CPU simulation against a brute-force march (2000 rays, odd
  and even sizes, all agree but one ray grazing a pixel corner; 5-6 iterations on average).
- Hits go through the same thickness, back-face and last-frame visibility tests as before. A hit
  the thickness test rejects (the ray went behind a thin object) is marched the old way
  (`SSLR_HIZ_FALLBACK 1`, `sslr_settings.h`). `SSLR_HIZ_ITERATIONS` 64.
- The log shows `IX-Ray SSLR trace: Hi-Z` (or `linear march`) and the pyramid size.

### Reactive mask (`r__upscaler_reactive`, no restart)

Particles, smoke, sparks, rain and glass have no depth or motion of their own, so a temporal
upscaler smears them. Both upscalers take a mask of such pixels and follow this frame there:
DLSS as `pInBiasCurrentColorMask`, FSR 3 as its reactive mask.

IX-Ray (develop `0ba2fbf9`) makes every translucent forward shader write its coverage; here those
shaders belong to the mods, so the mask is derived from what those passes changed, like FSR 3's
own generator: rt_Generic_0 is copied after water and before rain and forward rendering, and
`upscaler_reactive.ps` turns the largest per-channel difference into the mask (R8, scene
resolution). No mod shader changes.

| Command | Default | |
|---|---|---|
| `r__upscaler_reactive 0/1` | 1 | Mask on / off |
| `r__upscaler_reactive_scale` | 1 | Colour difference -> mask |
| `r__upscaler_reactive_cutoff` | 0.02 | Differences below are 0 |
| `r__upscaler_reactive_max` | 0.9 | Cap (FSR recommends staying below 1) |
| `r__upscaler_reactive_debug 0/1` | 0 | Shows the mask in magenta |

Caveats: only with an upscaler on and no MSAA; anything else drawn in the forward pass (lens
flares, HUD effects) is marked too; water (drawn before the copy) and volumetric light (after
the mask) are not. Cost: one full-screen copy and one full-screen pass at render resolution.

Checked: DXC compiles all new shaders on the vanilla stack and on SSS + ES + October 2026
Atmospherics with the generated overlay (no new errors); `tools/check_cpp.py` no new errors, and
the DLSS / FSR 3 backends (outside the checker's SDK paths) compiled separately with the DLSS and
FidelityFX headers, no errors before or after. Untested in game.

## brodrigz's DLSS / FSR 0.6 - 0.6.3: the DX11 fixes (perf and bodycam branches)

The upscaler here is qkff99's port of brodrigz's 0.5. From his 0.6 - 0.6.3
([`958b6d71`](https://github.com/brodrigz/xray-monolith/commit/958b6d71)) the parts that apply to
the DX11 exes are taken; his DX12 frame-generation executable is not (planned separately).

- **AO / IL at reduced resolution** (`82d09dac`): `phase_ssfx_ao/il` clamp the MCM divisor to
  [1, 8] for the viewport but passed the raw value to the shaders (and the `ssfx_ao` / `ssfx_il`
  bindings), so AO and indirect light broke below 1. The shaders now see the clamped value.
- **Lens flares after upscaling** (0.6): with an upscaler the main view's flares are drawn in
  `phase_dlss_postprocess`, at display resolution and without jitter, instead of in the scene,
  where they took the hands' motion vectors and came out as blocks over the HUD. The native-HUD
  depth pass re-locks its fullscreen quad (the flares replace the geometry). (On the bodycam
  branch the PiP scope pass, which is not upscaled, keeps its flares in the combine.)
- **`r_upscaler_debug 0/1/2`** (session only, never saved): 1 shows the upscaler's input, 2 its
  motion vectors (`dlss_debug_mv.ps`, in patch 0.63). The upscaler keeps evaluating, so history
  stays valid. With an older patch the motion-vector element is left empty and 2 shows the input.
- The log starts with a `[PBR-Test] DX11 / DX11-AVX build of <date>; upscaler: ...` line.
- Ultra Quality was already here.

**Compatibility patch 0.63.** `.github/pbr-release/dlss/` now carries brodrigz's 0.63, built from
his sources with his `package_compatibility.py`, with one change: the base component's
`modxml_dlss_options.script` and `st_monolith_dlss.xml` are his 0.5 versions (the 0.63 menu asks
the DX12 exe's `r_upscaler_support` / `r_frame_generation` / `fsr4`, which these exes do not
have; its lists came up empty), noted in `PBR-Test-NOTES.txt` inside. Every other shader shared
with 0.5 is byte-identical. 0.63 stops replacing SSS's / Atmospherics' AO, IL, SSR, contact
shadows and water reflections (his side-by-side tests showed no difference), so the
October 2026 Atmospherics component (`atmos_2026_10`, `tools/make_dlss_atmos_component.py`) is
no longer needed: its terrain shader was already identical to `atmos_rc7`'s and its Better
Beef's combine differs only in comments. CI folds `base atmos_rc7` into the DLSS shader packs.
