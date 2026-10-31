# Incident: video races to catch up with audio after a screen lock

Date: 2026-10-31
Component: gb-player (`PlaybackEngine`, `VideoView`)
Environment: Ubuntu, GNOME on X11
Severity: Minor. Playback was wrong for a while, but nothing crashed and no data was lost.
Status: Closed. Fixed in `2f48d62`, checked with a real screen lock, and covered by the
`gb_player_gui_stall` test. A first attempt, `b1ea7fe`, did not fix it.

## Summary

A recording played for a long time, a couple of chapters. During that time the laptop screen
locked. Audio kept playing normally. After unlocking, the video played at about 1.5–2× speed while
the audio stayed at normal speed, until the video caught up with the audio.

On GNOME/X11, locking the screen slows the player window's buffer swaps to about one a second.
Each swap blocks the whole GUI thread. Only the GUI thread took decoded frames off the queue, so
the decoder sat blocked behind a full queue, and the video fell further behind the audio every
second. After unlocking, the video could only catch up as fast as the decoder could run.

## Impact

- The picture fell behind the sound by about 0.55 s for every second the screen was locked. In a
  76-second lock it reached 54 s behind. After unlocking, the video played visibly fast for about
  24 s until it reached the audio.
- Telemetry linked to the video position (map cursor, height/speed cursor) followed the late
  picture.
- Only the GPU (frame-driven) rendering path was affected.

## Timeline

- Playback with audio ran across chapters. The screen locked. Audio continued. After unlocking,
  the video played at about 1.5–2× until it reached the audio. The user reported it and suggested
  pausing on lock.
- **First diagnosis, partly wrong.** Reading the code showed that frames are presented only from
  `QQuickWindow::afterAnimating`. That led to the assumption that a locked window is simply not
  drawn while the GUI thread stays free. `b1ea7fe` added a fallback: if no scene-graph frame
  arrives for 100 ms, the engine's 4 ms timer presents frames instead. All tests passed, but the
  fix was committed without a real lock test.
- **The user retested: no change.** The session turned out to be X11, where a locked window stays
  mapped. Instead of guessing again, an opt-in timing trace (`GB_PLAYER_TRACE=1`) was added.
  It showed that even in normal playback the 4 ms timer fired only about 60 times a second, once
  per screen refresh. So the GUI thread is paced by the buffer swap.
- **Trace of a real lock** (about 1 minute; the display turned off 30 s after locking):

  | | Before lock | Locked (~76 s) | After unlock |
  |---|---|---|---|
  | GUI thread wakes per second | 60 | **1** (longest gap ≈ 1000 ms) | 60 |
  | Sets shown + dropped per second | 25 + 0 | ~3 + ~8 | ~46 + ~35 |
  | Video behind the clock | 20 ms | grew to **54 s** | caught up over ~24 s |

  The throttling started at the moment of locking, not when the display turned off.
- **Second fix** (`2f48d62`): the decode worker drops stale frames by itself (see Resolution).
- **Trace of a second real lock with the fix** (about 1 minute): while locked, the GUI thread
  still woke once a second, but the worker dropped about 24 sets a second, and the video stayed at
  most about 1 s behind (the gap between wakes) without growing. After unlocking: 25 sets a
  second, no dropped frames, 47 ms behind straight away. The user saw a brief stutter (1–2 s)
  right after unlocking. The trace shows the player presenting every frame on time during it, so
  it is most likely GNOME's unlock animation rather than the player.

## Root cause

The player has four relevant parts:

1. **Frame-driven presentation.** With the GPU renderer, frames are presented from
   `PlaybackEngine::advanceFrame()`, which `VideoView` calls on `QQuickWindow::afterAnimating`.
2. **The GUI thread is paced by the buffer swap.** The swap blocks the GUI thread until the
   compositor accepts the frame, so every timer on that thread is limited to the swap rate too.
3. **Audio as master clock.** Audio runs on its own SDL thread. The playback clock follows the
   audio device position, so it keeps advancing whatever the GUI thread is doing.
4. **A small queue that only the GUI thread drained.** The decode worker fills a queue of about 4
   frame sets and then waits. Only `tick()`, on the GUI thread, removed sets from the queue,
   presenting the newest one that was due and dropping the older ones.

When the screen locked, GNOME slowed the window's swaps to about one a second, so the GUI thread
woke about once a second. Each time, it could drop at most the few sets that were queued, and the
decoder then had to refill the queue. The video advanced about 11 sets a second against the 25
the clock required. After unlocking, the GUI thread ran at full rate again. The video then moved
forward at decoder speed (about 80 sets a second, which looks like 1.5–2×) until it reached the
clock.

### Why the first fix failed

`b1ea7fe` assumed that the render loop stops while the GUI thread stays free. On X11 the window
stays mapped while locked. The render loop keeps running at the swap rate, and the GUI thread
stalls with it. So the timer fallback never got a chance to run.

## Resolution

`2f48d62` (`fix(gb-player): let the decode worker drop stale frames when the GUI stalls`):

- The GUI thread publishes the clock state (running flag, base ticks, origin) to the worker under
  a new `clock_mutex_`. It does so whenever the clock is rebased or playing or scrubbing changes.
  The lock order is `mutex_` first, then `clock_mutex_`.
- Before each wait, the worker runs `dropStaleLocked()`. It drops queued sets while the set after
  the front one is already due. This is exactly the rule `tick()` applies, so the worker never
  drops a set that would have been shown. The first set after a seek is never dropped.
- While its queue is full, the worker waits for at most 10 ms (`kWorkerPoll`) instead of
  indefinitely. So it keeps decoding in step with the clock however rarely the GUI thread runs.

The video therefore never falls more than one GUI-wake interval behind, and it is in sync as soon
as drawing resumes. `b1ea7fe`'s timer fallback is kept: it still helps when the window is
not exposed (such as when minimized) while the GUI thread is free.

### Alternatives considered

- **Pause on screen lock and resume on unlock** (the user's first idea). Rejected. Lock detection
  is not reliable across GNOME, X11 and Wayland, and it would stop audio that the user may want to
  keep hearing with the screen off.
- **Seek the video to the clock when it falls far behind.** Rejected as the main fix. A seek turns
  audio off and then back on, so the user would hear a gap. It also depends on the GUI thread
  waking up to notice the lag.
- **Move presentation off the GUI thread or change the Qt render loop.** Rejected. It is much
  larger, and the swap blocking is outside the player's control.

### Trade-off

While locked, the video keeps decoding at the same CPU cost as visible playback.

## Verification

- A real 1-minute screen lock on GNOME/X11 with `GB_PLAYER_TRACE=1` (`__stuff/GS040047.360`):
  the video stays in sync during the lock and after unlocking (see Timeline).
- Normal playback is unchanged: 25 sets a second, no dropped frames, 25–50 ms behind the clock.
- Builds cleanly; all 62 tests pass (`ctest --preset dev`).
- **Regression test** (added after the fix): `gb_player_gui_stall` runs the real benchmark
  (`--benchmark 1 --seeks 0 --stall 3`) on a generated 10 s, 25 fps clip with SDL's dummy audio
  driver. While playing, it blocks the GUI thread for 3 s, which stops `advanceFrame()` and the
  engine timer alike, as the throttled swap did. It fails unless the decode worker decodes at
  least 80% of the sets due during the stall. With the fix, 74 of 75 sets were decoded and dropped
  by the worker. With `dropStaleLocked()` and the `kWorkerPoll` timeout turned off, 0 were
  decoded and the test failed. Under the offscreen platform the engine presents from its timer,
  not frame-driven; that does not change what the test proves, since a blocked GUI thread stops
  both paths. All 63 tests pass.

## Lessons and follow-ups

- **Measure before fixing.** The first fix came from reading the code. It was committed after
  only a build and the tests, and it failed. A one-line-a-second trace showed the real mechanism
  in a single run. Timing problems need a trace from the real environment before a fix is
  committed.
- **The GUI thread cannot be relied on to keep time.** It is paced by the compositor and can
  stall for seconds. Any work that must keep pace with the audio clock belongs on a thread the
  compositor cannot block.
- **Keep the diagnostic.** `GB_PLAYER_TRACE=1` is documented in the gb-player README for future
  timing problems.
- **Test the mechanism, not the symptom.** The regression test does not simulate a screen lock.
  It reproduces what the lock did to the player, a blocked GUI thread, and checks the property
  the fix guarantees. An injected clock, first thought necessary, turned out not to be: the worker
  decodes and waits in real time anyway, and the gap between passing and failing (74 against 0
  sets) leaves a wide margin for slow machines.
- **Closed without investigation:** the 1–2 s stutter right after unlocking. It was brief, and
  the trace shows the player presenting on time during it, so it is taken to be GNOME's unlock
  animation. Reopen if a similar stutter appears outside an unlock.
