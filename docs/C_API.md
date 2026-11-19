# Gardenia Ambika Core C ABI (v1)

Public header: `include/gardenia_core/gardenia_core.h`

## Ownership

| Concern | Owner |
|---------|-------|
| Voice allocation / steal / mono stack / sustain policy | Host |
| Stage-A synthesis DSP | Gardenia Ambika Core (fixed slots) |
| Host filter / VCA / DC / delay / reverb stages | Host |
| Stage-B fuzz | Gardenia Ambika Core |

## Contracts

- Caller owns all audio and control buffers.
- No allocation, locking, logging, or file I/O in process functions.
- Events must be sorted by `sample_offset` (stable order for ties).
- Poly segmentation: `minimumSubBlockSize=32`, first-event threshold `1`.
- FTZ|DAZ (`0x8040`) is established for each process call and restored on return.
- One instance, one audio thread; create/prepare/destroy must not run concurrently with process.
- ABI v1 supports exactly **1–8** fixed voice slots (`GARDENIA_CORE_MAX_VOICE_SLOTS=8`).

Further constraints:

- [ABI_STABILITY.md](ABI_STABILITY.md) — versioning and struct evolution
- [THREADING.md](THREADING.md) — concurrency rules
- [REALTIME_SAFETY.md](REALTIME_SAFETY.md) — real-time process restrictions

## Stage-B fuzz input

Stage-B processes a caller-supplied pre-fuzz input buffer with an optional
per-sample amount/mix control. Golden tests use the corpus streams
`after-noise-route.f32`, `fuzz-mix.f32`, and `after-fuzz.f32`.

## Versioning

- ABI: `GARDENIA_CORE_ABI_VERSION_*` (major must match).
- Implementation: `gardenia_core_get_implementation_version()`.
- Extensible structs start with `struct_size` + `abi_version`; reserved fields must be zero.

## Shared library

- Windows Release: `build/Release/gardenia-core.dll`
- Windows Debug: `build/Debug/gardenia-core.dll`

Consumers need only the public C header. Internal C++ types (`CoreVoice`, and
related headers) are not part of the ABI.
