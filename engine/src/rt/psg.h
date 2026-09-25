/* Texas Instruments SN76489 as found in the Sega Master System. */
#ifndef RT_PSG_H
#define RT_PSG_H

#include <stdint.h>

typedef struct Psg {
    uint16_t tone[3];     /* 10-bit periods */
    uint8_t noise;        /* noise control (3 bits) */
    uint8_t vol[4];       /* attenuation, 15 = silent */
    uint8_t latch;        /* latched register 0..7 */
    /* generator state */
    uint16_t counter[4];
    uint8_t output[4];
    uint16_t lfsr;
    double phase;         /* fractional PSG clocks carried between renders */
} Psg;

void psg_reset(Psg *p);
void psg_write(Psg *p, uint8_t value);
/* Renders `count` mono samples at `rate` Hz. */
void psg_render(Psg *p, int16_t *out, int count, int rate);

#endif
