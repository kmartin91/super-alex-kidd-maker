/*
 * entityicons: captures what each entity type looks like, for the level editor.
 *
 * For every TYPE:LEVEL argument the game runs twice, headless and deterministic,
 * to a few frames into LEVEL: once with an entity of TYPE placed on screen and
 * once without. The sprite pixels that differ are the entity's appearance.
 *
 * usage: entityicons ROM OUT.json TYPE:LEVEL [TYPE:LEVEL...]
 * Output: {"<type>": {"w", "h", "dx", "dy", "rgba": base64}}; (dx, dy) is the
 * top-left corner of the image relative to the entity's position.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/runtime.h"

#define SPAWN_SLOT 15          /* last normal entity slot */
#define SPAWN_X 0x80
#define SPAWN_Y 0x70
#define SETTLE_FRAMES 40       /* gameplay frames before spawning */
#define WATCH_FRAMES 6         /* frames after spawning */

static uint8_t *rom;
static uint32_t rom_size;
static int want_level, want_type, spawn;
static long frame_no, gameplay_frames;
static bool poked;
static uint32_t sprites[VDP_WIDTH * VDP_HEIGHT]; /* 0 = transparent */
static int ent_x, ent_y;

static void render_sprites(const Vdp *v, uint32_t *out) {
    const uint8_t *r = v->reg;
    uint16_t sat = (uint16_t)((r[5] & 0x7E) << 7);
    uint16_t base = (r[6] & 0x04) ? 0x2000 : 0;
    int h = (r[1] & 0x02) ? 16 : 8;
    memset(out, 0, sizeof(uint32_t) * VDP_WIDTH * VDP_HEIGHT);
    for (int i = 63; i >= 0; i--) {  /* lower index drawn last = on top */
        int n_active = 0;
        for (int k = 0; k < 64; k++) {
            if (v->vram[(sat + k) & 0x3FFF] == 0xD0) break;
            n_active++;
        }
        if (i >= n_active) continue;
        int sy = v->vram[(sat + i) & 0x3FFF] + 1;
        if (sy > 240) sy -= 256;
        int sx = v->vram[(sat + 0x80 + i * 2) & 0x3FFF];
        int n = v->vram[(sat + 0x81 + i * 2) & 0x3FFF];
        if (h == 16) n &= 0xFE;
        if (r[0] & 0x08) sx -= 8;
        for (int y = 0; y < h; y++) {
            int py = sy + y;
            if (py < 0 || py >= VDP_HEIGHT) continue;
            const uint8_t *row = &v->vram[(base + n * 32 + y * 4) & 0x3FFF];
            for (int x = 0; x < 8; x++) {
                int px = sx + x;
                if (px < 0 || px >= VDP_WIDTH) continue;
                int bit = 7 - x;
                int c = ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
                        (((row[3] >> bit) & 1) << 3);
                if (c) out[py * VDP_WIDTH + px] = vdp_color(v->cram[16 + c]);
            }
        }
    }
}

static void frame_hook(void) {
    frame_no++;
    uint8_t state = mach.ram[0x001F];
    mach.joy = (frame_no > 60 && frame_no < 66) ? JOY_BTN1 : 0;
    if (!poked && (state & 0x0F) == 3) {
        mach.ram[0x0023] = (uint8_t)want_level;
        mach.ram[0x0025] = 3;
        poked = true;
    }
    if (poked && state == 0x8A) gameplay_frames++;
    if (gameplay_frames == SETTLE_FRAMES && spawn) {
        uint8_t *e = &mach.ram[0x0300 + SPAWN_SLOT * 0x20];
        memset(e, 0, 0x20);
        e[0x00] = (uint8_t)want_type;
        /* As the entity loader does ($6F9F): x is relative to v_horizontalScroll ($C0AF). */
        e[0x0B] = mach.ram[0x00AF];
        e[0x0C] = (uint8_t)(SPAWN_X + mach.ram[0x00B0]);
        e[0x0E] = SPAWN_Y;
    }
    if (gameplay_frames == SETTLE_FRAMES + WATCH_FRAMES) {
        render_sprites(&mach.vdp, sprites);
        const uint8_t *e = &mach.ram[0x0300 + SPAWN_SLOT * 0x20];
        ent_x = e[0x0C] - mach.ram[0x00B0];
        ent_y = e[0x0E];
        rt_quit();
    }
    if (frame_no > 20000) rt_quit();
}

static bool run(int level, int type, bool with_entity) {
    want_level = level;
    want_type = type;
    spawn = with_entity;
    frame_no = gameplay_frames = 0;
    poked = false;
    machine_init(&mach, rom, rom_size);
    memset(&cpu, 0, sizeof(cpu));
    rt_frame_hook = frame_hook;
    rt_run();
    if (getenv("ICONS_DEBUG"))
        fprintf(stderr, "run level %d type $%02X spawn %d: %ld frames, %ld in gameplay\n", level, type, with_entity,
                frame_no, gameplay_frames);
    return gameplay_frames >= SETTLE_FRAMES + WATCH_FRAMES;
}

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
        fprintf(stderr, "usage: %s ROM OUT.json TYPE:LEVEL...\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t buf[0x20000];
    rom_size = (uint32_t)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    rom = buf;
    FILE *out = fopen(argv[2], "w");
    if (!out) return 2;
    fprintf(out, "{");
    int written = 0;
    static uint32_t without[VDP_WIDTH * VDP_HEIGHT];
    for (int a = 3; a < argc; a++) {
        int type, level;
        if (sscanf(argv[a], "%i:%i", &type, &level) != 2) continue;
        if (!run(level, type, false)) continue;
        memcpy(without, sprites, sizeof(sprites));
        if (!run(level, type, true)) continue;
        int x0 = VDP_WIDTH, y0 = VDP_HEIGHT, x1 = -1, y1 = -1;
        for (int y = 0; y < VDP_HEIGHT; y++)
            for (int x = 0; x < VDP_WIDTH; x++) {
                int i = y * VDP_WIDTH + x;
                if (sprites[i] && sprites[i] != without[i]) {
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
            }
        if (x1 < 0) {
            fprintf(stderr, "type $%02X: nothing visible in level %d\n", type, level);
            continue;
        }
        int w = x1 - x0 + 1, h = y1 - y0 + 1;
        uint8_t *rgba = calloc((size_t)(w * h), 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int i = (y0 + y) * VDP_WIDTH + x0 + x;
                uint32_t c = sprites[i];
                if (!c || c == without[i]) continue;
                uint8_t *p = &rgba[(y * w + x) * 4];
                p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; p[3] = 255;
            }
        fprintf(out, "%s\n \"%d\": {\"w\": %d, \"h\": %d, \"dx\": %d, \"dy\": %d, \"rgba\": \"", written ? "," : "",
                type, w, h, x0 - ent_x, y0 - ent_y);
        b64(out, rgba, (size_t)(w * h * 4));
        fprintf(out, "\"}");
        free(rgba);
        written++;
    }
    fprintf(out, "\n}\n");
    fclose(out);
    fprintf(stderr, "%d icon(s) written to %s\n", written, argv[2]);
    return 0;
}
