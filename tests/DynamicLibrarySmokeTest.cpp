/*
  DynamicLibrarySmokeTest.cpp — LoadLibrary the built gardenia-core.dll (test-only).
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <string>

#include <gardenia_core/gardenia_core.h>

using CreateFn = gardenia_core_instance* (GARDENIA_CORE_CALL*)(const gardenia_core_create_config*);
using DestroyFn = void (GARDENIA_CORE_CALL*)(gardenia_core_instance*);
using AbiMajorFn = uint32_t (GARDENIA_CORE_CALL*)(void);
using PrepareFn = gardenia_core_result (GARDENIA_CORE_CALL*)(gardenia_core_instance*, double, uint32_t);

static std::wstring dll_path()
{
#ifdef _DEBUG
    const char* dir = GARDENIA_CORE_DLL_DIR_DEBUG;
#else
    const char* dir = GARDENIA_CORE_DLL_DIR_RELEASE;
#endif
    std::string p = std::string(dir) + "\\gardenia-core.dll";
    return std::wstring(p.begin(), p.end());
}

int main()
{
    const auto path = dll_path();
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (mod == nullptr)
        mod = LoadLibraryW(path.c_str());
    if (mod == nullptr)
    {
        std::cerr << "FAIL LoadLibrary GetLastError=" << GetLastError() << "\n";
        return 1;
    }

    auto* getMajor = reinterpret_cast<AbiMajorFn>(GetProcAddress(mod, "gardenia_core_get_abi_version_major"));
    auto* create = reinterpret_cast<CreateFn>(GetProcAddress(mod, "gardenia_core_create"));
    auto* destroy = reinterpret_cast<DestroyFn>(GetProcAddress(mod, "gardenia_core_destroy"));
    auto* prepare = reinterpret_cast<PrepareFn>(GetProcAddress(mod, "gardenia_core_prepare"));
    auto* processA = reinterpret_cast<decltype(&gardenia_core_process_stage_a)>(
        GetProcAddress(mod, "gardenia_core_process_stage_a"));
    auto* processB = reinterpret_cast<decltype(&gardenia_core_process_stage_b_fuzz)>(
        GetProcAddress(mod, "gardenia_core_process_stage_b_fuzz"));
    auto* snapInit = reinterpret_cast<decltype(&gardenia_core_snapshot_init_defaults)>(
        GetProcAddress(mod, "gardenia_core_snapshot_init_defaults"));

    if (! getMajor || ! create || ! destroy || ! prepare || ! processA || ! processB || ! snapInit)
    {
        std::cerr << "FAIL missing exports\n";
        FreeLibrary(mod);
        return 1;
    }
    if (getMajor() != GARDENIA_CORE_ABI_VERSION_MAJOR)
    {
        std::cerr << "FAIL abi major mismatch\n";
        FreeLibrary(mod);
        return 1;
    }

    gardenia_core_create_config cfg {};
    cfg.struct_size = sizeof(cfg);
    cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
    cfg.maximum_voice_slots = 8;
    cfg.maximum_block_size = 64;
    cfg.sample_rate = 48000.0;
    cfg.deterministic_seed = 1;

    auto* inst = create(&cfg);
    if (inst == nullptr)
    {
        std::cerr << "FAIL create via DLL\n";
        FreeLibrary(mod);
        return 1;
    }
    if (prepare(inst, 48000.0, 64) != GARDENIA_CORE_OK)
    {
        std::cerr << "FAIL prepare via DLL\n";
        destroy(inst);
        FreeLibrary(mod);
        return 1;
    }

    gardenia_core_synthesis_snapshot snap {};
    snap.struct_size = sizeof(snap);
    snap.abi_version = GARDENIA_CORE_ABI_VERSION;
    snapInit(&snap);

    float stem[64] {};
    gardenia_core_voice_output out {};
    out.struct_size = sizeof(out);
    out.abi_version = GARDENIA_CORE_ABI_VERSION;
    out.voice_slot = 0;
    out.stem = stem;
    out.buffer_capacity = 64;
    out.sample_stride = 1;

    gardenia_core_event ev {};
    ev.struct_size = sizeof(ev);
    ev.abi_version = GARDENIA_CORE_ABI_VERSION;
    ev.type = GARDENIA_CORE_EVENT_VOICE_START;
    ev.voice_slot = 0;
    ev.note = 60;
    ev.velocity = 0.8f;

    if (processA(inst, &snap, &ev, 1, &out, 1, 64) != GARDENIA_CORE_OK)
    {
        std::cerr << "FAIL stage_a via DLL\n";
        destroy(inst);
        FreeLibrary(mod);
        return 1;
    }

    float fin[64] {}, fout[64] {}, amt[64] {};
    for (int i = 0; i < 64; ++i)
        amt[i] = 0.25f;
    gardenia_core_fuzz_io fio {};
    fio.struct_size = sizeof(fio);
    fio.abi_version = GARDENIA_CORE_ABI_VERSION;
    fio.voice_slot = 0;
    fio.input = fin;
    fio.amount = amt;
    fio.output = fout;
    fio.buffer_capacity = 64;
    fio.input_stride = fio.control_stride = fio.output_stride = 1;
    if (processB(inst, &fio, 1, 64) != GARDENIA_CORE_OK)
    {
        std::cerr << "FAIL stage_b via DLL\n";
        destroy(inst);
        FreeLibrary(mod);
        return 1;
    }

    destroy(inst);
    FreeLibrary(mod);
    std::cout << "PASS DynamicLibrarySmoke\n";
    return 0;
}
