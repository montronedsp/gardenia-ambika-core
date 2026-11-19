/*
  AbiLayoutTests.cpp — layout / ABI version assertions + optional manifest.
*/
#include <gardenia_core/gardenia_core.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static_assert(sizeof(int32_t) == 4, "int32");
static_assert(sizeof(uint32_t) == 4, "uint32");
static_assert(sizeof(uint64_t) == 8, "uint64");
static_assert(sizeof(float) == 4, "float");
static_assert(sizeof(double) == 8, "double");

int main()
{
    if (gardenia_core_get_abi_version_major() != 1u
        || gardenia_core_get_abi_version_minor() != 0u
        || gardenia_core_get_abi_version_patch() != 0u)
    {
        std::cerr << "FAIL abi version numbers\n";
        return 1;
    }

    const std::size_t createSz = sizeof(gardenia_core_create_config);
    const std::size_t snapSz = sizeof(gardenia_core_synthesis_snapshot);
    const std::size_t evSz = sizeof(gardenia_core_event);
    const std::size_t outSz = sizeof(gardenia_core_voice_output);
    const std::size_t fuzzSz = sizeof(gardenia_core_fuzz_io);

    static_assert (sizeof (gardenia_core_create_config) >= 64,
                   "Unexpected create_config layout");
    static_assert (sizeof (gardenia_core_synthesis_snapshot)
                       >= GARDENIA_CORE_PATCH_BLOB_BYTES,
                   "Unexpected synthesis_snapshot layout");

#ifdef GARDENIA_CORE_BUILD_DIR
    {
#if defined(_WIN32)
        CreateDirectoryA((std::string(GARDENIA_CORE_BUILD_DIR) + "\\abi").c_str(), nullptr);
#endif
        const std::string path = std::string(GARDENIA_CORE_BUILD_DIR) + "/abi/gardenia-core-abi-v1.json";
        std::ofstream out(path);
        if (out)
        {
            out << "{\n"
                << "  \"abi_major\": 1,\n"
                << "  \"abi_minor\": 0,\n"
                << "  \"abi_patch\": 0,\n"
                << "  \"create_config_size\": " << createSz << ",\n"
                << "  \"snapshot_size\": " << snapSz << ",\n"
                << "  \"event_size\": " << evSz << ",\n"
                << "  \"voice_output_size\": " << outSz << ",\n"
                << "  \"fuzz_io_size\": " << fuzzSz << ",\n"
                << "  \"max_voice_slots\": " << GARDENIA_CORE_MAX_VOICE_SLOTS << ",\n"
                << "  \"max_block_size\": " << GARDENIA_CORE_MAX_BLOCK_SIZE << "\n"
                << "}\n";
        }
    }
#endif

    std::cout << "PASS AbiLayout"
              << " create=" << createSz
              << " snap=" << snapSz
              << " event=" << evSz
              << " out=" << outSz
              << " fuzz=" << fuzzSz << "\n";
    return 0;
}
