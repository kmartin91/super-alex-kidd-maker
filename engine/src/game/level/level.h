/*
 * Level engine (module "level"): everything that turns the level data of
 * docs/level-format.md into a scrolling picture and a collision map.
 *
 * Data flow
 * ---------
 *   level descriptor ($66CF table, bank 1)            loadLevel
 *        |  bank, rows/columns tables, start screen, size, scroll flags,
 *        v  metatile table
 *   rows table[v] -> row[h] -> screen   (horizontal moves)   level_fetch_screen_from_rows
 *   cols table[h] -> col[v] -> screen   (vertical moves)     level_fetch_screen_from_columns
 *        |  RLE stream (bank = v_levelBankNumber)
 *        v
 *   decoded screen $D700: 16 x 12 metatile ids              level_decode_screen
 *        |  metatile table (bank 5): 4 name-table words per id
 *        v
 *   column buffer $CF00 (24 words) / row buffer $CF38 (32 words)
 *        |                                                   level_build_column / level_build_row
 *        +--> RAM name-table mirror $C800 (collision reads it)  updateNametable (main loop)
 *        +--> VDP name table $3800                              draw (VBlank)
 *
 * The scrolling state machine (updateScroll) is described in scroll.c, the
 * per-level "scroll flags updaters" (end of vertical drops, castle rooms) in
 * scroll_flags.c.
 *
 * All ROM reads happen at the original CPU addresses after the original
 * mapper writes: nothing here copies ROM data.
 */
#ifndef GAME_WIP_LEVEL_LEVEL_H
#define GAME_WIP_LEVEL_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"
#include "game/vdp_io.h"

/* ------------------------------------------------------------- hardware */

#define MAPPER_SLOT2 0xFFFF
#define BANK(n) (0x80 | (n))    /* the game always writes $80 | bank to $FFFF */

/* The name table sits at VRAM $3800; the RAM mirror at $C800 has the same
 * layout (28 rows of 32 words). Mirror address - $5000 = VDP write address. */
#define NAMETABLE_VDP_WRITE 0x7800
#define NAMETABLE_VDP_END_HIGH 0x7F     /* high byte one row past the last row */
#define NAMETABLE_VDP_BEFORE_HIGH 0x77  /* high byte one row before the first row */
#define NAMETABLE_VDP_LAST_ROW_HIGH 0x7E
#define NAMETABLE_MIRROR 0xC800
#define NAMETABLE_MIRROR_END_HIGH 0xCF
#define MIRROR_TO_VDP 0x5000            /* mirror address - this = VDP write address */
#define NAMETABLE_ROWS 28
#define NAMETABLE_LINES 0xE0            /* 28 rows * 8 = vertical scroll range */

/* VDP register 0 values used by the level code. */
#define VDP_R0_NORMAL 0x26              /* mode 4, left column masked */
#define VDP_R0_UNMASKED 0x06            /* mode 4, left column visible */

/* ------------------------------------------------------------ level data */

/* Screen geometry. */
#define SCREEN_METATILE_COLUMNS 16
#define SCREEN_METATILE_ROWS 12
#define METATILE_ENTRY_BYTES 8          /* TL, TR, BL, BR name-table words */

/* ROM tables (never copied: read with rd8/rd16 at these addresses). */
#define LEVEL_DESCRIPTORS_MINUS_2 0x66CD /* LevelDescriptorPointerTable - 2 (bank 1) */

/* Level descriptor layout (12 bytes, docs/level-format.md section 3). */
enum {
    DESC_BANK = 0,        /* value written to $FFFF before reading layouts */
    DESC_ROWS_TABLE = 1,  /* word: rows table (indexed by vertical screen) */
    DESC_COLS_TABLE = 3,  /* word: columns table (indexed by horizontal screen) */
    DESC_START_X = 5,     /* horizontal screen before the first draw (first shown = -1) */
    DESC_START_Y = 6,     /* vertical screen */
    DESC_WIDTH = 7,       /* last horizontal screen reachable by scrolling right */
    DESC_HEIGHT = 8,      /* last vertical screen (castles: initial entity index) */
    DESC_SCROLL_FLAGS = 9,
    DESC_METATILES = 10,  /* word: metatile pointer table in bank 5 */
};

/* v_scrollFlags / descriptor scroll flags. */
enum {
    SCROLL_DOWN = 0x01,
    SCROLL_UP = 0x02,
    SCROLL_LEFT = 0x04,
    SCROLL_RIGHT = 0x08,
    SCROLL_DROP_TO_SCREEN_0 = 0x20,   /* drop: lower row, screen 0 (level 3) */
    SCROLL_DROP_TO_QUARTER = 0x40,    /* drop: lower row, screen h/4 (levels 5, 9) */
    SCROLL_SPECIAL = 0x80,            /* vertical part / castle / drop in progress */
    SCROLL_VERTICAL_MASK = SCROLL_DOWN | SCROLL_UP,
};

/* v_currentScreenNumber bit 7: a new screen started to enter, its entities
 * must be loaded (the entity loaders clear it). */
#define NEW_SCREEN 0x80

/* v_UpdateNameTableFlags: what the scroll engine left in the buffers. */
enum {
    COLUMN_READY = 0x01,  /* v_columnToDraw ($CF00): 24 words, one 8-px column */
    ROW_READY = 0x02,     /* v_rowToDraw ($CF38): 32 words, one 8-px row */
};

/* ------------------------------------------- level state ($C0A0-$C0C9) */

/* Better names for the level engine's RAM (the generated names are kept in
 * game/ram.h; see docs/notes/level.md). The whole $C0A0-$C0C9 block is
 * cleared at level start and saved/restored as a unit (v_temporaryLevelDataCopy). */
enum {
    v_rowsTable = v_levelLayoutPointer,            /* $C0A3 */
    v_columnsTable = v_SecondLevelLayoutPointer,   /* $C0A8 */
    v_metatileTable = v_metatileNametablePointer,  /* $C087 */
    v_columnRowsLeft = v_linesToLoadToNametable,   /* $C0A1: metatiles left in the column being built */
    v_rowColumnsLeft = v_columnsToLoadToNametable, /* $C0A6: metatiles left in the row being built */
    v_nametableBuffersReady = v_UpdateNameTableFlags, /* $C0AA: COLUMN_READY | ROW_READY */

    /* Horizontal: 8.8 fixed point; the high bytes are pixels. */
    v_horizontalScrollSpeedHigh = 0xC0AC,          /* sign: bit 7 set = camera moves right */
    v_horizontalColumnPixels = 0xC0AE,             /* high byte of v_horizontalScrollAccumulator: 0-7 */
    v_horizontalScrollPixel = v_levelData_C0B0,    /* $C0B0: VDP register 8 value */

    /* Column cursor: the next 8-px column of the screen grid to build (0-31),
     * kept in three redundant forms. */
    v_columnHalf = v_levelData_C0B3_,              /* $C0B3: column & 1 (left/right half of the metatile) */
    v_columnCursor = v_levelData_C0B4_,            /* $C0B4: word, column * $80 */
    v_metatileColumn = v_levelData_C0B5_,          /* $C0B5: its high byte = column / 2 */
    v_nametableColumn = v_levelData_C0B7_,         /* $C0B7: column * 2 = byte offset in a name-table row */
    v_rowVdpAddress = v_levelData_C0B7_,           /* word $C0B7: with $C0B8 = $78, the VDP address of that
                                                      column in name-table row 0 (used to draw rows) */

    /* Vertical. */
    v_verticalScrollSpeedHigh = v_levelData_C0BA_, /* $C0BA: sign: bit 7 set = camera moves up */
    v_verticalScrollAccumulator = v_levelData_C0BB_, /* $C0BB: 8.8 */
    v_verticalRowPixels = v_levelData_C0BC_,       /* $C0BC: its high byte: 0-7 */
    v_verticalScrollLine = 0xC0BE,                 /* high byte of v_verticalScroll: VDP register 9 (0-$DF) */

    /* Row cursor: the next 8-px row of the screen grid (0-23). */
    v_rowHalf = v_levelData_C0C1_,                 /* $C0C1: row & 1 (top/bottom half of the metatile) */
    v_rowCursor = v_levelData_C0C2_,               /* $C0C2: word, row * $80 */
    v_metatileRow = v_levelData_C0C3_,             /* $C0C3: its high byte = row / 2 */
    v_topRowVdpAddress = v_levelData_C0C5_,        /* $C0C5: word, VDP address of the name-table row at the
                                                      top of the screen grid ($7800-$7EC0) */

    /* Castles (levels 11 and 16). */
    v_castleBlocksEnabled = _RAM_C08E_,            /* $C08E: 1 = keep broken blocks in $D900 */
    v_roomRecordApplied = v_specialLevelScrollFlags, /* $C077: persistent deletes applied for this move */
    v_roomRecord = targetBase_RAM_C07A_,           /* $C078: word, this room's record in $D900 */
    v_roomMoveColumnMasked = v_shouldBlankLeftmostColumn, /* $C05C */

    /* Name-table change requests (handleNametableChangeRequest). */
    v_nametableChangeSource = nametableChangeSourceMetatile, /* $C206: word, 8 bytes in bank 5 */
    v_patchListCursor = v_shopEntranceDoorNametablePointer,  /* $C06A: reused by the name-table changer */
    v_patchList = v_unknownEntityByteCount_RAM_D8A0_,        /* $D8A0: count, (dest word, metatile) x count */
    v_changerCounter = _RAM_C07F_,                 /* $C07F: punches counted by name-table changers */
};

/* Single bytes of the Entity fields (8.8 positions: the high byte is the pixel). */
enum {
    ENTITY_SIZE = 0x20,
    ENTITY_X_SUBPIXEL = 0x0B,   /* xPos low */
    ENTITY_X = 0x0C,            /* xPos high */
    ENTITY_Y = 0x0E,            /* yPos high */
    ENTITY_PAGE_X = 0x09,       /* isOffScreenFlags low: $FF right, $01 left of the camera */
    ENTITY_PAGE_Y = 0x0A,       /* isOffScreenFlags high: $01 below, $FF above */
};
#define entity_byte(slot, field) ram8((uint16_t)((slot) + (field)))

/* ------------------------------------------------------------ helpers */

static inline void map_bank(uint8_t bank) { wr8(MAPPER_SLOT2, bank); }

/* Address of the 8-byte entry of metatile `id` (bank 5 must be mapped). */
static inline uint16_t metatile_entry(uint8_t id) {
    return rd16((uint16_t)(ram16(v_metatileTable) + 2 * id));
}

/* Flags of CP A,n exactly as the Z80 computes them. Used where the original
 * leaves such a result in the alternate AF (ex af,af') for its callers. */
static inline uint16_t z80_af_after_cp(uint8_t a, uint8_t n) {
    uint16_t saved = cpu.af;
    cpu.a = a;
    alu_cp(n);
    uint16_t af = cpu.af;
    cpu.af = saved;
    return af;
}

/* A and F after AND n on `a` (same purpose). */
static inline uint16_t z80_af_after_and(uint8_t a, uint8_t n) {
    uint16_t saved = cpu.af;
    cpu.a = a;
    alu_and(n);
    uint16_t af = cpu.af;
    cpu.af = saved;
    return af;
}

/* layout.c */
void level_decode_screen(uint16_t stream, uint16_t dst);
void level_fetch_screen_from_rows(uint8_t horizontal_screen);
void level_fetch_screen_from_columns(uint8_t vertical_screen);

/* scroll.c */
void level_update_scroll(void);
void level_build_column(void);
void level_build_row(void);
void level_build_row_below(void);

/* nametable.c */
void level_update_nametable_mirror(void);
void level_draw(void);
void level_write_scroll_registers(void);

#endif
