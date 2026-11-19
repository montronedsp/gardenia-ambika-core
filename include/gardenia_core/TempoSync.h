#pragma once

#include <cmath>
#include <cstdint>

namespace gardenia_core
{

enum TempoDivision : uint8_t
{
    DIV_1_1 = 0,
    DIV_1_2,
    DIV_1_4,
    DIV_1_8,
    DIV_1_16,
    DIV_1_32,
    DIV_1_4T,
    DIV_1_8T,
    DIV_1_16T,
    DIV_1_4D,
    DIV_1_8D,
    DIV_1_16D,
    DIV_LAST
};

static constexpr float kBeatsPerDivision[] = {
    4.0f,
    2.0f,
    1.0f,
    0.5f,
    0.25f,
    0.125f,
    1.0f / 1.5f,
    0.5f / 1.5f,
    0.25f / 1.5f,
    1.5f,
    0.75f,
    0.375f
};

inline float divisionToHz (float bpm, uint8_t divisionIndex) noexcept
{
    if (bpm <= 0.0f || divisionIndex >= DIV_LAST)
        return 0.0f;
    return (bpm / 60.0f) * kBeatsPerDivision[divisionIndex];
}

} // namespace gardenia_core
