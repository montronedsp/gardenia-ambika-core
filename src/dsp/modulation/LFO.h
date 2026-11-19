#pragma once
#include "gardenia_core/gardenia_core_math.h"
#include <cmath>
#include <cstdint>
/*
  LFO.h — Ambika-derived LFO generator for Gardenia Ambika Core.
*/


#include "gardenia_core/Patch.h"

namespace gardenia_core
{

class LFO
{
public:
    LFO()
    {
        phase_ = 0.0;
        phaseIncrement_ = 0.0;
        looped_ = false;
        value_ = 0;
    }

    void init(double sampleRate)
    {
        sampleRate_ = sampleRate;
        // Original control rate is ~975 Hz (39kHz / 40)
        controlRate_ = sampleRate / 40.0;
    }

    void setRandomSeed(uint32_t seed) noexcept
    {
        rngState_ = (seed == 0u) ? 1u : seed;
    }

    float render(uint8_t shape)
    {
        phase_ += phaseIncrement_;
        
        if (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            looped_ = true;
        }
        else
        {
            looped_ = false;
        }

        float value = 0.0f;
        
        switch (shape)
        {
            case LFO_WAVEFORM_RAMP:
                value = (float)phase_;
                break;

            case LFO_WAVEFORM_S_H:
                if (looped_)
                {
                    value_ = static_cast<uint8_t>(nextRandomUInt32() >> 24);
                }
                value = value_ / 255.0f;
                break;

            case LFO_WAVEFORM_TRIANGLE:
                if (phase_ < 0.5)
                {
                    value = (float)(phase_ * 2.0);
                }
                else
                {
                    value = (float)(2.0 - phase_ * 2.0);
                }
                break;

            case LFO_WAVEFORM_SQUARE:
                value = (phase_ < 0.5) ? 1.0f : 0.0f;
                break;

            default:
                // Wavetable LFOs (WAVE_1 to WAVE_16)
                if (shape >= LFO_WAVEFORM_WAVE_1 && shape <= LFO_WAVEFORM_WAVE_16)
                {
                    // For now, use triangle as placeholder
                    // Full implementation would use wavetable lookup
                    if (phase_ < 0.5)
                    {
                        value = (float)(phase_ * 2.0);
                    }
                    else
                    {
                        value = (float)(2.0 - phase_ * 2.0);
                    }
                }
                else
                {
                    value = 0.0f;
                }
                break;
        }

        return value;
    }

    void setPhase(double phase)
    {
        looped_ = (phase <= phase_);
        phase_ = phase;
        if (phase_ >= 1.0) phase_ -= 1.0;
        if (phase_ < 0.0) phase_ += 1.0;
    }

    void setPhaseIncrement(double phaseIncrement)
    {
        phaseIncrement_ = phaseIncrement;
    }

    bool looped() const { return looped_; }

private:
    uint32_t nextRandomUInt32() noexcept
    {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return rngState_;
    }

    double phaseIncrement_;
    double phase_;
    bool looped_;
    uint8_t value_;
    uint32_t rngState_ = 1u;

    double sampleRate_;
    double controlRate_;
};

} // namespace gardenia_core
