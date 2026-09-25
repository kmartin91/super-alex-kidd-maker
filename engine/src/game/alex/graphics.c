/*
 * Alex's sprite animation and tile loading ($4182-$4221).
 *
 * Alex's sprite descriptors start with a tile set index. The frame's tile set
 * index is written to v_alexTilesIndex; during VBlank the interrupt handler
 * compares it with the set currently in VRAM and streams the new tiles to
 * VRAM $2000 when it changed (Alex's tiles are not all resident).
 */
#include "alex.h"
#include "game/vdp_io.h"

#define ALEX_TILE_SETS 0x8000       /* bank 4: word table of tile set descriptors */
#define ALEX_TILES_BANK 0x84        /* mapper value for bank 4 */
#define ALEX_TILES_VRAM 0x2000
#define TILE_ROWS 8
#define TILE_STORED_PLANES 3        /* the 4th bitplane is always 0 */

/* $41A3 loadAlexSpriteDescriptor: show sprite descriptor `descriptor`.
 * Leaves HL = descriptor + 1 (the sprite list), as the original. */
void alex_set_sprite(uint16_t descriptor) {
    ram8(v_alexTilesIndex) = rd8(descriptor);
    ALEX->spriteDescriptorPointer = (uint16_t)(descriptor + 1);
    cpu.hl = (uint16_t)(descriptor + 1);
}

/* $4182 loadAlexAnimationDescriptor: advance the animation `animation` by one
 * frame. The frame changes every animationTimerResetValue frames and wraps
 * after the last one; the animation tables are shared, so switching between
 * animations keeps the current frame number. */
void alex_animate(uint16_t animation) {
    Entity *alex = ALEX;
    uint8_t frame_count = rd8(animation);
    uint8_t frame = alex->animationFrame;
    if (--alex->animationTimer == 0) {
        alex->animationTimer = alex->animationTimerResetValue;
        frame++;
    }
    if (frame >= frame_count) frame = 0;
    alex->animationFrame = frame;
    alex_set_sprite(rd16((uint16_t)(animation + 1 + (uint8_t)(frame * 2))));
}

LIFTED(loadAlexSpriteDescriptor, 0x41A3) {
    alex_set_sprite(cpu.hl);
    LIFTED_RETURN();
}

LIFTED(loadAlexAnimationDescriptor, 0x4182) {
    alex_animate(cpu.hl);
    LIFTED_RETURN();
}

/* $41C1 loadAlexTilesToVRAM: stream tile set number offset/2 (BC = offset in
 * the table) to the VDP address already set. Each tile is stored as 8 rows of
 * 3 bitplanes; the 4th plane is written as 0 (Alex uses 8 colours).
 * Registers as the original leaves them: B counts down once per byte sent
 * (OUTI), A' = 0 and F' = the flags of the last OUTI. */
static void load_tiles(uint16_t offset) {
    wr8(0xFFFF, ALEX_TILES_BANK);
    uint16_t descriptor = rd16((uint16_t)(ALEX_TILE_SETS + offset));
    uint8_t tile_count = rd8(descriptor);
    uint16_t next_tile = (uint16_t)(descriptor + 1);
    uint8_t b = (uint8_t)(offset >> 8);
    do {
        uint16_t src = rd16(next_tile);
        next_tile = (uint16_t)(next_tile + 2);
        for (int row = 0; row < TILE_ROWS; row++) {
            for (int plane = 0; plane < TILE_STORED_PLANES; plane++) vdp_write(rd8(src++));
            vdp_write(0x00);
        }
        b = (uint8_t)(b - TILE_ROWS * TILE_STORED_PLANES);
    } while (--tile_count);
    cpu.b = b;
    cpu.af_ = (uint16_t)(FLAG_P | FLAG_N | (b ? 0 : FLAG_Z));
}

LIFTED(loadAlexTilesToVRAM, 0x41C1) {
    load_tiles(cpu.bc);
    LIFTED_RETURN();
}

/* $41B9 loadAlexTilesToVRAM2000: tile set A to VRAM $2000. */
static void load_tiles_2000(uint8_t tile_set) {
    vdp_set_address(VDP_VRAM_WRITE(ALEX_TILES_VRAM));
    load_tiles((uint8_t)(tile_set * 2));
}

LIFTED(loadAlexTilesToVRAM2000, 0x41B9) {
    load_tiles_2000(cpu.a);
    LIFTED_RETURN();
}

/* $41AC requestLevelTilesUpdateIfAlexTilesChanged (VBlank): load the tile set
 * of Alex's current sprite if it is not the one in VRAM. */
LIFTED(requestLevelTilesUpdateIfAlexTilesChanged, 0x41AC) {
    uint8_t wanted = ram8(v_alexTilesIndex);
    if (wanted != ram8(RAM_ALEX_LOADED_TILES)) {
        ram8(RAM_ALEX_LOADED_TILES) = wanted;
        ram8(v_shouldUpdateLevelTiles) = 1;
        load_tiles_2000(wanted);
    }
    LIFTED_RETURN();
}
