/*
 * Level intro and level end (game states 3 and 4, $18CD-$1B3F).
 *
 * Level starting (state 3): shows the map of Radaxian on a parchment that
 * unrolls from left to right. The first call draws the rolled parchment and
 * the right roll, loads the map tiles and spawns Alex eating a rice ball.
 * Every 3 frames the next 2-tile-wide column of the map is revealed: the main
 * loop sets v_shouldUpdateMapNametable and the VBlank handler draws the column
 * plus the right roll one column further. When the map is complete
 * (v_mapLoadingState = $15) the arrow pointing at the current level (and
 * Janken's castle from level 16) is shown for $50 frames, then gameplay starts
 * (state $A, uninitialized).
 *
 * Level completed (state 4): the first call stops the music, clears the
 * screen, the entities, the shop stock and the per-level flags; the second
 * call increments v_level and enters the level intro.
 */
#include "states.h"

#define LEVEL_STARTING_DURATION 0x50  /* frames the finished map stays on screen */
#define MAP_COLUMN_REVEAL_FRAMES 0x03 /* frames between two revealed columns */
#define MAP_FULLY_REVEALED 0x15       /* v_mapLoadingState when the map is complete */
#define MAP_COLUMN_ROWS 0x12          /* each map column is 2 entries x 18 rows */
#define MAP_COLUMN_BYTES 0x02

/* Bank 5. */
#define MAP_PARCHMENT_COLUMNS 0x9E45           /* pointer to each map column (entry 0 unused) */
#define MAP_PARCHMENT_LEFT_COLUMN 0x9E75
#define MAP_PARCHMENT_LEFT_COLUMN2 0x9E99
#define MAP_PARCHMENT_RIGHT_SIDE_COLUMN1 0xA18D
#define MAP_PARCHMENT_RIGHT_SIDE_COLUMN2 0xA1B1
#define MAP_TILES 0xA1D5
#define LEVEL_STARTING_PALETTE 0x1B97          /* bank 0 */
/* Bank 7. */
#define ARROW_TILE 0xA209
#define JANKENS_CASTLE_TILES 0x8269
/* Bank 3. */
#define ALEX_EATING_RICE_BALL_TILES 0xBC69

#define LEVEL_10 0x0A /* completing it sets bit 0 of $D802 (cleared by entity $51) */

/* ------------------------------------------------------- level completed */

/* $18CD: nothing to do at VBlank. */
LIFTED(handleInterruptLevelCompletedState, 0x18CD) {
    LIFTED_RETURN();
}

/* $18CE: main-loop handler of state 4. */
LIFTED(updateLevelCompletedState, 0x18CE) {
    enter_state_handler();

    if (ram8(v_gameState) & STATE_INITIALIZED) {
        /* Second call: go to the next level's intro. */
        map_bank(BANK(2));
        wait_frame(IRQ_SPRITES);
        ram8(v_level)++;
        map_bank(BANK(5));
        ram8(v_gameState) = STATE_LEVEL_STARTING;
        LIFTED_RETURN();
    }

    ram8(v_gameState) |= STATE_INITIALIZED;
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
    cpu.b = 5;
    CALL_ROUTINE(f_sleepTenthsOfSecond);
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);

    fill_bytes(_RAM_D7D0_, 0x00, 0x0F); /* shop stock of the level */
    ram8(_RAM_C08E_) = 0;
    destroy_entities(ENTITY_SLOT(1), ENTITY_ARRAY_SIZE);
    fill_bytes(_RAM_D800_, 0x00, 0x08); /* per-level progress flags */
    if (ram8(v_level) == LEVEL_10) ram8(_RAM_D802_) |= 0x01;
    cpu.bc = 0; /* as left by the LDIR */

    /* VDP register 0 = $26 (mode 4, line interrupts, left column blank). */
    vdp_set_address(VDP_REGISTER(0, 0x26));
    cpu.de = VDP_REGISTER(0, 0x26);

    ram8(v_newEntityHorizontalOffset) = 0;
    ram8(v_shopFlags) = 0;
    ram8(v_alexActionState) = ACTION_NONE;
    ram8(v_shouldSpawnRidingBoat_RAM_C051_) = 0;
    ram8(v_currentLevelIsBonusLevel) = 0;
    enable_interrupts();
    TAIL_CALL(f_enableDisplay);
}

/* -------------------------------------------------------- level starting */

/* Draws one 2x18 column of the map parchment at the destination pointer. */
static void draw_map_column(uint16_t src, uint16_t vram_dst) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.b = MAP_COLUMN_ROWS;
    cpu.c = MAP_COLUMN_BYTES;
    CALL_HELPER(f_copyNameTableBlockToVram);
}

/* Moves v_currentMapNametableDestinationPointer by `delta` entries of 2 bytes. */
static uint16_t advance_map_destination(int delta) {
    uint16_t dst = (uint16_t)(ram16(v_currentMapNametableDestinationPointer) + delta * 2);
    ram16(v_currentMapNametableDestinationPointer) = dst;
    return dst;
}

/* initLevelStartingState ($1A46): draws the rolled-up parchment. */
static void init_level_starting(void) {
    ram8(v_gameState) |= STATE_INITIALIZED;
    CALL_HELPER(f_clearVDPTablesAndDisableScreen);
    CALL_HELPER(f_clearEntities);
    CALL_HELPER(f_clearScroll);
    map_bank(BANK(2));
    CALL_HELPER(f_reset_9DF3); /* audioEngine.reset */

    ram8(v_mapLoadingState) = 0;
    ram8(v_shouldUpdateMapNametable) = 0;
    ram8(v_nextMapNametableUpdateTimer) = MAP_COLUMN_REVEAL_FRAMES;

    map_bank(BANK(5));
    cpu.hl = LEVEL_STARTING_PALETTE;
    cpu.de = VDP_CRAM_WRITE(0);
    cpu.bc = 0x0010;
    CALL_HELPER(f_copyBytesToVRAM);
    /* First sprite colour black. */
    vdp_set_address(VDP_CRAM_WRITE(0x10));
    vdp_write(0x00);
    cpu.hl = MAP_TILES;
    cpu.de = VDP_VRAM_WRITE(0x0000);
    CALL_HELPER(f_decompressTilesToVram);

    /* Left edge, first column, then the right roll (2 columns). */
    ram16(v_currentMapNametableDestinationPointer) = NAMETABLE_WRITE(4, 3);
    draw_map_column(MAP_PARCHMENT_LEFT_COLUMN, NAMETABLE_WRITE(4, 3));
    draw_map_column(MAP_PARCHMENT_LEFT_COLUMN2, advance_map_destination(1));
    draw_map_column(MAP_PARCHMENT_RIGHT_SIDE_COLUMN1, advance_map_destination(1));
    draw_map_column(MAP_PARCHMENT_RIGHT_SIDE_COLUMN2, advance_map_destination(1));
    /* The next revealed column goes right after the left edge. */
    ram16(v_currentMapNametableDestinationPointer) = NAMETABLE_WRITE(5, 3);
    ram8(v_mapLoadingState) = 1;

    map_bank(BANK(7));
    cpu.de = VDP_VRAM_WRITE(0x2800); /* blank sprite tile $40 */
    cpu.bc = 0x0020;
    cpu.l = 0x00;
    CALL_HELPER(f_fillVram);
    cpu.hl = ARROW_TILE;
    cpu.de = VDP_VRAM_WRITE(0x2820);
    cpu.bc = 0x0020;
    CALL_HELPER(f_copyBytesToVRAM);
    cpu.hl = JANKENS_CASTLE_TILES;
    cpu.de = VDP_VRAM_WRITE(0x2840);
    cpu.bc = 0x0140;
    CALL_HELPER(f_copyBytesToVRAM);

    /* Only 3 entity slots on this screen. */
    ram8(v_entitydataArrayLength) = 3;
    ram16(v_entitydataArrayPointer) = v_entities;
    map_bank(BANK(3));
    cpu.hl = ALEX_EATING_RICE_BALL_TILES;
    cpu.de = VDP_VRAM_WRITE(0x2C00);
    CALL_HELPER(f_decompressTilesToVram);
    cpu.ix = ENTITY_SLOT(3);
    entity_at(ENTITY_SLOT(3))->type = ENTITY_ALEX_EATING_RICE_BALL;
    map_bank(BANK(2));
    CALL_HELPER(f_updateEntities);

    map_bank(BANK(5));
    ram8(v_levelStartingTimer) = LEVEL_STARTING_DURATION;
    ram8(v_soundControl) = SOUND_LEVEL_STARTING;
    enable_interrupts();
}

/* $194F: main-loop handler of state 3. */
LIFTED(updateLevelStartingState, 0x194F) {
    enter_state_handler();

    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        init_level_starting();
        TAIL_CALL(f_enableDisplay);
    }

    if (ram8(v_mapLoadingState) != MAP_FULLY_REVEALED) {
        /* Unrolling: the interrupt handler draws a column when requested. */
        map_bank(BANK(2));
        wait_frame(ram8(v_shouldUpdateMapNametable) ? IRQ_SPRITES_AND_STATE : IRQ_SPRITES);
        CALL_ROUTINE(f_updateEntities);
        map_bank(BANK(5));
        ram8(v_shouldUpdateMapNametable) = 0;
        if (--ram8(v_nextMapNametableUpdateTimer) != 0) LIFTED_RETURN();

        uint8_t column = ++ram8(v_mapLoadingState);
        uint8_t offset = (uint8_t)(column * 2);
        cpu.bc = offset;
        ram16(v_currentMapOrTextNametablePointer) = rd16((uint16_t)(MAP_PARCHMENT_COLUMNS + offset));
        ram8(v_nextMapNametableUpdateTimer) = MAP_COLUMN_REVEAL_FRAMES;
        ram8(v_shouldUpdateMapNametable) = 1;
        advance_map_destination(1);
        LIFTED_RETURN();
    }

    /* updateLevelStartingStateMapAnimated ($19AB): the map is complete. */
    cpu.ix = ENTITY_SLOT(1);
    entity_at(ENTITY_SLOT(1))->type = ENTITY_MAP_ARROW;
    if (ram8(v_level) >= 0x10) {
        cpu.ix = ENTITY_SLOT(2);
        Entity *castle = entity_at(ENTITY_SLOT(2));
        castle->type = ENTITY_JANKENS_CASTLE;
        X_PIXEL(castle) = 0x98;
        Y_PIXEL(castle) = 0x50;
    }
    do {
        map_bank(BANK(2));
        wait_frame(IRQ_SPRITES);
        CALL_ROUTINE(f_updateEntities);
    } while (--ram8(v_levelStartingTimer) != 0);

    cpu.ix = ENTITY_SLOT(1);
    CALL_ROUTINE(f_destroyCurrentEntity);
    cpu.ix = ENTITY_SLOT(2);
    CALL_ROUTINE(f_destroyCurrentEntity);
    cpu.ix = ENTITY_SLOT(3);
    CALL_ROUTINE(f_destroyCurrentEntity);
    CALL_ROUTINE(f_updateEntities);
    ram8(v_gameState) = STATE_GAMEPLAY;
    cpu.b = 5;
    TAIL_CALL(f_sleepTenthsOfSecond);
}

/* $1A01: VBlank handler of state 3: draws the newly revealed map column and
 * the right roll of the parchment just after it. */
LIFTED(handleInterruptLevelStartingState, 0x1A01) {
    map_bank(BANK(5));
    draw_map_column(ram16(v_currentMapOrTextNametablePointer), ram16(v_currentMapNametableDestinationPointer));
    draw_map_column(MAP_PARCHMENT_RIGHT_SIDE_COLUMN1, advance_map_destination(1));
    draw_map_column(MAP_PARCHMENT_RIGHT_SIDE_COLUMN2, advance_map_destination(1));
    advance_map_destination(-2);
    LIFTED_RETURN();
}
