/*
 * tileharvest: plays the game headless and records every 8x8 tile it displays,
 * producing a reference graphics pack (see rt/hdpack.h) to repaint:
 *   OUTDIR/pack.txt                  manifest (scale 1)
 *   OUTDIR/<context>_bg.png          background tiles, grouped as 16x16 blocks
 *   OUTDIR/<context>_sprites.png     sprite tiles
 * Contexts: title, worldmap, pause, shop, level01..level17. A pattern shown with
 * several palettes in one context (colour cycling, fades) gets one cell and a
 * "tilep" line, so one drawing covers all its colours.
 *
 * usage: tileharvest ROM OUTDIR [--frames N] [--seeds K]
 * Passes: title + demos, then every level played with random input (K seeds,
 * N frames each, infinite lives, pause map opened now and then).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/hdpack.h"
#include "rt/runtime.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb/stb_image_write.h"
#pragma GCC diagnostic pop

#define SHEET_COLS 16
#define MAX_SHEETS 64
#define MAX_KEYS 65536

typedef struct Sheet {
    char name[64];
    uint32_t *px;    /* RGBA as 0xAABBGGRR for stbi_write_png */
    int rows;        /* in cells */
    int cursor;      /* next free cell index */
    int block_mode;  /* 2x2 blocks */
} Sheet;

typedef struct Entry {
    uint64_t key;           /* appearance */
    uint64_t pattern;       /* pattern (palette indexes) */
    int context;
    bool variant;           /* seen with other colours in the same context */
    int sheet, col, row;
} Entry;

static Sheet sheets[MAX_SHEETS];
static int nsheets;
static Entry entries[MAX_KEYS];
static int nentries;
static uint64_t *known;       /* open-addressing set of keys */
static int known_cap = 1 << 17;
static int cur_context;
static bool key_known(uint64_t k);
static void key_add(uint64_t k);

/* (pattern, context) -> entry index, open addressing. */
static uint64_t *pc_keys;
static int *pc_entry;
#define PC_CAP (1 << 17)

static uint64_t pc_key(uint64_t pattern, int context) {
    uint64_t k = pattern ^ ((uint64_t)(context + 1) * 0xD6E8FEB86659FD93ull);
    return k ? k : 2;
}
static int pc_find(uint64_t k) {
    for (uint32_t i = (uint32_t)k & (PC_CAP - 1);; i = (i + 1) & (PC_CAP - 1)) {
        if (pc_keys[i] == k) return pc_entry[i];
        if (pc_keys[i] == 0) return -1;
    }
}
static void pc_add(uint64_t k, int e) {
    for (uint32_t i = (uint32_t)k & (PC_CAP - 1);; i = (i + 1) & (PC_CAP - 1)) {
        if (pc_keys[i] == 0 || pc_keys[i] == k) {
            pc_keys[i] = k;
            pc_entry[i] = e;
            return;
        }
    }
}

/* A tile not seen before in this appearance: true if it needs its own cell. A
 * pattern already present in this context with other colours becomes a variant. */
static bool needs_cell(uint64_t key, uint64_t pattern) {
    if (key_known(key)) return false;
    int e = pc_find(pc_key(pattern, cur_context));
    if (e >= 0) {
        entries[e].variant = true;
        key_add(key);
        return false;
    }
    return true;
}

static uint8_t *rom;
static uint32_t rom_size;
static long frame_no, frames_per_pass = 6000;
static int pass_level;
static uint32_t rng;
static uint8_t held;
static int held_left;

static bool key_known(uint64_t k) {
    uint32_t m = (uint32_t)known_cap - 1;
    for (uint32_t i = (uint32_t)k & m;; i = (i + 1) & m) {
        if (known[i] == k) return true;
        if (known[i] == 0) return false;
    }
}
static void key_add(uint64_t k) {
    uint32_t m = (uint32_t)known_cap - 1;
    for (uint32_t i = (uint32_t)k & m;; i = (i + 1) & m) {
        if (known[i] == k) return;
        if (known[i] == 0) {
            known[i] = k;
            return;
        }
    }
}

static Sheet *sheet_for(const char *name, int block_mode) {
    for (int i = 0; i < nsheets; i++)
        if (!strcmp(sheets[i].name, name)) return &sheets[i];
    Sheet *s = &sheets[nsheets++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->block_mode = block_mode;
    return s;
}

static void sheet_ensure_rows(Sheet *s, int rows) {
    if (rows <= s->rows) return;
    int new_rows = rows + 8;
    s->px = realloc(s->px, sizeof(uint32_t) * (size_t)(SHEET_COLS * 8 * new_rows * 8));
    memset(s->px + SHEET_COLS * 8 * s->rows * 8, 0, sizeof(uint32_t) * (size_t)(SHEET_COLS * 8 * (new_rows - s->rows) * 8));
    s->rows = new_rows;
}

/* Draws tile `tile` with palette half `half` into cell (col,row); dim = already defined elsewhere. */
static void draw_cell(Sheet *s, int col, int row, int tile, int half, bool dim) {
    sheet_ensure_rows(s, row + 1);
    const Vdp *v = &mach.vdp;
    for (int y = 0; y < 8; y++) {
        const uint8_t *r = &v->vram[(tile * 32 + y * 4) & 0x3FFF];
        for (int x = 0; x < 8; x++) {
            int bit = 7 - x;
            int c = ((r[0] >> bit) & 1) | (((r[1] >> bit) & 1) << 1) | (((r[2] >> bit) & 1) << 2) | (((r[3] >> bit) & 1) << 3);
            uint32_t argb = vdp_color(v->cram[half * 16 + c]);
            uint32_t rr = (argb >> 16) & 255, gg = (argb >> 8) & 255, bb = argb & 255;
            uint32_t a = (half && c == 0) ? 0 : 255;
            if (dim) { rr /= 3; gg /= 3; bb /= 3; }
            s->px[(row * 8 + y) * SHEET_COLS * 8 + col * 8 + x] = a << 24 | bb << 16 | gg << 8 | rr;
        }
    }
}

static void add_entry(uint64_t key, uint64_t pattern, int sheet, int col, int row) {
    if (nentries >= MAX_KEYS) return;
    entries[nentries] = (Entry){key, pattern, cur_context, false, sheet, col, row};
    pc_add(pc_key(pattern, cur_context), nentries);
    nentries++;
    key_add(key);
}

static const char *context_name(int context) {
    static char buf[32];
    switch (context) {
    case 100: return "title";
    case 101: return "worldmap";
    case 102: return "pause";
    case 103: return "shop";
    default:
        snprintf(buf, sizeof(buf), "level%02d", context);
        return buf;
    }
}

static void harvest_frame(void) {
    const Vdp *v = &mach.vdp;
    if (!(v->reg[1] & 0x40)) return; /* display off: loading */
    char name[64];
    cur_context = hdpack_context(mach.ram);
    const char *ctx = context_name(cur_context);
    /* Background: 2x2 blocks of the name table. */
    snprintf(name, sizeof(name), "%s_bg", ctx);
    Sheet *bg = sheet_for(name, 1);
    int bgi = (int)(bg - sheets);
    uint16_t nt = (uint16_t)((v->reg[2] & 0x0E) << 10);
    for (int by = 0; by < 28; by += 2) {
        for (int bx = 0; bx < 32; bx += 2) {
            int tiles[4], halves[4];
            uint64_t keys[4], patterns[4];
            bool fresh[4], any_new = false;
            for (int k = 0; k < 4; k++) {
                int cy = by + k / 2, cx = bx + k % 2;
                uint16_t ea = (uint16_t)(nt + (cy * 32 + cx) * 2);
                uint16_t entry = (uint16_t)(v->vram[ea & 0x3FFF] | (v->vram[(ea + 1) & 0x3FFF] << 8));
                tiles[k] = entry & 0x1FF;
                halves[k] = (entry & 0x0800) ? 1 : 0;
                keys[k] = hdpack_tile_key(v, tiles[k], halves[k]);
                patterns[k] = hdpack_pattern_key(v, tiles[k], halves[k]);
                fresh[k] = needs_cell(keys[k], patterns[k]);
                if (fresh[k]) any_new = true;
            }
            if (!any_new) continue;
            int cell = bg->cursor;
            bg->cursor += 4;
            int bcol = (cell / 4) % (SHEET_COLS / 2), brow = (cell / 4) / (SHEET_COLS / 2);
            for (int k = 0; k < 4; k++) {
                int col = bcol * 2 + k % 2, row = brow * 2 + k / 2;
                bool mine = fresh[k] && !key_known(keys[k]); /* a block may repeat a tile */
                draw_cell(bg, col, row, tiles[k], halves[k], !mine);
                if (mine) add_entry(keys[k], patterns[k], bgi, col, row);
            }
        }
    }
    /* Sprites. */
    snprintf(name, sizeof(name), "%s_sprites", ctx);
    Sheet *sp = sheet_for(name, 0);
    int spi = (int)(sp - sheets);
    uint16_t sat = (uint16_t)((v->reg[5] & 0x7E) << 7);
    int base = (v->reg[6] & 0x04) ? 256 : 0;
    int tall = (v->reg[1] & 0x02) ? 2 : 1;
    for (int i = 0; i < 64; i++) {
        uint8_t y = v->vram[(sat + i) & 0x3FFF];
        if (y == 0xD0) break;
        int n = v->vram[(sat + 0x81 + i * 2) & 0x3FFF];
        if (tall == 2) n &= 0xFE;
        for (int part = 0; part < tall; part++) {
            int tile = (base + n + part) & 511;
            uint64_t key = hdpack_tile_key(v, tile, 1);
            uint64_t pattern = hdpack_pattern_key(v, tile, 1);
            if (!needs_cell(key, pattern)) continue;
            int cell = sp->cursor++;
            int col = cell % SHEET_COLS, row = cell / SHEET_COLS;
            draw_cell(sp, col, row, tile, 1, false);
            add_entry(key, pattern, spi, col, row);
        }
    }
}

static uint32_t next_rand(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void frame_hook(void) {
    frame_no++;
    uint8_t state = mach.ram[0x001F];
    harvest_frame();
    if (pass_level == 0) { /* title and demos */
        mach.joy = 0;
    } else {
        if ((state & 0x0F) == 3) mach.ram[0x0023] = (uint8_t)pass_level;
        if ((state & 0x0F) >= 3) mach.ram[0x0025] = 3;
        if (frame_no < 200) {
            mach.joy = (frame_no % 40) < 5 ? JOY_BTN1 : 0;
        } else {
            if (held_left-- <= 0) {
                uint32_t r = next_rand();
                static const uint8_t moves[] = {JOY_RIGHT, JOY_RIGHT, JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP, 0};
                held = moves[r % sizeof(moves)];
                if (r & 0x100) held |= JOY_BTN1;
                if (r & 0x200) held |= JOY_BTN2;
                held_left = 5 + (int)((r >> 12) % 40);
                if ((r >> 24) % 61 == 0) rt_nmi_pending = 1;
            }
            mach.joy = held;
        }
    }
    if (frame_no >= frames_per_pass) rt_quit();
}

static void run_pass(int level, uint32_t seed) {
    pass_level = level;
    frame_no = 0;
    rng = seed * 2654435761u + 7;
    held_left = 0;
    machine_init(&mach, rom, rom_size);
    memset(&cpu, 0, sizeof(cpu));
    rt_nmi_pending = 0;
    rt_frame_hook = frame_hook;
    rt_run();
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s ROM OUTDIR [--frames N] [--seeds K]\n", argv[0]);
        return 2;
    }
    int seeds = 2;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frames")) frames_per_pass = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--seeds")) seeds = atoi(argv[i + 1]);
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t buf[0x20000];
    rom_size = (uint32_t)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    rom = buf;
    known = calloc((size_t)known_cap, sizeof(uint64_t));
    pc_keys = calloc(PC_CAP, sizeof(uint64_t));
    pc_entry = calloc(PC_CAP, sizeof(int));

    run_pass(0, 1);
    for (int level = 1; level <= 17; level++)
        for (int s = 1; s <= seeds; s++) run_pass(level, (uint32_t)(level * 100 + s));

    char path[1024];
    snprintf(path, sizeof(path), "%s/pack.txt", argv[2]);
    FILE *m = fopen(path, "w");
    if (!m) {
        fprintf(stderr, "cannot write %s (does the directory exist?)\n", path);
        return 1;
    }
    fprintf(m, "# Reference graphics pack harvested from the original game.\n"
               "# Repaint the sheets (any scale: multiply the sheet size by N and set 'scale N').\n"
               "# Background sheets show 16x16 blocks; dimmed cells are defined in another sheet.\n"
               "scale 1\n");
    for (int si = 0; si < nsheets; si++) {
        Sheet *s = &sheets[si];
        int used_rows = s->block_mode ? ((s->cursor / 4 + SHEET_COLS / 2 - 1) / (SHEET_COLS / 2)) * 2
                                      : (s->cursor + SHEET_COLS - 1) / SHEET_COLS;
        if (used_rows == 0) continue;
        snprintf(path, sizeof(path), "%s/%s.png", argv[2], s->name);
        stbi_write_png(path, SHEET_COLS * 8, used_rows * 8, 4, s->px, SHEET_COLS * 8 * 4);
        fprintf(m, "\nsheet %s.png\n", s->name);
        for (int e = 0; e < nentries; e++) {
            const Entry *en = &entries[e];
            if (en->sheet != si) continue;
            fprintf(m, "tile %d %d %016llx\n", en->col, en->row, (unsigned long long)en->key);
            if (en->variant)
                fprintf(m, "tilep %d %d %016llx %d\n", en->col, en->row, (unsigned long long)en->pattern, en->context);
        }
    }
    fclose(m);
    int variants = 0;
    for (int e = 0; e < nentries; e++) variants += entries[e].variant;
    fprintf(stderr, "%d tiles (%d with colour variants) in %d sheets written to %s\n", nentries, variants, nsheets, argv[2]);
    return 0;
}
