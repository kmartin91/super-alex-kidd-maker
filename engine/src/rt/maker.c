#include "rt/maker.h"

#include <string.h>

Maker maker;

void maker_detect(const uint8_t *rom, uint32_t rom_size) {
    memset(&maker, 0, sizeof(maker));
    if (rom_size < MAKER_BLOCK_OFFSET + 8 + 256 || memcmp(rom + MAKER_BLOCK_OFFSET, "AKMAKER1", 8) != 0) return;
    maker.active = true;
    memcpy(maker.home, rom + MAKER_BLOCK_OFFSET + 8, 256);
    const uint8_t *janken = rom + MAKER_BLOCK_OFFSET + 8 + 256;
    if (rom_size >= MAKER_BLOCK_OFFSET + 8 + 256 + 8 + sizeof(maker.janken) && memcmp(janken, "AKJANKEN", 8) == 0) {
        memcpy(maker.janken, janken + 8, sizeof(maker.janken));
        for (int i = 0; i < 4; i++) {
            if (maker.janken[i][0] > MAKER_JANKEN_MOVES) maker.janken[i][0] = MAKER_JANKEN_MOVES;
            for (int m = 1; m <= MAKER_JANKEN_MOVES; m++) maker.janken[i][m] %= 3;
        }
        const uint8_t *zone = janken + 8 + sizeof(maker.janken);
        if (rom_size >= (uint32_t)(zone - rom) + 8 + 5 && memcmp(zone, "AKZONE01", 8) == 0) {
            maker.zone.defined = true;
            maker.zone.row = zone[8];
            maker.zone.width = zone[9];
            maker.zone.entity_base = zone[10];
            maker.zone.alex_x = zone[11];
            maker.zone.alex_y = zone[12];
        }
    }

    const uint8_t *vehicle = rom + MAKER_BLOCK_OFFSET + MAKER_VEHICLE_OFFSET;
    if (rom_size >= MAKER_BLOCK_OFFSET + MAKER_VEHICLE_OFFSET + 8 + 1 && memcmp(vehicle, "AKVEHIC1", 8) == 0) {
        maker.crash_ends_try = vehicle[8] != 0;
    }
}

int maker_janken_next(uint8_t opponent_data) {
    if (!maker.active) return -1;
    const uint8_t *moves = maker.janken[(opponent_data >> 1) & 3];
    if (moves[0] == 0) return -1;
    return moves[1 + maker.janken_throws % moves[0]];
}
