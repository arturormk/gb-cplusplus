# Architecture decisions

## ADR-0001: tools-first container inspection

**Status:** accepted for M0 / first M1 slice.

`gb-inspect` is a thin command-line presentation layer over the dependency-free `gb_container`
library. This gives tests and future tools the same checked production parser while preserving an
independently buildable executable. The parser reads fixed-size fields on demand and never loads a
media payload or the complete file into memory.

Only boxes whose payload is structurally defined as a direct list of child boxes are recursively
walked. Unknown boxes are retained as FourCC, offset, size, and header size; their payload is not
guessed. Nesting is limited to 32 levels. Every size is checked against its header and parent
boundary before offsets advance.

The CLI JSON schema begins at `gb.inspect/v1`. Breaking field semantics require a new schema ID.
Text output is intended for people; scripts should consume JSON. Exit codes distinguish command
misuse, I/O/missing input, corruption, and unsupported formats.

## Unresolved risks

- The actual top-level and nested box topology of GoPro MAX (2025) `.360` files is unverified.
- Stream/sample description parsing is not implemented, so codecs and data tracks are not yet
  enumerated.
- The current parser uses seekable regular files; pipes and remote inputs are out of scope.
- Some ISO BMFF container-like boxes have internal headers or counted records and intentionally
  remain opaque until their layouts receive dedicated parsers and fixtures.
- The project license and later Qt/FFmpeg dependency choices remain unresolved.
