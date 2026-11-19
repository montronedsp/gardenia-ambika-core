/*
  Patch.h — Ambika-derived patch and part data structures for Gardenia Ambika Core.
*/

#pragma once
#include <cstdint>
#include <cstring>


#ifdef _WIN32
#undef Patch
#undef Part
#endif

namespace gardenia_core
{

// Oscillator settings
struct OscillatorSettings
{
    uint8_t shape = 0;
    uint8_t parameter = 0;
    int8_t range = 0;
    int8_t detune = 0;
};

// Filter settings (for CV output, not digital filter)
struct FilterSettings
{
    uint8_t cutoff = 127;
    uint8_t resonance = 0;
    uint8_t mode = 0;
};

// Envelope/LFO settings
struct EnvelopeLfoSettings
{
    uint8_t attack = 0;
    uint8_t decay = 40;
    uint8_t sustain = 20;
    uint8_t release = 60;
    uint8_t shape = 0;
    uint8_t rate = 16;
    uint8_t sync_enabled = 0;   // 0 = free-running (Hz), 1 = tempo-synced
    uint8_t sync_division = 3;  // TempoDivision: 3 = 1/4 note default
    uint8_t retrigger_mode = 0;
};

// Modulation routing
struct Modulation
{
    uint8_t source = 0;
    uint8_t destination = 0;
    int8_t amount = 0;
};

// Modifier
struct Modifier
{
    uint8_t operands[2] = { 0, 0 };
    uint8_t op = 0;
};

// Oscillator algorithms (from patch.h)
enum OscillatorAlgorithm
{
    WAVEFORM_NONE = 0,
    WAVEFORM_SAW,
    WAVEFORM_SQUARE,
    WAVEFORM_TRIANGLE,
    WAVEFORM_SINE,
    WAVEFORM_CZ_SAW,
    WAVEFORM_CZ_SAW_LP,
    WAVEFORM_CZ_SAW_PK,
    WAVEFORM_CZ_SAW_BP,
    WAVEFORM_CZ_SAW_HP,
    WAVEFORM_CZ_PLS_LP,
    WAVEFORM_CZ_PLS_PK,
    WAVEFORM_CZ_PLS_BP,
    WAVEFORM_CZ_PLS_HP,
    WAVEFORM_CZ_TRI_LP,
    WAVEFORM_QUAD_SAW_PAD,
    WAVEFORM_FM,
    WAVEFORM_8BITLAND,
    WAVEFORM_DIRTY_PWM,
    WAVEFORM_FILTERED_NOISE,
    WAVEFORM_VOWEL,
    WAVEFORM_WAVETABLE_1,
    WAVEFORM_WAVETABLE_16 = WAVEFORM_WAVETABLE_1 - 1 + 16,
    WAVEFORM_WAVEQUENCE,
    WAVEFORM_LAST
};

// Wavetable model selector.
// Kept outside Patch so historic raw Patch/Part state payload sizes remain stable.
enum WavetableModel
{
    WT_MODEL_AMBIKA = 0,
    WT_MODEL_WAVEEDIT_MODERN,
    WT_MODEL_LAST
};

// Sub-oscillator algorithms
enum SubOscillatorAlgorithm
{
    WAVEFORM_SUB_OSC_SQUARE_1 = 0,
    WAVEFORM_SUB_OSC_TRIANGLE_1,
    WAVEFORM_SUB_OSC_PULSE_1,
    WAVEFORM_SUB_OSC_SQUARE_2,
    WAVEFORM_SUB_OSC_TRIANGLE_2,
    WAVEFORM_SUB_OSC_PULSE_2,
    WAVEFORM_SUB_OSC_CLICK,
    WAVEFORM_SUB_OSC_GLITCH,
    WAVEFORM_SUB_OSC_BLOW,
    WAVEFORM_SUB_OSC_METALLIC,
    WAVEFORM_SUB_OSC_POP,
    WAVEFORM_SUB_OSC_LAST
};

// Mix operators
enum Operator
{
    OP_SUM = 0,
    OP_SYNC,
    OP_RING_MOD,
    OP_XOR,
    OP_FOLD,
    OP_BITS,
    OP_FM,
    OP_LAST
};

// Modifier operations
enum ModifierOp
{
    MODIFIER_NONE = 0,
    MODIFIER_SUM,
    MODIFIER_PRODUCT,
    MODIFIER_ATTENUATE,
    MODIFIER_MAX,
    MODIFIER_MIN,
    MODIFIER_XOR,
    MODIFIER_GE,
    MODIFIER_LE,
    MODIFIER_QUANTIZE,
    MODIFIER_LAG_PROCESSOR,
    MODIFIER_LAST
};

// LFO waveforms
enum LfoWave
{
    LFO_WAVEFORM_TRIANGLE = 0,
    LFO_WAVEFORM_SQUARE,
    LFO_WAVEFORM_S_H,
    LFO_WAVEFORM_RAMP,
    LFO_WAVEFORM_WAVE_1,
    LFO_WAVEFORM_WAVE_2,
    LFO_WAVEFORM_WAVE_3,
    LFO_WAVEFORM_WAVE_4,
    LFO_WAVEFORM_WAVE_5,
    LFO_WAVEFORM_WAVE_6,
    LFO_WAVEFORM_WAVE_7,
    LFO_WAVEFORM_WAVE_8,
    LFO_WAVEFORM_WAVE_9,
    LFO_WAVEFORM_WAVE_10,
    LFO_WAVEFORM_WAVE_11,
    LFO_WAVEFORM_WAVE_12,
    LFO_WAVEFORM_WAVE_13,
    LFO_WAVEFORM_WAVE_14,
    LFO_WAVEFORM_WAVE_15,
    LFO_WAVEFORM_WAVE_16,
    LFO_WAVEFORM_LAST
};

// LFO retrigger modes
enum LfoRetriggerMode
{
    LFO_RETRIGGER_NONE = 0,
    LFO_RETRIGGER_ON_NOTE,
    LFO_RETRIGGER_ON_GATE
};

// Modulation sources
enum ModulationSource
{
    MOD_SRC_ENV_1 = 0,
    MOD_SRC_ENV_2,
    MOD_SRC_ENV_3,
    MOD_SRC_ENV_4,
    MOD_SRC_LFO_1,
    MOD_SRC_LFO_2,
    MOD_SRC_LFO_3,
    MOD_SRC_LFO_4,
    MOD_SRC_OP_1,
    MOD_SRC_OP_2,
    MOD_SRC_OP_3,
    MOD_SRC_OP_4,
    MOD_SRC_SEQ_1,
    MOD_SRC_SEQ_2,
    MOD_SRC_ARP_STEP,
    MOD_SRC_VELOCITY,
    MOD_SRC_AFTERTOUCH,
    MOD_SRC_PITCH_BEND,
    MOD_SRC_WHEEL,
    MOD_SRC_WHEEL_2,
    MOD_SRC_EXPRESSION,
    MOD_SRC_NOTE,
    MOD_SRC_GATE,
    MOD_SRC_NOISE,
    MOD_SRC_RANDOM,
    MOD_SRC_CONSTANT_256,
    MOD_SRC_CONSTANT_128,
    MOD_SRC_CONSTANT_64,
    MOD_SRC_CONSTANT_32,
    MOD_SRC_CONSTANT_16,
    MOD_SRC_CONSTANT_8,
    MOD_SRC_CONSTANT_4,
    MOD_SRC_LAST
};

// Modulation destinations
enum ModulationDestination
{
    MOD_DST_PARAMETER_1 = 0,
    MOD_DST_PARAMETER_2,
    MOD_DST_OSC_1,
    MOD_DST_OSC_2,
    MOD_DST_OSC_1_2_COARSE,
    MOD_DST_OSC_1_2_FINE,
    MOD_DST_MIX_BALANCE,
    MOD_DST_MIX_PARAM,
    MOD_DST_MIX_NOISE,
    MOD_DST_MIX_SUB_OSC,
    MOD_DST_MIX_FUZZ,
    MOD_DST_MIX_CRUSH,
    MOD_DST_FILTER_CUTOFF,
    MOD_DST_FILTER_RESONANCE,
    MOD_DST_ATTACK,
    MOD_DST_DECAY,
    MOD_DST_RELEASE,
    MOD_DST_LFO_4,
    MOD_DST_VCA,
    MOD_DST_LAST
};

// Filter modes
enum FilterMode
{
    FILTER_MODE_LP = 0,
    FILTER_MODE_BP,
    FILTER_MODE_HP,
    FILTER_MODE_NOTCH
};

// Filter types (for unified filter interface)
enum FilterType
{
    FILTER_TYPE_WDF = 0,
    FILTER_TYPE_ACID303,
    FILTER_TYPE_COUNT
};

// Host-side filter model selectors (values preserved for patch compatibility).
enum WdfFilterModel
{
    WDF_FILTER_MODEL_RC_LOWPASS = 0,
    WDF_FILTER_MODEL_SALLEN_KEY,
    WDF_FILTER_MODEL_COUNT
};

// Constants
static constexpr uint8_t kNumEnvelopes = 5;  // Volume, Filter, AUX ENV 1, AUX ENV 2, and one spare
static constexpr uint8_t kNumLfos = kNumEnvelopes;
static constexpr uint8_t kNumModulations = 14;
static constexpr uint8_t kNumModifiers = 4;
static constexpr uint8_t kNumOscillators = 2;
static constexpr uint8_t kNumModulationSources = MOD_SRC_LAST;
static constexpr uint8_t kNumModulationDestinations = MOD_DST_LAST;

// Complete patch structure
struct Patch
{
    // Oscillators (offset 0-8)
    OscillatorSettings osc[kNumOscillators];
    
    // Mixer (offset 8-16)
    uint8_t mix_balance = 32;
    uint8_t mix_op = OP_SUM;
    uint8_t mix_parameter = 0;
    uint8_t mix_sub_osc_shape = WAVEFORM_SUB_OSC_SQUARE_1;
    uint8_t mix_sub_osc = 0;
    uint8_t mix_noise = 0;
    uint8_t mix_noise_to_filter = 0;  // 0 = bypass filter, 1 = route to filter
    uint8_t mix_fuzz = 0;
    uint8_t mix_crush = 0;
    
    // Filter (offset 16-24)
    FilterSettings filter[2];
    int8_t filter_env = 0;  // Attenuverter: 0 = no modulation, -127 to -1 = negative, 1 to 127 = positive
    int8_t filter_lfo = 0;
    uint8_t filter_type = FILTER_TYPE_WDF; // Host filter type selector
    uint8_t wdf_filter_model = 0; // Host filter model selection
    uint8_t filter_bass_boost = 64; // Host filter bass boost (0-127, default 0.5)
    uint8_t filter_cutoff_extend = 0; // Host filter cutoff range extension
    
    // Envelopes/LFOs (offset 24-48)
    EnvelopeLfoSettings env_lfo[kNumEnvelopes];
    
    // Voice LFO (offset 48-54)
    uint8_t voice_lfo_shape = LFO_WAVEFORM_TRIANGLE;
    uint8_t voice_lfo_rate = 16;
    uint8_t voice_lfo_sync_enabled = 0;
    uint8_t voice_lfo_sync_division = 3;
    
    // Modulation matrix (offset 50-92)
    Modulation modulation[kNumModulations];
    
    // Modifiers (offset 92-104)
    Modifier modifier[kNumModifiers];
    
    // Delay tempo sync
    uint8_t delay_sync_enabled = 0;
    uint8_t delay_sync_division = 3;
    uint8_t delay_lfo_sync_enabled = 0;
    uint8_t delay_lfo_sync_division = 3;

    // Padding (offset 108-112)
    uint8_t padding[2] = { 0 };
};

// Part settings (per-voice)
struct Part
{
    uint8_t volume = 127;
    uint8_t padding[4] = { 0 };
    uint8_t legato = 0;
    uint8_t portamento_time = 0;
};

} // namespace gardenia_core
