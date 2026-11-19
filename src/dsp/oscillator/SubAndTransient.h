#pragma once
#include "gardenia_core/gardenia_core_math.h"
#include "gardenia_core/Patch.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace gardenia_core
{
// Sub-oscillator
class SubOscillator
{
public:
    SubOscillator()
    {
        phase_ = 0.0;
        phaseIncrement_ = 0.0;
    }

    void setIncrement(double increment)
    {
        phaseIncrement_ = increment;
    }
    
    // PolyBLEP for sub-oscillator anti-aliasing
    inline float polyBLEP(double phase, double phaseIncrement)
    {
        if (phaseIncrement == 0.0) return 0.0f;
        
        if (phase < phaseIncrement)
        {
            double t = phase / phaseIncrement;
            return static_cast<float>(t + t - t * t - 1.0);
        }
        else if (phase > 1.0 - phaseIncrement)
        {
            double t = (phase - 1.0) / phaseIncrement;
            return static_cast<float>(t * t + t + t + 1.0);
        }
        
        return 0.0f;
    }

    void render(uint8_t shape, float* buffer, float amount, int numSamples)
    {
        double increment = phaseIncrement_;
        uint8_t baseShape = shape;
        
        // Shapes 3-5 are 2 octaves down versions of 0-2
        if (shape >= WAVEFORM_SUB_OSC_SQUARE_2 && shape <= WAVEFORM_SUB_OSC_PULSE_2)
        {
            increment *= 0.5; // Additional octave down (2 octaves down total from main osc)
            baseShape = shape - 3; // Map to base shapes (0-2)
        }
        else if (shape < WAVEFORM_SUB_OSC_CLICK)
        {
            baseShape = shape; // Shapes 0-2 are 1 octave down from main osc
        }
        else
        {
            // Transient shapes (6+) should not be handled here
            return;
        }

        // After mapping, baseShape is 0 (square), 1 (triangle), or 2 (pulse)
        // Note: pulseWidth is only used for square (0) and pulse (2), not triangle (1)
        float pulseWidth = (baseShape == 0) ? 0.5f : 0.25f; // Square = 0.5, Pulse = 0.25
        float mixGain = 1.0f - amount;

        for (int i = 0; i < numSamples; ++i)
        {
            phase_ += increment;
            if (phase_ >= 1.0) phase_ -= 1.0;

            float v = 0.0f;
            // Check if triangle (baseShape 1)
            if (baseShape == 1)
            {
                // Triangle wave
                if (phase_ < 0.5)
                    v = (float)(phase_ * 4.0 - 1.0);
                else
                    v = (float)(3.0 - phase_ * 4.0);
            }
            else
            {
                // Anti-aliased square or pulse wave using PolyBLEP
                v = (phase_ < pulseWidth) ? -1.0f : 1.0f;
                // Apply PolyBLEP at both transitions
                v += polyBLEP(phase_, increment);
                v -= polyBLEP(fmod(phase_ + (1.0 - pulseWidth), 1.0), increment);
            }

            buffer[i] = buffer[i] * mixGain + v * amount;
        }
    }

private:
    double phase_;
    double phaseIncrement_;
};

// Transient generator
class TransientGenerator
{
public:
    // Constants for transient generation
    static constexpr int kMaxCounter = 255;        // 8-bit counter maximum (full envelope length)
    static constexpr int kClickThreshold = 32;     // Threshold for click pulse generation
    static constexpr int kDecimationRate = 2;      // Decimation rate for blow noise
    static constexpr int kDecimationMod = 17;      // Decimation modulo value
    static constexpr int kBlowGateBit = 0x80;      // Bit 7 for sawtooth envelope direction (128)
    
    TransientGenerator()
    {
        counter_ = 0;
        rngState_ = 1;
        decimate_ = 0;
        gain_ = 0.0f;
    }

    void trigger()
    {
        counter_ = kMaxCounter;
    }

    void render(uint8_t shape, float* buffer, float amount, int numSamples)
    {
        if (shape < WAVEFORM_SUB_OSC_CLICK) return;
        if (shape > WAVEFORM_SUB_OSC_POP) return;

        int samplesToProcess = gardenia_core::min_val(counter_, numSamples);
        if (samplesToProcess <= 0) return;

        for (int i = 0; i < samplesToProcess; ++i)
        {
            float value = 0.0f;
            
            switch (shape)
            {
                case WAVEFORM_SUB_OSC_CLICK:
                    if (counter_ > 0)
                    {
                        // Original: gain_ = counter_; return counter_ < 32 ? 255 : 0;
                        // Convert 8-bit (0-255) to float (-1 to +1): (value - 128) / 128
                        gain_ = counter_ / static_cast<float>(kMaxCounter);
                        value = (counter_ < kClickThreshold) ? 1.0f : 0.0f; // Click pulse
                        --counter_;
                    }
                    else
                    {
                        gain_ = 0.0f;
                        value = 0.0f;
                    }
                    break;
                    
                case WAVEFORM_SUB_OSC_GLITCH:
                    if (counter_ > 0)
                    {
                        // Original: gain_ = counter_; rng_state_ = rng_state_ * 73 + counter_; return rng_state_;
                        gain_ = counter_ / static_cast<float>(kMaxCounter);
                        if (rngState_ == 0) rngState_ = 1;
                        // Simple LCG: rng_state_ * 73 + counter_
                        static constexpr uint16_t kLcgMultiplier = 73;  // Linear Congruential Generator multiplier
                        rngState_ = static_cast<uint16_t>(rngState_ * kLcgMultiplier + counter_);
                        // Convert 16-bit signed to float (-1 to +1)
                        static constexpr float kInt16ToFloat = 1.0f / 32768.0f;
                        value = static_cast<int16_t>(rngState_) * kInt16ToFloat;
                        --counter_;
                    }
                    else
                    {
                        gain_ = 0.0f;
                        value = 0.0f;
                    }
                    break;
                    
                case WAVEFORM_SUB_OSC_BLOW:
                    // Blow: decimated noise with envelope
                    if (counter_ > 0)
                    {
                        static constexpr uint16_t kLcgMultiplier = 73;  // Linear Congruential Generator multiplier
                        static constexpr int kDecimateThreshold = 16;   // Decimation threshold
                        
                        decimate_ += kDecimationRate;
                        if (decimate_ >= kDecimateThreshold)
                        {
                            decimate_ -= kDecimationMod;
                            if (rngState_ == 0) rngState_ = 1;
                            rngState_ = static_cast<uint16_t>(rngState_ * kLcgMultiplier + counter_);
                            
                            if (decimate_ == 0 && counter_ > 0)
                            {
                                --counter_;
                            }
                        }
                        
                        // Sawtooth envelope: up then down at bit 7 (0x80 = 128)
                        gain_ = (counter_ & kBlowGateBit) ? 
                                (kMaxCounter - counter_) / static_cast<float>(kMaxCounter) : 
                                counter_ / static_cast<float>(kMaxCounter);
                        
                        if (rngState_ == 0) rngState_ = 1;
                        // Convert 16-bit signed to float (-1 to +1)
                        static constexpr float kInt16ToFloat = 1.0f / 32768.0f;
                        value = static_cast<int16_t>(rngState_) * kInt16ToFloat;
                    }
                    else
                    {
                        gain_ = 0.0f;
                        value = 0.0f;
                    }
                    break;
                    
                case WAVEFORM_SUB_OSC_METALLIC:
                    if (counter_ > 0)
                    {
                        // Original: gain_ = counter_ >= 64 ? 255 : counter_ << 2; return counter_ * 57;
                        static constexpr int kMetallicThreshold = 64;    // Threshold for full gain
                        static constexpr int kMetallicGainShift = 2;     // Bit shift for gain (multiply by 4)
                        static constexpr int kMetallicMultiplier = 57;   // Harmonic multiplier for metallic sound
                        static constexpr float kUint8ToFloat = 1.0f / 128.0f;  // Convert 8-bit to normalized float
                        
                        gain_ = (counter_ >= kMetallicThreshold) ? 1.0f : 
                                (counter_ << kMetallicGainShift) / static_cast<float>(kMaxCounter);
                        // Multiply and wrap to create harmonic content
                        uint8_t metallicValue = static_cast<uint8_t>(counter_ * kMetallicMultiplier);
                        value = (static_cast<int16_t>(metallicValue) * kUint8ToFloat) - 1.0f;
                        --counter_;
                    }
                    else
                    {
                        gain_ = 0.0f;
                        value = 0.0f;
                    }
                    break;
                    
                case WAVEFORM_SUB_OSC_POP:
                    if (counter_ > 0)
                    {
                        // Original: gain_ = counter_ > 0 ? 255 : 0; return 0;
                        // Pop is just a gain envelope (no signal), used to gate/mute the buffer
                        gain_ = 1.0f;  // Full gain while counter is active
                        value = 0.0f;  // No signal, just envelope
                        --counter_;
                    }
                    else
                    {
                        gain_ = 0.0f;
                        value = 0.0f;
                    }
                    break;
                    
                default:
                    gain_ = 0.0f;
                    value = 0.0f;
                    break;
            }

            float amplitude = gain_ * amount;
            buffer[i] += value * amplitude;
        }
    }

private:
    int counter_;
    uint16_t rngState_;
    int decimate_;
    float gain_;
};


} // namespace gardenia_core
