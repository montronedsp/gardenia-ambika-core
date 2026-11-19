#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace gardenia_core
{

template <typename T>
constexpr T clamp (T value, T low, T high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

// JUCE-compatible argument order: jlimit(lowerLimit, upperLimit, valueToConstrain)
template <typename T>
constexpr T jlimit (T low, T high, T value) noexcept
{
    return clamp (value, low, high);
}

template <typename T>
constexpr T min_val (T a, T b) noexcept
{
    return a < b ? a : b;
}

template <typename T>
constexpr T max_val (T a, T b) noexcept
{
    return a > b ? a : b;
}

inline int round_to_int (double value) noexcept
{
    return value >= 0.0 ? static_cast<int> (value + 0.5) : static_cast<int> (value - 0.5);
}

inline int round_to_int (float value) noexcept
{
    return round_to_int (static_cast<double> (value));
}

template <typename FloatType>
struct MathConstants
{
    static constexpr FloatType pi =
        static_cast<FloatType> (3.141592653589793238462643383279502884);
    static constexpr FloatType twoPi = static_cast<FloatType> (2) * pi;
    static constexpr FloatType halfPi = pi / static_cast<FloatType> (2);
};

// Deterministic LCG matching Gardenia Voice::nextRandomUInt32
inline uint32_t lcg_next (uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return state;
}

inline float lcg_next_float (uint32_t& state) noexcept
{
    return static_cast<float> (lcg_next (state) >> 8) * (1.0f / 16777216.0f);
}

inline uint8_t float_to_ambika_byte (float value) noexcept
{
    const float clamped = clamp (value, -1.0f, 0.9921875f);
    return static_cast<uint8_t> (clamp (round_to_int (clamped * 128.0f + 128.0f), 0, 255));
}

inline float ambika_byte_to_float (uint8_t sample) noexcept
{
    return (static_cast<float> (sample) - 128.0f) * (1.0f / 128.0f);
}

inline int wrap_byte_to_s8 (uint8_t value) noexcept
{
    return value < 128u ? static_cast<int> (value) : static_cast<int> (value) - 256;
}

inline int s8s8_mul_shift8 (int a, int b) noexcept
{
    return (clamp (a, -128, 127) * clamp (b, -128, 127)) >> 8;
}

} // namespace gardenia_core
