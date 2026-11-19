/*
  Ambika-parameterized fuzz / distortion — desktop algorithm.

  Ambika applies a dry/wet waveshape after noise using `wav_res_distortion`.
  This Stage-B path uses 2x linear oversampling with a tanh drive curve for the
  current golden-matching behaviour. The Ambika LUT remains available in
  AmbikaDistortionTable.h for reference.
*/

#pragma once

#include <cmath>
#include "gardenia_core/gardenia_core_math.h"

namespace gardenia_core
{

#if !defined(GARDENIA_CORE_HAS_SIMPLE_OVERSAMPLER)
#define GARDENIA_CORE_HAS_SIMPLE_OVERSAMPLER 1
class SimpleOversampler
{
public:
    SimpleOversampler() : prevSample_ (0.0f) {}

    template <typename ProcessFunction>
    float process (float input, ProcessFunction processFn)
    {
        const float upsampled1 = (prevSample_ + input) * 0.5f;
        const float upsampled2 = input;
        const float processed1 = processFn (upsampled1);
        const float processed2 = processFn (upsampled2);
        const float output = (processed1 + processed2) * 0.5f;
        prevSample_ = input;
        return output;
    }

    void reset() noexcept { prevSample_ = 0.0f; }

private:
    float prevSample_;
};
#endif

class AmbikaFuzzProcessor
{
public:
    static constexpr float kDistortionDrive = 10.0f;
    static constexpr float kActiveThreshold = 0.01f;

    void reset() noexcept { oversampler_.reset(); }

    // Process one sample. fuzzAmount in [0,1] matches Gardenia dst_[MOD_DST_MIX_FUZZ].
    float processSample (float filteredInput, float fuzzAmount) noexcept
    {
        const float fuzzGain = clamp (fuzzAmount, 0.0f, 1.0f);
        const float fuzzDryGain = 1.0f - fuzzGain;

        if (fuzzGain > kActiveThreshold)
        {
            return oversampler_.process (filteredInput, [fuzzGain, fuzzDryGain] (float x)
            {
                const float distorted = std::tanh (x * (1.0f + fuzzGain * kDistortionDrive));
                return x * fuzzDryGain + distorted * fuzzGain;
            });
        }

        return filteredInput;
    }

    void processBlock (const float* filteredInput,
                       const float* fuzzAmount,
                       float* fuzzOutput,
                       uint32_t numSamples) noexcept
    {
        if (filteredInput == nullptr || fuzzOutput == nullptr || numSamples == 0)
            return;

        for (uint32_t i = 0; i < numSamples; ++i)
        {
            const float amount = (fuzzAmount != nullptr) ? fuzzAmount[i] : 0.0f;
            fuzzOutput[i] = processSample (filteredInput[i], amount);
        }
    }

private:
    SimpleOversampler oversampler_;
};

} // namespace gardenia_core
