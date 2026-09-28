# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## One deadzone for every controller

`xbox_pad_axis.c` applies a 43% radial deadzone to the left stick, sized for
the worn Duke controller used in testing (it rests up to 41% off centre). A
controller in good shape loses the first 43% of its travel too; the rest of
the range is rescaled so slow walking still works. A per-controller
calibration (measure the rest position at boot) would fix this.
`stick_deadzone` in `settings.ini` can only raise the value, not lower it.

## Not ported

- The NES emulator's draw path (the furniture NES games). Draws are skipped
  and logged once.
- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.

## Performance headroom

The present call waits for the GPU to go idle every frame, so CPU and GPU
work never overlap. That is the next lever if a scene drops below 60 fps.
Top of the profile is `pb_busy`.
