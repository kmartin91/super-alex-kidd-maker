/* VDP access helpers used by the lifted game code. */
#ifndef GAME_VDP_IO_H
#define GAME_VDP_IO_H

#include <stdint.h>

#define VDP_DATA 0xBE
#define VDP_CONTROL 0xBF

/* VDP control words: address | command << 14. */
#define VDP_VRAM_READ(addr) ((uint16_t)(addr))
#define VDP_VRAM_WRITE(addr) ((uint16_t)(0x4000 | (addr)))
#define VDP_REGISTER(n, value) ((uint16_t)(0x8000 | ((n) << 8) | (value)))
#define VDP_CRAM_WRITE(index) ((uint16_t)(0xC000 | (index)))

/* Name table rows are 32 entries of 2 bytes. */
#define NAMETABLE_ROW_BYTES 0x40

void vdp_set_address(uint16_t control_word);
void vdp_write(uint8_t value);
uint8_t vdp_read(void);
/* Copies `count` bytes (0 means 65536) from Z80 memory to the VDP data port. */
void vdp_write_bytes(uint16_t src, uint32_t count);
/* Writes `count` copies of `value` (0 means 65536). */
void vdp_fill(uint8_t value, uint32_t count);
/* 16-bit counts as the original routines take them in BC: 0 means 65536. */
static inline uint32_t bc_count(uint16_t bc) { return bc ? bc : 0x10000u; }

#endif
