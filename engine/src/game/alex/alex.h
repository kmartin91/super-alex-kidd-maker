/*
 * Alex, the player character: shared definitions of the "alex" module.
 *
 * Alex always lives in entity slot 0 ($C300); every routine of this module
 * that runs as Alex's updater is entered with IX = v_alex. The original code
 * mixes (ix+n) and absolute v_alex.field addressing for the same fields; the
 * lifted code uses the ALEX pointer for both. Routines that can run with IX on
 * another entity (block contents, the $60 event entity) use entity_at(cpu.ix).
 *
 * Speeds and positions are 8.8 fixed point: the high byte is the pixel part,
 * the low byte 1/256 of a pixel. Speeds are signed (two's complement):
 * $0200 = 2 px/frame right/down, $FE00 = 2 px/frame left/up.
 */
#ifndef GAME_WIP_ALEX_H
#define GAME_WIP_ALEX_H

#include <stdbool.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"

#define ALEX (entity_at(v_alex))

/* Byte halves of the 16-bit Entity fields: HI(e->xPos) is the pixel column,
 * HI(e->isOffScreenFlags) is non-zero while the entity is vertically outside
 * the screen ($FF above it). */
#define HI(field) (((uint8_t *)&(field))[1])
#define LO(field) (((uint8_t *)&(field))[0])

/* ---------------------------------------------------------------- states */
/* Entity.state of Alex: index into the handler table at $2982. */
enum {
    ALEX_STATE_SPAWNING = 0x00,         /* first frame of a life/level */
    ALEX_STATE_IDLE = 0x01,
    ALEX_STATE_WALKING = 0x02,
    ALEX_STATE_IN_AIR = 0x03,           /* jumping or falling */
    ALEX_STATE_CROUCHED = 0x04,
    ALEX_STATE_SWIMMING = 0x05,
    ALEX_STATE_PETICOPTER = 0x06,
    ALEX_STATE_CANE_FLIGHT = 0x07,      /* floating with the Cane of Flight (set after the item menu) */
    ALEX_STATE_MOTORCYCLE = 0x08,
    ALEX_STATE_MOTORCYCLE_JUMP = 0x09,
    ALEX_STATE_CLIMBING = 0x0A,         /* on a ladder tile ($3F) */
    ALEX_STATE_BOAT = 0x0B,
    ALEX_STATE_BOAT_JUMP = 0x0C,
    ALEX_STATE_REACHING_DOOR = 0x0D,    /* walking to a shop door */
    ALEX_STATE_CROSSING_DOOR = 0x0E,
    ALEX_STATE_DEAD = 0x0F,
    ALEX_STATE_SCREEN_FLIP = 0x10,      /* frozen while the screen scrolls by one screen */
    ALEX_STATE_TO_HATCH = 0x11,         /* moving to a hatch in the floor */
    ALEX_STATE_DOWN_HATCH = 0x12,       /* going down through the hatch */
    ALEX_STATE_DIVING = 0x13,           /* falling into the water after a vehicle crash */
    ALEX_STATE_AUTO_WALK = 0x14,        /* walking in from the left of the screen */
    ALEX_STATE_JANKEN_COUNT = 0x15,     /* janken: "jan-ken-pon" count, choosing a hand */
    ALEX_STATE_JANKEN_WALK = 0x16,      /* janken: walking to his place */
    ALEX_STATE_JANKEN_DANCE = 0x17,     /* janken: waiting, choosing a hand */
    ALEX_STATE_JANKEN_THROW = 0x18,     /* janken: showing the chosen hand */
    ALEX_STATE_PETRIFIED = 0x19,        /* janken lost: turned to stone, then dies */
    ALEX_STATE_FROZEN = 0x1A,           /* held still by the $60 event entity */
    ALEX_STATE_VEHICLE_CRASH = 0x1B,    /* falling after the boat/peticopter was destroyed */
};

/* v_alexActionState: what button 2 does (item or vehicle in use). */
enum {
    ACTION_NONE = 0x00,
    ACTION_CANE_OF_FLIGHT = 0x01,
    ACTION_INVINCIBLE = 0x02,
    ACTION_CAPSULE_A = 0x03,
    ACTION_CAPSULE_B = 0x04,
    ACTION_POWER_BRACELET = 0x05,
    ACTION_MOTORCYCLE = 0x07,
    ACTION_BOAT = 0x08,
    ACTION_PETICOPTER = 0x09,
};

/* Entity.unknown3 of Alex: motion flags. */
enum {
    MOTION_FACING_RIGHT = 0x01,  /* sprite orientation */
    MOTION_RIGHT = 0x02,         /* horizontal motion direction (0 = left) */
    MOTION_HORIZONTAL = 0x04,    /* moving horizontally (speed being applied) */
    MOTION_DOWN = 0x08,          /* vertical direction for swimming/flying (0 = up) */
    MOTION_VERTICAL = 0x10,      /* moving vertically (swimming/flying) */
    MOTION_LANDED = 0x40,        /* the gravity step found the ground this frame */
    MOTION_FALLING = 0x80,       /* in the air, moving down */
};

/* Entity.unknown8 of Alex: action flags. */
enum {
    ACTFLAG_PUNCHING = 0x01,     /* an attack is in progress (no new action) */
    ACTFLAG_THROWING = 0x02,     /* the attack is a thrown capsule: no punch pose timer */
    ACTFLAG_GRAVITY = 0x04,      /* in the air: the jump boost is over, gravity applies */
    ACTFLAG_ATTACK_BOX = 0x08,   /* the fist hurts enemies */
    ACTFLAG_STUNNED = 0x10,      /* shaking after punching a skull box */
    ACTFLAG_ENGINE_ON = 0x40,    /* peticopter: took off */
};

/* Entity.flags bit 7: Alex was hit (enemy, deadly tile); handled by updateAlex. */
#define ENTITY_FLAG_HIT 0x80

/* Nametable attribute bits (high byte of an entry in the RAM copy at $C800).
 * Bits 5-7 are unused by the VDP; the game stores the tile behaviour there. */
enum {
    TILE_SOLID = 0x80,
    TILE_SPECIAL = 0x40,
    TILE_VARIANT = 0x20,
    TILE_CLASS_MASK = 0xE0,
    TILE_CLASS_WATER = 0x20,     /* water */
    TILE_CLASS_DOOR = 0x60,      /* doors, ladders and deadly tiles */
    TILE_CLASS_FLOOR = 0xA0,     /* solid floor with an effect (ladder top, hatch, puzzle...) */
    TILE_CLASS_BREAKABLE = 0xC0, /* (attr & $C0) == $C0: breakable block */
};

#define TILE_LADDER 0x3F /* tile index of ladders */

/* Joypad bits of v_inputData / v_inputDataChanges. */
enum {
    PAD_UP = 0x01,
    PAD_DOWN = 0x02,
    PAD_LEFT = 0x04,
    PAD_RIGHT = 0x08,
    PAD_JUMP = 0x10,   /* button 1 */
    PAD_ACTION = 0x20, /* button 2 */
};

/* Sound requests written to v_soundControl. */
enum {
    SOUND_BASE_SONG = 0x82,
    SOUND_BIKE_SONG = 0x85,
    SOUND_PETICOPTER_SONG = 0x88,
    SOUND_DEAD = 0x89,
    SOUND_PUNCH = 0x8A,
    SOUND_COINS = 0x8E,
    SOUND_THROW = 0x90,
    SOUND_JUMP = 0x91,
    SOUND_SPLASH = 0x92,
    SOUND_FALLING = 0x9B,
    SOUND_FX_1 = 0xB1, /* landing, grabbing a ladder */
};

/* Entity types spawned by this module. */
enum {
    ENTITY_CAPSULE_A = 0x05,
    ENTITY_CAPSULE_B = 0x07,
    ENTITY_STATIC_SPRITE = 0x18,
    ENTITY_MONEY_BAG = 0x3C,
    ENTITY_GHOST = 0x4F,
    ENTITY_PUZZLE_REWARD = 0x52,
};

/* ---------------------------------------------------- sprite data (bank 2) */
/* Sprite descriptors: first byte = Alex tile set index, then the sprite list. */
enum {
    SPR_IDLE_LEFT = 0x90A7,
    SPR_IDLE_RIGHT = 0x90BC,
    SPR_CROUCH_LEFT = 0x8DA7,
    SPR_CROUCH_RIGHT = 0x8DBC,
    SPR_AIR_LEFT = 0x8F00,
    SPR_AIR_RIGHT = 0x8F15,
    SPR_PUNCH_LEFT = 0x8DD1,
    SPR_PUNCH_RIGHT = 0x8DE9,
    SPR_SWIM_PUNCH_LEFT = 0x8E49,
    SPR_SWIM_PUNCH_RIGHT = 0x8E5E,
    SPR_SWIM_LEFT = 0x8E01,
    SPR_SWIM_RIGHT = 0x8E25,
    SPR_MOTORCYCLE = 0x8F2A,
    SPR_MOTORCYCLE_JUMP = 0x8F60,
    SPR_BOAT = 0x9152,
    SPR_BOAT_JUMP = 0x9137,
    SPR_PETICOPTER_LEFT = 0x8F7B,
    SPR_PETICOPTER_RIGHT = 0x9011,
    SPR_DIVING = 0x9122,
    SPR_PETRIFIED = 0x90D1,
};

/* Animation descriptors: frame count, then one sprite descriptor per frame. */
enum {
    ANIM_JANKEN_DANCE = 0x8CE6,
    ANIM_WALK_LEFT = 0x8CEB,
    ANIM_WALK_RIGHT = 0x8CF4,
    ANIM_SWIM_LEFT = 0x8CFD,
    ANIM_SWIM_RIGHT = 0x8D02,
    ANIM_PETICOPTER_LEFT = 0x8D07,
    ANIM_PETICOPTER_RIGHT = 0x8D10,
    ANIM_MOTORCYCLE = 0x8D19,
    ANIM_BOAT = 0x8D1E,
    ANIM_DEAD = 0x8D23,
    ANIM_JANKEN_COUNT = 0x8D2A,
    ANIM_CLIMB = 0x9188,
};

/* ------------------------------------------------ unnamed RAM of this module */
#define RAM_ALEX_LOADED_TILES 0xC201   /* tile set index currently in VRAM */
#define RAM_METATILE_BANK _RAM_C203_   /* bank of nametableChangeSourceMetatile */
#define RAM_IN_WATER v_nametableEntryAttrLastThreeBits /* $20 when Alex's body is in water */
#define RAM_SPECIAL_TILE _RAM_C211_    /* pointer to the ladder/door/hatch tile touched */
#define RAM_PUZZLE_PROGRESS _RAM_C229_ /* level 17 floor puzzle: correct steps */
#define RAM_PUZZLE_LAST_COLOR _RAM_C22A_

/* ------------------------------------------------------------ call helpers */
/* Calls a routine through the original register ABI (inputs in `cpu`, outputs
 * read back from `cpu`). Only used for routines that return to their caller:
 * the emulated stack must be balanced afterwards. */
static inline void alex_call(GameFn fn) {
    uint16_t sp = cpu.sp;
    push16(0x0000);
    fn();
    if (cpu.sp != sp) rt_stack_error(0, sp);
}

/* The high byte of IX+d, which BIT n,(IX+d) copies into the undocumented X/Y
 * flags. */
#define ALEX_EA_HIGH 0xC3

/* Flags left by `or a` on a zero register (Z and P/V set). The terrain probes
 * save the caller's flags in F' (EX AF,AF'), so a few call sites hand them the
 * flags the original instruction sequence leaves; see physics.c. */
#define FLAGS_ZERO (FLAG_Z | FLAG_P)

/* Flags of CP v with A = a, without touching A. */
static inline uint8_t z80_cp_flags(uint8_t a, uint8_t v) {
    uint8_t saved = cpu.a;
    cpu.a = a;
    alu_cp(v);
    cpu.a = saved;
    return cpu.f;
}

/* Flags of AND/OR/XOR leaving `result`. */
static inline void z80_logic_flags(uint8_t result, bool is_and) {
    cpu.f = (uint8_t)(flag_szxyp(result) | (is_and ? FLAG_H : 0));
}

/* Bit tests that also set the flags like the Z80 BIT instruction, for the
 * code paths whose flags reach F' (see physics.c): z80_test_ix for
 * BIT n,(ix+d) on Alex's fields, z80_test for BIT n,r. */
static inline bool z80_test_ix(uint8_t value, uint8_t mask) {
    alu_bit(__builtin_ctz(mask), value, ALEX_EA_HIGH);
    return value & mask;
}

static inline bool z80_test(uint8_t value, uint8_t mask) {
    alu_bit(__builtin_ctz(mask), value, value);
    return value & mask;
}

/* ------------------------------------------------- module-internal routines */
/* graphics.c */
void alex_set_sprite(uint16_t descriptor);
void alex_animate(uint16_t animation);

/* physics.c: terrain probes (offsets packed as dy << 8 | dx from Alex's
 * top-left corner; each returns true when a solid tile is found) */
bool alex_probe_wall(uint16_t offset, uint8_t dy2, uint8_t extra_rows);
bool alex_probe_column(uint16_t offset, uint8_t dy2);
bool alex_probe_wall_offscreen(uint16_t offset);
bool alex_probe_row(uint16_t offset, uint8_t dx2);
bool alex_probe_row_offscreen(uint16_t offset);
bool alex_probe_further_right(uint8_t dx);
/* physics.c: vertical motion */
void alex_gravity(uint16_t head_offset);
void alex_check_ceiling(uint16_t head_offset);
void alex_land(uint8_t probe_y);
void alex_boat_gravity(uint16_t head_offset);
/* physics.c: horizontal motion */
void alex_accelerate_left(uint16_t accel, uint16_t max_speed);
void alex_friction_left_if_moving(uint16_t friction);
bool alex_friction_left(uint16_t friction);
void alex_stop(void);
void alex_set_x_speed(uint16_t speed);
void alex_turn_right(uint16_t brake);
void alex_accelerate_right(uint16_t accel, uint16_t max_speed);
void alex_friction_right_if_moving(uint16_t friction);
bool alex_friction_right(uint16_t friction);
void alex_turn_left(uint16_t brake);
void alex_slow_down_to(uint16_t decel, uint16_t min_speed);
/* physics.c: vertical motion when swimming/flying */
void alex_accelerate_up(uint16_t accel, uint16_t max_speed);
void alex_brake_rising(uint16_t brake);
void alex_accelerate_down(uint16_t accel, uint16_t max_speed);
void alex_brake_sinking(uint16_t brake);
void alex_tick_stun(void);

/* tiles.c */
void alex_interact_with_tile(uint16_t offset);
void alex_interact_with_floor(uint16_t offset);

/* states.c */
void alex_set_idle(void);
void alex_load_idle_sprite(void);
void alex_fall(void);
void alex_jump(void);
void alex_update_in_air(void);
void alex_crouch(void);
void alex_walk_left(void);
void alex_walk_right(void);
void alex_walk(void);

/* special.c */
void alex_start_climbing(void);
void alex_freeze_for_screen_flip(void);
void alex_save_copy(void);

/* swimming.c */
void alex_splash(void);
void alex_update_swimming(void);
void alex_clear_attacks(void);

/* vehicles.c */
void alex_lose_vehicle(void);
void alex_crash_vehicle(void);
void alex_wreck_boat(void);

/* actions.c */
void alex_handle_action(void);
bool alex_tick_punch(void);
void alex_break_block(uint16_t attr_pointer, uint16_t screen_yx);
void alex_request_background_tile(void);

/* camera.c */
void alex_update_camera(void);

#endif
