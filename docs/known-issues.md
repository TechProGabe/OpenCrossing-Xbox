# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Untested on hardware

- The NES screen (furniture NES games): `blit_draw` in `xbox_nv2a.c` draws
  it, checked with a test pattern in xemu (`-DXBOX_DBG_NES_TEST=N`), not yet
  with a real NES game on a console.
- The stick deadzone is per console, not measured per controller:
  `controller.ini` (default 43%, sized for the worn test pad). Automatic
  calibration was tried against the hardware stick traces and rejected: a
  worn stick's rest point moves after every release (18-41%), so a learned
  value undershoots and walks the character on its own.

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.

## Performance headroom

The present call waits for the GPU to go idle every frame, so CPU and GPU
work never overlap. That is the next lever if a scene drops below 60 fps.
Top of the profile is `pb_busy`.
