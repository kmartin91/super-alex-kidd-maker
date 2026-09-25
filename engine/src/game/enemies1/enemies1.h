/*
 * Shared definitions of the "enemies1" module: entity updaters and helpers
 * located below $5901 (Alex's weapons and items, tile objects, enemies).
 *
 * Only the files of this module include this header. Everything here is either
 * a constant or a small static inline helper, so nothing leaks into the other
 * modules linked into the same program.
 */
#ifndef GAME_WIP_ENEMIES1_H
#define GAME_WIP_ENEMIES1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"

/* ------------------------------------------------------------------ slots */

#define ENTITY_SIZE 0x20
/* Address of entity slot n, numbered from 1 like the reference disassembly
 * (v_entities.1 = $C300 = Alex, v_entities.2 = $C320...). */
#define ENTITY_SLOT(n) ((uint16_t)(v_entities + ((n) - 1) * ENTITY_SIZE))

/* Fixed slots used by Alex's weapons and items. */
#define WEAPON_SLOT ENTITY_SLOT(2)        /* $C320: shot, shockwave, capsule A helper #1 */
#define WEAPON_SLOT_2 ENTITY_SLOT(3)      /* $C340: capsule A helper #2 */
#define ITEM_SLOT ENTITY_SLOT(4)          /* $C360: thrown capsule / barrier, wreck puff */
#define FIRST_ENEMY_SLOT ENTITY_SLOT(7)   /* $C3C0: slots 7..16 hold the level's enemies */
#define ENEMY_SLOT_COUNT 10
#define PROJECTILE_SLOT ENTITY_SLOT(17)   /* $C500: slots 17..21 hold enemy projectiles */
#define PROJECTILE_SLOT_COUNT 5

/* ------------------------------------------------------------ entity types */

enum {
    ENTITY_VEHICLE_SHOT = 0x02,           /* boat / peticopter missile */
    ENTITY_VEHICLE_WRECK_PUFF = 0x03,     /* puff shown when a vehicle is destroyed */
    ENTITY_VEHICLE_SHOT_COOLDOWN = 0x04,  /* invisible: re-arms the vehicle gun */
    ENTITY_CAPSULE_A_THROWN = 0x05,       /* Magic Capsule A flying / falling */
    ENTITY_CAPSULE_A_OPEN = 0x06,         /* Magic Capsule A releasing helpers */
    ENTITY_CAPSULE_B_THROWN = 0x07,       /* Magic Capsule B flying / falling */
    ENTITY_CAPSULE_B_BARRIER = 0x08,      /* Magic Capsule B protective barrier */
    ENTITY_CAPSULE_HELPER_WALKING = 0x09, /* capsule A helper walking to an enemy */
    ENTITY_CAPSULE_HELPER_FALLING = 0x0A, /* capsule A helper jumping / falling */
    ENTITY_SINKING_BLOCK_A = 0x10,        /* tile object setups, become type $14 */
    ENTITY_SINKING_BLOCK_B = 0x11,
    ENTITY_SINKING_BLOCK_C = 0x12,
    ENTITY_SINKING_BLOCK_D = 0x13,
    ENTITY_SINKING_BLOCK = 0x14,          /* background block moving down row by row */
    ENTITY_DESCENDING_BAND = 0x15,        /* waterfall-like curtain pouring down */
    ENTITY_COLLAPSING_FLOOR = 0x16,       /* floor that crumbles when Alex comes near */
    ENTITY_PUNCHABLE_FLOOR = 0x17,        /* same, triggered by Alex's attack */
    ENTITY_SHOCKWAVE = 0x1B,              /* Power Bracelet shot */
    ENTITY_BAT_LEFT = 0x20,
    ENTITY_MERMAN_BUBBLE = 0x22,
    ENTITY_MERMAN = 0x23,
    ENTITY_OCTOPUS_ARM = 0x24,
    ENTITY_SWORDSMAN_WALK_LEFT = 0x25,    /* 8-hit scimitar warrior of The Blakwoods */
    ENTITY_SWORDSMAN_WALK_RIGHT = 0x26,
    ENTITY_SWORDSMAN_ATTACK_LEFT = 0x27,
    ENTITY_SWORDSMAN_ATTACK_RIGHT = 0x28,
    ENTITY_MONKEY_LEAF = 0x29,
    ENTITY_MONKEY = 0x2A,
    ENTITY_SMOKE_PUFF = 0x2B,             /* death puff of ordinary enemies */
    ENTITY_PLANT = 0x2C,
    ENTITY_MONSTERBIRD_LEFT = 0x2D,
    ENTITY_KILLER_FISH_LEFT = 0x2E,
    ENTITY_MONSTER_FROG = 0x2F,
    ENTITY_SMALL_FISH_LEFT = 0x30,
    ENTITY_SEA_HORSE_LEFT = 0x31,
    ENTITY_SEA_HORSE_RIGHT = 0x32,
    ENTITY_MONSTERBIRD_RIGHT = 0x33,
    ENTITY_SMALL_FISH_RIGHT = 0x34,
    ENTITY_KILLER_FISH_RIGHT = 0x35,
    ENTITY_BAT_RIGHT = 0x36,
    ENTITY_MONSTER_FROG_JUMPING = 0x37,
    ENTITY_BIG_DEATH_PUFF = 0x43,         /* death puff of 8-hit enemies */
    ENTITY_RICE_BALL = 0x44,
    ENTITY_STAGGERED = 0x4A,              /* 8-hit enemy knocked back by a hit */
    ENTITY_MAP_ARROW = 0x56,
    ENTITY_JANKENS_CASTLE = 0x58,
    ENTITY_EVENT_TRIGGER = 0x60,
    ENTITY_TOLL = 0x61,
    ENTITY_LEVEL_START_ICON = 0x62,
    ENTITY_SECRET_WALL = 0x63,
};

/* ------------------------------------------------------------ entity flags */

enum {
    EF_INITIALIZED = 0x01,       /* the updater ran its first-frame setup */
    EF_DESTROY_OFFSCREEN = 0x02, /* the movement code removes it when it leaves the screen */
    EF_TRIGGERED = 0x10,         /* tile objects $16/$17: collapse started */
    EF_HIT = 0x80,               /* set by checkEntityCollision on a touch (weapon hit...) */
};

/* Alex's Entity.unknown8 bits (weapon state). */
enum {
    ALEX_ATTACKING = 0x01,       /* a punch or weapon attack is in progress */
    ALEX_WEAPON_OUT = 0x02,      /* a thrown item / shockwave is on screen */
    ALEX_ATTACK_UNSPENT = 0x08,  /* the current attack has not damaged anything yet */
};

/* Alex's Entity.unknown3 bits. */
enum {
    ALEX_FACING_RIGHT = 0x01,
    ALEX_MOVING_RIGHT = 0x02,
};

/* Alex states read here (Entity.state of slot 0). */
enum { ALEX_DEAD = 0x0F };

/* Name table attribute bits used by the game as collision flags. */
enum {
    TILE_SOLID = 0x80,
    TILE_BREAKABLE = 0x40,
};

/* Game states (v_gameState & $7F). */
enum { STATE_LEVEL_STARTING = 3 };

/* Values of v_nametableChangeRequest: $80 | index into the handler table at
 * $4237, processed by the main loop. */
enum {
    NT_CHANGE_METATILE = 0x80,
    NT_CHANGE_OCTOPUS_DEFEATED = 0x83,
    NT_CHANGE_OCTOPUS_DEFEATED_LATE = 0x84,
    NT_CHANGE_COPY_BLOCK = 0x85,   /* _LABEL_4B9E_ */
    NT_CHANGE_DRAW_BAND = 0x86,    /* _LABEL_4BF3_ */
    NT_CHANGE_ERASE_BLOCK = 0x87,  /* _LABEL_4BCD_ */
};

/* v_scrollFlags & SCROLL_ANY: the screen is scrolling this frame. */
#define SCROLL_ANY 0x0F

/* Sound requests (written to v_soundControl). */
enum {
    SOUND_SMOKE_PUFF = 0x8B,
    SOUND_BOSS_DEFEATED = 0x95,
    SOUND_MERMAN_BUBBLES = 0x97,
    SOUND_MONKEY_LEAF = 0x98,
    SOUND_SWORDSMAN_ATTACK = 0x99,
    SOUND_BAND_STARTS = 0x9F,
    SOUND_FLOOR_COLLAPSES = 0xA0,
    SOUND_BLOCK_SINKS = 0xA2,
    SOUND_SHOCK_WAVE = 0xA4,
    SOUND_HELPER_APPEARS = 0xA5,
    SOUND_HELPER_JUMPS = 0xA7,
    SOUND_VEHICLE_SHOT_EXPLODES = 0xA9,
    SOUND_MAGIC_CAPSULE_B = 0xAB,
};

/* Sprite descriptor with no sprite. */
#define NULL_SPRITE 0x80E1

/* ------------------------------------------------- byte views of 16-bit fields */

/* The 16-bit Entity fields are little endian: .low at +0, .high at +1. For
 * positions the high byte is the pixel, the low byte the sub-pixel fraction. */
#define ENT_LO(e, field) (((uint8_t *)(e))[offsetof(Entity, field)])
#define ENT_HI(e, field) (((uint8_t *)(e))[offsetof(Entity, field) + 1])

/* Pixel position on the current screen. */
#define ENT_X(e) ENT_HI(e, xPos)
#define ENT_Y(e) ENT_HI(e, yPos)
/* Screen offsets: 0 = on the visible screen. Horizontally +1 means one screen
 * to the LEFT ($FF one to the right); vertically +1 means one screen below. */
#define ENT_SCREEN_X(e) ENT_LO(e, isOffScreenFlags)
#define ENT_SCREEN_Y(e) ENT_HI(e, isOffScreenFlags)

static inline bool entity_on_screen(const Entity *e) {
    return (ENT_SCREEN_X(e) | ENT_SCREEN_Y(e)) == 0;
}

static inline Entity *alex(void) { return entity_at(v_alex); }

/* 16-bit value stored in two consecutive bytes that are not declared as one
 * field (e.g. stateTimer/unknown8 used as a pointer). */
#define ENT_WORD(lo_byte, hi_byte) ((uint16_t)((lo_byte) | ((hi_byte) << 8)))

/* ------------------------------------------------------------ flag helpers */

static inline void set_carry(bool carry) {
    cpu.f = (uint8_t)(carry ? (cpu.f | FLAG_C) : (cpu.f & ~FLAG_C));
}

/* ------------------------------------------------- calls to other routines */

/* Calls an original routine (generated or lifted) through the register ABI.
 * Used for callees that always return to their caller; an unexpected stack
 * imbalance stops the program. */
static inline void call_routine(GameFn fn) {
    uint16_t sp = cpu.sp;
    push16(0x0000);
    fn();
    if (cpu.sp != sp) rt_stack_error(0x0000, sp);
}

/* isAlexAttackingEntity: true when Alex's current attack (punch, shot,
 * shockwave, capsule helper, barrier...) touches the entity in IX. */
static inline bool alex_attack_hits(void) {
    call_routine(f_isAlexAttackingEntity);
    return !(cpu.f & FLAG_C);
}

/* tryToKillAlexIfColliding: hurts Alex if he touches the entity in IX. */
static inline void hurt_alex_on_contact(void) { call_routine(f_tryToKillAlexIfColliding); }

/* getNearEntityTileAttrWithOffset: collision attribute of the background tile
 * at (x + dx, y + dy) from the entity in IX. Leaves BC/HL describing the tile
 * (B = its nametable row coordinate, HL = its name table entry + 1). */
static inline uint8_t tile_attr_at(uint8_t dy, uint8_t dx) {
    cpu.de = (uint16_t)(dy << 8 | dx);
    call_routine(f_getNearEntityTileAttrWithOffset);
    return cpu.a;
}

/* isEntityCollidingWithTerrainAtOffset (used by flying/swimming enemies to
 * detect walls): true on collision. */
static inline bool terrain_at(uint8_t dy, uint8_t dx, uint8_t height) {
    cpu.de = (uint16_t)(dy << 8 | dx);
    cpu.a = height;
    call_routine(f_isEntityCollidingWithTerrainAtOffset);
    return (cpu.f & FLAG_C) != 0;
}

/* Ends the updater by running handleEntityAnimation with the given animation
 * descriptor (a jump in the original, so it returns to our caller). */
#define ANIMATE_AND_RETURN(descriptor)            \
    do {                                          \
        cpu.hl = (descriptor);                    \
        TAIL_CALL(f_handleEntityAnimation);       \
    } while (0)

/* Ends the updater by freeing the entity slot (jp destroyCurrentEntity). */
#define DESTROY_AND_RETURN() TAIL_CALL(f_destroyCurrentEntity)

/* Name table mirror in RAM: $C800-$CEFF, 28 rows of 64 bytes (tile, attribute
 * pairs). Subtracting $5000 turns a mirror address into the VDP control word
 * that writes the same entry in VRAM ($3800 | $4000). */
#define NAMETABLE_MIRROR 0xC800
#define NAMETABLE_MIRROR_END_PAGE 0xCF
#define NAMETABLE_ROW 0x40
#define MIRROR_TO_VDP_WRITE 0x5000

#endif
