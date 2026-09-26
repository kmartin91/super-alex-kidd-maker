/*
 * Maker mode: what a level of Super Alex Kidd Maker needs beyond the original
 * game. It is only active when the (modded) ROM carries a maker block, so the
 * original game runs exactly as before.
 *
 * Levels you can walk back in: the camera scrolls both ways, and the entities
 * of the start screen appear too (game/level/scroll.c, layout.c,
 * states/gameplay.c).
 *
 * Enemies from any level: on the Master System each level loads the sprite
 * tiles of its own enemies only, at fixed places, so an enemy from another
 * level would be drawn with someone else's tiles. In maker mode, when a level
 * loads, the sprite tiles of every level are captured into extended sprite
 * memory; each sprite drawn for an entity remembers the level its enemy comes
 * from ("home"), and the renderer takes that sprite's tiles and colours from
 * there.
 *
 * Maker block (written by maker/public/js/backend.js), at MAKER_BLOCK_OFFSET:
 *   "AKMAKER1"            magic
 *   u8 home[256]          entity type -> home level (1..17), 0 = the level's own
 * optionally followed by:
 *   "AKJANKEN"            magic
 *   u8 janken[4][16]      per janken opponent (data >> 1: 0 Janken, 1 Gooseka,
 *                         2 Chokkinna, 3 Parplin): count, then up to 15
 *                         throws (0 rock, 1 scissors, 2 paper) played in
 *                         turn, ties included; count 0 = the game's choices
 */
#ifndef RT_MAKER_H
#define RT_MAKER_H

#include <stdbool.h>
#include <stdint.h>

#define MAKER_BLOCK_OFFSET 0x7FE00
#define MAKER_JANKEN_MOVES 15

/* Extra entity slots (as many enemies alive at once as a level wants): 96
 * slots after the game's 30, in extra RAM at $E000 (on the console, $E000-
 * $FFFF only repeats $C000-$DFFF; the original game never uses it). */
#define MAKER_EXTRA_RAM 0xE000
#define MAKER_EXTRA_RAM_END 0xF000
#define MAKER_EXTRA_SLOTS 96
#define MAKER_SLOTS (30 + MAKER_EXTRA_SLOTS)
/* Sprites beyond the 64 of the console's sprite table. */
#define MAKER_EXTRA_SPRITES 512

typedef struct MakerSprite {
    uint8_t y, x, tile, home;
} MakerSprite;
#define MAKER_LEVELS 17
#define MAKER_SPRITE_TILES_BYTES 0x1800 /* sprite tiles 256..447 (VRAM $2000-$37FF) */

typedef struct Maker {
    bool active;
    uint8_t home[256];
    uint8_t janken[4][1 + MAKER_JANKEN_MOVES]; /* see the block above */
    uint8_t janken_throws;                     /* throws so far in this match */
    int capturing; /* level whose sprite tiles VRAM writes go to, 0 = none */
    bool loading_own_sprites; /* the level's own enemies are being loaded */
    uint8_t sprite_tiles[MAKER_LEVELS + 1][MAKER_SPRITE_TILES_BYTES];
    uint8_t sprite_cram[MAKER_LEVELS + 1][16];
    uint8_t slot_home[MAKER_SLOTS];     /* per entity slot (maker_slot_index): its home level */
    uint8_t slot_type[MAKER_SLOTS];     /* the type slot_home was decided for */
    uint16_t slot_record[MAKER_SLOTS];  /* level record the entity was spawned from, 0 = none */
    MakerSprite sprites[MAKER_EXTRA_SPRITES]; /* this frame's sprites beyond the 64 */
    int sprite_count;
    /* Capture of an entity's appearance (engine/tools/capture.c), also
     * without maker mode: the entity captured and those it creates are
     * "traced", and so are the sprites they add to the RAM sprite table. */
    bool trace;
    bool traced[30];
    bool ram_traced[64];
    uint8_t ram_home[64]; /* per slot of the RAM sprite table */
    /* Scrolling both ways (game/level/scroll.c): the last screen that entered
     * came from the left; the start screen's entities are still to load. */
    bool camera_both_ways;     /* plain horizontal level, once drawn */
    uint8_t scroll_flags_set;  /* the left/right flags maker mode last set */
    bool entered_from_left;
    bool start_screen_pending;
    uint8_t after_start_screen; /* screen number to load next (vertical levels), 0 = none */
    uint8_t vdp_home[64]; /* per slot of the sprite table in VRAM */
} Maker;

extern Maker maker;

/* Maker levels: the next throw of a janken opponent (its `data`) as chosen in
 * the editor, or -1 to let the game choose. */
int maker_janken_next(uint8_t opponent_data);

/* RAM offset of a $C000-$FFFF address: the console repeats $C000-$DFFF at
 * $E000-$FFFF, except maker mode's extra RAM. */
static inline uint16_t maker_ram_offset(uint16_t a) {
    if (a >= MAKER_EXTRA_RAM && a < MAKER_EXTRA_RAM_END && maker.active) return (uint16_t)(a - 0xC000);
    return a & 0x1FFF;
}

/* Index of an entity slot address among the game's 30 then the extra ones. */
static inline int maker_slot_index(uint16_t slot) {
    return slot >= MAKER_EXTRA_RAM ? 30 + ((slot - MAKER_EXTRA_RAM) >> 5) : ((slot - 0xC300) >> 5) & 31;
}

/* Called with the ROM the machine runs: turns maker mode on or off. */
void maker_detect(const uint8_t *rom, uint32_t rom_size);

/* A VRAM write in maker mode. While a level's sprite tiles are captured, it
 * goes to that level's extended sprite memory instead (returns true). During
 * play, sprite tiles loaded at run time (the janken battle's) go everywhere,
 * so that entities from any level see them. */
static inline bool maker_capture_write(uint16_t addr, uint8_t value) {
    if (!maker.active || addr < 0x2000 || addr >= 0x2000 + MAKER_SPRITE_TILES_BYTES) return false;
    if (maker.capturing) {
        maker.sprite_tiles[maker.capturing][addr - 0x2000] = value;
        return true;
    }
    if (maker.loading_own_sprites) return false;
    for (int h = 1; h <= MAKER_LEVELS; h++) maker.sprite_tiles[h][addr - 0x2000] = value;
    return false;
}

#endif
