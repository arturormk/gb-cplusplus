# TimeWarp and TimeLapse videos

Status: Planned for v1.0 (2026-11-01). No TimeWarp or TimeLapse recording has been examined yet.
Everything below about how the camera stores these modes is an assumption until samples are
recorded and fingerprinted.

GoBro must open, play and export TimeWarp and TimeLapse videos from the GoPro MAX (2025) as well
as it does real-time videos, with the telemetry, map, graphs and clock right.

## The two modes

- **TimeWarp** is a stabilised, speeded-up video meant for a camera that moves: walking, cycling,
  driving. The speed is a fixed factor or automatic, and may change during the recording.
- **TimeLapse** is a video made from frames taken at a fixed interval, meant for a camera that
  stays put: clouds, a sunset, a crowd.

The mode is only what the user chose. Nothing stops anyone from walking with a TimeLapse or
standing still with a TimeWarp, so **GoBro must not infer motion from the mode**. It treats both
as *time-compressed* recordings and decides from the timing it measures, and from the GPS, what the
recording contains.

## What is different from a real-time video

In a real-time video one second of media is one second of the world. In a time-compressed video
it is many seconds, and the factor may vary within the file. This breaks every place where the
code takes media time for real time:

| Where | Real-time assumption | What must change |
|---|---|---|
| Clock | Media position is elapsed time. | Show the capture time (UTC or local wall clock) next to the media position, and label which is which. |
| Telemetry timing | GPS records are spaced in media time. | Keep record times in media ticks (ADR-0004), and add a mapping from media time to capture time (below). |
| Speed | Reported GPS speed is real speed, and so is distance over media time. | Show the reported speed or distance over capture time. Distance over media time is too fast by the factor. |
| Route jump filter | `mark_route_jumps` compares each step with the reported speed times the media time between records. | Use capture time. In media time every step looks like a jump. |
| Graph smoothing | The player and `gb-render` average over a centred 3-second media window. | Decide whether the window is in media or capture seconds, and label the graph's time axis. |
| Automatic map scale | `ahead` is in minutes of recording. | Define `ahead` in capture time, or warn that a TimeWarp covers a lot of route per media minute. |
| Audio | There is an audio track to play and gate (ADR-0008). | Expect no audio track, or audio only in real-speed stretches. Playback must work and stay smooth without it. |
| `gb-render [output] speed` | Speeds up a real-time source. | Applies on top of the recording's own factor. Report the combined factor in `--dry-run` and `--json`. |
| Chapters | Chapters are contiguous in media and GPS time (measured on MP4). | Check that the rule holds for long time-compressed recordings. |

A stationary TimeLapse brings its own problem: hours of GPS jitter around one spot. The route
should show a point or a small cluster, not a scribble, and speed should read as zero or near zero,
not as jitter. This is also where the jump filter's outlier clusters need checking.

## The media-to-capture time mapping

The central new piece is a mapping from media time to capture time for each recording,
measured rather than read off a mode label:

- **Measured from the GPS.** The GPS UTC anchors (`GPSU`) give capture time at known media ticks.
  Their slope against media time is the time-compression factor, and changes in that slope show
  automatic speed and real-speed stretches. This needs no knowledge of the mode, so it works for
  a TimeLapse taken while moving and for one taken standing still.
- **Declared in the file.** The mode, speed and interval are probably also recorded in the GPMF
  settings or the container metadata, as for photos (ADR-0011). If they are, `gb-metadata` and the
  player's panel show them, and they are checked against the measured mapping. They are never
  the only source: both real-time sample videos carry `RATE 1_10SEC`, so a setting can be left
  over from another mode ([lens modes](lens-modes.md#evidence-so-far)). Video settings are in
  `moov/udta/GPMF`, which the tools do not read yet.
- **Without GPS.** Without a fix there is no measured mapping. Use the declared factor if there
  is one and say so; otherwise show media time only and say that capture time is unknown. Never
  guess.

The mapping is piecewise linear, monotonic, and stored with its provenance (measured, declared,
or unknown) and its uncertainty, following ADR-0004. It belongs in `gb_telemetry`, so the
laboratory tools, the player and `gb-render` share it. An ADR records the design and amends
ADR-0004.

## Work

1. **Samples.** Record with the MAX (2025), in 360° and in single-lens mode where the camera
   allows it:
   - a TimeWarp while moving at a fixed speed, and one at automatic speed, with a real-speed
     stretch if the camera has one;
   - a TimeLapse standing still, and a TimeLapse while moving;
   - a long enough recording of each kind to make at least two chapters.

   Fingerprint them in [`formats/`](../formats/), as was done for the first samples: tracks,
   frame rate, audio, GPMF keys, GPS rate and timing, and where the mode, speed and interval are
   stored. Add them to `__stuff/`.
2. **Laboratory tools.** `gb-inspect` and `gb-metadata` report the mode, speed and interval if the
   file declares them. `gb-time` reports the measured mapping, its segments and their factors,
   and how they compare with the declared values ([`tools/gb-time`](../../tools/gb-time/README.md)
   currently says it does not model TimeWarp or time-lapse cadence).
3. **Library.** The mapping in `gb_telemetry`; the jump filter, smoothing, scale curve and speed
   readouts in `gb_geo` and `gb_export` use capture time.
4. **Player.** The clock shows both times. The panel shows the mode and factor. The graph and map
   use capture time. Playback, seeking and Capture mode work without audio.
5. **`gb-render`.** Time and speed fields use capture time, a field shows the time-compression
   factor, and `[output] speed` combines with the recording's factor. The manifest records capture
   time per frame.
6. **Tests.** The CTest fixture writer generates videos whose GPS UTC advances faster than media
   time: a constant factor, a change of factor halfway through, a stationary jittering track, and
   no audio track. They check the mapping, the jump filter, smoothing, speed readouts, the
   manifest, and audio-free playback. No private media in the tree (`docs/testing.md`).
7. **Docs.** Format support, the BLUEPRINT status, and the `gb-player` and `gb-render` READMEs.

## Not in scope for v1.0

- TimeLapse *photo* sequences (a series of JPEGs). Each JPEG still opens as a photo.
- Making a TimeWarp out of a real-time video beyond what `[output] speed` already does
  (frame-rate conversion, slow motion, stabilised retiming).

## Open questions

1. Where does the MAX (2025) record the mode, speed and interval, and does it record real-speed
   stretches?
2. Are TimeWarp `.360` files the same two-stream layout as real-time `.360` files?
3. Is the GPS sampled at its usual rate in capture time, so many records per media second, or is
   it thinned or tied to frames?
4. Do time-compressed recordings have an audio track at all?
