# README-Claude — about the `clean-renderer` branches

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
