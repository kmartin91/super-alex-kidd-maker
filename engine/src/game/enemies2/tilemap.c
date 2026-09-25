/*
 * Terrain probes ($7C44-$7C7F): look up the name-table entry displayed at a
 * screen position, in the RAM copy of the name table (v_nametable, $C800,
 * 32 columns x 28 rows of 2-byte entries). The attribute byte (second byte)
 * carries the tile flags: bit 7 = solid, bit 5 = shop door.
 *
 * Results (register ABI): A = attribute byte, HL = its address, B = name
 * table y (pixels, wrapped to 0-$DF), C = screen x, flags S/Z/P from the
 * row (y & $F8).
 */
#include "game/enemies2/enemies2.h"

#define NAMETABLE_HEIGHT_PX 0xE0

/* Column part of a screen x coordinate: 2 bytes per name-table entry. */
static uint8_t column_offset(uint8_t screen_x) { return (uint8_t)((screen_x >> 2) & 0x3E); }

/* E = column offset and C = screen x, from a level x (pixels). */
static void set_column(uint8_t x) {
    uint8_t screen_x = (uint8_t)(x - (uint8_t)(ram16(v_horizontalScroll) >> 8));
    cpu.c = screen_x;
    cpu.e = column_offset(screen_x);
}

/* $7C44: attribute of the tile at (entity x + E, entity y + D). */
LIFTED(getNearEntityTileAttrWithOffset, 0x7C44) {
    const Entity *e = entity_at(cpu.ix);
    cpu.a = (uint8_t)(x_pixel(e) + cpu.e);
    TAIL_CALL(f__LABEL_7C4F_);
}

/* $7C48: attribute of the tile at (A, entity y + D). */
LIFTED(_LABEL_7C4F_, 0x7C48) {
    const Entity *e = entity_at(cpu.ix);
    set_column(cpu.a);
    cpu.a = (uint8_t)(y_pixel(e) + cpu.d);
    TAIL_CALL(f_sub_7C56);
}

/* $7C56: attribute of the tile at screen y A in the column E. */
LIFTED(sub_7C56, 0x7C56) {
    uint8_t scroll = (uint8_t)(ram16(v_verticalScroll) >> 8);
    unsigned sum = (unsigned)cpu.a + scroll;
    uint8_t y = (uint8_t)sum;
    /* Wrap to the 224-pixel name table. */
    if (sum > 0xFF) y = (uint8_t)(y + 0x20);
    if (y >= NAMETABLE_HEIGHT_PX) y = (uint8_t)(y + 0x20);
    uint8_t row = y & 0xF8;
    cpu.b = y;
    cpu.d = (uint8_t)(v_nametable >> 8);
    cpu.hl = (uint16_t)(v_nametable + row * 8 + cpu.e + 1); /* 64 bytes per row */
    cpu.a = rd8(cpu.hl);
    cpu.f = (uint8_t)(flag_szxyp(row) | FLAG_H);
    LIFTED_RETURN();
}

/* $7C73: attribute of the tile at level position (E, D). */
LIFTED(_LABEL_7C7A_, 0x7C73) {
    uint8_t y = cpu.d;
    set_column(cpu.e);
    cpu.a = y;
    TAIL_CALL(f_sub_7C56);
}
