# ADR-0003: Bounded native GPMF inspection

Status: Accepted

Date: 2026-10-29

## Context

M1 needs loss-aware GPMF inspection and raw payload export while keeping the laboratory tools easy
to build on a fresh checkout. FFmpeg can locate `gpmd` packets and GoPro publishes a reference GPMF
parser, but either dependency would enlarge the current runtime/build surface. Container sample
location and GPMF value interpretation are separate responsibilities.

The official `gopro/gpmf-parser` reference was reviewed at commit
`9a7150632892c7356c91145c889016f07b0ed48d` (2026-08-18). Its documented KLV layout, big-endian
types, nesting, padding, `SCAL`, `SIUN`/`UNIT`, and dual MIT/Apache-2.0 licensing informed this
implementation. No source was copied or vendored.

## Decision

`gb_container` parses the bounded ISO BMFF track and sample tables and exposes byte-accurate sample
references. `gb_metadata` independently parses bounded GPMF KLV payloads, retains raw bytes for
every entry, decodes primitive types, applies sibling scaling/units, and leaves complex,
compressed, or unknown semantics inspectable rather than guessed. `gb-metadata` is the standalone
presentation and raw-export layer.

The implementation limits nesting, element counts, sample-table entries, and individual payload
reads. Integer track ticks and rational time bases remain authoritative.

## Consequences

M1 has no new runtime dependency and can be tested with synthetic media. The official parser
remains the behavioral reference and a future differential-test candidate. This slice does not
decode compressed/complex data, static image metadata, or capture/UTC synchronization; those
limitations are explicit in tool output documentation.
