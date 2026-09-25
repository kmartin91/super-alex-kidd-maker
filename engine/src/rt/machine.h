/* Master System memory map, Sega mapper and I/O ports. */
#ifndef RT_MACHINE_H
#define RT_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "psg.h"
#include "vdp.h"

/* Active-low joypad bits on port $DC. */
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_BTN1  0x10
#define JOY_BTN2  0x20

typedef struct Machine {
    const uint8_t *rom;
    uint32_t rom_size;
    uint8_t bank_mask;
    uint8_t slot[3];     /* ROM bank mapped in each 16 KB slot */
    uint8_t ram[0x2000];
    Vdp vdp;
    Psg psg;
    uint8_t joy;         /* pressed JOY_* bits (active high here) */
    bool reset_button;   /* held */
} Machine;

void machine_init(Machine *m, const uint8_t *rom, uint32_t rom_size);

static inline uint8_t machine_read(const Machine *m, uint16_t a) {
    if (a >= 0xC000) return m->ram[a & 0x1FFF];
    if (a < 0x0400) return m->rom[a];
    uint32_t bank = m->slot[a >> 14];
    return m->rom[(bank << 14) | (a & 0x3FFF)];
}

void machine_write(Machine *m, uint16_t a, uint8_t v);
uint8_t machine_in(Machine *m, uint8_t port);
void machine_out(Machine *m, uint8_t port, uint8_t v);

#endif
