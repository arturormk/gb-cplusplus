# ADR-0008: Player audio output, audio-master clock and stable-playback gating

Status: Accepted, completes M4 (2026-10-30)

## Context

ADR-0007 deferred audio. MAX `.360` recordings carry a stereo 48 kHz AAC track (stream 1) and a
four-channel 48 kHz `pcm_s32le` track (stream 6, spatial/ambisonic). Single-lens MP4 has stereo
AAC. Qt Multimedia is not part of the supported package set, and `QMediaPlayer` could not share
the player's synchronized video clock anyway.

Many players emit audio while the user drags the timeline or holds a seek key. The result is
stutter, fragments and stale buffered sound. This player must not do that. Audio is heard only
during normal, stable playback.

## Decision

1. `gb_media` gains `AudioReader`. It opens its own demuxer, discards every other stream, decodes
   one mono or stereo stream, and returns interleaved stereo float at the stream's own rate. Mono
   is duplicated to both channels at unity gain; nothing is rematrixed or resampled. Seeks are
   sample-accurate: the reader seeks 4096 samples early so AAC converges, then trims to the exact
   target. Positions are samples relative to the stream start, the same origin
   `SynchronizedReader` uses for video.
2. `select_playback_audio` picks the first mono or stereo stream. Multichannel streams, such as
   the MAX ambisonic track, are reported and not played: without a spatial renderer, any stereo
   fold-down would be an invented mix.
3. Output uses SDL2's audio subsystem only (callback mode, float32 stereo, 1024-frame buffer).
   SDL converts to the device format, so the callback works at the stream rate and the sample
   count stays exact. If no device can be opened, the player plays video only and says why.
4. **Gating.** Audio is engaged only when all of these hold:
   - the player is playing;
   - the timeline is not being scrubbed (press to release);
   - no seek is waiting for its first frame; and
   - 250 ms have passed since the last seek was shown or the last scrub ended.

   Seeking, scrubbing and pausing disengage audio at once. A 5 ms fade avoids a click, and every
   buffered sample is discarded, so stale audio is never heard after a jump. Held arrow keys (about
   30 seeks/s) and repeated clicks therefore stay silent. Frame stepping pauses, so it is silent
   too. While scrubbing, the clock holds and each completed seek is shown as a still frame.
5. **Alignment.** On engagement, audio is anchored to the running video clock, as the pair
   (PTS, time point). The output first stays silent. At each callback it computes which sample
   the clock will want at the moment that buffer is heard, trims or pads to it, and only then
   becomes audible. The picture never jumps when audio joins.
6. **Clock.** While audio is audible, its position is the master clock: the first sample of the
   last buffer, plus the time elapsed since that callback, minus the latency estimate. The value
   is monotonic and bounded by the samples written. The wall clock is rebased on that position at
   every tick, so it continues seamlessly whenever audio stops (pause, seek, end of the audio
   stream, or an audio underrun). After an underrun, audio realigns on the same timeline. Video
   underruns do not stop audio: as in ADR-0007, late frames are dropped and the timeline does not
   slow down.
7. `gb-player --benchmark` adds an audio gating check: audio must be heard during playback and
   resume after each disturbance. It must produce no audible frames during 40 timeline-scrub
   seeks while playing, during 15 seeks 150 ms apart while playing, or while paused. The run
   fails otherwise. CTest runs this with SDL's dummy driver on a generated 10 s clip.

## Consequences

The build now requires `libsdl2-dev` (SDL 2.0.10+). Only its audio subsystem is used. Signal
handlers are disabled. SDL2 cannot report device latency, so the estimate is one buffer
(21.3 ms at 48 kHz). The true A/V offset is therefore known only to within about one buffer plus
the server's own latency. The offset is not measured acoustically, and no user offset control
exists yet. Spatial audio from the MAX ambisonic track is not rendered. Audio starts 250 ms after
a seek during playback; this is deliberate.
