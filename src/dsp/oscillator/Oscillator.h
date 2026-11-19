/*
  Oscillator.h — Ambika-derived oscillator algorithms for Gardenia Ambika Core.
*/

#pragma once

#include "gardenia_core/gardenia_core_math.h"
#include <algorithm>
#include "gardenia_core/Patch.h"
#include "AmbikaWavetableData.h"
#include "AmbikaVowelData.h"
#include "WaveEditWavetableData.h"
#include <cmath>
#include <cstdint>

namespace gardenia_core
{

// Helper: Convert MIDI note to frequency
inline double midiNoteToFrequency(int midiNote)
{
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

// Helper: Convert frequency to phase increment
inline double frequencyToPhaseIncrement(double frequency, double sampleRate)
{
    return frequency / sampleRate;
}

// PolyBLEP (Polynomial Band-Limited Step) for anti-aliasing
// This removes aliasing from discontinuities in waveforms (saw, square, pulse)
// Based on: Välimäki, V., & Huovilainen, A. (2006). "Antialiasing Oscillators in Subtractive Synthesis"
inline float polyBLEP(double phase, double phaseIncrement)
{
    // Polynomial approximation of integrated step function
    // This corrects the discontinuity at phase wraparound
    
    if (phaseIncrement == 0.0) return 0.0f;  // Safety check
    
    // Transition at phase = 0 (rising edge)
    if (phase < phaseIncrement)
    {
        double t = phase / phaseIncrement;
        // Polynomial: 2t - t² - 1
        return static_cast<float>(t + t - t * t - 1.0);
    }
    // Transition at phase = 1 (falling edge / wraparound)
    else if (phase > 1.0 - phaseIncrement)
    {
        double t = (phase - 1.0) / phaseIncrement;
        // Polynomial: t² + 2t + 1
        return static_cast<float>(t * t + t + t + 1.0);
    }
    
    return 0.0f;
}

// Helper: Convert unsigned parameter (0-127) to signed (-128 to +127)
// This matches the original Ambika behavior where parameters are stored as uint8_t
// but often interpreted as int8_t for bipolar control
inline float parameterToSigned(float parameter)
{
    // Map 0-127 to -128 to +127: parameter 0→-128, 64→0, 127→+127
    // Formula: signedParam = (parameter * 255 / 127) - 128
    float signedParam = (parameter * 255.0f / 127.0f) - 128.0f;
    return gardenia_core::jlimit(-128.0f, 127.0f, signedParam);
}

class Oscillator
{
public:
    // Oscillator algorithm constants
    static constexpr double kPhaseToUint8 = 256.0;        // Phase scaling for 8-bit precision algorithms
    static constexpr double kCzClipThreshold = 32.0;      // Casio CZ-style phase clipping threshold
    static constexpr double kCzMaxPhase = 255.0;          // Maximum phase value for CZ algorithms
    static constexpr float kNoiseFilterMin = 4.0f;        // Minimum filter coefficient for filtered noise
    static constexpr float kNoiseFilterScale = 4.0f;      // Filter coefficient scaling factor
    static constexpr float kNoiseHpThreshold = 64.0f;     // Threshold for high-pass noise mode
    static constexpr int kUint8Range = 255;               // 8-bit value range (0-255)
    static constexpr int kInt8Center = 128;               // 8-bit center value for bipolar signals
    
    Oscillator()
    {
        phase_ = 0.0;
        phaseIncrement_ = 0.0;
        shape_ = WAVEFORM_NONE;
        wavetableModel_ = WT_MODEL_AMBIKA;
        parameter_ = 0.0f;
        fmParameter_ = 0.0f;
        note_ = 60;
        sampleRate_ = 44100.0;
        
        // Initialize state
        secondaryPhase_ = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            formantPhase_[i] = 0.0;
            formantIncrement_[i] = 0.0;
            formantAmplitude_[i] = 0.0f;
            formantPhaseU16_[i] = 0;
            formantIncrementU16_[i] = 0;
            formantAmplitudeByte_[i] = 0;
        }
        noiseModulation_ = 0.0f;
        noiseModulationByte_ = 0;
        vowelUpdate_ = 0;
        lpNoiseSample_ = 0.0f;
        rngState_ = 1;
        noiseResetState_ = 1;
        for (int i = 0; i < 3; ++i)
        {
            quadSawPhase_[i] = 0.0;
        }
        
        // PWM filter state (per-instance, not static)
        pwmFilterState_ = 0.0f;
        pwmWidth_ = 0.5f;
        pwmWidthTarget_ = 0.5f;
        
        // 8-bit parameter smoothing
        bitlandParam_ = 0.0f;
        bitlandParamTarget_ = 0.0f;
    }

    void init(double sampleRate)
    {
        sampleRate_ = sampleRate;
    }

    void setRandomSeed(uint32_t seed) noexcept
    {
        rngState_ = static_cast<uint16_t>((seed == 0u ? 1u : seed) & 0xffffu);
        if (rngState_ == 0)
            rngState_ = 1;
        noiseResetState_ = rngState_;
    }

    void reset()
    {
        noiseResetState_ = nextRandomUInt16();
        rngState_ = noiseResetState_;
    }

    void setParameter(float parameter)
    {
        parameter_ = parameter;
    }

    void setFmParameter(float fmParameter)
    {
        fmParameter_ = fmParameter;
    }

    void render(uint8_t shape, int midiNote, double increment,
                bool* syncInput, bool* syncOutput, float* buffer, int numSamples,
                uint8_t wavetableModel = WT_MODEL_AMBIKA)
    {
        shape_ = shape;
        wavetableModel_ = static_cast<uint8_t> (gardenia_core::jlimit (
            static_cast<int> (WT_MODEL_AMBIKA),
            static_cast<int> (WT_MODEL_LAST - 1),
            static_cast<int> (wavetableModel)));
        note_ = midiNote;
        phaseIncrement_ = increment;

        // Handle sync
        syncInput_ = syncInput;
        syncOutput_ = syncOutput;

        // Render based on shape
        if (shape_ == WAVEFORM_SQUARE && parameter_ == 0.0f)
        {
            renderSimpleWavetable(buffer, numSamples);
        }
        else
        {
            switch (shape_)
            {
                case WAVEFORM_NONE:
                    renderSilence(buffer, numSamples);
                    break;
                case WAVEFORM_SAW:
                case WAVEFORM_TRIANGLE:
                case WAVEFORM_SINE:
                    renderSimpleWavetable(buffer, numSamples);
                    break;
                case WAVEFORM_SQUARE:
                    renderBandlimitedPwm(buffer, numSamples);
                    break;
                case WAVEFORM_CZ_SAW:
                    renderCzSaw(buffer, numSamples);
                    break;
                case WAVEFORM_CZ_SAW_LP:
                case WAVEFORM_CZ_SAW_PK:
                case WAVEFORM_CZ_SAW_BP:
                case WAVEFORM_CZ_SAW_HP:
                    renderCzResoSaw(buffer, numSamples);
                    break;
                case WAVEFORM_CZ_PLS_LP:
                case WAVEFORM_CZ_PLS_PK:
                case WAVEFORM_CZ_PLS_BP:
                case WAVEFORM_CZ_PLS_HP:
                    renderCzResoPulse(buffer, numSamples);
                    break;
                case WAVEFORM_CZ_TRI_LP:
                    renderCzResoTri(buffer, numSamples);
                    break;
                case WAVEFORM_QUAD_SAW_PAD:
                    renderQuadSawPad(buffer, numSamples);
                    break;
                case WAVEFORM_FM:
                    renderFm(buffer, numSamples);
                    break;
                case WAVEFORM_8BITLAND:
                    render8BitLand(buffer, numSamples);
                    break;
                case WAVEFORM_DIRTY_PWM:
                    renderDirtyPwm(buffer, numSamples);
                    break;
                case WAVEFORM_FILTERED_NOISE:
                    renderFilteredNoise(buffer, numSamples);
                    break;
                case WAVEFORM_VOWEL:
                    renderVowel(buffer, numSamples);
                    break;
                case WAVEFORM_WAVEQUENCE:
                    renderWavequence(buffer, numSamples);
                    break;
                default:
                    if (shape_ >= WAVEFORM_WAVETABLE_1 && shape_ <= WAVEFORM_WAVETABLE_16)
                    {
                        if (wavetableModel_ == WT_MODEL_WAVEEDIT_MODERN)
                            renderWaveEditWavetable(buffer, numSamples);
                        else
                            renderInterpolatedWavetable(buffer, numSamples);
                    }
                    else
                    {
                        renderSilence(buffer, numSamples);
                    }
                    break;
            }
        }
    }

    // Render with phase modulation (for FM)
    void renderWithPhaseModulation(uint8_t shape, int midiNote, double increment,
                                   bool* syncInput, bool* syncOutput, 
                                   const float* modulationBuffer, float modulationAmount,
                                   float* buffer, int numSamples)
    {
        shape_ = shape;
        note_ = midiNote;
        phaseIncrement_ = increment;

        // Handle sync
        syncInput_ = syncInput;
        syncOutput_ = syncOutput;

        // For FM, we'll use simple wavetable rendering with phase modulation
        // This works best with basic waveforms
        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            // Apply phase modulation from modulation buffer
            double modulatedPhase = phase_;
            if (modulationBuffer)
            {
                // Scale modulation: modulationAmount controls FM depth
                // modulationBuffer is typically -1 to +1, scale it appropriately
                double phaseOffset = modulationBuffer[i] * modulationAmount;
                modulatedPhase += phaseOffset;
                // Wrap phase
                while (modulatedPhase >= 1.0) modulatedPhase -= 1.0;
                while (modulatedPhase < 0.0) modulatedPhase += 1.0;
            }

            float sample = 0.0f;
            
            // Generate waveform based on shape
            switch (shape_)
            {
                case WAVEFORM_SAW:
                    sample = (float)modulatedPhase * 2.0f - 1.0f;
                    break;
                case WAVEFORM_SQUARE:
                    sample = (modulatedPhase < 0.5) ? 1.0f : -1.0f;
                    break;
                case WAVEFORM_TRIANGLE:
                    if (modulatedPhase < 0.5)
                        sample = (float)(modulatedPhase * 4.0 - 1.0);
                    else
                        sample = (float)(3.0 - modulatedPhase * 4.0);
                    break;
                case WAVEFORM_SINE:
                    sample = static_cast<float>(std::sin(modulatedPhase * gardenia_core::MathConstants<double>::twoPi));
                    break;
                default:
                    // For other waveforms, fall back to sine with modulation
                    sample = static_cast<float>(std::sin(modulatedPhase * gardenia_core::MathConstants<double>::twoPi));
                    break;
            }

            buffer[i] = sample;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

private:
    void renderSilence(float* buffer, int numSamples)
    {
        std::fill(buffer, buffer + (numSamples), 0.0f);
    }

    void renderSimpleWavetable(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            float sample = 0.0f;
            
            switch (shape_)
            {
                case WAVEFORM_SAW:
                    // Anti-aliased sawtooth using PolyBLEP
                    sample = (float)phase_ * 2.0f - 1.0f;
                    sample -= polyBLEP(phase_, phaseIncrement_);  // Remove discontinuity
                    break;
                    
                case WAVEFORM_SQUARE:
                    // Anti-aliased square wave using PolyBLEP
                    sample = (phase_ < 0.5) ? 1.0f : -1.0f;
                    // PolyBLEP at both edges (0 and 0.5)
                    sample += polyBLEP(phase_, phaseIncrement_);
                    sample -= polyBLEP(fmod(phase_ + 0.5, 1.0), phaseIncrement_);
                    break;
                    
                case WAVEFORM_TRIANGLE:
                    // Triangle is naturally band-limited (no sharp discontinuities)
                    if (phase_ < 0.5)
                        sample = (float)(phase_ * 4.0 - 1.0);
                    else
                        sample = (float)(3.0 - phase_ * 4.0);
                    break;
                    
                case WAVEFORM_SINE:
                    // Sine is naturally band-limited
                    sample = static_cast<float>(std::sin(phase_ * gardenia_core::MathConstants<double>::twoPi));
                    break;
                    
                default:
                    sample = 0.0f;
                    break;
            }

            // Apply parameter modulation (waveshaping)
            // Original: sample is 8-bit (0-255), parameter is 0-127
            // Convert sample to 8-bit range for comparison
            static constexpr float kFloatToUint8 = 127.5f;  // Convert -1..+1 to 0..255
            static constexpr float kUint8ToFloat = 1.0f / 127.5f;  // Convert 0..255 to -1..+1
            static constexpr float kParamHalve = 0.5f;  // Parameter halving for waveshaping
            
            float sample8bit = (sample + 1.0f) * kFloatToUint8;
            float param8bit = parameter_;
            
            if (shape_ == WAVEFORM_TRIANGLE)
            {
                // Triangle: if sample < parameter, clamp to parameter (hard clipping)
                if (sample8bit < param8bit)
                {
                    sample = (param8bit * kUint8ToFloat) - 1.0f;
                }
            }
            else
            {
                // Saw/Square/Sine: if sample < parameter, add parameter/2 (phase distortion)
                if (sample8bit < param8bit)
                {
                    float addValue = (param8bit * kParamHalve) * kUint8ToFloat;
                    sample += addValue;
                    sample = gardenia_core::jlimit(-1.0f, 1.0f, sample); // Clamp to prevent overflow
                }
            }

            buffer[i] = sample;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderBandlimitedPwm(float* buffer, int numSamples)
    {
        // PWM anti-aliasing constants
        static constexpr float kPwmSmoothingCoeff = 0.95f;  // Smooth parameter changes
        static constexpr float kPwmFilterCoeff = 0.15f;     // Anti-aliasing filter coefficient
        static constexpr float kPwmMinWidth = 0.1f;         // Minimum pulse width (prevents too-narrow pulses)
        static constexpr float kPwmMaxWidth = 0.9f;         // Maximum pulse width
        static constexpr float kInt8ToParam = 128.0f;       // Int8 center value
        static constexpr float kParamToWidth = 1.0f / 255.0f; // Parameter to width scaling
        
        // Calculate target PWM width from parameter
        // Original Ambika behavior (from manual):
        // - Parameter 0 = 50% pulse width (standard square wave)
        // - Parameter increases = pulse width narrows
        // Map 0-127 to original's interpretation with proper scaling
        float originalParam = kInt8ToParam - parameter_;
        float int8Value = originalParam - kInt8ToParam;
        pwmWidthTarget_ = (int8Value + kInt8ToParam) * kParamToWidth;
        pwmWidthTarget_ = gardenia_core::jlimit(kPwmMinWidth, kPwmMaxWidth, pwmWidthTarget_);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            // Smooth PWM width parameter changes
            pwmWidth_ = pwmWidth_ * kPwmSmoothingCoeff + pwmWidthTarget_ * (1.0f - kPwmSmoothingCoeff);

            // Generate PWM signal with PolyBLEP anti-aliasing
            float sample = (phase_ < pwmWidth_) ? 1.0f : -1.0f;
            // Apply PolyBLEP at both transitions (rising and falling edges)
            sample += polyBLEP(phase_, phaseIncrement_);
            sample -= polyBLEP(fmod(phase_ + (1.0 - pwmWidth_), 1.0), phaseIncrement_);
            
            // Additional one-pole smoothing filter for extra anti-aliasing
            pwmFilterState_ = pwmFilterState_ * (1.0f - kPwmFilterCoeff) + sample * kPwmFilterCoeff;
            buffer[i] = pwmFilterState_;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderCzSaw(float* buffer, int numSamples)
    {
        const uint8_t parameterByte = parameterToByte (parameter_);
        const uint8_t mix = static_cast<uint8_t> (gardenia_core::jlimit (0, 255, static_cast<int> (parameterByte) << 1));

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
                phase_ = 0.0;

            advanceMainPhase();
            const uint8_t phi = static_cast<uint8_t> (phaseToU16 (phase_) >> 8);
            const uint8_t clippedPhi = phi < 0x20 ? static_cast<uint8_t> (phi << 3) : 0xffu;
            const uint16_t czPhase = u8MixU16 (phi, clippedPhi, mix);
            buffer[i] = ambikaByteToFloat (readAmbikaSineByte (czPhase));

            if (syncOutput_) syncOutput_[i] = phaseWrapped_;
        }
    }

    void renderCzResoSaw(float* buffer, int numSamples)
    {
        const uint8_t parameterByte = parameterToByte (parameter_);
        const uint32_t phaseIncrement16 = phaseIncrementToU16();
        const uint16_t czIncrement = static_cast<uint16_t> (
            phaseIncrement16 + ((phaseIncrement16 * static_cast<uint32_t> (parameterByte)) >> 2));
        const uint8_t type = static_cast<uint8_t> (shape_ - WAVEFORM_CZ_SAW_LP);
        uint16_t phase2 = phaseToU16 (secondaryPhase_);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
                phase_ = 0.0;

            advanceMainPhase();
            if (phaseWrapped_)
                phase2 = AmbikaVowelData::kCzPhaseReset[static_cast<std::size_t> (type & 0x03)];

            phase2 = static_cast<uint16_t> (phase2 + czIncrement);
            const uint8_t carrier = readAmbikaSineByte (phase2);
            const uint8_t window = static_cast<uint8_t> (~static_cast<uint8_t> (phaseToU16 (phase_) >> 8));
            const uint8_t sample = (type & 2) != 0
                ? static_cast<uint8_t> (s8u8MulShift8 (wrapByteToS8 (static_cast<uint8_t> (carrier + 128u)), window) + 128)
                : u8u8MulShift8 (carrier, window);
            buffer[i] = ambikaByteToFloat (sample);

            if (syncOutput_) syncOutput_[i] = phaseWrapped_;
        }

        secondaryPhase_ = static_cast<double> (phase2) * (1.0 / 65536.0);
    }

    void renderCzResoPulse(float* buffer, int numSamples)
    {
        const uint8_t parameterByte = parameterToByte (parameter_);
        const uint32_t phaseIncrement16 = phaseIncrementToU16();
        const uint16_t czIncrement = static_cast<uint16_t> (
            phaseIncrement16 + ((phaseIncrement16 * static_cast<uint32_t> (parameterByte)) >> 2));
        const uint8_t type = static_cast<uint8_t> (shape_ - WAVEFORM_CZ_SAW_LP);
        uint16_t phase2 = phaseToU16 (secondaryPhase_);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
                phase_ = 0.0;

            advanceMainPhase();
            if (phaseWrapped_)
                phase2 = AmbikaVowelData::kCzPhaseReset[static_cast<std::size_t> (type & 0x03)];

            phase2 = static_cast<uint16_t> (phase2 + czIncrement);
            uint8_t carrier = readAmbikaSineByte (phase2);
            const uint16_t phase16 = phaseToU16 (phase_);
            uint8_t window = 0;
            if (phase16 < 0x4000)
                window = 255;
            else if (phase16 < 0x8000)
                window = static_cast<uint8_t> (~static_cast<uint8_t> ((phase16 - 0x4000) >> 6));

            if (type == 5)
                carrier = static_cast<uint8_t> ((carrier >> 1) + 128u);

            const uint8_t sample = (type & 2) != 0
                ? static_cast<uint8_t> (s8u8MulShift8 (wrapByteToS8 (static_cast<uint8_t> (carrier + 128u)), window) + 128)
                : u8u8MulShift8 (carrier, window);
            buffer[i] = ambikaByteToFloat (sample);

            if (syncOutput_) syncOutput_[i] = phaseWrapped_;
        }

        secondaryPhase_ = static_cast<double> (phase2) * (1.0 / 65536.0);
    }

    void renderCzResoTri(float* buffer, int numSamples)
    {
        const uint8_t parameterByte = parameterToByte (parameter_);
        const uint32_t phaseIncrement16 = phaseIncrementToU16();
        const uint16_t czIncrement = static_cast<uint16_t> (
            phaseIncrement16 + ((phaseIncrement16 * static_cast<uint32_t> (parameterByte)) >> 2));
        const uint8_t type = static_cast<uint8_t> (shape_ - WAVEFORM_CZ_SAW_LP);
        uint16_t phase2 = phaseToU16 (secondaryPhase_);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
                phase_ = 0.0;

            advanceMainPhase();
            if (phaseWrapped_)
                phase2 = AmbikaVowelData::kCzPhaseReset[static_cast<std::size_t> (type & 0x03)];

            phase2 = static_cast<uint16_t> (phase2 + czIncrement);
            const uint8_t carrier = readAmbikaSineByte (phase2);
            const uint16_t phase16 = phaseToU16 (phase_);
            const uint8_t window = (phase16 & 0x8000) != 0
                ? static_cast<uint8_t> (~static_cast<uint8_t> (phase16 >> 7))
                : static_cast<uint8_t> (phase16 >> 7);
            const uint8_t sample = (type & 2) != 0
                ? static_cast<uint8_t> (s8u8MulShift8 (wrapByteToS8 (static_cast<uint8_t> (carrier + 128u)), window) + 128)
                : u8u8MulShift8 (carrier, window);
            buffer[i] = ambikaByteToFloat (sample);

            if (syncOutput_) syncOutput_[i] = phaseWrapped_;
        }

        secondaryPhase_ = static_cast<double> (phase2) * (1.0 / 65536.0);
    }

    void renderFm(float* buffer, int numSamples)
    {
        // Exact Ambika ratio table, stored as Q8 multipliers. Ambika maps the
        // FM range lane to 24..48, clamps, subtracts 24, then multiplies the
        // oscillator phase increment by lut[index] / 256.
        static constexpr uint16_t fmRatiosQ8[25] = {
              32,    64,   128,   129,   181,   201,   256,   257,
             362,   402,   448,   512,   514,   576,   704,   724,
             768,   804,   886,  1024,  1086,  1206,  1280,  1448,
            2048
        };

        int offset = gardenia_core::round_to_int (fmParameter_);
        if (offset < 24) offset = 0;
        else if (offset > 48) offset = 24;
        else offset -= 24;

        const double multiplier = static_cast<double> (fmRatiosQ8[gardenia_core::jlimit (0, 24, offset)]) * (1.0 / 256.0);
        double modulatorIncrement = phaseIncrement_ * multiplier;
        float fmAmount = parameter_ * 2.0f; // Don't modify parameter_ directly

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
                secondaryPhase_ = 0.0;
            }

            secondaryPhase_ += modulatorIncrement;
            if (secondaryPhase_ >= 1.0) secondaryPhase_ -= 1.0;

            float modulator = std::sin(static_cast<float>(secondaryPhase_) * gardenia_core::MathConstants<float>::twoPi);
            double modulation = modulator * fmAmount / 127.0;

            buffer[i] = static_cast<float>(std::sin((phase_ + modulation) * gardenia_core::MathConstants<double>::twoPi));

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void render8BitLand(float* buffer, int numSamples)
    {
        // Smooth parameter changes to avoid discontinuities when knob moves
        const float paramSmoothingCoeff = 0.95f;
        bitlandParamTarget_ = parameter_; // Original uses parameter_ directly
        
        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            // Smooth parameter changes to prevent clicks
            bitlandParam_ = bitlandParam_ * paramSmoothingCoeff + bitlandParamTarget_ * (1.0f - paramSmoothingCoeff);
            
            // Original: (((phase >> 8) ^ (x << 1)) & (~x)) + (x >> 1)
            int x = (int)bitlandParam_;
            int phaseInt = (int)(phase_ * 256.0);
            int result = ((phaseInt ^ (x << 1)) & (~x)) + (x >> 1);
            buffer[i] = (result / 128.0f) - 1.0f;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderDirtyPwm(float* buffer, int numSamples)
    {
        // Ambika byte-domain behaviour:
        //   output = ((phase.integral >> 8) < (127 + parameter)) ? 0 : 255
        // Parameter is the normal unsigned 0..127 oscillator timbre lane.
        const int threshold = 127 + parameterToByte (parameter_);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            const int phaseByte = gardenia_core::jlimit (0, 255, static_cast<int> (phase_ * 256.0));
            const uint8_t sampleByte = (phaseByte < threshold) ? 0u : 255u;
            buffer[i] = ambikaByteToFloat (sampleByte);

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderQuadSawPad(float* buffer, int numSamples)
    {
        // Ambika uses an unsigned timbre lane for the extra saw increments:
        //   phase_spread = ((phase_increment.integral * parameter) >> 13) + 1
        // Convert Gardenia's normalized phase increment to the same 16-bit
        // integral phase domain, then back to normalized phase increments.
        const int parameterByte = parameterToByte (parameter_);
        const int phaseIncrement16 = gardenia_core::jlimit (
            0,
            65535,
            static_cast<int> (phaseIncrement_ * 65536.0 + 0.5));
        const int phaseSpread16 = ((phaseIncrement16 * parameterByte) >> 13) + 1;
        const double phaseSpread = static_cast<double> (phaseSpread16) * (1.0 / 65536.0);

        double increments[3];
        double currentIncrement = phaseIncrement_;
        for (int i = 0; i < 3; ++i)
        {
            currentIncrement += phaseSpread;
            increments[i] = currentIncrement;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
                for (int j = 0; j < 3; ++j) quadSawPhase_[j] = 0.0;
            }

            float value = (float)phase_ * 2.0f - 1.0f;
            for (int j = 0; j < 3; ++j)
            {
                quadSawPhase_[j] += increments[j];
                if (quadSawPhase_[j] >= 1.0) quadSawPhase_[j] -= 1.0;
                value += (float)(quadSawPhase_[j] * 2.0 - 1.0);
            }
            buffer[i] = value * 0.25f; // Average of 4 saws

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderFilteredNoise(float* buffer, int numSamples)
    {
        if (rngState_ == 0) rngState_ = 1;

        // Ambika keeps this path in byte-domain:
        //   filter_coefficient = parameter << 2, wrapping to uint8_t
        //   if <= 4, force 4
        //   lp = U8Mix(lp, noise, filter_coefficient)
        const uint8_t parameterByte = parameterToByte (parameter_);
        uint8_t filterCoefficient = static_cast<uint8_t> (parameterByte << 2);
        if (filterCoefficient <= 4)
            filterCoefficient = 4;

        uint8_t lpNoiseByte = static_cast<uint8_t> (
            gardenia_core::jlimit (0, 255, gardenia_core::round_to_int (lpNoiseSample_)));
        static constexpr uint16_t kLfsrTaps = 0xb400;

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                rngState_ = noiseResetState_;
                if (rngState_ == 0)
                    rngState_ = 1;
            }

            rngState_ = (rngState_ >> 1) ^ ((-(rngState_ & 1)) & kLfsrTaps);
            const uint8_t noiseByte = static_cast<uint8_t> (rngState_ >> 8);
            lpNoiseByte = ambikaU8Mix (lpNoiseByte, noiseByte, filterCoefficient);

            if (parameterByte >= 64)
            {
                const uint8_t hpByte = static_cast<uint8_t> (
                    static_cast<int> (noiseByte) - static_cast<int> (lpNoiseByte) - 128);
                buffer[i] = ambikaByteToFloat (hpByte);
            }
            else
            {
                buffer[i] = ambikaByteToFloat (lpNoiseByte);
            }
        }

        lpNoiseSample_ = static_cast<float> (lpNoiseByte);
    }

    void renderVowel(float* buffer, int numSamples)
    {
        // Ambika vowel/formant renderer. The timbre lane selects/interpolates
        // nine 7-byte vowel frames: three formant increments, three amplitudes,
        // and one noise/reset modulation amount.
        const uint8_t parameterByte = parameterToByte (parameter_);
        vowelUpdate_ = (vowelUpdate_ + 1) & 3;
        if (vowelUpdate_ == 0 || formantIncrementU16_[0] == 0)
        {
            const int offset1 = gardenia_core::jlimit (
                0,
                AmbikaVowelData::kVowelDataSize - (2 * AmbikaVowelData::kVowelFrameSize),
                static_cast<int> (parameterByte >> 4) * AmbikaVowelData::kVowelFrameSize);
            const int offset2 = offset1 + AmbikaVowelData::kVowelFrameSize;
            const uint8_t balance = static_cast<uint8_t> (parameterByte & 0x0f);

            for (int j = 0; j < 3; ++j)
            {
                const uint8_t a = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset1 + j)];
                const uint8_t b = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset2 + j)];
                formantIncrementU16_[j] = static_cast<uint16_t> (u8u4MixU12 (a, b, balance) << 3);
            }

            for (int j = 0; j < 3; ++j)
            {
                const uint8_t a = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset1 + 3 + j)];
                const uint8_t b = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset2 + 3 + j)];
                formantAmplitudeByte_[j] = u8u4MixU8 (a, b, balance);
            }

            const uint8_t noiseA = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset1 + 6)];
            const uint8_t noiseB = AmbikaVowelData::kVowelData[static_cast<std::size_t> (offset2 + 6)];
            noiseModulationByte_ = u8u4MixU8 (noiseA, noiseB, balance);
        }

        const int phaseIncrement16 = static_cast<int> (phaseIncrementToU16());
        const int randomS8 = wrapByteToS8 (static_cast<uint8_t> (nextRandomUInt16() >> 8));
        const int phaseNoise = s8s8Mul (randomS8, wrapByteToS8 (noiseModulationByte_));

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
                for (int j = 0; j < 3; ++j)
                    formantPhaseU16_[j] = 0;
            }

            int result = 0;

            formantPhaseU16_[0] = static_cast<uint16_t> (formantPhaseU16_[0] + formantIncrementU16_[0]);
            uint8_t phaselet = static_cast<uint8_t> ((formantPhaseU16_[0] >> 8) & 0xf0);
            result = wrapByteToS8 (AmbikaVowelData::kFormantSine[static_cast<std::size_t> (phaselet | (formantAmplitudeByte_[0] & 0x0f))]);

            formantPhaseU16_[1] = static_cast<uint16_t> (formantPhaseU16_[1] + formantIncrementU16_[1]);
            phaselet = static_cast<uint8_t> ((formantPhaseU16_[1] >> 8) & 0xf0);
            result += wrapByteToS8 (AmbikaVowelData::kFormantSine[static_cast<std::size_t> (phaselet | (formantAmplitudeByte_[1] & 0x0f))]);

            formantPhaseU16_[2] = static_cast<uint16_t> (formantPhaseU16_[2] + formantIncrementU16_[2]);
            phaselet = static_cast<uint8_t> ((formantPhaseU16_[2] >> 8) & 0xf0);
            result += wrapByteToS8 (AmbikaVowelData::kFormantSquare[static_cast<std::size_t> (phaselet | (formantAmplitudeByte_[2] & 0x0f))]);

            const uint16_t phase16 = phaseToU16 (phase_);
            result = s8u8MulShift8 (gardenia_core::jlimit (-128, 127, result), static_cast<uint8_t> (phase16 >> 8));

            if ((static_cast<int> (phase16) + phaseNoise) < phaseIncrement16)
            {
                formantPhaseU16_[0] = 0;
                formantPhaseU16_[1] = 0;
                formantPhaseU16_[2] = 0;
            }

            const uint8_t sampleByte = static_cast<uint8_t> (s16ClipS8 (4 * result) + 128);
            buffer[i] = ambikaByteToFloat (sampleByte);

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0)
                phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderInterpolatedWavetable(float* buffer, int numSamples)
    {
        const int wavetableIndex = gardenia_core::jlimit (
            0,
            AmbikaWavetableData::kWavetableCount - 1,
            static_cast<int> (shape_) - static_cast<int> (WAVEFORM_WAVETABLE_1));
        const int definitionOffset = wavetableIndex * AmbikaWavetableData::kWavetableDefinitionSize;
        const int numSteps = gardenia_core::jlimit (
            1,
            AmbikaWavetableData::kWavetableDefinitionSize - 2,
            static_cast<int> (AmbikaWavetableData::kWavetables[static_cast<std::size_t> (definitionOffset)]));

        // Ambika computes an 8:8 wavetable pointer from (parameter << 1) * steps.
        // Gardenia keeps the same musical mapping, using the existing float
        // timbre lane for smoother host/mod-matrix sweeps.
        const float safeParameter = clampFiniteOscParameter (parameter_);
        const float pointer = safeParameter * 2.0f * static_cast<float> (numSteps);
        const int step = gardenia_core::jlimit (0, numSteps - 1, static_cast<int> (pointer * (1.0f / 256.0f)));
        const float blend = gardenia_core::jlimit (0.0f, 1.0f, pointer * (1.0f / 256.0f) - static_cast<float> (step));

        const int waveIndex1 = getWavetableDefinitionWaveIndex (definitionOffset, step);
        const int waveIndex2 = getWavetableDefinitionWaveIndex (definitionOffset, step + 1);

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            const float wave1 = interpolateAmbikaWave (waveIndex1, phase_);
            const float wave2 = interpolateAmbikaWave (waveIndex2, phase_);
            buffer[i] = wave1 + (wave2 - wave1) * blend;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderWaveEditWavetable(float* buffer, int numSamples)
    {
        const int bankIndex = gardenia_core::jlimit (
            0,
            WaveEditWavetableData::kBankCount - 1,
            static_cast<int> (shape_) - static_cast<int> (WAVEFORM_WAVETABLE_1));

        const float safeParameter = clampFiniteOscParameter (parameter_);
        const float framePosition = safeParameter
            * static_cast<float> (WaveEditWavetableData::kFramesPerBank - 1)
            / 127.0f;
        const int frame1 = gardenia_core::jlimit (
            0,
            WaveEditWavetableData::kFramesPerBank - 1,
            static_cast<int> (framePosition));
        const int frame2 = gardenia_core::min_val (frame1 + 1, WaveEditWavetableData::kFramesPerBank - 1);
        const float frameBlend = gardenia_core::jlimit (0.0f, 1.0f, framePosition - static_cast<float> (frame1));

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            const float wave1 = interpolateWaveEditFrame (bankIndex, frame1, phase_);
            const float wave2 = interpolateWaveEditFrame (bankIndex, frame2, phase_);
            buffer[i] = wave1 + (wave2 - wave1) * frameBlend;

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    void renderWavequence(float* buffer, int numSamples)
    {
        // Ambika wavequence reads directly from the shared wave bank. Gardenia's
        // existing oscillator parameter lane is 0..127, so map that full range
        // across the valid 80-wave Ambika bank and clamp defensively.
        const float safeParameter = clampFiniteOscParameter (parameter_);
        const int waveIndex = gardenia_core::jlimit (
            0,
            AmbikaWavetableData::kWaveCount - 1,
            gardenia_core::round_to_int (safeParameter * static_cast<float> (AmbikaWavetableData::kWaveCount - 1) / 127.0f));

        for (int i = 0; i < numSamples; ++i)
        {
            if (syncInput_ && syncInput_[i])
            {
                phase_ = 0.0;
            }

            buffer[i] = interpolateAmbikaWave (waveIndex, phase_);

            phase_ += phaseIncrement_;
            if (phase_ >= 1.0) phase_ -= 1.0;

            if (syncOutput_) syncOutput_[i] = (phase_ < phaseIncrement_);
        }
    }

    static float clampFiniteOscParameter(float parameter) noexcept
    {
        return std::isfinite (parameter) ? gardenia_core::jlimit (0.0f, 127.0f, parameter) : 0.0f;
    }

    static uint8_t parameterToByte(float parameter) noexcept
    {
        return static_cast<uint8_t> (gardenia_core::jlimit (0, 127, static_cast<int> (clampFiniteOscParameter (parameter))));
    }

    static uint8_t ambikaU8Mix(uint8_t a, uint8_t b, uint8_t balance) noexcept
    {
        return static_cast<uint8_t> (
            static_cast<int> (a)
            + (((static_cast<int> (b) - static_cast<int> (a)) * static_cast<int> (balance)) >> 8));
    }

    static int wrapByteToS8(uint8_t value) noexcept
    {
        return value < 128u ? static_cast<int> (value) : static_cast<int> (value) - 256;
    }

    static uint8_t u8u8MulShift8(uint8_t a, uint8_t b) noexcept
    {
        return static_cast<uint8_t> ((static_cast<int> (a) * static_cast<int> (b)) >> 8);
    }

    static int s8u8MulShift8(int signedValue, uint8_t gain) noexcept
    {
        return (gardenia_core::jlimit (-128, 127, signedValue) * static_cast<int> (gain)) >> 8;
    }

    static int s8s8Mul(int a, int b) noexcept
    {
        return gardenia_core::jlimit (-128, 127, a) * gardenia_core::jlimit (-128, 127, b);
    }

    static int s16ClipS8(int value) noexcept
    {
        return gardenia_core::jlimit (-128, 127, value);
    }

    static uint16_t u8MixU16(uint8_t a, uint8_t b, uint8_t balance) noexcept
    {
        return static_cast<uint16_t> (
            (static_cast<uint16_t> (a) << 8)
            + (static_cast<int> (b) - static_cast<int> (a)) * static_cast<int> (balance));
    }

    static uint16_t u8u4MixU12(uint8_t a, uint8_t b, uint8_t balance) noexcept
    {
        return static_cast<uint16_t> (
            (static_cast<uint16_t> (a) << 4)
            + (static_cast<int> (b) - static_cast<int> (a)) * static_cast<int> (balance & 0x0f));
    }

    static uint8_t u8u4MixU8(uint8_t a, uint8_t b, uint8_t balance) noexcept
    {
        return static_cast<uint8_t> (
            static_cast<int> (a)
            + (((static_cast<int> (b) - static_cast<int> (a)) * static_cast<int> (balance & 0x0f)) >> 4));
    }

    static uint8_t readAmbikaSineByte(uint16_t phase) noexcept
    {
        return AmbikaVowelData::kSine[static_cast<std::size_t> (phase >> 8)];
    }

    static uint16_t phaseToU16(double phase) noexcept
    {
        const double wrapped = std::isfinite (phase) ? phase - std::floor (phase) : 0.0;
        const double positive = wrapped < 0.0 ? wrapped + 1.0 : wrapped;
        return static_cast<uint16_t> (gardenia_core::jlimit (0, 65535, static_cast<int> (positive * 65536.0)));
    }

    uint32_t phaseIncrementToU16() const noexcept
    {
        return static_cast<uint32_t> (gardenia_core::jlimit (0, 65535, static_cast<int> (phaseIncrement_ * 65536.0 + 0.5)));
    }

    void advanceMainPhase() noexcept
    {
        phase_ += phaseIncrement_;
        phaseWrapped_ = false;
        if (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            phaseWrapped_ = true;
        }
    }

    static int getWavetableDefinitionWaveIndex(int definitionOffset, int step) noexcept
    {
        const int clampedStep = gardenia_core::jlimit (0, AmbikaWavetableData::kWavetableDefinitionSize - 2, step);
        const int definitionIndex = definitionOffset + 1 + clampedStep;
        return gardenia_core::jlimit (
            0,
            AmbikaWavetableData::kWaveCount - 1,
            static_cast<int> (AmbikaWavetableData::kWavetables[static_cast<std::size_t> (definitionIndex)]));
    }

    static float ambikaByteToFloat(std::uint8_t sample) noexcept
    {
        // Ambika's oscillator domain is unsigned 8-bit with 128 as the
        // neutral/silence centre. Preserve that centre exactly to avoid a
        // small DC bias when rendering the Ambika wave bank as float audio.
        return (static_cast<float> (sample) - 128.0f) * (1.0f / 128.0f);
    }

    static float interpolateAmbikaWave(int waveIndex, double phase) noexcept
    {
        const int clampedWaveIndex = gardenia_core::jlimit (0, AmbikaWavetableData::kWaveCount - 1, waveIndex);
        const double wrappedPhase = std::isfinite (phase) ? phase - std::floor (phase) : 0.0;
        const double positivePhase = wrappedPhase < 0.0 ? wrappedPhase + 1.0 : wrappedPhase;
        const double position = positivePhase * static_cast<double> (AmbikaWavetableData::kWaveCycleLength);
        const int sampleIndex = gardenia_core::jlimit (
            0,
            AmbikaWavetableData::kWaveCycleLength - 1,
            static_cast<int> (position));
        const float fraction = static_cast<float> (position - static_cast<double> (sampleIndex));
        const int waveOffset = clampedWaveIndex * AmbikaWavetableData::kWaveSampleCount;
        const auto a = ambikaByteToFloat (
            AmbikaWavetableData::kWaves[static_cast<std::size_t> (waveOffset + sampleIndex)]);
        const auto b = ambikaByteToFloat (
            AmbikaWavetableData::kWaves[static_cast<std::size_t> (waveOffset + sampleIndex + 1)]);

        return a + (b - a) * gardenia_core::jlimit (0.0f, 1.0f, fraction);
    }

    static float waveEditInt16ToFloat(std::int16_t sample) noexcept
    {
        return gardenia_core::jlimit (-1.0f, 1.0f, static_cast<float> (sample) * (1.0f / 32768.0f));
    }

    static float interpolateWaveEditFrame(int bankIndex, int frameIndex, double phase) noexcept
    {
        const int clampedBank = gardenia_core::jlimit (0, WaveEditWavetableData::kBankCount - 1, bankIndex);
        const int clampedFrame = gardenia_core::jlimit (0, WaveEditWavetableData::kFramesPerBank - 1, frameIndex);
        const double wrappedPhase = std::isfinite (phase) ? phase - std::floor (phase) : 0.0;
        const double positivePhase = wrappedPhase < 0.0 ? wrappedPhase + 1.0 : wrappedPhase;
        const double position = positivePhase * static_cast<double> (WaveEditWavetableData::kSamplesPerFrame);
        const int sampleIndex = gardenia_core::jlimit (
            0,
            WaveEditWavetableData::kSamplesPerFrame - 1,
            static_cast<int> (position));
        const int nextSampleIndex = (sampleIndex + 1) & (WaveEditWavetableData::kSamplesPerFrame - 1);
        const float fraction = static_cast<float> (position - static_cast<double> (sampleIndex));
        const int frameOffset = clampedBank * WaveEditWavetableData::kSamplesPerBank
                              + clampedFrame * WaveEditWavetableData::kSamplesPerFrame;
        const auto a = waveEditInt16ToFloat (
            WaveEditWavetableData::kSamples[static_cast<std::size_t> (frameOffset + sampleIndex)]);
        const auto b = waveEditInt16ToFloat (
            WaveEditWavetableData::kSamples[static_cast<std::size_t> (frameOffset + nextSampleIndex)]);

        return a + (b - a) * gardenia_core::jlimit (0.0f, 1.0f, fraction);
    }

    double phase_;
    double phaseIncrement_;
    bool phaseWrapped_ = false;
    uint8_t shape_;
    uint8_t wavetableModel_;
    float parameter_;
    float fmParameter_;
    int note_;
    double sampleRate_;

    // State for various algorithms
    double secondaryPhase_;
    double formantPhase_[3];
    double formantIncrement_[3];
    float formantAmplitude_[3];
    uint16_t formantPhaseU16_[3];
    uint16_t formantIncrementU16_[3];
    uint8_t formantAmplitudeByte_[3];
    float noiseModulation_;
    uint8_t noiseModulationByte_;
    int vowelUpdate_;
    float lpNoiseSample_;
    uint16_t rngState_;
    uint16_t noiseResetState_;
    double quadSawPhase_[3];

    bool* syncInput_;
    bool* syncOutput_;
    
    // PWM filter state (per-instance)
    float pwmFilterState_;
    float pwmWidth_;
    float pwmWidthTarget_;
    
    // 8-bit parameter smoothing
    float bitlandParam_;
    float bitlandParamTarget_;

    uint16_t nextRandomUInt16() noexcept
    {
        rngState_ = static_cast<uint16_t>(rngState_ * 109u + 1021u);
        if (rngState_ == 0)
            rngState_ = 1;
        return rngState_;
    }
};

} // namespace gardenia_core
