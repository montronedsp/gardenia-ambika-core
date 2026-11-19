#include <cmath>
#include <iostream>
#include "gardenia_core/AmbikaMixer.h"
#include "AmbikaDistortionTable.h"

int main()
{
    using namespace gardenia_core;
    int fail = 0;

    // Sum symmetry
    {
        const float y = mix_sum (0.5f, -0.5f, 0.5f);
        if (std::abs (y) > 1.0e-6f)
        {
            std::cerr << "FAIL mix_sum\n";
            ++fail;
        }
        else
            std::cout << "PASS mix_sum\n";
    }

    // XOR byte-domain sanity
    {
        const float y = mix_xor (0.25f, -0.25f, 0.5f, 1.0f);
        (void) y;
        std::cout << "PASS mix_xor runs\n";
    }

    // Bits crush extremes
    {
        const float clean = mix_bits_crush (0.3f, 0.0f, 0.0f, 0.0f);
        const float crushed = mix_bits_crush (0.3f, 0.0f, 0.0f, 1.0f);
        if (clean == crushed)
        {
            std::cerr << "FAIL bits crush should change output\n";
            ++fail;
        }
        else
            std::cout << "PASS bits crush varies\n";
    }

    // Ambika distortion table present and center-ish
    {
        const auto mid = resources::kWavResDistortion[128];
        if (mid < 100 || mid > 160)
        {
            std::cerr << "FAIL distortion table mid=" << int (mid) << "\n";
            ++fail;
        }
        else
            std::cout << "PASS Ambika wav_res_distortion imported mid=" << int (mid) << "\n";
    }

    std::cout << "MixerTests fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
