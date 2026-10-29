# Format support

## Implemented

- ISO BMFF identity through a structurally valid top-level `ftyp` box.
- 32-bit, 64-bit extended, and size-to-parent box lengths.
- Checked top-level hierarchy and direct-child recursion for a conservative set of structural
  container boxes.
- UUID box header accounting.
- Lossless FourCC bytes, byte offsets, total sizes, and header sizes in JSON.
- Content-based JPEG detection with checked marker and entropy termination parsing.
- JPEG dimensions, precision, component count, and directly appended codestream enumeration.
- Structural detection of JFIF, EXIF, XMP, MPF and APP6 payloads without interpreting private
  metadata values.

## Evidence from GoPro MAX (2025) samples

Five owner-provided samples have now established one native `.360` topology, one single-stream
MP4 topology, and three JPEG layouts. See
[`formats/gopro-max-2025-sample-findings.md`](formats/gopro-max-2025-sample-findings.md) for
fingerprints, exact commands, redacted observations, and remaining uncertainties. This is format
evidence, not a claim that the current library decodes those streams or supports every mode.

## Not implemented

- Native decoding/reconstruction of the observed GoPro MAX (2025) `.360` topology.
- Track, codec, sample table, timing, GPMF, EXIF, XMP, MPF, or APP6 metadata decoding.
- Payload extraction, fragmented-media semantics, HEIF item interpretation, or file repair.

The observed topology has been independently cross-checked with FFprobe and ExifTool. General MAX
support remains unverified beyond the fingerprinted samples, and playback/reconstruction is not
implemented.
