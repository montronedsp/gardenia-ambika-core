/*
  AbiGoldenTests.cpp — Stage-A/B goldens through the public C ABI.
*/
#include <gardenia_core/gardenia_core.h>
#include "gardenia_core/Patch.h"
#include "HostReplayAdapter.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef GARDENIA_BASELINE_OUTPUT_DIR
#error GARDENIA_BASELINE_OUTPUT_DIR required
#endif

namespace
{

using gardenia_core_test::CcEvent;
using gardenia_core_test::NoteEvent;
using gardenia_core_test::PitchBendEvent;
using gardenia_core_test::TimedEvent;
using gardenia_core_test::EvKind;
using gardenia_core_test::merge_events;
using gardenia_core_test::NoteStack;

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

bool load_u8 (const std::string& path, std::vector<uint8_t>& out, size_t n)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    out.resize (n);
    in.read (reinterpret_cast<char*> (out.data()), static_cast<std::streamsize> (n));
    return (static_cast<bool> (in) || in.eof());
}

bool bit_eq (float a, float b)
{
    uint32_t ua = 0, ub = 0;
    std::memcpy (&ua, &a, sizeof (float));
    std::memcpy (&ub, &b, sizeof (float));
    return ua == ub;
}

std::string scenario_dir (const char* id)
{
    return std::string (GARDENIA_BASELINE_OUTPUT_DIR) + "/scenarios/" + id + "/sr-48000/bs-64/";
}

void fill_snapshot (gardenia_core_synthesis_snapshot& snap,
                    const gardenia_core::Patch& p,
                    const gardenia_core::Part& part,
                    float bpm,
                    uint8_t wt,
                    bool delayBypass,
                    bool reverbBypass)
{
    std::memset (&snap, 0, sizeof (snap));
    snap.struct_size = sizeof (snap);
    snap.abi_version = GARDENIA_CORE_ABI_VERSION;
    std::memcpy (snap.patch_blob, &p, sizeof (p));
    std::memcpy (snap.part_blob, &part, sizeof (part));
    snap.tempo_bpm = bpm;
    snap.wavetable_model = wt;
    snap.delay_bypass = delayBypass ? 1u : 0u;
    snap.reverb_bypass = reverbBypass ? 1u : 0u;
}

struct SlotState
{
    int note = -1;
    bool keyDown = false;
    bool sustain = false;
    bool envelopeLive = false;
    uint32_t noteOnTime = 0;
};

struct SlotAllocator
{
    std::array<SlotState, 8> slots {};
    bool sustainDown = false;
    uint32_t counter = 0;
    NoteStack stack {};
    bool monoSustain = false;

    int findFree() const
    {
        for (int i = 0; i < 8; ++i)
            if (! slots[static_cast<size_t> (i)].envelopeLive)
                return i;
        return -1;
    }

    int steal (int midiNote) const
    {
        std::array<int, 8> order {};
        int n = 0;
        for (int i = 0; i < 8; ++i)
            if (slots[static_cast<size_t> (i)].envelopeLive)
                order[static_cast<size_t> (n++)] = i;
        std::sort (order.begin(), order.begin() + n, [&] (int a, int b) {
            return slots[static_cast<size_t> (a)].noteOnTime < slots[static_cast<size_t> (b)].noteOnTime;
        });
        int low = -1, top = -1;
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            const auto& s = slots[static_cast<size_t> (i)];
            if (s.note < 0)
                continue;
            if (s.note >= 0 && ! s.keyDown && ! s.sustain)
                continue;
            if (low < 0 || s.note < slots[static_cast<size_t> (low)].note)
                low = i;
            if (top < 0 || s.note > slots[static_cast<size_t> (top)].note)
                top = i;
        }
        if (top == low)
            top = -1;
        auto prot = [&] (int i) { return i == low || i == top; };
        for (int k = 0; k < n; ++k)
            if (slots[static_cast<size_t> (order[static_cast<size_t> (k)])].note == midiNote)
                return order[static_cast<size_t> (k)];
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            const auto& s = slots[static_cast<size_t> (i)];
            if (! prot (i) && s.note >= 0 && ! s.keyDown && ! s.sustain)
                return i;
        }
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            if (! prot (i) && ! slots[static_cast<size_t> (i)].keyDown)
                return i;
        }
        for (int k = 0; k < n; ++k)
            if (! prot (order[static_cast<size_t> (k)]))
                return order[static_cast<size_t> (k)];
        return top >= 0 ? top : (low >= 0 ? low : 0);
    }

    void emitPoly (const TimedEvent& te, uint32_t relOff, std::vector<gardenia_core_event>& out)
    {
        gardenia_core_event e {};
        e.struct_size = sizeof (e);
        e.abi_version = GARDENIA_CORE_ABI_VERSION;
        e.sample_offset = relOff;

        if (te.kind == EvKind::NoteOn)
        {
            for (int i = 0; i < 8; ++i)
            {
                if (slots[static_cast<size_t> (i)].note == te.a)
                {
                    gardenia_core_event rel = e;
                    rel.type = GARDENIA_CORE_EVENT_VOICE_RELEASE;
                    rel.voice_slot = static_cast<uint32_t> (i);
                    out.push_back (rel);
                    slots[static_cast<size_t> (i)].note = -1;
                    slots[static_cast<size_t> (i)].keyDown = false;
                    slots[static_cast<size_t> (i)].sustain = false;
                }
            }
            int idx = findFree();
            if (idx < 0)
            {
                idx = steal (te.a);
                gardenia_core_event stop = e;
                stop.type = GARDENIA_CORE_EVENT_VOICE_STOP;
                stop.voice_slot = static_cast<uint32_t> (idx);
                out.push_back (stop);
                slots[static_cast<size_t> (idx)] = {};
            }
            auto& s = slots[static_cast<size_t> (idx)];
            s.note = te.a;
            s.keyDown = true;
            s.sustain = sustainDown;
            s.envelopeLive = true;
            s.noteOnTime = ++counter;
            e.type = GARDENIA_CORE_EVENT_VOICE_START;
            e.voice_slot = static_cast<uint32_t> (idx);
            e.note = te.a;
            e.velocity = te.velocity;
            e.legato = 0;
            out.push_back (e);
        }
        else if (te.kind == EvKind::NoteOff)
        {
            bool emittedCoreEvent = false;
            for (int i = 0; i < 8; ++i)
            {
                auto& s = slots[static_cast<size_t> (i)];
                if (s.note != te.a)
                    continue;
                s.keyDown = false;
                if (! s.sustain)
                {
                    e.type = GARDENIA_CORE_EVENT_VOICE_RELEASE;
                    e.voice_slot = static_cast<uint32_t> (i);
                    out.push_back (e);
                    s.note = -1;
                    emittedCoreEvent = true;
                }
            }
            // Sustained note-off is host-owned but still segments MIDI in Phase-2.
            if (! emittedCoreEvent)
            {
                gardenia_core_event barrier = e;
                barrier.type = GARDENIA_CORE_EVENT_NONE;
                barrier.voice_slot = 0;
                out.push_back (barrier);
            }
        }
        else if (te.kind == EvKind::CC && te.a == 64)
        {
            const bool down = te.b >= 64;
            bool emittedCoreEvent = false;
            if (down)
            {
                sustainDown = true;
                for (auto& s : slots)
                    if (s.note >= 0 && s.keyDown)
                        s.sustain = true;
            }
            else
            {
                for (int i = 0; i < 8; ++i)
                {
                    auto& s = slots[static_cast<size_t> (i)];
                    if (s.note < 0)
                        continue;
                    s.sustain = false;
                    if (! s.keyDown)
                    {
                        e.type = GARDENIA_CORE_EVENT_VOICE_RELEASE;
                        e.voice_slot = static_cast<uint32_t> (i);
                        out.push_back (e);
                        s.note = -1;
                        emittedCoreEvent = true;
                    }
                }
                sustainDown = false;
            }
            // Sustain is host-owned, but Phase-2 MIDI still segments at this offset
            // (noise/LFO are call-boundary sensitive).
            if (! emittedCoreEvent)
            {
                gardenia_core_event barrier = e;
                barrier.type = GARDENIA_CORE_EVENT_NONE;
                barrier.voice_slot = 0;
                out.push_back (barrier);
            }
        }
        else if (te.kind == EvKind::CC)
        {
            // One TimedEvent → one synth-wide MIDI message (not per-voice).
            e.type = GARDENIA_CORE_EVENT_CONTROL;
            e.voice_slot = 0;
            e.control_id = (te.a == 1) ? GARDENIA_CORE_CONTROL_MOD_WHEEL
                                       : (te.a == 2) ? GARDENIA_CORE_CONTROL_WHEEL_2
                                       : (te.a == 11) ? GARDENIA_CORE_CONTROL_EXPRESSION
                                       : static_cast<uint32_t> (te.a);
            e.value_f32 = static_cast<float> (te.b) / 127.0f;
            out.push_back (e);
            // Mirror HostReplay: apply the same controller to remaining slots without
            // extra segment points (same sample_offset, applied before next render).
            for (int i = 1; i < 8; ++i)
            {
                gardenia_core_event c = e;
                c.voice_slot = static_cast<uint32_t> (i);
                out.push_back (c);
            }
        }
        else if (te.kind == EvKind::PitchBend)
        {
            e.type = GARDENIA_CORE_EVENT_PITCH_BEND;
            e.voice_slot = 0;
            e.flags = GARDENIA_CORE_EVENT_FLAG_PITCH_WHEEL_RAW;
            e.value_i32 = te.a;
            out.push_back (e);
            for (int i = 1; i < 8; ++i)
            {
                gardenia_core_event c = e;
                c.voice_slot = static_cast<uint32_t> (i);
                out.push_back (c);
            }
        }
    }

    void emitMono (const TimedEvent& te, std::vector<gardenia_core_event>& out)
    {
        gardenia_core_event e {};
        e.struct_size = sizeof (e);
        e.abi_version = GARDENIA_CORE_ABI_VERSION;
        e.sample_offset = 0;
        e.voice_slot = 0;
        if (te.kind == EvKind::NoteOn)
        {
            const bool legato = ! stack.empty();
            stack.noteOn (te.a, te.velocity);
            e.type = GARDENIA_CORE_EVENT_VOICE_START;
            e.note = te.a;
            e.velocity = te.velocity;
            e.legato = legato ? 1u : 0u;
            out.push_back (e);
            slots[0].note = te.a;
            slots[0].envelopeLive = true;
        }
        else if (te.kind == EvKind::NoteOff)
        {
            stack.noteOff (te.a);
            if (stack.empty() && ! monoSustain)
            {
                e.type = GARDENIA_CORE_EVENT_VOICE_RELEASE;
                out.push_back (e);
                slots[0].note = -1;
            }
            else if (! stack.empty())
            {
                const auto& top = stack.mostRecent();
                e.type = GARDENIA_CORE_EVENT_VOICE_START;
                e.note = top.note;
                e.velocity = top.velocity;
                e.legato = 1;
                out.push_back (e);
                slots[0].note = top.note;
            }
        }
        else if (te.kind == EvKind::CC && te.a == 64)
        {
            monoSustain = te.b >= 64;
            if (! monoSustain && stack.empty())
            {
                e.type = GARDENIA_CORE_EVENT_VOICE_RELEASE;
                out.push_back (e);
                slots[0].note = -1;
            }
        }
        else if (te.kind == EvKind::CC)
        {
            e.type = GARDENIA_CORE_EVENT_CONTROL;
            e.control_id = (te.a == 1) ? GARDENIA_CORE_CONTROL_MOD_WHEEL : static_cast<uint32_t> (te.a);
            e.value_f32 = static_cast<float> (te.b) / 127.0f;
            out.push_back (e);
        }
        else if (te.kind == EvKind::PitchBend)
        {
            e.type = GARDENIA_CORE_EVENT_PITCH_BEND;
            e.flags = GARDENIA_CORE_EVENT_FLAG_PITCH_WHEEL_RAW;
            e.value_i32 = te.a;
            out.push_back (e);
        }
    }
};

struct CApiRender
{
    gardenia_core_instance* inst = nullptr;
    std::array<std::vector<float>, 8> stem {}, noise {}, filterEnv {}, lfo1 {}, lfo2 {}, lfo3 {};
    std::array<std::vector<float>, 8> voiceLfo {}, cutoffMod {}, cutoffHz {}, resonance {};
    std::array<std::vector<float>, 8> signalGain {}, noiseGain {}, fuzzMix {}, vca {}, pitch {};
    std::array<std::vector<uint8_t>, 8> active {}, gate {};
    SlotAllocator alloc {};

    bool init (uint32_t samples)
    {
        gardenia_core_create_config cfg {};
        cfg.struct_size = sizeof (cfg);
        cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
        cfg.maximum_voice_slots = 8;
        cfg.maximum_block_size = 64;
        cfg.sample_rate = 48000.0;
        cfg.deterministic_seed = 0;
        inst = gardenia_core_create (&cfg);
        if (inst == nullptr)
            return false;
        for (int i = 0; i < 8; ++i)
        {
            stem[i].assign (samples, 0.0f);
            noise[i].assign (samples, 0.0f);
            filterEnv[i].assign (samples, 0.0f);
            lfo1[i].assign (samples, 0.0f);
            lfo2[i].assign (samples, 0.0f);
            lfo3[i].assign (samples, 0.0f);
            voiceLfo[i].assign (samples, 0.0f);
            cutoffMod[i].assign (samples, 0.0f);
            cutoffHz[i].assign (samples, 0.0f);
            resonance[i].assign (samples, 0.0f);
            signalGain[i].assign (samples, 0.0f);
            noiseGain[i].assign (samples, 0.0f);
            fuzzMix[i].assign (samples, 0.0f);
            vca[i].assign (samples, 0.0f);
            pitch[i].assign (samples, 0.0f);
            active[i].assign (samples, 0);
            gate[i].assign (samples, 0);
        }
        return true;
    }

    ~CApiRender()
    {
        if (inst != nullptr)
            gardenia_core_destroy (inst);
    }

    bool render (const gardenia_core_synthesis_snapshot& snap,
                 const std::vector<TimedEvent>& events,
                 uint32_t samples,
                 bool mono)
    {
        uint32_t pos = 0;
        size_t evIdx = 0;
        while (pos < samples)
        {
            const uint32_t blockEnd = std::min (pos + 64u, samples);
            const uint32_t n = blockEnd - pos;
            std::vector<TimedEvent> local;
            while (evIdx < events.size() && events[evIdx].sample >= pos && events[evIdx].sample < blockEnd)
                local.push_back (events[evIdx++]);

            std::vector<gardenia_core_event> blockEvs;
            if (mono)
            {
                for (const auto& te : local)
                    alloc.emitMono (te, blockEvs);
            }
            else
            {
                for (const auto& te : local)
                    alloc.emitPoly (te, te.sample - pos, blockEvs);
            }

            std::array<gardenia_core_voice_output, 8> outs {};
            for (uint32_t i = 0; i < 8; ++i)
            {
                auto& o = outs[i];
                o.struct_size = sizeof (o);
                o.abi_version = GARDENIA_CORE_ABI_VERSION;
                o.voice_slot = i;
                o.stem = stem[i].data() + pos;
                o.noise = noise[i].data() + pos;
                o.filter_envelope = filterEnv[i].data() + pos;
                o.lfo_1 = lfo1[i].data() + pos;
                o.lfo_2 = lfo2[i].data() + pos;
                o.lfo_3 = lfo3[i].data() + pos;
                o.voice_lfo = voiceLfo[i].data() + pos;
                o.cutoff_mod = cutoffMod[i].data() + pos;
                o.cutoff_hz = cutoffHz[i].data() + pos;
                o.resonance = resonance[i].data() + pos;
                o.signal_gain = signalGain[i].data() + pos;
                o.noise_gain = noiseGain[i].data() + pos;
                o.fuzz_mix = fuzzMix[i].data() + pos;
                o.vca = vca[i].data() + pos;
                o.pitch = pitch[i].data() + pos;
                o.active = active[i].data() + pos;
                o.gate = gate[i].data() + pos;
                o.buffer_capacity = n;
                o.sample_stride = 1;
            }

            if (gardenia_core_process_stage_a (
                    inst, &snap, blockEvs.empty() ? nullptr : blockEvs.data(),
                    static_cast<uint32_t> (blockEvs.size()), outs.data(), 8, n) != GARDENIA_CORE_OK)
                return false;

            for (uint32_t i = 0; i < 8; ++i)
            {
                bool any = false;
                for (uint32_t s = 0; s < n; ++s)
                    if (active[i][pos + s])
                    { any = true; break; }
                if (! any && alloc.slots[i].note < 0)
                    alloc.slots[i].envelopeLive = false;
                else if (any)
                    alloc.slots[i].envelopeLive = true;
            }

            pos = blockEnd;
        }
        return true;
    }
};

void check_f (const std::string& tag, const std::vector<float>& got, const std::vector<float>& exp,
              uint32_t samples, int& pass, int& fail)
{
    for (uint32_t i = 0; i < samples; ++i)
    {
        if (i >= got.size() || i >= exp.size() || ! bit_eq (got[i], exp[i]))
        {
            std::cerr << "FAIL " << tag << " firstDiff=" << i << "\n";
            ++fail;
            return;
        }
    }
    std::cout << "PASS " << tag << "\n";
    ++pass;
}

void run_scenario_c_api (const char* id,
                         gardenia_core::Patch patch,
                         gardenia_core::Part part,
                         const std::vector<NoteEvent>& notes,
                         const std::vector<CcEvent>& ccs,
                         const std::vector<PitchBendEvent>& pbs,
                         uint32_t samples,
                         bool delayBypass,
                         bool reverbBypass,
                         bool mono,
                         int slotLo,
                         int slotHi,
                         uint8_t wt,
                         int& pass,
                         int& fail)
{
    CApiRender rend;
    if (! rend.init (samples))
    {
        std::cerr << "FAIL create " << id << "\n";
        ++fail;
        return;
    }
    gardenia_core_synthesis_snapshot snap;
    fill_snapshot (snap, patch, part, 120.0f, wt, delayBypass, reverbBypass);
    if (! rend.render (snap, merge_events (notes, ccs, pbs), samples, mono))
    {
        std::cerr << "FAIL render " << id << "\n";
        ++fail;
        return;
    }

    for (int vs = slotLo; vs <= slotHi; ++vs)
    {
        const std::string prefix = scenario_dir (id) + "voice-0" + std::to_string (vs) + "-";
        const size_t vi = static_cast<size_t> (vs);
        const char* streams[] = {
            "stem", "noise", "filter-env", "lfo1", "lfo2", "lfo3", "voice-lfo",
            "cutoff-mod", "cutoff", "resonance", "signal-gain", "noise-gain",
            "fuzz-mix", "vca", "pitch"
        };
        const std::vector<float>* bufs[] = {
            &rend.stem[vi], &rend.noise[vi], &rend.filterEnv[vi], &rend.lfo1[vi], &rend.lfo2[vi],
            &rend.lfo3[vi], &rend.voiceLfo[vi], &rend.cutoffMod[vi], &rend.cutoffHz[vi],
            &rend.resonance[vi], &rend.signalGain[vi], &rend.noiseGain[vi], &rend.fuzzMix[vi],
            &rend.vca[vi], &rend.pitch[vi]
        };
        for (size_t s = 0; s < 15; ++s)
        {
            std::vector<float> exp;
            if (! load_f32 (prefix + streams[s] + ".f32", exp))
            {
                std::cerr << "FAIL load " << id << " " << streams[s] << "\n";
                ++fail;
                continue;
            }
            check_f (std::string (id) + " " + streams[s], *bufs[s], exp, samples, pass, fail);
        }
        std::vector<uint8_t> gexp, aexp;
        if (! load_u8 (prefix + "gate.u8", gexp, samples) || ! load_u8 (prefix + "active.u8", aexp, samples))
        {
            std::cerr << "FAIL load " << id << " gate/active\n";
            ++fail;
        }
        else
        {
            bool ok = true;
            size_t idx = 0;
            for (uint32_t i = 0; i < samples; ++i)
                if (rend.gate[vi][i] != gexp[i] || rend.active[vi][i] != aexp[i])
                { ok = false; idx = i; break; }
            if (! ok)
            {
                std::cerr << "FAIL " << id << " gate/active firstDiff=" << idx << "\n";
                ++fail;
            }
            else
            {
                std::cout << "PASS " << id << " gate\n";
                std::cout << "PASS " << id << " active\n";
                pass += 2;
            }
        }
    }
}

gardenia_core::Patch default_patch()
{
    gardenia_core::Patch p;
    p.osc[0].shape = gardenia_core::WAVEFORM_SAW;
    p.osc[1].shape = gardenia_core::WAVEFORM_SQUARE;
    p.mix_balance = 32;
    p.mix_op = gardenia_core::OP_SUM;
    p.filter_type = gardenia_core::FILTER_TYPE_WDF;
    p.filter[0].cutoff = 127;
    return p;
}

gardenia_core::Part default_part()
{
    gardenia_core::Part part;
    part.volume = 100;
    return part;
}

struct ScenarioOpts
{
    gardenia_core::Patch patch = default_patch();
    gardenia_core::Part part = default_part();
    std::vector<NoteEvent> notes;
    std::vector<CcEvent> ccs;
    std::vector<PitchBendEvent> pbs;
    uint32_t samples = 24000;
    bool delayBypass = false;
    bool reverbBypass = false;
    bool mono = false;
    int voiceSlot = 0;
    int voiceSlotEnd = -1;
    uint8_t wavetableModel = 0;
};

void run_opts (const char* id, const ScenarioOpts& o, int& pass, int& fail)
{
    const int hi = o.voiceSlotEnd >= 0 ? o.voiceSlotEnd : o.voiceSlot;
    run_scenario_c_api (id, o.patch, o.part, o.notes, o.ccs, o.pbs, o.samples,
                        o.delayBypass, o.reverbBypass, o.mono, o.voiceSlot, hi,
                        o.wavetableModel, pass, fail);
}

void fuzz_c_api (int& pass, int& fail)
{
    // Exact FuzzGoldenTests corpus (8 scenarios × 8 voices + bypass = 65).
    const char* ids[] = {
        "fuzz-distortion",
        "default-single-note",
        "silence-no-midi",
        "wdf-cutoff-res-sweep",
        "acid-cutoff-res-sweep",
        "deterministic-noise",
        "four-note-chord",
        "force-steal-12-notes"
    };

    gardenia_core_create_config cfg {};
    cfg.struct_size = sizeof (cfg);
    cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
    cfg.maximum_voice_slots = 8;
    cfg.maximum_block_size = 2048;
    cfg.sample_rate = 48000.0;
    cfg.deterministic_seed = 1;
    auto* inst = gardenia_core_create (&cfg);
    if (inst == nullptr)
    {
        std::cerr << "FAIL fuzz create\n";
        ++fail;
        return;
    }

    int fuzzPass = 0;
    for (const char* id : ids)
    {
        for (int v = 0; v < 8; ++v)
        {
            char tag[16];
            std::snprintf (tag, sizeof (tag), "voice-%02d-", v);
            const std::string prefix = scenario_dir (id) + tag;
            std::vector<float> in, mix, exp;
            if (! load_f32 (prefix + "after-noise-route.f32", in)
                || ! load_f32 (prefix + "fuzz-mix.f32", mix)
                || ! load_f32 (prefix + "after-fuzz.f32", exp))
            {
                std::cerr << "FAIL fuzz load " << id << " v" << v << "\n";
                ++fail;
                continue;
            }
            if (in.size() != mix.size() || in.size() != exp.size())
            {
                std::cerr << "FAIL fuzz size " << id << " v" << v << "\n";
                ++fail;
                continue;
            }
            gardenia_core_reset (inst, 1);
            std::vector<float> out (in.size(), 0.0f);
            uint32_t done = 0;
            bool ok = true;
            while (done < in.size())
            {
                const uint32_t n = std::min<uint32_t> (2048u, static_cast<uint32_t> (in.size() - done));
                gardenia_core_fuzz_io fio {};
                fio.struct_size = sizeof (fio);
                fio.abi_version = GARDENIA_CORE_ABI_VERSION;
                fio.voice_slot = static_cast<uint32_t> (v % 8);
                fio.input = in.data() + done;
                fio.mix = mix.data() + done;
                fio.output = out.data() + done;
                fio.buffer_capacity = n;
                fio.input_stride = fio.control_stride = fio.output_stride = 1;
                if (gardenia_core_process_stage_b_fuzz (inst, &fio, 1, n) != GARDENIA_CORE_OK)
                { ok = false; break; }
                done += n;
            }
            if (! ok)
            {
                std::cerr << "FAIL fuzz process " << id << " v" << v << "\n";
                ++fail;
                continue;
            }
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (! bit_eq (out[i], exp[i]))
                {
                    std::cerr << "FAIL fuzz " << id << " v" << v << " first=" << i << "\n";
                    ++fail;
                    ok = false;
                    break;
                }
            }
            if (ok)
            {
                std::cout << "PASS fuzz-c-api " << id << " voice=" << v
                          << " samples=" << in.size() << "\n";
                ++pass;
                ++fuzzPass;
            }
        }
    }

    {
        gardenia_core_reset (inst, 1);
        std::vector<float> in (64), out (64), amt (64, 0.0f);
        for (int i = 0; i < 64; ++i)
            in[static_cast<size_t> (i)] = 0.1f * static_cast<float> (i);
        gardenia_core_fuzz_io fio {};
        fio.struct_size = sizeof (fio);
        fio.abi_version = GARDENIA_CORE_ABI_VERSION;
        fio.voice_slot = 0;
        fio.input = in.data();
        fio.amount = amt.data();
        fio.output = out.data();
        fio.buffer_capacity = 64;
        fio.input_stride = fio.control_stride = fio.output_stride = 1;
        gardenia_core_process_stage_b_fuzz (inst, &fio, 1, 64);
        bool ok = true;
        for (int i = 0; i < 64; ++i)
            if (! bit_eq (in[static_cast<size_t> (i)], out[static_cast<size_t> (i)]))
                ok = false;
        if (ok) { std::cout << "PASS fuzz-c-api bypass\n"; ++pass; ++fuzzPass; }
        else { std::cerr << "FAIL fuzz-c-api bypass\n"; ++fail; }
    }

    gardenia_core_destroy (inst);
    std::cout << "INFO fuzz-c-api pass=" << fuzzPass << " (expect 65)\n";
}

} // namespace

int main()
{
    int pass = 0, fail = 0;

    fuzz_c_api (pass, fail);

    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        run_opts ("default-single-note", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = {
            { 64, true, 60, 0.8f }, { 64, true, 64, 0.8f }, { 64, true, 67, 0.8f }, { 64, true, 71, 0.8f },
            { 18000, false, 60, 0.0f }, { 18000, false, 64, 0.0f }, { 18000, false, 67, 0.0f }, { 18000, false, 71, 0.0f },
        };
        o.voiceSlotEnd = 3;
        run_opts ("four-note-chord", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.samples = 12000;
        run_opts ("silence-no-midi", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.mix_noise = 100;
        o.patch.mix_noise_to_filter = 0;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_NONE;
        o.patch.osc[1].shape = gardenia_core::WAVEFORM_NONE;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("deterministic-noise", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_SAW;
        o.patch.osc[1].shape = gardenia_core::WAVEFORM_NONE;
        o.patch.mix_balance = 0;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("osc-shape-1", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_WAVETABLE_1;
        o.patch.osc[0].parameter = 40;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("ambika-wavetable", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_VOWEL;
        o.patch.osc[0].parameter = 64;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("vowel-formant", o, pass, fail);
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
        run_opts ("cutoff-mod-route", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.osc[0].shape = gardenia_core::WAVEFORM_WAVETABLE_1;
        o.patch.osc[0].parameter = 20;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        o.wavetableModel = gardenia_core::WT_MODEL_WAVEEDIT_MODERN;
        run_opts ("waveedit-mode", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].attack = 127;
        o.patch.env_lfo[0].decay = 127;
        o.patch.env_lfo[0].sustain = 127;
        o.patch.env_lfo[0].release = 127;
        o.notes = { { 64, true, 60, 0.8f }, { 8000, false, 60, 0.0f } };
        run_opts ("env-max", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].release = 40;
        o.notes = { { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f } };
        o.delayBypass = true;
        o.reverbBypass = true;
        run_opts ("release-into-silence", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.env_lfo[0].release = 100;
        o.notes = { { 64, true, 60, 0.8f }, { 3000, false, 60, 0.0f },
                    { 4500, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("retrigger-during-release", o, pass, fail);
    }

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
            run_opts (id.c_str(), o, pass, fail);
        }
    }

    {
        ScenarioOpts o;
        o.part.legato = 1;
        o.mono = true;
        o.notes = {
            { 64, true, 60, 0.8f }, { 2000, true, 64, 0.8f }, { 4000, false, 60, 0.0f },
            { 6000, false, 64, 0.0f }, { 8000, true, 67, 0.8f }, { 16000, false, 67, 0.0f },
        };
        o.samples = 20000;
        run_opts ("mono-overlap-retrigger", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.part.legato = 1;
        o.part.portamento_time = 80;
        o.mono = true;
        o.notes = { { 64, true, 48, 0.8f }, { 3000, true, 72, 0.8f }, { 16000, false, 72, 0.0f } };
        o.samples = 20000;
        run_opts ("portamento-mono", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = {
            { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f },
            { 16000, true, 62, 0.8f }, { 20000, false, 62, 0.0f },
        };
        o.ccs = { { 2000, 64, 127 }, { 12000, 64, 0 } };
        o.samples = 28000;
        run_opts ("sustain-pedal", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        o.pbs = { { 2000, 8192 + 2000 }, { 8000, 8192 - 2000 }, { 14000, 8192 } };
        run_opts ("pitch-bend", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.notes = { { 64, true, 60, 0.8f }, { 18000, false, 60, 0.0f } };
        o.ccs = { { 1000, 1, 0 }, { 4000, 1, 100 }, { 10000, 1, 40 } };
        run_opts ("mod-wheel", o, pass, fail);
    }
    {
        ScenarioOpts o;
        for (int i = 0; i < 12; ++i)
            o.notes.push_back ({ static_cast<uint32_t> (64 + i * 200), true, 48 + i, 0.8f });
        for (int i = 0; i < 12; ++i)
            o.notes.push_back ({ 20000, false, 48 + i, 0.0f });
        o.samples = 28000;
        o.voiceSlotEnd = 7;
        run_opts ("force-steal-12-notes", o, pass, fail);
    }

    {
        ScenarioOpts o;
        o.patch.filter[0].resonance = 20;
        o.patch.modulation[0].source = gardenia_core::MOD_SRC_LFO_1;
        o.patch.modulation[0].destination = gardenia_core::MOD_DST_FILTER_RESONANCE;
        o.patch.modulation[0].amount = 80;
        o.patch.env_lfo[0].rate = 90;
        o.notes = { { 64, true, 55, 0.8f }, { 16000, false, 55, 0.0f } };
        o.samples = 20000;
        run_opts ("resonance-mod-route", o, pass, fail);
    }
    {
        ScenarioOpts o;
        o.patch.modulation[0].source = gardenia_core::MOD_SRC_LFO_2;
        o.patch.modulation[0].destination = gardenia_core::MOD_DST_VCA;
        o.patch.modulation[0].amount = 60;
        o.notes = { { 64, true, 60, 0.8f }, { 16000, false, 60, 0.0f } };
        o.samples = 20000;
        run_opts ("vca-mod-route", o, pass, fail);
    }

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
            run_opts (id.c_str(), o, pass, fail);
        }
    }

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
            run_opts (id.c_str(), o, pass, fail);
        }
    }

    {
        ScenarioOpts o;
        o.patch.env_lfo[0].attack = 0;
        o.patch.env_lfo[0].decay = 0;
        o.patch.env_lfo[0].sustain = 0;
        o.patch.env_lfo[0].release = 0;
        o.notes = { { 64, true, 60, 0.8f }, { 4000, false, 60, 0.0f } };
        o.samples = 12000;
        run_opts ("env-min", o, pass, fail);
    }

    // Event-offset / same-block / call-boundary self-checks via C ABI
    {
        const uint32_t offsets[] = { 0u, 1u, 16u, 31u, 63u };
        for (uint32_t off : offsets)
        {
            const uint32_t blockStart = 17984u;
            const uint32_t noteOffAbs = blockStart + off;
            CApiRender rend;
            if (! rend.init (noteOffAbs + 128u))
            { std::cerr << "FAIL note-off-offset create\n"; ++fail; continue; }
            gardenia_core_synthesis_snapshot snap;
            fill_snapshot (snap, default_patch(), default_part(), 120.0f, 0, false, false);
            std::vector<NoteEvent> notes = { { 64, true, 60, 0.8f }, { noteOffAbs, false, 60, 0.0f } };
            if (! rend.render (snap, merge_events (notes, {}, {}), noteOffAbs + 128u, false))
            { std::cerr << "FAIL note-off-offset render\n"; ++fail; continue; }
            const auto& g = rend.gate[0];
            bool ok = true;
            if (noteOffAbs > 0 && (g[noteOffAbs - 1] != 1 || g[noteOffAbs] != 0))
                ok = false;
            if (noteOffAbs == 0 && g[0] != 0)
                ok = false;
            if (! ok) { std::cerr << "FAIL note-off-offset-" << off << "\n"; ++fail; }
            else { std::cout << "PASS note-off-offset-" << off << "\n"; ++pass; }
        }
        {
            CApiRender rend;
            rend.init (256);
            gardenia_core_synthesis_snapshot snap;
            fill_snapshot (snap, default_patch(), default_part(), 120.0f, 0, false, false);
            rend.render (snap, merge_events ({{ 16, true, 60, 0.8f }, { 48, false, 60, 0.0f }}, {}, {}), 256, false);
            const auto& g = rend.gate[0];
            const bool ok = g[15] == 0 && g[16] == 1 && g[47] == 1 && g[48] == 0;
            if (! ok) { std::cerr << "FAIL same-block-note-on-off\n"; ++fail; }
            else { std::cout << "PASS same-block-note-on-off\n"; ++pass; }
        }
        {
            CApiRender rend;
            rend.init (256);
            gardenia_core_synthesis_snapshot snap;
            fill_snapshot (snap, default_patch(), default_part(), 120.0f, 0, false, false);
            rend.render (snap, merge_events ({
                { 0, true, 60, 0.8f }, { 0, true, 64, 0.8f },
                { 80, false, 60, 0.0f }, { 80, false, 64, 0.0f },
            }, {}, {}), 256, false);
            const bool ok = rend.gate[0][79] == 1 && rend.gate[0][80] == 0
                         && rend.gate[1][79] == 1 && rend.gate[1][80] == 0;
            if (! ok) { std::cerr << "FAIL same-offset-dual-note-off\n"; ++fail; }
            else { std::cout << "PASS same-offset-dual-note-off\n"; ++pass; }
        }
    }

    {
        // Prove C-ABI segmentation changes noise call-boundary behaviour.
        gardenia_core_create_config cfg {};
        cfg.struct_size = sizeof (cfg);
        cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
        cfg.maximum_voice_slots = 8;
        cfg.maximum_block_size = 64;
        cfg.sample_rate = 48000.0;
        cfg.deterministic_seed = 0;
        auto* a = gardenia_core_create (&cfg);
        auto* b = gardenia_core_create (&cfg);
        gardenia_core_synthesis_snapshot snap;
        fill_snapshot (snap, default_patch(), default_part(), 120.0f, 0, false, false);
        float noiseA[64] {}, noiseB[64] {}, stem[64] {};
        auto makeOut = [&] (float* noise) {
            gardenia_core_voice_output o {};
            o.struct_size = sizeof (o);
            o.abi_version = GARDENIA_CORE_ABI_VERSION;
            o.voice_slot = 0;
            o.stem = stem;
            o.noise = noise;
            o.buffer_capacity = 64;
            o.sample_stride = 1;
            return o;
        };
        gardenia_core_event start {};
        start.struct_size = sizeof (start);
        start.abi_version = GARDENIA_CORE_ABI_VERSION;
        start.type = GARDENIA_CORE_EVENT_VOICE_START;
        start.note = 60;
        start.velocity = 0.8f;
        auto oa = makeOut (noiseA);
        gardenia_core_process_stage_a (a, &snap, &start, 1, &oa, 1, 64);
        auto ob = makeOut (noiseB);
        gardenia_core_event barrier = start;
        barrier.type = GARDENIA_CORE_EVENT_NONE;
        barrier.sample_offset = 16;
        gardenia_core_event evs[2] = { start, barrier };
        gardenia_core_process_stage_a (b, &snap, evs, 2, &ob, 1, 64);
        bool noiseDiffers = false;
        for (int i = 16; i < 64; ++i)
            if (! bit_eq (noiseA[i], noiseB[i]))
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
        gardenia_core_destroy (a);
        gardenia_core_destroy (b);
    }

    std::cout << "AbiGolden summary pass=" << pass << " fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
