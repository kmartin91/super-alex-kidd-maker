/*
 * Scroll engine: updateScroll ($67BD) and the column/row builders.
 *
 * The scrolling state machine
 * ---------------------------
 * The camera moves by v_horizontalScrollSpeed or v_verticalScrollSpeed (8.8
 * fixed point, never both: a horizontal speed disables vertical scrolling for
 * the frame). Speeds are set by the camera handlers of Alex's code; the scroll
 * flags (v_scrollFlags) say which directions are allowed.
 *
 * Horizontal (the name table is exactly 32 columns = one screen wide):
 *   - v_horizontalScroll (high byte = VDP register 8) moves by the speed; the
 *     accumulator v_horizontalScrollAccumulator counts pixels and every 8
 *     pixels one 8-px column is built into $CF00 (v_columnToDraw).
 *   - The column cursor (0-31, v_columnHalf / v_columnCursor /
 *     v_nametableColumn) is the name-table column where the next column goes
 *     when moving right; it is also the column that enters on the left when
 *     moving left (after stepping back), because the column just past the
 *     right edge and the leftmost (masked) column are the same.
 *   - Moving right (negative speed): when the cursor is at column 0 the next
 *     screen rows[v][h+1] is fetched (v_currentScreenNumber + 1, NEW_SCREEN);
 *     when the cursor wraps from 31 to 0, h is incremented and, if h reached
 *     v_levelWidth, SCROLL_RIGHT is cleared: the scroll stops.
 *     Without SCROLL_RIGHT the speed is zeroed at the next column boundary.
 *   - Moving left (positive speed): the cursor steps back first; when it wraps
 *     from 0 to 31, h is decremented and rows[v][h] fetched
 *     (v_currentScreenNumber - 1, NEW_SCREEN). At h = 0 the move is refused:
 *     SCROLL_LEFT is cleared, the speed zeroed and the scroll pixel set to 8.
 *
 * Vertical (the name table is 28 rows, the screen 24):
 *   - v_verticalScroll (high byte = VDP register 9, kept in 0-$DF) moves by
 *     the speed; every 8 pixels v_topRowVdpAddress (the name-table row at the
 *     top of the screen grid) moves by one row, wrapping at 28 rows, and one
 *     8-px row is built into $CF38 (v_rowToDraw) with the row cursor (0-23).
 *   - Moving down: the row built is the one entering below the screen; when
 *     the cursor wraps from 23 to 0, v is incremented and the screen below,
 *     columns[h][v+1], fetched. When v reaches v_levelHeight, v is reset to 0,
 *     SCROLL_DOWN cleared and the scroll snapped to a multiple of 16 lines:
 *     horizontal scrolling then continues on row 0 of the rows table.
 *   - Moving up: when the cursor wraps from 0 to 23, v is decremented and
 *     columns[h][v] fetched; at v = 0 SCROLL_UP is cleared and the speed zeroed.
 *
 * Every screen change updates v_currentScreenNumber with NEW_SCREEN (bit 7),
 * which tells the entity loader to spawn the entities of the new screen.
 *
 * Only one decoded screen exists ($D700). The engine is therefore one-way per
 * screen: reversing direction in the middle of a screen would build the new
 * columns/rows from the wrong screen. The original levels never do that
 * (castles always move by exactly one screen).
 *
 * Register results: several callers read BC and DE after updateScroll
 * (out: BC DE), so each exit path leaves them as the original does; this is
 * noted where it happens.
 */
#include "level.h"

#define COLUMNS_PER_SCREEN 32           /* 8-px columns */
#define COLUMN_CURSOR_STEP 0x0080       /* v_columnCursor per column */
#define COLUMN_CURSOR_LAST 0x0F80       /* column 31 */
#define COLUMN_CURSOR_WRAP_HIGH 0x10    /* 32 columns */
#define NAMETABLE_COLUMN_LAST 0x3E      /* byte offset of name-table column 31 */
#define NAMETABLE_ROW_WIDTH 0x40        /* bytes per name-table row */
#define ROW_CURSOR_STEP 0x0080
#define ROW_CURSOR_LAST 0x0B80          /* row 23 */
#define ROW_CURSOR_WRAP_HIGH 0x0C       /* 24 rows */
#define PIXELS_PER_CELL 8
#define LEFT_EDGE_SCROLL_PIXEL 8        /* camera position when the left edge is reached */

/* ---------------------------------------------------- column / row builders */

/* $685E loadLinesToNametable: builds one 8-px column of the decoded screen
 * into v_columnToDraw: for each of the v_columnRowsLeft metatile rows (12),
 * the top and bottom words of metatile column v_metatileColumn, left or
 * right half (v_columnHalf). Maps bank 5 (metatile table).
 * out: BC = 2*half - 4 and DE = end of the buffer, as the LDIs leave them. */
void level_build_column(void) {
    ram8(v_nametableBuffersReady) = COLUMN_READY;
    uint16_t cell = (uint16_t)(v_decompressedLevelLayoutData + ram8(v_metatileColumn));
    uint16_t out = v_columnToDraw;
    map_bank(BANK(5));
    uint8_t half_offset, rows_left;
    do {
        uint16_t entry = metatile_entry(rd8(cell));
        half_offset = (uint8_t)(2 * ram8(v_columnHalf));
        uint16_t top = (uint16_t)(entry + half_offset);        /* TL or TR */
        uint16_t bottom = (uint16_t)(top + 4);                 /* BL or BR */
        wr8(out++, rd8(top));
        wr8(out++, rd8((uint16_t)(top + 1)));
        wr8(out++, rd8(bottom));
        wr8(out++, rd8((uint16_t)(bottom + 1)));
        rows_left = (uint8_t)(ram8(v_columnRowsLeft) - 1);
        ram8(v_columnRowsLeft) = rows_left;
        cell = (uint16_t)(cell + SCREEN_METATILE_COLUMNS);
    } while (rows_left != 0);
    cpu.bc = (uint16_t)(half_offset - 4);
    cpu.de = out;
}

LIFTED(loadLinesToNametable_LABEL_6865_, 0x685E) {
    level_build_column();
    LIFTED_RETURN();
}

/* $6A6F (_LABEL_6A76_): builds one 8-px row of the decoded screen into
 * v_rowToDraw: metatile row v_metatileRow, top or bottom half (v_rowHalf),
 * v_rowColumnsLeft metatiles (16) of 2 words. Maps bank 5.
 * out: BC = 4*half - 4 and DE = end of the buffer, as the LDIs leave them. */
void level_build_row(void) {
    ram8(v_nametableBuffersReady) = ROW_READY;
    uint16_t cell = (uint16_t)(v_decompressedLevelLayoutData + SCREEN_METATILE_COLUMNS * ram8(v_metatileRow));
    uint16_t out = v_rowToDraw;
    map_bank(BANK(5));
    uint8_t half_offset, columns_left;
    do {
        uint16_t entry = metatile_entry(rd8(cell));
        half_offset = (uint8_t)(4 * ram8(v_rowHalf));
        uint16_t words = (uint16_t)(entry + half_offset);      /* TL TR or BL BR */
        for (int i = 0; i < 4; i++) wr8(out++, rd8((uint16_t)(words + i)));
        columns_left = (uint8_t)(ram8(v_rowColumnsLeft) - 1);
        ram8(v_rowColumnsLeft) = columns_left;
        cell++;
    } while (columns_left != 0);
    cpu.bc = (uint16_t)(half_offset - 4);
    cpu.de = out;
}

LIFTED(_LABEL_6A76_, 0x6A6F) {
    level_build_row();
    LIFTED_RETURN();
}

/* $6A6C (_LABEL_6A73_): fetches the screen below the camera
 * (columns[h][v+1]) and builds a row from it. */
void level_build_row_below(void) {
    level_fetch_screen_from_columns((uint8_t)(ram8(v_verticalScreenNumber) + 1));
    level_build_row();
}

LIFTED(_LABEL_6A73_, 0x6A6C) {
    level_build_row_below();
    LIFTED_RETURN();
}

/* ------------------------------------------------------- horizontal */

/* v_currentScreenNumber +- 1, with NEW_SCREEN set (a screen starts to enter:
 * load its entities) or cleared (the step is taken back). */
static void step_screen_number(int delta, bool new_screen) {
    uint8_t n = (uint8_t)(ram8(v_currentScreenNumber) + delta);
    ram8(v_currentScreenNumber) = new_screen ? (uint8_t)(n | NEW_SCREEN) : (uint8_t)(n & ~NEW_SCREEN);
}

/* Flips a metatile half (v_columnHalf / v_rowHalf). Except when moving down,
 * the original does it between two EX AF,AF' that park a carry flag, so AF'
 * is left with A = the new half and the flags of its AND 1. */
static void step_half(uint16_t half, int delta) {
    uint8_t value = (uint8_t)(ram8(half) + delta);
    ram8(half) = value & 1;
    cpu.af_ = z80_af_after_and(value, 1);
}

/* Camera moving left (positive speed). `accumulator` = old accumulator + speed. */
static void scroll_left(uint16_t accumulator) {
    ram16(v_horizontalScrollAccumulator) = accumulator;
    if ((accumulator >> 8) < PIXELS_PER_CELL) return;
    ram8(v_horizontalColumnPixels) = (uint8_t)((accumulator >> 8) & 7);

    /* Step the column cursor back one column. */
    uint8_t nt_column = ram8(v_nametableColumn);
    ram8(v_nametableColumn) = nt_column < 2 ? NAMETABLE_COLUMN_LAST : (uint8_t)(nt_column - 2);
    uint16_t cursor = ram16(v_columnCursor);
    bool previous_screen = cursor < COLUMN_CURSOR_STEP;
    ram16(v_columnCursor) = previous_screen ? COLUMN_CURSOR_LAST : (uint16_t)(cursor - COLUMN_CURSOR_STEP);
    step_half(v_columnHalf, -1);
    cpu.bc = COLUMN_CURSOR_STEP;

    if (!previous_screen) {
        level_build_column();
        return;
    }
    /* The cursor wrapped: the column comes from the screen to the left. */
    uint8_t h = ram8(v_horizontalScreenNumber);
    ram8(v_horizontalScreenNumber) = (uint8_t)(h - 1);
    step_screen_number(-1, true);
    if (h != 0) {
        level_fetch_screen_from_rows((uint8_t)(h - 1));
        level_build_column();
        return;
    }
    /* Left edge of the level. QUIRK: h is left at $FF and the column cursor
     * is not restored. */
    step_screen_number(+1, false);
    ram8(v_scrollFlags) &= (uint8_t)~SCROLL_LEFT;
    ram16(v_horizontalScrollSpeed) = 0;
    ram8(v_horizontalScrollPixel) = LEFT_EDGE_SCROLL_PIXEL;
}

/* Camera moving right (negative speed). `accumulator` = old accumulator +
 * speed, `crossed` = that sum went below zero (no carry). */
static void scroll_right(uint16_t accumulator, bool crossed) {
    ram16(v_horizontalScrollAccumulator) = accumulator;
    if (!crossed) return;
    if (!(ram8(v_scrollFlags) & SCROLL_RIGHT)) {
        ram16(v_horizontalScrollSpeed) = 0;
        return;
    }
    ram8(v_horizontalColumnPixels) = (uint8_t)((accumulator >> 8) & 7);

    if (ram8(v_nametableColumn) == 0) {
        /* Column 0 of the next screen. */
        step_screen_number(+1, true);
        level_fetch_screen_from_rows((uint8_t)(ram8(v_horizontalScreenNumber) + 1));
    }
    level_build_column();

    /* Step the column cursor forward. */
    uint8_t nt_column = (uint8_t)(ram8(v_nametableColumn) + 2);
    ram8(v_nametableColumn) = nt_column >= NAMETABLE_ROW_WIDTH ? 0 : nt_column;
    uint16_t cursor = (uint16_t)(ram16(v_columnCursor) + COLUMN_CURSOR_STEP);
    bool screen_done = (cursor >> 8) >= COLUMN_CURSOR_WRAP_HIGH;
    ram16(v_columnCursor) = screen_done ? 0 : cursor;
    step_half(v_columnHalf, +1);

    cpu.bc = ram16(v_levelWidth);   /* ld bc,(v_levelWidth): C = width, B = $C0A1 */
    if (!screen_done) return;
    uint8_t h = (uint8_t)(ram8(v_horizontalScreenNumber) + 1);
    ram8(v_horizontalScreenNumber) = h;
    if (h < ram8(v_levelWidth)) return;
    /* Last screen reached. */
    ram8(v_scrollFlags) &= (uint8_t)~SCROLL_RIGHT;
    step_screen_number(-1, false);
}

/* --------------------------------------------------------- vertical */

/* Camera moving down (positive speed). */
static void scroll_down(uint16_t accumulator) {
    ram16(v_verticalScrollAccumulator) = accumulator;
    if ((accumulator >> 8) < PIXELS_PER_CELL) return;
    ram8(v_verticalRowPixels) = (uint8_t)((accumulator >> 8) & 7);

    uint16_t top = (uint16_t)(ram16(v_topRowVdpAddress) + NAMETABLE_ROW_WIDTH);
    if ((top >> 8) == NAMETABLE_VDP_END_HIGH) top = (uint16_t)((NAMETABLE_VDP_WRITE & 0xFF00) | (top & 0xFF));
    ram16(v_topRowVdpAddress) = top;
    ram8(v_rowHalf) = (uint8_t)(ram8(v_rowHalf) + 1) & 1;   /* no EX AF,AF' on this path */
    uint16_t cursor = (uint16_t)(ram16(v_rowCursor) + ROW_CURSOR_STEP);
    ram16(v_rowCursor) = cursor;
    cpu.bc = ROW_CURSOR_STEP;
    if ((cursor >> 8) < ROW_CURSOR_WRAP_HIGH) {
        level_build_row();
        return;
    }
    /* The screen below is now fully in view. */
    step_screen_number(+1, true);
    ram16(v_rowCursor) = 0;
    cpu.bc = ram16(v_levelHeight);  /* ld bc,(v_levelHeight): C = height, B = $C0A6 */
    uint8_t v = (uint8_t)(ram8(v_verticalScreenNumber) + 1);
    ram8(v_verticalScreenNumber) = v;
    if (v < ram8(v_levelHeight)) {
        level_build_row_below();
        return;
    }
    /* Bottom of the level: stop, back to row 0 of the rows table. */
    ram8(v_verticalScreenNumber) = 0;
    ram8(v_scrollFlags) &= (uint8_t)~SCROLL_DOWN;
    ram16(v_verticalScrollSpeed) = 0;
    ram8(v_verticalScrollLine) &= 0xF0;
    ram8(v_verticalRowPixels) = 0;
    step_screen_number(-1, false);
}

/* Camera moving up (negative speed). `crossed` = the accumulator went below zero. */
static void scroll_up(uint16_t accumulator, bool crossed) {
    ram16(v_verticalScrollAccumulator) = accumulator;
    if (!crossed) return;
    ram8(v_verticalRowPixels) = (uint8_t)((accumulator >> 8) & 7);

    uint16_t top = (uint16_t)(ram16(v_topRowVdpAddress) - NAMETABLE_ROW_WIDTH);
    if ((top >> 8) == NAMETABLE_VDP_BEFORE_HIGH) top = (uint16_t)((NAMETABLE_VDP_LAST_ROW_HIGH << 8) | (top & 0xFF));
    ram16(v_topRowVdpAddress) = top;
    uint16_t cursor = ram16(v_rowCursor);
    bool previous_screen = cursor < ROW_CURSOR_STEP;
    ram16(v_rowCursor) = previous_screen ? ROW_CURSOR_LAST : (uint16_t)(cursor - ROW_CURSOR_STEP);
    step_half(v_rowHalf, -1);
    cpu.bc = ROW_CURSOR_STEP;
    if (!previous_screen) {
        level_build_row();
        return;
    }
    uint8_t v = ram8(v_verticalScreenNumber);
    if (v == 0) {
        /* Top of the level. */
        ram8(v_scrollFlags) &= (uint8_t)~SCROLL_UP;
        ram16(v_verticalScrollSpeed) = 0;
        return;
    }
    step_screen_number(-1, true);
    ram8(v_verticalScreenNumber) = (uint8_t)(v - 1);
    level_fetch_screen_from_columns((uint8_t)(v - 1));
    level_build_row();
}

/* $69C4 updateVerticalScroll (part of updateScroll). */
static void update_vertical_scroll(void) {
    uint16_t speed = ram16(v_verticalScrollSpeed);
    cpu.de = speed;
    if (speed == 0) return;
    ram8(v_rowColumnsLeft) = SCREEN_METATILE_COLUMNS;

    uint16_t old_accumulator = ram16(v_verticalScrollAccumulator);
    uint16_t scroll = (uint16_t)(ram16(v_verticalScroll) + speed);
    ram16(v_verticalScroll) = scroll;
    /* Keep the scroll line inside the 28-row name table. */
    uint8_t line = (uint8_t)(scroll >> 8);
    if (line >= NAMETABLE_LINES) line = (speed & 0x8000) ? (uint8_t)(line - 0x20) : (uint8_t)(line - NAMETABLE_LINES);
    ram8(v_verticalScrollLine) = line;

    cpu.bc = old_accumulator;  /* left in BC/DE when no row is due */
    cpu.de = scroll;
    uint32_t sum = (uint32_t)speed + old_accumulator;
    if (!(speed & 0x8000)) scroll_down((uint16_t)sum);
    else scroll_up((uint16_t)sum, sum <= 0xFFFF);
}

/* $67BD updateScroll: moves the camera by the horizontal speed, or if it is
 * zero by the vertical speed, and prepares the column or row that enters
 * the screen (see the top of this file). Called every frame of gameplay, and
 * in loops to draw whole screens (loadLevel, sub-area loader).
 * out: BC, DE (see the exit paths). */
void level_update_scroll(void) {
    uint16_t speed = ram16(v_horizontalScrollSpeed);
    if (speed == 0) {
        update_vertical_scroll();
        return;
    }
    ram8(v_columnRowsLeft) = SCREEN_METATILE_ROWS;

    uint16_t old_accumulator = ram16(v_horizontalScrollAccumulator);
    uint16_t scroll = (uint16_t)(ram16(v_horizontalScroll) + speed);
    ram16(v_horizontalScroll) = scroll;

    cpu.bc = old_accumulator;  /* left in BC/DE when no column is due */
    cpu.de = scroll;
    uint32_t sum = (uint32_t)speed + old_accumulator;
    if (!(speed & 0x8000)) scroll_left((uint16_t)sum);
    else scroll_right((uint16_t)sum, sum <= 0xFFFF);
}

LIFTED(updateScroll_LABEL_67C4_, 0x67BD) {
    level_update_scroll();
    LIFTED_RETURN();
}
