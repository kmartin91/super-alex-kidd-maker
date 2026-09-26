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
    }
}

int maker_janken_next(uint8_t opponent_data) {
    if (!maker.active) return -1;
    const uint8_t *moves = maker.janken[(opponent_data >> 1) & 3];
    if (moves[0] == 0) return -1;
    return moves[1 + maker.janken_throws % moves[0]];
}
