#include "hdpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb/stb_image.h"
#pragma GCC diagnostic pop

typedef struct HdTile {
    uint64_t key;
    uint32_t *argb; /* (8*scale)^2 pixels */
} HdTile;

struct HdPack {
    int scale;
    int count;
    int capacity; /* power of two, open addressing */
    HdTile *slots;
};

#define TRANSPARENT_MARK 0x40

int hdpack_context(const uint8_t *ram) {
    switch (ram[0x001F] & 0x0F) { /* v_gameState */
    case 0: case 1: return 100;
    case 3: return 101;
    case 5: return 103;
    case 0xB: return 102;
    default: return ram[0x0023]; /* v_level */
    }
}

uint64_t hdpack_pattern_key(const Vdp *v, int tile, int half) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)(half + 1) * 0x9E3779B97F4A7C15ull;
    const uint8_t *pat = &v->vram[(tile * 32) & 0x3FFF];
    for (int i = 0; i < 32; i++) {
        h ^= pat[i];
        h *= 1099511628211ull;
    }
    return h ? h : 1;
}

uint64_t hdpack_tile_key(const Vdp *v, int tile, int half) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t *pat = &v->vram[(tile * 32) & 0x3FFF];
    for (int y = 0; y < 8; y++) {
        const uint8_t *row = pat + y * 4;
        for (int x = 0; x < 8; x++) {
            int bit = 7 - x;
            int c = ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
                    (((row[3] >> bit) & 1) << 3);
            uint8_t val = (half && c == 0) ? TRANSPARENT_MARK : (uint8_t)(v->cram[half * 16 + c] & 0x3F);
            h ^= val;
            h *= 1099511628211ull;
        }
    }
    return h ? h : 1;
}

static inline uint64_t context_key(uint64_t pattern_key, int context) {
    uint64_t k = pattern_key ^ ((uint64_t)(context + 1) * 0xD6E8FEB86659FD93ull);
    return k ? k : 2;
}

static const HdTile *find(const HdPack *p, uint64_t key) {
    if (!p || !p->count) return NULL;
    uint32_t mask = (uint32_t)p->capacity - 1;
    for (uint32_t i = (uint32_t)key & mask;; i = (i + 1) & mask) {
        if (p->slots[i].key == key) return &p->slots[i];
        if (p->slots[i].key == 0) return NULL;
    }
}

static void insert(HdPack *p, uint64_t key, uint32_t *argb) {
    if ((p->count + 1) * 2 > p->capacity) {
        int cap = p->capacity ? p->capacity * 2 : 1024;
        HdTile *old = p->slots;
        int old_cap = p->capacity;
        p->slots = calloc((size_t)cap, sizeof(HdTile));
        p->capacity = cap;
        p->count = 0;
        for (int i = 0; i < old_cap; i++)
            if (old[i].key) insert(p, old[i].key, old[i].argb);
        free(old);
    }
    uint32_t mask = (uint32_t)p->capacity - 1;
    for (uint32_t i = (uint32_t)key & mask;; i = (i + 1) & mask) {
        if (p->slots[i].key == key) { /* first definition wins */
            free(argb);
            return;
        }
        if (p->slots[i].key == 0) {
            p->slots[i].key = key;
            p->slots[i].argb = argb;
            p->count++;
            return;
        }
    }
}

HdPack *hdpack_load(const char *dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/pack.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "pack: cannot read %s\n", path);
        return NULL;
    }
    HdPack *p = calloc(1, sizeof(HdPack));
    p->scale = 1;
    unsigned char *sheet = NULL;
    int sw = 0, sh = 0;
    char line[512], sheet_name[256] = "";
    int lineno = 0, errors = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == 0) continue;
        int col, row, context;
        unsigned long long key;
        char name[256];
        bool is_tile = false;
        if (sscanf(s, "tilep %d %d %llx %d", &col, &row, &key, &context) == 4) {
            key = context_key((uint64_t)key, context);
            is_tile = true;
        } else if (sscanf(s, "tile %d %d %llx", &col, &row, &key) == 3) {
            is_tile = true;
        } else if (sscanf(s, "scale %d", &p->scale) == 1) {
            if (p->scale < 1 || p->scale > 8) p->scale = 1;
        } else if (sscanf(s, "sheet %255s", name) == 1) {
            if (sheet) stbi_image_free(sheet);
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            int n;
            sheet = stbi_load(path, &sw, &sh, &n, 4);
            snprintf(sheet_name, sizeof(sheet_name), "%s", name);
            if (!sheet) {
                fprintf(stderr, "pack: cannot load %s (%s)\n", path, stbi_failure_reason());
                errors++;
            }
        }
        if (!is_tile) continue;
        int size = 8 * p->scale;
        if (!sheet || (col + 1) * size > sw || (row + 1) * size > sh) {
            if (errors++ < 5) fprintf(stderr, "pack: %s line %d: cell %d,%d outside sheet %s\n", dir, lineno, col, row, sheet_name);
            continue;
        }
        uint32_t *argb = malloc(sizeof(uint32_t) * (size_t)(size * size));
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                const unsigned char *px = &sheet[((row * size + y) * sw + col * size + x) * 4];
                argb[y * size + x] = (uint32_t)px[3] << 24 | (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
            }
        insert(p, (uint64_t)key, argb);
    }
    if (sheet) stbi_image_free(sheet);
    fclose(f);
    fprintf(stderr, "pack: %d tile(s) at scale %d from %s%s\n", p->count, p->scale, dir, errors ? " (with errors)" : "");
    return p;
}

void hdpack_free(HdPack *p) {
    if (!p) return;
    for (int i = 0; i < p->capacity; i++) free(p->slots[i].argb);
    free(p->slots);
    free(p);
}

int hdpack_scale(const HdPack *p) { return p ? p->scale : 1; }
int hdpack_tile_count(const HdPack *p) { return p ? p->count : 0; }

static inline int pattern_pixel(const Vdp *v, int tile, int row, int col) {
    const uint8_t *r = &v->vram[(tile * 32 + row * 4) & 0x3FFF];
    int bit = 7 - col;
    return ((r[0] >> bit) & 1) | (((r[1] >> bit) & 1) << 1) | (((r[2] >> bit) & 1) << 2) | (((r[3] >> bit) & 1) << 3);
}

typedef struct LineSprite {
    int x, top, tile, zoom;
    const HdTile *sub[2]; /* tile and tile+1 (8x16) */
} LineSprite;

static const HdTile *lookup(const HdPack *p, const Vdp *v, int tile, int half, int context) {
    const HdTile *t = find(p, hdpack_tile_key(v, tile, half));
    if (!t) t = find(p, context_key(hdpack_pattern_key(v, tile, half), context));
    return t;
}

void hdpack_render(const Vdp *v, const HdPack *p, int context, int scale, uint32_t *out) {
    if (p) scale = p->scale;
    const int W = VDP_WIDTH * scale;
    uint32_t pal[32];
    for (int i = 0; i < 32; i++) pal[i] = vdp_color(v->cram[i]);
    const uint8_t *r = v->reg;
    uint32_t backdrop = pal[16 + (r[7] & 0x0F)];
    if (!(r[1] & 0x40)) {
        for (int i = 0; i < W * VDP_HEIGHT * scale; i++) out[i] = backdrop;
        return;
    }
    uint16_t nt = (uint16_t)((r[2] & 0x0E) << 10);
    uint16_t sat = (uint16_t)((r[5] & 0x7E) << 7);
    int spr_base = (r[6] & 0x04) ? 256 : 0;
    int spr_h = (r[1] & 0x02) ? 16 : 8;
    int zoom = (r[1] & 0x01) ? 2 : 1;

    /* Per-frame substitution cache per (tile, palette half). */
    static const HdTile *cache[512][2];
    static uint8_t cached[512][2];
    memset(cached, 0, sizeof(cached));
#define SUB(t, half) \
    (p ? (cached[(t)][(half)] ? cache[(t)][(half)] \
                              : (cached[(t)][(half)] = 1, cache[(t)][(half)] = lookup(p, v, (t), (half), context))) \
       : NULL)

    int sprite_count = 0;
    while (sprite_count < 64 && v->vram[(sat + sprite_count) & 0x3FFF] != 0xD0) sprite_count++;

    for (int y = 0; y < VDP_HEIGHT; y++) {
        LineSprite ls[8];
        int nls = 0;
        for (int i = 0; i < sprite_count && nls < 8; i++) {
            int top = v->vram[(sat + i) & 0x3FFF] + 1;
            if (top > 240) top -= 256;
            int dy = y - top;
            if (dy < 0 || dy >= spr_h * zoom) continue;
            LineSprite *s = &ls[nls++];
            s->top = top;
            s->zoom = zoom;
            s->x = v->vram[(sat + 0x80 + i * 2) & 0x3FFF] - ((r[0] & 0x08) ? 8 : 0);
            int n = v->vram[(sat + 0x81 + i * 2) & 0x3FFF];
            if (spr_h == 16) n &= 0xFE;
            s->tile = spr_base + n;
            s->sub[0] = SUB(s->tile & 511, 1);
            s->sub[1] = spr_h == 16 ? SUB((s->tile + 1) & 511, 1) : NULL;
        }
        int hs = ((r[0] & 0x40) && y < 16) ? 0 : r[8];
        for (int x = 0; x < VDP_WIDTH; x++) {
            uint32_t *o = out + (y * scale) * W + x * scale;
            if ((r[0] & 0x20) && x < 8) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) o[sy * W + sx] = backdrop;
                continue;
            }
            int vs = ((r[0] & 0x80) && x >= 192) ? 0 : r[9];
            int by = (y + vs) % 224, bx = (x - hs) & 0xFF;
            uint16_t ea = (uint16_t)(nt + ((by >> 3) * 32 + (bx >> 3)) * 2);
            uint16_t entry = (uint16_t)(v->vram[ea & 0x3FFF] | (v->vram[(ea + 1) & 0x3FFF] << 8));
            int tile = entry & 0x1FF, half = (entry & 0x0800) ? 1 : 0;
            bool hflip = entry & 0x0200, vflip = entry & 0x0400;
            int prow = vflip ? 7 - (by & 7) : (by & 7);
            int pcol = hflip ? 7 - (bx & 7) : (bx & 7);
            int bgc = pattern_pixel(v, tile, prow, pcol);
            bool bg_prio = (entry & 0x1000) && bgc != 0;
            const HdTile *bgsub = SUB(tile, half);

            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    uint32_t color;
                    if (bgsub) {
                        int size = 8 * scale;
                        int u = (bx & 7) * scale + sx, w = (by & 7) * scale + sy;
                        if (hflip) u = size - 1 - u;
                        if (vflip) w = size - 1 - w;
                        color = bgsub->argb[w * size + u] | 0xFF000000u;
                    } else {
                        color = pal[half * 16 + bgc];
                    }
                    if (!bg_prio) {
                        for (int k = 0; k < nls; k++) {
                            const LineSprite *s = &ls[k];
                            int dx = x - s->x;
                            if (dx < 0 || dx >= 8 * s->zoom) continue;
                            int dy = y - s->top;
                            int U = (dx * scale + sx) / s->zoom, V = (dy * scale + sy) / s->zoom;
                            int part = V / (8 * scale);
                            int t = (s->tile + part) & 511;
                            const HdTile *ss = s->sub[part];
                            int vv = V % (8 * scale);
                            if (ss) {
                                uint32_t px = ss->argb[vv * 8 * scale + U];
                                if (px >> 24 < 128) continue;
                                color = px | 0xFF000000u;
                            } else {
                                int c = pattern_pixel(v, t, vv / scale, U / scale);
                                if (!c) continue;
                                color = pal[16 + c];
                            }
                            break;
                        }
                    }
                    o[sy * W + sx] = color;
                }
            }
        }
    }
#undef SUB
}
