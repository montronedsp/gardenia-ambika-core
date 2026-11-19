/*
  SynthesisGoldenTests — Stage-A CoreVoice vs baseline goldens.
  Pinned: 48000 Hz / 64 / Release baseline-output.
  Allocator/mono/sustain/PB/MW replay is TEST-ONLY (HostReplayAdapter.h).
*/
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "HostReplayAdapter.h"
#include "gardenia_core/Patch.h"

#ifndef GARDENIA_BASELINE_OUTPUT_DIR
#error GARDENIA_BASELINE_OUTPUT_DIR required
#endif

namespace
{

using gardenia_core_test::CcEvent;
using gardenia_core_test::NoteEvent;
using gardenia_core_test::PitchBendEvent;
using gardenia_core_test::StageARender;
using gardenia_core_test::merge_events;
using gardenia_core_test::midi_velocity_roundtrip;
using gardenia_core_test::kSr;

bool bit_eq_f (float a, float b)
{
    uint32_t ua = 0, ub = 0;
    std::memcpy (&ua, &a, sizeof (float));
    std::memcpy (&ub, &b, sizeof (float));
    return ua == ub;
}

bool load_f32 (const std::string& path, std::vector<float>& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    in.seekg (0, std::ios::end);
    const auto bytes = static_cast<size_t> (in.tellg());
    in.seekg (0, std::ios::beg);
    if (bytes % sizeof (float) != 0)
        return false;
    out.resize (bytes / sizeof (float));
    in.read (reinterpret_cast<char*> (out.data()), static_cast<std::streamsize> (bytes));
    return static_cast<bool> (in) || in.eof();
}

bool load_u8 (const std::string& path, std::vector<uint8_t>& out, size_t expectSamples)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    in.seekg (0, std::ios::end);
    const auto bytes = static_cast<size_t> (in.tellg());
    in.seekg (0, std::ios::beg);
    out.resize (bytes);
    in.read (reinterpret_cast<char*> (out.data()), static_cast<std::streamsize> (bytes));
    if (out.size() < expectSamples)
        return false;
    out.resize (expectSamples);
    return true;
}

gardenia_core::Patch make_default_patch()
{
    gardenia_core::Patch p;
    p.osc[0].shape = gardenia_core::WAVEFORM_SAW;
    p.osc[1].shape = gardenia_core::WAVEFORM_SQUARE;
    p.mix_balance = 32;
    p.mix_op = gardenia_core::OP_SUM;
    p.filter_type = gardenia_core::FILTER_TYPE_WDF;
    p.filter[0].cutoff = 127;
    p.filter[0].resonance = 0;
    p.filter_env = 0;
    return p;
}

gardenia_core::Part make_default_part()
{
    gardenia_core::Part part;
    part.volume = 100;
    part.legato = 0;
    part.portamento_time = 0;
    return part;
}

struct DiffResult
{
    bool ok = true;
    size_t index = 0;
    float a = 0.0f;
    float b = 0.0f;
};

DiffResult compare_f32 (const std::vector<float>& got, const std::vector<float>& exp, size_t count)
{
    DiffResult r;
    const size_t n = std::min ({ got.size(), exp.size(), count });
    for (size_t i = 0; i < n; ++i)
    {
        if (! bit_eq_f (got[i], exp[i]))
        {
            r.ok = false;
            r.index = i;
            r.a = got[i];
            r.b = exp[i];
            return r;
        }
    }
    if (got.size() < count || exp.size() < count)
        r.ok = false;
    return r;
}

DiffResult compare_u8 (const std::vector<uint8_t>& got, const std::vector<uint8_t>& exp, size_t count)
{
    DiffResult r;
    const size_t n = std::min ({ got.size(), exp.size(), count });
    for (size_t i = 0; i < n; ++i)
    {
        if (got[i] != exp[i])
        {
            r.ok = false;
            r.index = i;
            r.a = static_cast<float> (got[i]);
            r.b = static_cast<float> (exp[i]);
            return r;
        }
    }
    return r;
}

int check_stream (const char* name, const DiffResult& d, int& pass, int& fail)
{
    if (d.ok)
    {
        std::cout << "PASS " << name << "\n";
        ++pass;
        return 0;
    }
    uint32_t ua = 0, ub = 0;
    std::memcpy (&ua, &d.a, sizeof (float));
    std::memcpy (&ub, &d.b, sizeof (float));
    std::cerr << "FAIL " << name << " firstDiff=" << d.index
              << " core=" << d.a << " baseline=" << d.b
              << " bits=" << std::hex << ua << "/" << ub << std::dec << "\n";
    ++fail;
    return 1;
}

std::string scenario_dir (const char* id)
{
    return std::string (GARDENIA_BASELINE_OUTPUT_DIR) + "/scenarios/" + id + "/sr-48000/bs-64/";
}

struct ScenarioOpts
{
    gardenia_core::Patch patch = make_default_patch();
    gardenia_core::Part part = make_default_part();
    std::vector<NoteEvent> notes;
    std::vector<CcEvent> ccs;
    std::vector<PitchBendEvent> pbs;
    uint32_t samples = 24000;
    bool delayBypass = false;
    bool reverbBypass = false;
    bool mono = false;
    int voiceSlot = 0;
    int voiceSlotEnd = -1; // inclusive; -1 => voiceSlot only
    uint32_t compareSamples = 0;
    uint8_t wavetableModel = gardenia_core::WT_MODEL_AMBIKA;
};

int run_scenario (const char* id, const ScenarioOpts& opt, int& pass, int& fail)
{
    uint32_t compareSamples = opt.compareSamples;
    if (compareSamples == 0 || compareSamples > opt.samples)
        compareSamples = opt.samples;

    StageARender rend;
    rend.prepare (opt.samples, opt.patch, opt.part, opt.delayBypass, opt.reverbBypass,
                  120.0f, opt.wavetableModel, opt.mono);
    rend.render (opt.samples, merge_events (opt.notes, opt.ccs, opt.pbs));

    const int slotLo = opt.voiceSlot;
    const int slotHi = opt.voiceSlotEnd >= 0 ? opt.voiceSlotEnd : opt.voiceSlot;

    for (int voiceSlot = slotLo; voiceSlot <= slotHi; ++voiceSlot)
    {
        const std::string prefix = scenario_dir (id) + "voice-0" + std::to_string (voiceSlot) + "-";
        const std::string tag = std::string (id) + (slotHi > slotLo ? (" v" + std::to_string (voiceSlot)) : "");

        auto load_cmp = [&] (const char* stream, const std::vector<float>& got) {
            std::vector<float> exp;
            if (! load_f32 (prefix + stream + ".f32", exp))
            {
                std::cerr << "FAIL load " << tag << " " << stream << "\n";
                ++fail;
                return;
            }
            check_stream ((tag + " " + stream).c_str(),
                          compare_f32 (got, exp, compareSamples), pass, fail);
        };

        const size_t vi = static_cast<size_t> (voiceSlot);
        load_cmp ("stem", rend.stem[vi]);
        load_cmp ("noise", rend.noise[vi]);
        load_cmp ("filter-env", rend.filterEnv[vi]);
        load_cmp ("lfo1", rend.lfo1[vi]);
        load_cmp ("lfo2", rend.lfo2[vi]);
        load_cmp ("lfo3", rend.lfo3[vi]);
        load_cmp ("voice-lfo", rend.voiceLfo[vi]);
        load_cmp ("cutoff-mod", rend.cutoffMod[vi]);
        load_cmp ("cutoff", rend.cutoffHz[vi]);
        load_cmp ("resonance", rend.resonance[vi]);
        load_cmp ("signal-gain", rend.signalGain[vi]);
        load_cmp ("noise-gain", rend.noiseGain[vi]);
        load_cmp ("fuzz-mix", rend.fuzzMix[vi]);
        load_cmp ("vca", rend.vca[vi]);
        load_cmp ("pitch", rend.pitch[vi]);

        {
            std::vector<uint8_t> exp;
            if (! load_u8 (prefix + "active.u8", exp, opt.samples))
            {
                std::cerr << "FAIL load " << tag << " active\n";
                ++fail;
            }
            else
                check_stream ((tag + " active").c_str(),
                              compare_u8 (rend.active[vi], exp, compareSamples), pass, fail);
        }
        {
            std::vector<uint8_t> exp;
            if (! load_u8 (prefix + "gate.u8", exp, opt.samples))
            {
                std::cerr << "FAIL load " << tag << " gate\n";
                ++fail;
            }
            else
                check_stream ((tag + " gate").c_str(),
                              compare_u8 (rend.gate[vi], exp, compareSamples), pass, fail);
        }
    }
    return 0;
}

} // namespace

int main()
{
    int pass = 0;
    int fail = 0;

    // --- Previously passing Stage-A core ---
    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        run_scenario ("default-single-note", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = {
            { 64, true, 60, 0.8f }, { 64, true, 64, 0.8f }, { 64, true, 67, 0.8f }, { 64, true, 71, 0.8f },
            { 18000, false, 60, 0.0f }, { 18000, false, 64, 0.0f }, { 18000, false, 67, 0.0f }, { 18000, false, 71, 0.0f },
        };
        o.voiceSlotEnd = 3;
        run_scenario ("four-note-chord", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.samples = 12000;
        run_scenario ("silence-no-midi", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch = make_default_patch();
        o.patch.mix_noise = 100;
        o.patch.mix_noise_to_filter = 0;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_NONE;
        o.patch.osc[1].shape = gardenia_core::WAVEFORM_NONE;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("deterministic-noise", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_SAW;
        o.patch.osc[1].shape = gardenia_core::WAVEFORM_NONE;
        o.patch.mix_balance = 0;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("osc-shape-1", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_WAVETABLE_1;
        o.patch.osc[0].parameter = 40;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("ambika-wavetable", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_VOWEL;
        o.patch.osc[0].parameter = 64;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("vowel-formant", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.filter[0].cutoff = 40;
        o.patch.filter_env = 100;
        o.patch.env_lfo[1].attack = 10;
        o.patch.env_lfo[1].decay = 60;
        o.patch.env_lfo[1].sustain = 0;
        o.patch.env_lfo[1].release = 40;
        o.notes = { { 64, true, 48, 0.8f }, { 16000, false, 48, 0.0f } };
        run_scenario ("cutoff-mod-route", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_WAVETABLE_1;
        o.patch.osc[0].parameter = 20;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        o.wavetableModel = gardenia_core::WT_MODEL_WAVEEDIT_MODERN;
        run_scenario ("waveedit-mode", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].attack = 127;
        o.patch.env_lfo[0].decay = 127;
        o.patch.env_lfo[0].sustain = 127;
        o.patch.env_lfo[0].release = 127;
        o.notes = { { 64, true, 60, 0.8f }, { 8000, false, 60, 0.0f } };
        run_scenario ("env-max", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].release = 40;
        o.notes = { { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f } };
        o.delayBypass = true;
        o.reverbBypass = true;
        run_scenario ("release-into-silence", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].release = 100;
        o.notes = { { 64, true, 60, 0.8f }, { 3000, false, 60, 0.0f },
                    { 4500, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("retrigger-during-release", o, pass, fail);
    }

    // Oscillator shape sweep
    {
        const int shapes[] = {
            gardenia_core::WAVEFORM_NONE, gardenia_core::WAVEFORM_SAW, gardenia_core::WAVEFORM_SQUARE,
            gardenia_core::WAVEFORM_TRIANGLE, gardenia_core::WAVEFORM_SINE, gardenia_core::WAVEFORM_CZ_SAW,
            gardenia_core::WAVEFORM_CZ_SAW_LP, gardenia_core::WAVEFORM_FM, gardenia_core::WAVEFORM_8BITLAND,
            gardenia_core::WAVEFORM_DIRTY_PWM, gardenia_core::WAVEFORM_FILTERED_NOISE,
            gardenia_core::WAVEFORM_VOWEL, gardenia_core::WAVEFORM_WAVETABLE_1,
            gardenia_core::WAVEFORM_QUAD_SAW_PAD,
        };
        for (int shape : shapes)
        {
            ScenarioOpts o;
            o.patch.osc[0].shape = static_cast<uint8_t> (shape);
            o.patch.osc[1].shape = gardenia_core::WAVEFORM_NONE;
            o.patch.mix_balance = 0;
            o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
            o.samples = 20000;
            const std::string id = "osc-shape-" + std::to_string (shape);
            run_scenario (id.c_str(), o, pass, fail);
        }
    }

    // --- Allocator / control parity (existing Phase-2 goldens) ---
    {
        ScenarioOpts o;
        o.part.legato = 1;
        o.mono = true;
        o.notes = {
            { 64, true, 60, 0.8f }, { 2000, true, 64, 0.8f }, { 4000, false, 60, 0.0f },
            { 6000, false, 64, 0.0f }, { 8000, true, 67, 0.8f }, { 16000, false, 67, 0.0f },
        };
        o.samples = 20000;
        run_scenario ("mono-overlap-retrigger", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.part.legato = 1;
        o.part.portamento_time = 80;
        o.mono = true;
        o.notes = { { 64, true, 48, 0.8f }, { 3000, true, 72, 0.8f }, { 16000, false, 72, 0.0f } };
        o.samples = 20000;
        run_scenario ("portamento-mono", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = {
            { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f },
            { 16000, true, 62, 0.8f }, { 20000, false, 62, 0.0f },
        };
        o.ccs = { { 2000, 64, 127 }, { 12000, 64, 0 } };
        o.samples = 28000;
        run_scenario ("sustain-pedal", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        o.pbs = { { 2000, 8192 + 2000 }, { 8000, 8192 - 2000 }, { 14000, 8192 } };
        run_scenario ("pitch-bend", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        o.ccs = { { 1000, 1, 0 }, { 4000, 1, 100 }, { 10000, 1, 40 } };
        run_scenario ("mod-wheel", o, pass, fail);
    }
    {
        ScenarioOpts o;
        for (int i = 0; i < 12; ++i)
            o.notes.push_back ({ static_cast<uint32_t> (64 + i * 200), true, 48 + i, 0.8f });
        for (int i = 0; i < 12; ++i)
            o.notes.push_back ({ 20000, false, 48 + i, 0.0f });
        o.samples = 28000;
        o.voiceSlotEnd = 7;
        run_scenario ("force-steal-12-notes", o, pass, fail);
    }

    // Mod routes with matrix
    {
        ScenarioOpts o;
        o.patch.filter[0].resonance = 20;
        o.patch.modulation[0].source = gardenia_core::MOD_SRC_LFO_1;
        o.patch.modulation[0].destination = gardenia_core::MOD_DST_FILTER_RESONANCE;
        o.patch.modulation[0].amount = 80;
        o.patch.env_lfo[0].rate = 90;
        o.notes = { { 64, true, 55, 0.8f }, { 16000, false, 55, 0.0f } };
        o.samples = 20000;
        run_scenario ("resonance-mod-route", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.modulation[0].source = gardenia_core::MOD_SRC_LFO_2;
        o.patch.modulation[0].destination = gardenia_core::MOD_DST_VCA;
        o.patch.modulation[0].amount = 60;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_scenario ("vca-mod-route", o, pass, fail);
    }

    // Mixer / operator matrix
    {
        const int ops[] = {
            gardenia_core::OP_SUM, gardenia_core::OP_SYNC, gardenia_core::OP_RING_MOD,
            gardenia_core::OP_XOR, gardenia_core::OP_FOLD, gardenia_core::OP_BITS, gardenia_core::OP_FM
        };
        for (int op : ops)
        {
            ScenarioOpts o;
            o.patch.mix_op = static_cast<uint8_t> (op);
            o.patch.osc[0].shape = gardenia_core::WAVEFORM_SAW;
            o.patch.osc[1].shape = gardenia_core::WAVEFORM_SQUARE;
            o.patch.mix_balance = 64;
            if (op == gardenia_core::OP_BITS)
                o.patch.mix_crush = 80;
            if (op == gardenia_core::OP_FOLD || op == gardenia_core::OP_FM)
                o.patch.mix_parameter = 64;
            o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
            o.samples = 20000;
            const std::string id = "mix-op-" + std::to_string (op);
            run_scenario (id.c_str(), o, pass, fail);
        }
    }

    // LFO shape matrix
    {
        const int shapes[] = {
            gardenia_core::LFO_WAVEFORM_TRIANGLE, gardenia_core::LFO_WAVEFORM_SQUARE,
            gardenia_core::LFO_WAVEFORM_S_H, gardenia_core::LFO_WAVEFORM_RAMP,
            gardenia_core::LFO_WAVEFORM_WAVE_1, gardenia_core::LFO_WAVEFORM_WAVE_8
        };
        for (int shape : shapes)
        {
            ScenarioOpts o;
            o.patch.env_lfo[0].shape = static_cast<uint8_t> (shape);
            o.patch.env_lfo[0].rate = 80;
            o.patch.modulation[0].source = gardenia_core::MOD_SRC_LFO_1;
            o.patch.modulation[0].destination = gardenia_core::MOD_DST_OSC_1;
            o.patch.modulation[0].amount = 40;
            o.notes = { { 64, true, 48, 0.8f }, { 16000, false, 48, 0.0f } };
            o.samples = 20000;
            const std::string id = "lfo-shape-" + std::to_string (shape);
            run_scenario (id.c_str(), o, pass, fail);
        }
    }

    // env-min with FTZ/DAZ (ScopedNoDenormals in adapter)
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].attack = 0;
        o.patch.env_lfo[0].decay = 0;
        o.patch.env_lfo[0].sustain = 0;
        o.patch.env_lfo[0].release = 0;
        o.notes = { { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f } };
        o.samples = 12000;
        run_scenario ("env-min", o, pass, fail);
    }

    // Event-offset gate matrix (self-check)
    {
        const uint32_t offsets[] = { 0u, 1u, 16u, 31u, 63u };
        for (uint32_t off : offsets)
        {
            const uint32_t blockStart = 17984u;
            const uint32_t noteOffAbs = blockStart + off;
            StageARender rend;
            rend.prepare (noteOffAbs + 128u, make_default_patch(), make_default_part(), false, false, 120.0f);
            std::vector<NoteEvent> notes = { { 64, true, 60, 0.8f }, { noteOffAbs, false, 60, 0.0f } };
            rend.render (noteOffAbs + 128u, merge_events (notes, {}, {}));
            const auto& g = rend.gate[0];
            bool ok = true;
            if (noteOffAbs > 0 && (g[noteOffAbs - 1] != 1 || g[noteOffAbs] != 0))
                ok = false;
            if (noteOffAbs == 0 && g[0] != 0)
                ok = false;
            if (! ok)
            {
                std::cerr << "FAIL note-off-offset-" << off << "\n";
                ++fail;
            }
            else
            {
                std::cout << "PASS note-off-offset-" << off << "\n";
                ++pass;
            }
        }
        {
            StageARender rend;
            rend.prepare (256, make_default_patch(), make_default_part(), false, false, 120.0f);
            rend.render (256, merge_events ({{ 16, true, 60, 0.8f }, { 48, false, 60, 0.0f }}, {}, {}));
            const auto& g = rend.gate[0];
            const bool ok = g[15] == 0 && g[16] == 1 && g[47] == 1 && g[48] == 0;
            if (! ok) { std::cerr << "FAIL same-block-note-on-off\n"; ++fail; }
            else { std::cout << "PASS same-block-note-on-off\n"; ++pass; }
        }
        {
            StageARender rend;
            rend.prepare (256, make_default_patch(), make_default_part(), false, false, 120.0f);
            rend.render (256, merge_events ({
                { 0, true, 60, 0.8f }, { 0, true, 64, 0.8f },
                { 80, false, 60, 0.0f }, { 80, false, 64, 0.0f },
            }, {}, {}));
            const bool ok = rend.gate[0][79] == 1 && rend.gate[0][80] == 0
                         && rend.gate[1][79] == 1 && rend.gate[1][80] == 0;
            if (! ok) { std::cerr << "FAIL same-offset-dual-note-off\n"; ++fail; }
            else { std::cout << "PASS same-offset-dual-note-off\n"; ++pass; }
        }
    }

    // Call-boundary: noise diverges for 64 vs 16+48
    {
        gardenia_core::CoreVoice a, b;
        auto patch = make_default_patch();
        auto part = make_default_part();
        std::vector<float> noiseA (64, 0.0f), noiseB (64, 0.0f);
        for (auto* v : { &a, &b })
        {
            v->init (kSr);
            v->setPatch (patch);
            v->setPart (part);
            v->setTempoBpm (120.0f);
            v->setDelayBypass (false);
            v->setReverbBypass (false);
            v->kill();
            v->setRandomSeed (0x9e3779b9u);
            v->resetAllControllers();
            v->trigger (60, midi_velocity_roundtrip (0.8f), false);
        }
        gardenia_core::StageABuffers bufA, bufB;
        bufA.noise = noiseA.data(); bufA.capacity = 64; bufA.writeOffset = 0;
        bufB.noise = noiseB.data(); bufB.capacity = 64; bufB.writeOffset = 0;
        float audio[64] {};
        a.setStageABuffers (&bufA);
        a.processBlock (audio, 64);
        a.setStageABuffers (nullptr);
        b.setStageABuffers (&bufB);
        b.processBlock (audio, 16);
        bufB.writeOffset = 16;
        b.processBlock (audio + 16, 48);
        b.setStageABuffers (nullptr);
        bool noiseDiffers = false;
        for (int i = 16; i < 64; ++i)
            if (! bit_eq_f (noiseA[static_cast<size_t> (i)], noiseB[static_cast<size_t> (i)]))
            { noiseDiffers = true; break; }
        if (! noiseDiffers)
        {
            std::cerr << "FAIL call-boundary: noise identical for 64 vs 16+48\n";
            ++fail;
        }
        else
        {
            std::cout << "PASS call-boundary: noise diverges for 64 vs 16+48\n";
            ++pass;
        }
    }

    std::cout << "SynthesisGolden summary pass=" << pass << " fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
