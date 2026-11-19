/*
  Ambika oscillator mixer / operators + OP_BITS bit-reduction.

  Derived from Ambika voicecard/voice.cc ProcessBlock mix operators, with a
  desktop float adaptation and the FM mix-operator extension.
*/

#pragma once

#include <cmath>
#include <cstdint>
#include "gardenia_core/gardenia_core_math.h"

namespace gardenia_core
{

enum class MixOperator : uint8_t
{
    Sum = 0,
    Sync = 1,
    RingMod = 2,
    Xor = 3,
    Fold = 4,
    Bits = 5,
    Fm = 6 // Gardenia extension
};

static constexpr int kBitCrushBits = 8;
static constexpr int kBitMask = 0xFF;
static constexpr float kXorScale = 128.0f;
static constexpr float kXorOffset = 128.0f;
static constexpr float kFoldScale = 2.0f;
static constexpr float kOscMixScale = 0.5f;

inline float mix_sum (float osc1, float osc2, float osc2Gain, float mixScale = kOscMixScale) noexcept
{
    const float osc1Gain = 1.0f - osc2Gain;
    return (osc1 * osc1Gain + osc2 * osc2Gain) * mixScale;
}

inline float mix_ring (float osc1, float osc2, float osc2Gain, float wetGain, float mixScale = kOscMixScale) noexcept
{
    const float osc1Gain = 1.0f - osc2Gain;
    const float dryGain = 1.0f - wetGain;
    const float mix = (osc1 * osc1Gain + osc2 * osc2Gain) * mixScale;
    const uint8_t osc1Byte = float_to_ambika_byte (osc1);
    const uint8_t osc2Byte = float_to_ambika_byte (osc2);
    const uint8_t ringByte = static_cast<uint8_t> (
        s8s8_mul_shift8 (wrap_byte_to_s8 (static_cast<uint8_t> (osc1Byte + 128u)),
                         wrap_byte_to_s8 (static_cast<uint8_t> (osc2Byte + 128u)))
        + 128);
    const float ring = ambika_byte_to_float (ringByte);
    return mix * dryGain + ring * wetGain;
}

inline float mix_xor (float osc1, float osc2, float osc2Gain, float wetGain, float mixScale = kOscMixScale) noexcept
{
    const float osc1Gain = 1.0f - osc2Gain;
    const float dryGain = 1.0f - wetGain;
    const float mix = (osc1 * osc1Gain + osc2 * osc2Gain) * mixScale;
    const uint8_t xord = static_cast<uint8_t> (float_to_ambika_byte (osc1) ^ float_to_ambika_byte (osc2));
    return mix * dryGain + ambika_byte_to_float (xord) * wetGain;
}

// Ambika FOLD is byte-domain U8Mix(mix, mix+128). Gardenia uses float abs fold + optional OS.
// This function implements the Gardenia float fold kernel (without oversampler state).
inline float mix_fold_kernel (float mix, float wetGain) noexcept
{
    const float dryGain = 1.0f - wetGain;
    const float folded = std::abs (mix) * kFoldScale - 1.0f;
    return mix * dryGain + folded * wetGain;
}

// Gardenia OP_BITS + crush bit-depth extension.
inline float mix_bits_crush (float osc1, float osc2, float osc2Gain, float crushNorm, float mixScale = kOscMixScale) noexcept
{
    const float osc1Gain = 1.0f - osc2Gain;
    const float crush = clamp (crushNorm, 0.0f, 1.0f);
    const int bits = clamp (kBitCrushBits - round_to_int (crush * static_cast<float> (kBitCrushBits - 1)),
                            1, kBitCrushBits);
    const int mask = (kBitMask << (kBitCrushBits - bits)) & kBitMask;
    const float mix = (osc1 * osc1Gain + osc2 * osc2Gain) * mixScale;
    const int quantized = (static_cast<int> (mix * kXorScale + kXorOffset)) & mask;
    return (static_cast<float> (quantized) / kXorScale) - 1.0f;
}

} // namespace gardenia_core
