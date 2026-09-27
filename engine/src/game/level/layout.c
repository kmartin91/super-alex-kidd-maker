/*
 * Level layout: the level descriptor, fetching screens through the rows and
 * columns tables, the screen RLE decoder, and screen drops.
 *
 * A level is a grid of screens. Two tables (in the bank named by the level
 * descriptor) give access to it:
 *   rows table    word per vertical screen v   -> row v:    word per horizontal screen h -> screen
 *   columns table word per horizontal screen h -> column h: word per vertical screen v   -> screen
 * Horizontal scrolling reads rows[v][h +- 1], vertical scrolling columns[h][v +- 1].
 * A screen is an RLE stream of 192 metatile ids (16 x 12, row-major), decoded
 * into v_decompressedLevelLayoutData ($D700). Only one screen is decoded at a
 * time: the one the scroll engine is currently drawing columns/rows from.
 *
 * Table indexes are doubled in 8 bits: at most 128 screens per row/column.
 */
#include "level.h"
#include "rt/maker.h"

#define DECODED_SCREEN v_decompressedLevelLayoutData  /* $D700, 192 bytes */

enum {
    LEVEL_MT_ETERNAL = 1,   /* vertical descent, then a horizontal part */
    LEVEL_SWAMP = 13,       /* starts on screen 7 and scrolls left */
    LEVEL_CRAG_LAKE = 17,   /* vertical */
};

#define REVERSE_LEVEL_START_SCREEN 7
#define FIRST_SCREEN_SCROLL_SPEED 0x0100  /* 1 px per step, towards the left */

/* Screen drops (_LABEL_6671_): entity index of the lower row (v_currentScreenNumber). */
#define DROP_SCREEN_0_ENTITY_INDEX 0x06        /* level 3: $86 */
#define DROP_QUARTER_ENTITY_INDEX_BASE 0x10    /* levels 5, 9: $90 + h/4 */

/* Slots destroyed by a drop when still off screen: 5-29 (0-based). */
#define DROP_FIRST_SLOT 0xC3A0
#define DROP_SLOT_COUNT 25

/* ---------------------------------------------------------------- RLE */

/* $6BE8 (_LABEL_6BEF_): decodes the screen RLE stream at `stream` (slot 2)
 * into `dst`:  0 = end;  $01-$7F n = next byte repeated n times;
 *              $81-$FF = the next (n & $7F) bytes copied.
 * The length is not checked: a stream longer than 192 bytes overwrites the RAM
 * after $D7C0. QUIRK: a literal token $80 copies 65536 bytes (LDIR with BC = 0). */
void level_decode_screen(uint16_t stream, uint16_t dst) {
    for (;;) {
        uint8_t token = rd8(stream);
        if (token == 0) return;
        if (!(token & 0x80)) {
            uint8_t value = rd8((uint16_t)(stream + 1));
            for (uint8_t n = token; n != 0; n--) wr8(dst++, value);
            stream = (uint16_t)(stream + 2);
        } else {
            uint32_t n = token & 0x7F;
            if (n == 0) n = 0x10000;
            stream++;
            while (n--) wr8(dst++, rd8(stream++));
        }
    }
}

LIFTED(_LABEL_6BEF_, 0x6BE8) {
    level_decode_screen(cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* -------------------------------------------------------- screen fetch */

/* rows[v][h] -> decoded screen, with v = v_verticalScreenNumber. The level
 * bank is mapped again before every fetch, so layouts and screens may live in
 * any bank. */
void level_fetch_screen_from_rows(uint8_t horizontal_screen) {
    uint8_t screen_offset = (uint8_t)(2 * horizontal_screen);
    uint8_t row_offset = (uint8_t)(2 * ram8(v_verticalScreenNumber));
    map_bank(ram8(v_levelBankNumber));
    uint16_t row = rd16((uint16_t)(ram16(v_rowsTable) + row_offset));
    uint16_t screen = rd16((uint16_t)(row + screen_offset));
    level_decode_screen(screen, DECODED_SCREEN);
}

/* columns[h][v] -> decoded screen, with h = v_horizontalScreenNumber. */
void level_fetch_screen_from_columns(uint8_t vertical_screen) {
    uint8_t screen_offset = (uint8_t)(2 * vertical_screen);
    uint8_t column_offset = (uint8_t)(2 * ram8(v_horizontalScreenNumber));
    map_bank(ram8(v_levelBankNumber));
    uint16_t column = rd16((uint16_t)(ram16(v_columnsTable) + column_offset));
    uint16_t screen = rd16((uint16_t)(column + screen_offset));
    level_decode_screen(screen, DECODED_SCREEN);
}

/* $683A (_LABEL_6841_): A = horizontal screen. Fetches rows[v][A], then
 * builds the scroll column from it (falls into loadLinesToNametable).
 * out: BC, DE as the column builder leaves them. */
LIFTED(_LABEL_6841_, 0x683A) {
    level_fetch_screen_from_rows(cpu.a);
    level_build_column();
    LIFTED_RETURN();
}

/* $6B1A (_LABEL_6B21_): fetches the screen below the camera, columns[h][v+1]. */
LIFTED(_LABEL_6B21_, 0x6B1A) {
    level_fetch_screen_from_columns((uint8_t)(ram8(v_verticalScreenNumber) + 1));
    LIFTED_RETURN();
}

/* $6B1E: A = vertical screen: fetches columns[h][A]. */
LIFTED(sub_6B1E, 0x6B1E) {
    level_fetch_screen_from_columns(cpu.a);
    LIFTED_RETURN();
}

/* ----------------------------------------------------------- loadLevel */

/* Address of level `level`'s 12-byte descriptor (LevelDescriptorPointerTable,
 * bank 1, read like rst $10). */
static uint16_t level_descriptor(uint8_t level) {
    return rd16((uint16_t)(LEVEL_DESCRIPTORS_MINUS_2 + (uint8_t)(2 * level)));
}

/* Copies the descriptor fields into the level state and maps the level bank. */
static void read_level_descriptor(uint8_t level) {
    uint16_t desc = level_descriptor(level);
    uint8_t bank = rd8((uint16_t)(desc + DESC_BANK));
    map_bank(bank);
    ram8(v_levelBankNumber) = bank;
    ram16(v_rowsTable) = rd16((uint16_t)(desc + DESC_ROWS_TABLE));
    ram16(v_columnsTable) = rd16((uint16_t)(desc + DESC_COLS_TABLE));
    ram8(v_horizontalScreenNumber) = rd8((uint16_t)(desc + DESC_START_X));
    ram8(v_verticalScreenNumber) = rd8((uint16_t)(desc + DESC_START_Y));
    ram8(v_levelWidth) = rd8((uint16_t)(desc + DESC_WIDTH));
    ram8(v_levelHeight) = rd8((uint16_t)(desc + DESC_HEIGHT));
    ram8(v_levelScrollFlags) = rd8((uint16_t)(desc + DESC_SCROLL_FLAGS));
    ram16(v_metatileTable) = rd16((uint16_t)(desc + DESC_METATILES));
}

/* Writes VDP register 0 and its RAM copy. */
static void set_vdp_register0(uint8_t value) {
    ram8(v_VDPRegister0Value) = value;
    vdp_set_address(VDP_REGISTER(0, value));
}

/* $65AA loadLevel: reads the level descriptor of v_level into the level
 * state, draws the first screen and sets the initial scroll flags.
 *
 * The first screen is drawn by the normal scroll code: starting from screen
 * startX with the camera at 0, it scrolls left 256 pixels one pixel at a
 * time, which builds 32 columns of screen rows[startY][startX-1] from right to
 * left. That screen's entities are never loaded (v_currentScreenNumber is set
 * without NEW_SCREEN afterwards). Hard-coded cases:
 *   levels 1 and 17: vertical start, v = 0, entity index 1 loaded at once, the
 *     first row of the screen below is prepared, VDP column 0 unmasked;
 *   level 13: scrolls left from screen 7;
 *   others: descriptor flag bit 7 makes Alex walk in (castle entrance) and
 *     starts the castle entity index at the descriptor's height byte. */
LIFTED(loadLevel, 0x65AA) {
    maker.zone.inside = false; /* a (re)started level starts in the main area */
    maker.zone.request = 0;
    maker.camera_both_ways = false;
    maker.scroll_flags_set = 0;
    read_level_descriptor(ram8(v_level));
    ram16(v_rowVdpAddress) = NAMETABLE_VDP_WRITE;
    ram16(v_topRowVdpAddress) = NAMETABLE_VDP_WRITE;

    do {
        ram16(v_horizontalScrollSpeed) = FIRST_SCREEN_SCROLL_SPEED;
        level_update_scroll();
        level_update_nametable_mirror();
        level_draw();
    } while (ram16(v_horizontalScroll) != 0);

    ram8(v_currentScreenNumber) = ram8(v_verticalScreenNumber);
    ram16(v_horizontalScrollSpeed) = 0;

    uint8_t level = ram8(v_level);
    if (level == LEVEL_MT_ETERNAL || level == LEVEL_CRAG_LAKE) {
        ram8(v_verticalScreenNumber) = 0;
        ram8(v_currentScreenNumber) = NEW_SCREEN | 1;
        if (maker.active) {
            /* Maker mode (rt/maker.h): the start screen's entities first,
             * then those of the screen below as in the original. */
            ram8(v_currentScreenNumber) = NEW_SCREEN | 0;
            maker.start_screen_pending = true;
            maker.after_start_screen = NEW_SCREEN | 1;
        }
        ram8(v_rowColumnsLeft) = SCREEN_METATILE_COLUMNS;
        level_build_row_below();
        level_update_nametable_mirror();
        level_draw();
        set_vdp_register0(VDP_R0_UNMASKED);
        ram8(v_scrollFlags) = ram8(v_levelScrollFlags);
    } else if (level == LEVEL_SWAMP) {
        ram8(v_currentScreenNumber) = REVERSE_LEVEL_START_SCREEN;
        ram8(v_scrollFlags) = ram8(v_levelScrollFlags);
    } else {
        uint8_t flags = ram8(v_levelScrollFlags);
        ram8(v_scrollFlags) = flags;
        if (flags & SCROLL_SPECIAL) {
            ram8(v_shouldAlexStartWalkingtoNextScreen) = 1;
            ram8(v_entityIndex) = ram8(v_levelHeight);
        } else if (maker.active && flags == SCROLL_RIGHT) {
            /* Maker mode (rt/maker.h), plain horizontal level: the camera
             * scrolls both ways, and the start screen's entities load too. */
            maker.camera_both_ways = true;
            maker.start_screen_pending = true;
            ram8(v_currentScreenNumber) |= NEW_SCREEN;
        }
    }
    LIFTED_RETURN();
}

/* ---------------------------------------------------------- screen drop */

/* $666A (_LABEL_6671_): Alex goes down into the screen below (ENTER-DOWN
 * tile, hole, vehicle crash, castle hole). The callers then set SCROLL_DOWN
 * and a vertical speed. Off-screen entities of slots 5-29 are destroyed, then
 * according to the scroll flags:
 *   SCROLL_SPECIAL (castles): the screen number steps on (+1, NEW_SCREEN);
 *   SCROLL_DROP_TO_QUARTER (levels 5, 9): lower screen h/4, entity index $10 + h/4;
 *   SCROLL_DROP_TO_SCREEN_0 (level 3): lower screen 0, entity index 6;
 * and the first row of the screen below is prepared. No flag: nothing. */
LIFTED(_LABEL_6671_, 0x666A) {
    ram8(v_isScrollingDownToNextScreen) = 1;

    for (uint16_t slot = DROP_FIRST_SLOT, n = 0; n < DROP_SLOT_COUNT; n++, slot += ENTITY_SIZE) {
        const Entity *e = entity_at(slot);
        if (e->type != 0 && e->isOffScreenFlags != 0) {
            cpu.ix = slot;
            CALL_ROUTINE(f_destroyCurrentEntity);
        }
    }

    uint8_t drop = ram8(v_scrollFlags) & (SCROLL_SPECIAL | SCROLL_DROP_TO_QUARTER | SCROLL_DROP_TO_SCREEN_0);
    if (drop == 0) LIFTED_RETURN();

    if (drop & SCROLL_SPECIAL) {
        ram8(v_currentScreenNumber) = (uint8_t)(ram8(v_currentScreenNumber) + 1) | NEW_SCREEN;
    } else {
        if (drop & SCROLL_DROP_TO_QUARTER) {
            uint8_t h = ram8(v_horizontalScreenNumber) >> 2;
            ram8(v_horizontalScreenNumber) = h;
            ram8(v_currentScreenNumber) = (uint8_t)(NEW_SCREEN + DROP_QUARTER_ENTITY_INDEX_BASE + h);
        } else {
            ram8(v_horizontalScreenNumber) = 0;
            ram8(v_currentScreenNumber) = NEW_SCREEN | DROP_SCREEN_0_ENTITY_INDEX;
        }
        /* The lower screen's entities are placed relative to the fine
         * horizontal scroll at the moment of the drop. */
        ram8(v_newEntityHorizontalOffset) = ram8(v_horizontalScrollPixel);
    }
    ram8(v_rowColumnsLeft) = SCREEN_METATILE_COLUMNS;
    level_build_row_below();
    map_bank(BANK(2));
    LIFTED_RETURN();
}

/* Maker levels (rt/maker.h, states/zone.c): draws the bonus zone, row
 * maker.zone.row of the level's layout, from its first screen, the way
 * loadLevel draws a plain horizontal level (camera both ways, the first
 * screen's entities loaded at once). The level variables are cleared. */
void maker_zone_draw(void) {
    read_level_descriptor(ram8(v_level));
    ram8(v_horizontalScreenNumber) = 1;
    ram8(v_verticalScreenNumber) = maker.zone.row;
    ram8(v_levelWidth) = maker.zone.width;
    ram8(v_levelHeight) = 0;
    ram8(v_levelScrollFlags) = SCROLL_RIGHT;
    ram16(v_rowVdpAddress) = NAMETABLE_VDP_WRITE;
    ram16(v_topRowVdpAddress) = NAMETABLE_VDP_WRITE;
    maker.camera_both_ways = false;
    maker.scroll_flags_set = 0;
    do {
        ram16(v_horizontalScrollSpeed) = FIRST_SCREEN_SCROLL_SPEED;
        level_update_scroll();
        level_update_nametable_mirror();
        level_draw();
    } while (ram16(v_horizontalScroll) != 0);
    ram16(v_horizontalScrollSpeed) = 0;
    ram8(v_scrollFlags) = SCROLL_RIGHT;
    maker.camera_both_ways = true;
    maker.start_screen_pending = true;
    maker.after_start_screen = 0;
    ram8(v_currentScreenNumber) = NEW_SCREEN | 0;
}
