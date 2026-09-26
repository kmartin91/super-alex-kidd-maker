#include "machine.h"
#include "maker.h"

#include <string.h>

void machine_init(Machine *m, const uint8_t *rom, uint32_t rom_size) {
    memset(m, 0, sizeof(*m));
    m->rom = rom;
    m->rom_size = rom_size;
    m->bank_mask = (uint8_t)((rom_size >> 14) - 1);
    m->slot[0] = 0;
    m->slot[1] = 1;
    m->slot[2] = 2;
    vdp_reset(&m->vdp);
    psg_reset(&m->psg);
    maker_detect(rom, rom_size);
}

void machine_write(Machine *m, uint16_t a, uint8_t v) {
    if (a < 0xC000) return; /* ROM */
    m->ram[maker_ram_offset(a)] = v;
    if (a >= 0xFFFD) m->slot[a - 0xFFFD] = v & m->bank_mask;
}

uint8_t machine_in(Machine *m, uint8_t port) {
    switch (port & 0xC1) {
    case 0x40: return 0; /* V counter: unused by the game */
    case 0x41: return 0; /* H counter */
    case 0x80: return vdp_read_data(&m->vdp);
    case 0x81: return vdp_read_status(&m->vdp);
    case 0xC0: return (uint8_t)~m->joy;             /* $DC: player 1 */
    case 0xC1: return m->reset_button ? 0xEF : 0xFF; /* $DD: reset is bit 4 */
    default: return 0xFF;
    }
}

void machine_out(Machine *m, uint8_t port, uint8_t v) {
    switch (port & 0xC1) {
    case 0x40:
    case 0x41: psg_write(&m->psg, v); break;
    case 0x80: vdp_write_data(&m->vdp, v); break;
    case 0x81: vdp_write_control(&m->vdp, v); break;
    default: break; /* memory/IO control and $DE/$DF keyboard PPI: ignored */
    }
}
