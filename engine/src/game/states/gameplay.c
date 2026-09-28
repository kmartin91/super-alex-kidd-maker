/*
 * Demo and gameplay states (game states 2, 9 and 10; $09E5-$0D07) and the
 * per-screen entity loader ($6F3D-$6FD7).
 *
 * Gameplay (state $A, also used as state 9 during janken battles): the first
 * call loads the level (tiles, palette, layout, entities, music) and spawns
 * Alex; then each frame scrolls, spawns the entities of newly reached
 * screens, updates the entities, draws the new name-table columns and waits
 * for VBlank. Pressing pause (NMI) sets v_shouldOpenMap and switches to the
 * map state ($B).
 *
 * Demo (state 2): picks the next of the 4 demo levels, sets the "demo" input
 * flag (bit 5 of v_inputFlags, which makes readInput ignore the joypad) and
 * runs the normal gameplay; its interrupt handler feeds recorded input
 * (duration, buttons) pairs from ROM and returns to the title screen when
 * they run out or when a button is pressed.
 */
#include "states.h"
#include "rt/maker.h"

#define DEMO_COUNT 5              /* demo indexes run 1..4 */
#define DEMO_LEVELS 0x0A7C        /* bank 0: level of each demo */
#define DEMO_INPUT_POINTERS 0x0A80 /* bank 5: recorded input of each demo */

/* Per-level tables of initGameplayState (entry of level 1). */
#define SHOP_DOORS_CONFIGS 0x0D70      /* 3 bytes: door x offset, door name-table pointer */
#define ENTITY_DESCRIPTORS_POINTERS 0xB505 /* bank 2: entities of each screen */
#define STARTING_POSITIONS 0x0DA3      /* 2 bytes: Alex x, y */
#define PALETTE_UPDATERS_POINTERS 0x0D2C
#define LEVEL_TILE_UPDATERS_POINTERS 0x156D
#define SCROLL_FLAGS_UPDATERS_POINTERS 0x0D0A
#define ENTITY_LOADERS_POINTERS 0x0D4E
#define LEVEL_QUESTION_MARK_BOX_INDEXES 0x0E30

/* Levels whose broken blocks are remembered (names from the reference). */
#define LEVEL_RADACTIAN_CASTLE 0x0B
#define LEVEL_CRAGG_LAKE 0x10
#define RADACTIAN_CASTLE_METATILE_DELETES 0x97DD /* bank 2 */
#define CRAGG_LAKE_METATILE_DELETES 0x9800       /* bank 2 */

#define OCTOPUS_ARMS_POINTERS 0x70FB /* bank 0: 2 arm layouts (name-table pointer + 8 x 4 bytes) */

/* Level data variables cleared when a level is (re)built: v_levelWidth.. */
#define LEVEL_DATA_SIZE 0x2B

/* ------------------------------------------------------------------ demo */

/* $09E5: main-loop handler of the demo (state 2). Note: no EXX here, the
 * handler works with the main loop's register bank. */
LIFTED(updateDemoState, 0x09E5) {
    cpu.hl = v_gameState;
    if (ram8(v_gameState) & STATE_INITIALIZED) TAIL_CALL(f_updateGameplayState);
    ram8(v_gameState) |= STATE_INITIALIZED;

    uint8_t demo = (uint8_t)(ram8(v_nextDemoIndex) + 1);
    if (demo >= DEMO_COUNT) demo = 1;
    ram8(v_nextDemoIndex) = demo;

    uint8_t level = rd8((uint16_t)(DEMO_LEVELS - 1 + demo));
    ram8(v_level) = level;
    /* The motorcycle has to be bought in the shop; the level 2 demo starts on it. */
    if (level == 2) ram8(v_alexActionState) = ACTION_RIDING_MOTORCYCLE;

    map_bank(BANK(5));
    /* The pointer is kept one byte before the next (duration, input) pair. */
    ram16(v_demoInputDataPointer) = (uint16_t)(load_ath_pointer(DEMO_INPUT_POINTERS - 2, demo) - 1);
    ram8(v_inputFlags) = (uint8_t)((ram8(v_inputFlags) & 0x03) | INPUT_FLAG_DEMO);
    /* Duration 1: the first pair is read on the first interrupt. */
    ram16(v_demoCurrentInputData) = 0x01FF;
    cpu.hl = 0x01FF;
    TAIL_CALL(f_initGameplayState);
}

/* $0A35: VBlank handler of the demo: replays the recorded joypad input, then
 * runs the gameplay interrupt handler. */
LIFTED(handleInterruptDemoState, 0x0A35) {
    map_bank(BANK(5));

    /* Any button returns to the title screen. */
    if (ram8(v_inputData) & (JOY_BTN1 | JOY_BTN2)) {
        ram8(v_gameState) = STATE_TITLE_FROM_DEMO;
        ram8(v_inputFlags) &= (uint8_t)~INPUT_FLAG_DEMO;
        LIFTED_RETURN();
    }

    uint8_t frames_left = ram8(v_demoCurrentInputData + 1);
    uint8_t input = ram8(v_demoCurrentInputData);
    if (--frames_left == 0) {
        /* Next recorded pair: (duration, joypad bits); duration 0 ends the demo. */
        uint16_t p = (uint16_t)(ram16(v_demoInputDataPointer) + 1);
        uint8_t duration = rd8(p);
        if (duration == 0) {
            ram8(v_gameState) = STATE_TITLE;
            ram8(v_inputFlags) &= (uint8_t)~INPUT_FLAG_DEMO;
            LIFTED_RETURN();
        }
        p++;
        uint8_t previous = input;
        input = rd8(p);
        frames_left = duration;
        ram16(v_demoInputDataPointer) = p;
        ram8(v_inputDataChanges) = (uint8_t)((previous ^ input) & input); /* newly pressed */
    }
    ram8(v_demoCurrentInputData) = input;
    ram8(v_demoCurrentInputData + 1) = frames_left;
    ram8(v_inputData) = input;
    cpu.b = frames_left;
    cpu.c = input;
    TAIL_CALL(f_handleInterruptGameplayState);
}

/* -------------------------------------------------------------- gameplay */

/* $0A88: main-loop handler of states 9 and 10. */
LIFTED(initOrUpdateGameplayState, 0x0A88) {
    enter_state_handler();
    if (!(ram8(v_gameState) & STATE_INITIALIZED)) TAIL_CALL(f_initGameplayState);
    TAIL_CALL(f_updateGameplayState);
}

/* $0A8E: one frame of gameplay. */
LIFTED(updateGameplayState, 0x0A8E) {
    CALL_ROUTINE(f_updateScrollFlags);
    CALL_ROUTINE(f_loadNewEntities);
    CALL_ROUTINE(f_updateEntities);
    CALL_ROUTINE(f_updateScroll_LABEL_67C4_);
    CALL_ROUTINE(f_updateNametable_LABEL_6B49_);
    wait_frame(IRQ_SPRITES_AND_STATE);

    if (ram8(v_shouldOpenMap)) {
        /* The pause button was pressed during the frame (see handlePauseInterrupt). */
        ram8(v_shouldOpenMap) = 0;
        ram8(v_gameState) = STATE_MAP;
    }
    LIFTED_RETURN();
}

/* $0AB1: VBlank handler of the gameplay (and of the demo, the map exit...). */
LIFTED(handleInterruptGameplayState, 0x0AB1) {
    CALL_ROUTINE(f_updateInvincibility);
    CALL_ROUTINE(f_handleNametableChangeRequest);
    CALL_ROUTINE(f_updateLevelTiles);
    TAIL_CALL(f_draw);
}

static void copy_to_vram(uint16_t src, uint16_t vram_dst, uint16_t count) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.bc = count;
    CALL_HELPER(f_copyBytesToVRAM);
}

/* Horizontally mirrored copy of tiles, written right after the last VRAM write. */
static void copy_mirrored_tiles(uint16_t src, uint16_t count) {
    cpu.hl = src;
    cpu.bc = count;
    CALL_HELPER(f_copyMirroredTilesToVramAtCurrentAddress);
}

/* Radactian Castle and Cragg Lake start with some blocks already removed:
 * copies the ROM list into v_metatileDeletesTable ($D900, one $100-byte page
 * per screen row, one $20-byte block per screen column). Each block's entry is
 * a count byte followed by `count` metatile positions, written to every other
 * byte; a count of 0 leaves the block empty. $FF after a row ends the list.
 * The two block pointers live in RAM (targetBase/targetBlock). */
static void load_persistent_metatile_deletes(uint16_t src, uint8_t screens_per_row) {
    uint16_t dst = v_metatileDeletesTable;
    for (;;) {
        ram16(targetBase_RAM_C07A_) = dst;
        ram16(targetBlock_RAM_C07A_) = dst;
        for (int screen = 0; screen < screens_per_row; screen++) {
            uint8_t count = rd8(src);
            if (count == 0) {
                src++;
            } else {
                int bytes = (uint8_t)(count + 1) ? (uint8_t)(count + 1) : 256;
                for (int i = 0; i < bytes; i++) {
                    wr8(dst, rd8(src++));
                    dst = (uint16_t)(dst + 2);
                }
            }
            dst = (uint16_t)(ram16(targetBlock_RAM_C07A_) + 0x20);
            ram16(targetBlock_RAM_C07A_) = dst;
        }
        if (rd8(src) == 0xFF) return;
        dst = (uint16_t)(ram16(targetBase_RAM_C07A_) + 0x100);
        ram16(targetBase_RAM_C07A_) = dst;
    }
}

/* $0ABD: builds a level from scratch (entered with STATE_INITIALIZED clear,
 * e.g. after the level intro or a lost life on the first screen). */
LIFTED(initGameplayState, 0x0ABD) {
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
    CALL_ROUTINE(f_clearEntities);
    map_bank(BANK(2));
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */

    fill_bytes(v_levelWidth, 0x00, LEVEL_DATA_SIZE);
    ram8(v_entitydataArrayLength) = ENTITY_ARRAY_SIZE;
    ram16(v_entitydataArrayPointer) = v_entities;

    CALL_ROUTINE(f_loadLevelPalette);
    CALL_ROUTINE(f_loadLevelSpriteTiles);
    copy_to_vram(0xB0A9, VDP_VRAM_WRITE(0x21A0), 0x0060); /* rice ball */
    copy_to_vram(0x9349, VDP_VRAM_WRITE(0x26C0), 0x0100); /* money bags (+1 extra tile) */

    uint8_t level = ram8(v_level);
    uint8_t spawn = rd8(level_entry(LEVEL_SPAWN_STATES, level, 1));
    if (spawn != 0) {
        if (spawn == 1) ram8(v_shouldSpawnRidingBoat_RAM_C051_) = 1;
        /* Maker levels (rt/maker.h) may also start on the motorbike (7), which
         * the game only gives in shops. */
        else if (maker.active && spawn == ACTION_RIDING_MOTORCYCLE) ram8(v_alexActionState) = ACTION_RIDING_MOTORCYCLE;
        else ram8(v_alexActionState) = ACTION_FLYING_PETICOPTER;
        /* Vehicle bullet tiles. */
        copy_to_vram(0x9B29, VDP_VRAM_WRITE(0x2200), 0x0020);
        copy_to_vram(0x9429, VDP_VRAM_WRITE(0x2220), 0x01C0);
    }

    map_bank(BANK(5));
    cpu.de = VDP_VRAM_WRITE(0x1600);
    cpu.hl = 0xB2B1; /* 4bpp text characters */
    CALL_ROUTINE(f_decompressTilesToVram);
    map_bank(BANK(3));
    copy_to_vram(0x8000, VDP_VRAM_WRITE(0x0020), 0x0480); /* rock / question / star boxes */
    CALL_ROUTINE(f_loadLevelTiles);
    CALL_ROUTINE(f_loadLevel);

    level = ram8(v_level);
    uint16_t door = level_entry(SHOP_DOORS_CONFIGS, level, 3);
    ram8(v_shopDoorOffset) = rd8(door);
    ram16(v_shopDoorNametablePointer) = rd16((uint16_t)(door + 1));

    map_bank(BANK(2));
    ram16(v_entityDescriptorsPointer) = load_ath_pointer(ENTITY_DESCRIPTORS_POINTERS - 2, ram8(v_level));
    ram8(_RAM_C08E_) = 0;
    ram8(v_hasBattleStarted) = 0;
    fill_bytes(v_metatileDeletesTable, 0x00, 0x600);

    level = ram8(v_level);
    if (level == LEVEL_RADACTIAN_CASTLE) {
        ram8(_RAM_C08E_) = 1; /* broken blocks are remembered in this level */
        load_persistent_metatile_deletes(RADACTIAN_CASTLE_METATILE_DELETES, 5);
    } else if (level == LEVEL_CRAGG_LAKE) {
        ram8(_RAM_C08E_) = 1;
        load_persistent_metatile_deletes(CRAGG_LAKE_METATILE_DELETES, 7);
    }

    /* initGameplayStateSecondary ($0C43): spawn Alex at the level start. */
    cpu.ix = v_alex;
    Entity *alex = entity_at(v_alex);
    alex->type = ENTITY_ALEX;
    level = ram8(v_level);
    uint16_t start = level_entry(STARTING_POSITIONS, level, 2);
    X_PIXEL(alex) = rd8(start);
    Y_PIXEL(alex) = rd8((uint16_t)(start + 1));
    cpu.bc = (uint8_t)(level * 2); /* C is an input of updateEntities */
    CALL_ROUTINE(f_updateAlexSpawning);
    CALL_ROUTINE(f_updateEntities);

    ram16(paletteUpdaterPointer) = load_ath_pointer(PALETTE_UPDATERS_POINTERS - 2, ram8(v_level));
    ram16(v_levelTileUpdaterPointer) = rd16(level_entry(LEVEL_TILE_UPDATERS_POINTERS, ram8(v_level), 2));
    ram8(v_levelTileUpdateTimer) = 1;
    ram16(scrollFlagsUpdaterPointer) = load_ath_pointer(SCROLL_FLAGS_UPDATERS_POINTERS - 2, ram8(v_level));
    ram16(v_entityLoaderPointer) = load_ath_pointer(ENTITY_LOADERS_POINTERS - 2, ram8(v_level));
    ram8(v_questionMarkBoxIndex) = rd8(level_entry(LEVEL_QUESTION_MARK_BOX_INDEXES, ram8(v_level), 1));

    map_bank(BANK(7));
    copy_to_vram(0xAFC9, VDP_VRAM_WRITE(0x2400), 0x00E0); /* ghost facing left */
    copy_mirrored_tiles(0xAFC9, 0x00E0);                  /* ghost facing right */
    map_bank(BANK(5));
    copy_to_vram(0xB191, VDP_VRAM_WRITE(0x25C0), 0x0080); /* 1up */
    copy_to_vram(0xB0B1, VDP_VRAM_WRITE(0x2640), 0x0060); /* power bracelet */
    copy_mirrored_tiles(0xB0F1, 0x0020);

    map_bank(BANK(2));
    ram8(v_soundControl) = level_song(ram8(v_level));
    /* Maker levels keep the song for on foot in their table: one that starts
     * on the motorbike or the Peticopter plays its song, as Alex's spawn
     * asked (the request above replaced it). */
    if (maker.active) {
        uint8_t action = ram8(v_alexActionState);
        if (action == ACTION_RIDING_MOTORCYCLE) ram8(v_soundControl) = SOUND_BIKE_SONG;
        else if (action == ACTION_FLYING_PETICOPTER) ram8(v_soundControl) = SOUND_PETICOPTER_SONG;
    }
    ram8(v_gameState) |= STATE_INITIALIZED;
    cpu.hl = v_gameState;
    enable_interrupts();
    TAIL_CALL(f_enableDisplay);
}

/* ------------------------------------------------------ entity loading */

/* $6F3D: runs the level's entity loader (v_entityLoaderPointer: loadEntitiesNormal
 * or, in the castle, loadEntitiesSpecial). */
LIFTED(loadNewEntities, 0x6F3D) {
    cpu.hl = ram16(v_entityLoaderPointer);
    rt_dispatch(cpu.hl);
}

/* $6F41: when Alex reaches a new screen (bit 7 of v_currentScreenNumber),
 * spawns the entities listed for it. The new entities are flagged off-screen
 * on the side the screen scrolls in from. */
LIFTED(loadEntitiesNormal_LABEL_6F48_, 0x6F41) {
    uint8_t screen = ram8(v_currentScreenNumber);
    if (!(screen & 0x80)) LIFTED_RETURN();
    screen &= 0x7F;
    ram8(v_currentScreenNumber) = screen;

    cpu.hl = load_ath_pointer(ram16(v_entityDescriptorsPointer), screen);
    ram8(v_newEntityVerticalOffset) = 0;

    /* (vertical << 8) | horizontal off-screen flags of the new entities. */
    uint16_t offscreen;
    uint8_t scroll = ram8(v_scrollFlags);
    if (maker.active && maker.start_screen_pending) {
        /* Maker mode: the start screen, already on view. */
        maker.start_screen_pending = false;
        offscreen = 0x0000;
        if (maker.after_start_screen) {
            ram8(v_currentScreenNumber) = maker.after_start_screen;
            maker.after_start_screen = 0;
        }
    } else if (maker.active && maker.camera_both_ways) {
        /* Maker mode: the scroll flags allow both ways; the screen came in
         * from where the camera really went. */
        offscreen = maker.entered_from_left ? 0x0001 : 0x00FF;
    } else if (ram8(v_isScrollingDownToNextScreen) || (scroll & SCROLL_DOWN)) offscreen = 0x0100;
    else if (scroll & SCROLL_UP) offscreen = 0xFF00;
    else if (scroll & SCROLL_LEFT) offscreen = 0x0001;
    else offscreen = 0x00FF;
    cpu.de = offscreen;
    TAIL_CALL(f__LABEL_6F7E_);
}

/* $6F7E: spawns the entities of a screen descriptor. HL = descriptor, DE =
 * off-screen flags for the new entities. A descriptor is a list of groups:
 *   n (1..$7F), n entity records (type, y, x, data) -> into free slots 7-16
 *   $81 + record -> slot 29 (or 30 if 29 is taken)
 *   $82 + index  -> the 8 arms of the octopus (slots 7-14)
 *   $84 + record -> slot 6 (always-present entity: boss, janken opponent...)
 *   other >= $80, n, n bytes -> copied to $D8A0
 *   0 -> end. */
LIFTED(_LABEL_6F7E_, 0x6F77) {
    ram8(v_isScrollingDownToNextScreen) = 0;
    ram16(v_addedEntitiesShouldBeOffscreenHorizontally) = cpu.de;

    for (;;) {
        uint8_t group = rd8(cpu.hl);
        if (group == 0) LIFTED_RETURN();
        if (!(group & 0x80)) {
            /* Ordinary entities: _LABEL_6F8F_ finds free slots for them. */
            cpu.b = group;
            TAIL_CALL(f__LABEL_6F8F_);
        }
        if (group & 0x01) {
            /* Entity in slot 29, or 30 when 29 is in use. */
            uint16_t slot = ENTITY_SLOT(29);
            if (ram8(slot) != 0) slot = ENTITY_SLOT(30);
            cpu.ix = slot;
            entity_at(slot)->flags &= (uint8_t)~0x03;
            cpu.b = 1;
            CALL_ROUTINE(f__LABEL_6FA6_);
            cpu.hl++;
        } else if (group & 0x02) {
            /* Octopus arms; the original does this in the alternate register
             * bank, so BC', DE' and HL' are left as below. */
            uint16_t p = (uint16_t)(cpu.hl + 1);
            uint8_t offset = (uint8_t)(rd8(p) * 2);
            uint16_t arms = rd16((uint16_t)(OCTOPUS_ARMS_POINTERS - 2 + offset)); /* 1-based */
            ram16(v_shopDoorNametablePointer) = rd16(arms); /* octopus name-table position */
            arms = (uint16_t)(arms + 2);
            uint16_t slot = ENTITY_SLOT(7);
            for (int i = 0; i < 8; i++, slot += ENTITY_SIZE) {
                Entity *arm = entity_at(slot);
                arm->unknown5 = rd8(arms++);
                arm->unknown10 = rd8(arms++);
                arm->unknown11 = rd8(arms++);
                arm->unknown4 = rd8(arms++);
                arm->type = 0x24; /* octopus arm */
                arm->isOffScreenFlags = (uint16_t)(ram8(v_addedEntitiesShouldBeOffscreenHorizontally) |
                                                   (ram8(v_addedEntitiesShouldBeOffscreenVertically) << 8));
                arm->unknown6 = 0;
                arm->battleDecision = 0;
                arm->flags &= (uint8_t)~0x03;
            }
            cpu.ix = slot;
            cpu.bc_ = offset; /* B' = 0 after the DJNZ, C' = 2 * index */
            cpu.de_ = ENTITY_SIZE;
            cpu.hl_ = arms;
            cpu.hl = (uint16_t)(p + 1);
        } else if (group & 0x04) {
            /* Always-present entity in slot 6. */
            cpu.ix = ENTITY_SLOT(6);
            entity_at(ENTITY_SLOT(6))->flags &= (uint8_t)~0x03;
            cpu.b = 1;
            CALL_ROUTINE(f__LABEL_6FA6_);
            cpu.hl++;
        } else {
            /* Raw bytes for $D8A0. */
            uint8_t count = rd8((uint16_t)(cpu.hl + 1));
            uint16_t src = (uint16_t)(cpu.hl + 2);
            uint32_t n = bc_count(count);
            copy_bytes(v_unknownEntityByteCount_RAM_D8A0_, src, n);
            cpu.hl = (uint16_t)(src + n);
            cpu.de = (uint16_t)(v_unknownEntityByteCount_RAM_D8A0_ + n);
            cpu.bc = 0;
        }
    }
}
