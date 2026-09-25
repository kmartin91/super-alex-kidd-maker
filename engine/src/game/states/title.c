/*
 * Title screen (game states 0 and 1, $076D-$09D8).
 *
 * Flow: the first frame draws the logo, loads the sprite tiles used by the
 * animated vignettes and starts the intro music. Then every frame waits for
 * VBlank and updates the entities. Button 1 or 2 starts a new game; after
 * $01D0 frames without input the demo state takes over.
 *
 * The interrupt handler drives the animation: every 32 frames it reveals
 * the next vignette (Alex swimming, riding the boat, the tree, the
 * peticopter, the janken flight, then "PUSH START BUTTON"); once all six are
 * shown it cycles the logo colour every 3 frames.
 */
#include "states.h"

#define TITLE_SCREEN_DURATION 0x3C      /* frames before the first vignette */
#define TITLE_SCREEN_TILE_DURATION 0x20 /* frames between vignettes */
#define TITLE_LOGO_COLOR_DURATION 0x03  /* frames between logo colours */
#define TITLE_VIGNETTE_COUNT 6
#define TITLE_INTRO_FRAMES 0x01D0 /* frames before the demo starts */

/* ROM data (bank 4 in slot 2 unless noted). */
#define TITLE_SCREEN_TILES 0xB332
#define LOGO_TOP_NAMETABLE 0xAD9E
#define LOGO_BOTTOM_NAMETABLE 0xAE46
#define TITLE_SCREEN_PALETTE 0x08C6          /* bank 0 */
#define INITIAL_GAME_VALUES 0x0824           /* bank 0: 25 bytes copied to v_gameState.. */
#define TITLE_FRAME_UPDATERS 0x08E6          /* bank 0: 6 routine pointers */
#define TITLE_LOGO_ANIMATION_COLORS 0x08F2   /* bank 0: 4 colours */
#define JANKEN_TILES 0xA357                  /* bank 4 */

/* Sprite descriptors of bank 2 (the byte after their first one is copied). */
#define ALEX_PETICOPTER_DESCRIPTOR 0x8F7B
#define ALEX_BOAT_DESCRIPTOR 0x9152
#define ALEX_AIR_RIGHT_DESCRIPTOR 0x8F15
#define ALEX_SWIMMING_DESCRIPTOR 0x8E01
#define JANKEN_FLYING_DESCRIPTOR 0x961A
#define TITLE_DESCRIPTOR_COPY_SIZE 0x24

/* The title screen keeps its patched sprite descriptors in RAM that is later
 * used as the RAM copy of the name table. */
#define TITLE_PETICOPTER_SPRITES v_nametable /* $C800 */
#define TITLE_BOAT_SPRITES _RAM_C828_
#define TITLE_JUMPING_SPRITES _RAM_C850_
#define TITLE_SWIMMING_SPRITES _RAM_C878_

/* Places a static (non-moving) sprite entity on the title screen. */
static void spawn_title_sprite(uint16_t slot, uint16_t descriptor, uint8_t x, uint8_t y) {
    Entity *e = entity_at(slot);
    e->type = ENTITY_STATIC;
    e->spriteDescriptorPointer = descriptor;
    X_PIXEL(e) = x;
    Y_PIXEL(e) = y;
}

/* Draws a rectangle of name-table entries (rows x bytes per row) from ROM. */
static void draw_block(uint16_t src, uint16_t vram_dst, uint8_t rows, uint8_t row_bytes) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.b = rows;
    cpu.c = row_bytes;
    CALL_HELPER(f_copyNameTableBlockToVram);
}

/* $076D: main-loop handler of the title screen (states 0 and 1). */
LIFTED(initOrUpdateTitleScreenState, 0x076D) {
    enter_state_handler();

    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        ram8(v_gameState) |= STATE_INITIALIZED;
        ram8(v_nametableCopyFlags) = 0;
        CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);

        /* Blank the first sprite tile. */
        cpu.de = VDP_VRAM_WRITE(0x2000);
        cpu.bc = 0x0020;
        cpu.l = 0x00;
        CALL_ROUTINE(f_fillVram);

        map_bank(BANK(2));
        CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */
        CALL_ROUTINE(f_updateHighScore);

        /* Wipe all game variables from v_score up to $DDFF. */
        fill_bytes(v_score, 0x00, 0x1DE0);

        ram8(v_titleScreenTimer) = TITLE_SCREEN_DURATION;
        ram8(v_currentTitleScreen) = 0;
        ram8(v_titleScreenLogoTimer) = 0;

        map_bank(BANK(4));
        cpu.hl = TITLE_SCREEN_TILES;
        cpu.de = VDP_VRAM_WRITE(0x0020);
        CALL_ROUTINE(f_decompressTilesToVram);
        draw_block(LOGO_TOP_NAMETABLE, VDP_VRAM_WRITE(0x388E), 0x06, 0x1C);
        draw_block(LOGO_BOTTOM_NAMETABLE, VDP_VRAM_WRITE(0x39DA), 0x07, 0x1A);

        /* 32-byte palette (the reference splits it into titleScreenPalette
         * and _DATA_8CA_). */
        cpu.hl = TITLE_SCREEN_PALETTE;
        cpu.de = VDP_CRAM_WRITE(0);
        cpu.b = 0x20;
        CALL_ROUTINE(f_memcpyToVRAM);

        CALL_ROUTINE(f_loadTitleSprites);
        CALL_ROUTINE(f_enableDisplay);
        enable_interrupts();
        ram16(v_introTimer) = TITLE_INTRO_FRAMES;
        ram8(v_soundControl) = SOUND_INTRO;
    }

    /* updateTitleScreenState ($07EC) */
    wait_frame(IRQ_SPRITES_AND_STATE);
    CALL_ROUTINE(f_updateEntities);

    uint8_t input = ram8(v_inputData);
    cpu.b = input; /* left in B for the next handler */
    if (input & (JOY_BTN1 | JOY_BTN2)) TAIL_CALL(f_startGame);

    uint16_t timer = (uint16_t)(ram16(v_introTimer) - 1);
    ram16(v_introTimer) = timer;
    cpu.hl = timer;
    if (timer == 0) ram8(v_gameState) = STATE_DEMO;
    LIFTED_RETURN();
}

/* $080C: a button was pressed on the title screen: reset the game variables
 * (state = level starting, level 1, 3 lives, ...) from ROM and start. */
LIFTED(startGame, 0x080C) {
    /* VDP register 0: mode 4, change height, line interrupts enabled. */
    ram8(v_VDPRegister0Value) = 0x26;
    copy_bytes(v_gameState, INITIAL_GAME_VALUES, 0x19);
    ram8(v_nametableCopyFlags) = 0;
    ram8(v_inputFlags) = 0;
    /* Registers as left by LDIR. */
    cpu.hl = INITIAL_GAME_VALUES + 0x19;
    cpu.de = v_gameState + 0x19;
    cpu.bc = 0;
    cpu.a = 0;
    LIFTED_RETURN();
}

/* $0842: VBlank handler of the title screen: vignettes, then logo colours. */
LIFTED(handleInterruptTitleScreenState, 0x0842) {
    if (--ram8(v_titleScreenTimer) != 0) LIFTED_RETURN();
    ram8(v_titleScreenTimer) = TITLE_SCREEN_TILE_DURATION;

    uint8_t vignette = ram8(v_currentTitleScreen);
    if (vignette < TITLE_VIGNETTE_COUNT) {
        /* advanceTitleScreenLevelTile: tail-jump to showTitle*Frame. */
        ram8(v_currentTitleScreen) = (uint8_t)(vignette + 1);
        map_bank(BANK(4));
        rt_dispatch(rd16((uint16_t)(TITLE_FRAME_UPDATERS + vignette * 2)));
        return;
    }

    /* All vignettes shown: cycle the logo colour (CRAM entry 2). */
    ram8(v_titleScreenTimer) = TITLE_LOGO_COLOR_DURATION;
    uint8_t step = ++ram8(v_titleScreenLogoTimer);
    vdp_set_address(VDP_CRAM_WRITE(2));
    vdp_write(rd8((uint16_t)(TITLE_LOGO_ANIMATION_COLORS + (step & 0x03))));
    LIFTED_RETURN();
}

/* $0872: vignette 1, Alex swimming under water. */
LIFTED(showTitleUnderwaterFrame, 0x0872) {
    draw_block(0xAEFC, VDP_VRAM_WRITE(0x3828), 0x07, 0x18);
    spawn_title_sprite(ENTITY_SLOT(4), TITLE_SWIMMING_SPRITES, 0xC9, 0x0C);
    LIFTED_RETURN();
}

/* $0881: vignette 2, Alex riding the boat. */
LIFTED(showTitleBoatFrame, 0x0881) {
    draw_block(0xB0A4, VDP_VRAM_WRITE(0x3B98), 0x06, 0x1C);
    spawn_title_sprite(ENTITY_SLOT(2), TITLE_BOAT_SPRITES, 0x70, 0x7C);
    LIFTED_RETURN();
}

/* $0890: vignette 3, the tree in the top-left corner (no sprite). */
LIFTED(showTitleTreeFrame, 0x0890) {
    draw_block(0xAFA4, VDP_VRAM_WRITE(0x3800), 0x08, 0x0E);
    LIFTED_RETURN();
}

/* $089C: vignette 4, Alex flying the peticopter. */
LIFTED(showTitlePeticopterFrame, 0x089C) {
    draw_block(0xB014, VDP_VRAM_WRITE(0x39F4), 0x0C, 0x0C);
    spawn_title_sprite(ENTITY_SLOT(1), TITLE_PETICOPTER_SPRITES, 0xDC, 0x46);
    LIFTED_RETURN();
}

/* $08AB: vignette 5, Alex jumping and Janken flying. */
LIFTED(showTitleJankenFrame, 0x08AB) {
    draw_block(0xB1B2, VDP_VRAM_WRITE(0x3A00), 0x10, 0x18);
    spawn_title_sprite(ENTITY_SLOT(3), TITLE_JUMPING_SPRITES, 0x18, 0x4F);
    spawn_title_sprite(ENTITY_SLOT(7), JANKEN_FLYING_DESCRIPTOR, 0x30, 0x77);
    LIFTED_RETURN();
}

/* $08BA: vignette 6, "PUSH START BUTTON". */
LIFTED(showTitlePushStartFrame, 0x08BA) {
    draw_block(0xB14C, VDP_VRAM_WRITE(0x3D1A), 0x03, 0x22);
    LIFTED_RETURN();
}

/* $08F6: loads the sprite tiles of the title vignettes (Alex on the
 * peticopter, on the boat, jumping, swimming, and Janken) and builds RAM
 * copies of their sprite descriptors with the tile numbers shifted to where
 * the tiles were loaded. Returns C = last tile offset (from sub_0951). */
LIFTED(loadTitleSprites, 0x08F6) {
    ram8(v_entitydataArrayLength) = ENTITY_ARRAY_SIZE;
    ram16(v_entitydataArrayPointer) = v_entities;
    CALL_ROUTINE(f_clearEntities);

    cpu.a = 0x1D; /* peticopter */
    CALL_ROUTINE(f_loadAlexTilesToVRAM2000);
    cpu.bc = 0x0036; /* boat */
    CALL_ROUTINE(f_loadAlexTilesToVRAM);
    cpu.bc = 0x002C; /* jumping */
    CALL_ROUTINE(f_loadAlexTilesToVRAM);
    cpu.bc = 0x0014; /* swimming */
    CALL_ROUTINE(f_loadAlexTilesToVRAM);
    cpu.hl = JANKEN_TILES;
    cpu.de = VDP_VRAM_WRITE(0x2400);
    CALL_ROUTINE(f_decompressTilesToVram);

    map_bank(BANK(2));
    static const struct { uint16_t descriptor, ram_copy; uint8_t tile_offset; } copies[] = {
        {ALEX_PETICOPTER_DESCRIPTOR + 1, TITLE_PETICOPTER_SPRITES, 0x00},
        {ALEX_BOAT_DESCRIPTOR + 1, TITLE_BOAT_SPRITES, 0x0B},
        {ALEX_AIR_RIGHT_DESCRIPTOR + 1, TITLE_JUMPING_SPRITES, 0x13},
        {ALEX_SWIMMING_DESCRIPTOR + 1, TITLE_SWIMMING_SPRITES, 0x19},
    };
    for (size_t i = 0; i < sizeof(copies) / sizeof(copies[0]); i++) {
        cpu.hl = copies[i].descriptor;
        cpu.de = copies[i].ram_copy;
        cpu.a = copies[i].tile_offset;
        if (i + 1 == sizeof(copies) / sizeof(copies[0])) TAIL_CALL(f_sub_0951);
        CALL_ROUTINE(f_sub_0951);
    }
}

/* $0951: copies a sprite descriptor (HL, $24 bytes) to RAM (DE) and adds A
 * to the tile number of each of its sprites. Layout of the copy: sprite
 * count n, n Y offsets, one byte, then (X offset, tile) pairs. Returns C = A.
 * Falls through into `return` ($0966) in the original. */
LIFTED(sub_0951, 0x0951) {
    uint16_t copy = cpu.de;
    uint8_t tile_offset = cpu.a;
    copy_bytes(copy, cpu.hl, TITLE_DESCRIPTOR_COPY_SIZE);

    uint8_t sprite_count = rd8(copy);
    uint16_t tile = (uint16_t)(copy + 1 + sprite_count);
    int n = sprite_count ? sprite_count : 256; /* DJNZ loop */
    for (int i = 0; i < n; i++) {
        tile = (uint16_t)(tile + 2);
        wr8(tile, (uint8_t)(rd8(tile) + tile_offset));
    }
    cpu.c = tile_offset;
    LIFTED_RETURN();
}

/* $0966: a lone RET (end of sub_0951, also referenced by the label table). */
LIFTED(return, 0x0966) {
    LIFTED_RETURN();
}
