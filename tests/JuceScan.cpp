#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

// Scans gardenia-core sources for forbidden JUCE / host-filter symbols.
int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::path (__FILE__).parent_path().parent_path();

    const std::regex forbidden (
        R"(JuceHeader|juce::|AudioBuffer|MidiBuffer|MidiMessage|ValueTree|APVTS|AudioProcessorValueTreeState|SynthesiserVoice|#include\s*<JuceHeader)",
        std::regex::icase);

    const std::regex privateDsp (
        R"(Acid303Filter|Acid303Mapping|UnifiedFilter|WdfLowPassFilter|chowdsp_wdf|BrightSkiesDelay|BrightSkiesReverb)");

    int hits = 0;
    for (auto& p : fs::recursive_directory_iterator (root))
    {
        if (! p.is_regular_file())
            continue;
        const auto ext = p.path().extension().string();
        if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".cc" && ext != ".md")
            continue;
        // Allow README / notices to mention JUCE exclusion in prose.
        // HostReplayAdapter.h is TEST-ONLY host-allocation replay documentation.
        if (p.path().filename() == "README.md"
            || p.path().filename() == "BUILDING.md"
            || p.path().filename() == "THIRD_PARTY_NOTICES.md"
            || p.path().filename() == "JuceScan.cpp"
            || p.path().filename() == "HostReplayAdapter.h")
            continue;
        // Skip generated build trees and local tooling.
        {
            const auto pathStr = p.path().generic_string();
            if (pathStr.find ("/build/") != std::string::npos || pathStr.find ("/tools/") != std::string::npos)
                continue;
        }

        std::ifstream in (p.path());
        std::string line;
        size_t lineNo = 0;
        while (std::getline (in, line))
        {
            ++lineNo;
            if (std::regex_search (line, forbidden) || std::regex_search (line, privateDsp))
            {
                std::cerr << "FORBIDDEN " << p.path() << ":" << lineNo << " :: " << line << "\n";
                ++hits;
            }
        }
    }

    if (hits != 0)
    {
        std::cerr << "JuceScan FAIL hits=" << hits << "\n";
        return 1;
    }
    std::cout << "PASS JuceScan (no JUCE/private-filter implementation symbols in core sources)\n";
    return 0;
}
