/*
 * Level graphics: background tilesets, palettes and their per-frame
 * animations (docs/level-format.md section 8).
 *
 * Background tile map of every level: tiles 1-36 item boxes, 37-46 gold bag
 * and cloud (loadLevelTiles), 53-170 the level tileset loaded by the per-level
 * loader below, 176-239 the font. All tileset sources are in bank 3, which the
 * callers map (initGameplayState, the sub-area loader, the map and shop exits).
 */
#include "level.h"

/* VDP write address of background tile n (32 bytes per tile). */
#define TILE(n) VDP_VRAM_WRITE((n) * 32)

/* Bank 0 tables indexed by v_level (1-based: the "- 2" addresses are what the
 * code passes to rst $10 / rst $20). */
#define TILESET_LOADERS_MINUS_2 0x0E7B      /* tilesetLoadersPointers - 2 */
#define SPRITE_TILE_LOADERS_MINUS_2 0x1140  /* spriteTilesLoadersPointers - 2 */
#define LEVEL_PALETTES_MINUS_2 0x1110       /* levelPalettesPointers - 2 (bank 7 addresses) */

/* Bank 3 tile sets. */
#define MAIN_TILESETS_MINUS_2 0x847E        /* levelMainTilesetPointers - 2 */
#define GOLD_BAG_AND_CLOUD_TILES 0x84A2     /* RLE -> tiles 37-46 */
#define ADDITIONAL_SET_1 0x8583             /* RLE -> tiles 120-170 */
#define ADDITIONAL_SET_2 0x89E1             /* RLE -> tiles 118-170 */
#define ADDITIONAL_SET_3 0x8E65             /* RLE -> tiles 61-64 */
#define ADDITIONAL_SET_4 0xB7F6             /* raw $A0 bytes -> tiles 115-119 */
#define ADDITIONAL_SET_5 0xB896             /* RLE -> tiles 104-111 */
#define LEVEL_17_ADDITIONAL_SET 0xB75C      /* RLE -> tiles 57-61 */

/* Main tilesets shared through a fixed index. */
enum { MAIN_SET_OF_LEVEL_2 = 2, MAIN_SET_OF_LEVEL_3 = 3, MAIN_SET_OF_LEVEL_11 = 11 };

/* ------------------------------------------------------------ helpers */

/* levelMainTilesetPointers[index - 1] (bank 3), read like rst $10. */
static uint16_t main_tileset(uint8_t index) {
    return rd16((uint16_t)(MAIN_TILESETS_MINUS_2 + (uint8_t)(2 * index)));
}

/* fillVram: `count` tiles of the byte `value` from tile `first`. */
static void fill_tiles(int first, int count, uint8_t value) {
    cpu.de = TILE(first);
    cpu.bc = (uint16_t)(count * 32);
    cpu.l = value;
    CALL_ROUTINE(f_fillVram);
}

/* decompressTilesToVram ("Phantasy Star" RLE) from `src` to tile `first`. */
static void decompress_tiles(uint16_t src, int first) {
    cpu.hl = src;
    cpu.de = TILE(first);
    CALL_ROUTINE(f_decompressTilesToVram);
}

/* rst $30: `bytes` raw bytes from `src` to tile `first`. */
static void copy_raw_tiles(uint16_t src, int first, uint8_t bytes) {
    cpu.hl = src;
    cpu.de = TILE(first);
    cpu.b = bytes;
    CALL_ROUTINE(f_memcpyToVRAM);
}

/* ------------------------------------------------------ tileset loaders */

/* $0E6C loadLevelTiles: the gold bag and cloud, then the level's tileset
 * through tilesetLoadersPointers (rst $20). Bank 3 must be mapped. */
LIFTED(loadLevelTiles, 0x0E6C) {
    decompress_tiles(GOLD_BAG_AND_CLOUD_TILES, 37);
    if (call_jump_table(TILESET_LOADERS_MINUS_2, ram8(v_level))) return;
    LIFTED_RETURN();
}

/* $0E9F (levels 1, and part of 5 and 9's): blank 53-56, main set 57-,
 * raw set 115-119, set 1 120-170. */
LIFTED(loadMtEthernalTileset, 0x0E9F) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    copy_raw_tiles(ADDITIONAL_SET_4, 115, 0xA0);
    decompress_tiles(ADDITIONAL_SET_1, 120);
    LIFTED_RETURN();
}

/* $0EC9 (level 8): set 2 118-170, then the level set from tile 53 (no fill). */
LIFTED(loadTheBlakwoodsTileset, 0x0EC9) {
    decompress_tiles(ADDITIONAL_SET_2, 118);
    decompress_tiles(main_tileset(ram8(v_level)), 53);
    LIFTED_RETURN();
}

/* $0EDF (level 16): blank 53-56, level set 57-, set 1 120-170. */
LIFTED(loadJankensCastleTileset, 0x0EDF) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    decompress_tiles(ADDITIONAL_SET_1, 120);
    LIFTED_RETURN();
}

/* $0F00 (levels 10, 15): blank 53-56, level set 57-, set 2 118-170. */
LIFTED(loadBingooLowlandTileset, 0x0F00) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    decompress_tiles(ADDITIONAL_SET_2, 118);
    LIFTED_RETURN();
}

/* $0F21 (level 3): blank 53-68, level set 69-, set 5 104-111, raw set
 * 115-119, set 1 120-170. */
LIFTED(loadLakeFathomTileset, 0x0F21) {
    fill_tiles(53, 16, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 69);
    decompress_tiles(ADDITIONAL_SET_5, 104);
    copy_raw_tiles(ADDITIONAL_SET_4, 115, 0xA0);
    decompress_tiles(ADDITIONAL_SET_1, 120);
    LIFTED_RETURN();
}

/* $0F54 (levels 4, 11): blank 53-56, level set 57-. */
LIFTED(loadTheIslandOfStNurariTileset, 0x0F54) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    LIFTED_RETURN();
}

/* $0F6C (levels 6, 12): blank 53-56, level 2's set 57-, level set 73-,
 * set 2 118-170. */
LIFTED(loadTheVillageOfNamuiTileset, 0x0F6C) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(MAIN_SET_OF_LEVEL_2), 57);
    decompress_tiles(main_tileset(ram8(v_level)), 73);
    decompress_tiles(ADDITIONAL_SET_2, 118);
    LIFTED_RETURN();
}

/* $0F99 (level 5): level 1's loader, then set 5 104-111 and set 3 61-64. */
LIFTED(loadLakeFathomPart2Tileset, 0x0F99) {
    CALL_ROUTINE(f_loadMtEthernalTileset);
    decompress_tiles(ADDITIONAL_SET_5, 104);
    decompress_tiles(ADDITIONAL_SET_3, 61);
    LIFTED_RETURN();
}

/* $0FAE (level 7): tiles 53-56 filled with $0A, level set 57-. */
LIFTED(loadMtKaveTileset, 0x0FAE) {
    fill_tiles(53, 4, 0x0A);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    LIFTED_RETURN();
}

/* $0FC6 (level 9): blank 53-56, level set 57-, set 5 104-111, set 1
 * 120-170, set 3 61-64. */
LIFTED(loadRiverTileset, 0x0FC6) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(ram8(v_level)), 57);
    decompress_tiles(ADDITIONAL_SET_5, 104);
    decompress_tiles(ADDITIONAL_SET_1, 120);
    decompress_tiles(ADDITIONAL_SET_3, 61);
    LIFTED_RETURN();
}

/* $0FF9 (levels 2, 13): blank 53-56, level 2's set 57-, set 2 118-170,
 * set 3 61-64. */
LIFTED(loadMtEthernalStage2Tileset, 0x0FF9) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(MAIN_SET_OF_LEVEL_2), 57);
    decompress_tiles(ADDITIONAL_SET_2, 118);
    decompress_tiles(ADDITIONAL_SET_3, 61);
    LIFTED_RETURN();
}

/* $1022 (level 17): blank 53-68, level 3's set 69-, level set 85-,
 * level-17 set 57-61, set 1 120-170. */
LIFTED(loadCraggLakeTileset, 0x1022) {
    fill_tiles(53, 16, 0x00);
    decompress_tiles(main_tileset(MAIN_SET_OF_LEVEL_3), 69);
    decompress_tiles(main_tileset(ram8(v_level)), 85);
    decompress_tiles(LEVEL_17_ADDITIONAL_SET, 57);
    decompress_tiles(ADDITIONAL_SET_1, 120);
    LIFTED_RETURN();
}

/* $1058 (level 14): blank 53-56, level 11's set 57-, level set 109-. */
LIFTED(loadTheKingdomOfNibanaPart1Tileset, 0x1058) {
    fill_tiles(53, 4, 0x00);
    decompress_tiles(main_tileset(MAIN_SET_OF_LEVEL_11), 57);
    decompress_tiles(main_tileset(ram8(v_level)), 109);
    LIFTED_RETURN();
}

/* ---------------------------------------------------------- sprite tiles */

/* $1134 loadLevelSpriteTiles: maps bank 7 and jumps to the level's sprite
 * tile loader (spriteTilesLoadersPointers, sprite_tiles.c). */
LIFTED(loadLevelSpriteTiles, 0x1134) {
    map_bank(BANK(7));
    cpu.a = ram8(v_level);
    cpu.hl = SPRITE_TILE_LOADERS_MINUS_2;
    TAIL_CALL(f_jumpToAthPointer);
}

/* -------------------------------------------------------------- palettes */

#define PALETTE_BYTES 32

/* $10FF loadLevelPalette: the level's 32 colours (bank 7) to CRAM.
 * out: BC = $00BE as rst $30 leaves it. */
LIFTED(loadLevelPalette, 0x10FF) {
    map_bank(BANK(7));
    uint16_t palette = rd16((uint16_t)(LEVEL_PALETTES_MINUS_2 + (uint8_t)(2 * ram8(v_level))));
    vdp_set_address(VDP_CRAM_WRITE(0));
    vdp_write_bytes(palette, PALETTE_BYTES);
    cpu.bc = VDP_DATA;
    LIFTED_RETURN();
}

/* Bank 0 colour cycles. */
#define WATER_SPARKLE_COLOURS 0x10D6   /* 4 colours */
#define INVINCIBLE_COLOURS 0x10DA      /* 4 colours */
#define CRAM_WATER_SPARKLE 0x0B        /* background colour 11 */
#define CRAM_ALEX_CLOTHES 0x14         /* sprite colour 4 */
#define WATER_SPARKLE_PERIOD 8         /* reload of the down-counter: every 9 frames */
#define INVINCIBLE_FLASH_PERIOD 4      /* every 5 frames */

/* One step of a 4-colour cycle: down-counter `timer` (reloaded with
 * `period` when it goes negative), colour index at timer + 1. */
static void cycle_colour(uint16_t timer, uint8_t period, uint16_t colours, uint8_t cram_index) {
    uint8_t t = (uint8_t)(ram8(timer) - 1);
    ram8(timer) = t;
    if (!(t & 0x80)) return;
    ram8(timer) = period;
    uint8_t index = ram8(timer + 1);
    if (index >= 4) index = 0;
    ram8(timer + 1) = (uint8_t)(index + 1);
    vdp_set_address(VDP_CRAM_WRITE(cram_index));
    vdp_write(rd8((uint16_t)(colours + index)));
}

/* $10B0: in Alex's timed action states 1 and 2 (the ones updateInvincibility
 * counts down; 2 = invincible), his clothes colour flashes. */
static void flash_invincible_alex(void) {
    uint8_t action = ram8(v_alexActionState);
    if (action == 0 || action >= 3) return;
    cycle_colour(v_invincibilityColorTimer, INVINCIBLE_FLASH_PERIOD, INVINCIBLE_COLOURS, CRAM_ALEX_CLOTHES);
}

LIFTED(_LABEL_10B0_, 0x10B0) {
    flash_invincible_alex();
    LIFTED_RETURN();
}

/* $1089 (levels 1, 3, 5, 9, 16, 17): water sparkle on background colour 11
 * (not in a sub-area), then the invincibility flash. */
LIFTED(paletteUpdater_LABEL_1089_, 0x1089) {
    if (!ram8(v_currentLevelIsBonusLevel))
        cycle_colour(v_waterColorTimer, WATER_SPARKLE_PERIOD, WATER_SPARKLE_COLOURS, CRAM_WATER_SPARKLE);
    flash_invincible_alex();
    LIFTED_RETURN();
}

/* $10DE-$10FC: the other levels' updaters, all "jp $10B0". */
LIFTED(paletteUpdater_LABEL_10DE_, 0x10DE) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10E1_, 0x10E1) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10E4_, 0x10E4) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10E7_, 0x10E7) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10EA_, 0x10EA) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10ED_, 0x10ED) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10F0_, 0x10F0) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10F3_, 0x10F3) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10F6_, 0x10F6) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10F9_, 0x10F9) { TAIL_CALL(f__LABEL_10B0_); }
LIFTED(paletteUpdater_LABEL_10FC_, 0x10FC) { TAIL_CALL(f__LABEL_10B0_); }

#define STATE_JANKEN_GAME_READY 0x89
#define STATE_MAP_READY 0x8B

/* $107C updatePalette (VBlank): runs the level's palette updater
 * (paletteUpdaterPointer) in the initialised states from the Janken game
 * ($89) on, except the map. */
LIFTED(updatePalette, 0x107C) {
    uint8_t state = ram8(v_gameState);
    if (state < STATE_JANKEN_GAME_READY || state == STATE_MAP_READY) LIFTED_RETURN();
    cpu.hl = ram16(paletteUpdaterPointer);
    rt_dispatch(cpu.hl);
}

/* ------------------------------------------------------- tile animation */

#define TILE_ANIMATION_PERIOD 0x12    /* frames */
#define v_tileAnimationRequested v_shouldUpdateLevelTiles

/* Frame tables (bank 0): words -> raw tiles in bank 5. */
#define WATER_FRAMES 0x1620           /* 6 entries, played 0 1 2 3 2 1 */
#define SWAMP_FRAMES 0x162C
#define LAVA_A_FRAMES 0x1638          /* 4 entries */
#define BOILING_WATER_FRAMES 0x1640
#define LAVA_B_FRAMES 0x1648

/* $158F updateLevelTiles (VBlank): every 18 frames, maps bank 5 and runs the
 * level's tile animator (v_levelTileUpdaterPointer). QUIRK:
 * v_shouldUpdateLevelTiles is cleared but has no effect (both paths
 * decrement the timer once). */
LIFTED(updateLevelTiles, 0x158F) {
    ram8(v_tileAnimationRequested) = 0;
    uint8_t timer = (uint8_t)(ram8(v_levelTileUpdateTimer) - 1);
    ram8(v_levelTileUpdateTimer) = timer;
    if (timer != 0) LIFTED_RETURN();
    ram8(v_levelTileUpdateTimer) = TILE_ANIMATION_PERIOD;
    map_bank(BANK(5));
    cpu.hl = ram16(v_levelTileUpdaterPointer);
    rt_dispatch(cpu.hl);
}

/* $15C8 getAnimatedTileAddress: A = frame, DE = frame table -> HL = frame data. */
static uint16_t frame_address(uint16_t table, uint8_t frame) {
    return rd16((uint16_t)(table + (uint8_t)(2 * frame)));
}

/* Advances the frame counter at `counter` (wraps at `frames`, counting
 * 1, 2, ..., frames-1, 0) and returns that frame's data. */
static uint16_t next_frame(uint16_t counter, uint8_t frames, uint16_t table) {
    uint8_t frame = (uint8_t)(ram8(counter) + 1);
    ram8(counter) = frame;
    if (frame >= frames) {
        frame = 0;
        ram8(counter) = 0;
    }
    return frame_address(table, frame);
}

LIFTED(getAnimatedTileAddress, 0x15C8) {
    cpu.hl = frame_address(cpu.de, cpu.a);
    LIFTED_RETURN();
}

/* $15C6 resetAnimatedTileTimer: HL = frame counter: restart at frame 0. */
LIFTED(resetAnimatedTileTimer, 0x15C6) {
    wr8(cpu.hl, 0);
    cpu.hl = frame_address(cpu.de, 0);
    LIFTED_RETURN();
}

/* $15AF getFourFrameTileAddress: DE = table -> HL = next frame (0-3). */
LIFTED(getFourFrameTileAddress, 0x15AF) {
    cpu.hl = next_frame(v_fourFrameLevelTileIndex, 4, cpu.de);
    LIFTED_RETURN();
}

/* $15BC getSixFrameTileAddress: DE = table -> HL = next frame (0-5). */
LIFTED(getSixFrameTileAddress, 0x15BC) {
    cpu.hl = next_frame(v_sixFrameLevelTileIndex, 6, cpu.de);
    LIFTED_RETURN();
}

/* Copies one animation frame (bank 5) over `bytes` bytes of tiles from `first`. */
static void show_frame(uint16_t frame, int first, uint8_t bytes) {
    cpu.hl = frame;
    cpu.de = TILE(first);
    cpu.b = bytes;
    CALL_ROUTINE(f_memcpyToVRAM);
}

/* $15D2 (levels 1, 3, 5, 9, 17): water, tiles 136-137. */
LIFTED(updateWaterTilesA, 0x15D2) {
    show_frame(next_frame(v_sixFrameLevelTileIndex, 6, WATER_FRAMES), 136, 0x40);
    LIFTED_RETURN();
}

/* $15DF (levels 2, 6, 10, 13, 15): swamp, tiles 70-71. */
LIFTED(updateSwampTiles, 0x15DF) {
    show_frame(next_frame(v_sixFrameLevelTileIndex, 6, SWAMP_FRAMES), 70, 0x40);
    LIFTED_RETURN();
}

/* $15EC (level 4): lava, tiles 79-81. */
LIFTED(updateLavaTilesA, 0x15EC) {
    show_frame(next_frame(v_fourFrameLevelTileIndex, 4, LAVA_A_FRAMES), 79, 0x60);
    LIFTED_RETURN();
}

/* $15F9 (level 16): boiling water, tiles 69-71, and water, tiles 136-137. */
LIFTED(updateWaterTilesB, 0x15F9) {
    show_frame(next_frame(v_fourFrameLevelTileIndex, 4, BOILING_WATER_FRAMES), 69, 0x60);
    show_frame(next_frame(v_sixFrameLevelTileIndex, 6, WATER_FRAMES), 136, 0x40);
    LIFTED_RETURN();
}

/* $1612 (level 7): lava, tiles 90-92. */
LIFTED(updateLavaTilesB, 0x1612) {
    show_frame(next_frame(v_fourFrameLevelTileIndex, 4, LAVA_B_FRAMES), 90, 0x60);
    LIFTED_RETURN();
}

/* $161F (levels 8, 11, 12, 14): no animation. */
LIFTED(doNotUpdateTiles, 0x161F) {
    LIFTED_RETURN();
}
