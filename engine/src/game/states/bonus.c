/*
 * Bonus levels, the rooms of the last level and the ending (game state 8,
 * $1650-$18CC).
 *
 * State 8 is requested by entity $4C when Alex touches it. Its first call
 * builds a new playfield and then hands over to the normal gameplay ($8A):
 *  - levels 1-16: the bonus level, a horizontal level drawn with the tiles,
 *    palette and sprites of the next level (v_currentLevelIsBonusLevel = 1);
 *  - level 17 (the last level): a room with a character (entity $60, or $61
 *    in the second room). _RAM_C07F_ (set by entity $4C from its data byte)
 *    selects the room variant;
 *  - level 17 once its final event happened (_RAM_D800_ != 0): the ending. The
 *    screen scrolls up at $0039 lines per frame while the interrupt handler
 *    writes the ending text (v_endingSequencePointer, bank 3) into each name
 *    table row that scrolls in. At the end marker it clears the pointer's
 *    high byte (_RAM_C095_); the main loop then waits $BD frames, empties the
 *    wallet, leaves 1 life and enters the life-lost state, which shows GAME
 *    OVER.
 * _RAM_D800_..D807 are per-level event flags cleared when a level is
 * completed; D800 is set by entity $51 (the level's final event).
 */
#include "states.h"
#include "rt/maker.h"

#define LEVEL_LAST 0x11
#define ENDING_TEXT 0xB96A           /* bank 3 */
#define ENDING_SCROLL_SPEED 0x0039   /* 8.8 lines per frame */
#define ENDING_FINAL_WAIT 0xBD       /* frames after the last line */
#define BONUS_LEVEL_LAYOUT 0x8AD6    /* bank 5, shared by all bonus levels */
#define LAST_LEVEL_ROOM_LAYOUT 0xBC53 /* bank 5 */

/* Ending text commands (first byte of each entry). */
#define ENDING_EMPTY_LINE 0x00
#define ENDING_END 0xFF
#define ENDING_CLEAR_LINE 0xFE
/* Any other value n: n characters, then the start column (byte offset), then text. */

/* $1650: main-loop handler of state 8. */
LIFTED(updateBonusLevelState, 0x1650) {
    enter_state_handler();

    if (!(ram8(v_gameState) & STATE_INITIALIZED) && maker.active && maker.zone.request) {
        maker_zone_transition(); /* Maker levels: into or out of the bonus zone */
        LIFTED_RETURN();
    }
    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        /* _LABEL_1735_: build the bonus level / castle room / ending. */
        CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
        cpu.b = 5;
        CALL_ROUTINE(f_sleepTenthsOfSecond);
        CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
        fill_bytes(v_levelWidth, 0x00, 0x2B);
        ram8(v_newEntityHorizontalOffset) = 0;

        uint16_t layout;
        uint8_t screens, width;
        if (ram8(v_level) != LEVEL_LAST) {
            /* Bonus level: graphics of the next level. */
            ram8(v_currentLevelIsBonusLevel) = 1;
            map_bank(BANK(3));
            uint8_t level = ram8(v_level);
            ram8(v_level) = (uint8_t)(level + 1);
            CALL_ROUTINE(f_loadLevelTiles);
            CALL_ROUTINE(f_loadLevelPalette);
            map_bank(BANK(7));
            CALL_ROUTINE(f_loadLevelSpriteTiles);
            ram8(v_level) = level;
            cpu.a = level;
            layout = BONUS_LEVEL_LAYOUT;
            screens = 0x06;
            width = 0x07;
        } else if (ram8(_RAM_D800_) == 0) {
            /* A room of the last level. */
            layout = LAST_LEVEL_ROOM_LAYOUT;
            screens = ram8(_RAM_C07F_) ? 0x04 : 0x03;
            width = 0x00;
        } else {
            /* _LABEL_189A_: the ending. */
            vdp_set_address(VDP_CRAM_WRITE(0x00));
            vdp_write(0x00);
            vdp_set_address(VDP_CRAM_WRITE(0x10));
            vdp_write(0x00);
            CALL_ROUTINE(f_clearEntities);
            CALL_ROUTINE(f_updateEntities);
            map_bank(BANK(3));
            ram16(v_endingSequencePointer) = ENDING_TEXT;
            ram16(v_verticalScrollSpeed) = ENDING_SCROLL_SPEED;
            ram8(v_soundControl) = SOUND_ENDING_SONG;
            ram8(v_gameState) |= STATE_INITIALIZED;
            cpu.hl = v_gameState;
            enable_interrupts();
            TAIL_CALL(f_enableDisplay);
        }

        map_bank(BANK(5));
        ram16(v_levelLayoutPointer) = layout;
        ram16(v_SecondLevelLayoutPointer) = layout;
        ram8(v_horizontalScreenNumber) = screens;
        ram8(v_verticalScreenNumber) = 0;
        ram8(v_levelWidth) = width;
        ram8(v_levelHeight) = 0;
        ram8(v_levelScrollFlags) = SCROLL_RIGHT;
        ram16(v_levelData_C0B7_) = VDP_VRAM_WRITE(0x3800);
        ram16(v_levelData_C0C5_) = VDP_VRAM_WRITE(0x3800);
        cpu.b = screens;
        cpu.c = width;

        /* Scroll through the whole level once to draw its name table. */
        do {
            ram16(v_horizontalScrollSpeed) = 0x0100;
            CALL_ROUTINE(f_updateScroll_LABEL_67C4_);
            CALL_ROUTINE(f_updateNametable_LABEL_6B49_);
            CALL_ROUTINE(f_draw);
        } while (ram16(v_horizontalScroll) != 0);

        ram8(v_currentScreenNumber) = 0x88;
        ram16(v_horizontalScrollSpeed) = 0;
        ram8(v_scrollFlags) = ram8(v_levelScrollFlags);
        uint8_t count = ram8(v_entitydataArrayLength);
        destroy_entities(ENTITY_SLOT(1), count);

        map_bank(BANK(2));
        cpu.ix = v_alex;
        Entity *alex = entity_at(v_alex);
        alex->type = ENTITY_ALEX;
        X_PIXEL(alex) = 0x10;
        Y_PIXEL(alex) = 0x88;
        if (ram8(v_level) == LEVEL_LAST) {
            ram8(v_scrollFlags) = 0;
            if (!ram8(_RAM_C07F_)) {
                /* First room: Alex enters from below; entity $4C (the exit
                 * trigger) waits on the right. */
                cpu.c = 0x4C;
                cpu.de = 0x88F0;
                cpu.b = 0x01;
                X_PIXEL(alex) = 0x70;
                Y_PIXEL(alex) = 0xA0;
                ram8(v_alex + offsetof(Entity, isOffScreenFlags) + 1) = 0xFF;
                Entity *exit = entity_at(ENTITY_SLOT(6));
                exit->type = 0x4C;
                X_PIXEL(exit) = 0xF0;
                Y_PIXEL(exit) = 0x88;
                exit->data = 0x01;
            }
            /* The room's character: entity $60 (or $61 in the second room). */
            uint8_t type = 0x60;
            uint16_t position = 0x98C0; /* y << 8 | x */
            if (ram8(_RAM_C07F_)) {
                type = 0x61;
                position = 0x9008;
            }
            cpu.c = type;
            cpu.de = position;
            Entity *character = entity_at(ENTITY_SLOT(7));
            character->type = type;
            X_PIXEL(character) = (uint8_t)position;
            Y_PIXEL(character) = (uint8_t)(position >> 8);
        }

        /* _LABEL_1874_ */
        cpu.ix = v_alex;
        CALL_ROUTINE(f_updateAlexSpawning);
        CALL_ROUTINE(f_updateEntities);
        /* VDP register 0 = $26 (the value in v_VDPRegister0Value is not updated). */
        vdp_set_address(VDP_REGISTER(0, 0x26));
        ram8(v_soundControl) = level_song((uint8_t)(ram8(v_level) + 1));
        ram8(v_gameState) |= STATE_INITIALIZED;
        cpu.hl = v_gameState;
        enable_interrupts();
        TAIL_CALL(f_enableDisplay);
    }

    wait_frame(IRQ_SPRITES_AND_STATE);
    if (ram8(_RAM_D800_) == 0) {
        /* Bonus level or castle room built: play it. */
        ram8(v_gameState) = STATE_GAMEPLAY | STATE_INITIALIZED;
        cpu.b = 10;
        TAIL_CALL(f_sleepTenthsOfSecond);
    }

    /* Ending: wait until the interrupt handler reaches the end of the text. */
    if (ram8(_RAM_C095_) != 0) LIFTED_RETURN();
    ram16(v_verticalScrollSpeed) = 0;
    ram8(_RAM_C014_) = ENDING_FINAL_WAIT;
    do {
        wait_frame(IRQ_SPRITES);
    } while (--ram8(_RAM_C014_) != 0);

    map_bank(BANK(2));
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
    fill_bytes(v_money, 0x00, 3);
    cpu.hl = v_money + 2; /* registers as left by the two LDI */
    cpu.de = v_money + 3;
    cpu.bc = (uint16_t)(cpu.bc - 2);
    ram8(v_lives) = 1;
    ram8(v_gameState) = STATE_LIFE_LOST; /* 1 life - 1 = GAME OVER */
    LIFTED_RETURN();
}

/* $16A6: VBlank handler of state 8: only does something during the ending,
 * where it scrolls the screen up and writes each new line of text. */
LIFTED(handleInterruptBonusLevelState, 0x16A6) {
    if (ram8(_RAM_D800_) == 0) LIFTED_RETURN();
    CALL_ROUTINE(f_updateVdpAddressAfterDraw);

    /* The name table is 224 lines high: skip lines 224-255. */
    uint8_t previous_line = (uint8_t)(ram16(v_verticalScroll) >> 8);
    uint16_t scroll = (uint16_t)(ram16(v_verticalScrollSpeed) + ram16(v_verticalScroll));
    uint8_t line = (uint8_t)(scroll >> 8);
    if (line >= 0xE0) line += 0x20;
    scroll = (uint16_t)((line << 8) | (scroll & 0xFF));
    ram16(v_verticalScroll) = scroll;
    if (line == previous_line || (line & 0x07)) LIFTED_RETURN();

    /* A new tile row became visible at the bottom: row (line + 192) mod 224. */
    uint8_t bottom = (uint8_t)(line + 0xC0);
    if (line + 0xC0 > 0xFF || bottom >= 0xE0) bottom += 0x20;
    uint16_t row = (uint16_t)(VDP_VRAM_WRITE(0x3800) + bottom * 8);

    map_bank(BANK(3));
    uint16_t text = ram16(v_endingSequencePointer);
    uint8_t command = rd8(text++);
    if (command == ENDING_END) {
        ram8(_RAM_C095_) = 0; /* high byte of the pointer: "text finished" */
        LIFTED_RETURN();
    }
    if (command == ENDING_CLEAR_LINE) {
        cpu.l = 0x00;
        cpu.de = row;
        cpu.bc = 0x0040;
        if ((uint8_t)((row >> 8) + row) >= 0xFE) {
            /* Clear split between this row and the top of the name table. */
            cpu.bc = 0x0020;
            CALL_ROUTINE(f_fillVram);
            cpu.bc = 0x0020;
            cpu.de = VDP_VRAM_WRITE(0x3800);
        }
        CALL_ROUTINE(f_fillVram);
    } else if (command != ENDING_EMPTY_LINE) {
        /* Clear the row, then write `command` characters from the column byte. */
        cpu.de = row;
        cpu.l = 0x00;
        cpu.bc = 0x0040;
        CALL_ROUTINE(f_fillVram);
        cpu.de = (uint16_t)(row | rd8(text++));
        cpu.hl = text;
        cpu.b = command;
        ram8(v_nametableCopyFlags) = 0;
        CALL_ROUTINE(f_copyNametableEntriesToVRAM);
        text = cpu.hl;
    }
    ram16(v_endingSequencePointer) = text;
    LIFTED_RETURN();
}
