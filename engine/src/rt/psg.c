#include "psg.h"

#include <string.h>

#define PSG_CLOCK (3579545.0 / 16.0)

/* 2 dB per step, scaled so four channels at full volume stay within int16. */
static const int16_t volume_table[16] = {
    8000, 6355, 5048, 4009, 3185, 2530, 2010, 1596,
    1268, 1007, 800, 635, 505, 401, 318, 0,
};

void psg_reset(Psg *p) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < 4; i++) p->vol[i] = 15;
    p->lfsr = 0x8000;
}

void psg_write(Psg *p, uint8_t value) {
    if (value & 0x80) {
        p->latch = (value >> 4) & 7;
        uint8_t data = value & 0x0F;
        switch (p->latch) {
        case 0: case 2: case 4:
            p->tone[p->latch >> 1] = (uint16_t)((p->tone[p->latch >> 1] & 0x3F0) | data);
            break;
        case 6:
            p->noise = data & 7;
            p->lfsr = 0x8000;
            break;
        default:
            p->vol[p->latch >> 1] = data;
            break;
        }
        return;
    }
    switch (p->latch) {
    case 0: case 2: case 4:
        p->tone[p->latch >> 1] = (uint16_t)((p->tone[p->latch >> 1] & 0x00F) | ((value & 0x3F) << 4));
        break;
    case 6:
        p->noise = value & 7;
        p->lfsr = 0x8000;
        break;
    default:
        p->vol[p->latch >> 1] = value & 0x0F;
        break;
    }
}

static void psg_clock(Psg *p) {
    for (int ch = 0; ch < 3; ch++) {
        if (p->counter[ch] > 0) p->counter[ch]--;
        if (p->counter[ch] == 0) {
            p->counter[ch] = p->tone[ch];
            /* Period 0 or 1 holds the output high (used for sample playback). */
            p->output[ch] = p->tone[ch] <= 1 ? 1 : (uint8_t)(p->output[ch] ^ 1);
        }
    }
    if (p->counter[3] > 0) p->counter[3]--;
    if (p->counter[3] == 0) {
        uint8_t rate = p->noise & 3;
        p->counter[3] = rate == 3 ? (uint16_t)(p->tone[2] * 2) : (uint16_t)(0x20 << rate);
        if (p->counter[3] == 0) p->counter[3] = 1;
        uint16_t feedback = (p->noise & 4) ? ((p->lfsr ^ (p->lfsr >> 3)) & 1) : (p->lfsr & 1);
        p->lfsr = (uint16_t)((p->lfsr >> 1) | (feedback << 15));
        p->output[3] = p->lfsr & 1;
    }
}

void psg_render(Psg *p, int16_t *out, int count, int rate) {
    double step = PSG_CLOCK / rate;
    for (int i = 0; i < count; i++) {
        p->phase += step;
        int ticks = 0;
        int32_t acc = 0;
        while (p->phase >= 1.0) {
            p->phase -= 1.0;
            psg_clock(p);
            int32_t s = 0;
            for (int ch = 0; ch < 4; ch++)
                s += p->output[ch] ? volume_table[p->vol[ch]] : -volume_table[p->vol[ch]];
            acc += s;
            ticks++;
        }
        out[i] = (int16_t)(ticks ? acc / ticks : 0);
    }
}
