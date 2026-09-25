/*
 * Sprite tiles of the level's enemies and items ($1164-$156C): loadLevelSpriteTiles
 * (graphics.c) maps bank 7 and jumps to the level's list below; each list
 * calls a few loaders that copy raw 4bpp tiles from bank 7 to sprite tiles
 * 318-447. Sprites facing the other way get a horizontally mirrored copy right
 * after (copyMirroredTilesToVramAtCurrentAddress, core module).
 */
#include "level.h"

/* VDP write address of sprite tile n (sprite tiles start at VRAM $2000). */
#define SPRITE_TILE(n) VDP_VRAM_WRITE(0x2000 + (n) * 32)

/* Raw copy of `tiles` tiles from `src` (bank 7) to sprite tile `first`. */
static void copy_sprite_tiles(uint16_t src, int first, int tiles) {
    vdp_set_address(SPRITE_TILE(first));
    vdp_write_bytes(src, (uint32_t)tiles * 32);
}

/* The same tiles again, bit-reversed (facing the other way), at the VDP
 * address where the previous copy ended. */
static void copy_mirrored_sprite_tiles(uint16_t src, int tiles) {
    cpu.hl = src;
    cpu.bc = (uint16_t)(tiles * 32);
    CALL_ROUTINE(f_copyMirroredTilesToVramAtCurrentAddress);
}

static void copy_sprite_tiles_both_ways(uint16_t src, int first, int tiles) {
    copy_sprite_tiles(src, first, tiles);
    copy_mirrored_sprite_tiles(src, tiles);
}

/* ------------------------------------------------------ single loaders */

/* $132F */ LIFTED(loadSmokePuffTiles, 0x132F) { copy_sprite_tiles(0x87E9, 68, 17); LIFTED_RETURN(); }
/* $133B */ LIFTED(loadMonsterBirdTiles, 0x133B) { copy_sprite_tiles_both_ways(0x8A09, 125, 12); LIFTED_RETURN(); }
/* $1350 */ LIFTED(loadMermanBubblesTiles, 0x1350) { copy_sprite_tiles(0x8DC9, 172, 2); LIFTED_RETURN(); }
/* $135C */ LIFTED(loadMermanTiles, 0x135C) { copy_sprite_tiles(0x8B89, 174, 18); LIFTED_RETURN(); }
/* $1368 */ LIFTED(loadSmallFishTiles, 0x1368) { copy_sprite_tiles_both_ways(0xAE09, 85, 8); LIFTED_RETURN(); }
/* $137D */ LIFTED(loadKillerFishTiles, 0x137D) { copy_sprite_tiles_both_ways(0x95E9, 101, 12); LIFTED_RETURN(); }
/* $1392 */ LIFTED(loadMonkeyLeafTiles, 0x1392) { copy_sprite_tiles(0x9CA9, 96, 1); LIFTED_RETURN(); }
/* $139E */ LIFTED(loadMonkeyTiles, 0x139E) { copy_sprite_tiles(0x9B49, 85, 11); LIFTED_RETURN(); }
/* $13AA */ LIFTED(loadMonsterFrogTiles, 0x13AA) { copy_sprite_tiles(0x9D49, 128, 8); LIFTED_RETURN(); }
/* $13B6 */ LIFTED(loadPlantTiles, 0x13B6) { copy_sprite_tiles(0x9E49, 187, 5); LIFTED_RETURN(); }
/* $13C2 */ LIFTED(loadSeaHorseTiles, 0x13C2) { copy_sprite_tiles(0xAF09, 144, 6); LIFTED_RETURN(); }
/* $13DA */ LIFTED(loadStNurariTiles, 0x13DA) { copy_sprite_tiles(0x8FC9, 149, 8); LIFTED_RETURN(); }
/* $13E6 */ LIFTED(loadFlyingFishTiles, 0x13E6) { copy_sprite_tiles_both_ways(0x8EC9, 149, 8); LIFTED_RETURN(); }
/* $13FB */ LIFTED(loadOxTiles, 0x13FB) { copy_sprite_tiles_both_ways(0x9769, 96, 23); LIFTED_RETURN(); }
/* $1425 */ LIFTED(loadRollingRockTiles, 0x1425) { copy_sprite_tiles(0x9EE9, 180, 6); LIFTED_RETURN(); }
/* $1431 */ LIFTED(loadGrizzlyBearTiles, 0x1431) { copy_sprite_tiles(0xA2A9, 97, 87); LIFTED_RETURN(); }
/* $143D */ LIFTED(loadLightningTiles, 0x143D) { copy_sprite_tiles(0xA0A9, 159, 1); LIFTED_RETURN(); }
/* $1449 */ LIFTED(loadDarkCloudTiles, 0x1449) { copy_sprite_tiles(0xA0C9, 153, 6); LIFTED_RETURN(); }
/* $1455 */ LIFTED(loadFlameTiles, 0x1455) { copy_sprite_tiles(0x9FA9, 160, 8); LIFTED_RETURN(); }
/* $1461 */ LIFTED(loadScorpionTiles, 0x1461) { copy_sprite_tiles_both_ways(0x8E09, 168, 6); LIFTED_RETURN(); }
/* $1476 */ LIFTED(loadEgleTiles, 0x1476) { copy_sprite_tiles(0x90C9, 140, 20); LIFTED_RETURN(); }
/* $1482 */ LIFTED(loadMoonlightStoneMedallionTiles, 0x1482) { copy_sprite_tiles(0xA229, 110, 4); LIFTED_RETURN(); }
/* $148E */ LIFTED(loadPrincessLoraTiles, 0x148E) { copy_sprite_tiles(0x8629, 114, 14); LIFTED_RETURN(); }
/* $149A */ LIFTED(loadBatTiles, 0x149A) { copy_sprite_tiles(0xAD89, 186, 4); LIFTED_RETURN(); }
/* $14A6 */ LIFTED(loadGreenDebrisTiles, 0x14A6) { copy_sprite_tiles(0x85A9, 66, 2); LIFTED_RETURN(); }
/* $14B2 */ LIFTED(loadDebrisATiles, 0x14B2) { copy_sprite_tiles(0x8529, 64, 2); LIFTED_RETURN(); }
/* $14BE */ LIFTED(loadDebrisBTiles, 0x14BE) { copy_sprite_tiles(0x8529, 62, 2); LIFTED_RETURN(); }
/* $14CA */ LIFTED(loadBlueDebrisTiles, 0x14CA) { copy_sprite_tiles(0x8569, 62, 2); LIFTED_RETURN(); }
/* $14D6 */ LIFTED(loadWoodsDebrisTiles, 0x14D6) { copy_sprite_tiles(0x85E9, 62, 2); LIFTED_RETURN(); }
/* $14E2 */ LIFTED(loadTelapathyBallTiles, 0x14E2) { copy_sprite_tiles(0x83C9, 124, 4); LIFTED_RETURN(); }
/* $14EE */ LIFTED(loadLetterTiles, 0x14EE) { copy_sprite_tiles(0x8169, 136, 4); LIFTED_RETURN(); }
/* $14FA */ LIFTED(loadHirottaStoneTiles, 0x14FA) { copy_sprite_tiles(0x81E9, 89, 4); LIFTED_RETURN(); }
/* $1506 */ LIFTED(loadGoldCrownTiles, 0x1506) { copy_sprite_tiles(0x8449, 136, 4); LIFTED_RETURN(); }
/* $1512 */ LIFTED(loadVillageElderTiles, 0x1512) { copy_sprite_tiles(0x8069, 85, 8); LIFTED_RETURN(); }
/* $1561 */ LIFTED(loadSunStoneMedallionTiles, 0x1561) { copy_sprite_tiles(0xA189, 140, 4); LIFTED_RETURN(); }

/* $151E: the teleport powder lives in bank 5: map it, copy (and mirror),
 * then map bank 7 back. */
LIFTED(loadTeleportPowderTiles, 0x151E) {
    map_bank(BANK(5));
    copy_sprite_tiles_both_ways(0xAF51, 140, 1);
    copy_sprite_tiles(0xAF71, 142, 2);
    map_bank(BANK(7));
    LIFTED_RETURN();
}

/* $154A: the Peticopter, also in bank 5. */
LIFTED(loadPeticopterTiles, 0x154A) {
    map_bank(BANK(5));
    copy_sprite_tiles(0xB211, 183, 4);
    map_bank(BANK(7));
    LIFTED_RETURN();
}

/* $13CE and $1410: loaders reached only by the jumps that end the level 3
 * and level 6 lists. */
static void load_octopus_arm_tiles(void) { copy_sprite_tiles(0x9B09, 191, 1); }
static void load_daruman_tiles(void) { copy_sprite_tiles_both_ways(0x9A49, 148, 6); }

/* --------------------------------------------------------- level lists */

/* $1164 (level 1). */
LIFTED(loadMtEthernalSpriteTiles, 0x1164) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonsterBirdTiles);
    CALL_ROUTINE(f_loadMermanBubblesTiles);
    CALL_ROUTINE(f_loadKillerFishTiles);
    CALL_ROUTINE(f_loadMermanTiles);
    TAIL_CALL(f_loadSmallFishTiles);
}

/* $117F (level 2). */
LIFTED(loadMtEthernalStage2SpriteTiles, 0x117F) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonkeyLeafTiles);
    CALL_ROUTINE(f_loadMonkeyTiles);
    CALL_ROUTINE(f_loadMonsterFrogTiles);
    TAIL_CALL(f_loadPlantTiles);
}

/* $119D (level 3). */
LIFTED(loadLakeFathomSpriteTiles, 0x119D) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadBlueDebrisTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadSmallFishTiles);
    CALL_ROUTINE(f_loadKillerFishTiles);
    CALL_ROUTINE(f_loadSeaHorseTiles);
    load_octopus_arm_tiles();
    LIFTED_RETURN();
}

/* $11B5 (level 4). */
LIFTED(loadTheIslandOfStNurariSpriteTiles, 0x11B5) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadBlueDebrisTiles);
    CALL_ROUTINE(f_loadPeticopterTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonsterBirdTiles);
    CALL_ROUTINE(f_loadStNurariTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    TAIL_CALL(f_loadPlantTiles);
}

/* $11D3 (level 5). */
LIFTED(loadLakeFathomPart2SpriteTiles, 0x11D3) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadSmallFishTiles);
    CALL_ROUTINE(f_loadMonsterBirdTiles);
    CALL_ROUTINE(f_loadKillerFishTiles);
    TAIL_CALL(f_loadFlyingFishTiles);
}

/* $11EB (level 6). */
LIFTED(loadTheVillageOfNamuiSpriteTiles, 0x11EB) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadOxTiles);
    CALL_ROUTINE(f_loadVillageElderTiles);
    load_daruman_tiles();
    LIFTED_RETURN();
}

/* $1206 (level 7). */
LIFTED(loadMtKaveSpriteTiles, 0x1206) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadBlueDebrisTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadTelapathyBallTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadRollingRockTiles);
    TAIL_CALL(f_loadBatTiles);
}

/* $1221 (level 8). */
LIFTED(loadTheBlakwoodsSpriteTiles, 0x1221) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadWoodsDebrisTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonkeyLeafTiles);
    CALL_ROUTINE(f_loadBatTiles);
    CALL_ROUTINE(f_loadGrizzlyBearTiles);
    TAIL_CALL(f_loadMonkeyTiles);
}

/* $1239 (level 9). */
LIFTED(loadRiverSpriteTiles, 0x1239) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonsterBirdTiles);
    CALL_ROUTINE(f_loadSmallFishTiles);
    CALL_ROUTINE(f_loadMermanBubblesTiles);
    CALL_ROUTINE(f_loadFlyingFishTiles);
    TAIL_CALL(f_loadMermanTiles);
}

/* $1254 (level 10). */
LIFTED(loadBingooLowlandSpriteTiles, 0x1254) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadPlantTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadLightningTiles);
    CALL_ROUTINE(f_loadDarkCloudTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    TAIL_CALL(f_loadScorpionTiles);
}

/* $126F (level 11). */
LIFTED(loadTheRadactianCastleSpriteTiles, 0x126F) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadBlueDebrisTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadTelapathyBallTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadLetterTiles);
    CALL_ROUTINE(f_loadEgleTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadRollingRockTiles);
    CALL_ROUTINE(f_loadMonkeyTiles);
    CALL_ROUTINE(f_loadMonkeyLeafTiles);
    CALL_ROUTINE(f_loadMonsterFrogTiles);
    TAIL_CALL(f_loadBatTiles);
}

/* $1299 (level 12). */
LIFTED(loadTheCityOfRadactianSpriteTiles, 0x1299) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadMonsterFrogTiles);
    TAIL_CALL(f_loadSmokePuffTiles);
}

/* $12AE (level 13). */
LIFTED(loadSwampSpriteTiles, 0x12AE) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadPlantTiles);
    TAIL_CALL(f_loadMonsterBirdTiles);
}

/* $12C0 (level 14). */
LIFTED(loadTheKingdomOfNibanaPart1SpriteTiles, 0x12C0) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadHirottaStoneTiles);
    TAIL_CALL(f_loadSmokePuffTiles);
}

/* $12CF (level 15). */
LIFTED(loadTheKingdomOfNibanaPart2SpriteTiles, 0x12CF) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonkeyLeafTiles);
    CALL_ROUTINE(f_loadMonkeyTiles);
    CALL_ROUTINE(f_loadMonsterFrogTiles);
    TAIL_CALL(f_loadPlantTiles);
}

/* $12ED (level 16). */
LIFTED(loadJankensCastleSpriteTiles, 0x12ED) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadMonsterFrogTiles);
    CALL_ROUTINE(f_loadMoonlightStoneMedallionTiles);
    CALL_ROUTINE(f_loadTeleportPowderTiles);
    CALL_ROUTINE(f_loadPrincessLoraTiles);
    CALL_ROUTINE(f_loadFlameTiles);
    CALL_ROUTINE(f_loadScorpionTiles);
    CALL_ROUTINE(f_loadRollingRockTiles);
    TAIL_CALL(f_loadPlantTiles);
}

/* $1311 (level 17). */
LIFTED(loadCraggLakeSpriteTiles, 0x1311) {
    CALL_ROUTINE(f_loadDebrisATiles);
    CALL_ROUTINE(f_loadGreenDebrisTiles);
    CALL_ROUTINE(f_loadDebrisBTiles);
    CALL_ROUTINE(f_loadSmokePuffTiles);
    CALL_ROUTINE(f_loadSmallFishTiles);
    CALL_ROUTINE(f_loadMoonlightStoneMedallionTiles);
    CALL_ROUTINE(f_loadSunStoneMedallionTiles);
    CALL_ROUTINE(f_loadGoldCrownTiles);
    CALL_ROUTINE(f_loadMermanBubblesTiles);
    TAIL_CALL(f_loadMermanTiles);
}
