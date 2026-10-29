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
