#include "Oscillator.h"
#include "Envelope.h"
#include "LFO.h"
#include "SubAndTransient.h"
#include <cstdint>

int main()
{
    gardenia_core::Oscillator o;
    o.init (48000.0);
    gardenia_core::Envelope e;
    e.init (48000.0);
    gardenia_core::LFO l;
    l.init (48000.0);
    float buf[64] {};
    const double incr = 440.0 / 48000.0;
    o.render (static_cast<uint8_t> (gardenia_core::WAVEFORM_SAW), 69, incr,
              nullptr, nullptr, buf, 64);
    return 0;
}
