# ADR-0002: Content-based JPEG inspection

- Status: Accepted
- Scope: First photo-format inspection slice

## Context

The first GoPro MAX (2025) corpus contains conventional, superwide, and 360 JPEG photos. Treating
every input as ISO BMFF caused valid JPEGs to be reported as corrupt containers. File extensions
are not sufficient capability evidence, and the panoramic samples contain a primary codestream,
an appended MPF preview, and several metadata-bearing application segments.

Adding photo inspection must not silently change the existing ISO BMFF machine-output contract or
require a full image decoder merely to report structure.

## Decision

`gb-inspect` dispatches on validated file signatures rather than extensions. ISO BMFF output
retains `gb.inspect/v1`; JPEG uses the separate `gb.inspect.jpeg/v1` schema.

The dependency-free `gb_image` library walks JPEG markers with checked lengths, bounds the number
of codestreams and segments, and scans entropy data sequentially with constant memory. It reports
dimensions, precision, components, offsets, lengths, completion, and structural signatures for
JFIF, EXIF, XMP, MPF, and APP6 without guessing payload semantics.

Directly appended JPEG codestreams are enumerated because every current MAX photo contains an MPF
dependent preview after the primary end-of-image marker. No pixel decoding is performed.

## Consequences

- Valid JPEG inputs are distinguished from corrupt ISO BMFF inputs without using filenames.
- Existing ISO BMFF JSON consumers are unaffected.
- The current MAX photo corpus can be structurally inspected without ExifTool, ImageMagick, or a
  JPEG decoding dependency.
- JPEGs whose appended codestream is separated by padding or discoverable only through an MPF
  offset are not yet enumerated.
- EXIF, XMP, MPF, and APP6 fields remain uninterpreted until loss-aware metadata parsers and
  fixtures are added.
- Lossless and progressive variants not represented in the real corpus require additional
  evidence, although their structural markers can be retained.
