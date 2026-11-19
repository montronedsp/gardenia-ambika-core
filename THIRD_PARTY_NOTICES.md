# Third-party notices

## Mutable Instruments Ambika software

Gardenia Ambika Core contains source and resource data adapted from the Ambika
firmware by Emilie Gillet.

- Upstream: https://github.com/pichenettes/ambika
- Reference revision: `2c4a6908f373ea0d4191ef7355929691b87941ef`
- License: GNU General Public License version 3 or later
- Copyright: Copyright 2011 Emilie Gillet

Adapted material includes oscillator algorithms, mixer operators, envelopes,
LFOs, wavetable and vowel/formant tables, and the Ambika distortion lookup
table (`wav_res_distortion`). Upstream notices remain in adapted source and
resource headers where present.

Gardenia Ambika Core is an independent project and is not an official Mutable
Instruments product.

## Cantarino formant synthesis

Ambika includes a variant of the formant synthesis algorithm used in Peter
Knight's Cantarino speech synthesizer. That inheritance is preserved through the
Ambika-derived vowel/formant path in this library. The exact Cantarino revision
used by Ambika is not separately recorded upstream.

## WaveEdit Online wavetable banks

Selected WaveEdit Online wavetable banks are embedded as compile-time sample
data in `src/resources/WaveEditWavetableData.h`.

- Source: https://waveeditonline.com/ (also mirrored at
  https://github.com/smpldsnds/wavedit-online)
- License: CC0 1.0 Universal Public Domain Dedication
- Banks included: ADDITIVE, ANALOG_W, BASIC_WA, BASS_BY_, BBELLS, BELL02,
  BOWED_00, DIGITAL_, DRONE, FMADDI02, HVOICEA, ORGANS01, SINE2SAW, VOICE_A,
  VOXSYNTH, WAVETABL

## MontroneDSP adaptations

Desktop float ports, oversampling adaptations used by the fuzz stage, the
versioned C ABI, and host-facing control-stream mapping introduced by
MontroneDSP are distributed under GPL-3.0-or-later together with this project.
