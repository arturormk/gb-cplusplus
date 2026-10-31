# gb-telemetry

`gb-telemetry` extracts `GPS5` records from an ISO BMFF `gpmd` track as schema-versioned JSON,
CSV, GPX 1.1, or GeoJSON. JSON and CSV retain invalid and unknown-validity records; GPX and
GeoJSON omit them unless `--include-invalid` is requested.

```sh
gb-telemetry --format json recording.360
gb-telemetry --format csv recording.360
gb-telemetry --format gpx recording.360 > route.gpx
gb-telemetry --format geojson recording.360 > route.geojson
```

Coordinates, altitude, and speeds are recorded values. A point's media time is an integer tick
with the track's rational time base. Individual records do not carry media timestamps, so records
are distributed uniformly over their containing GPMF sample and carry a one-record timing
uncertainty. `GPSU` is a recorded UTC anchor; later UTC values in the same payload are derived.
No position is interpolated or extrapolated. Current extraction supports the verified `GPS5`
layout; `GPS9` and sensor-series export are not yet interpreted.

The writers live in the `gb::telemetry` library (`gb/telemetry/export.hpp`), which `gb-player`
also uses to export the GPS track of every chapter of a recording as one file. Output for a single
file is exactly what `gb-telemetry` prints. For several chapters, in chapter order:

- JSON: schema `gb.telemetry.sequence/v1`, with a `sources` array of `gb.telemetry/v1` objects.
- CSV: a leading `source_file` column names the chapter file of each row.
- GPX: one track named after the first and last chapter, with one `trkseg` per chapter whose
  extensions carry `<gb:source>` with the chapter file name.
- GeoJSON: each feature gains a `source_file` property.

Media times stay relative to each chapter file.
