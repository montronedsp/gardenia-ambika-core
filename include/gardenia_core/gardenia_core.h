/*
  gardenia_core.h — Public plain-C ABI for Gardenia Ambika Core (ABI v1).

  Authority for public consumers. Does not expose C++ classes, STL, or JUCE.
  Voice allocation remains host-side; this API uses fixed voice slots.
*/
#ifndef GARDENIA_CORE_H
#define GARDENIA_CORE_H

#include <stdint.h>
#include "gardenia_core/gardenia_core_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Export / calling convention                                                */
/* -------------------------------------------------------------------------- */

#if defined(_WIN32) || defined(_WIN64)
#  define GARDENIA_CORE_CALL __cdecl
#  if defined(GARDENIA_CORE_STATIC)
#    define GARDENIA_CORE_API
#  elif defined(GARDENIA_CORE_BUILD_SHARED)
#    define GARDENIA_CORE_API __declspec(dllexport)
#  else
#    define GARDENIA_CORE_API __declspec(dllimport)
#  endif
#else
#  define GARDENIA_CORE_CALL
#  if defined(GARDENIA_CORE_BUILD_SHARED)
#    define GARDENIA_CORE_API __attribute__((visibility("default")))
#  else
#    define GARDENIA_CORE_API
#  endif
#endif

/* -------------------------------------------------------------------------- */
/* Limits                                                                     */
/* -------------------------------------------------------------------------- */

#define GARDENIA_CORE_MAX_VOICE_SLOTS       8u
#define GARDENIA_CORE_MAX_BLOCK_SIZE        2048u
#define GARDENIA_CORE_PATCH_BLOB_BYTES      256u
#define GARDENIA_CORE_PART_BLOB_BYTES       16u

/* -------------------------------------------------------------------------- */
/* Results                                                                    */
/* -------------------------------------------------------------------------- */

typedef int32_t gardenia_core_result;

#define GARDENIA_CORE_OK                     ((gardenia_core_result)0)
#define GARDENIA_CORE_ERR_INVALID_ARGUMENT   ((gardenia_core_result)-1)
#define GARDENIA_CORE_ERR_NULL_INSTANCE      ((gardenia_core_result)-2)
#define GARDENIA_CORE_ERR_INCOMPATIBLE_ABI   ((gardenia_core_result)-3)
#define GARDENIA_CORE_ERR_UNSUPPORTED        ((gardenia_core_result)-4)
#define GARDENIA_CORE_ERR_INVALID_VOICE      ((gardenia_core_result)-5)
#define GARDENIA_CORE_ERR_INVALID_EVENT      ((gardenia_core_result)-6)
#define GARDENIA_CORE_ERR_INVALID_BUFFER     ((gardenia_core_result)-7)
#define GARDENIA_CORE_ERR_INSUFFICIENT_CAPACITY ((gardenia_core_result)-8)
#define GARDENIA_CORE_ERR_NOT_PREPARED       ((gardenia_core_result)-9)
#define GARDENIA_CORE_ERR_INTERNAL           ((gardenia_core_result)-10)
#define GARDENIA_CORE_ERR_ALLOCATION         ((gardenia_core_result)-11)

/* -------------------------------------------------------------------------- */
/* Feature / output flags                                                     */
/* -------------------------------------------------------------------------- */

#define GARDENIA_CORE_FEATURE_STAGE_A            (1u << 0)
#define GARDENIA_CORE_FEATURE_STAGE_B_FUZZ       (1u << 1)
#define GARDENIA_CORE_FEATURE_DETERMINISTIC_SEED (1u << 2)
#define GARDENIA_CORE_FEATURE_SAMPLE_OFFSET_EVENTS (1u << 3)
#define GARDENIA_CORE_FEATURE_WAVETABLE          (1u << 4)
#define GARDENIA_CORE_FEATURE_VOWEL              (1u << 5)
#define GARDENIA_CORE_FEATURE_WAVEEDIT           (1u << 6)
#define GARDENIA_CORE_FEATURE_MIXER_OPS          (1u << 7)
#define GARDENIA_CORE_FEATURE_BIT_DOMAIN         (1u << 8)
#define GARDENIA_CORE_FEATURE_SUB_TRANSIENT      (1u << 9)
#define GARDENIA_CORE_FEATURE_CONTROL_STREAMS    (1u << 10)

#define GARDENIA_CORE_CREATE_FLAG_NONE           0u

/* -------------------------------------------------------------------------- */
/* Opaque instance                                                            */
/* -------------------------------------------------------------------------- */

typedef struct gardenia_core_instance gardenia_core_instance;

/* -------------------------------------------------------------------------- */
/* Create / capabilities                                                      */
/* -------------------------------------------------------------------------- */

typedef struct gardenia_core_create_config {
    uint32_t struct_size;
    uint32_t abi_version;

    uint32_t maximum_voice_slots;   /* ABI v1: must be 1..8 */
    uint32_t maximum_block_size;    /* ABI v1: 1..2048 */
    double   sample_rate;           /* Hz, must be > 0 */
    uint64_t deterministic_seed;

    uint32_t flags;
    uint32_t reserved_u32[7];
    uint64_t reserved_u64[4];
} gardenia_core_create_config;

typedef struct gardenia_core_capabilities {
    uint32_t struct_size;
    uint32_t abi_version;

    uint32_t maximum_voice_slots;
    uint32_t maximum_block_size;
    uint32_t output_flags;
    uint32_t feature_flags;

    uint32_t reserved_u32[8];
} gardenia_core_capabilities;

/* -------------------------------------------------------------------------- */
/* Synthesis snapshot (core-owned params only)                                */
/* -------------------------------------------------------------------------- */
/*
  patch_blob / part_blob store the binary layout of the internal Patch / Part
  for ABI major 1. Use gardenia_core_snapshot_init_defaults() then mutate via
  parameter IDs, or fill blobs from a host-side converter.
*/

typedef struct gardenia_core_synthesis_snapshot {
    uint32_t struct_size;
    uint32_t abi_version;

    uint8_t  patch_blob[GARDENIA_CORE_PATCH_BLOB_BYTES];
    uint8_t  part_blob[GARDENIA_CORE_PART_BLOB_BYTES];

    float    tempo_bpm;
    uint32_t wavetable_model; /* 0 Ambika, 1 WaveEdit modern, … */
    uint32_t delay_bypass;    /* 0/1 — sticky-tail proxy only; no delay DSP */
    uint32_t reverb_bypass;   /* 0/1 */

    uint32_t reserved_u32[8];
    uint64_t reserved_u64[4];
} gardenia_core_synthesis_snapshot;

/* Stable parameter IDs for introspection / partial updates (ABI v1 subset). */
typedef uint32_t gardenia_core_param_id;
#define GARDENIA_CORE_PARAM_OSC1_SHAPE          1u
#define GARDENIA_CORE_PARAM_OSC1_PARAMETER      2u
#define GARDENIA_CORE_PARAM_OSC2_SHAPE          3u
#define GARDENIA_CORE_PARAM_MIX_BALANCE         4u
#define GARDENIA_CORE_PARAM_MIX_OP              5u
#define GARDENIA_CORE_PARAM_MIX_PARAMETER       6u
#define GARDENIA_CORE_PARAM_MIX_NOISE           7u
#define GARDENIA_CORE_PARAM_MIX_CRUSH           8u
#define GARDENIA_CORE_PARAM_FILTER_CUTOFF       9u
#define GARDENIA_CORE_PARAM_FILTER_RESONANCE    10u
#define GARDENIA_CORE_PARAM_FILTER_ENV          11u
#define GARDENIA_CORE_PARAM_ENV1_ATTACK         12u
#define GARDENIA_CORE_PARAM_ENV1_DECAY          13u
#define GARDENIA_CORE_PARAM_ENV1_SUSTAIN        14u
#define GARDENIA_CORE_PARAM_ENV1_RELEASE        15u
#define GARDENIA_CORE_PARAM_PART_VOLUME         16u
#define GARDENIA_CORE_PARAM_PART_LEGATO         17u
#define GARDENIA_CORE_PARAM_PART_PORTAMENTO     18u
#define GARDENIA_CORE_PARAM_TEMPO_BPM           19u
#define GARDENIA_CORE_PARAM_WAVETABLE_MODEL     20u

/* -------------------------------------------------------------------------- */
/* Events — host selects target voice slots                                   */
/* -------------------------------------------------------------------------- */

typedef uint32_t gardenia_core_event_type;
#define GARDENIA_CORE_EVENT_NONE            0u
#define GARDENIA_CORE_EVENT_VOICE_START     1u
#define GARDENIA_CORE_EVENT_VOICE_RELEASE   2u
#define GARDENIA_CORE_EVENT_VOICE_STOP      3u
#define GARDENIA_CORE_EVENT_PITCH_BEND      4u
#define GARDENIA_CORE_EVENT_CONTROL         5u

/* Control IDs for EVENT_CONTROL (normalized float in value_f32). */
#define GARDENIA_CORE_CONTROL_MOD_WHEEL     1u
#define GARDENIA_CORE_CONTROL_WHEEL_2       2u
#define GARDENIA_CORE_CONTROL_EXPRESSION    3u
#define GARDENIA_CORE_CONTROL_AFTERTOUCH    4u

typedef struct gardenia_core_event {
    uint32_t struct_size;
    uint32_t abi_version;

    uint32_t type;
    uint32_t sample_offset;   /* 0 .. num_samples (num_samples = after last sample) */
    uint32_t voice_slot;

    int32_t  note;            /* MIDI note for start; unused otherwise */
    float    velocity;        /* 0..1 for start */
    uint32_t legato;          /* 0/1 for start */
    uint32_t control_id;      /* for CONTROL / PITCH_BEND unused */
    float    value_f32;       /* pitch bend: normalized [0,1] centre 0.5; controls: normalized */
    int32_t  value_i32;       /* optional raw 0..16383 pitch wheel if flags say so */

    uint32_t flags;
    uint32_t reserved_u32[7];
    uint64_t reserved_u64[2];
} gardenia_core_event;

#define GARDENIA_CORE_EVENT_FLAG_PITCH_WHEEL_RAW (1u << 0) /* use value_i32 0..16383 */

/* -------------------------------------------------------------------------- */
/* Stage-A voice output (caller-owned buffers)                                */
/* -------------------------------------------------------------------------- */

typedef struct gardenia_core_voice_output {
    uint32_t struct_size;
    uint32_t abi_version;

    uint32_t voice_slot;
    uint32_t flags;

    float* stem;
    float* noise;
    float* filter_envelope;
    float* lfo_1;
    float* lfo_2;
    float* lfo_3;
    float* voice_lfo;
    float* cutoff_mod;
    float* cutoff_hz;
    float* resonance;
    float* signal_gain;
    float* noise_gain;
    float* fuzz_mix;
    float* vca;
    float* pitch;
    uint8_t* active;
    uint8_t* gate;

    uint32_t buffer_capacity;
    uint32_t sample_stride; /* 1 = planar contiguous; >1 reserved */

    uint32_t note;          /* out: last note assigned by host events (informational) */
    float    velocity;

    uint32_t reserved_u32[8];
    uint64_t reserved_u64[4];
} gardenia_core_voice_output;

/* -------------------------------------------------------------------------- */
/* Stage-B fuzz I/O                                                           */
/* -------------------------------------------------------------------------- */

typedef struct gardenia_core_fuzz_io {
    uint32_t struct_size;
    uint32_t abi_version;

    uint32_t voice_slot;
    uint32_t flags;

    const float* input;     /* pre-fuzz input samples */
    const float* amount;    /* per-sample fuzz amount (may be NULL => 0) */
    const float* mix;       /* optional alias of amount; if non-NULL preferred */
    float*       output;

    uint32_t buffer_capacity;
    uint32_t input_stride;
    uint32_t control_stride;
    uint32_t output_stride;

    uint32_t reserved_u32[8];
    uint64_t reserved_u64[4];
} gardenia_core_fuzz_io;

/* -------------------------------------------------------------------------- */
/* Version / capability queries (thread-safe, no allocation)                  */
/* -------------------------------------------------------------------------- */

GARDENIA_CORE_API uint32_t GARDENIA_CORE_CALL
gardenia_core_get_abi_version_major(void);

GARDENIA_CORE_API uint32_t GARDENIA_CORE_CALL
gardenia_core_get_abi_version_minor(void);

GARDENIA_CORE_API uint32_t GARDENIA_CORE_CALL
gardenia_core_get_abi_version_patch(void);

GARDENIA_CORE_API const char* GARDENIA_CORE_CALL
gardenia_core_get_implementation_version(void);

GARDENIA_CORE_API const char* GARDENIA_CORE_CALL
gardenia_core_get_license_identifier(void);

GARDENIA_CORE_API const char* GARDENIA_CORE_CALL
gardenia_core_result_string(gardenia_core_result result);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_get_capabilities(gardenia_core_capabilities* out_caps);

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

GARDENIA_CORE_API gardenia_core_instance* GARDENIA_CORE_CALL
gardenia_core_create(const gardenia_core_create_config* config);

GARDENIA_CORE_API void GARDENIA_CORE_CALL
gardenia_core_destroy(gardenia_core_instance* instance);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_prepare(
    gardenia_core_instance* instance,
    double sample_rate,
    uint32_t maximum_block_size);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_reset(
    gardenia_core_instance* instance,
    uint64_t deterministic_seed);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_snapshot_init_defaults(gardenia_core_synthesis_snapshot* snapshot);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_snapshot_set_param(
    gardenia_core_synthesis_snapshot* snapshot,
    gardenia_core_param_id param,
    float value);

/* -------------------------------------------------------------------------- */
/* Processing (no allocation, no locking, no logging, no exceptions)          */
/* -------------------------------------------------------------------------- */
/*
  Events must be sorted by sample_offset ascending. Equal offsets preserve array order.
  Segmentation matches Phase-3 poly replay: minimumSubBlockSize=32, first-event threshold=1.
  FTZ|DAZ are established for the call and restored before return.
*/

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_process_stage_a(
    gardenia_core_instance* instance,
    const gardenia_core_synthesis_snapshot* snapshot,
    const gardenia_core_event* events,
    uint32_t event_count,
    gardenia_core_voice_output* voice_outputs,
    uint32_t voice_output_count,
    uint32_t num_samples);

GARDENIA_CORE_API gardenia_core_result GARDENIA_CORE_CALL
gardenia_core_process_stage_b_fuzz(
    gardenia_core_instance* instance,
    gardenia_core_fuzz_io* voice_io,
    uint32_t voice_io_count,
    uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* GARDENIA_CORE_H */
