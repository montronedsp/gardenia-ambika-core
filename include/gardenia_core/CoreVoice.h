/*
  CoreVoice Stage A — JUCE-free Ambika-derived synthesis path.

  Renders oscillator/mixer/sub/transient/noise stems and modulation control
  streams. Host filter, VCA, delay, reverb, and Stage-B fuzz are outside this
  class; Stage-B fuzz is provided separately by AmbikaFuzzProcessor.
*/
#pragma once

#include "gardenia_core/gardenia_core_math.h"
#include "gardenia_core/gardenia_core_types.h"
#include "gardenia_core/Patch.h"
#include "gardenia_core/CutoffControlMapping.h"
#include "gardenia_core/TempoSync.h"
#include "Oscillator.h"
#include "Envelope.h"
#include "LFO.h"
#include "SubAndTransient.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gardenia_core
{

struct StageABuffers
{
    float* stem = nullptr;
    float* noise = nullptr;
    float* filterEnv = nullptr;
    float* lfo1 = nullptr;
    float* lfo2 = nullptr;
    float* lfo3 = nullptr;
    float* voiceLfo = nullptr;
    float* cutoffMod = nullptr;
    float* cutoffHz = nullptr;
    float* resonance = nullptr;
    float* signalGain = nullptr;
    float* noiseGain = nullptr;
    float* fuzzMix = nullptr;
    float* vca = nullptr;
    float* pitch = nullptr;
    uint8_t* active = nullptr;
    uint8_t* gate = nullptr;
    uint32_t capacity = 0;
    uint32_t writeOffset = 0;
};

#if !defined(GARDENIA_CORE_HAS_SIMPLE_OVERSAMPLER)
#define GARDENIA_CORE_HAS_SIMPLE_OVERSAMPLER 1
class SimpleOversampler
{
public:
    SimpleOversampler() : prevSample_(0.0f) {}
    
    // Process one sample with 2x oversampling
    // processFn should be: float processFn(float input)
    template<typename ProcessFunction>
    float process(float input, ProcessFunction processFn)
    {
        // Upsample: interpolate between previous and current sample
        float upsampled1 = (prevSample_ + input) * 0.5f;  // Midpoint
        float upsampled2 = input;
        
        // Process both samples at 2x rate
        float processed1 = processFn(upsampled1);
        float processed2 = processFn(upsampled2);
        
        // Downsample: average the two processed samples
        float output = (processed1 + processed2) * 0.5f;
        
        prevSample_ = input;
        return output;
    }
    
    void reset() { prevSample_ = 0.0f; }
    
private:
    float prevSample_;
};
#endif

class CoreVoice
{
public:
    static constexpr int kMaxProcessBlockSamples = 2048;

    // Audio processing constants
    static constexpr float kDcBlockerCoeff = 0.999f;  // DC blocker coefficient (high-pass ~1.1 Hz at 44.1kHz) - optimized for deep bass
    static constexpr float kOutputScale = 0.5f;       // Output scaling to prevent clipping
    static constexpr float kSoftClipThreshold = 0.9f; // Soft clipping threshold
    static constexpr float kSoftClipGain = 0.1f;      // Soft clipping gain for overdrive region
    static constexpr float kSoftClipSharpness = 10.0f; // Tanh sharpness for soft clipping
    static constexpr float kFilterSmoothingCoeff = 0.1f; // Filter parameter smoothing to prevent zipper noise
    static constexpr float kDistortionDrive = 10.0f;   // Distortion drive amount for fuzz effect
    
    CoreVoice()
    {
        sampleRate_ = 44100.0;
        
        osc1_.init(sampleRate_);
        osc2_.init(sampleRate_);
        envelope_.init(sampleRate_);
        filterEnvelope_.init(sampleRate_);
        auxEnvelope1_.init(sampleRate_);
        auxEnvelope2_.init(sampleRate_);
        
        // Initialize LFOs
        lfo1_.init(sampleRate_);
        lfo2_.init(sampleRate_);
        lfo3_.init(sampleRate_);
        voiceLfo_.init(sampleRate_);
        
        gate_ = 0.0f;
        pitchValue_ = 0.0;
        pitchTarget_ = 0.0;
        pitchIncrement_ = 0.0;
        velocity_ = 0.0f;
        
        // Initialize LFO phases
        for (int i = 0; i < 4; ++i)
        {
            lfoPhases_[i] = 0.0f;
        }
        
        // Initialize DC blocker
        dcBlockerX1_ = 0.0f;
        dcBlockerY1_ = 0.0f;
        
        // Pre-allocate audio buffers to fixed render chunk size.
        // This avoids allocations in the audio thread
        const size_t maxBufferSize = static_cast<size_t>(kMaxProcessBlockSamples);
        osc1Buffer_.resize(maxBufferSize, 0.0f);
        osc2Buffer_.resize(maxBufferSize, 0.0f);
        syncState_.resize(maxBufferSize, 0);
        noSync_.resize(maxBufferSize, 0);
        envValues_.resize(maxBufferSize, 0.0f);
        filterEnvValues_.resize(maxBufferSize, 0.0f);
        auxEnvValues1_.resize(maxBufferSize, 0.0f);
        auxEnvValues2_.resize(maxBufferSize, 0.0f);
        
        resetAllControllers();
    }

    void init(double sampleRate)
    {
        sampleRate_ = sampleRate;
        
        osc1_.init(sampleRate);
        osc2_.init(sampleRate);
        envelope_.init(sampleRate);
        filterEnvelope_.init(sampleRate);
        auxEnvelope1_.init(sampleRate);
        auxEnvelope2_.init(sampleRate);
        
        // Initialize LFOs
        lfo1_.init(sampleRate);
        lfo2_.init(sampleRate);
        lfo3_.init(sampleRate);
        voiceLfo_.init(sampleRate);
        
        // Reset DC blocker
        dcBlockerX1_ = 0.0f;
        dcBlockerY1_ = 0.0f;

        // Deterministic reset of retained voice state for Stage-A equivalence.
        gate_ = 0.0f;
        velocity_ = 0.0f;
        pitchValue_ = 0.0;
        pitchTarget_ = 0.0;
        pitchIncrement_ = 0.0;
        smoothedFilterCutoff_ = 1000.0f;
        foldingOversampler_.reset();
        for (int i = 0; i < 4; ++i)
            lfoPhases_[i] = 0.0f;
        resetAllControllers();
        stickyFxTail_ = false;

        
        // Scratch buffers are fixed-capacity and prepared in the constructor.
    }

    void setRandomSeed(uint32_t seed) noexcept
    {
        rngState_ = (seed == 0u) ? 1u : seed;
        osc1_.setRandomSeed(nextRandomUInt32());
        osc2_.setRandomSeed(nextRandomUInt32());
        lfo1_.setRandomSeed(nextRandomUInt32());
        lfo2_.setRandomSeed(nextRandomUInt32());
        lfo3_.setRandomSeed(nextRandomUInt32());
        voiceLfo_.setRandomSeed(nextRandomUInt32());
        // Consume two draws to match monolith Voice seeding of filter_ + delay_
        // (private FX are not present in CoreVoice; sequence parity is required).
        (void) nextRandomUInt32();
        (void) nextRandomUInt32();
    }


    void resetAllControllers()
    {
        modulationSources_[MOD_SRC_PITCH_BEND] = 0.5f;
        modulationSources_[MOD_SRC_AFTERTOUCH] = 0.0f;
        modulationSources_[MOD_SRC_WHEEL] = 0.0f;
        modulationSources_[MOD_SRC_WHEEL_2] = 0.0f;
        modulationSources_[MOD_SRC_EXPRESSION] = 0.0f;
        modulationSources_[MOD_SRC_CONSTANT_4] = 4.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_8] = 8.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_16] = 16.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_32] = 32.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_64] = 64.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_128] = 128.0f / 255.0f;
        modulationSources_[MOD_SRC_CONSTANT_256] = 1.0f;
    }

    void trigger(int midiNote, float velocity, bool legato)
    {
        velocity_ = velocity;
        pitchTarget_ = midiNote * 128.0;
        
        bool wasGateOff = (gate_ == 0.0f);

        
        if (!part_.legato || !legato)
        {
            gate_ = 1.0f;
            
            // Trigger envelopes with attack
            envelope_.trigger(ATTACK);
            filterEnvelope_.trigger(ATTACK);
            auxEnvelope1_.trigger(ATTACK);
            auxEnvelope2_.trigger(ATTACK);
            
            // Reset DC blocker on new note (prevents DC offset accumulation)
            dcBlockerX1_ = 0.0f;
            dcBlockerY1_ = 0.0f;
            
            // Reset oversampler state
            foldingOversampler_.reset();
            
            transientGenerator_.trigger();
            if (!delayBypass_ || !reverbBypass_)
                stickyFxTail_ = true;
            modulationSources_[MOD_SRC_VELOCITY] = velocity;
            modulationSources_[MOD_SRC_RANDOM] = nextRandomFloat();
            osc2_.reset();
            
            // Handle LFO retriggering based on mode
            // Patch LFO 1
            if (getPatchForRead().env_lfo[0].retrigger_mode == LFO_RETRIGGER_ON_NOTE)
            {
                lfo1_.setPhase(0.0);
            }
            else if (getPatchForRead().env_lfo[0].retrigger_mode == LFO_RETRIGGER_ON_GATE && wasGateOff)
            {
                lfo1_.setPhase(0.0);
            }
            
            // Patch LFO 2
            if (getPatchForRead().env_lfo[1].retrigger_mode == LFO_RETRIGGER_ON_NOTE)
            {
                lfo2_.setPhase(0.0);
            }
            else if (getPatchForRead().env_lfo[1].retrigger_mode == LFO_RETRIGGER_ON_GATE && wasGateOff)
            {
                lfo2_.setPhase(0.0);
            }
            
            // Patch LFO 3
            if (getPatchForRead().env_lfo[2].retrigger_mode == LFO_RETRIGGER_ON_NOTE)
            {
                lfo3_.setPhase(0.0);
            }
            else if (getPatchForRead().env_lfo[2].retrigger_mode == LFO_RETRIGGER_ON_GATE && wasGateOff)
            {
                lfo3_.setPhase(0.0);
            }
            
            // Voice LFO (uses env_lfo[0] retrigger mode as shown in example)
            if (getPatchForRead().env_lfo[0].retrigger_mode == LFO_RETRIGGER_ON_NOTE)
            {
                voiceLfo_.setPhase(0.0);
            }
            else if (getPatchForRead().env_lfo[0].retrigger_mode == LFO_RETRIGGER_ON_GATE && wasGateOff)
            {
                voiceLfo_.setPhase(0.0);
            }
        }
        
        // Match Ambika phrase semantics: a fresh monophonic phrase starts on pitch,
        // while true legato transitions glide from the previous note.
        if (pitchValue_ == 0.0 || (part_.legato != 0 && !legato))
        {
            pitchValue_ = pitchTarget_;
            pitchIncrement_ = 0.0; // No portamento needed for first note
        }
        else
        {
            // Calculate portamento increment for glide between notes
            // Portamento works in both poly and mono modes
            double delta = pitchTarget_ - pitchValue_;
            double portamentoTime = part_.portamento_time / 127.0;
            
            if (portamentoTime > 0.0)
            {
                // Exponential portamento curve for smooth glide
                double portamentoIncrement = 1.0 / (1.0 + portamentoTime * 100.0);
                pitchIncrement_ = delta * portamentoIncrement;
                
                // Ensure minimum increment for smooth movement
                if (std::abs(pitchIncrement_) < 0.1)
                {
                    pitchIncrement_ = (delta < 0) ? -0.1 : 0.1;
                }
            }
            else
            {
                // No portamento - instant pitch change
                pitchValue_ = pitchTarget_;
                pitchIncrement_ = 0.0;
            }
        }
    }

    void release()
    {
        gate_ = 0.0f;
        // Trigger envelope releases
        envelope_.trigger(RELEASE);
        filterEnvelope_.trigger(RELEASE);
        auxEnvelope1_.trigger(RELEASE);
        auxEnvelope2_.trigger(RELEASE);
    }

    void kill()
    {
        gate_ = 0.0f;
        envelope_.trigger(DEAD);
        filterEnvelope_.trigger(DEAD);
        auxEnvelope1_.trigger(DEAD);
        auxEnvelope2_.trigger(DEAD);
        stickyFxTail_ = false;
        
        // Reset DC blocker when voice is killed (prevents DC offset accumulation)
        dcBlockerX1_ = 0.0f;
        dcBlockerY1_ = 0.0f;
    }

    void setPatch(const Patch& patch)
    {
        // Write to inactive buffer, then swap (lock-free for audio thread reads)
        const int writeIdx = 1 - patchReadIndex_.load(std::memory_order_acquire);
        patchBuffer_[writeIdx] = patch;
        patchReadIndex_.store(writeIdx, std::memory_order_release);

        // Update envelope parameters from patch
        envelope_.update(
            patch.env_lfo[0].attack,
            patch.env_lfo[0].decay,
            patch.env_lfo[0].sustain,
            patch.env_lfo[0].release
        );
        
        // Update filter envelope parameters from patch
        filterEnvelope_.update(
            patch.env_lfo[1].attack,
            patch.env_lfo[1].decay,
            patch.env_lfo[1].sustain,
            patch.env_lfo[1].release
        );
        
        // Update auxiliary envelope 1 parameters from patch
        auxEnvelope1_.update(
            patch.env_lfo[2].attack,
            patch.env_lfo[2].decay,
            patch.env_lfo[2].sustain,
            patch.env_lfo[2].release
        );
        
        // Update auxiliary envelope 2 parameters from patch
        auxEnvelope2_.update(
            patch.env_lfo[3].attack,
            patch.env_lfo[3].decay,
            patch.env_lfo[3].sustain,
            patch.env_lfo[3].release
        );
        
        // Initialize filter cutoff control base (filter apply remains host-side).
        const float filterCutoff = CutoffControlMapping::mapPatchCutoffToHz(patch);
        (void) filterCutoff;
        float filterResonance = patch.filter[0].resonance / 127.0f;
        filterResonance = gardenia_core::jlimit(0.0f, 1.0f, filterResonance);
        (void) filterResonance;
    }

    void setPart(const Part& part)
    {
        part_ = part;
    }

    const Part& getPart() const
    {
        return part_;
    }

    void setPitchBendNormalized(float value) noexcept
    {
        modulationSources_[MOD_SRC_PITCH_BEND] = gardenia_core::jlimit(0.0f, 1.0f, value);
    }

    void setAftertouchNormalized(float value) noexcept
    {
        modulationSources_[MOD_SRC_AFTERTOUCH] = gardenia_core::jlimit(0.0f, 1.0f, value);
    }

    void setModWheelNormalized(float value) noexcept
    {
        modulationSources_[MOD_SRC_WHEEL] = gardenia_core::jlimit(0.0f, 1.0f, value);
    }

    void setWheel2Normalized(float value) noexcept
    {
        modulationSources_[MOD_SRC_WHEEL_2] = gardenia_core::jlimit(0.0f, 1.0f, value);
    }

    void setExpressionNormalized(float value) noexcept
    {
        modulationSources_[MOD_SRC_EXPRESSION] = gardenia_core::jlimit(0.0f, 1.0f, value);
    }

    void setFilterBypass(bool bypass) noexcept
    {
        filterBypass_ = bypass;
    }

    bool getFilterBypass() const
    {
        return filterBypass_;
    }
    // Deprecated unused Analog/90s getters were empty non-void stubs and are removed.


    void setFilterEnvMultiplier(int multiplier)
    {
        filterEnvMultiplier_ = multiplier; // 0 = x1, 1 = x2, 2 = x4
    }

    int getFilterEnvMultiplier() const
    {
        return filterEnvMultiplier_;
    }

    void setSubOscSolo(bool solo)
    {
        subOscSolo_ = solo;
    }

    void setWavetableModel(uint8_t model) noexcept
    {
        wavetableModel_ = static_cast<uint8_t> (gardenia_core::jlimit (
            static_cast<int> (WT_MODEL_AMBIKA),
            static_cast<int> (WT_MODEL_LAST - 1),
            static_cast<int> (model)));
    }

    bool getSubOscSolo() const
    {
        return subOscSolo_;
    }
    
    // Delay control methods
    

    
    
    
    
    
    
    
    
    
    
    // Reverb control methods
    
    
    
    
    
    

    void setTempoBpm(float bpm) noexcept { tempoBpm_ = bpm; }

    void setStageABuffers(StageABuffers* buffers) noexcept { stageABuffers_ = buffers; }

    void setDelayBypass(bool bypass) noexcept { delayBypass_ = bypass; }
    void setReverbBypass(bool bypass) noexcept { reverbBypass_ = bypass; }


    void processBlock(float* outputBuffer, int numSamples)
    {
        if (outputBuffer == nullptr || numSamples <= 0)
            return;

        if (numSamples > kMaxProcessBlockSamples)
        {
            int processed = 0;
            while (processed < numSamples)
            {
                const int chunkSamples = gardenia_core::min_val(kMaxProcessBlockSamples, numSamples - processed);
                processBlock(outputBuffer + processed, chunkSamples);
                processed += chunkSamples;
            }
            return;
        }

        loadSources();
        processModulationMatrix();
        updateDestinations();
        renderBlock(outputBuffer, numSamples);
    }

    // Test-only: advance block-rate sources without rendering audio (LFO/RNG/env tick).
    void tickBlockSourcesForTest()
    {
        loadSources();
        processModulationMatrix();
        updateDestinations();
    }

    float getVCA()
    {
        return envelope_.render();
    }

    bool isActive() const
    {
        // Voice is active if envelope is not dead
        return envelope_.stage() != DEAD;
    }
    
    // Check if voice has any tail remaining (delay or reverb)
    bool hasTail() const
    {
        // Private delay/reverb are host-side. A sticky keep-alive flag is retained
        // for future host-integration handshakes (see PROVENANCE), but must not
        // extend Stage-A rendering after synthesis ends — that diverges dry-FX
        // goldens (e.g. env-min zeros after envelope DEAD while monolith
        // delay_/reverb_.hasTail() is already false).
        if (delayBypass_ && reverbBypass_)
            return false;
        (void) stickyFxTail_;
        return false;
    }

private:
    void loadSources()
    {
        const auto& p = getPatchForRead();
        float bpm = tempoBpm_;

        // LFO rate: sync mode uses tempo division, free mode uses patch rate (Ambika formula)
        auto getLfoRateHz = [&p, bpm](int lfoIdx) -> double {
            if (lfoIdx < 4 && p.env_lfo[lfoIdx].sync_enabled)
                return static_cast<double>(divisionToHz(bpm, p.env_lfo[lfoIdx].sync_division));
            return std::pow(2.0, (p.env_lfo[lfoIdx].rate - 64.0) / 16.0);
        };

        double lfo1Rate = getLfoRateHz(0);
        double lfo2Rate = getLfoRateHz(1);
        double lfo3Rate = getLfoRateHz(2);

        double voiceLfoRate;
        if (p.voice_lfo_sync_enabled)
            voiceLfoRate = static_cast<double>(divisionToHz(bpm, p.voice_lfo_sync_division));
        else
            voiceLfoRate = std::pow(2.0, (p.voice_lfo_rate - 64.0) / 16.0);

        lfo1_.setPhaseIncrement(lfo1Rate / sampleRate_);
        lfo2_.setPhaseIncrement(lfo2Rate / sampleRate_);
        lfo3_.setPhaseIncrement(lfo3Rate / sampleRate_);
        voiceLfo_.setPhaseIncrement(voiceLfoRate / sampleRate_);
        
        // Update modulation sources with LFO values
        // Note: LFOs are only used as modulation SOURCES (MOD_SRC_LFO_1-4), not as destinations.
        // The modulation matrix (getPatchForRead().modulation[]) defaults to all zeros, so LFOs are not
        // assigned to any destinations by default. They can be routed via the modulation matrix.
        modulationSources_[MOD_SRC_LFO_1] = lfo1_.render(getPatchForRead().env_lfo[0].shape);
        modulationSources_[MOD_SRC_LFO_2] = lfo2_.render(getPatchForRead().env_lfo[1].shape);
        modulationSources_[MOD_SRC_LFO_3] = lfo3_.render(getPatchForRead().env_lfo[2].shape);
        modulationSources_[MOD_SRC_LFO_4] = voiceLfo_.render(getPatchForRead().voice_lfo_shape);
        
        // Update modulation sources
        modulationSources_[MOD_SRC_NOISE] = nextRandomFloat();
        // Persisted for forward compatibility, but there is no active
        // sequencer/arpeggiator generator in the current DSP path yet.
        modulationSources_[MOD_SRC_SEQ_1] = 0.0f;
        modulationSources_[MOD_SRC_SEQ_2] = 0.0f;
        modulationSources_[MOD_SRC_ARP_STEP] = 0.0f;
        modulationSources_[MOD_SRC_NOTE] = (float)(pitchValue_ / 16384.0);
        modulationSources_[MOD_SRC_GATE] = gate_;
        
        // Envelope 1 is the main amplitude envelope
        modulationSources_[MOD_SRC_ENV_1] = envelope_.render();

        // Apply modifiers
        for (int i = 0; i < kNumModifiers; ++i)
        {
            if (getPatchForRead().modifier[i].op == 0) continue;

            uint8_t xIdx = getPatchForRead().modifier[i].operands[0];
            uint8_t yIdx = getPatchForRead().modifier[i].operands[1];
            float x = modulationSources_[xIdx];
            float y = modulationSources_[yIdx];
            uint8_t op = getPatchForRead().modifier[i].op;

            float result = 0.0f;
            switch (op)
            {
                case MODIFIER_SUM:
                    result = (x + y) * 0.5f;
                    break;
                case MODIFIER_PRODUCT:
                    result = x * y;
                    break;
                case MODIFIER_ATTENUATE:
                    result = x * (1.0f - y);
                    break;
                case MODIFIER_MAX:
                    result = gardenia_core::max_val(x, y);
                    break;
                case MODIFIER_MIN:
                    result = gardenia_core::min_val(x, y);
                    break;
                case MODIFIER_XOR:
                    result = ((int)(x * 255.0f) ^ (int)(y * 255.0f)) / 255.0f;
                    break;
                case MODIFIER_GE:
                    result = (x >= y) ? 1.0f : 0.0f;
                    break;
                case MODIFIER_LE:
                    result = (x <= y) ? 1.0f : 0.0f;
                    break;
                case MODIFIER_QUANTIZE:
                {
                    int quantizeBits = (int)(y * 8.0f);
                    int mask = (0xFF << (8 - quantizeBits)) & 0xFF;
                    result = ((int)(x * 255.0f) & mask) / 255.0f;
                    break;
                }
                case MODIFIER_LAG_PROCESSOR:
                {
                    float lag = y * 0.25f + 0.01f;
                    float oldValue = modulationSources_[MOD_SRC_OP_1 + i];
                    result = oldValue * (1.0f - lag) + x * lag;
                    break;
                }
            }

            modulationSources_[MOD_SRC_OP_1 + i] = result;
        }

        // Initialize destinations
        modulationDestinations_[MOD_DST_VCA] = (part_.volume * 2.0f) / 255.0f;
        
        dst_[MOD_DST_OSC_1] = 0.5f;
        dst_[MOD_DST_OSC_2] = 0.5f;
        dst_[MOD_DST_OSC_1_2_COARSE] = 0.5f;
        dst_[MOD_DST_OSC_1_2_FINE] = 0.5f;
        dst_[MOD_DST_PARAMETER_1] = getPatchForRead().osc[0].parameter / 127.0f;
        dst_[MOD_DST_PARAMETER_2] = getPatchForRead().osc[1].parameter / 127.0f;
        dst_[MOD_DST_MIX_BALANCE] = getPatchForRead().mix_balance / 127.0f;
        dst_[MOD_DST_MIX_PARAM] = getPatchForRead().mix_parameter / 127.0f;
        dst_[MOD_DST_MIX_FUZZ] = getPatchForRead().mix_fuzz / 127.0f;
        dst_[MOD_DST_MIX_CRUSH] = getPatchForRead().mix_crush / 127.0f;
        dst_[MOD_DST_MIX_NOISE] = getPatchForRead().mix_noise / 127.0f;
        dst_[MOD_DST_MIX_SUB_OSC] = getPatchForRead().mix_sub_osc / 127.0f;
        
        dst_[MOD_DST_FILTER_CUTOFF] = 1.0f;
        dst_[MOD_DST_FILTER_RESONANCE] = 0.0f;
        dst_[MOD_DST_ATTACK] = 0.5f;
        dst_[MOD_DST_DECAY] = 0.5f;
        dst_[MOD_DST_RELEASE] = 0.5f;
        dst_[MOD_DST_LFO_4] = getPatchForRead().voice_lfo_rate / 127.0f;
    }

    void processModulationMatrix()
    {
        for (int i = 0; i < kNumModulations; ++i)
        {
            uint8_t source = getPatchForRead().modulation[i].source;
            uint8_t destination = getPatchForRead().modulation[i].destination;
            int8_t amount = getPatchForRead().modulation[i].amount; // -63 to +63
            
            // Skip if no modulation assigned
            if (amount == 0 || source >= MOD_SRC_LAST || destination >= MOD_DST_LAST)
                continue;
            
            // Convert amount to normalized range (-1.0 to +1.0)
            float normalizedAmount = amount / 63.0f;
            
            // Special case: last slot is affected by mod wheel
            if (i == kNumModulations - 1)
            {
                normalizedAmount *= modulationSources_[MOD_SRC_WHEEL] * 2.0f;
            }
            
            float sourceValue = modulationSources_[source];
            
            // Handle bipolar sources (pitch bend, note)
            // These need to be centered around 0.5 and scaled to ±1.0
            if (source == MOD_SRC_PITCH_BEND || source == MOD_SRC_NOTE)
            {
                sourceValue = (sourceValue - 0.5f) * 2.0f; // -1.0 to +1.0
            }
            
            // Apply modulation
            dst_[destination] += normalizedAmount * sourceValue;
            
            // Clamp destination to valid range
            dst_[destination] = gardenia_core::jlimit(0.0f, 1.0f, dst_[destination]);
        }
    }
    
    // Update per-sample modulation sources and recalculate destinations used per sample
    void updatePerSampleModulation(float envValue, float filterEnvValue, float auxEnvValue1, float auxEnvValue2)
    {
        // Update envelope sources per sample
        modulationSources_[MOD_SRC_ENV_1] = envValue;
        modulationSources_[MOD_SRC_ENV_2] = filterEnvValue;
        modulationSources_[MOD_SRC_ENV_3] = auxEnvValue1;
        modulationSources_[MOD_SRC_ENV_4] = auxEnvValue2;
        
        // Reset per-sample destinations to base values
        dst_[MOD_DST_FILTER_CUTOFF] = 1.0f;
        dst_[MOD_DST_FILTER_RESONANCE] = 0.0f;
        dst_[MOD_DST_MIX_BALANCE] = getPatchForRead().mix_balance / 127.0f;
        dst_[MOD_DST_MIX_PARAM] = getPatchForRead().mix_parameter / 127.0f;
        dst_[MOD_DST_MIX_FUZZ] = getPatchForRead().mix_fuzz / 127.0f;
        dst_[MOD_DST_MIX_CRUSH] = getPatchForRead().mix_crush / 127.0f;
        dst_[MOD_DST_MIX_NOISE] = getPatchForRead().mix_noise / 127.0f;
        dst_[MOD_DST_MIX_SUB_OSC] = getPatchForRead().mix_sub_osc / 127.0f;
        
        // Process modulation matrix for per-sample destinations
        for (int i = 0; i < kNumModulations; ++i)
        {
            uint8_t source = getPatchForRead().modulation[i].source;
            uint8_t destination = getPatchForRead().modulation[i].destination;
            int8_t amount = getPatchForRead().modulation[i].amount; // -63 to +63
            
            // Skip if no modulation assigned
            if (amount == 0 || source >= MOD_SRC_LAST || destination >= MOD_DST_LAST)
                continue;
            
            // Only recalculate per-sample destinations
            bool isPerSampleDest = (destination == MOD_DST_FILTER_CUTOFF || 
                                    destination == MOD_DST_FILTER_RESONANCE ||
                                    destination == MOD_DST_MIX_BALANCE ||
                                    destination == MOD_DST_MIX_PARAM ||
                                    destination == MOD_DST_MIX_FUZZ ||
                                    destination == MOD_DST_MIX_CRUSH ||
                                    destination == MOD_DST_MIX_NOISE ||
                                    destination == MOD_DST_MIX_SUB_OSC);
            
            if (!isPerSampleDest)
                continue;
            
            // Convert amount to normalized range (-1.0 to +1.0)
            float normalizedAmount = amount / 63.0f;
            
            // Special case: last slot is affected by mod wheel
            if (i == kNumModulations - 1)
            {
                normalizedAmount *= modulationSources_[MOD_SRC_WHEEL] * 2.0f;
            }
            
            float sourceValue = modulationSources_[source];
            
            // Handle bipolar sources (pitch bend, note)
            // These need to be centered around 0.5 and scaled to ±1.0
            if (source == MOD_SRC_PITCH_BEND || source == MOD_SRC_NOTE)
            {
                sourceValue = (sourceValue - 0.5f) * 2.0f; // -1.0 to +1.0
            }
            
            // Apply modulation
            dst_[destination] += normalizedAmount * sourceValue;
            
            // Clamp destination to valid range
            dst_[destination] = gardenia_core::jlimit(0.0f, 1.0f, dst_[destination]);
        }
    }

    void updateDestinations()
    {
        modulationDestinations_[MOD_DST_FILTER_CUTOFF] = 1.0f;
        modulationDestinations_[MOD_DST_FILTER_RESONANCE] = 0.0f;
        modulationDestinations_[MOD_DST_MIX_CRUSH] = dst_[MOD_DST_MIX_CRUSH] + 0.01f;

        osc1_.setParameter(dst_[MOD_DST_PARAMETER_1] * 127.0f);
        osc1_.setFmParameter(getPatchForRead().osc[0].range + 36.0f);
        osc2_.setParameter(dst_[MOD_DST_PARAMETER_2] * 127.0f);
        osc2_.setFmParameter(getPatchForRead().osc[1].range + 36.0f);

        const auto getModulatedEnvelopeValue = [this](uint8_t baseValue, ModulationDestination destination)
        {
            const auto modulationOffset = gardenia_core::round_to_int((dst_[destination] - 0.5f) * 127.0f);
            return static_cast<uint8_t>(gardenia_core::jlimit(0, 127, static_cast<int>(baseValue) + modulationOffset));
        };

        const auto& p = getPatchForRead();
        envelope_.update(getModulatedEnvelopeValue(p.env_lfo[0].attack, MOD_DST_ATTACK),
                         getModulatedEnvelopeValue(p.env_lfo[0].decay, MOD_DST_DECAY),
                         p.env_lfo[0].sustain,
                         getModulatedEnvelopeValue(p.env_lfo[0].release, MOD_DST_RELEASE));
        filterEnvelope_.update(getModulatedEnvelopeValue(p.env_lfo[1].attack, MOD_DST_ATTACK),
                               getModulatedEnvelopeValue(p.env_lfo[1].decay, MOD_DST_DECAY),
                               p.env_lfo[1].sustain,
                               getModulatedEnvelopeValue(p.env_lfo[1].release, MOD_DST_RELEASE));
        auxEnvelope1_.update(getModulatedEnvelopeValue(p.env_lfo[2].attack, MOD_DST_ATTACK),
                             getModulatedEnvelopeValue(p.env_lfo[2].decay, MOD_DST_DECAY),
                             p.env_lfo[2].sustain,
                             getModulatedEnvelopeValue(p.env_lfo[2].release, MOD_DST_RELEASE));
        auxEnvelope2_.update(getModulatedEnvelopeValue(p.env_lfo[3].attack, MOD_DST_ATTACK),
                             getModulatedEnvelopeValue(p.env_lfo[3].decay, MOD_DST_DECAY),
                             p.env_lfo[3].sustain,
                             getModulatedEnvelopeValue(p.env_lfo[3].release, MOD_DST_RELEASE));

        if (!p.voice_lfo_sync_enabled)
        {
            const auto modulatedVoiceLfoRate = gardenia_core::jlimit(0.0f, 1.0f, dst_[MOD_DST_LFO_4]) * 127.0f;
            const auto voiceLfoRateHz = std::pow(2.0, (modulatedVoiceLfoRate - 64.0f) / 16.0f);
            voiceLfo_.setPhaseIncrement(voiceLfoRateHz / sampleRate_);
        }
    }

    void renderBlock(float* buffer, int numSamples)
    {
        ((void)0); // jassert(numSamples <= kMaxProcessBlockSamples);
        if (numSamples > kMaxProcessBlockSamples)
            numSamples = kMaxProcessBlockSamples;

        // Apply portamento
        double basePitch = pitchValue_;
        if (pitchIncrement_ != 0.0)
        {
            basePitch += pitchIncrement_;
            if ((pitchIncrement_ > 0.0) != (basePitch < pitchTarget_))
            {
                basePitch = pitchTarget_;
                pitchIncrement_ = 0.0;
            }
            pitchValue_ = basePitch;
        }

        basePitch += (dst_[MOD_DST_OSC_1_2_COARSE] - 0.5f) * 4.0 * 128.0;
        basePitch += (dst_[MOD_DST_OSC_1_2_FINE] - 0.5f) * 0.5 * 128.0;

        // Calculate pitch with modulations
        double pitch1 = basePitch;
        if (getPatchForRead().osc[0].shape != WAVEFORM_FM)
        {
            pitch1 += getPatchForRead().osc[0].range * 128.0;
        }
        pitch1 += getPatchForRead().osc[0].detune;
        pitch1 += (dst_[MOD_DST_OSC_1] - 0.5f) * 16.0 * 128.0;

        double pitch2 = basePitch;
        if (getPatchForRead().osc[1].shape != WAVEFORM_FM)
        {
            pitch2 += getPatchForRead().osc[1].range * 128.0;
        }
        pitch2 += getPatchForRead().osc[1].detune;
        pitch2 += (dst_[MOD_DST_OSC_2] - 0.5f) * 16.0 * 128.0;

        int midiNote1 = gardenia_core::jlimit(0, 127, (int)(pitch1 / 128.0) - 12);
        int midiNote2 = gardenia_core::jlimit(0, 127, (int)(pitch2 / 128.0) - 12);

        double midiNoteFloat1 = (pitch1 / 128.0) - 12.0;
        double midiNoteFloat2 = (pitch2 / 128.0) - 12.0;
        double freq1 = midiNoteToFrequency((int)midiNoteFloat1) * std::pow(2.0, (midiNoteFloat1 - (int)midiNoteFloat1) / 12.0);
        double freq2 = midiNoteToFrequency((int)midiNoteFloat2) * std::pow(2.0, (midiNoteFloat2 - (int)midiNoteFloat2) / 12.0);
        double increment1 = frequencyToPhaseIncrement(freq1, sampleRate_);
        double increment2 = frequencyToPhaseIncrement(freq2, sampleRate_);

        subOsc_.setIncrement(increment1 * 0.5);

        // Use pre-allocated buffers (no allocation in audio thread!)
        // Clear only the portion we need
        std::fill(osc1Buffer_.begin(), osc1Buffer_.begin() + numSamples, 0.0f);
        std::fill(osc2Buffer_.begin(), osc2Buffer_.begin() + numSamples, 0.0f);
        std::fill(syncState_.begin(), syncState_.begin() + numSamples, static_cast<uint8_t>(0));
        std::fill(noSync_.begin(), noSync_.begin() + numSamples, static_cast<uint8_t>(0));

        // Oscillator mixing constants (declared here to be used throughout rendering)
        static constexpr float kOscMixScale = 0.5f;       // Scale factor for mixing two oscillators
        static constexpr float kFmAmountScale = 2.0f;     // FM modulation depth scaling
        static constexpr float kXorScale = 127.0f;        // XOR operation bit scaling
        static constexpr float kXorOffset = 128.0f;       // XOR operation offset
        static constexpr float kFoldScale = 2.0f;         // Wave folding scale factor
        static constexpr int kBitCrushBits = 8;           // Bit reduction resolution (8 bits)
        static constexpr int kBitMask = 0xFF;             // 8-bit mask

        // For FM, OSC2 is the modulator and must be rendered first
        if (getPatchForRead().mix_op == OP_FM)
        {
            // Render OSC2 first (modulator)
            osc2_.render(getPatchForRead().osc[1].shape, midiNote2, increment2,
                         reinterpret_cast<bool*>(noSync_.data()), 
                         reinterpret_cast<bool*>(noSync_.data()), 
                         osc2Buffer_.data(), numSamples, wavetableModel_);
            
            // Render OSC1 with phase modulation from OSC2 (carrier)
            // FM amount is controlled by mix parameter (wetGain)
            float fmAmount = dst_[MOD_DST_MIX_PARAM] * kFmAmountScale; // Scale for appropriate FM depth
            osc1_.renderWithPhaseModulation(getPatchForRead().osc[0].shape, midiNote1, increment1,
                                           reinterpret_cast<bool*>(noSync_.data()), 
                                           reinterpret_cast<bool*>(syncState_.data()),
                                           osc2Buffer_.data(), fmAmount,
                                           osc1Buffer_.data(), numSamples);
        }
        else
        {
            // Normal rendering for other operators
            osc1_.render(getPatchForRead().osc[0].shape, midiNote1, increment1, 
                         reinterpret_cast<bool*>(noSync_.data()), 
                         reinterpret_cast<bool*>(syncState_.data()), 
                         osc1Buffer_.data(), numSamples, wavetableModel_);
            
            bool* syncInput = (getPatchForRead().mix_op == OP_SYNC) ? 
                             reinterpret_cast<bool*>(syncState_.data()) : 
                             reinterpret_cast<bool*>(noSync_.data());
            osc2_.render(getPatchForRead().osc[1].shape, midiNote2, increment2,
                         syncInput, 
                         reinterpret_cast<bool*>(noSync_.data()), 
                         osc2Buffer_.data(), numSamples, wavetableModel_);
        }

        const float mixScale = kOscMixScale;
        
        // Pre-render envelope values for per-sample modulation (to avoid double-rendering)
        // Use pre-allocated buffers (no allocation!)
        for (int i = 0; i < numSamples; ++i)
        {
            envValues_[i] = envelope_.render();
            filterEnvValues_[i] = filterEnvelope_.render();
            auxEnvValues1_[i] = auxEnvelope1_.render();
            auxEnvValues2_[i] = auxEnvelope2_.render();
        }

        // Mix oscillators based on operator
        // Mix parameters are updated per-sample for smooth modulation
        switch (getPatchForRead().mix_op)
        {
            case OP_RING_MOD:
                for (int i = 0; i < numSamples; ++i)
                {
                    // Update modulation per sample before using mix parameters
                    updatePerSampleModulation(envValues_[i], filterEnvValues_[i], auxEnvValues1_[i], auxEnvValues2_[i]);
                    
                    // Get per-sample mix parameters
                    float osc2Gain = dst_[MOD_DST_MIX_BALANCE];
                    float osc1Gain = 1.0f - osc2Gain;
                    float wetGain = dst_[MOD_DST_MIX_PARAM];
                    float dryGain = 1.0f - wetGain;
                    
                    float mix = (osc1Buffer_[i] * osc1Gain + osc2Buffer_[i] * osc2Gain) * mixScale;
                    const uint8_t osc1Byte = gardenia_core::float_to_ambika_byte (osc1Buffer_[i]);
                    const uint8_t osc2Byte = gardenia_core::float_to_ambika_byte (osc2Buffer_[i]);
                    const uint8_t ringByte = static_cast<uint8_t> (
                        gardenia_core::s8s8_mul_shift8 (
                            gardenia_core::wrap_byte_to_s8 (static_cast<uint8_t> (osc1Byte + 128u)),
                            gardenia_core::wrap_byte_to_s8 (static_cast<uint8_t> (osc2Byte + 128u))) + 128);
                    const float ring = gardenia_core::ambika_byte_to_float (ringByte);
                    buffer[i] = mix * dryGain + ring * wetGain;
                }
                break;
            case OP_XOR:
                for (int i = 0; i < numSamples; ++i)
                {
                    updatePerSampleModulation(envValues_[i], filterEnvValues_[i], auxEnvValues1_[i], auxEnvValues2_[i]);
                    
                    float osc2Gain = dst_[MOD_DST_MIX_BALANCE];
                    float osc1Gain = 1.0f - osc2Gain;
                    float wetGain = dst_[MOD_DST_MIX_PARAM];
                    float dryGain = 1.0f - wetGain;
                    
                    float mix = (osc1Buffer_[i] * osc1Gain + osc2Buffer_[i] * osc2Gain) * mixScale;
                    const uint8_t xord = static_cast<uint8_t> (
                        gardenia_core::float_to_ambika_byte (osc1Buffer_[i]) ^ gardenia_core::float_to_ambika_byte (osc2Buffer_[i]));
                    const float xordFloat = gardenia_core::ambika_byte_to_float (xord);
                    buffer[i] = mix * dryGain + xordFloat * wetGain;
                }
                break;
            case OP_FOLD:
                for (int i = 0; i < numSamples; ++i)
                {
                    updatePerSampleModulation(envValues_[i], filterEnvValues_[i], auxEnvValues1_[i], auxEnvValues2_[i]);
                    
                    float osc2Gain = dst_[MOD_DST_MIX_BALANCE];
                    float osc1Gain = 1.0f - osc2Gain;
                    float wetGain = dst_[MOD_DST_MIX_PARAM];
                    float dryGain = 1.0f - wetGain;
                    
                    float mix = (osc1Buffer_[i] * osc1Gain + osc2Buffer_[i] * osc2Gain) * mixScale;
                    
                    // Wave folding with 2x oversampling to reduce aliasing
                    if (wetGain > 0.01f)
                    {
                        buffer[i] = foldingOversampler_.process(mix, [wetGain, dryGain](float x) {
                            float folded = std::abs(x) * kFoldScale - 1.0f;
                            return x * dryGain + folded * wetGain;
                        });
                    }
                    else
                    {
                        buffer[i] = mix;  // Bypass if no folding
                    }
                }
                break;
            case OP_BITS:
                for (int i = 0; i < numSamples; ++i)
                {
                    updatePerSampleModulation(envValues_[i], filterEnvValues_[i], auxEnvValues1_[i], auxEnvValues2_[i]);
                    
                    float osc2Gain = dst_[MOD_DST_MIX_BALANCE];
                    float osc1Gain = 1.0f - osc2Gain;
                    // Gardenia extension: Crush controls bit depth while OP_BITS
                    // remains the selected operator. Keep 0 = clean 8-bit mask
                    // and 1 = strongest 1-bit mask; do not collapse to silence.
                    const float crush = gardenia_core::jlimit (0.0f, 1.0f, dst_[MOD_DST_MIX_CRUSH]);
                    const int bits = gardenia_core::jlimit (1, kBitCrushBits,
                        kBitCrushBits - gardenia_core::round_to_int (crush * static_cast<float> (kBitCrushBits - 1)));
                    const int mask = (kBitMask << (kBitCrushBits - bits)) & kBitMask;
                    
                    float mix = (osc1Buffer_[i] * osc1Gain + osc2Buffer_[i] * osc2Gain) * mixScale;
                    // Quantize to reduced bit depth
                    int quantized = ((int)(mix * kXorScale + kXorOffset)) & mask;
                    buffer[i] = (quantized / kXorScale) - 1.0f;
                }
                break;
            case OP_FM:
                // For FM, OSC1 (carrier) is already modulated by OSC2 (modulator)
                // Output is the FM result - the mix parameter controls FM depth
                for (int i = 0; i < numSamples; ++i)
                {
                    // OSC1 is the FM result (carrier modulated by OSC2)
                    buffer[i] = osc1Buffer_[i] * mixScale;
                }
                break;
            default:
                for (int i = 0; i < numSamples; ++i)
                {
                    updatePerSampleModulation(envValues_[i], filterEnvValues_[i], auxEnvValues1_[i], auxEnvValues2_[i]);
                    
                    float osc2Gain = dst_[MOD_DST_MIX_BALANCE];
                    float osc1Gain = 1.0f - osc2Gain;
                    buffer[i] = (osc1Buffer_[i] * osc1Gain + osc2Buffer_[i] * osc2Gain) * mixScale;
                }
                break;
        }

        // Add sub-oscillator (scale to match main oscillator levels)
        // Note: subGain modulation is handled per-sample, but sub-osc rendering is per-block
        // Use initial value for sub-osc rendering (per-sample gain modulation would require per-sample rendering)
        float initialSubGain = dst_[MOD_DST_MIX_SUB_OSC] * mixScale;
        
        // If sub osc solo is enabled, clear the buffer first (mute main oscillators)
        if (subOscSolo_)
        {
            std::fill(buffer, buffer + numSamples, 0.0f);
        }
        
        if (getPatchForRead().mix_sub_osc_shape < WAVEFORM_SUB_OSC_CLICK)
        {
            subOsc_.render(getPatchForRead().mix_sub_osc_shape, buffer, initialSubGain, numSamples);
        }
        else
        {
            transientGenerator_.render(getPatchForRead().mix_sub_osc_shape, buffer, initialSubGain * 2.0f, numSamples);
        }

        // Pre-calculate filter parameters that don't change per sample
        const auto& patchForFilter = getPatchForRead();
        float baseFilterCutoff = CutoffControlMapping::mapPatchCutoffToHz(patchForFilter);
        
        // Apply filter envelope modulation (attenuverter)
        // filter_env is -127 to 127, where 0 is center (no modulation)
        // Convert to -1.0 to 1.0 range (centered at 0)
        float filterEnvAmount = getPatchForRead().filter_env / 127.0f;
        filterEnvAmount = gardenia_core::jlimit(-1.0f, 1.0f, filterEnvAmount);
        
        // Apply multiplier: 0 = x1, 1 = x2, 2 = x4
        float multiplier = 1.0f;
        if (filterEnvMultiplier_ == 1)
            multiplier = 2.0f;
        else if (filterEnvMultiplier_ == 2)
            multiplier = 4.0f;
        
        // Base filter resonance (will be modulated per-sample if needed)
        float baseFilterResonance = getPatchForRead().filter[0].resonance / 127.0f;

        // Apply VCA with envelope
        float baseVCA = modulationDestinations_[MOD_DST_VCA];
        
        for (int i = 0; i < numSamples; ++i)
        {
            // Use pre-rendered envelope values (already rendered above to avoid double-rendering)
            float envValue = envValues_[i];
            float filterEnvValue = filterEnvValues_[i];
            float auxEnvValue1 = auxEnvValues1_[i];
            float auxEnvValue2 = auxEnvValues2_[i];

            // Update modulation sources and recalculate per-sample destinations
            updatePerSampleModulation(envValue, filterEnvValue, auxEnvValue1, auxEnvValue2);

            // Stage A: stem is pre-private-filter synthesis sample in buffer[i].
            // Compute noise + host control streams; do not apply host filter/fuzz/VCA/DC/FX.
            float sample = buffer[i];

            float noise = nextRandomFloat() * 2.0f - 1.0f;

            float noiseGain = dst_[MOD_DST_MIX_NOISE];
            float signalGain = 1.0f - noiseGain;
            float fuzzGain = dst_[MOD_DST_MIX_FUZZ];

            float filterResonance = baseFilterResonance + dst_[MOD_DST_FILTER_RESONANCE];
            filterResonance = gardenia_core::jlimit(0.0f, 1.0f, filterResonance);

            float envelopeModulation;
            if (filterEnvAmount >= 0.0f)
            {
                envelopeModulation = filterEnvValue * filterEnvAmount;
            }
            else
            {
                envelopeModulation = (1.0f - filterEnvValue) * filterEnvAmount;
            }

            envelopeModulation *= multiplier;

            float octaveModulation = envelopeModulation * 4.0f;
            float frequencyMultiplier = std::pow(2.0f, octaveModulation);

            float instantCutoff = baseFilterCutoff * frequencyMultiplier;

            const float filterLfoAmount = gardenia_core::jlimit(-1.0f, 1.0f, getPatchForRead().filter_lfo / 127.0f);
            const float filterLfoBipolar = (modulationSources_[MOD_SRC_LFO_2] - 0.5f) * 2.0f;
            instantCutoff *= std::pow(2.0f, filterLfoBipolar * filterLfoAmount * 4.0f);

            instantCutoff += (dst_[MOD_DST_FILTER_CUTOFF] - 1.0f) * 10000.0f;
            if (getPatchForRead().filter_type == FILTER_TYPE_ACID303)
            {
                instantCutoff = gardenia_core::jlimit(CutoffControlMapping::kCutoffMinHz,
                                                     CutoffControlMapping::kCutoffMaxExtendedHz,
                                                     instantCutoff);
            }
            else
            {
                instantCutoff = gardenia_core::jlimit(20.0f, 20000.0f, instantCutoff);
            }

            smoothedFilterCutoff_ += kFilterSmoothingCoeff * (instantCutoff - smoothedFilterCutoff_);

            float vcaLevel = baseVCA * envValue * velocity_;
            vcaLevel = gardenia_core::jlimit(0.0f, 1.0f, vcaLevel);

            if (stageABuffers_ != nullptr)
            {
                const uint32_t absIndex = stageABuffers_->writeOffset + static_cast<uint32_t>(i);
                const uint32_t capacity = stageABuffers_->capacity;
                auto storeF = [&](float* dst, float v) {
                    if (dst != nullptr && absIndex < capacity)
                        dst[absIndex] = v;
                };
                auto storeU = [&](uint8_t* dst, uint8_t v) {
                    if (dst != nullptr && absIndex < capacity)
                        dst[absIndex] = v;
                };
                storeF(stageABuffers_->stem, sample);
                storeF(stageABuffers_->noise, noise);
                storeF(stageABuffers_->filterEnv, filterEnvValue);
                storeF(stageABuffers_->lfo1, modulationSources_[MOD_SRC_LFO_1]);
                storeF(stageABuffers_->lfo2, modulationSources_[MOD_SRC_LFO_2]);
                storeF(stageABuffers_->lfo3, modulationSources_[MOD_SRC_LFO_3]);
                storeF(stageABuffers_->voiceLfo, modulationSources_[MOD_SRC_LFO_4]);
                storeF(stageABuffers_->cutoffMod, dst_[MOD_DST_FILTER_CUTOFF]);
                storeF(stageABuffers_->cutoffHz, smoothedFilterCutoff_);
                storeF(stageABuffers_->resonance, filterResonance);
                storeF(stageABuffers_->signalGain, signalGain);
                storeF(stageABuffers_->noiseGain, noiseGain);
                storeF(stageABuffers_->fuzzMix, fuzzGain);
                storeF(stageABuffers_->vca, vcaLevel);
                storeF(stageABuffers_->pitch, static_cast<float>(pitchValue_));
                storeU(stageABuffers_->active, isActive() ? 1u : 0u);
                storeU(stageABuffers_->gate, gate_ > 0.0f ? 1u : 0u);
            }

            buffer[i] = sample;
        }
    }

    // Double-buffered patch for lock-free UI/audio thread safety
    std::array<Patch, 2> patchBuffer_;
    std::atomic<int> patchReadIndex_{ 0 };

    inline const Patch& getPatchForRead() const
    {
        return patchBuffer_[patchReadIndex_.load(std::memory_order_acquire)];
    }





    Part part_;
    
    Oscillator osc1_;
    Oscillator osc2_;
    SubOscillator subOsc_;
    TransientGenerator transientGenerator_;
    Envelope envelope_;  // Main amplitude envelope (env_lfo[0])
    Envelope filterEnvelope_;  // Filter envelope (env_lfo[1])
    Envelope auxEnvelope1_;  // Auxiliary envelope 1 (env_lfo[2])
    Envelope auxEnvelope2_;  // Auxiliary envelope 2 (env_lfo[3])
    
    // LFOs: 3 patch-level (shared), 1 voice-level
    LFO lfo1_, lfo2_, lfo3_;  // Patch LFOs
    LFO voiceLfo_;             // Voice LFO
    
    // LFO state
    float lfoPhases_[4];

    float gate_;
    float velocity_;
    double pitchValue_;
    double pitchTarget_;
    double pitchIncrement_;

    std::array<float, kNumModulationSources> modulationSources_{};
    std::array<float, kNumModulationDestinations> modulationDestinations_{};
    std::array<float, kNumModulationDestinations> dst_{};

    double sampleRate_;
    float tempoBpm_ = 120.0f;
    bool delayBypass_ = false;
    bool reverbBypass_ = false;
    bool stickyFxTail_ = false; // Stage-A proxy for delay/reverb hasTail keep-alive
    StageABuffers* stageABuffers_ = nullptr;

    // Filter state
    bool filterBypass_ = false;
    int filterEnvMultiplier_ = 0; // 0 = x1, 1 = x2, 2 = x4
    bool subOscSolo_ = false;
    uint8_t wavetableModel_ = WT_MODEL_AMBIKA;
    float smoothedFilterCutoff_ = 1000.0f;  // Smoothed cutoff to prevent zipper noise
    
    // DC blocker state
    float dcBlockerX1_;
    float dcBlockerY1_;
    
    // Oversamplers for nonlinear effects (reduces aliasing from nonlinear processing)
    SimpleOversampler foldingOversampler_;
    
    // Pre-allocated buffers for audio rendering (to avoid allocations in audio thread)
    std::vector<float> osc1Buffer_;
    std::vector<float> osc2Buffer_;
    std::vector<uint8_t> syncState_;
    std::vector<uint8_t> noSync_;
    std::vector<float> envValues_;
    std::vector<float> filterEnvValues_;
    std::vector<float> auxEnvValues1_;
    std::vector<float> auxEnvValues2_;
    uint32_t rngState_ = 1u;


    uint32_t nextRandomUInt32() noexcept
    {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return rngState_;
    }

    float nextRandomFloat() noexcept
    {
        return static_cast<float>(nextRandomUInt32() >> 8) * (1.0f / 16777216.0f);
    }
};

} // namespace gardenia_core
