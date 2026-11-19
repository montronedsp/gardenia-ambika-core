/*
  gardenia_core_instance.h — Internal C++ instance (not public ABI).
*/
#pragma once

#include "gardenia_core/gardenia_core.h"
#include "gardenia_core/CoreVoice.h"
#include "gardenia_core/AmbikaFuzzProcessor.h"
#include "gardenia_core/Patch.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gardenia_core_api
{

constexpr uint32_t kMaxVoices = GARDENIA_CORE_MAX_VOICE_SLOTS;
constexpr uint32_t kMaxBlock = GARDENIA_CORE_MAX_BLOCK_SIZE;

struct VoiceRuntime
{
    gardenia_core::CoreVoice voice;
    gardenia_core::AmbikaFuzzProcessor fuzz;
    int currentNote = -1;
    float currentVelocity = 0.0f;
};

struct Instance
{
    uint32_t voiceCount = kMaxVoices;
    uint32_t maxBlock = 64;
    double sampleRate = 48000.0;
    uint64_t seed = 0;
    bool prepared = false;

    std::array<VoiceRuntime, kMaxVoices> voices {};
    std::vector<float> scratch; // maxBlock samples, allocated at prepare

    gardenia_core::Patch lastPatch {};
    gardenia_core::Part lastPart {};
    float tempoBpm = 120.0f;
    uint8_t wavetableModel = 0;
    bool delayBypass = false;
    bool reverbBypass = false;
};

bool validate_header (uint32_t struct_size, uint32_t abi_version, uint32_t min_size) noexcept;
bool reserved_ok_u32 (const uint32_t* p, uint32_t n) noexcept;
bool reserved_ok_u64 (const uint64_t* p, uint32_t n) noexcept;

void apply_snapshot_to_voices (Instance& inst, const gardenia_core_synthesis_snapshot& snap);
void blob_from_patch (gardenia_core_synthesis_snapshot& snap, const gardenia_core::Patch& p, const gardenia_core::Part& part);
bool blob_to_patch (const gardenia_core_synthesis_snapshot& snap, gardenia_core::Patch& p, gardenia_core::Part& part);

} // namespace gardenia_core_api

struct gardenia_core_instance
{
    gardenia_core_api::Instance impl;
};
