# README-Claude — about the `clean-renderer` and `pbr` branches

**The changes on this branch were made by an AI (Claude, by Anthropic), working at the
request of the branch owner.** The code changes, the commit messages and this document were
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
- Reflections use the sky cubemaps without the skybox rotation, and there are no screen-space
  reflections or contact shadows (IX-Ray's versions of those depend on systems Monolith does not
  have).
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
