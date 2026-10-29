# ADR-0001: Tools-first container inspection

- Status: Accepted
- Scope: M0 and the first M1 slice

## Context

The project must learn the structure of real GoPro media before building a player or committing to
large framework dependencies. The first utility must remain independently useful, handle
multi-gigabyte files with bounded memory, preserve unknown structures, and expose stable output to
scripts. Parser behavior later used by applications must not diverge from laboratory behavior.

## Decision

`gb-inspect` is a thin command-line presentation layer over the dependency-free `gb_container`
library. Tests and future tools use the same checked production parser. The parser reads fixed-size
fields on demand and never loads a media payload or complete file into memory.

Only boxes whose payload is structurally defined as a direct list of child boxes are recursively
walked. Unknown boxes are retained as FourCC, offset, size, and header size; their payload is not
guessed. Nesting is limited to 32 levels. Every size is checked against its header and parent
boundary before offsets advance.

ISO BMFF JSON uses the `gb.inspect/v1` schema. Breaking field semantics require a new schema ID.
Text output is intended for people; scripts should consume JSON. Exit codes distinguish command
misuse, I/O or missing input, corruption, and unsupported formats.

## Consequences

- Container inspection remains usable without Qt, FFmpeg, a display server, or network access.
- Very large `mdat` payloads can be skipped through seeking with constant memory use.
- New container semantics must first be represented and tested in `gb_container`, then exposed by
  the CLI.
- Unknown and internally structured boxes remain opaque until dedicated parsers and evidence are
  available.
- The initial schema describes box structure, not yet the semantic stream inventory required by
  the full M1 exit condition.
