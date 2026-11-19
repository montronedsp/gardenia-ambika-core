#pragma once

#include "gardenia_core/gardenia_core_math.h"
#include "gardenia_core/Patch.h"
#include <cmath>
#include <cstdint>

namespace gardenia_core
{

// Pure math cutoff control mapping used by Stage-A control streams.
// Produces cutoff Hz values for host-side filter control without embedding a filter.
struct CutoffControlMapping
{
    static constexpr float kCutoffMinHz = 150.0f;
    static constexpr float kCutoffMaxHz = 11000.0f;
    static constexpr float kCutoffMaxExtendedHz = 20000.0f;

    static float cutoffNormToHz (float norm, float extendNorm) noexcept
    {
        const float clampedNorm = jlimit (0.0f, 1.0f, norm);
        const float clampedExtend = jlimit (0.0f, 1.0f, extendNorm);
        const float maxRatio = kCutoffMaxExtendedHz / kCutoffMaxHz;
        const float maxHz = kCutoffMaxHz * std::pow (maxRatio, clampedExtend);
        const float ratio = maxHz / kCutoffMinHz;
        return kCutoffMinHz * std::pow (ratio, clampedNorm);
    }

    static float cutoffByteToHz (uint8_t cutoffByte, uint8_t extendByte) noexcept
    {
        return cutoffNormToHz (cutoffByte / 127.0f, extendByte / 127.0f);
    }

    static float mapPatchCutoffToHz (const Patch& patch) noexcept
    {
        if (patch.filter_type == FILTER_TYPE_ACID303)
            return cutoffByteToHz (patch.filter[0].cutoff, patch.filter_cutoff_extend);

        const float norm = patch.filter[0].cutoff / 127.0f;
        return 20.0f + norm * norm * (20000.0f - 20.0f);
    }
};

} // namespace gardenia_core
