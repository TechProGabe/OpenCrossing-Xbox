# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Random crash that takes down the whole console (hardware, 2026-09-27)

Seen twice on a retail Xbox, build with commit `072b3db8`:

1. After 10+ minutes in town, during a summer thunderstorm, right as
   lightning struck. `perf.log` was clean up to then (60 fps).
2. On the next boot, less than a minute in (no `perf.log` line yet), as
   Mr. Resetti appeared.

The tester thinks the trigger may be unrelated to either event, so treat
lightning and Resetti as where it happened, not as the cause.

What we know: no `hang.log` either time, so the 1 Hz TIME_CRITICAL watchdog
thread never ran (or the console was switched off within ~6 s). A stuck game
thread would not stop the watchdog. A CPU exception (the kernel halts), a
whole-machine hang at raised IRQL, or a stalled device access would.
`boot.log` stops at frame 120 by design, so the last lines before the crash
are lost.

Next steps, in order:

- Catch it. Install an exception handler that writes the faulting address,
  registers and stack words to `hang.log` and the screen. Keep a rolling
  log on the HDD (flushed every few seconds) instead of stopping `boot.log`
  at frame 120.
- Repro in xemu with the tester's save from crash 2, which boots straight
  into Resetti (kept outside the repo).
- Leads if it is the rare-content path: the thunder sound
  (`sAdo_SysTrgStart(0x424)` in `aWeather_MakeKaminari`,
  `src/actor/ac_weather.c`) and Resetti's scene both load data that isn't
  used often, so check for a NULL from a failed allocation (about 6 MB free
  at runtime) or a bad disc/ARAM read.

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
