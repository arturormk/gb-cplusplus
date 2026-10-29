# gb-time

`gb-time` reports every track's integer decode timeline, sample-table discontinuities, recorded
`GPSU` UTC anchors, and the inferred media-to-UTC relationship. `--at TICKS` selects the closest
GPS record without interpolating a position.

```sh
gb-time recording.360
gb-time --json recording.360
gb-time --at 5000 recording.360
```

Media time is always emitted as integer ticks plus a rational time base. UTC drift is a diagnostic
estimate derived from adjacent anchors. GPS records are uniformly assigned within a GPMF sample,
so their time has a one-record interval uncertainty. The tool does not currently claim a mapping
to sensor capture time, apply edit lists, or model TimeWarp/time-lapse capture cadence.
