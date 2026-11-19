#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>

#include "gardenia_core/AmbikaFuzzProcessor.h"
#include "gardenia_core/gardenia_core_types.h"

#ifndef GARDENIA_BASELINE_OUTPUT_DIR
#error GARDENIA_BASELINE_OUTPUT_DIR must be defined
#endif

namespace
{

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

bool floats_bit_identical (float a, float b)
{
    uint32_t ua = 0, ub = 0;
    std::memcpy (&ua, &a, sizeof (float));
    std::memcpy (&ub, &b, sizeof (float));
    return ua == ub;
}

struct Diff
{
    bool equal = true;
    uint32_t first = 0;
    float maxAbs = 0.0f;
    float baseline = 0.0f;
    float core = 0.0f;
};

Diff compare (const std::vector<float>& a, const std::vector<float>& b)
{
    Diff d;
    const auto n = static_cast<uint32_t> (std::min (a.size(), b.size()));
    if (a.size() != b.size())
        d.equal = false;
    for (uint32_t i = 0; i < n; ++i)
    {
        const float err = std::abs (a[i] - b[i]);
        d.maxAbs = std::max (d.maxAbs, err);
        if (d.equal && ! floats_bit_identical (a[i], b[i]))
        {
            d.equal = false;
            d.first = i;
            d.baseline = a[i];
            d.core = b[i];
        }
    }
    return d;
}

bool run_scenario (const std::string& scenarioId, int voiceSlot)
{
    const std::string base = std::string (GARDENIA_BASELINE_OUTPUT_DIR)
                           + "/scenarios/" + scenarioId + "/sr-48000/bs-64/";
    char voiceTag[16];
    std::snprintf (voiceTag, sizeof (voiceTag), "voice-%02d-", voiceSlot);
    const std::string prefix = base + voiceTag;

    std::vector<float> fuzzInput, fuzzMix, afterFuzz;
    // Stage-B input is the pre-fuzz sample stream from the golden corpus.
    if (! load_f32 (prefix + "after-noise-route.f32", fuzzInput)
        || ! load_f32 (prefix + "fuzz-mix.f32", fuzzMix)
        || ! load_f32 (prefix + "after-fuzz.f32", afterFuzz))
    {
        std::cerr << "FAIL load traces for " << scenarioId << " voice " << voiceSlot << "\n";
        return false;
    }

    if (fuzzInput.size() != fuzzMix.size() || fuzzInput.size() != afterFuzz.size())
    {
        std::cerr << "FAIL size mismatch " << scenarioId << "\n";
        return false;
    }

    std::vector<float> coreOut (fuzzInput.size(), 0.0f);
    gardenia_core::AmbikaFuzzProcessor fuzz;
    fuzz.reset();
    fuzz.processBlock (fuzzInput.data(), fuzzMix.data(), coreOut.data(),
                       static_cast<uint32_t> (fuzzInput.size()));

    const auto diff = compare (afterFuzz, coreOut);
    if (! diff.equal)
    {
        std::cerr << "FAIL " << scenarioId << " voice=" << voiceSlot
                  << " first=" << diff.first
                  << " baseline=" << diff.baseline
                  << " core=" << diff.core
                  << " maxAbs=" << diff.maxAbs << "\n";
        return false;
    }

    std::cout << "PASS fuzz " << scenarioId << " voice=" << voiceSlot
              << " samples=" << fuzzInput.size() << "\n";
    return true;
}

} // namespace

int main()
{
    // Pinned golden config: 48000 / 64 / Release traces.
    const char* scenarios[] = {
        "fuzz-distortion",
        "default-single-note",
        "silence-no-midi",
        "wdf-cutoff-res-sweep",
        "acid-cutoff-res-sweep",
        "deterministic-noise",
        "four-note-chord",
        "force-steal-12-notes"
    };

    int fail = 0;
    int pass = 0;
    for (const char* id : scenarios)
    {
        for (int v = 0; v < gardenia_core::kMaxVoices; ++v)
        {
            // Only test slots that have non-empty activity cheaply: always run;
            // inactive slots should still match (zeros through fuzz).
            if (run_scenario (id, v))
                ++pass;
            else
                ++fail;
        }
    }

    // Bypass / min / max synthetic unit checks
    {
        gardenia_core::AmbikaFuzzProcessor fuzz;
        fuzz.reset();
        const float in = 0.25f;
        const float bypass = fuzz.processSample (in, 0.0f);
        if (! floats_bit_identical (bypass, in))
        {
            std::cerr << "FAIL fuzz bypass identity\n";
            ++fail;
        }
        else
        {
            std::cout << "PASS fuzz bypass identity\n";
            ++pass;
        }
    }

    std::cout << "FuzzGolden summary pass=" << pass << " fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
