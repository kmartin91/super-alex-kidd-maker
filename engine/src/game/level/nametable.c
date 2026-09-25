/*
 * Name-table pipeline: the column/row buffers built by the scroll engine are
 * copied to the RAM mirror of the name table by updateNametable (main loop,
 * right after updateScroll) and to the VDP by draw (VBlank). Collision reads
 * the mirror, so both must agree.
 *
 * Where a buffer goes:
 *   column: name-table column derived from the fine horizontal scroll (the
 *     column just scrolled in: right edge when moving right, left edge when
 *     moving left), 24 rows starting at v_topRowVdpAddress, wrapping at row 28;
 *   row: name-table row derived from the fine vertical scroll (just below the
 *     screen when moving down, the top row when moving up), starting at column
 *     v_nametableColumn and wrapping at column 32.
 *
 * Several copies keep a loop counter or a saved value in the alternate AF
 * (ex af,af'); the value it ends with is observable by the callers, so it is
 * reproduced (noted "AF'").
 */
#include "level.h"

#define GRID_ROWS 24                    /* 8-px rows of one screen */
#define COLUMN_OFFSET_MASK 0x3E
#define ROW_START_ADDRESS_MASK 0xFFC0   /* name-table address -> start of its row */
#define VDP_REG_HSCROLL 8
#define VDP_REG_VSCROLL 9

/* copyBytesToVRAM ($0145) with its register effects (A = 0, BC = $00BE,
 * HL = past the data; DE kept). */
static void vram_copy(uint16_t vdp_address, uint16_t src, uint16_t count) {
    cpu.de = vdp_address;
    cpu.hl = src;
    cpu.bc = count;
    CALL_ROUTINE(f_copyBytesToVRAM);
}

/* Byte offset, in a name-table row, of the column the scroll engine just
 * built: the fine scroll gives the column at the right edge when moving right;
 * moving left, 8 pixels back, the column at the (masked) left edge. */
static uint8_t incoming_column_offset(void) {
    uint8_t pixel = ram8(v_horizontalScrollPixel);
    if (!(ram8(v_horizontalScrollSpeedHigh) & 0x80)) pixel = (uint8_t)(pixel - 8);
    return (uint8_t)(((uint8_t)~pixel >> 2) & COLUMN_OFFSET_MASK);
}

/* Pixel line (multiple of 8) of the name-table row the scroll engine just
 * built: moving up, the top row; otherwise the row 24 rows below the top
 * (just under the screen), wrapping at 28 rows. */
static uint8_t incoming_row_line(void) {
    uint8_t line = ram8(v_verticalScrollLine) & 0xF8;
    if (ram8(v_verticalScrollSpeedHigh) & 0x80) return line;
    return line >= 0x20 ? (uint8_t)(line - 0x20) : (uint8_t)(line - 0x40);
}

/* ------------------------------------------------------------- mirror */

static void copy_column_to_mirror(void) {
    uint16_t dst = (uint16_t)(NAMETABLE_MIRROR +
                              (uint16_t)((ram16(v_topRowVdpAddress) & 0x07FF) + incoming_column_offset()));
    uint16_t src = v_columnToDraw;
    uint8_t high = 0;
    for (int row = 0; row < GRID_ROWS; row++) {
        wr8(dst, rd8(src));
        wr8((uint16_t)(dst + 1), rd8((uint16_t)(src + 1)));
        src = (uint16_t)(src + 2);
        dst = (uint16_t)(dst + NAMETABLE_ROW_BYTES);
        high = (uint8_t)(dst >> 8);
        if (high == NAMETABLE_MIRROR_END_HIGH) dst = (uint16_t)((NAMETABLE_MIRROR & 0xFF00) | (dst & 0xFF));
    }
    cpu.af_ = z80_af_after_cp(high, NAMETABLE_MIRROR_END_HIGH); /* AF' */
    cpu.bc = NAMETABLE_ROW_BYTES;
    cpu.de = dst;
}

static void copy_row_to_mirror(void) {
    uint8_t column = ram8(v_nametableColumn);
    uint16_t dst = (uint16_t)(NAMETABLE_MIRROR + (uint16_t)(column + incoming_row_line() * 8));
    /* The row buffer starts with screen column 0, which lives at name-table
     * column `column`: copy up to the end of the row, then wrap to its start.
     * Done with LDIR on the Z80 registers: the original parks `column` in AF'
     * meanwhile, so AF' ends with A = bytes of the first part and the flags
     * of the first LDIR. */
    cpu.a = NAMETABLE_ROW_BYTES;
    alu_sub(column);
    cpu.hl = v_rowToDraw;
    cpu.de = dst;
    cpu.bc = cpu.a;
    op_ldir();
    cpu.af_ = cpu.af;
    if (column != 0) {
        cpu.de = (uint16_t)(cpu.de - NAMETABLE_ROW_BYTES);
        cpu.bc = column;
        op_ldir();
    }
}

/* $6B42 updateNametable: maps bank 2 (entity data for the loaders that run
 * next), then copies the buffers flagged in v_nametableBuffersReady into the
 * RAM name-table mirror. The flags stay set for draw.
 * out: BC, DE as the copies leave them (untouched when nothing is ready). */
void level_update_nametable_mirror(void) {
    map_bank(BANK(2));
    if (ram8(v_nametableBuffersReady) & COLUMN_READY) copy_column_to_mirror();
    if (ram8(v_nametableBuffersReady) & ROW_READY) copy_row_to_mirror();
}

LIFTED(updateNametable_LABEL_6B49_, 0x6B42) {
    level_update_nametable_mirror();
    LIFTED_RETURN();
}

/* ---------------------------------------------------------------- VDP */

static void draw_column(void) {
    uint16_t address = (uint16_t)(ram16(v_topRowVdpAddress) + incoming_column_offset());
    uint16_t src = v_columnToDraw;
    uint8_t high = 0;
    for (int row = 0; row < GRID_ROWS; row++) {
        vdp_set_address(address);
        vdp_write(rd8(src));
        vdp_write(rd8((uint16_t)(src + 1)));
        src = (uint16_t)(src + 2);
        address = (uint16_t)(address + NAMETABLE_ROW_BYTES);
        high = (uint8_t)(address >> 8);
        if (high == NAMETABLE_VDP_END_HIGH)
            address = (uint16_t)((NAMETABLE_VDP_WRITE & 0xFF00) | (address & 0xFF));
    }
    cpu.af_ = z80_af_after_cp(high, NAMETABLE_VDP_END_HIGH); /* AF' */
}

static void draw_row(void) {
    /* v_rowVdpAddress: $78 << 8 | v_nametableColumn = column in row 0. */
    uint16_t address = (uint16_t)(ram16(v_rowVdpAddress) + incoming_row_line() * 8);
    uint8_t column = ram8(v_nametableColumn);
    vram_copy(address, v_rowToDraw, (uint8_t)(NAMETABLE_ROW_BYTES - column));
    cpu.af_ = cpu.af; /* AF': the original parks `column` there across the copy */
    if (column != 0) vram_copy((uint16_t)(address & ROW_START_ADDRESS_MASK), cpu.hl, column);
}

/* $69AE updateVdpAddressAfterDraw: clears the buffer flags and writes the
 * scroll registers (R8 = v_horizontalScrollPixel, R9 = v_verticalScrollLine).
 * Also used by the map, the shop and the sub-area state to restore them. */
void level_write_scroll_registers(void) {
    ram8(v_nametableBuffersReady) = 0;
    vdp_set_address(VDP_REGISTER(VDP_REG_HSCROLL, ram8(v_horizontalScrollPixel)));
    vdp_set_address(VDP_REGISTER(VDP_REG_VSCROLL, ram8(v_verticalScrollLine)));
}

LIFTED(updateVdpAddressAfterDraw, 0x69AE) {
    level_write_scroll_registers();
    LIFTED_RETURN();
}

/* $6919 draw: VBlank part of the scroll: writes the ready column and/or row to
 * the VDP name table, then the scroll registers. */
void level_draw(void) {
    if (ram8(v_nametableBuffersReady) & COLUMN_READY) draw_column();
    if (ram8(v_nametableBuffersReady) & ROW_READY) draw_row();
    level_write_scroll_registers();
}

LIFTED(draw, 0x6919) {
    level_draw();
    LIFTED_RETURN();
}

/* ------------------------------------------------ shop name table (RLE) */

/* $0E4B: decodes one byte plane of a name table (tile bytes or attribute
 * bytes), written to every other byte from `dst`:
 *   0 = end;  $01-$7F n = next byte repeated n times;
 *   $80-$FF = the next (n & $7F) bytes copied (QUIRK: $80 copies 256).
 * Returns the address of the terminating 0. */
static uint16_t decode_nametable_plane(uint16_t src, uint16_t dst) {
    for (;;) {
        uint8_t token = rd8(src);
        if (token == 0) return src;
        if (!(token & 0x80)) {
            uint8_t value = rd8((uint16_t)(src + 1));
            for (uint8_t n = token; n != 0; n--, dst = (uint16_t)(dst + 2)) wr8(dst, value);
            src = (uint16_t)(src + 2);
        } else {
            int n = (token & 0x7F) ? (token & 0x7F) : 256;
            for (; n > 0; n--, dst = (uint16_t)(dst + 2)) wr8(dst, rd8(++src));
            src++;
        }
    }
}

LIFTED(decompressNametable_bitplane, 0x0E4B) {
    cpu.hl = decode_nametable_plane(cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* $0E41 decompressNametable: HL = stream, DE = RAM name table (shop screen):
 * the tile-byte plane, then the attribute-byte plane. */
LIFTED(decompressNametable, 0x0E41) {
    uint16_t end = decode_nametable_plane(cpu.hl, cpu.de);
    decode_nametable_plane((uint16_t)(end + 1), (uint16_t)(cpu.de + 1));
    LIFTED_RETURN();
}
