/*
 * Objects drawn in the background layer ($497D-$4C26): sinking blocks,
 * collapsing floors, descending bands, and the name table change handlers
 * they request (copy / erase / draw a block of name table entries).
 *
 * These entities have no sprite. They point into the RAM mirror of the name
 * table ($C800-$CEFF, see NAMETABLE_MIRROR) and ask the main loop to update it
 * through v_nametableChangeRequest, one change per frame:
 *   v_nametableChangeDestination   mirror address of the top-left entry
 *   nametableChangeSourceMetatile  ROM source of a copy (bank 7)
 *   _RAM_C208_ / _RAM_C209_        rows / bytes per row (2 bytes per tile)
 * They wait while the screen scrolls (the mirror is being rewritten then).
 */
#include "game/enemies1/enemies1.h"
#include "game/vdp_io.h"

#define NT_CHANGE_ROWS _RAM_C208_
#define NT_CHANGE_ROW_BYTES _RAM_C209_

#define BLOCK_GRAPHICS_BANK 0x87 /* mapper value: bank 7 in slot 2 */
#define BAND_EDGE_TILE 0x43
#define BAND_BODY_TILE 0x44
#define BAND_ATTRIBUTES 0x70     /* in front of sprites */

/* $4C1C: next name table row (+64 bytes), wrapping from the bottom of the
 * mirror ($CF00) back to its top ($C800). */
static uint16_t next_nametable_row(uint16_t entry) {
    entry = (uint16_t)(entry + NAMETABLE_ROW);
    if ((entry >> 8) >= NAMETABLE_MIRROR_END_PAGE) entry = (uint16_t)((NAMETABLE_MIRROR & 0xFF00) | (entry & 0x00FF));
    return entry;
}

LIFTED(_LABEL_4C23_, 0x4C1C) {
    cpu.hl = next_nametable_row(cpu.hl);
    LIFTED_RETURN();
}

/* Mirror address of the name table entry under pixel (x, y) of the screen. */
static uint16_t nametable_entry_at(uint8_t x, uint8_t y) {
    cpu.de = ENT_WORD(x, y);
    call_routine(f__LABEL_7C89_);
    return cpu.hl;
}

/* ------------------------------------------------ name table change handlers */

/* $4B97 (request $85): copies a block of name table entries from ROM (bank 7)
 * to the mirror and to VRAM, row by row. */
LIFTED(_LABEL_4B9E_, 0x4B97) {
    wr8(0xFFFF, BLOCK_GRAPHICS_BANK);
    uint16_t source = ram16(nametableChangeSourceMetatile);
    uint16_t dest = ram16(v_nametableChangeDestination);
    uint8_t rows = ram8(NT_CHANGE_ROWS), row_bytes = ram8(NT_CHANGE_ROW_BYTES);
    do {
        uint32_t mirror_bytes = row_bytes ? row_bytes : 0x10000; /* LDIR with BC = 0 */
        for (uint32_t i = 0; i < mirror_bytes; i++)
            wr8((uint16_t)(dest + i), rd8((uint16_t)(source + i)));
        uint16_t vdp_word = (uint16_t)(dest - MIRROR_TO_VDP_WRITE);
        uint32_t vram_bytes = row_bytes ? row_bytes : 256;
        vdp_set_address(vdp_word);
        vdp_write_bytes(source, vram_bytes);
        source = (uint16_t)(source + vram_bytes);
        /* QUIRK: the row wrap test is applied to the VDP address, where it
         * never triggers: a block crossing the bottom of the name table is
         * written past the end of the mirror. */
        dest = (uint16_t)(next_nametable_row(vdp_word) + MIRROR_TO_VDP_WRITE);
    } while (--rows);
    LIFTED_RETURN();
}

/* $4BC6 (request $87): clears a block of name table entries (mirror and VRAM). */
LIFTED(_LABEL_4BCD_, 0x4BC6) {
    uint16_t row = ram16(v_nametableChangeDestination);
    uint8_t rows = ram8(NT_CHANGE_ROWS), row_bytes = ram8(NT_CHANGE_ROW_BYTES);
    do {
        uint32_t count = row_bytes ? row_bytes : 256;
        for (uint32_t i = 0; i < count; i++) wr8((uint16_t)(row + i), 0);
        vdp_set_address((uint16_t)(row - MIRROR_TO_VDP_WRITE));
        vdp_fill(0, count);
        row = next_nametable_row(row);
    } while (--rows);
    LIFTED_RETURN();
}

/* Draws one row of a band: every entry of the `width` tiles starting at `row`
 * that is empty (tile 0) or already band body becomes `tile` in front of the
 * sprites; the row is then sent to VRAM. Returns the next row. */
static uint16_t draw_band_row(uint16_t row, uint8_t width, uint8_t tile) {
    uint8_t row_low = (uint8_t)row;
    uint16_t vdp_word = (uint16_t)(row - MIRROR_TO_VDP_WRITE);
    uint16_t p = row;
    uint8_t n = width;
    do {
        uint8_t current = rd8(p);
        if (current == 0 || current == BAND_BODY_TILE) {
            wr8(p, tile);
            wr8((uint16_t)(p + 1), BAND_ATTRIBUTES);
        }
        p = (uint16_t)(p + 2);
    } while (--n);
    /* QUIRK: only the low byte of the row address is restored, so a row that
     * ends on a 256-byte boundary continues from the wrong page. */
    p = (uint16_t)((p & 0xFF00) | row_low);
    uint32_t count = (uint8_t)(width * 2) ? (uint8_t)(width * 2) : 256;
    vdp_set_address(vdp_word);
    vdp_write_bytes(p, count);
    p = (uint16_t)(((p + count) & 0xFF00) | row_low);
    return next_nametable_row(p);
}

/* $4BFE: HL = row, B = width in tiles, C = tile. Returns HL = next row. */
LIFTED(sub_4BFE, 0x4BFE) {
    cpu.hl = draw_band_row(cpu.hl, cpu.b, cpu.c);
    LIFTED_RETURN();
}

/* $4BEC (request $86): draws the two rows of a band: edge tile $43 over body
 * tile $44, _RAM_C209_ tiles wide. */
LIFTED(_LABEL_4BF3_, 0x4BEC) {
    uint16_t row = ram16(v_nametableChangeDestination);
    row = draw_band_row(row, ram8(NT_CHANGE_ROW_BYTES), BAND_EDGE_TILE);
    draw_band_row(row, ram8(NT_CHANGE_ROW_BYTES), BAND_BODY_TILE);
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ sinking blocks */

/* Sinking block fields:
 *   state/stateTimer   ROM address (bank 7) of the block's name table entries
 *   unknown10          rows, unknown11: bytes per row
 *   unknown8/unknown9  mirror address of the block's top-left entry
 *   unknown7           frames per step, data: countdown to the next step */

/* Common setup of types $10-$13: remembers the block and where it is, then
 * becomes type $14. */
static void start_sinking_block(Entity *e, uint16_t source, uint8_t rows, uint8_t row_bytes) {
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (ram8(v_scrollFlags) & SCROLL_ANY) return;
    e->state = (uint8_t)source;
    e->stateTimer = (uint8_t)(source >> 8);
    e->unknown11 = row_bytes;
    e->unknown10 = rows;
    uint16_t entry = nametable_entry_at(ENT_X(e), ENT_Y(e));
    e->unknown8 = (uint8_t)(entry & 0xFC);
    e->unknown9 = (uint8_t)(entry >> 8);
    e->type = ENTITY_SINKING_BLOCK;
}

/* $49F5: HL = source block, B = bytes per row, C = rows. */
LIFTED(_LABEL_49FC_, 0x49F5) {
    start_sinking_block(entity_at(cpu.ix), cpu.hl, cpu.c, cpu.b);
    LIFTED_RETURN();
}

/* $49EB/$4A26/$4A32/$4A3E: the four sinking block variants (graphics, size
 * and speed). */
LIFTED(updateEntity0x10, 0x49EB) {
    Entity *e = entity_at(cpu.ix);
    e->unknown7 = 0x0F;
    start_sinking_block(e, 0xBE9E, 3, 2 * 2); /* 2 tiles x 3 rows, 15 frames/row */
    LIFTED_RETURN();
}

LIFTED(updateEntity0x11, 0x4A26) {
    Entity *e = entity_at(cpu.ix);
    e->unknown7 = 0x19;
    start_sinking_block(e, 0xBEAA, 9, 4 * 2); /* 4 tiles x 9 rows, 25 frames/row */
    LIFTED_RETURN();
}

LIFTED(updateEntity0x12, 0x4A32) {
    Entity *e = entity_at(cpu.ix);
    e->unknown7 = 0x0A;
    start_sinking_block(e, 0xBEF2, 0x0B, 2 * 2); /* 2 tiles x 11 rows, 10 frames/row */
    LIFTED_RETURN();
}

LIFTED(updateEntity0x13, 0x4A3E) {
    Entity *e = entity_at(cpu.ix);
    e->unknown7 = 0x1E;
    start_sinking_block(e, 0xBF26, 3, 16 * 2); /* 16 tiles x 3 rows, 30 frames/row */
    LIFTED_RETURN();
}

/* $497D: sinking block. Every unknown7 frames it moves one tile row down
 * (the source block is redrawn one row lower, its first row usually erasing
 * the old top) until the row below it is not empty. */
LIFTED(updateEntity0x14, 0x497D) {
    Entity *e = entity_at(cpu.ix);
    e->flags |= EF_DESTROY_OFFSCREEN;
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    if (--e->data != 0) LIFTED_RETURN();
    if (ram8(v_nametableChangeRequest) != 0) {
        e->data++; /* the previous change is still pending: retry next frame */
        LIFTED_RETURN();
    }
    e->data = e->unknown7;
    ram8(v_soundControl) = SOUND_BLOCK_SINKS;

    uint16_t block = ENT_WORD(e->unknown8, e->unknown9);
    uint8_t rows = e->unknown10, row_bytes = e->unknown11;
    /* Tile indexes of the row just below the block (no wrap: QUIRK). */
    uint16_t p = (uint16_t)(rows * NAMETABLE_ROW + block);
    uint8_t tiles = (uint8_t)((row_bytes >> 1) | (row_bytes << 7)); /* RRCA */
    uint8_t n = (uint8_t)(tiles - 1);
    uint8_t below = rd8(p);
    do {
        p = (uint16_t)(p + 2);
        below |= rd8(p);
    } while (--n);
    if (below != 0) DESTROY_AND_RETURN();

    block = next_nametable_row(block);
    e->unknown8 = (uint8_t)block;
    e->unknown9 = (uint8_t)(block >> 8);
    ram16(v_nametableChangeDestination) = block;
    ram16(nametableChangeSourceMetatile) = ENT_WORD(e->state, e->stateTimer);
    ram16(NT_CHANGE_ROWS) = ENT_WORD(rows, row_bytes);
    ram8(v_nametableChangeRequest) = NT_CHANGE_COPY_BLOCK;
    LIFTED_RETURN();
}

/* --------------------------------------------------------- collapsing floors */

/* Collapsing floor fields:
 *   data               number of steps (from the level data)
 *   unknown8/unknown9  mirror address of the hole's left end
 *   unknown11          hole width in bytes, unknown6: steps left
 *   unknown7           frames per step, animationTimer: countdown */

/* $4AD0: common setup once the position is known. */
LIFTED(_LABEL_4AD7_, 0x4AD0) {
    Entity *e = entity_at(cpu.ix);
    e->flags |= EF_INITIALIZED;
    e->unknown7 = 3;
    e->unknown6 = e->data;
    e->unknown11 = 0;
    e->unknown10 = 2;
    LIFTED_RETURN();
}

/* $4A72: starts the collapse. */
LIFTED(_LABEL_4A79_, 0x4A72) {
    Entity *e = entity_at(cpu.ix);
    ram8(v_soundControl) = SOUND_FLOOR_COLLAPSES;
    e->flags |= EF_TRIGGERED;
    e->animationTimer = 1;
    TAIL_CALL(f__LABEL_4A86_);
}

/* $4A7F: one collapse step every 3 frames: erases 2 rows of the floor, the
 * hole growing by one tile on each side, until the steps run out. */
LIFTED(_LABEL_4A86_, 0x4A7F) {
    Entity *e = entity_at(cpu.ix);
    if (--e->animationTimer != 0) LIFTED_RETURN();
    if (ram8(v_nametableChangeRequest) != 0) {
        e->animationTimer++;
        LIFTED_RETURN();
    }
    e->animationTimer = e->unknown7;
    e->unknown11 = (uint8_t)(e->unknown11 + 4);
    ram16(NT_CHANGE_ROWS) = ENT_WORD(2, e->unknown11);
    ram16(v_nametableChangeDestination) = ENT_WORD(e->unknown8, e->unknown9);
    /* QUIRK: 8-bit decrement, the left end cannot leave its 256-byte page. */
    e->unknown8 = (uint8_t)(e->unknown8 - 2);
    ram8(v_nametableChangeRequest) = NT_CHANGE_ERASE_BLOCK;
    if (--e->unknown6 == 0) DESTROY_AND_RETURN();
    LIFTED_RETURN();
}

/* $4A4A: floor that collapses when Alex walks up to 16 pixels to the right
 * of it. The hole starts 16 pixels below the entity. */
LIFTED(updateEntity0x16, 0x4A4A) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    if (!(e->flags & EF_INITIALIZED)) {
        uint16_t entry = nametable_entry_at(ENT_X(e), (uint8_t)(ENT_Y(e) + 0x10));
        e->unknown8 = (uint8_t)(entry & 0xFE);
        e->unknown9 = (uint8_t)(entry >> 8);
        TAIL_CALL(f__LABEL_4AD7_);
    }
    e->flags |= EF_DESTROY_OFFSCREEN;
    if (e->flags & EF_TRIGGERED) TAIL_CALL(f__LABEL_4A86_);
    uint8_t alex_x = ENT_X(alex());
    if (alex_x < ENT_X(e) || alex_x - ENT_X(e) >= 0x10) LIFTED_RETURN();
    TAIL_CALL(f__LABEL_4A79_);
}

/* $4AE7: floor that collapses when Alex's attack touches the entity. The hole
 * is at a fixed screen position (X $74, Y $A0). */
LIFTED(updateEntity0x17, 0x4AE7) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    if (!(e->flags & EF_INITIALIZED)) {
        uint16_t entry = (uint16_t)(nametable_entry_at(0x74, 0xA0) - 1);
        e->unknown8 = (uint8_t)entry;
        e->unknown9 = (uint8_t)(entry >> 8);
        TAIL_CALL(f__LABEL_4AD7_);
    }
    e->flags |= EF_DESTROY_OFFSCREEN;
    if (e->flags & EF_TRIGGERED) TAIL_CALL(f__LABEL_4A86_);
    if (!alex_attack_hits()) LIFTED_RETURN();
    TAIL_CALL(f__LABEL_4A79_);
}

/* ------------------------------------------------------------ descending band */

#define BAND_ROWS 0x12
#define BAND_STEP_FRAMES 0x0F

/* $4B1C: waterfall-like curtain of checkered tiles ($43 edge over $44 body,
 * `data` tiles wide) pouring down one row every 15 frames for 18 rows, only
 * over empty entries (it runs behind solid blocks).
 * unknown8/unknown9: mirror address of the current row, unknown6: rows left. */
LIFTED(updateEntity0x15, 0x4B1C) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->unknown7 = 1;
        e->unknown6 = BAND_ROWS;
        uint16_t entry = nametable_entry_at(ENT_X(e), ENT_Y(e));
        e->unknown8 = (uint8_t)(entry & 0xFE);
        e->unknown9 = (uint8_t)(entry >> 8);
        ram8(v_soundControl) = SOUND_BAND_STARTS;
        LIFTED_RETURN();
    }
    if (entity_on_screen(e)) {
        if (--e->animationTimer != 0) LIFTED_RETURN();
        if (ram8(v_nametableChangeRequest) != 0) {
            e->animationTimer++;
            LIFTED_RETURN();
        }
        e->animationTimer = BAND_STEP_FRAMES;
        ram8(v_nametableChangeRequest) = NT_CHANGE_DRAW_BAND;
        ram8(NT_CHANGE_ROW_BYTES) = e->data;
        uint16_t row = ENT_WORD(e->unknown8, e->unknown9);
        ram16(v_nametableChangeDestination) = row;
        if (--e->unknown6 != 0) {
            row = next_nametable_row(row);
            e->unknown8 = (uint8_t)row;
            e->unknown9 = (uint8_t)(row >> 8);
            LIFTED_RETURN();
        }
    }
    call_routine(f_handler_LABEL_99D3_);
    DESTROY_AND_RETURN();
}
