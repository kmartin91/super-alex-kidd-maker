/*
 * Shadow test harness: plays a scenario while every lifted routine is run side
 * by side with its generated translation (see rt/shadow.h), then reports
 * per-routine call and mismatch counts.
 *
 * usage: shadow ROM [scenario options, see scenario.h] [--depth N] [--fail-dir DIR]
 *        shadow ROM --replay FILE     re-run one saved failing call
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/runtime.h"
#include "rt/shadow.h"
#include "scenario.h"

/* Inputs per frame, so that both runs of a shadow session see the same input. */
typedef struct FrameInput {
    uint8_t joy;
    bool nmi;
    uint8_t npokes;
    uint16_t poke_addr[16];
    uint8_t poke_val[16];
} FrameInput;

static FrameInput *log_;
static long log_len, log_cap;
static FrameInput *recording;

static void record_poke(uint16_t addr, uint8_t v) {
    wr8(addr, v);
    if (recording && recording->npokes < 16) {
        recording->poke_addr[recording->npokes] = addr;
        recording->poke_val[recording->npokes] = v;
        recording->npokes++;
    }
}

static void frame_hook(void) {
    long i = rt_shadow_frame_index;
    if (i < log_len) {
        /* Replay (second run of a shadow session). */
        FrameInput *in = &log_[i];
        for (int k = 0; k < in->npokes; k++) wr8(in->poke_addr[k], in->poke_val[k]);
        mach.joy = in->joy;
        if (in->nmi) rt_nmi_pending = 1;
        return;
    }
    /* Once every main-loop handler is lifted, every frame is inside a compared call:
     * stop at the limit anyway (calls still in progress are simply not counted). */
    if (i >= sc_max_frames) {
        int bad = rt_shadow_report();
        printf("%s: %ld frames, %d lifted routine(s) with mismatches\n", bad ? "FAILED" : "OK", i, bad);
        exit(bad ? 1 : 0);
    }
    if (log_len >= log_cap) {
        log_cap = log_cap ? log_cap * 2 : 4096;
        log_ = realloc(log_, (size_t)log_cap * sizeof(FrameInput));
    }
    FrameInput *in = &log_[log_len];
    memset(in, 0, sizeof(*in));
    recording = in;
    scenario_frame(i, record_poke, &in->joy, &in->nmi);
    recording = NULL;
    log_len++;
    mach.joy = in->joy;
    if (in->nmi) rt_nmi_pending = 1;
    if (i % 1200 == 0 && rt_shadow_depth() == 0)
        fprintf(stderr, "frame %ld (state %02X)\n", i, mach.ram[0x001F]);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM [scenario options] [--depth N] [--fail-dir DIR] | --replay FILE\n", argv[0]);
        return 2;
    }
    const char *replay = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) rt_shadow_max_depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fail-dir") && i + 1 < argc) rt_shadow_fail_dir = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!scenario_parse_arg(argc, argv, &i)) {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    uint32_t size;
    uint8_t *rom = scenario_load_rom(argv[1], &size);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    machine_init(&mach, rom, size);
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = 0xFFFF;
    if (replay) return rt_shadow_replay(replay);
    if (rt_shadow_registered_count() == 0) {
        printf("no lifted routines in this build\n");
        return 0;
    }
    scenario_init();
    rt_frame_hook = frame_hook;
    rt_run();
    return 0;
}
