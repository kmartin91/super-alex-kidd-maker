#include "vdp.h"

#include <string.h>

void vdp_reset(Vdp *v) {
    memset(v, 0, sizeof(*v));
}

void vdp_write_control(Vdp *v, uint8_t value) {
    if (!v->latch) {
        v->latch = true;
        v->latch_byte = value;
        v->addr = (v->addr & 0x3F00) | value;
        return;
    }
    v->latch = false;
    v->addr = (uint16_t)(((value & 0x3F) << 8) | v->latch_byte);
    v->code = value >> 6;
    if (v->code == 0) {
        v->read_buf = v->vram[v->addr];
        v->addr = (v->addr + 1) & 0x3FFF;
    } else if (v->code == 2) {
        v->reg[value & 0x0F] = v->latch_byte;
    }
}

void vdp_write_data(Vdp *v, uint8_t value) {
    v->latch = false;
    if (v->code == 3) {
        v->cram[v->addr & 0x1F] = value;
    } else {
        v->vram[v->addr] = value;
    }
    v->read_buf = value;
    v->addr = (v->addr + 1) & 0x3FFF;
}

uint8_t vdp_read_data(Vdp *v) {
    uint8_t r = v->read_buf;
    v->latch = false;
    v->read_buf = v->vram[v->addr];
    v->addr = (v->addr + 1) & 0x3FFF;
    return r;
}

uint8_t vdp_read_status(Vdp *v) {
    uint8_t r = v->status | 0x1F;
    v->status = 0;
    v->latch = false;
    return r;
}

uint32_t vdp_color(uint8_t c) {
    static const uint8_t lv[4] = {0, 85, 170, 255};
    return 0xFF000000u | ((uint32_t)lv[c & 3] << 16) | ((uint32_t)lv[(c >> 2) & 3] << 8) | lv[(c >> 4) & 3];
}

static inline uint8_t tile_pixel(const uint8_t *row, int bit) {
    return (uint8_t)(((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) |
                     (((row[2] >> bit) & 1) << 2) | (((row[3] >> bit) & 1) << 3));
}

void vdp_render(const Vdp *v, uint32_t *pixels) {
    uint32_t pal[32];
    for (int i = 0; i < 32; i++) pal[i] = vdp_color(v->cram[i]);
    const uint8_t *r = v->reg;
    uint32_t backdrop = pal[16 + (r[7] & 0x0F)];

    if (!(r[1] & 0x40)) {
        for (int i = 0; i < VDP_WIDTH * VDP_HEIGHT; i++) pixels[i] = backdrop;
        return;
    }

    uint16_t nt = (uint16_t)((r[2] & 0x0E) << 10);
    uint16_t sat = (uint16_t)((r[5] & 0x7E) << 7);
    uint16_t spr_base = (r[6] & 0x04) ? 0x2000 : 0x0000;
    int spr_h = (r[1] & 0x02) ? 16 : 8;
    int zoom = (r[1] & 0x01) ? 2 : 1;

    for (int y = 0; y < VDP_HEIGHT; y++) {
        uint32_t *line = pixels + y * VDP_WIDTH;
        uint8_t bg_color[VDP_WIDTH];
        uint8_t bg_prio[VDP_WIDTH];

        int hs = ((r[0] & 0x40) && y < 16) ? 0 : r[8];
        for (int x = 0; x < VDP_WIDTH; x++) {
            int vs = ((r[0] & 0x80) && x >= 192) ? 0 : r[9];
            int by = (y + vs) % 224;
            int bx = (x - hs) & 0xFF;
            uint16_t ea = (uint16_t)(nt + ((by >> 3) * 32 + (bx >> 3)) * 2);
            uint16_t entry = (uint16_t)(v->vram[ea & 0x3FFF] | (v->vram[(ea + 1) & 0x3FFF] << 8));
            int tile = entry & 0x1FF;
            int row = by & 7;
            if (entry & 0x0400) row = 7 - row;
            int bit = bx & 7;
            if (!(entry & 0x0200)) bit = 7 - bit;
            uint8_t c = tile_pixel(&v->vram[(tile * 32 + row * 4) & 0x3FFF], bit);
            bg_color[x] = (uint8_t)(c | ((entry & 0x0800) ? 16 : 0));
            bg_prio[x] = (entry & 0x1000) && c != 0;
        }

        /* Sprites: lower SAT index has priority; at most 8 per line. Rendering is
         * side-effect free, so overflow/collision status bits are not emulated. */
        int8_t spr_drawn[VDP_WIDTH];
        memset(spr_drawn, 0, sizeof(spr_drawn));
        int count = 0;
        for (int i = 0; i < 64; i++) {
            uint8_t sy = v->vram[(sat + i) & 0x3FFF];
            if (sy == 0xD0) break;
            int top = sy + 1;
            if (top > 240) top -= 256;
            int dy = y - top;
            if (dy < 0 || dy >= spr_h * zoom) continue;
            if (++count > 8) break;
            int sx = v->vram[(sat + 0x80 + i * 2) & 0x3FFF];
            int n = v->vram[(sat + 0x81 + i * 2) & 0x3FFF];
            if (spr_h == 16) n &= 0xFE;
            if (r[0] & 0x08) sx -= 8;
            int prow = dy / zoom;
            const uint8_t *prow_data = &v->vram[(spr_base + n * 32 + prow * 4) & 0x3FFF];
            for (int px = 0; px < 8 * zoom; px++) {
                int x = sx + px;
                if (x < 0 || x >= VDP_WIDTH) continue;
                uint8_t c = tile_pixel(prow_data, 7 - px / zoom);
                if (c == 0) continue;
                if (spr_drawn[x]) continue;
                spr_drawn[x] = 1;
                if (!bg_prio[x]) bg_color[x] = (uint8_t)(16 + c);
            }
        }

        for (int x = 0; x < VDP_WIDTH; x++) line[x] = pal[bg_color[x]];
        if (r[0] & 0x20)
            for (int x = 0; x < 8; x++) line[x] = backdrop;
    }
}
