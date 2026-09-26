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

#include "capture.h"

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

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s ROM LEVEL OUT.json [--frames-after N]\n", argv[0]);
        return 2;
    }
    int level = atoi(argv[2]);
    long settle = 30;
    if (argc > 5 && !strcmp(argv[4], "--frames-after")) settle = atol(argv[5]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t rom[0x20000];
    size_t n = fread(rom, 1, sizeof(rom), f);
    fclose(f);
    static LevelVideo v;
    if (!capture_level_video(rom, (uint32_t)n, level, settle, &v)) {
        fprintf(stderr, "level %d never became playable\n", level);
        return 1;
    }
    FILE *o = fopen(argv[3], "w");
    if (!o) {
        fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    fprintf(o, "{\"level\": %d, \"frame\": %ld, \"slot2\": %d,\n \"vdp_regs\": [", level, v.frame, v.slot2);
    for (int i = 0; i < 11; i++) fprintf(o, "%s%d", i ? ", " : "", v.vdp_regs[i]);
    fprintf(o, "],\n \"cram\": [");
    for (int i = 0; i < 32; i++) fprintf(o, "%s%d", i ? ", " : "", v.cram[i]);
    fprintf(o, "],\n \"vram\": \"");
    b64(o, v.vram, sizeof(v.vram));
    fprintf(o, "\",\n \"ram\": \"");
    b64(o, v.ram, sizeof(v.ram));
    fprintf(o, "\"}\n");
    fclose(o);
    return 0;
}
