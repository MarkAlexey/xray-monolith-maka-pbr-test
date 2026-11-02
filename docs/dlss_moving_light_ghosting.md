# DLSS ghosting from moving lights

Notes from the November 2026 investigation on branch `dlss-post-after-upscale`.
Nothing here is implemented.

Test setup: GAMMA with Atmospherics 2.69 RC7.71, Screen Space Shaders 23, DLSS
preset K, Quality, DX12 build.

## Symptom

An NPC's flashlight sweeps across a wall in a dark room. As the light moves away,
the shadow comes back to the wall with a lag, in a square, grid-like pattern.

- It is on the lit surfaces, not in the air.
- It happens only while the light moves; a still light shows nothing.
- `r_upscaler_debug 1` (the upscaler's input) is clean.
- `r_upscaler_debug 2` shows motion vectors on the NPC, none on the lit area.
- It is more visible with `r2_volumetric_lights off`, because the beam no longer
  covers it.

## The cause

DLSS reuses each pixel's history along its motion vector. The wall does not move, so
its motion vector is zero and DLSS keeps the old, lit history. Only the lighting
changed, and nothing tells DLSS that. It rejects the stale history late and in
blocks, which is the grid.

## Ruled out

None of these changed the artifact:

- SSS beams: `r2_volumetric_lights off`.
- SSS screen-space light shadows: `ssfx_sss_quality 12,4,1,0`.
- SSFX indirect light: `ssfx_il 6.666667,0,0.6,5`.
- SSFX AO: `ssfx_ao 1,0,1,1`.
- CAS sharpening: `r_dlss_sharpness 0`.
- Low resolution light shadow maps: `ssfx_shadows 1536,1536,0`.

The spot light shaders (`accum_spot_normal.ps`, `accum_base.ps`, `shadow.h`) use no
screen-space or per-frame noise.

## Rejected

- **Motion vectors for moving lights.** A pixel has one motion vector, and it must
  describe the surface. Giving the wall the light's motion makes DLSS fetch history
  from the wrong part of the wall, so the texture smears with the light. The lit
  area also stretches and changes shape, so there is no single correct vector.
- **The DLSS bias-current-color mask.** Presets K and J ignore it, and those are the
  presets where the problem shows. Not worth building.

## Ideas for later

### History drop under moving lights

The engine knows which lights moved this frame and where they were on the last one.

1. Each frame, for every light whose position or direction changed, draw its light
   volume at the old and the new transform into an L8 mask at render resolution.
   The light volumes are already drawn for lighting (`lights_render.cpp`).
2. Under the mask, change the motion vectors before `dlss::Evaluate` so that DLSS
   drops history there. This is mode 3 of the reactive mask test on branch
   `dlss-transparency-artifacts` (`dlss_reactive_mv.ps`, see
   `docs/dlss_transparency_artifacts.md` there): point the vectors off screen,
   staggered over a few frames per pixel.

Unlike the reactive test, which guessed from image differences, the mask covers
exactly the pixels whose lighting changed. It is still a workaround: those pixels
lose temporal anti-aliasing while the light moves, and the edges of the mask may
show.

### Other options

- **Another preset.** M, L or J may reject stale lighting faster than K. Needs
  `vid_restart` after `r_dlss_preset`.
- **HDR input.** Floating-point scene colour before tone mapping, with tone mapping
  after DLSS. More precision in dark areas and real brightness ratios may make DLSS
  reject stale lighting sooner. Not guaranteed to remove the grid.
