/*
  AbiInvalidInputTests.cpp — robustness / error paths (no crashes).
*/
#include <gardenia_core/gardenia_core.h>
#include <cstring>
#include <iostream>
#include <vector>

static int fails = 0;

static void expect(gardenia_core_result got, gardenia_core_result want, const char* name)
{
    if (got != want)
    {
        std::cerr << "FAIL " << name << " got=" << got << " want=" << want << "\n";
        ++fails;
    }
}

int main()
{
    gardenia_core_destroy(nullptr);

    expect(gardenia_core_prepare(nullptr, 48000.0, 64), GARDENIA_CORE_ERR_NULL_INSTANCE, "prep null");
    expect(gardenia_core_reset(nullptr, 0), GARDENIA_CORE_ERR_NULL_INSTANCE, "reset null");
    expect(gardenia_core_process_stage_a(nullptr, nullptr, nullptr, 0, nullptr, 0, 0),
           GARDENIA_CORE_ERR_NULL_INSTANCE, "a null");
    expect(gardenia_core_process_stage_b_fuzz(nullptr, nullptr, 0, 0),
           GARDENIA_CORE_ERR_NULL_INSTANCE, "b null");

    gardenia_core_create_config bad {};
    bad.struct_size = 4;
    bad.abi_version = GARDENIA_CORE_ABI_VERSION;
    if (gardenia_core_create(&bad) != nullptr)
    {
        std::cerr << "FAIL small create config accepted\n";
        ++fails;
    }

    gardenia_core_create_config cfg {};
    cfg.struct_size = sizeof(cfg);
    cfg.abi_version = (2u << 16); // wrong major
    cfg.maximum_voice_slots = 8;
    cfg.maximum_block_size = 64;
    cfg.sample_rate = 48000.0;
    if (gardenia_core_create(&cfg) != nullptr)
    {
        std::cerr << "FAIL wrong abi major accepted\n";
        ++fails;
    }

    cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
    cfg.maximum_voice_slots = 99;
    if (gardenia_core_create(&cfg) != nullptr)
    {
        std::cerr << "FAIL too many voices accepted\n";
        ++fails;
    }

    cfg.maximum_voice_slots = 8;
    cfg.maximum_block_size = 0;
    if (gardenia_core_create(&cfg) != nullptr)
    {
        std::cerr << "FAIL zero block accepted\n";
        ++fails;
    }

    cfg.maximum_block_size = 64;
    cfg.sample_rate = -1.0;
    if (gardenia_core_create(&cfg) != nullptr)
    {
        std::cerr << "FAIL negative sr accepted\n";
        ++fails;
    }

    cfg.sample_rate = 48000.0;
    cfg.reserved_u32[0] = 1;
    if (gardenia_core_create(&cfg) != nullptr)
    {
        std::cerr << "FAIL non-zero reserved accepted\n";
        ++fails;
    }
    cfg.reserved_u32[0] = 0;

    auto* inst = gardenia_core_create(&cfg);
    if (inst == nullptr)
    {
        std::cerr << "FAIL valid create\n";
        return 1;
    }

    expect(gardenia_core_prepare(inst, 48000.0, 4096), GARDENIA_CORE_ERR_INVALID_ARGUMENT, "prep oversize");

    gardenia_core_synthesis_snapshot snap {};
    snap.struct_size = sizeof(snap);
    snap.abi_version = GARDENIA_CORE_ABI_VERSION;
    gardenia_core_snapshot_init_defaults(&snap);

    gardenia_core_voice_output out {};
    out.struct_size = sizeof(out);
    out.abi_version = GARDENIA_CORE_ABI_VERSION;
    out.voice_slot = 99;
    out.buffer_capacity = 64;
    out.sample_stride = 1;
    expect(gardenia_core_process_stage_a(inst, &snap, nullptr, 0, &out, 1, 64),
           GARDENIA_CORE_ERR_INVALID_VOICE, "bad voice");

    out.voice_slot = 0;
    out.buffer_capacity = 8;
    std::vector<float> tiny(8);
    out.stem = tiny.data();
    expect(gardenia_core_process_stage_a(inst, &snap, nullptr, 0, &out, 1, 64),
           GARDENIA_CORE_ERR_INSUFFICIENT_CAPACITY, "capacity");

    gardenia_core_event ev {};
    ev.struct_size = sizeof(ev);
    ev.abi_version = GARDENIA_CORE_ABI_VERSION;
    ev.type = 99;
    ev.voice_slot = 0;
    ev.sample_offset = 0;
    out.buffer_capacity = 64;
    std::vector<float> buf(64);
    out.stem = buf.data();
    expect(gardenia_core_process_stage_a(inst, &snap, &ev, 1, &out, 1, 64),
           GARDENIA_CORE_ERR_INVALID_EVENT, "bad event type");

    gardenia_core_event e0 {}, e1 {};
    e0.struct_size = e1.struct_size = sizeof(e0);
    e0.abi_version = e1.abi_version = GARDENIA_CORE_ABI_VERSION;
    e0.type = e1.type = GARDENIA_CORE_EVENT_VOICE_START;
    e0.voice_slot = e1.voice_slot = 0;
    e0.sample_offset = 10;
    e1.sample_offset = 5; // unsorted
    gardenia_core_event unsorted[2] = { e0, e1 };
    expect(gardenia_core_process_stage_a(inst, &snap, unsorted, 2, &out, 1, 64),
           GARDENIA_CORE_ERR_INVALID_EVENT, "unsorted");

    gardenia_core_fuzz_io fio {};
    fio.struct_size = sizeof(fio);
    fio.abi_version = GARDENIA_CORE_ABI_VERSION;
    fio.voice_slot = 0;
    fio.input = buf.data();
    fio.output = buf.data(); // overlap
    fio.buffer_capacity = 64;
    fio.input_stride = fio.output_stride = fio.control_stride = 1;
    expect(gardenia_core_process_stage_b_fuzz(inst, &fio, 1, 64),
           GARDENIA_CORE_ERR_INVALID_BUFFER, "overlap");

    expect(gardenia_core_process_stage_a(inst, &snap, nullptr, 0, &out, 1, 0),
           GARDENIA_CORE_OK, "zero samples");

    gardenia_core_destroy(inst);

    if (fails != 0)
    {
        std::cerr << "AbiInvalidInput FAIL count=" << fails << "\n";
        return 1;
    }
    std::cout << "PASS AbiInvalidInput\n";
    return 0;
}
