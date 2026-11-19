#pragma once
#include "gardenia_core/gardenia_core_math.h"
/*
  Envelope.h — Ambika-derived envelope generator for Gardenia Ambika Core.
*/


#include "gardenia_core/Patch.h"

namespace gardenia_core
{

enum EnvelopeStage
{
    ATTACK = 0,
    DECAY = 1,
    SUSTAIN = 2,
    RELEASE = 3,
    DEAD = 4,
    NUM_SEGMENTS
};

class Envelope
{
public:
    Envelope()
    {
        reset();
    }

    void init(double sampleRate)
    {
        sampleRate_ = sampleRate;
        
        // Ultra-short de-click filter (~0.5ms at 44.1kHz)
        // This is CRITICAL for removing clicks
        declickCoeff_ = 1.0f - std::exp(-1.0f / (float)(sampleRate * 0.0005));
        declickCoeff_ = gardenia_core::jlimit(0.01f, 0.99f, declickCoeff_);
    }

    void reset()
    {
        currentValue_ = 0.0f;
        targetValue_ = 0.0f;
        smoothedValue_ = 0.0f;
        stage_ = DEAD;
        stageProgress_ = 0.0f;
        
        attackTime_ = 0.001f;  // 1ms minimum
        decayTime_ = 0.1f;
        sustainLevel_ = 0.7f;
        releaseTime_ = 0.1f;
    }

    uint8_t stage() const { return stage_; }

    void trigger(uint8_t newStage)
    {
        if (newStage == DEAD)
        {
            stage_ = DEAD;
            targetValue_ = 0.0f;
            stageProgress_ = 0.0f;
            return;
        }

        // CRITICAL: Never jump values - always smooth transition
        if (newStage == ATTACK)
        {
            stage_ = ATTACK;
            // Start from wherever we are now
            currentValue_ = smoothedValue_;
            targetValue_ = 1.0f;
            stageProgress_ = 0.0f;
        }
        else if (newStage == DECAY)
        {
            stage_ = DECAY;
            currentValue_ = smoothedValue_;
            targetValue_ = sustainLevel_;
            stageProgress_ = 0.0f;
        }
        else if (newStage == SUSTAIN)
        {
            stage_ = SUSTAIN;
            currentValue_ = sustainLevel_;
            targetValue_ = sustainLevel_;
            stageProgress_ = 0.0f;
        }
        else if (newStage == RELEASE)
        {
            stage_ = RELEASE;
            // Store the current smoothed value as the release start point
            // This ensures smooth transition without clicks
            targetValue_ = smoothedValue_;  // Store release start value
            currentValue_ = smoothedValue_;
            stageProgress_ = 0.0f;
        }
    }

    void update(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release)
    {
        // Convert MIDI values to time in seconds
        // Using exponential scaling for musical response
        
        // Attack: 0.5ms to 10 seconds
        float attackNorm = attack / 127.0f;
        attackTime_ = 0.0005f + (attackNorm * attackNorm * attackNorm) * 10.0f;
        
        // Decay: 1ms to 10 seconds
        float decayNorm = decay / 127.0f;
        decayTime_ = 0.001f + (decayNorm * decayNorm * decayNorm) * 10.0f;
        
        // Sustain: 0 to 1 (linear)
        sustainLevel_ = sustain / 127.0f;
        
        // Release: 1ms to 15 seconds
        float releaseNorm = release / 127.0f;
        releaseTime_ = 0.001f + (releaseNorm * releaseNorm * releaseNorm) * 15.0f;
    }

    float render()
    {
        if (stage_ == DEAD)
        {
            currentValue_ = 0.0f;
            smoothedValue_ = 0.0f;
            return 0.0f;
        }

        // Calculate increment based on stage
        float timeInStage = 0.0f;
        float increment = 0.0f;
        
        switch (stage_)
        {
            case ATTACK:
                timeInStage = attackTime_;
                if (timeInStage > 0.0f)
                    increment = 1.0f / (float)(sampleRate_ * timeInStage);
                else
                    increment = 1.0f; // Instant
                break;
                
            case DECAY:
                timeInStage = decayTime_;
                if (timeInStage > 0.0f)
                    increment = 1.0f / (float)(sampleRate_ * timeInStage);
                else
                    increment = 1.0f;
                break;
                
            case RELEASE:
                timeInStage = releaseTime_;
                if (timeInStage > 0.0f)
                    increment = 1.0f / (float)(sampleRate_ * timeInStage);
                else
                    increment = 1.0f;
                break;
                
            case SUSTAIN:
                currentValue_ = sustainLevel_;
                targetValue_ = sustainLevel_;
                smoothedValue_ += declickCoeff_ * (currentValue_ - smoothedValue_);
                return gardenia_core::jlimit(0.0f, 1.0f, smoothedValue_);
                
            default:
                break;
        }

        // Update progress
        stageProgress_ += increment;

        // Calculate current value with smooth curve
        if (stage_ == ATTACK)
        {
            // Exponential attack curve (feels more natural)
            float t = gardenia_core::jlimit(0.0f, 1.0f, stageProgress_);
            float curved = 1.0f - std::exp(-5.0f * t); // Exponential curve
            currentValue_ = curved;
            
            // Check if attack finished
            if (stageProgress_ >= 1.0f)
            {
                trigger(DECAY);
            }
        }
        else if (stage_ == DECAY)
        {
            // Exponential decay curve
            float t = gardenia_core::jlimit(0.0f, 1.0f, stageProgress_);
            float curved = std::exp(-3.0f * t); // Exponential decay
            currentValue_ = sustainLevel_ + (1.0f - sustainLevel_) * curved;
            
            // Check if decay finished
            if (stageProgress_ >= 1.0f)
            {
                trigger(SUSTAIN);
            }
        }
        else if (stage_ == RELEASE)
        {
            // Exponential release curve
            float t = gardenia_core::jlimit(0.0f, 1.0f, stageProgress_);
            float curved = std::exp(-4.0f * t); // Exponential release
            
            // Use the currentValue_ which was set to smoothedValue_ when release was triggered
            // This ensures smooth transition from sustain/release point
            currentValue_ = targetValue_ * curved;
            
            // Check if release finished
            if (stageProgress_ >= 1.0f || currentValue_ < 0.0001f)
            {
                trigger(DEAD);
                return 0.0f;
            }
        }

        // CRITICAL: One-pole smoothing filter to eliminate ALL clicks
        // Always smooth the output to avoid discontinuities
        smoothedValue_ += declickCoeff_ * (currentValue_ - smoothedValue_);
        
        // Ensure we never go outside bounds
        smoothedValue_ = gardenia_core::jlimit(0.0f, 1.0f, smoothedValue_);
        
        return smoothedValue_;
    }

private:
    // Current stage
    uint8_t stage_;
    
    // Timing parameters (in seconds)
    float attackTime_;
    float decayTime_;
    float sustainLevel_;
    float releaseTime_;
    
    // State variables
    float currentValue_;      // Target value for current stage
    float smoothedValue_;     // Actual output (smoothed)
    float targetValue_;       // Ultimate target
    float stageProgress_;     // 0.0 to 1.0 progress through current stage
    
    // De-click filter coefficient
    float declickCoeff_;
    
    double sampleRate_;
};

} // namespace gardenia_core