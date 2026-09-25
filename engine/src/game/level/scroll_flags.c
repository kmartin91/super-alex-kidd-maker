/*
 * Scroll-flags updaters: per-level code run every frame (updateScrollFlags,
 * through scrollFlagsUpdaterPointer set at level start from
 * scrollFlagsUpdatersPointers $0D0A). They switch between the vertical and
 * horizontal phases of a level:
 *   $645B  default: end of level 1's vertical descent -> scroll right;
 *   $6476  castles (11, 16): end of a one-screen room move, and the
 *          persistent list of broken blocks of the room being entered;
 *   $6532  levels 5 and 9: drop into the lower row, then scroll right on it;
 *   $6574  level 3: the same with a 2-screen lower row.
 */
#include "level.h"

#define DROP_LOWER_ROW 1              /* vertical screen of the lower row */
#define QUARTER_DROP_LAST_SCREEN 3    /* levels 5, 9: the lower row has screens 0-3 */
#define SCREEN_0_DROP_WIDTH 1         /* level 3: the lower row has screens 0-1 */

/* VDP register 0, with interrupts disabled around the two control writes. */
static void set_vdp_register0_di(uint8_t value) {
    cpu.iff1 = cpu.iff2 = 0;
    ram8(v_VDPRegister0Value) = value;
    vdp_set_address(VDP_REGISTER(0, value));
    cpu.iff1 = cpu.iff2 = 1;
}

/* $6457 updateScrollFlags: jumps to the level's updater. */
LIFTED(updateScrollFlags, 0x6457) {
    cpu.hl = ram16(scrollFlagsUpdaterPointer);
    rt_dispatch(cpu.hl);
}

/* $645B: once the vertical part of level 1 is over (SCROLL_SPECIAL set,
 * neither up nor down), mask column 0 again and allow scrolling right. */
LIFTED(scrollFlagsUpdater_LABEL_6462_, 0x645B) {
    uint8_t flags = ram8(v_scrollFlags);
    if (flags == 0 || !(flags & SCROLL_SPECIAL) || (flags & SCROLL_VERTICAL_MASK)) LIFTED_RETURN();
    set_vdp_register0_di(VDP_R0_NORMAL);
    ram8(v_scrollFlags) = SCROLL_RIGHT;
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ castles */

/* Castle rooms keep the blocks broken in them: $D900 + row * $100 + column *
 * $20 holds, per room, `n, (flag, position) x n` (see docs/level-format.md 6.4).
 * When a room starts to enter, the positions whose flag is set are erased
 * (metatile 0) from the freshly decoded screen before it is drawn.
 *
 * v_currentScreenNumber equals h + v in castles (every one-screen move changes
 * it by 1), so the room column is (screen number - row).
 *
 * The original runs this loop on the alternate registers (exx) and leaves
 * them with their final values: BC' = room column * $20, HL' = end of the
 * record, DE' = last erased position (untouched if none). */
static void erase_broken_blocks_of_entering_room(uint8_t moving) {
    uint8_t row = (uint8_t)(ram8(v_verticalScreenNumber) + ((moving & SCROLL_DOWN) ? 1 : 0));
    uint8_t column = (uint8_t)((ram8(v_currentScreenNumber) & ~NEW_SCREEN) - row);
    uint8_t column_offset = (uint8_t)((column << 5) | (column >> 3)); /* RLCA x5 */
    uint16_t record = (uint16_t)(v_metatileDeletesTable + (row << 8) + column_offset);
    ram16(v_roomRecord) = record;

    uint16_t p = record;
    uint8_t count = rd8(p);
    if (count != 0) {
        p++;
        for (uint8_t n = count; n != 0; n--, p = (uint16_t)(p + 2)) {
            if (rd8(p) == 0) continue;
            uint8_t position = rd8((uint16_t)(p + 1));
            wr8((uint16_t)(v_decompressedLevelLayoutData + position), 0);
            cpu.de_ = position;
        }
    }
    cpu.bc_ = column_offset;
    cpu.hl_ = p;
}

/* $6476: castle room moves. The camera handler starts a move of exactly one
 * screen (flags = direction | SCROLL_SPECIAL). This updater:
 *   - masks column 0 during horizontal moves;
 *   - when the new room starts to enter (NEW_SCREEN), erases its broken blocks
 *     (once per move);
 *   - when the move is complete (scroll back at 0: horizontal scroll and
 *     accumulator, or row cursor and vertical accumulator), clears the
 *     direction, the speeds, the auto-walk and unmasks column 0. A downward
 *     move was counted twice in v_currentScreenNumber (hole + scroll), so one
 *     is taken back. */
LIFTED(scrollFlagsUpdater_LABEL_647D_, 0x6476) {
    uint8_t moving = ram8(v_scrollFlags) & (uint8_t)~SCROLL_SPECIAL;
    if (moving == 0) LIFTED_RETURN();

    uint16_t position, accumulator;
    if (moving & SCROLL_VERTICAL_MASK) {
        position = ram16(v_rowCursor);
        accumulator = ram16(v_verticalScrollAccumulator);
    } else {
        if (!ram8(v_roomMoveColumnMasked)) {
            ram8(v_roomMoveColumnMasked) = 1;
            set_vdp_register0_di(VDP_R0_NORMAL);
        }
        position = ram16(v_horizontalScroll);
        accumulator = ram16(v_horizontalScrollAccumulator);
    }

    if (!ram8(v_roomRecordApplied) && (ram8(v_currentScreenNumber) & NEW_SCREEN)) {
        ram8(v_roomRecordApplied) = 1;
        erase_broken_blocks_of_entering_room(moving);
    }

    if (position != 0 || accumulator != 0) LIFTED_RETURN();

    uint8_t flags = ram8(v_scrollFlags);
    if (flags & SCROLL_DOWN) {
        uint8_t screen = (uint8_t)(ram8(v_currentScreenNumber) - 1);
        ram8(v_currentScreenNumber) = screen & (uint8_t)~NEW_SCREEN;
    }
    ram8(v_scrollFlags) = flags & SCROLL_SPECIAL;
    ram8(v_roomMoveColumnMasked) = 0;
    ram8(v_shouldAlexStartWalkingtoNextScreen) = 0;
    ram8(v_roomRecordApplied) = 0;
    ram16(v_horizontalScrollSpeed) = 0;
    ram16(v_verticalScrollSpeed) = 0;
    set_vdp_register0_di(VDP_R0_UNMASKED);
    LIFTED_RETURN();
}

/* -------------------------------------------------------------- drops */

/* Common part of the drop updaters. Returns true when a finished drop must be
 * turned into horizontal scrolling on the lower row: the column cursor is
 * reset and v = 1. While horizontal: a vertical flag means a drop has just
 * started (see _LABEL_6671_): SCROLL_RIGHT is replaced by SCROLL_SPECIAL. */
static bool drop_has_ended(void) {
    uint8_t flags = ram8(v_scrollFlags);
    if (flags == 0) return false;
    if (!(flags & SCROLL_SPECIAL)) {
        if (flags & SCROLL_VERTICAL_MASK)
            ram8(v_scrollFlags) = (uint8_t)((ram8(v_scrollFlags) & ~SCROLL_RIGHT) | SCROLL_SPECIAL);
        return false;
    }
    return !(flags & SCROLL_VERTICAL_MASK);
}

/* Restart the column cursor at 0. QUIRK: the word write to $C0B7 also clears
 * $C0B8, the high byte of v_rowVdpAddress ($78): a later vertical row draw
 * would go to VRAM $0000+ (tile patterns). Never happens: the lower rows are
 * only scrolled horizontally. */
static void reset_column_cursor(void) {
    ram16(v_columnCursor) = 0;
    ram16(v_columnHalf) = 0;
    ram16(v_rowVdpAddress) = 0;
}

/* $6532 (levels 5 and 9): after the drop (crash or ENTER-DOWN) the lower row
 * scrolls right up to screen 3; dropping from screen >= 3 stops scrolling. */
LIFTED(scrollFlagsUpdater_LABEL_6539_, 0x6532) {
    if (!drop_has_ended()) LIFTED_RETURN();
    ram8(v_verticalScreenNumber) = DROP_LOWER_ROW;
    reset_column_cursor();
    if (ram8(v_horizontalScreenNumber) < QUARTER_DROP_LAST_SCREEN) {
        ram8(v_scrollFlags) = SCROLL_RIGHT;
        ram8(v_levelWidth) = QUARTER_DROP_LAST_SCREEN;
    } else {
        ram8(v_scrollFlags) = 0;
    }
    LIFTED_RETURN();
}

/* $6574 (level 3): after the drop the lower row scrolls right up to screen 1. */
LIFTED(scrollFlagsUpdater_LABEL_657B_, 0x6574) {
    if (!drop_has_ended()) LIFTED_RETURN();
    reset_column_cursor();
    ram8(v_verticalScreenNumber) = DROP_LOWER_ROW;
    ram8(v_levelWidth) = SCREEN_0_DROP_WIDTH;
    ram8(v_scrollFlags) = SCROLL_RIGHT;
    LIFTED_RETURN();
}
