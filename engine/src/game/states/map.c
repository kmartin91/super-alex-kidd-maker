/*
 * Pause map and inventory (game state $B, $1FCD-$2673).
 *
 * Pressing pause during gameplay (NMI, see handlePauseInterrupt) sets
 * v_shouldOpenMap; the gameplay handler then switches to state $B.
 *
 * Opening: the music is stopped, the level variables and the RAM name table
 * are saved, and the pause screen is built in the RAM name table: the map of
 * Radaxian (24 columns), the items Alex owns (copyPauseItemsToNametable),
 * lives, money and score. Map entity slots are used instead of the level's:
 * 1 = item selection arrow, 2 = arrow on the map, 3 = Janken's castle (from
 * level 16).
 *
 * Using an item: the selection arrow (updateItemSelectArrow) moves with the
 * pad; when it is over an item (tile flags bits 5-7 = item number) it blinks
 * and button 1/2 uses it through the table at $2544:
 *   1 magic capsule A, 2 magic capsule B, 3 telepathy ball (action $06, the
 *   ball stays in the inventory), 4 cane of flight, 5 teleport powder,
 *   6 Hirotta stone (shows its inscription full screen), 7 power bracelet.
 * (The sun stone medallion's right half is also tagged 7, but the arrow
 * cannot reach it: x is limited to $70-$E8.)
 * Using an item sets v_alexActionState, clears the item's picture (RAM name
 * table now, VRAM at the next VBlank) and removes the capsule/bracelet
 * entities Alex may carry. Only one item can be used per pause (bit 0 of
 * v_inventoryItemSelectionState), and none while on a vehicle.
 *
 * Closing (pause again): exitMapState restores everything, reloads the level
 * graphics (and the tiles of the item in use) and resumes gameplay ($8A).
 */
#include "states.h"

#define MAP_COLUMNS 0x18
#define MAP_PARCHMENT_COLUMNS 0x9E45   /* bank 5: pointer to each map column */
#define MAP_PARCHMENT_LEFT_COLUMN 0x9E75
#define MAP_TILES 0xA1D5               /* bank 5 */
#define MAGIC_CAPSULE_TILES 0xAF11     /* bank 5 */
#define ALEX_STATE_TILES 0xB0B1        /* bank 5 */
#define TEXT_CHARACTER_TILES 0xB2B1    /* bank 5 */
#define NUMBER_TILES 0xB385            /* bank 2 */
#define ARROW_TILE 0xA209              /* bank 7 */
#define JANKENS_CASTLE_TILES 0x8269    /* bank 7 */
#define BOX_TILES 0x8000               /* bank 3 */
#define PAUSE_SCORE_TEXT_TILES 0x8000  /* bank 7 */
#define PAUSE_MENU_PALETTE 0x23FD      /* bank 0 */
#define PAUSE_SCORE_TEXT_ENTRIES 0x241D
#define PAUSE_LIVES_ICON_ENTRIES 0x2429
#define PAUSE_MONEY_BAG_ENTRIES 0x2431
#define PAUSE_ITEMS_DRAW_ENTRIES 0xBDB9 /* bank 6: item 10..1 -> (destination, picture) */
#define ITEM_USE_ROUTINES 0x2544        /* bank 0: item 1-7 */
#define ARROW_SPRITE_DESCRIPTOR 0x8A1D
#define ARROW_ANIMATION_DESCRIPTOR 0x8A18
#define HIROTTA_STONE_PALETTE 0x2674
#define HIROTTA_STONE_NAMETABLE 0x9924 /* bank 5 */
#define HIROTTA_STONE_TILES 0x9AD4     /* bank 5 */

#define ITEM_SELECTION_USED 0x01   /* v_inventoryItemSelectionState: an item was used */
#define ITEM_SELECTION_REDRAW 0x80 /* ... and its picture must be cleared in VRAM */

/* Sounds (reference names; they accompany the cane of flight / invincibility). */
#define SOUND_ITEM_ACTION_1 SOUND_MAGIC_CAPSULE_A
#define SOUND_ITEM_ACTION_2 SOUND_MAGIC_CAPSULE_B

static void copy_to_vram(uint16_t src, uint16_t vram_dst, uint16_t count) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.bc = count;
    CALL_HELPER(f_copyBytesToVRAM);
}

static void copy_mirrored_tiles(uint16_t src, uint16_t count) {
    cpu.hl = src;
    cpu.bc = count;
    CALL_HELPER(f_copyMirroredTilesToVramAtCurrentAddress);
}

/* The song of the level, adjusted for swimming, vehicles and bonus levels
 * (same logic when the map opens and when it closes, in a different order). */
static uint8_t swimming_song(uint8_t song) {
    if (entity_at(v_alex)->state == ALEX_SWIMMING) {
        song = SOUND_UNDERWATER_SONG;
        if (ram8(v_level) == 0x10) song = SOUND_CASTLE_SONG;
    }
    return song;
}
static uint8_t vehicle_song(uint8_t song) {
    uint8_t action = ram8(v_alexActionState);
    if (action == ACTION_RIDING_MOTORCYCLE) song = SOUND_BIKE_SONG;
    else if (action > ACTION_RIDING_MOTORCYCLE && action != ACTION_RIDING_BOAT) song = SOUND_PETICOPTER_SONG;
    return song;
}

/* initMapState ($2198): builds the pause screen. */
static void open_map(void) {
    ram8(v_gameState) |= STATE_INITIALIZED;
    CALL_HELPER(f_reset_9DF3); /* audioEngine.reset */
    copy_bytes(v_temporaryLevelDataCopy, v_levelWidth, 0x2A);
    fill_bytes(v_levelWidth, 0x00, 0x2A);
    CALL_HELPER(f_clearVDPTablesAndDisableScreen);
    cpu.b = 5;
    CALL_HELPER(f_sleepTenthsOfSecond);
    copy_bytes(v_nametableCopy, v_nametable, 0x700);
    fill_bytes(v_nametable, 0x00, 0x700);

    /* The whole map, column by column, into the RAM name table. */
    ram8(v_mapLoadingState) = 0;
    map_bank(BANK(5));
    ram16(v_currentMapNametableDestinationPointer) = _RAM_C808_;
    ram16(v_currentMapOrTextNametablePointer) = MAP_PARCHMENT_LEFT_COLUMN;
    for (int i = 0; i < MAP_COLUMNS; i++) {
        cpu.de = ram16(v_currentMapNametableDestinationPointer);
        cpu.hl = ram16(v_currentMapOrTextNametablePointer);
        cpu.bc = 0x1202;
        CALL_HELPER(f_copyTileBlock);
        ram16(v_currentMapNametableDestinationPointer) += 2;
        uint8_t column = ++ram8(v_mapLoadingState);
        ram16(v_currentMapOrTextNametablePointer) = rd16((uint16_t)(MAP_PARCHMENT_COLUMNS + (uint8_t)(column * 2)));
    }

    ram8(v_inventoryItemSelectionState) = 0;
    ram8(v_entitydataArrayLength) = 3;
    ram16(v_entitydataArrayPointer) = v_mapEntities;
    fill_bytes(v_mapEntities, 0x00, 0x60);
    CALL_HELPER(f_updateVdpAddressAfterDraw);

    map_bank(BANK(7));
    cpu.de = VDP_VRAM_WRITE(0x2800); /* blank sprite tile $40 */
    cpu.bc = 0x0020;
    cpu.l = 0x00;
    CALL_HELPER(f_fillVram);
    copy_to_vram(ARROW_TILE, VDP_VRAM_WRITE(0x2820), 0x0020);
    copy_to_vram(JANKENS_CASTLE_TILES, VDP_VRAM_WRITE(0x2840), 0x0140);

    map_bank(BANK(2));
    Entity *selector = entity_at(MAP_ENTITY_SLOT(1));
    selector->type = ENTITY_ITEM_SELECT_ARROW;
    selector->spriteDescriptorPointer = ARROW_SPRITE_DESCRIPTOR;
    selector->animationTimerResetValue = 0x08;
    selector->animationTimer = 0x08;
    X_PIXEL(selector) = 0x74;
    Y_PIXEL(selector) = 0x8E;
    selector->flags |= 0x01;
    cpu.ix = MAP_ENTITY_SLOT(2);
    Entity *arrow = entity_at(MAP_ENTITY_SLOT(2));
    arrow->type = ENTITY_MAP_ARROW;
    arrow->flags &= (uint8_t)~0x01;
    CALL_HELPER(f_updateEntities);
    if (ram8(v_level) >= 0x10) {
        cpu.ix = MAP_ENTITY_SLOT(3);
        Entity *castle = entity_at(MAP_ENTITY_SLOT(3));
        castle->type = ENTITY_JANKENS_CASTLE;
        X_PIXEL(castle) = 0x98;
        Y_PIXEL(castle) = 0x38;
    }

    map_bank(BANK(2));
    cpu.de = VDP_VRAM_WRITE(0x1800);
    cpu.hl = NUMBER_TILES;
    cpu.bc = 0x0050;
    cpu.a = 0x01;
    CALL_HELPER(f_load1bppTiles);
    map_bank(BANK(5));
    cpu.hl = MAP_TILES;
    cpu.de = VDP_VRAM_WRITE(0x0000);
    CALL_HELPER(f_decompressTilesToVram);
    copy_to_vram(MAGIC_CAPSULE_TILES, VDP_VRAM_WRITE(0x1980), 0x01C0);
    copy_to_vram(ALEX_STATE_TILES, VDP_VRAM_WRITE(0x1BA0), 0x01E0);
    map_bank(BANK(7));
    cpu.hl = PAUSE_SCORE_TEXT_TILES;
    cpu.de = VDP_VRAM_WRITE(0x1F80);
    CALL_HELPER(f_decompressTilesToVram);
    copy_to_vram(0x83C9, VDP_VRAM_WRITE(0x1B20), 0x0080); /* telepathy ball */
    copy_to_vram(0xA229, VDP_VRAM_WRITE(0x1D80), 0x0080); /* moonlight stone medallion */
    copy_to_vram(0x8169, VDP_VRAM_WRITE(0x1E00), 0x0080); /* letter */
    copy_to_vram(0x81E9, VDP_VRAM_WRITE(0x1E80), 0x0080); /* Hirotta stone */
    copy_to_vram(0x9349, VDP_VRAM_WRITE(0x1780), 0x0080); /* money bag */
    copy_to_vram(0xA189, VDP_VRAM_WRITE(0x1F00), 0x0080); /* sun stone medallion */
    copy_to_vram(PAUSE_MENU_PALETTE, VDP_CRAM_WRITE(0), 0x0020);

    CALL_HELPER(f_copyPauseItemsToNametable);
    copy_to_vram(v_nametable, VDP_VRAM_WRITE(0x3800), 0x0600);
    cpu.hl = PAUSE_LIVES_ICON_ENTRIES;
    cpu.de = NAMETABLE_WRITE(1, 21);
    cpu.bc = 0x0204;
    CALL_HELPER(f_copyNameTableBlockToVram);
    cpu.hl = v_lives;
    cpu.de = NAMETABLE_WRITE(4, 22);
    cpu.c = 0x01;
    CALL_HELPER(f_drawBCDDigits);
    cpu.hl = PAUSE_MONEY_BAG_ENTRIES;
    cpu.de = NAMETABLE_WRITE(1, 19);
    cpu.bc = 0x0204;
    CALL_HELPER(f_copyNameTableBlockToVram);
    /* Trailing "0" of the money, then the money. */
    vdp_set_address(NAMETABLE_WRITE(9, 20));
    vdp_write(0xC0);
    cpu.hl = v_money + 2;
    cpu.de = NAMETABLE_WRITE(3, 20);
    CALL_HELPER(f_drawThreeBcdBytes);
    ram8(v_nametableCopyFlags) = 0x08;
    cpu.hl = PAUSE_SCORE_TEXT_ENTRIES;
    cpu.de = NAMETABLE_WRITE(10, 22);
    cpu.b = 0x0C;
    CALL_HELPER(f_copyNametableEntriesToVRAM);
    cpu.hl = v_score + 2;
    cpu.de = NAMETABLE_WRITE(15, 22);
    CALL_HELPER(f_drawThreeBcdBytes);

    map_bank(BANK(2));
    uint8_t song = swimming_song(maker_foot_song(level_song(ram8(v_level))));
    song = vehicle_song(song);
    if (ram8(v_currentLevelIsBonusLevel)) song = SOUND_BASE_SONG;
    ram8(v_soundControl) = song;
    enable_interrupts();
    CALL_HELPER(f_enableDisplay);
}

/* $1FCD: main-loop handler of state $B. */
LIFTED(updateMapState, 0x1FCD) {
    enter_state_handler();
    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        open_map();
        LIFTED_RETURN();
    }
    CALL_ROUTINE(f_updateEntities);
    wait_frame(IRQ_SPRITES_AND_STATE);
    if (!ram8(v_shouldOpenMap)) LIFTED_RETURN();
    /* Pause pressed again: close the map. */
    ram8(v_shouldOpenMap) = 0;
    TAIL_CALL(f_exitMapState);
}

/* $1FE6: VBlank handler of state $B (realHandleInterruptMapState, $263D):
 * after an item was used, clears its picture (2 rows of 4 bytes) in VRAM
 * (clearNametableArea, $01C5). */
LIFTED(handleInterruptMapState, 0x1FE6) {
    if (!(ram8(v_inventoryItemSelectionState) & ITEM_SELECTION_REDRAW)) LIFTED_RETURN();
    ram8(v_inventoryItemSelectionState) &= (uint8_t)~ITEM_SELECTION_REDRAW;
    uint16_t row = ram16(v_selectedItemNametablePointer);
    for (int i = 0; i < 2; i++, row = (uint16_t)(row + NAMETABLE_ROW_BYTES)) {
        vdp_set_address(row);
        vdp_fill(0x00, 4);
    }
    LIFTED_RETURN();
}

/* $1FE9: closes the pause screen and resumes gameplay.
 * QUIRK: clearVDPTablesAndDisableScreen leaves interrupts disabled until the
 * EI near the end, and updateEntities runs in between. If Alex gets hit by
 * an enemy during that update, the death code waits for a VBlank with
 * interrupts off and the original game freezes (the port's runtime delivers
 * the interrupt anyway and prints a warning). Kept as is. */
LIFTED(exitMapState, 0x1FE9) {
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
    map_bank(BANK(2));
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
    copy_bytes(v_nametable, v_nametableCopy, 0x700);
    copy_to_vram(v_nametable, VDP_VRAM_WRITE(0x3800), 0x0700);
    CALL_ROUTINE(f_updateVdpAddressAfterDraw);
    ram16(v_entitydataArrayPointer) = v_entities;
    ram8(v_entitydataArrayLength) = ENTITY_ARRAY_SIZE;
    copy_bytes(v_levelWidth, v_temporaryLevelDataCopy, 0x2A);

    /* A bonus level uses the graphics of the next level. */
    uint8_t level = ram8(v_level);
    if (ram8(v_currentLevelIsBonusLevel)) ram8(v_level)++;
    CALL_ROUTINE(f_loadLevelPalette);
    map_bank(BANK(3));
    CALL_ROUTINE(f_loadLevelTiles);
    copy_to_vram(BOX_TILES, VDP_VRAM_WRITE(0x0020), 0x0480);
    ram8(v_level) = level;
    map_bank(BANK(5));
    cpu.hl = TEXT_CHARACTER_TILES;
    cpu.de = VDP_VRAM_WRITE(0x1600);
    CALL_ROUTINE(f_decompressTilesToVram);

    /* Tiles of the power-up in use (shots / bracelet). */
    map_bank(BANK(7));
    switch (ram8(v_alexActionState)) {
    case ACTION_MAGIC_CAPSULE_A:
        copy_to_vram(0x9CC9, VDP_VRAM_WRITE(0x2200), 0x0080);
        copy_mirrored_tiles(0x9CC9, 0x0080);
        map_bank(BANK(5));
        copy_to_vram(0xAFB1, VDP_VRAM_WRITE(0x2300), 0x0080);
        break;
    case ACTION_MAGIC_CAPSULE_B:
        copy_to_vram(0x84C9, VDP_VRAM_WRITE(0x2280), 0x00C0);
        map_bank(BANK(5));
        copy_to_vram(0xB031, VDP_VRAM_WRITE(0x2200), 0x0080);
        break;
    case ACTION_POWER_BRACELET:
        copy_to_vram(0x83A9, VDP_VRAM_WRITE(0x2200), 0x0020);
        copy_mirrored_tiles(0x83A9, 0x0020);
        break;
    default:
        break;
    }

    level = ram8(v_level);
    if (ram8(v_currentLevelIsBonusLevel)) ram8(v_level)++;
    map_bank(BANK(7));
    CALL_ROUTINE(f_loadLevelSpriteTiles);
    ram8(v_level) = level;

    map_bank(BANK(2));
    if (ram8(v_alexActionState) == ACTION_CANE_OF_FLIGHT) {
        cpu.ix = v_alex;
        CALL_ROUTINE(f__LABEL_2A6E_);
    }
    for (int slot = 1; slot <= 3; slot++) {
        cpu.ix = MAP_ENTITY_SLOT(slot);
        CALL_ROUTINE(f_destroyCurrentEntity);
    }
    cpu.ix = v_alex;
    CALL_ROUTINE(f_updateEntities);
    CALL_ROUTINE(f_updateScroll_LABEL_67C4_);
    CALL_ROUTINE(f_updateNametable_LABEL_6B49_);
    cpu.ix = v_alex;

    uint8_t song = maker_foot_song(level_song(ram8(v_level)));
    if (ram8(v_currentLevelIsBonusLevel)) song = SOUND_BASE_SONG;
    else song = swimming_song(song);
    ram8(v_soundControl) = vehicle_song(song);

    enable_interrupts();
    ram8(v_gameState) = STATE_GAMEPLAY | STATE_INITIALIZED;
    wait_frame(IRQ_SPRITES_AND_STATE);
    uint8_t action = ram8(v_alexActionState);
    if (action != ACTION_NONE && action < ACTION_MAGIC_CAPSULE_A) {
        uint8_t sound = action == ACTION_CANE_OF_FLIGHT ? SOUND_ITEM_ACTION_1 : SOUND_ITEM_ACTION_2;
        cpu.b = sound;
        ram8(v_soundControl) = sound;
    }
    CALL_ROUTINE(f_enableDisplay);
    cpu.b = 10;
    TAIL_CALL(f_sleepTenthsOfSecond);
}

/* $2439: updater of the item selection arrow (map entity 1, IX). */
LIFTED(updateItemSelectArrow, 0x2439) {
    Entity *arrow = entity_at(cpu.ix);
    cpu.b = arrow->unknown3;
    arrow->spriteDescriptorPointer = ARROW_SPRITE_DESCRIPTOR;
    cpu.b &= (uint8_t)~0x10;
    uint8_t input = ram8(v_inputData);
    cpu.c = input;
    /* Up/down do nothing (pauseStateOnUpOrDownPressed is an empty routine). */
    cpu.hl = 0x0000;
    if (input & (JOY_UP | JOY_DOWN)) CALL_ROUTINE(f_pauseStateOnUpOrDownPressed);
    arrow->ySpeed = cpu.hl;
    cpu.hl = 0x0000;
    CALL_ROUTINE(f__LABEL_24CF_);
    arrow->xSpeed = cpu.hl;
    cpu.b |= 0x02;
    arrow->unknown3 = cpu.b;

    /* No item while on a vehicle, or after an item was used. */
    if (entity_at(v_alex)->state == ALEX_STATE_0x10) LIFTED_RETURN();
    if (ram8(v_alexActionState) >= ACTION_RIDING_MOTORCYCLE) LIFTED_RETURN();
    if (ram8(v_shouldSpawnRidingBoat_RAM_C051_)) LIFTED_RETURN();
    if (ram8(v_inventoryItemSelectionState)) LIFTED_RETURN();

    /* Item number in bits 5-7 of the flags of the tile under the arrow. */
    cpu.de = 0x1404;
    CALL_ROUTINE(f_getNearEntityTileAttrWithOffset);
    uint8_t item = cpu.a & 0xE0;
    if (item == 0) LIFTED_RETURN();
    cpu.d = item;

    /* Blink (animation in the alternate bank, as the original). */
    op_exx();
    cpu.hl = ARROW_ANIMATION_DESCRIPTOR;
    CALL_ROUTINE(f_handleEntityAnimation);
    op_exx();
    if (!(ram8(v_inputData) & (JOY_BTN1 | JOY_BTN2))) LIFTED_RETURN();

    ram8(v_soundControl) = SOUND_POWERUP;
    ram8(v_inventoryItemSelectionState) = ITEM_SELECTION_REDRAW | ITEM_SELECTION_USED;
    ram8(v_invincibilityTimer) = 0;
    /* Tail-jump to the item's use routine. */
    uint8_t offset = (uint8_t)(item >> 4);
    cpu.bc = offset;
    cpu.hl = rd16((uint16_t)(ITEM_USE_ROUTINES - 2 + offset));
    cpu.a = cpu.l;
    rt_dispatch(cpu.hl);
}

/* $24B6: called when up or down is pressed on the pause screen; does nothing. */
LIFTED(pauseStateOnUpOrDownPressed, 0x24B6) {
    LIFTED_RETURN();
}

/* $24B7: unused (no reference in the ROM): vertical counterpart of
 * _LABEL_24CF_. Carry of A.rrc = move up: HL = $FE00 unless y < $88 (and
 * clears bit 3 of B); else HL = $0200 unless y >= $9C (and sets bit 4). */
LIFTED(_LABEL_24B7_, 0x24B7) {
    bool up = cpu.a & 0x01;
    op_rrca();
    uint8_t y = Y_PIXEL(entity_at(cpu.ix));
    cpu.a = y;
    if (!up) {
        alu_cp(0x9C);
        if (y >= 0x9C) LIFTED_RETURN();
        cpu.hl = 0x0200;
        cpu.b |= 0x10;
    } else {
        alu_cp(0x88);
        if (y < 0x88) LIFTED_RETURN();
        cpu.hl = 0xFE00;
        cpu.b &= (uint8_t)~0x08;
    }
    LIFTED_RETURN();
}

/* $24CF: horizontal move of the selection arrow. C = joypad, B = arrow
 * flags (bit 2 set while left/right is held). Returns HL = x speed: $FE00
 * (left, down to x = $70) or $0200 (right, up to x = $E8), else unchanged. */
LIFTED(_LABEL_24CF_, 0x24CF) {
    uint8_t direction = cpu.c & (JOY_LEFT | JOY_RIGHT);
    cpu.b &= (uint8_t)~0x04;
    if (direction == 0) LIFTED_RETURN();
    cpu.b |= 0x04;
    uint8_t x = X_PIXEL(entity_at(cpu.ix));
    if (direction & JOY_LEFT) {
        if (x >= 0x70) cpu.hl = 0xFE00;
    } else {
        if (x < 0xE8) cpu.hl = 0x0200;
    }
    LIFTED_RETURN();
}

/* $24EC: draws the pictures of the items Alex owns (v_hasMagicCapsuleA ..
 * v_hasSunstoneMedallion) into the RAM name table. The sun stone medallion
 * flag is set first, so it always appears. */
LIFTED(copyPauseItemsToNametable, 0x24EC) {
    ram8(v_hasSunstoneMedallion) |= 0x01;
    uint16_t flag = v_hasMagicCapsuleA;
    for (uint8_t left = 10; left; left--, flag++) {
        if (ram8(flag) == 0) continue;
        map_bank(BANK(6));
        uint16_t entry = rd16((uint16_t)(PAUSE_ITEMS_DRAW_ENTRIES - 2 + left * 2));
        cpu.de = rd16(entry);
        cpu.hl = rd16((uint16_t)(entry + 2));
        cpu.bc = 0x0204;
        CALL_ROUTINE(f_copyTileBlock);
    }
    LIFTED_RETURN();
}

/* $2522: copies B rows of C bytes (0 = 65536) from HL to the RAM name table
 * at DE (rows $40 bytes apart). */
LIFTED(copyTileBlock, 0x2522) {
    uint16_t src = cpu.hl, dst = cpu.de;
    uint32_t row_bytes = bc_count(cpu.c);
    uint8_t rows = cpu.b;
    do {
        copy_bytes(dst, src, row_bytes);
        src = (uint16_t)(src + row_bytes);
        dst = (uint16_t)(dst + NAMETABLE_ROW_BYTES);
    } while (--rows);
    LIFTED_RETURN();
}

/* $2532: clears B rows of C bytes (0 = 256) of the RAM name table at DE.
 * Returns HL = start of the last row, DE = start of the row after it. */
LIFTED(clearRamNametableArea, 0x2532) {
    uint16_t row = cpu.de;
    uint8_t rows = cpu.b;
    int row_bytes = cpu.c ? cpu.c : 256;
    do {
        fill_bytes(row, 0x00, (uint32_t)row_bytes);
        cpu.hl = row;
        row = (uint16_t)(row + NAMETABLE_ROW_BYTES);
    } while (--rows);
    cpu.de = row;
    cpu.b = 0;
    cpu.a = 0;
    LIFTED_RETURN();
}

/* Item use routines (table $2544), reached by a jump from
 * updateItemSelectArrow: HL = VRAM position of the item's picture, DE = same
 * position in the RAM name table, A = new v_alexActionState. */
static void use_item(uint16_t vram_picture, uint16_t ram_picture, uint8_t action) {
    cpu.hl = vram_picture;
    cpu.de = ram_picture;
    cpu.a = action;
}

/* $255A: item 1, magic capsule A. */
LIFTED(useMagicCapsuleA, 0x255A) {
    ram8(v_hasMagicCapsuleA) = 0;
    use_item(NAMETABLE_WRITE(26, 19), _RAM_CCF4_, ACTION_MAGIC_CAPSULE_A);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $2568: item 2, magic capsule B. */
LIFTED(useMagicCapsuleB, 0x2568) {
    ram8(v_hasMagicCapsuleB) = 0;
    use_item(NAMETABLE_WRITE(28, 19), _RAM_CCF8_, ACTION_MAGIC_CAPSULE_B);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $2576: item 3, telepathy ball: sets action $06; unlike the other items
 * its v_has* flag is not cleared (only the picture goes away). */
LIFTED(_LABEL_2576_, 0x2576) {
    use_item(NAMETABLE_WRITE(16, 19), _RAM_CCE0_, ACTION_TELEPATHY_BALL);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $2580: item 4, cane of flight. */
LIFTED(useCaneOfFlight, 0x2580) {
    ram8(v_hasCaneOfFlight) = 0;
    ram16(v_invincibilityTimer) = 0x05FF;
    use_item(NAMETABLE_WRITE(24, 19), _RAM_CCF0_, ACTION_CANE_OF_FLIGHT);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $2594: item 5, teleport powder (invincibility). */
LIFTED(useTeleportPowder, 0x2594) {
    ram8(v_hasTeleportPowder) = 0;
    ram16(v_invincibilityTimer) = 0x05FF;
    use_item(NAMETABLE_WRITE(22, 19), _RAM_CCEC_, ACTION_INVINCIBLE);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $25A8: item 7, power bracelet. */
LIFTED(usePowerBracelet, 0x25A8) {
    ram8(v_hasPowerBracelet) = 0;
    use_item(NAMETABLE_WRITE(14, 19), _RAM_CCDC_, ACTION_POWER_BRACELET);
    TAIL_CALL(f__LABEL_25B4_);
}

/* $25B4: common part of the item routines: sets the action, removes the
 * picture (2 rows of 4 bytes) from the RAM name table, remembers its VRAM
 * position for the interrupt handler, and removes entities 2-4 (whatever
 * Alex carried) and some of Alex's flags (unknown8 &= $F4). */
LIFTED(_LABEL_25B4_, 0x25B4) {
    ram8(v_alexActionState) = cpu.a;
    ram16(v_selectedItemNametablePointer) = cpu.hl;
    cpu.bc = 0x0204;
    CALL_ROUTINE(f_clearRamNametableArea);
    cpu.hl = ENTITY_SLOT(2);
    for (int i = 0; i < 3; i++) {
        CALL_ROUTINE(f_clearEntity);
        cpu.hl++;
    }
    entity_at(v_alex)->unknown8 &= 0xF4;
    LIFTED_RETURN();
}

/* $25D3: item 6, Hirotta stone: shows the stone's inscription full screen
 * until pause is pressed, then closes the map. */
LIFTED(_LABEL_25D3_, 0x25D3) {
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
    fill_bytes(v_nametable, 0x00, 0x700);
    copy_to_vram(HIROTTA_STONE_PALETTE, VDP_CRAM_WRITE(0), 0x0020);
    map_bank(BANK(5));
    cpu.hl = HIROTTA_STONE_NAMETABLE;
    cpu.de = NAMETABLE_WRITE(9, 2);
    cpu.bc = 0x1218;
    CALL_ROUTINE(f_copyNameTableBlockToVram);
    cpu.hl = HIROTTA_STONE_TILES;
    cpu.de = VDP_VRAM_WRITE(0x0000);
    CALL_ROUTINE(f_decompressTilesToVram);
    map_bank(BANK(2));
    destroy_entities(MAP_ENTITY_SLOT(1), 3);
    CALL_ROUTINE(f_updateEntities);
    enable_interrupts();
    CALL_ROUTINE(f_enableDisplay);
    do {
        wait_frame(IRQ_SPRITES);
        CALL_ROUTINE(f_updateEntities);
    } while (!ram8(v_shouldOpenMap));
    ram8(v_shouldOpenMap) = 0;
    TAIL_CALL(f_exitMapState);
}
