# ADR-0004: Explicit telemetry and clock provenance

- Status: Accepted
- Date: 2026-10-29
- Scope: M2 telemetry and timing laboratory

## Context

GPS5 values occur in timed GPMF payloads, but each position record does not carry its own ISO BMFF
timestamp. GPSU supplies a UTC anchor for its stream while GPSF and GPSP separately describe fix
validity and dilution of precision. Treating every decoded coordinate as equally valid, assigning
floating-point seconds, or silently interpolating positions would hide important uncertainty.

## Decision

Use integer presentation ticks plus the track's rational time base as the authoritative media
time. Distribute GPS5 records uniformly through their containing GPMF sample and attach a
one-record-interval uncertainty. Treat GPSU as a recorded UTC anchor and UTC values offset from it
as derived. Interpret GPSF 2 and 3 as valid locks, other present values as invalid, and an absent
GPSF as unknown. Treat stored GPSP as DOP multiplied by 100 and expose raw GPSP divided by 100,
according to the official GPMF reference.

Never interpolate or extrapolate a missing position. Preserve all records in JSON and CSV; omit
invalid or unknown-validity points from GPX and GeoJSON unless the caller explicitly includes
them. Report invalid-fix and missing-sample intervals as gaps. Keep capture time distinct and
unavailable until evidence supports a mapping.

## Consequences

Downstream player and map code can consume the same GUI-independent model without losing whether
a value was recorded, derived, invalid, or uncertain. Exports remain useful with absent GPS and
do not turn missing data into zeroes. Fine-grained record timing is an inference bounded by the
GPMF sample duration, not a claim of exact sensor capture time. GPS9, edit lists, and TimeWarp
capture cadence require later evidence and explicit extensions.
