/*
 * Core module: bank-0 engine services shared by the whole game (interrupts,
 * VDP set-up and sprite upload, tile decompression, input, BCD score/money,
 * the entity framework and the entity collision helpers).
 *
 * Constants below are named after their meaning in the game; the reference
 * disassembly name is given when it differs.
 */
#ifndef GAME_WIP_CORE_H
#define GAME_WIP_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"
#include "game/vdp_io.h"

/* ------------------------------------------------------------ hardware */

#define MAPPER_SLOT2 0xFFFF       /* Sega mapper: bank in slot 2 ($8000-$BFFF) */
#define SLOT2_BANK2 0x82          /* bank 2: sound engine, sprite descriptors, hitboxes */

#define PORT_JOYPAD1 0xDC         /* player 1 pad, active low */
#define PORT_JOYPAD2 0xDD         /* player 2 pad and reset button, active low */
#define PORT_KEYBOARD_ROW 0xDE    /* SC-3000/SK-1100 keyboard PPI: row select */
#define PORT_KEYBOARD_CONTROL 0xDF

#define RESET_BUTTON 0x10         /* bit of PORT_JOYPAD2 */

/* VDP register 1 bits. */
#define VDP_R1_DISPLAY_VISIBLE 0x40

/* VRAM layout. */
#define VRAM_NAME_TABLE 0x3800
#define VRAM_SPRITE_TABLE 0x3F00  /* 64 Y bytes, then 64 (X, tile) pairs at +$80 */
#define SPRITE_LIST_END 0xD0      /* a sprite Y of $D0 ends the sprite list */
#define SPRITE_HIDDEN_Y 0xE0      /* below the 192-line screen */

/* CRAM entry of Alex's clothes (sprite palette colour 4) and their normal red. */
#define CRAM_ALEX_CLOTHES 0x14
#define ALEX_CLOTHES_RED 0x03

/* ---------------------------------------------------------- game state */

/* v_gameState: low nibble = state, bit 7 = state initialised. */
#define GAME_STATE_MASK 0x0F
#define GAME_STATE_READY 0x80
enum {
    STATE_TITLE = 0x0, STATE_TITLE_FROM_DEMO = 0x1, STATE_DEMO = 0x2, STATE_LEVEL_STARTING = 0x3,
    STATE_LEVEL_COMPLETED = 0x4, STATE_SHOP = 0x5, STATE_LIFE_LOST = 0x6, STATE_TEXT_BOX = 0x7,
    STATE_BONUS_LEVEL = 0x8, STATE_JANKEN_GAME = 0x9, STATE_GAMEPLAY = 0xA, STATE_MAP = 0xB,
};

/* v_interruptFlags: work requested from the next VBlank interrupt. The
 * interrupt clears the byte, which is what waitForInterrupt waits for. */
#define IRQ_UPLOAD_SPRITES 0x01     /* copy the RAM sprite table to VRAM */
#define IRQ_RUN_STATE_HANDLER 0x08  /* run the game state's interrupt handler */

/* v_inputFlags ($C005). */
#define INPUT_FROM_KEYBOARD 0x01    /* SC-3000 keyboard detected: read it instead of the pad */
#define INPUT_DEMO_PLAYING 0x20     /* set while the attract-mode demo runs: no score or money */

/* v_soundControl command that cuts the current sound effect. */
#define SOUND_FX_CUT 0xB2

/* ROM tables read by this module (never copied: always read with rd8/rd16). */
#define GAME_STATE_IRQ_HANDLERS 0x0127  /* word per game state */
#define INITIAL_VDP_REGISTER_WRITES 0x027D
#define MONEY_BAG_VALUES 0x0483          /* 2 x 3 BCD bytes */
#define SCORE_VALUES 0x0489              /* 8 x 3 BCD bytes */
#define ENTITY_UPDATERS 0x2890           /* word per entity type, entry 0 unused */
#define ENTITY_POINTS 0x576F             /* score index per entity type, from type 1 */
#define HITBOXES 0x91D0                  /* bank 2: 4-byte hitboxes */
#define ALEX_HIT_CHECKERS 0x7D15         /* word per Alex action state */
#define ALEX_DAMAGE_HANDLERS 0x7DA1      /* word per Alex action state */
#define ONE_BPP_FONT 0xB305              /* bank 2: 1bpp characters (unused loader) */

/* ------------------------------------------------------------- entities */

#define ENTITY_SIZE 0x20
#define ENTITY_SLOTS 30                    /* ENTITY_ARRAY_SIZE */
/* Slot n (1-based) as named in the reference (v_entities.n); slot 1 is Alex. */
#define ENTITY_SLOT(n) ((uint16_t)(v_entities + ((n) - 1) * ENTITY_SIZE))

/* Entity.flags bits. */
#define ENTITY_INITIALISED 0x01         /* cleared on spawn, set by the updater's init */
#define ENTITY_DIES_OFFSCREEN 0x02      /* destroyed instead of wrapping to the next screen */
#define ENTITY_HIT 0x80                 /* touched by a collision check (see collision.c) */

/* Alex (slot 1). */
enum {
    ALEX_SWIMMING = 0x05,
    ALEX_DEAD = 0x0F,
};
#define ALEX_PUNCHING 0x01              /* Entity.unknown8 bit (ALEX_UKNW8_PUNCH) */

/* v_alexActionState: the item or vehicle in use (set from the pause map's
 * inventory or the shop). */
enum {
    ACTION_NORMAL = 0,
    ACTION_CANE_OF_FLIGHT = 1,          /* ALEX_C054_STATE_1, timed */
    ACTION_TELEPORT_POWDER = 2,         /* ALEX_C054_INVINCIBLE, timed: cannot be hurt */
    ACTION_MAGIC_CAPSULE_A = 3,
    ACTION_MAGIC_CAPSULE_B = 4,
    ACTION_POWER_BRACELET = 5,
    ACTION_TELEPATHY_BALL = 6,          /* ALEX_C054_UKN_0x06 */
    ACTION_RIDING_MOTORCYCLE = 7,
    ACTION_RIDING_BOAT = 8,
    ACTION_FLYING_PETICOPTER = 9,
};

/* ---------------------------------------------------- shadow-test limits */

/* waitForInterrupt (every frame goes through it) and the rst $20 dispatcher
 * jumpToAthPointer/jumpToPointerAtA (the main loop runs every game state
 * through it) cannot be registered with the standard shadow harness: every
 * frame would then start inside a comparison session, and the harness only
 * stops between sessions, so it would never end. They are compiled in the
 * game and lockstep builds, and in shadow builds only when
 * CORE_SHADOW_FRAME_ROUTINES is defined (private harness that stops at the
 * frame limit regardless, see docs/notes/core.md). */
#if !defined(RT_SHADOW) || defined(CORE_SHADOW_FRAME_ROUTINES)
#define CORE_LIFT_FRAME_ROUTINES 1
#else
#define CORE_LIFT_FRAME_ROUTINES 0
#endif

/* ------------------------------------------------ helpers shared by files */

/* Z80 "inc l": advance the low byte of an address only (wraps in its page). */
static inline uint16_t core_inc_low(uint16_t addr, uint8_t n) {
    return (uint16_t)((addr & 0xFF00) | (uint8_t)(addr + n));
}

/* Sets the carry flag, leaving the other flags as they are. */
static inline void core_set_carry(bool carry) {
    cpu.f = (uint8_t)((cpu.f & ~FLAG_C) | (carry ? FLAG_C : 0));
}

/* score.c */
void core_add_score(uint8_t score_index);

/* entities.c */
void core_clear_entity(uint16_t slot);

#endif
