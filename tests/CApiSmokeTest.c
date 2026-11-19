/*
  CApiSmokeTest.c — Pure C client of gardenia_core.h (no C++).
*/
#include <gardenia_core/gardenia_core.h>
#include <stdio.h>
#include <string.h>

static int fail_count = 0;

static void expect_ok(gardenia_core_result r, const char* what)
{
    if (r != GARDENIA_CORE_OK)
    {
        printf("FAIL %s -> %s (%d)\n", what, gardenia_core_result_string(r), (int)r);
        ++fail_count;
    }
}

int main(void)
{
    float stem[64];
    float fuzz_in[64];
    float fuzz_amt[64];
    float fuzz_out[64];
    uint32_t i;

    if (gardenia_core_get_abi_version_major() != GARDENIA_CORE_ABI_VERSION_MAJOR)
    {
        printf("FAIL abi major\n");
        return 1;
    }
    if (gardenia_core_get_implementation_version() == NULL
        || gardenia_core_get_license_identifier() == NULL)
    {
        printf("FAIL version strings\n");
        return 1;
    }

    {
        gardenia_core_capabilities caps;
        memset(&caps, 0, sizeof(caps));
        caps.struct_size = (uint32_t)sizeof(caps);
        caps.abi_version = GARDENIA_CORE_ABI_VERSION;
        expect_ok(gardenia_core_get_capabilities(&caps), "capabilities");
        if (caps.maximum_voice_slots != GARDENIA_CORE_MAX_VOICE_SLOTS)
        {
            printf("FAIL voice slots\n");
            ++fail_count;
        }
    }

    if (gardenia_core_create(NULL) != NULL)
    {
        printf("FAIL null create\n");
        ++fail_count;
    }

    {
        gardenia_core_create_config cfg;
        gardenia_core_instance* inst;
        gardenia_core_synthesis_snapshot snap;
        gardenia_core_event ev;
        gardenia_core_voice_output out;
        gardenia_core_fuzz_io fio;

        memset(&cfg, 0, sizeof(cfg));
        cfg.struct_size = (uint32_t)sizeof(cfg);
        cfg.abi_version = GARDENIA_CORE_ABI_VERSION;
        cfg.maximum_voice_slots = 8;
        cfg.maximum_block_size = 64;
        cfg.sample_rate = 48000.0;
        cfg.deterministic_seed = 0x9e3779b9ull;

        inst = gardenia_core_create(&cfg);
        if (inst == NULL)
        {
            printf("FAIL create\n");
            return 1;
        }

        expect_ok(gardenia_core_prepare(inst, 48000.0, 64), "prepare");
        expect_ok(gardenia_core_reset(inst, 0x9e3779b9ull), "reset");

        memset(&snap, 0, sizeof(snap));
        snap.struct_size = (uint32_t)sizeof(snap);
        snap.abi_version = GARDENIA_CORE_ABI_VERSION;
        expect_ok(gardenia_core_snapshot_init_defaults(&snap), "snapshot defaults");

        memset(&ev, 0, sizeof(ev));
        ev.struct_size = (uint32_t)sizeof(ev);
        ev.abi_version = GARDENIA_CORE_ABI_VERSION;
        ev.type = GARDENIA_CORE_EVENT_VOICE_START;
        ev.sample_offset = 0;
        ev.voice_slot = 0;
        ev.note = 60;
        ev.velocity = 0.8f;

        memset(&out, 0, sizeof(out));
        out.struct_size = (uint32_t)sizeof(out);
        out.abi_version = GARDENIA_CORE_ABI_VERSION;
        out.voice_slot = 0;
        out.stem = stem;
        out.buffer_capacity = 64;
        out.sample_stride = 1;
        for (i = 0; i < 64; ++i)
            stem[i] = 0.0f;

        expect_ok(gardenia_core_process_stage_a(inst, &snap, &ev, 1, &out, 1, 64), "stage_a");

        for (i = 0; i < 64; ++i)
        {
            fuzz_in[i] = stem[i];
            fuzz_amt[i] = 0.5f;
            fuzz_out[i] = 0.0f;
        }
        memset(&fio, 0, sizeof(fio));
        fio.struct_size = (uint32_t)sizeof(fio);
        fio.abi_version = GARDENIA_CORE_ABI_VERSION;
        fio.voice_slot = 0;
        fio.input = fuzz_in;
        fio.amount = fuzz_amt;
        fio.output = fuzz_out;
        fio.buffer_capacity = 64;
        fio.input_stride = 1;
        fio.control_stride = 1;
        fio.output_stride = 1;
        expect_ok(gardenia_core_process_stage_b_fuzz(inst, &fio, 1, 64), "stage_b");

        gardenia_core_destroy(inst);
        gardenia_core_destroy(NULL);
    }

    if (fail_count != 0)
    {
        printf("CApiSmoke FAIL count=%d\n", fail_count);
        return 1;
    }
    printf("PASS CApiSmoke\n");
    return 0;
}
