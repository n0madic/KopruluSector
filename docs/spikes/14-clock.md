# Spike 14: game clock, catch-up and suspend

Plan: phase 0 item 14, D8, §0.2.
- Product code: `game/simulation/clock.lua`, tests in `tests/lua/test_clock.lua`.
- Spike: `spikes/14-clock/`.

## Clock contract

- Logical step length comes from the profile speed table `0x503360`: 167, 111, 83, 67, 56, 48, 42 ms.
- At most `max_catchup` steps run per rendered frame (default 4). Any backlog beyond that is dropped and counted in `dropped_ms`.
- Focus loss, iconify or suspend → `pause()`. Resume → `resume()` clears the accumulator **and** the wall-time reference, so background time is never replayed.
- The interpolation fraction for presentation is `accumulator / step`.
- The manager feeds measured wall time: `advance_wall(socket.gettime())`.

## Findings

1. **Defold's `dt` cannot drive catch-up.** After a 1 s stall inside one frame, the next `update` receives `dt` = 0.009 s on macOS and 0.015–0.017 s on Android; the measured wall delta is 1.009–1.017 s. With `dt` the clock silently lost 34 of 257 steps; with wall time it ran the 4 allowed steps and dropped the rest (840 ms).
2. **Resume needs a reset of the wall reference.** The first Android run reset only the accumulator. The first frame after returning from HOME then carried a 3 s delta and ran 4 catch-up steps at once (3,822 ms dropped). `advance_wall` and `resume()` fix this; there is a unit test for it.
3. **Events arrive on every tested platform.**
   - macOS: hide/show of the app gives `WINDOW_EVENT_FOCUS_LOST`/`GAINED`.
   - Android: HOME, then relaunch, gives `FOCUS_LOST` at 5.28 s and `FOCUS_GAINED` at 8.32 s; the emulator gives the same.
   - Pausing follows these events, not gaps between frames. Whether `update` keeps running while unfocused depends on the platform and `run_while_iconified`, and was not measured separately.

| Run (14 s wall) | Paused | Steps | Ideal steps for active time | Max steps/frame | Dropped |
|---|---:|---:|---:|---:|---:|
| macOS, hide 3.2 s, wall-driven | 3.20 s | 234 | 257 | 4 | 840 ms (the stall) |
| Android, HOME 3.0 s, final version | 3.04 s | 238 | 261 | 4 | 840 ms (the stall) |

The difference between steps and ideal steps is the dropped stall backlog plus the startup frame.

## Decisions

- The mission manager uses `clock.advance_wall(socket.gettime())` once per frame and runs the returned number of steps.
- It subscribes to `window.set_listener`: pause on `FOCUS_LOST`/`ICONIFIED`, resume on `FOCUS_GAINED`/`DEICONIFIED`.
- `max_catchup = 4` is the default. The plan's §10 tick budget (≤ 4 ms) gives ≤ 16 ms of catch-up work in the worst frame.

## Not measured

- iOS (no device). On the Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan) the stall check repeats the finding: after a 1 s stall the engine reports `dt` 0.008 s against a wall delta of 1.008 s; 14 s active gave 313 steps (ideal 333, the difference is the dropped 840 ms stall backlog), at most 4 per frame. Suspend/resume was not exercised on the phone.
- Browser tab hide/show. A hidden tab stops `requestAnimationFrame`, which the headless run cannot show.
- Long device sleep.
