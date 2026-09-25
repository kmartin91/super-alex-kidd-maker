/*
 * leveldump: runs the game headless until level N is playable and writes the
 * video state the level editor needs (VRAM tiles, palettes, VDP registers,
 * RAM) as JSON with base64 blobs.
 *
 * usage: leveldump ROM LEVEL OUT.json [--frames-after N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/runtime.h"

static int level;
static long frame_no, gameplay_frames, settle_frames = 30;
static bool poked;
static const char *out_path;

static void b64(FILE *f, const uint8_t *p, size_t n) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        fputc(tab[(v >> 18) & 63], f);
        fputc(tab[(v >> 12) & 63], f);
        fputc(i + 1 < n ? tab[(v >> 6) & 63] : '=', f);
        fputc(i + 2 < n ? tab[v & 63] : '=', f);
    }
}

static void dump(void) {
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out_path);
        exit(1);
    }
    fprintf(f, "{\"level\": %d, \"frame\": %ld, \"slot2\": %d,\n \"vdp_regs\": [", level, frame_no, mach.slot[2]);
    for (int i = 0; i < 11; i++) fprintf(f, "%s%d", i ? ", " : "", mach.vdp.reg[i]);
    fprintf(f, "],\n \"cram\": [");
    for (int i = 0; i < 32; i++) fprintf(f, "%s%d", i ? ", " : "", mach.vdp.cram[i]);
    fprintf(f, "],\n \"vram\": \"");
    b64(f, mach.vdp.vram, sizeof(mach.vdp.vram));
    fprintf(f, "\",\n \"ram\": \"");
    b64(f, mach.ram, sizeof(mach.ram));
    fprintf(f, "\"}\n");
    fclose(f);
}

static void frame_hook(void) {
    frame_no++;
    uint8_t state = mach.ram[0x001F];
    /* Start a game from the title screen. */
    mach.joy = (frame_no > 60 && frame_no < 66) ? JOY_BTN1 : 0;
    if (!poked && (state & 0x0F) == 3) {
        mach.ram[0x0023] = (uint8_t)level; /* v_level */
        poked = true;
    }
    if (poked && state == 0x8A) gameplay_frames++;
    if (gameplay_frames >= settle_frames) {
        dump();
        rt_quit();
    }
    if (frame_no > 20000) {
        fprintf(stderr, "level %d never became playable\n", level);
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s ROM LEVEL OUT.json [--frames-after N]\n", argv[0]);
        return 2;
    }
    level = atoi(argv[2]);
    out_path = argv[3];
    if (argc > 5 && !strcmp(argv[4], "--frames-after")) settle_frames = atol(argv[5]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t rom[0x20000];
    size_t n = fread(rom, 1, sizeof(rom), f);
    fclose(f);
    machine_init(&mach, rom, (uint32_t)n);
    memset(&cpu, 0, sizeof(cpu));
    rt_frame_hook = frame_hook;
    rt_run();
    return 0;
}
