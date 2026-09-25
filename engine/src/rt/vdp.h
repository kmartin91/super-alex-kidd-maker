/* Sega Master System VDP (TMS9918-derived, mode 4 only). */
#ifndef RT_VDP_H
#define RT_VDP_H

#include <stdbool.h>
#include <stdint.h>

#define VDP_WIDTH 256
#define VDP_HEIGHT 192

typedef struct Vdp {
    uint8_t vram[0x4000];
    uint8_t cram[32];
    uint8_t reg[16];
    uint16_t addr;       /* 14-bit access address */
    uint8_t code;        /* 0 = VRAM read, 1 = VRAM write, 2 = register, 3 = CRAM */
    bool latch;          /* first control byte received */
    uint8_t latch_byte;
    uint8_t read_buf;
    uint8_t status;      /* bit 7 frame interrupt, bit 6 sprite overflow, bit 5 collision */
} Vdp;

void vdp_reset(Vdp *v);
void vdp_write_control(Vdp *v, uint8_t value);
void vdp_write_data(Vdp *v, uint8_t value);
uint8_t vdp_read_data(Vdp *v);
uint8_t vdp_read_status(Vdp *v);

/* Renders the current VRAM/CRAM/register state into a 256x192 XRGB8888 buffer. */
void vdp_render(const Vdp *v, uint32_t *pixels);

/* Converts a CRAM entry (--BBGGRR) to XRGB8888. */
uint32_t vdp_color(uint8_t cram_value);

#endif
