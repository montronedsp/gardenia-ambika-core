// Ensures GardeniaCoreStatic has a translation unit.
#include "gardenia_core/gardenia_core_types.h"
#include "gardenia_core/AmbikaFuzzProcessor.h"
#include "gardenia_core/AmbikaMixer.h"
#include "AmbikaDistortionTable.h"

namespace gardenia_core
{
int core_library_anchor() noexcept
{
    return kCoreApiVersion
         + static_cast<int> (resources::kWavResDistortion[128]);
}
}
