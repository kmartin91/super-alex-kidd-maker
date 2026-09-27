/*
 * Shared definitions of the "states" module: the game-state machine (title,
 * demo, level intro, gameplay, shop, map, text boxes, janken battles, bonus
 * levels, life lost / game over, ending).
 *
 * How the state machine works
 * ---------------------------
 * v_gameState holds the current state in its low nibble. The main loop
 * ($0045) calls gameStateMainLoopPointers[state] forever; the VBlank
 * interrupt calls gameStateInterruptHandlersPointers[state] when bit 3 of
 * v_interruptFlags is set (the value handed to waitForInterrupt).
 * Bit 7 (STATE_INITIALIZED) tells a handler that its screen is already set
 * up: a handler sees it clear, draws its screen, sets it, and from then on
 * only updates. Writing a bare state number (bit 7 clear) therefore means
 * "enter this state from scratch", and writing state | STATE_INITIALIZED
 * means "resume it" (e.g. $8A resumes gameplay after the shop or the map).
 *
 * Register contract of the main-loop handlers: the main loop loads
 * HL = v_gameState and runs EXX before jumping to the handler, and most
 * handlers start with EXX to get HL back. Whatever the handler leaves in
 * BC/DE (and BC') flows into the next handler through the same EXX pair,
 * so the lifted handlers leave those registers exactly as the original.
 */
#ifndef GAME_WIP_STATES_H
#define GAME_WIP_STATES_H

#include <stddef.h>

#include "game/lift.h"
#include "rt/maker.h"
#include "game/ram.h"
#include "game/vdp_io.h"

/* ------------------------------------------------------------ game states */
enum GameState {
    STATE_TITLE = 0x0,
    STATE_TITLE_FROM_DEMO = 0x1,
    STATE_DEMO = 0x2,
    STATE_LEVEL_STARTING = 0x3,
    STATE_LEVEL_COMPLETED = 0x4,
    STATE_SHOP = 0x5,
    STATE_LIFE_LOST = 0x6,
    STATE_TEXT_BOX = 0x7,
    STATE_BONUS_LEVEL = 0x8,
    STATE_JANKEN_GAME = 0x9,
    STATE_GAMEPLAY = 0xA,
    STATE_MAP = 0xB,
};
#define STATE_INITIALIZED 0x80 /* bit 7: the state's screen is set up */
#define STATE_SHOP_EXIT 0x40   /* bit 6 (shop only): Alex walked out of the shop */
#define STATE_NUMBER_MASK 0x0F

/* ---------------------------------------------------------------- sounds */
enum Sound {
    SOUND_INTRO = 0x81,
    SOUND_BASE_SONG = 0x82,
    SOUND_UNDERWATER_SONG = 0x83,
    SOUND_CASTLE_SONG = 0x84,
    SOUND_BIKE_SONG = 0x85,
    SOUND_LEVEL_STARTING = 0x86,
    SOUND_JANKEN_MUSIC = 0x87,
    SOUND_PETICOPTER_SONG = 0x88,
    SOUND_BOSS_HIT = 0x8D,
    SOUND_POWERUP = 0x8F,
    SOUND_BATTLE_LOST = 0x93,
    SOUND_TEXTBOX = 0x94,
    SOUND_BOSS_DEFEATED = 0x95,
    SOUND_BOSS_SHOT = 0x96,
    SOUND_MAGIC_CAPSULE_A = 0xAA,
    SOUND_MAGIC_CAPSULE_B = 0xAB,
    SOUND_JANKEN_COUNT = 0xAD,
    SOUND_JANKEN_THROW = 0xAE,
    SOUND_GAME_OVER_SONG = 0xAF,
    SOUND_ENDING_SONG = 0xB0,
    SOUND_FX_1 = 0xB1, /* played when a text box message is complete */
};

/* ------------------------------------------------------ Alex's states */
enum AlexState {
    ALEX_SPAWNING = 0x00,
    ALEX_IDLE = 0x01,
    ALEX_WALKING = 0x02,
    ALEX_IN_AIR = 0x03,
    ALEX_SWIMMING = 0x05,
    ALEX_FLYING_PETICOPTER = 0x06,
    ALEX_DEAD = 0x0F,
    ALEX_STATE_0x10 = 0x10,
    ALEX_BATTLE_COUNTING = 0x15,
    ALEX_BATTLE_GO_TO_POSITION = 0x16,
    ALEX_BATTLE_DANCING = 0x17,
    ALEX_BATTLE_THROW = 0x18,
    ALEX_BATTLE_STATUE = 0x19,
    ALEX_STATE_0x1A = 0x1A, /* set once Janken is defeated */
};

/* v_alexActionState: power-up / vehicle Alex currently has. */
enum AlexAction {
    ACTION_NONE = 0x00,
    ACTION_CANE_OF_FLIGHT = 0x01,      /* ALEX_C054_STATE_1 */
    ACTION_INVINCIBLE = 0x02,          /* teleport powder, respawn grace period */
    ACTION_MAGIC_CAPSULE_A = 0x03,
    ACTION_MAGIC_CAPSULE_B = 0x04,
    ACTION_POWER_BRACELET = 0x05,
    ACTION_TELEPATHY_BALL = 0x06,      /* ALEX_C054_UKN_0x06, set by using the telepathy ball */
    ACTION_RIDING_MOTORCYCLE = 0x07,
    ACTION_RIDING_BOAT = 0x08,
    ACTION_FLYING_PETICOPTER = 0x09,
};

/* --------------------------------------------------------------- entities */
#define ENTITY_SIZE 0x20
#define ENTITY_ARRAY_SIZE 0x1E
/* Slots are numbered from 1 like the reference disassembly: v_entities.1 is
 * Alex ($C300), v_entities.7 is $C3C0... v_mapEntities.1 is $CF80. */
#define ENTITY_SLOT(n) ((uint16_t)(v_entities + ((n) - 1) * ENTITY_SIZE))
#define MAP_ENTITY_SLOT(n) ((uint16_t)(v_mapEntities + ((n) - 1) * ENTITY_SIZE))

enum EntityType {
    ENTITY_ALEX = 0x01,
    ENTITY_THOUGHT_CLOUD_HAND = 0x0B, /* janken choice shown in a thought cloud */
    ENTITY_BATTLE_SCORE = 0x0C,       /* round results under the names */
    ENTITY_STATIC = 0x18,
    ENTITY_ITEM_SELECT_ARROW = 0x21,
    ENTITY_MAP_ARROW = 0x56,
    ENTITY_JANKENS_CASTLE = 0x58,
    ENTITY_ALEX_EATING_RICE_BALL = 0x62,
};

/* Pixel (high) byte of the 8.8 fixed-point position of an entity. */
#define X_PIXEL(e) (((uint8_t *)(e))[offsetof(Entity, xPos) + 1])
#define Y_PIXEL(e) (((uint8_t *)(e))[offsetof(Entity, yPos) + 1])

/* v_scrollFlags / v_levelScrollFlags bits. */
#define SCROLL_DOWN 0x01
#define SCROLL_UP 0x02
#define SCROLL_LEFT 0x04
#define SCROLL_RIGHT 0x08
#define SCROLL_ANY 0x0F
#define SCROLL_VERTICAL 0x80

/* v_inputFlags bit 5: a demo is playing (readInput leaves the joypad alone). */
#define INPUT_FLAG_DEMO 0x20

/* ------------------------------------------------------------- hardware */
#define MAPPER_SLOT2 0xFFFF
/* The game writes bank numbers to the mapper with bit 7 set. */
#define BANK(n) ((uint8_t)(0x80 | (n)))
static inline void map_bank(uint8_t value) { wr8(MAPPER_SLOT2, value); }

static inline void disable_interrupts(void) { cpu.iff1 = cpu.iff2 = 0; }
static inline void enable_interrupts(void) { cpu.iff1 = cpu.iff2 = 1; }

/* v_interruptFlags values handed to waitForInterrupt. */
#define IRQ_SPRITES 0x01          /* bit 0: update the sprite table */
#define IRQ_SPRITES_AND_STATE 0x09 /* bits 0 and 3: also run the state's interrupt handler */
#define IRQ_WAIT_ONLY 0x80        /* just wait: no sprite update, no state handler */

/* CALL_ROUTINE for C helpers of this module: the callee must return to us
 * (none of the routines called this way unwinds to its caller's caller). */
#define CALL_HELPER(fn)                                    \
    do {                                                   \
        uint16_t sp_ = cpu.sp;                             \
        push16(0x0000);                                    \
        fn();                                              \
        if (cpu.sp != sp_) rt_stack_error(0x0000, sp_);    \
    } while (0)

/* Waits for the next VBlank with the given v_interruptFlags. */
static inline void wait_frame(uint8_t irq_flags) {
    cpu.a = irq_flags;
    CALL_HELPER(f_waitForInterrupt);
}

/* VDP write command for the name-table entry at (column, row); the screen
 * name table is at VRAM $3800, 32 two-byte entries per row. */
#define NAMETABLE_WRITE(col, row) VDP_VRAM_WRITE(0x3800 + (row) * NAMETABLE_ROW_BYTES + (col) * 2)

/* destroyCurrentEntity on `count` consecutive slots from `first`. Leaves the
 * registers like the original loop (IX past the last slot, B = 0, DE = $20). */
static inline void destroy_entities(uint16_t first, uint8_t count) {
    /* Maker mode (rt/maker.h): all of the level's entities, extra slots too. */
    if (maker.active && first == v_entities && count == ENTITY_ARRAY_SIZE)
        for (uint16_t a = MAKER_EXTRA_RAM; a < MAKER_EXTRA_RAM + MAKER_EXTRA_SLOTS * ENTITY_SIZE; a++) ram8(a) = 0;
    cpu.ix = first;
    int n = count ? count : 256; /* DJNZ loop */
    for (int i = 0; i < n; i++) {
        CALL_HELPER(f_destroyCurrentEntity);
        cpu.ix = (uint16_t)(cpu.ix + ENTITY_SIZE);
    }
    cpu.b = 0;
    cpu.de = ENTITY_SIZE;
}

/* Main-loop handlers start with EXX (see the top of this file). */
static inline void enter_state_handler(void) { op_exx(); }

/* rst loadAthPointer ($0010): word entry `index` of the table at `table`.
 * Leaves BC = 2 * index (8-bit) like the original. */
static inline uint16_t load_ath_pointer(uint16_t table, uint8_t index) {
    uint8_t offset = (uint8_t)(index * 2);
    cpu.bc = offset;
    return rd16((uint16_t)(table + offset));
}

/* ------------------------------------------------------ per-level tables */
/* Tables indexed by v_level (1 = Mt Eternal ... 17 = Janken's castle). The
 * addresses are those of the entry for level 1 (bank 0 unless noted). */
#define LEVEL_SONGS 0x0DC5 /* 1 byte: song requested when the level (re)starts */
#define LEVEL_SPAWN_STATES 0x0E1F /* 1 byte: 0 on foot, 1 on the boat, else peticopter (7 motorbike: Maker levels) */

/* Address of the entry of `level` in a per-level table of `size`-byte
 * entries whose level-1 entry is at `table` (8-bit offset like the original). */
static inline uint16_t level_entry(uint16_t table, uint8_t level, uint8_t size) {
    return (uint16_t)(table - size + (uint8_t)(level * size));
}

/* levelSongs[level]; like the original lookup it leaves BC = level. */
static inline uint8_t level_song(uint8_t level) {
    cpu.bc = level;
    return rd8((uint16_t)(LEVEL_SONGS - 1 + level));
}

void maker_zone_transition(void); /* zone.c */

/* Copy `count` bytes of RAM/ROM like LDIR (source and destination may overlap
 * forwards, which is how the game clears areas: dst = src + 1). */
static inline void copy_bytes(uint16_t dst, uint16_t src, uint32_t count) {
    while (count--) wr8(dst++, rd8(src++));
}
static inline void fill_bytes(uint16_t dst, uint8_t value, uint32_t count) {
    while (count--) wr8(dst++, value);
}

#endif
