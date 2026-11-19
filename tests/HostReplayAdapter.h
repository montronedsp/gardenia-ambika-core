/*
  HostReplayAdapter.h — TEST-ONLY golden replay harness.

  Replays host allocation / mono / sustain / pitch-bend / mod-wheel semantics
  against CoreVoice DSP. Voice allocation is not part of the public core API.
*/
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "gardenia_core/CoreVoice.h"
#include "gardenia_core/Patch.h"
#include "gardenia_core/gardenia_core_math.h"

#if defined(_MSC_VER)
#include <intrin.h>
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace gardenia_core_test
{

constexpr double kSr = 48000.0;
constexpr int kBs = 64;
constexpr int kVoices = 8;

// Match juce::ScopedNoDenormals used by Phase-2 BaselineOfflineEngine::render.
struct ScopedNoDenormals
{
#if defined(_MSC_VER)
    unsigned int csr_ = 0;
    ScopedNoDenormals() noexcept
    {
        csr_ = _mm_getcsr();
        _mm_setcsr (csr_ | 0x8040u); // FTZ | DAZ
    }
    ~ScopedNoDenormals() noexcept { _mm_setcsr (csr_); }
#else
    ScopedNoDenormals() noexcept = default;
#endif
};

inline float midi_velocity_roundtrip (float velocity) noexcept
{
    // Match host float note-on reconstruction: byte * (1.0f / 127.0f), not (float)byte / 127.0f.
    const float clamped = gardenia_core::jlimit (0.0f, 1.0f, velocity);
    const int byte = gardenia_core::jlimit (0, 127, gardenia_core::round_to_int (clamped * 127.0f));
    return static_cast<float> (byte) * (1.0f / 127.0f);
}

inline float pitch_wheel_to_normalized (int wheelValue) noexcept
{
    const int clamped = gardenia_core::jlimit (0, 16383, wheelValue);
    const float bipolar = gardenia_core::jlimit (-1.0f, 1.0f,
                                                 static_cast<float> (clamped - 8192) / 8192.0f);
    return 0.5f + bipolar * 0.5f;
}

inline float cc_to_normalized (int value) noexcept
{
    return static_cast<float> (gardenia_core::jlimit (0, 127, value)) / 127.0f;
}

enum class EvKind : uint8_t { NoteOn, NoteOff, CC, PitchBend };

struct TimedEvent
{
    uint32_t sample = 0;
    EvKind kind = EvKind::NoteOn;
    int a = 0;          // note / controller / pitch-wheel value
    int b = 0;          // CC value (unused for notes/PB)
    float velocity = 0.0f;
};

struct NoteEvent
{
    uint32_t sample = 0;
    bool on = false;
    int note = 0;
    float velocity = 0.0f;
};

struct CcEvent
{
    uint32_t sample = 0;
    int controller = 0;
    int value = 0;
};

struct PitchBendEvent
{
    uint32_t sample = 0;
    int value = 8192;
};

// Merge like BaselineOfflineEngine::collectMidiForBlock: notes, then CCs, then PBs,
// preserving relative order at identical sample offsets.
inline std::vector<TimedEvent> merge_events (const std::vector<NoteEvent>& notes,
                                             const std::vector<CcEvent>& ccs,
                                             const std::vector<PitchBendEvent>& pbs)
{
    std::vector<TimedEvent> out;
    out.reserve (notes.size() + ccs.size() + pbs.size());
    for (const auto& n : notes)
        out.push_back ({ n.sample, n.on ? EvKind::NoteOn : EvKind::NoteOff, n.note, 0, n.velocity });
    for (const auto& c : ccs)
        out.push_back ({ c.sample, EvKind::CC, c.controller, c.value, 0.0f });
    for (const auto& p : pbs)
        out.push_back ({ p.sample, EvKind::PitchBend, p.value, 0, 0.0f });
    std::stable_sort (out.begin(), out.end(),
                      [] (const TimedEvent& a, const TimedEvent& b) { return a.sample < b.sample; });
    return out;
}

// LIFO mono note stack matching the golden-harness path.
struct NoteStack
{
    static constexpr std::size_t kMax = 128;
    struct Entry { int note = -1; float velocity = 0.0f; };
    std::array<Entry, kMax> stack {};
    std::size_t size = 0;

    void clear() { size = 0; }
    bool empty() const { return size == 0; }

    void noteOn (int note, float velocity)
    {
        noteOff (note);
        if (size == kMax)
            --size;
        for (std::size_t i = size; i > 0; --i)
            stack[i] = stack[i - 1];
        stack[0] = { note, velocity };
        ++size;
    }

    void noteOff (int note)
    {
        int idx = -1;
        for (std::size_t i = 0; i < size; ++i)
            if (stack[i].note == note) { idx = static_cast<int> (i); break; }
        if (idx < 0)
            return;
        for (std::size_t i = static_cast<std::size_t> (idx); i + 1 < size; ++i)
            stack[i] = stack[i + 1];
        --size;
    }

    const Entry& mostRecent() const
    {
        static const Entry dummy { -1, 0.0f };
        return empty() ? dummy : stack[0];
    }
};

struct VoiceSlot
{
    gardenia_core::CoreVoice voice;
    int currentNote = -1;       // JUCE currentlyPlayingNote
    float currentVelocity = 0.0f;
    bool keyDown = false;
    bool sustainPedalDown = false;
    uint32_t noteOnTime = 0;
};

struct StageARender
{
    std::array<VoiceSlot, kVoices> slots {};
    std::array<std::vector<float>, kVoices> stem {};
    std::array<std::vector<float>, kVoices> noise {};
    std::array<std::vector<float>, kVoices> filterEnv {};
    std::array<std::vector<float>, kVoices> lfo1 {};
    std::array<std::vector<float>, kVoices> lfo2 {};
    std::array<std::vector<float>, kVoices> lfo3 {};
    std::array<std::vector<float>, kVoices> voiceLfo {};
    std::array<std::vector<float>, kVoices> cutoffMod {};
    std::array<std::vector<float>, kVoices> cutoffHz {};
    std::array<std::vector<float>, kVoices> resonance {};
    std::array<std::vector<float>, kVoices> signalGain {};
    std::array<std::vector<float>, kVoices> noiseGain {};
    std::array<std::vector<float>, kVoices> fuzzMix {};
    std::array<std::vector<float>, kVoices> vca {};
    std::array<std::vector<float>, kVoices> pitch {};
    std::array<std::vector<uint8_t>, kVoices> active {};
    std::array<std::vector<uint8_t>, kVoices> gate {};
    std::vector<float> scratch;

    bool monoMode = false;
    bool sustainPedalsDown = false;
    bool monoSustainPedalDown = false;
    uint32_t lastNoteOnCounter = 0;
    NoteStack noteStack {};

    void prepare (uint32_t samples, const gardenia_core::Patch& patch, const gardenia_core::Part& part,
                  bool delayBypass, bool reverbBypass, float bpm,
                  uint8_t wavetableModel = gardenia_core::WT_MODEL_AMBIKA,
                  bool mono = false)
    {
        monoMode = mono;
        sustainPedalsDown = false;
        monoSustainPedalDown = false;
        lastNoteOnCounter = 0;
        noteStack.clear();
        scratch.assign (static_cast<size_t> (kBs), 0.0f);

        for (int i = 0; i < kVoices; ++i)
        {
            auto allocF = [samples] (std::vector<float>& v) { v.assign (samples, 0.0f); };
            auto allocU = [samples] (std::vector<uint8_t>& v) { v.assign (samples, 0); };
            const size_t vi = static_cast<size_t> (i);
            allocF (stem[vi]);
            allocF (noise[vi]);
            allocF (filterEnv[vi]);
            allocF (lfo1[vi]);
            allocF (lfo2[vi]);
            allocF (lfo3[vi]);
            allocF (voiceLfo[vi]);
            allocF (cutoffMod[vi]);
            allocF (cutoffHz[vi]);
            allocF (resonance[vi]);
            allocF (signalGain[vi]);
            allocF (noiseGain[vi]);
            allocF (fuzzMix[vi]);
            allocF (vca[vi]);
            allocF (pitch[vi]);
            allocU (active[vi]);
            allocU (gate[vi]);

            auto& s = slots[vi];
            s.voice.init (kSr);
            s.voice.setPatch (patch);
            s.voice.setPart (part);
            s.voice.setTempoBpm (bpm);
            s.voice.setDelayBypass (delayBypass);
            s.voice.setReverbBypass (reverbBypass);
            s.voice.setWavetableModel (wavetableModel);
            s.voice.kill();
            s.voice.setRandomSeed (0x9e3779b9u + static_cast<uint32_t> (i) * 0x85ebca6bu);
            s.voice.resetAllControllers();
            s.currentNote = -1;
            s.currentVelocity = 0.0f;
            s.keyDown = false;
            s.sustainPedalDown = false;
            s.noteOnTime = 0;
        }
    }

    // Voice activity follows envelope state (voice_.isActive()).
    // (envelope), not JUCE's currentlyPlayingNote check.
    bool juceVoiceActive (int i) const
    {
        return slots[static_cast<size_t> (i)].voice.isActive()
            || slots[static_cast<size_t> (i)].voice.hasTail();
    }

    bool isPlayingButReleased (int i) const
    {
        const auto& s = slots[static_cast<size_t> (i)];
        return s.currentNote >= 0 && ! s.keyDown && ! s.sustainPedalDown;
    }

    int findFreeVoice() const
    {
        for (int i = 0; i < kVoices; ++i)
            if (! juceVoiceActive (i))
                return i;
        return -1;
    }

    int findVoiceToSteal (int midiNoteNumber) const
    {
        std::array<int, kVoices> order {};
        int n = 0;
        for (int i = 0; i < kVoices; ++i)
            if (juceVoiceActive (i))
                order[static_cast<size_t> (n++)] = i;

        std::sort (order.begin(), order.begin() + n, [&] (int a, int b) {
            return slots[static_cast<size_t> (a)].noteOnTime < slots[static_cast<size_t> (b)].noteOnTime;
        });

        int low = -1, top = -1;
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            if (isPlayingButReleased (i))
                continue;
            const int note = slots[static_cast<size_t> (i)].currentNote;
            if (low < 0 || note < slots[static_cast<size_t> (low)].currentNote)
                low = i;
            if (top < 0 || note > slots[static_cast<size_t> (top)].currentNote)
                top = i;
        }
        if (top == low)
            top = -1;

        auto isProt = [&] (int i) { return i == low || i == top; };

        for (int k = 0; k < n; ++k)
            if (slots[static_cast<size_t> (order[static_cast<size_t> (k)])].currentNote == midiNoteNumber)
                return order[static_cast<size_t> (k)];

        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            if (! isProt (i) && isPlayingButReleased (i))
                return i;
        }
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            if (! isProt (i) && ! slots[static_cast<size_t> (i)].keyDown)
                return i;
        }
        for (int k = 0; k < n; ++k)
        {
            const int i = order[static_cast<size_t> (k)];
            if (! isProt (i))
                return i;
        }
        if (top >= 0)
            return top;
        return low >= 0 ? low : 0;
    }

    void stopVoice (int idx, bool allowTailOff)
    {
        auto& s = slots[static_cast<size_t> (idx)];
        // stopNote always clears the current note assignment.
        s.currentNote = -1;
        s.keyDown = false;
        s.sustainPedalDown = false;
        if (allowTailOff)
            s.voice.release();
        else
            s.voice.kill();
    }

    void startVoice (int idx, int note, float velocity)
    {
        auto& s = slots[static_cast<size_t> (idx)];
        if (s.currentNote >= 0)
            stopVoice (idx, false);

        s.currentNote = note;
        s.currentVelocity = velocity;
        s.keyDown = true;
        s.sustainPedalDown = sustainPedalsDown;
        s.noteOnTime = ++lastNoteOnCounter;
        s.voice.trigger (note, velocity, false);
    }

    void polyNoteOn (int note, float velocity)
    {
        const float vel = midi_velocity_roundtrip (velocity);
        for (int i = 0; i < kVoices; ++i)
            if (slots[static_cast<size_t> (i)].currentNote == note)
                stopVoice (i, true);

        int idx = findFreeVoice();
        if (idx < 0)
            idx = findVoiceToSteal (note);
        startVoice (idx, note, vel);
    }

    void polyNoteOff (int note)
    {
        for (int i = 0; i < kVoices; ++i)
        {
            auto& s = slots[static_cast<size_t> (i)];
            if (s.currentNote != note)
                continue;
            s.keyDown = false;
            if (! (s.sustainPedalDown))
                stopVoice (i, true);
        }
    }

    void handleSustain (bool isDown)
    {
        if (isDown)
        {
            sustainPedalsDown = true;
            for (int i = 0; i < kVoices; ++i)
                if (slots[static_cast<size_t> (i)].currentNote >= 0
                    && slots[static_cast<size_t> (i)].keyDown)
                    slots[static_cast<size_t> (i)].sustainPedalDown = true;
        }
        else
        {
            for (int i = 0; i < kVoices; ++i)
            {
                auto& s = slots[static_cast<size_t> (i)];
                if (s.currentNote < 0)
                    continue;
                s.sustainPedalDown = false;
                if (! s.keyDown)
                    stopVoice (i, true);
            }
            sustainPedalsDown = false;
        }
    }

    void applyControllerAllVoices (int controller, int value)
    {
        if (controller == 64)
        {
            handleSustain (value >= 64);
            return;
        }
        const float n = cc_to_normalized (value);
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = slots[static_cast<size_t> (i)].voice;
            switch (controller)
            {
                case 1:  v.setModWheelNormalized (n); break;
                case 2:  v.setWheel2Normalized (n); break;
                case 11: v.setExpressionNormalized (n); break;
                case 121: v.resetAllControllers(); break;
                default: break;
            }
        }
    }

    void applyPitchBendAllVoices (int wheel)
    {
        const float n = pitch_wheel_to_normalized (wheel);
        for (int i = 0; i < kVoices; ++i)
            slots[static_cast<size_t> (i)].voice.setPitchBendNormalized (n);
    }

    void applyPolyEvent (const TimedEvent& ev)
    {
        switch (ev.kind)
        {
            case EvKind::NoteOn:  polyNoteOn (ev.a, ev.velocity); break;
            case EvKind::NoteOff: polyNoteOff (ev.a); break;
            case EvKind::CC:     applyControllerAllVoices (ev.a, ev.b); break;
            case EvKind::PitchBend: applyPitchBendAllVoices (ev.a); break;
        }
    }

    // Harness mono: all block events applied, then one full-block render (empty MIDI).
    void applyMonoEvent (const TimedEvent& ev)
    {
        auto& s0 = slots[0];
        switch (ev.kind)
        {
            case EvKind::NoteOn:
            {
                const float vel = midi_velocity_roundtrip (ev.velocity);
                noteStack.noteOn (ev.a, vel);
                if (noteStack.size == 1)
                {
                    s0.currentNote = ev.a;
                    s0.currentVelocity = vel;
                    s0.keyDown = true;
                    s0.voice.trigger (ev.a, vel, false);
                }
                else
                {
                    s0.currentNote = ev.a;
                    s0.currentVelocity = vel;
                    s0.keyDown = true;
                    s0.voice.trigger (ev.a, vel, true);
                }
                break;
            }
            case EvKind::NoteOff:
            {
                noteStack.noteOff (ev.a);
                if (noteStack.empty() && ! monoSustainPedalDown)
                {
                    s0.currentNote = -1;
                    s0.keyDown = false;
                    s0.voice.release();
                }
                else if (! noteStack.empty())
                {
                    const auto& top = noteStack.mostRecent();
                    s0.currentNote = top.note;
                    s0.currentVelocity = top.velocity;
                    s0.voice.trigger (top.note, top.velocity, true);
                }
                break;
            }
            case EvKind::CC:
                if (ev.a == 64)
                {
                    monoSustainPedalDown = ev.b >= 64;
                    if (! monoSustainPedalDown && noteStack.empty())
                    {
                        s0.currentNote = -1;
                        s0.keyDown = false;
                        s0.voice.release();
                    }
                }
                else
                {
                    const float n = cc_to_normalized (ev.b);
                    switch (ev.a)
                    {
                        case 1:  s0.voice.setModWheelNormalized (n); break;
                        case 2:  s0.voice.setWheel2Normalized (n); break;
                        case 11: s0.voice.setExpressionNormalized (n); break;
                        case 121: s0.voice.resetAllControllers(); break;
                        default: break;
                    }
                }
                break;
            case EvKind::PitchBend:
                s0.voice.setPitchBendNormalized (pitch_wheel_to_normalized (ev.a));
                break;
        }
    }

    void renderChunk (uint32_t writeOffset, int numSamples)
    {
        for (int i = 0; i < kVoices; ++i)
        {
            auto& s = slots[static_cast<size_t> (i)];
            // Render while JUCE would keep the voice (note assigned) OR envelope still active
            // after clearCurrentNote + release (tail audio / Stage-A traces).
            if (s.currentNote < 0 && ! s.voice.isActive() && ! s.voice.hasTail())
                continue;

            gardenia_core::StageABuffers buf;
            const size_t vi = static_cast<size_t> (i);
            buf.stem = stem[vi].data();
            buf.noise = noise[vi].data();
            buf.filterEnv = filterEnv[vi].data();
            buf.lfo1 = lfo1[vi].data();
            buf.lfo2 = lfo2[vi].data();
            buf.lfo3 = lfo3[vi].data();
            buf.voiceLfo = voiceLfo[vi].data();
            buf.cutoffMod = cutoffMod[vi].data();
            buf.cutoffHz = cutoffHz[vi].data();
            buf.resonance = resonance[vi].data();
            buf.signalGain = signalGain[vi].data();
            buf.noiseGain = noiseGain[vi].data();
            buf.fuzzMix = fuzzMix[vi].data();
            buf.vca = vca[vi].data();
            buf.pitch = pitch[vi].data();
            buf.active = active[vi].data();
            buf.gate = gate[vi].data();
            buf.capacity = static_cast<uint32_t> (stem[vi].size());
            buf.writeOffset = writeOffset;
            s.voice.setStageABuffers (&buf);

            std::fill (scratch.begin(), scratch.begin() + numSamples, 0.0f);
            s.voice.processBlock (scratch.data(), numSamples);
            s.voice.setStageABuffers (nullptr);
        }
    }

    void render (uint32_t totalSamples, const std::vector<TimedEvent>& events)
    {
        ScopedNoDenormals noDenormals;

        static constexpr int kMinimumSubBlockSize = 32;
        static constexpr bool kSubBlockSubdivisionIsStrict = false;

        uint32_t pos = 0;
        size_t evIdx = 0;
        while (pos < totalSamples)
        {
            const uint32_t blockEnd = std::min (pos + static_cast<uint32_t> (kBs), totalSamples);
            const int blockSamples = static_cast<int> (blockEnd - pos);

            std::vector<TimedEvent> blockEvents;
            while (evIdx < events.size() && events[evIdx].sample >= pos && events[evIdx].sample < blockEnd)
                blockEvents.push_back (events[evIdx++]);

            if (monoMode)
            {
                // Phase-2 harness mono: apply every event in the block, then render the full block.
                for (const auto& ev : blockEvents)
                    applyMonoEvent (ev);
                renderChunk (pos, blockSamples);
            }
            else
            {
                int startSample = 0;
                int numSamples = blockSamples;
                bool firstEvent = true;
                size_t it = 0;

                while (numSamples > 0)
                {
                    if (it >= blockEvents.size())
                    {
                        renderChunk (pos + static_cast<uint32_t> (startSample), numSamples);
                        break;
                    }

                    const int samplesToNext = static_cast<int> (blockEvents[it].sample - pos) - startSample;

                    if (samplesToNext >= numSamples)
                    {
                        renderChunk (pos + static_cast<uint32_t> (startSample), numSamples);
                        while (it < blockEvents.size())
                            applyPolyEvent (blockEvents[it++]);
                        break;
                    }

                    const int threshold = (firstEvent && ! kSubBlockSubdivisionIsStrict)
                                        ? 1
                                        : kMinimumSubBlockSize;
                    if (samplesToNext < threshold)
                    {
                        applyPolyEvent (blockEvents[it++]);
                        continue;
                    }

                    firstEvent = false;
                    if (samplesToNext > 0)
                        renderChunk (pos + static_cast<uint32_t> (startSample), samplesToNext);
                    applyPolyEvent (blockEvents[it++]);
                    startSample += samplesToNext;
                    numSamples -= samplesToNext;
                }

                while (it < blockEvents.size())
                    applyPolyEvent (blockEvents[it++]);
            }

            pos = blockEnd;
        }
    }
};

} // namespace gardenia_core_test
