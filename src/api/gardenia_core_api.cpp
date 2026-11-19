/*
  gardenia_core_api.cpp — C ABI implementation wrapping CoreVoice / AmbikaFuzzProcessor.
*/
#include "gardenia_core_instance.h"

#include <algorithm>
#include <cstring>
#include <new>

#if defined(_MSC_VER)
#include <intrin.h>
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace
{

struct ScopedNoDenormals
{
#if defined(_MSC_VER)
    unsigned int csr_ = 0;
    ScopedNoDenormals() noexcept
    {
        csr_ = _mm_getcsr();
        _mm_setcsr (csr_ | 0x8040u);
    }
    ~ScopedNoDenormals() noexcept { _mm_setcsr (csr_); }
#else
    ScopedNoDenormals() noexcept = default;
#endif
};

float midi_velocity_roundtrip (float velocity) noexcept
{
    // Match host float note-on velocity reconstruction after 7-bit quantization:
    //   byte = round_to_int (v * 127.0f);
    //   return byte * (1.0f / 127.0f);  // NOT (float)byte / 127.0f (1 ULP for some bytes)
    const float clamped = gardenia_core::jlimit (0.0f, 1.0f, velocity);
    const int byte = gardenia_core::jlimit (0, 127, gardenia_core::round_to_int (clamped * 127.0f));
    return static_cast<float> (byte) * (1.0f / 127.0f);
}

float pitch_wheel_raw_to_normalized (int32_t wheel) noexcept
{
    const int clamped = gardenia_core::jlimit (0, 16383, static_cast<int> (wheel));
    const float bipolar = gardenia_core::jlimit (-1.0f, 1.0f,
                                                 static_cast<float> (clamped - 8192) / 8192.0f);
    return 0.5f + bipolar * 0.5f;
}

bool abi_major_ok (uint32_t abi_version) noexcept
{
    const uint32_t major = (abi_version >> 16) & 0xffffu;
    return major == GARDENIA_CORE_ABI_VERSION_MAJOR;
}

} // namespace

namespace gardenia_core_api
{

bool validate_header (uint32_t struct_size, uint32_t abi_version, uint32_t min_size) noexcept
{
    if (struct_size < min_size)
        return false;
    return abi_major_ok (abi_version);
}

bool reserved_ok_u32 (const uint32_t* p, uint32_t n) noexcept
{
    for (uint32_t i = 0; i < n; ++i)
        if (p[i] != 0u)
            return false;
    return true;
}

bool reserved_ok_u64 (const uint64_t* p, uint32_t n) noexcept
{
    for (uint32_t i = 0; i < n; ++i)
        if (p[i] != 0ull)
            return false;
    return true;
}

void blob_from_patch (gardenia_core_synthesis_snapshot& snap,
                      const gardenia_core::Patch& p,
                      const gardenia_core::Part& part)
{
    static_assert (sizeof (gardenia_core::Patch) <= GARDENIA_CORE_PATCH_BLOB_BYTES, "Patch too large");
    static_assert (sizeof (gardenia_core::Part) <= GARDENIA_CORE_PART_BLOB_BYTES, "Part too large");
    std::memset (snap.patch_blob, 0, sizeof (snap.patch_blob));
    std::memset (snap.part_blob, 0, sizeof (snap.part_blob));
    std::memcpy (snap.patch_blob, &p, sizeof (p));
    std::memcpy (snap.part_blob, &part, sizeof (part));
}

bool blob_to_patch (const gardenia_core_synthesis_snapshot& snap,
                    gardenia_core::Patch& p,
                    gardenia_core::Part& part)
{
    static_assert (sizeof (gardenia_core::Patch) <= GARDENIA_CORE_PATCH_BLOB_BYTES, "Patch too large");
    static_assert (sizeof (gardenia_core::Part) <= GARDENIA_CORE_PART_BLOB_BYTES, "Part too large");
    std::memcpy (&p, snap.patch_blob, sizeof (p));
    std::memcpy (&part, snap.part_blob, sizeof (part));
    return true;
}

void apply_snapshot_to_voices (Instance& inst, const gardenia_core_synthesis_snapshot& snap)
{
    gardenia_core::Patch patch;
    gardenia_core::Part part;
    blob_to_patch (snap, patch, part);
    inst.lastPatch = patch;
    inst.lastPart = part;
    inst.tempoBpm = snap.tempo_bpm;
    inst.wavetableModel = static_cast<uint8_t> (snap.wavetable_model);
    inst.delayBypass = snap.delay_bypass != 0u;
    inst.reverbBypass = snap.reverb_bypass != 0u;

    for (uint32_t i = 0; i < inst.voiceCount; ++i)
    {
        auto& v = inst.voices[i];
        v.voice.setPatch (patch);
        v.voice.setPart (part);
        v.voice.setTempoBpm (inst.tempoBpm);
        v.voice.setWavetableModel (inst.wavetableModel);
        v.voice.setDelayBypass (inst.delayBypass);
        v.voice.setReverbBypass (inst.reverbBypass);
    }
}

static void seed_voices (Instance& inst, uint64_t seed)
{
    inst.seed = seed;
    for (uint32_t i = 0; i < inst.voiceCount; ++i)
    {
        const uint32_t s = static_cast<uint32_t> (seed)
                         + static_cast<uint32_t> (i) * 0x85ebca6bu;
        // Match Phase-3 golden seeds when seed==0x9e3779b9 pattern via create config.
        const uint32_t voiceSeed = (seed == 0)
            ? (0x9e3779b9u + static_cast<uint32_t> (i) * 0x85ebca6bu)
            : s;
        inst.voices[i].voice.setRandomSeed (voiceSeed);
        inst.voices[i].voice.resetAllControllers();
        inst.voices[i].fuzz.reset();
        inst.voices[i].currentNote = -1;
        inst.voices[i].currentVelocity = 0.0f;
    }
}

static void apply_event (Instance& inst, const gardenia_core_event& ev)
{
    if (ev.voice_slot >= inst.voiceCount)
        return;
    auto& vr = inst.voices[ev.voice_slot];

    switch (ev.type)
    {
        case GARDENIA_CORE_EVENT_VOICE_START:
        {
            const float vel = midi_velocity_roundtrip (ev.velocity);
            vr.currentNote = ev.note;
            vr.currentVelocity = vel;
            vr.voice.trigger (ev.note, vel, ev.legato != 0u);
            break;
        }
        case GARDENIA_CORE_EVENT_VOICE_RELEASE:
            vr.currentNote = -1;
            vr.voice.release();
            break;
        case GARDENIA_CORE_EVENT_VOICE_STOP:
            vr.currentNote = -1;
            vr.voice.kill();
            vr.fuzz.reset();
            break;
        case GARDENIA_CORE_EVENT_PITCH_BEND:
        {
            float n = ev.value_f32;
            if ((ev.flags & GARDENIA_CORE_EVENT_FLAG_PITCH_WHEEL_RAW) != 0u)
                n = pitch_wheel_raw_to_normalized (ev.value_i32);
            vr.voice.setPitchBendNormalized (n);
            break;
        }
        case GARDENIA_CORE_EVENT_CONTROL:
        {
            const float n = gardenia_core::jlimit (0.0f, 1.0f, ev.value_f32);
            switch (ev.control_id)
            {
                case GARDENIA_CORE_CONTROL_MOD_WHEEL:
                    vr.voice.setModWheelNormalized (n);
                    break;
                case GARDENIA_CORE_CONTROL_WHEEL_2:
                    vr.voice.setWheel2Normalized (n);
                    break;
                case GARDENIA_CORE_CONTROL_EXPRESSION:
                    vr.voice.setExpressionNormalized (n);
                    break;
                case GARDENIA_CORE_CONTROL_AFTERTOUCH:
                    vr.voice.setAftertouchNormalized (n);
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

static void render_voices (Instance& inst,
                           gardenia_core_voice_output* outs,
                           uint32_t outCount,
                           uint32_t writeOffset,
                           int numSamples)
{
    for (uint32_t oi = 0; oi < outCount; ++oi)
    {
        auto& out = outs[oi];
        if (out.voice_slot >= inst.voiceCount)
            continue;
        if (out.sample_stride != 0u && out.sample_stride != 1u)
            continue;

        auto& vr = inst.voices[out.voice_slot];
        if (vr.currentNote < 0 && ! vr.voice.isActive() && ! vr.voice.hasTail())
            continue;

        gardenia_core::StageABuffers buf {};
        buf.stem = out.stem;
        buf.noise = out.noise;
        buf.filterEnv = out.filter_envelope;
        buf.lfo1 = out.lfo_1;
        buf.lfo2 = out.lfo_2;
        buf.lfo3 = out.lfo_3;
        buf.voiceLfo = out.voice_lfo;
        buf.cutoffMod = out.cutoff_mod;
        buf.cutoffHz = out.cutoff_hz;
        buf.resonance = out.resonance;
        buf.signalGain = out.signal_gain;
        buf.noiseGain = out.noise_gain;
        buf.fuzzMix = out.fuzz_mix;
        buf.vca = out.vca;
        buf.pitch = out.pitch;
        buf.active = out.active;
        buf.gate = out.gate;
        buf.capacity = out.buffer_capacity;
        buf.writeOffset = writeOffset;

        vr.voice.setStageABuffers (&buf);
        std::fill (inst.scratch.begin(),
                   inst.scratch.begin() + numSamples,
                   0.0f);
        vr.voice.processBlock (inst.scratch.data(), numSamples);
        vr.voice.setStageABuffers (nullptr);

        out.note = (vr.currentNote >= 0) ? static_cast<uint32_t> (vr.currentNote) : 0u;
        out.velocity = vr.currentVelocity;
    }
}

static void process_segmented (Instance& inst,
                               const gardenia_core_event* events,
                               uint32_t eventCount,
                               gardenia_core_voice_output* outs,
                               uint32_t outCount,
                               uint32_t numSamples)
{
    static constexpr int kMinimumSubBlockSize = 32;
    static constexpr bool kSubBlockSubdivisionIsStrict = false;

    // Events already validated sorted.
    uint32_t evIt = 0;
    int startSample = 0;
    int remaining = static_cast<int> (numSamples);
    bool firstEvent = true;

    while (remaining > 0)
    {
        if (evIt >= eventCount)
        {
            render_voices (inst, outs, outCount, static_cast<uint32_t> (startSample), remaining);
            break;
        }

        const int samplesToNext = static_cast<int> (events[evIt].sample_offset) - startSample;

        if (samplesToNext >= remaining)
        {
            render_voices (inst, outs, outCount, static_cast<uint32_t> (startSample), remaining);
            while (evIt < eventCount)
                apply_event (inst, events[evIt++]);
            break;
        }

        const int threshold = (firstEvent && ! kSubBlockSubdivisionIsStrict)
                            ? 1
                            : kMinimumSubBlockSize;
        if (samplesToNext < threshold)
        {
            apply_event (inst, events[evIt++]);
            continue;
        }

        firstEvent = false;
        if (samplesToNext > 0)
            render_voices (inst, outs, outCount, static_cast<uint32_t> (startSample), samplesToNext);
        apply_event (inst, events[evIt++]);
        startSample += samplesToNext;
        remaining -= samplesToNext;
    }

    while (evIt < eventCount)
        apply_event (inst, events[evIt++]);
}

} // namespace gardenia_core_api

/* ========================= Public C API =================================== */

extern "C" {

uint32_t GARDENIA_CORE_CALL gardenia_core_get_abi_version_major (void)
{
    return GARDENIA_CORE_ABI_VERSION_MAJOR;
}

uint32_t GARDENIA_CORE_CALL gardenia_core_get_abi_version_minor (void)
{
    return GARDENIA_CORE_ABI_VERSION_MINOR;
}

uint32_t GARDENIA_CORE_CALL gardenia_core_get_abi_version_patch (void)
{
    return GARDENIA_CORE_ABI_VERSION_PATCH;
}

const char* GARDENIA_CORE_CALL gardenia_core_get_implementation_version (void)
{
    return GARDENIA_CORE_IMPLEMENTATION_VERSION;
}

const char* GARDENIA_CORE_CALL gardenia_core_get_license_identifier (void)
{
    return GARDENIA_CORE_LICENSE_IDENTIFIER;
}

const char* GARDENIA_CORE_CALL gardenia_core_result_string (gardenia_core_result result)
{
    switch (result)
    {
        case GARDENIA_CORE_OK: return "ok";
        case GARDENIA_CORE_ERR_INVALID_ARGUMENT: return "invalid_argument";
        case GARDENIA_CORE_ERR_NULL_INSTANCE: return "null_instance";
        case GARDENIA_CORE_ERR_INCOMPATIBLE_ABI: return "incompatible_abi";
        case GARDENIA_CORE_ERR_UNSUPPORTED: return "unsupported";
        case GARDENIA_CORE_ERR_INVALID_VOICE: return "invalid_voice";
        case GARDENIA_CORE_ERR_INVALID_EVENT: return "invalid_event";
        case GARDENIA_CORE_ERR_INVALID_BUFFER: return "invalid_buffer";
        case GARDENIA_CORE_ERR_INSUFFICIENT_CAPACITY: return "insufficient_capacity";
        case GARDENIA_CORE_ERR_NOT_PREPARED: return "not_prepared";
        case GARDENIA_CORE_ERR_INTERNAL: return "internal_error";
        case GARDENIA_CORE_ERR_ALLOCATION: return "allocation_failure";
        default: return "unknown";
    }
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_get_capabilities (gardenia_core_capabilities* out_caps)
{
    if (out_caps == nullptr)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (out_caps->struct_size < sizeof (gardenia_core_capabilities))
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (! abi_major_ok (out_caps->abi_version))
        return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;

    out_caps->maximum_voice_slots = GARDENIA_CORE_MAX_VOICE_SLOTS;
    out_caps->maximum_block_size = GARDENIA_CORE_MAX_BLOCK_SIZE;
    out_caps->output_flags = GARDENIA_CORE_FEATURE_CONTROL_STREAMS;
    out_caps->feature_flags =
        GARDENIA_CORE_FEATURE_STAGE_A |
        GARDENIA_CORE_FEATURE_STAGE_B_FUZZ |
        GARDENIA_CORE_FEATURE_DETERMINISTIC_SEED |
        GARDENIA_CORE_FEATURE_SAMPLE_OFFSET_EVENTS |
        GARDENIA_CORE_FEATURE_WAVETABLE |
        GARDENIA_CORE_FEATURE_VOWEL |
        GARDENIA_CORE_FEATURE_WAVEEDIT |
        GARDENIA_CORE_FEATURE_MIXER_OPS |
        GARDENIA_CORE_FEATURE_BIT_DOMAIN |
        GARDENIA_CORE_FEATURE_SUB_TRANSIENT |
        GARDENIA_CORE_FEATURE_CONTROL_STREAMS;
    for (uint32_t i = 0; i < 8; ++i)
        out_caps->reserved_u32[i] = 0;
    return GARDENIA_CORE_OK;
}

gardenia_core_instance* GARDENIA_CORE_CALL
gardenia_core_create (const gardenia_core_create_config* config)
{
    try
    {
        if (config == nullptr)
            return nullptr;
        if (config->struct_size < sizeof (gardenia_core_create_config))
            return nullptr;
        if (! abi_major_ok (config->abi_version))
            return nullptr;
        if (! gardenia_core_api::reserved_ok_u32 (config->reserved_u32, 7)
            || ! gardenia_core_api::reserved_ok_u64 (config->reserved_u64, 4))
            return nullptr;
        if (config->maximum_voice_slots == 0
            || config->maximum_voice_slots > GARDENIA_CORE_MAX_VOICE_SLOTS)
            return nullptr;
        if (config->maximum_block_size == 0
            || config->maximum_block_size > GARDENIA_CORE_MAX_BLOCK_SIZE)
            return nullptr;
        if (! (config->sample_rate > 0.0) || config->sample_rate > 192000.0)
            return nullptr;

        auto* inst = new (std::nothrow) gardenia_core_instance();
        if (inst == nullptr)
            return nullptr;

        inst->impl.voiceCount = config->maximum_voice_slots;
        inst->impl.maxBlock = config->maximum_block_size;
        inst->impl.sampleRate = config->sample_rate;
        inst->impl.seed = config->deterministic_seed;
        inst->impl.scratch.assign (config->maximum_block_size, 0.0f);

        for (uint32_t i = 0; i < inst->impl.voiceCount; ++i)
        {
            inst->impl.voices[i].voice.init (config->sample_rate);
            inst->impl.voices[i].fuzz.reset();
        }
        gardenia_core_api::seed_voices (inst->impl, config->deterministic_seed);
        inst->impl.prepared = true;
        return inst;
    }
    catch (...)
    {
        return nullptr;
    }
}

void GARDENIA_CORE_CALL gardenia_core_destroy (gardenia_core_instance* instance)
{
    if (instance == nullptr)
        return;
    try
    {
        delete instance;
    }
    catch (...)
    {
    }
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_prepare (gardenia_core_instance* instance,
                       double sample_rate,
                       uint32_t maximum_block_size)
{
    if (instance == nullptr)
        return GARDENIA_CORE_ERR_NULL_INSTANCE;
    try
    {
        if (! (sample_rate > 0.0) || sample_rate > 192000.0)
            return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
        if (maximum_block_size == 0 || maximum_block_size > GARDENIA_CORE_MAX_BLOCK_SIZE)
            return GARDENIA_CORE_ERR_INVALID_ARGUMENT;

        instance->impl.sampleRate = sample_rate;
        instance->impl.maxBlock = maximum_block_size;
        instance->impl.scratch.assign (maximum_block_size, 0.0f);
        for (uint32_t i = 0; i < instance->impl.voiceCount; ++i)
        {
            instance->impl.voices[i].voice.init (sample_rate);
            instance->impl.voices[i].voice.kill();
            instance->impl.voices[i].fuzz.reset();
            instance->impl.voices[i].currentNote = -1;
        }
        gardenia_core_api::seed_voices (instance->impl, instance->impl.seed);
        instance->impl.prepared = true;
        return GARDENIA_CORE_OK;
    }
    catch (...)
    {
        return GARDENIA_CORE_ERR_INTERNAL;
    }
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_reset (gardenia_core_instance* instance, uint64_t deterministic_seed)
{
    if (instance == nullptr)
        return GARDENIA_CORE_ERR_NULL_INSTANCE;
    if (! instance->impl.prepared)
        return GARDENIA_CORE_ERR_NOT_PREPARED;
    try
    {
        for (uint32_t i = 0; i < instance->impl.voiceCount; ++i)
        {
            instance->impl.voices[i].voice.kill();
            instance->impl.voices[i].fuzz.reset();
            instance->impl.voices[i].currentNote = -1;
            instance->impl.voices[i].currentVelocity = 0.0f;
        }
        gardenia_core_api::seed_voices (instance->impl, deterministic_seed);
        return GARDENIA_CORE_OK;
    }
    catch (...)
    {
        return GARDENIA_CORE_ERR_INTERNAL;
    }
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_snapshot_init_defaults (gardenia_core_synthesis_snapshot* snapshot)
{
    if (snapshot == nullptr)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (snapshot->struct_size < sizeof (gardenia_core_synthesis_snapshot))
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (! abi_major_ok (snapshot->abi_version))
        return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;

    gardenia_core::Patch p;
    p.osc[0].shape = gardenia_core::WAVEFORM_SAW;
    p.osc[1].shape = gardenia_core::WAVEFORM_SQUARE;
    p.mix_balance = 32;
    p.mix_op = gardenia_core::OP_SUM;
    p.filter_type = gardenia_core::FILTER_TYPE_WDF;
    p.filter[0].cutoff = 127;
    p.filter[0].resonance = 0;
    p.filter_env = 0;

    gardenia_core::Part part;
    part.volume = 100;
    part.legato = 0;
    part.portamento_time = 0;

    gardenia_core_api::blob_from_patch (*snapshot, p, part);
    snapshot->tempo_bpm = 120.0f;
    snapshot->wavetable_model = 0;
    snapshot->delay_bypass = 0;
    snapshot->reverb_bypass = 0;
    for (uint32_t i = 0; i < 8; ++i)
        snapshot->reserved_u32[i] = 0;
    for (uint32_t i = 0; i < 4; ++i)
        snapshot->reserved_u64[i] = 0;
    return GARDENIA_CORE_OK;
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_snapshot_set_param (gardenia_core_synthesis_snapshot* snapshot,
                                  gardenia_core_param_id param,
                                  float value)
{
    if (snapshot == nullptr)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (snapshot->struct_size < sizeof (gardenia_core_synthesis_snapshot))
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (! abi_major_ok (snapshot->abi_version))
        return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;

    gardenia_core::Patch p;
    gardenia_core::Part part;
    gardenia_core_api::blob_to_patch (*snapshot, p, part);

    auto u8 = [] (float v) -> uint8_t {
        return static_cast<uint8_t> (gardenia_core::jlimit (0, 127, gardenia_core::round_to_int (v)));
    };

    switch (param)
    {
        case GARDENIA_CORE_PARAM_OSC1_SHAPE: p.osc[0].shape = u8 (value); break;
        case GARDENIA_CORE_PARAM_OSC1_PARAMETER: p.osc[0].parameter = u8 (value); break;
        case GARDENIA_CORE_PARAM_OSC2_SHAPE: p.osc[1].shape = u8 (value); break;
        case GARDENIA_CORE_PARAM_MIX_BALANCE: p.mix_balance = u8 (value); break;
        case GARDENIA_CORE_PARAM_MIX_OP: p.mix_op = u8 (value); break;
        case GARDENIA_CORE_PARAM_MIX_PARAMETER: p.mix_parameter = u8 (value); break;
        case GARDENIA_CORE_PARAM_MIX_NOISE: p.mix_noise = u8 (value); break;
        case GARDENIA_CORE_PARAM_MIX_CRUSH: p.mix_crush = u8 (value); break;
        case GARDENIA_CORE_PARAM_FILTER_CUTOFF: p.filter[0].cutoff = u8 (value); break;
        case GARDENIA_CORE_PARAM_FILTER_RESONANCE: p.filter[0].resonance = u8 (value); break;
        case GARDENIA_CORE_PARAM_FILTER_ENV:
            p.filter_env = static_cast<int8_t> (gardenia_core::jlimit (-127, 127, gardenia_core::round_to_int (value)));
            break;
        case GARDENIA_CORE_PARAM_ENV1_ATTACK: p.env_lfo[0].attack = u8 (value); break;
        case GARDENIA_CORE_PARAM_ENV1_DECAY: p.env_lfo[0].decay = u8 (value); break;
        case GARDENIA_CORE_PARAM_ENV1_SUSTAIN: p.env_lfo[0].sustain = u8 (value); break;
        case GARDENIA_CORE_PARAM_ENV1_RELEASE: p.env_lfo[0].release = u8 (value); break;
        case GARDENIA_CORE_PARAM_PART_VOLUME: part.volume = u8 (value); break;
        case GARDENIA_CORE_PARAM_PART_LEGATO: part.legato = u8 (value); break;
        case GARDENIA_CORE_PARAM_PART_PORTAMENTO: part.portamento_time = u8 (value); break;
        case GARDENIA_CORE_PARAM_TEMPO_BPM: snapshot->tempo_bpm = value; break;
        case GARDENIA_CORE_PARAM_WAVETABLE_MODEL:
            snapshot->wavetable_model = static_cast<uint32_t> (gardenia_core::jlimit (0, 255, gardenia_core::round_to_int (value)));
            break;
        default:
            return GARDENIA_CORE_ERR_UNSUPPORTED;
    }

    gardenia_core_api::blob_from_patch (*snapshot, p, part);
    return GARDENIA_CORE_OK;
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_process_stage_a (gardenia_core_instance* instance,
                               const gardenia_core_synthesis_snapshot* snapshot,
                               const gardenia_core_event* events,
                               uint32_t event_count,
                               gardenia_core_voice_output* voice_outputs,
                               uint32_t voice_output_count,
                               uint32_t num_samples)
{
    if (instance == nullptr)
        return GARDENIA_CORE_ERR_NULL_INSTANCE;
    if (! instance->impl.prepared)
        return GARDENIA_CORE_ERR_NOT_PREPARED;
    if (snapshot == nullptr || voice_outputs == nullptr)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (event_count > 0 && events == nullptr)
        return GARDENIA_CORE_ERR_INVALID_EVENT;
    if (num_samples > instance->impl.maxBlock)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;

    try
    {
        if (snapshot->struct_size < sizeof (gardenia_core_synthesis_snapshot)
            || ! abi_major_ok (snapshot->abi_version))
            return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;
        if (! gardenia_core_api::reserved_ok_u32 (snapshot->reserved_u32, 8)
            || ! gardenia_core_api::reserved_ok_u64 (snapshot->reserved_u64, 4))
            return GARDENIA_CORE_ERR_INVALID_ARGUMENT;

        for (uint32_t i = 0; i < voice_output_count; ++i)
        {
            auto& o = voice_outputs[i];
            if (o.struct_size < sizeof (gardenia_core_voice_output) || ! abi_major_ok (o.abi_version))
                return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;
            if (o.voice_slot >= instance->impl.voiceCount)
                return GARDENIA_CORE_ERR_INVALID_VOICE;
            if (o.buffer_capacity < num_samples)
                return GARDENIA_CORE_ERR_INSUFFICIENT_CAPACITY;
            if (o.sample_stride != 0u && o.sample_stride != 1u)
                return GARDENIA_CORE_ERR_INVALID_BUFFER;
        }

        for (uint32_t i = 0; i < event_count; ++i)
        {
            const auto& e = events[i];
            if (e.struct_size < sizeof (gardenia_core_event) || ! abi_major_ok (e.abi_version))
                return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;
            if (e.voice_slot >= instance->impl.voiceCount)
                return GARDENIA_CORE_ERR_INVALID_VOICE;
            if (e.sample_offset > num_samples)
                return GARDENIA_CORE_ERR_INVALID_EVENT;
            if (e.type > GARDENIA_CORE_EVENT_CONTROL)
                return GARDENIA_CORE_ERR_INVALID_EVENT;
            if (i > 0 && events[i].sample_offset < events[i - 1].sample_offset)
                return GARDENIA_CORE_ERR_INVALID_EVENT;
        }

        ScopedNoDenormals nd;
        gardenia_core_api::apply_snapshot_to_voices (instance->impl, *snapshot);

        if (num_samples == 0)
        {
            for (uint32_t i = 0; i < event_count; ++i)
                gardenia_core_api::apply_event (instance->impl, events[i]);
            return GARDENIA_CORE_OK;
        }

        gardenia_core_api::process_segmented (instance->impl, events, event_count,
                                              voice_outputs, voice_output_count, num_samples);
        return GARDENIA_CORE_OK;
    }
    catch (...)
    {
        return GARDENIA_CORE_ERR_INTERNAL;
    }
}

gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_process_stage_b_fuzz (gardenia_core_instance* instance,
                                    gardenia_core_fuzz_io* voice_io,
                                    uint32_t voice_io_count,
                                    uint32_t num_samples)
{
    if (instance == nullptr)
        return GARDENIA_CORE_ERR_NULL_INSTANCE;
    if (! instance->impl.prepared)
        return GARDENIA_CORE_ERR_NOT_PREPARED;
    if (voice_io_count > 0 && voice_io == nullptr)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;
    if (num_samples > instance->impl.maxBlock)
        return GARDENIA_CORE_ERR_INVALID_ARGUMENT;

    try
    {
        ScopedNoDenormals nd;

        for (uint32_t i = 0; i < voice_io_count; ++i)
        {
            auto& io = voice_io[i];
            if (io.struct_size < sizeof (gardenia_core_fuzz_io) || ! abi_major_ok (io.abi_version))
                return GARDENIA_CORE_ERR_INCOMPATIBLE_ABI;
            if (io.voice_slot >= instance->impl.voiceCount)
                return GARDENIA_CORE_ERR_INVALID_VOICE;
            if (io.input == nullptr || io.output == nullptr)
                return GARDENIA_CORE_ERR_INVALID_BUFFER;
            if (io.buffer_capacity < num_samples)
                return GARDENIA_CORE_ERR_INSUFFICIENT_CAPACITY;
            if (io.input_stride > 1u || io.output_stride > 1u || io.control_stride > 1u)
                return GARDENIA_CORE_ERR_INVALID_BUFFER;
            if (io.input == io.output)
                return GARDENIA_CORE_ERR_INVALID_BUFFER;

            const float* amount = (io.mix != nullptr) ? io.mix : io.amount;
            instance->impl.voices[io.voice_slot].fuzz.processBlock (
                io.input, amount, io.output, num_samples);
        }
        return GARDENIA_CORE_OK;
    }
    catch (...)
    {
        return GARDENIA_CORE_ERR_INTERNAL;
    }
}

} // extern "C"
