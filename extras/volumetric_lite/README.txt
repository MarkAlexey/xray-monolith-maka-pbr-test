Light volumetric sun shafts (optional part of the Monolith PBR shader pack)
=========================================================================

What it does
  Replaces Atmospherics' volumetric sun shaft shader with the original GSC one (the one
  vanilla Anomaly, Screen Space Shaders and IX-Ray use): one shadow-map read per step,
  12 / 20 / 28 steps for low / medium / high (r2_sunshafts_quality). Atmospherics' version
  does about 110 shadow-map reads per pixel at high quality; this one at most 28.

Install
  Install this archive as its own mod in Mod Organizer and place it BELOW Atmospherics in the
  left pane (higher priority). accum_volumetric_sun.ps must show as winning.
  Remove it (or untick it) to get Atmospherics' shafts back.

What changes
  The shafts look like Screen Space Shaders' shafts, not Atmospherics': no edge enhancement.
  Weathers tuned for Atmospherics can make them dimmer or brighter. Edit the defines at the top
  of gamedata\shaders\r3\accum_volumetric_sun.ps to tune:
    VOLSUN_STEPS_LOW / _MEDIUM / _HIGH   steps per quality level
    VOLSUN_GAIN                          brightness multiplier (1.0 = as SSS)
    VOLSUN_OOB_LIT                       1: distant haze beyond the shadow range counts as lit
  The game recompiles the shader on the next start.

Credits
  GSC Game World (the original ray march), Screen Space Shaders (Ascii1457: weapon fade,
  bounds check, fog fade), IX-Ray team (reference port).
