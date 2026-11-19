#pragma once

#include <cstdint>

namespace gardenia_core
{

static constexpr int kMaxVoices = 8;
static constexpr int kMaxBlockSamples = 2048;
static constexpr int kCoreApiVersion = 1;

enum class CoreResult : int32_t
{
    Ok = 0,
    InvalidArgument = 1,
    NotPrepared = 2,
    BufferTooSmall = 3
};

enum class LifecycleEventType : uint8_t
{
    NoteOn = 1,
    NoteOff = 2,
    Retrigger = 3,
    Kill = 6
};

struct CoreEvent
{
    uint32_t sampleOffset = 0;
    uint8_t type = 0; // 1=noteOn, 2=noteOff, ...
    uint8_t channel = 0;
    int16_t note = 60;
    float velocity = 0.8f;
    uint16_t reserved = 0;
};

} // namespace gardenia_core
