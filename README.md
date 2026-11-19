# Gardenia Ambika Core

**Gardenia Ambika Core** is an open-source synthesis library
based on Mutable Instruments Ambika, adapted and extended
by MontroneDSP.

It is available as an optional synthesis extension for the
Gardenia software synthesizer.

## Features

- Ambika-derived oscillator and waveform synthesis
- Modulation, envelopes and LFOs
- Wavetable and formant synthesis
- Audio-rate synthesis processing
- Lightweight C interface
- Native shared-library implementation

## Building

The project uses CMake and a C++ compiler.

On Windows, Visual Studio 2022 is recommended.

See [BUILDING.md](BUILDING.md) for build instructions.

## Integration

The library exposes a versioned C interface through
`include/gardenia_core/gardenia_core.h`.

Build from source to produce the shared library
(`gardenia-core` on Windows). API contracts are summarised in
[docs/C_API.md](docs/C_API.md).

## License

Distributed under GPL-3.0-or-later.

See [LICENSE](LICENSE),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md),
and [REUSE.toml](REUSE.toml) (REUSE 3.3 / SPDX).

Based on the Ambika firmware by Emilie Gillet.
This is an independent project and is not an official
Mutable Instruments release.

---

**Gardenia Ambika Core — MontroneDSP**
