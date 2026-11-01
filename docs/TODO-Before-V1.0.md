# TODO before v1.0

v1.0 is the first release series (ADR-0010) and the M6 "initial useful release" of the BLUEPRINT.
This list collects what remains. Tick items off here, and move each into its own plan or ADR when
it needs one. Last reviewed 2026-11-01.

## Required

### TimeWarp and TimeLapse videos

Plan: [`plans/timewarp-timelapse.md`](plans/timewarp-timelapse.md).

- [ ] Record and fingerprint samples: TimeWarp fixed and automatic speed while moving, TimeLapse
      standing still and while moving, 360° and single-lens, and multi-chapter.
- [ ] Laboratory tools report the declared mode, speed and interval, and `gb-time` the measured
      media-to-capture time mapping.
- [ ] Media-to-capture time mapping in `gb_telemetry`, with an ADR amending ADR-0004.
- [ ] Route jump filter, graph smoothing, automatic map scale and speed readouts use capture time.
- [ ] `gb-player`: both clocks, mode and factor in the panel, playback and Capture mode without
      audio.
- [ ] `gb-render`: capture-time fields, a factor field, `[output] speed` on top of the recording's
      factor, capture time in the manifest.
- [ ] Synthetic CTest fixtures: constant factor, changing factor, stationary jitter, no audio.

### Lens modes: Narrow, Linear, Wide, Max SuperView

Plan: [`plans/lens-modes.md`](plans/lens-modes.md).

- [ ] Record a photo and a short video in each of Narrow (27 mm), Linear (19 mm), Wide (16 mm)
      and Max SuperView (13 mm), plus the 270° photo mode, noting what the camera showed.
      Fingerprint them.
- [ ] Parse the video settings block in `moov/udta/GPMF` (`VFOV`, `ZFOV`, `PRJT`, …) and report
      it in `gb-metadata` and `gb-inspect`.
- [ ] Recognise the mode from metadata for photos and videos, with its evidence. Show the GoPro
      name and focal length in the player panel, a `gb-render` field and `--json`.
- [ ] Resolve the "superwide" naming: the MP4 sample is Max SuperView; find the camera's name
      for the 270° `PANO` photo. Rename in `gb_image`, ADR-0011 and the sample findings.
- [ ] Synthetic tests for each lens letter, and missing, unknown and contradictory values.

### M6 gate

- [ ] Run the gate on the `__stuff/` samples: `gb-render --manifest` record checks plus a visual
      comparison with the player at chosen timestamps. Record it in `research/findings/`, as for
      M4 and M5 (export plan, phase 7).
- [ ] Rewrite the M6 entry in BLUEPRINT §11 to match what was built. `gb-map-render` and "GUI
      export presets" were replaced by the tile cache with `--map-no-network`, and by Capture
      mode with settings files. Then set its status.
- [ ] Update `docs/architecture.md`, `docs/dependencies.md` and the README for export and
      TimeWarp/TimeLapse.

### Packaging and release

- [ ] `.deb` and `.rpm` packages from the `release` workflow for even minors (ADR-0010 §7, the
      placeholder in `.github/workflows/release.yml`).
- [ ] Better README screenshots: a MAX `.360` in a spherical view instead of the flat MP4, and a
      Capture mode or `gb-render` frame.
- [ ] `VERSION` to `1.0`, a 1.0 section in `CHANGELOG.md`, tag `v1.0`.

## To decide: do before v1.0 or drop explicitly

- [ ] **Audio sync test for export.** The flash-and-click fixture in the export testing plan,
      checked at the start, middle and end of a range and across a chapter boundary. The M6 gate
      asks for audio synchronisation, so this one is close to required.
- [ ] **GPU-versus-CPU render test.** The offscreen test compares the GPU renderer with itself,
      not with `project_equirectangular` (export plan, phase 1).
- [ ] **Chapter consistency checks.** Chapter audio and `gpmd` durations are not checked when
      chapters are joined (export plan, phase 6).
- [ ] **Multi-chapter `.360`.** The chapter rule is measured on MP4 only (export plan, open
      question 1).
- [ ] **Tile limit.** Check the default `--max-tiles 300` against a real export with
      `width = auto` (export plan, open question 2).
- [ ] **Remaining export ADRs.** ADR-0012 and ADR-0013 cover most of the export plan's "ADRs to
      write". Check whether the recording timeline and location privacy need ADRs of their own.

## Not needed for v1.0

- A separate encoder thread in `gb-render` (speed only).
- Moving `ProfileView` onto `ExportGraph` (refactoring only).
- TimeLapse photo sequences, keyframed reframing, hardware encoders, ambisonic audio.
