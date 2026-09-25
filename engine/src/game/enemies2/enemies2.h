/*
 * Shared definitions for the enemies2 module: entity behaviours at $5901+
 * (items, level enemies, story characters) and the janken battle opponents
 * and bosses at $71A9-$7C7A.
 *
 * Only this module includes this header. Other modules' routines are called
 * through their f_<name> entry points with the original register ABI.
 */
#ifndef GAME_WIP_ENEMIES2_H
#define GAME_WIP_ENEMIES2_H

#include <stdbool.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"

/* ------------------------------------------------------------ entity slots */

/* Entity slot n, numbered like the reference (v_entities.1 = Alex, $C300). */
#define ENTITY_SLOT(n) ((uint16_t)(v_entities + ((n) - 1) * 0x20))

enum {
    SLOT_ALEX = ENTITY_SLOT(1),         /* $C300 */
    SLOT_OPPONENT = ENTITY_SLOT(6),     /* $C3A0 janken opponent / "always present" level entity */
    SLOT_BOSS_HEAD = ENTITY_SLOT(7),    /* $C3C0 boss head after a lost janken match */
    SLOT_BOSS_SPELL = ENTITY_SLOT(8),   /* $C3E0 Chokkinna's spell */
    SLOT_BLOCK_DEBRIS = ENTITY_SLOT(23),/* $C5C0 first debris piece of a broken block; score marks in battles */
    SLOT_DEBRIS_2 = ENTITY_SLOT(24),    /* $C5E0 the three other debris pieces follow */
    SLOT_THOUGHT_OPPONENT = ENTITY_SLOT(27), /* $C640 opponent thought cloud (Telepathy Ball) / spawned items */
    SLOT_THOUGHT_ALEX = ENTITY_SLOT(28),     /* $C660 Alex's thought cloud (janken choice preview) */
};

/* Entity.flags bits. */
enum {
    EF_INITIALIZED = 0x01,        /* the updater ran its one-time setup */
    EF_DESTROY_OFFSCREEN = 0x02,  /* the engine destroys the entity when it moves off screen
                                     (several updaters also use it as a private "phase 2" flag) */
    EF_HIT = 0x80,                /* set by the punch check when Alex's attack connects */
};

/* Entity types (Entity.type, index into the updater table at $2892). */
enum {
    ENTITY_GOOSEKA_HEAD = 0x0D,
    ENTITY_CHOKKINNA_HEAD = 0x0E,
    ENTITY_PARPLIN_HEAD = 0x0F,
    ENTITY_THOUGHT_CLOUD = 0x0B,
    ENTITY_SCORE_MARKS = 0x0C,
    ENTITY_CHOKKINNA_SPELL = 0x1A,
    ENTITY_DEBRIS_TOP_LEFT = 0x38,
    ENTITY_DEBRIS_BOTTOM_LEFT = 0x39,
    ENTITY_DEBRIS_TOP_RIGHT = 0x3A,
    ENTITY_DEBRIS_BOTTOM_RIGHT = 0x3B,
    ENTITY_FLAME_OR_SCORPION_LEFT = 0x3E,
    ENTITY_FLAME_OR_SCORPION_RIGHT = 0x3F,
    ENTITY_LIGHTNING_CLOUD_DRIFTING = 0x40,
    ENTITY_LIGHTNING_CLOUD_STRIKING = 0x41,
    ENTITY_DEFEATED = 0x43,       /* generic "enemy defeated" updater */
    ENTITY_RICE_BALL = 0x44,
    ENTITY_BULL_WALKING_LEFT = 0x46,
    ENTITY_BULL_KNOCKED_RIGHT = 0x47,
    ENTITY_BULL_WALKING_RIGHT = 0x48,
    ENTITY_BULL_KNOCKED_LEFT = 0x49,
    ENTITY_GHOST = 0x4F,
    ENTITY_STORY_ITEM = 0x52,
};

/* Alex states (Entity.state of slot 0). */
enum {
    ALEX_WALKING = 0x02,
    ALEX_IN_AIR = 0x03,
    ALEX_DEAD = 0x0F,
    ALEX_BATTLE_GO_TO_POSITION = 0x16,
};

/* Game states (v_gameState). */
enum {
    STATE_LEVEL_COMPLETED = 0x04,
    STATE_TEXT_BOX = 0x07,
    STATE_BONUS_LEVEL = 0x08,
};

/* Sound requests (v_soundControl). */
enum {
    SOUND_SMOKE_PUFF = 0x8B,
    SOUND_BOSS_HIT = 0x8D,        /* unnamed in the reference; played when a boss head is punched */
    SOUND_COINS = 0x8E,
    SOUND_POWERUP = 0x8F,
    SOUND_BOSS_DEFEATED = 0x95,
    SOUND_LIGHTNING = 0x9E,
    SOUND_STAR_BOX = 0xA3,
    SOUND_BOSS_HEAD = 0xAC,
};

/* Score table indexes for addScore (L). */
enum { SCORE_1000 = 4 * 3 };

/* Text box messages (v_textBoxMessageIndex). */
enum {
    TXT_BOSS_FIGHT = 0x0B,
    TXT_SAINT_NURARI = 0x0D,
    TXT_VILLAGE_ELDER = 0x0E,
    TXT_EGLE = 0x0F,
    TXT_KING_HIGH_STONE = 0x10,
    TXT_KING_HIGH_STONE_NO_LETTER = 0x11,
    TXT_PRINCESS_LORA = 0x14,
};

/* Scroll flags (v_scrollFlags). */
enum { SCROLL_ANY = 0x0F, SCROLL_VERTICAL = 0x80 };

/* Unnamed RAM used by this module (see docs/notes/enemies2.md). */
enum {
    v_storyEventCounter = _RAM_C07F_,  /* blocks broken / guard present, read by story entities */
    v_collectedItemFlags = _RAM_D800_, /* D800-D807: bit 0 = story item already collected */
    v_battleNametablePatchCount = _RAM_C218_,
    v_battleNametablePatches = _RAM_C219_,
    v_battleNameRowBackup = _RAM_C260_,   /* 46 bytes of the name row saved during a battle */
    v_scoreMarksSprite = _RAM_C2A0_,      /* RAM sprite descriptor of the round results marks */
    v_battleNameRow = _RAM_C908_,         /* name-table copy row where "ALEX" is written */
    v_thoughtCloudAlexArea = _RAM_CA08_,  /* name-table copy area under Alex's thought cloud */
    v_thoughtCloudOpponentArea = _RAM_CA2C_,
};

/* ----------------------------------------------------------------- helpers */

/* High byte (pixel part) of the 8.8 fixed-point positions. */
static inline uint8_t x_pixel(const Entity *e) { return (uint8_t)(e->xPos >> 8); }
static inline uint8_t y_pixel(const Entity *e) { return (uint8_t)(e->yPos >> 8); }
static inline void set_x_pixel(Entity *e, uint8_t px) { e->xPos = (uint16_t)((e->xPos & 0x00FF) | (px << 8)); }
static inline void set_y_pixel(Entity *e, uint8_t px) { e->yPos = (uint16_t)((e->yPos & 0x00FF) | (px << 8)); }

static inline bool is_offscreen(const Entity *e) { return e->isOffScreenFlags != 0; }

/* The game's "reverse direction": complements the high byte and negates the
 * low byte separately.
 * QUIRK: not a true 16-bit negation; a speed with a zero low byte comes out
 * 0x100 too small (e.g. $FF00 -> $0000, $0100 -> $FF00). */
static inline uint8_t reverse_x_speed(Entity *e) {
    uint8_t hi = (uint8_t)~(e->xSpeed >> 8);
    uint8_t lo = (uint8_t)(~e->xSpeed + 1);
    e->xSpeed = (uint16_t)((hi << 8) | lo);
    return lo;
}

/* ADD A,1 + DAA: BCD increment of a counter such as v_lives. */
static inline uint8_t bcd_increment(uint8_t v) {
    uint8_t sum = (uint8_t)(v + 1);
    uint8_t correction = 0;
    if ((sum & 0x0F) > 9 || (v & 0x0F) == 0x0F) correction |= 0x06;
    if (sum > 0x99 || v == 0xFF) correction |= 0x60;
    return (uint8_t)(sum + correction);
}

/* Word table in ROM, indexed like rst $10 (the index is doubled in 8 bits). */
static inline uint16_t rom_word_table(uint16_t table, uint8_t index) {
    return rd16((uint16_t)(table + (uint8_t)(index * 2)));
}

/* ldir between RAM/ROM addresses (non-overlapping blocks). */
static inline void copy_bytes(uint16_t dst, uint16_t src, uint16_t count) {
    while (count--) wr8(dst++, rd8(src++));
}

/* Tail jump to handleEntityAnimation with the given animation descriptor. */
#define ANIMATE(descriptor)                       \
    do {                                          \
        cpu.hl = (descriptor);                    \
        TAIL_CALL(f_handleEntityAnimation);       \
    } while (0)

/* CALL_ROUTINE for use inside static helpers: only for routines that always
 * return to their caller (no return-to-grandparent). */
static inline void call_leaf(GameFn fn) { CALL_ROUTINE(fn); }

/* Calls that report through the carry flag. */
static inline bool carry_set(void) { return (cpu.f & FLAG_C) != 0; }
static inline bool zero_set(void) { return (cpu.f & FLAG_Z) != 0; }

#endif
