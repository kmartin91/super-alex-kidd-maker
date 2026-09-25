/*
 * Alex's other states ($3180-$3477, $38C2-$39DA): shop doors, floor hatches,
 * ladders, the screen-flip freeze, the Cane of Flight, diving after a vehicle
 * crash, and the janken (rock-paper-scissors) battle.
 */
#include <stddef.h>

#include "alex.h"

#define DOOR_WALK_SPEED 0x0080
#define DOOR_CROSSING_FRAMES 0x21
#define NAMETABLE_CHANGE_SHOP_DOOR 0x81
#define NAMETABLE_CHANGE_HATCH 0x82
#define HATCH_DESCENT_FRAMES 0x40
#define HATCH_FALL_SPEED 0x00D0
#define HATCH_SCROLL_SPEED 0x0300
#define CLIMB_SPEED 0x0100
#define CLIMB_ANIMATION_DELAY 4
#define LADDER_JUMP_SPEED 0x0100
#define CANE_SPEED 0x0100
#define CANE_BOB_SPEED 0x0080
#define DIVE_SPEED 0x0300
#define AUTO_WALK_SPEED 0x0180
#define JANKEN_POSITION_X 0x28
#define JANKEN_HANDS 3
#define JANKEN_HAND_SPRITES 0x395B /* bank 0: rock, scissors, paper sprite descriptors */
#define JANKEN_OPPONENT_DECISION 0xC677 /* v_entities.28.battleDecision */

#define STATE_SHOP 0x05
#define STATE_SHOP_FROM_SHOP 0x85 /* STATE_CHANGED | STATE_SHOP */
#define STATE_BACK_FROM_SHOP 0xC5
#define SCROLL_DOWN 0x01
#define SCROLL_ANY 0x0F

/* The door/hatch column: v_shopDoorOffset relative to the screen. */
static uint8_t door_x(void) { return (uint8_t)(ram8(v_shopDoorOffset) + ram8(v_horizontalScroll + 1)); }

/* ------------------------------------------------------------ shop doors */

/* $31C0: walk left (into the door, or back towards it). */
static void walk_left_to_door(void) {
    ALEX->xSpeed = (uint16_t)-DOOR_WALK_SPEED;
    alex_animate(ANIM_WALK_LEFT);
}

LIFTED(sub_31C0, 0x31C0) {
    walk_left_to_door();
    LIFTED_RETURN();
}

/* $3180 updateAlexReachingDoor: walk to the door column (UP was pressed in
 * front of a door), then cross it. */
LIFTED(updateAlexReachingDoor, 0x3180) {
    Entity *alex = ALEX;
    uint8_t target = door_x();
    if (target == HI(alex->xPos)) {
        alex->state = ALEX_STATE_CROSSING_DOOR;
        alex->stateTimer = DOOR_CROSSING_FRAMES;
        ram8(v_nametableChangeRequest) = NAMETABLE_CHANGE_SHOP_DOOR; /* draw Alex behind the door */
    } else if (target < HI(alex->xPos)) {
        walk_left_to_door();
    } else {
        alex->xSpeed = DOOR_WALK_SPEED;
        alex_animate(ANIM_WALK_RIGHT);
    }
    LIFTED_RETURN();
}

/* $31A8 updateAlexCrossingDoor: walk through for 33 frames, then enter the
 * shop (or leave it). */
LIFTED(updateAlexCrossingDoor, 0x31A8) {
    Entity *alex = ALEX;
    alex->stateTimer = alu_dec(alex->stateTimer);
    if (alex->stateTimer) {
        walk_left_to_door();
        LIFTED_RETURN();
    }
    alex_stop();
    cpu.hl = v_gameState;
    ram8(v_gameState) = ram8(v_gameState) == STATE_SHOP_FROM_SHOP ? STATE_BACK_FROM_SHOP : STATE_SHOP;
    LIFTED_RETURN();
}

/* --------------------------------------------------------------- hatches */

/* $31CC (state $11): swim to the hatch column, then go down through it:
 * the level scrolls down one screen (state $12). */
LIFTED(alexHandler_31CC, 0x31CC) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    uint8_t target = door_x();
    if (target == HI(alex->xPos)) {
        alex->state = ALEX_STATE_DOWN_HATCH;
        alex->stateTimer = HATCH_DESCENT_FRAMES;
        ram8(v_nametableChangeRequest) = NAMETABLE_CHANGE_HATCH;
        alex->ySpeed = HATCH_FALL_SPEED;
        alex_stop();
        push16(cpu.ix);
        alex_call(f__LABEL_6671_); /* clear the entities, prepare the scroll */
        cpu.ix = pop16();
        cpu.hl = v_scrollFlags;
        ram8(v_scrollFlags) |= SCROLL_DOWN;
    } else if (target < HI(alex->xPos)) {
        alex->xSpeed = (uint16_t)-DOOR_WALK_SPEED;
        alex->unknown3 &= (uint8_t)~MOTION_RIGHT;
        alex_animate(ANIM_SWIM_LEFT);
    } else {
        alex->xSpeed = DOOR_WALK_SPEED;
        alex->unknown3 |= MOTION_RIGHT;
        alex_animate(ANIM_SWIM_RIGHT);
    }
    LIFTED_RETURN();
}

/* $3223 (state $12): scroll down for 64 frames, then swim. */
LIFTED(alexHandler_3223, 0x3223) {
    Entity *alex = ALEX;
    ram16(v_verticalScrollSpeed) = HATCH_SCROLL_SPEED;
    alex->stateTimer = alu_dec(alex->stateTimer);
    if (!alex->stateTimer) alex_splash();
    LIFTED_RETURN();
}

/* --------------------------------------------------------------- ladders */

/* $3230: grab the ladder whose tile RAM_SPECIAL_TILE points at: Alex is
 * aligned on its 16-px column. Leaves HL = &unknown3. */
void alex_start_climbing(void) {
    Entity *alex = ALEX;
    HI(alex->xPos) = (uint8_t)((ram8(RAM_SPECIAL_TILE) << 2) & 0xF0);
    alex_stop();
    alex->unknown3 &= MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_DOWN | 0x20;
    z80_logic_flags(alex->unknown3, true);
    alex->unknown3 &= (uint8_t)~MOTION_HORIZONTAL;
    alex->animationTimerResetValue = CLIMB_ANIMATION_DELAY;
    alex->state = ALEX_STATE_CLIMBING;
    ram8(v_soundControl) = SOUND_FX_1;
    cpu.hl = v_alex + 0x14;
}

LIFTED(_LABEL_3230_, 0x3230) {
    alex_start_climbing();
    LIFTED_RETURN();
}

/* The tile byte of the name table entry whose attribute is at `offset` from
 * Alex (HL is left on it); `solid` = its attribute has bit 7. */
static uint8_t tile_at(uint16_t offset, bool *solid) {
    cpu.de = offset;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if (solid) *solid = cpu.a & TILE_SOLID;
    cpu.hl--;
    return rd8(cpu.hl);
}

static void climb(uint16_t speed, bool down) {
    Entity *alex = ALEX;
    if (down) alex->unknown3 |= MOTION_DOWN;
    else alex->unknown3 &= (uint8_t)~MOTION_DOWN;
    alex->ySpeed = speed;
    alex_animate(ANIM_CLIMB);
}

/* Jump off the ladder sideways (1 px/frame) if nothing is in the way. The
 * original loads the probe offset in two steps and the second overwrites dx:
 * QUIRK, both sides probe at dx = 2. */
static void jump_off_ladder(uint16_t speed, uint8_t motion) {
    Entity *alex = ALEX;
    if (alex_probe_wall(0x0702, 0x08, 1)) return;
    alex->xSpeed = speed;
    alex->unknown3 = motion;
    alex_fall();
}

/* $3256 (state $0A): on a ladder. UP/DOWN climb at 1 px/frame (Alex leaves
 * the ladder when his feet reach solid ground), LEFT/RIGHT jump off. The pad
 * is decoded with RRCA like the original (the flags reach F'). */
LIFTED(alexHandler_3256, 0x3256) {
    Entity *alex = ALEX;
    alex_stop();
    alex->ySpeed = 0;
    cpu.a = ram8(v_inputData);
    op_rrca();
    if (cpu.f & FLAG_C) {
        /* UP */
        if (HI(alex->isOffScreenFlags)) LIFTED_RETURN();
        bool solid;
        uint8_t head = tile_at(0x0008, &solid);
        if (solid && head != TILE_LADDER) LIFTED_RETURN(); /* head against a solid tile */
        if (tile_at(0x0C08, NULL) != TILE_LADDER) {
            uint8_t feet = tile_at(0x1908, &solid);
            if (!solid) LIFTED_RETURN();
            if (feet != TILE_LADDER) {
                alex_set_idle(); /* reached the floor above */
                LIFTED_RETURN();
            }
        }
        climb((uint16_t)-CLIMB_SPEED, false);
        LIFTED_RETURN();
    }
    op_rrca();
    if (cpu.f & FLAG_C) {
        /* DOWN */
        if (tile_at(0x0C08, NULL) == TILE_LADDER) {
            if ((uint8_t)(HI(alex->yPos) + 0x18) < 0xC0) {
                bool solid;
                uint8_t feet = tile_at(0x1808, &solid);
                if (solid && feet != TILE_LADDER) {
                    alex_set_idle(); /* reached the floor */
                    LIFTED_RETURN();
                }
            }
        } else if (tile_at(0x1808, NULL) != TILE_LADDER) {
            alex_fall(); /* below the bottom of the ladder */
            LIFTED_RETURN();
        }
        climb(CLIMB_SPEED, true);
        LIFTED_RETURN();
    }
    op_rrca();
    if (cpu.f & FLAG_C) {
        jump_off_ladder((uint16_t)-LADDER_JUMP_SPEED, MOTION_HORIZONTAL); /* LEFT */
        LIFTED_RETURN();
    }
    op_rrca();
    if (cpu.f & FLAG_C) {
        /* RIGHT */
        jump_off_ladder(LADDER_JUMP_SPEED, MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL);
    }
    LIFTED_RETURN();
}

/* ----------------------------------------------------------- screen flip */

/* $3320: freeze Alex (state $10) while the screen scrolls by one screen;
 * his speeds and state are saved. */
void alex_freeze_for_screen_flip(void) {
    Entity *alex = ALEX;
    ram16(v_alexVerticalSpeedTemporaryCopy) = alex->ySpeed;
    ram16(v_alexHorizontalSpeedTemporaryCopy) = alex->xSpeed;
    alex->xSpeed = 0;
    alex->ySpeed = 0;
    ram8(v_alexStateTemporaryCopy) = alex->state;
    alex->state = ALEX_STATE_SCREEN_FLIP;
}

LIFTED(_LABEL_3320_, 0x3320) {
    alex_freeze_for_screen_flip();
    LIFTED_RETURN();
}

/* $335F saveTempAlexCopy: snapshot Alex's entity (32 bytes). */
void alex_save_copy(void) {
    for (int i = 0; i < 0x20; i++) ram8(temporaryAlexCopy + i) = ram8(v_alex + i);
    ram8(_RAM_C25C_) = 0;
}

LIFTED(saveTempAlexCopy, 0x335F) {
    alex_save_copy();
    LIFTED_RETURN();
}

/* $3340 (state $10): when the scroll is over, restore the speeds and the
 * state (walking in from the left becomes idle), and snapshot Alex. */
LIFTED(alexHandler_3340, 0x3340) {
    Entity *alex = ALEX;
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    alex->ySpeed = ram16(v_alexVerticalSpeedTemporaryCopy);
    alex->xSpeed = ram16(v_alexHorizontalSpeedTemporaryCopy);
    alex->state = ram8(v_alexStateTemporaryCopy);
    if (alex->state == ALEX_STATE_AUTO_WALK) alex_set_idle();
    alex_save_copy();
    LIFTED_RETURN();
}

/* -------------------------------------------------------- cane of flight */

/* The four moves of the Cane of Flight: each checks the terrain first. They
 * keep BC (the pad in C) and leave the flags the next pad test inherits. */

/* $33DC: UP */
static void cane_up(void) {
    Entity *alex = ALEX;
    uint16_t bc = cpu.bc;
    bool ceiling = alex_probe_row(0x0104, 0x0E);
    cpu.bc = bc;
    if (ceiling) return;
    cpu.f = z80_cp_flags(HI(alex->yPos), 0x04);
    if (HI(alex->yPos) < 0x04) return;
    alex->ySpeed = (uint16_t)-CANE_SPEED;
    alex->unknown3 &= (uint8_t)~(MOTION_DOWN | MOTION_FALLING);
    alex->unknown3 |= MOTION_VERTICAL;
}

/* $3400: DOWN */
static void cane_down(void) {
    Entity *alex = ALEX;
    uint16_t bc = cpu.bc;
    bool ground = alex_probe_row(0x1904, 0x08);
    cpu.bc = bc;
    if (ground) return;
    cpu.f = z80_cp_flags(HI(alex->yPos), 0x98);
    if (HI(alex->yPos) >= 0x98) return;
    alex->ySpeed = CANE_SPEED;
    alex->unknown3 |= MOTION_DOWN | MOTION_FALLING | MOTION_VERTICAL;
}

/* $3424: LEFT */
static void cane_left(void) {
    Entity *alex = ALEX;
    uint16_t bc = cpu.bc;
    bool wall = alex_probe_wall(0x0102, 0x0E, 1);
    cpu.bc = bc;
    if (wall) return;
    alex->unknown3 = (uint8_t)((alex->unknown3 & ~(MOTION_FACING_RIGHT | MOTION_RIGHT)) | MOTION_HORIZONTAL);
    z80_logic_flags(alex->unknown3, false);
    alex->xSpeed = (uint16_t)-CANE_SPEED;
}

/* $3442: RIGHT */
static void cane_right(void) {
    Entity *alex = ALEX;
    uint16_t bc = cpu.bc;
    bool wall = alex_probe_wall(0x010E, 0x0E, 1);
    cpu.bc = bc;
    if (wall) return;
    alex->unknown3 |= MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL;
    z80_logic_flags(alex->unknown3, false);
    alex->xSpeed = CANE_SPEED;
}

LIFTED(sub_33DC, 0x33DC) {
    cane_up();
    LIFTED_RETURN();
}
LIFTED(_LABEL_3400_, 0x3400) {
    cane_down();
    LIFTED_RETURN();
}
LIFTED(_LABEL_3424_, 0x3424) {
    cane_left();
    LIFTED_RETURN();
}
LIFTED(_LABEL_3442_, 0x3442) {
    cane_right();
    LIFTED_RETURN();
}

static void cane_idle_sprite(void) {
    alex_set_sprite(ALEX->unknown3 & MOTION_RIGHT ? SPR_IDLE_RIGHT : SPR_IDLE_LEFT);
}

/* $336F (state 7): floating with the Cane of Flight (action state 1): the
 * pad moves Alex at 1 px/frame in any direction, and he bobs up and down
 * (+-0.5 px/frame, changing every 4 frames). Without the cane he falls. */
LIFTED(alexHandler_336F, 0x336F) {
    Entity *alex = ALEX;
    alex_stop();
    alex->ySpeed = 0;
    alex->unknown3 &= (uint8_t)~(MOTION_VERTICAL | MOTION_HORIZONTAL);
    alex_interact_with_tile(0x0C08);
    uint8_t action = ram8(v_alexActionState);
    cpu.f = z80_cp_flags(action, ACTION_CANE_OF_FLIGHT);
    if (action != ACTION_CANE_OF_FLIGHT) {
        alex->unknown3 &= (uint8_t)~(MOTION_DOWN | MOTION_VERTICAL);
        alex_fall();
        LIFTED_RETURN();
    }
    if (alex->unknown8 & ACTFLAG_PUNCHING) {
        if (alex_tick_punch()) cane_idle_sprite();
        LIFTED_RETURN();
    }
    if (z80_test(ram8(v_inputDataChanges), PAD_ACTION)) {
        alex_handle_action();
        LIFTED_RETURN();
    }
    uint8_t held = ram8(v_inputData);
    cpu.c = held;
    if (z80_test(held, PAD_UP)) cane_up();
    if (z80_test(held, PAD_DOWN)) cane_down();
    if (z80_test(held, PAD_LEFT)) cane_left();
    if (z80_test(held, PAD_RIGHT)) cane_right();
    alex->stateTimer++;
    uint16_t bob = (alex->stateTimer & 0x04) ? (uint16_t)-CANE_BOB_SPEED : CANE_BOB_SPEED;
    alex->ySpeed = (uint16_t)(alex->ySpeed + bob);
    cane_idle_sprite();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ misc states */

/* $3468 updateAutoWalkingRight (state $14): walk in from the left of the
 * screen at 1.5 px/frame (the camera code ends it). */
LIFTED(updateAutoWalkingRight, 0x3468) {
    ALEX->xSpeed = AUTO_WALK_SPEED;
    ALEX->unknown3 |= MOTION_HORIZONTAL;
    alex_animate(ANIM_WALK_RIGHT);
    LIFTED_RETURN();
}

/* $38C2 (state $1B): falling after a vehicle crash (the camera code of
 * levels 5 and 9 turns it into diving). */
LIFTED(alexHandler_38C2, 0x38C2) {
    alex_stop();
    LIFTED_RETURN();
}

/* $38C5 (state $13): diving: fall at 3 px/frame until the ground; once the
 * scroll is over, swim when the head is in water. */
LIFTED(alexHandler_38C5, 0x38C5) {
    Entity *alex = ALEX;
    alex_stop();
    alex_set_sprite(SPR_DIVING);
    /* the flags of the state dispatch are handed to the probe (F') */
    uint16_t speed = DIVE_SPEED;
    if (alex_probe_row(0x1904, 0x08) && HI(alex->yPos) >= 0x04) speed = 0;
    alex->ySpeed = speed;
    alex->unknown8 = 0;
    cpu.hl = _RAM_C20B_;
    if ((ram8(v_scrollFlags) & SCROLL_DOWN) | ram8(_RAM_C20B_)) LIFTED_RETURN();
    cpu.de = 0x0008;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if ((cpu.a & TILE_CLASS_MASK) == TILE_CLASS_WATER) alex_splash();
    LIFTED_RETURN();
}

/* --------------------------------------------------------------- janken */

/* $3928: UP/DOWN (just pressed) cycle through the hands 0-2. */
static void choose_hand(void) {
    Entity *alex = ALEX;
    uint8_t pressed = ram8(v_inputDataChanges);
    if (pressed & PAD_UP) {
        alex->battleDecision = alu_dec(alex->battleDecision);
        if (alex->battleDecision & 0x80) alex->battleDecision = JANKEN_HANDS - 1;
    } else if (pressed & PAD_DOWN) {
        alex->battleDecision = alu_inc(alex->battleDecision);
        if (alex->battleDecision >= JANKEN_HANDS) alex->battleDecision = 0;
    }
}

LIFTED(_LABEL_3928_, 0x3928) {
    choose_hand();
    LIFTED_RETURN();
}

/* $3919 (state $15): "jan, ken, pon" count: choose a hand. The opponent
 * (slot 28) mirrors the choice in its battleDecision. */
LIFTED(alexHandler_3919, 0x3919) {
    choose_hand();
    ram8(JANKEN_OPPONENT_DECISION) = ALEX->battleDecision;
    alex_animate(ANIM_JANKEN_COUNT);
    LIFTED_RETURN();
}

/* $39A5 (state $17): waiting for the count: choose a hand. */
LIFTED(alexHandler_39A5, 0x39A5) {
    choose_hand();
    ram8(JANKEN_OPPONENT_DECISION) = ALEX->battleDecision;
    alex_animate(ANIM_JANKEN_DANCE);
    LIFTED_RETURN();
}

/* $3949 (state $18): show the chosen hand. */
LIFTED(alexHandler_3949, 0x3949) {
    uint8_t hand = entity_at(cpu.ix)->battleDecision;
    cpu.de = (uint8_t)(hand * 2);
    alex_set_sprite(rd16((uint16_t)(JANKEN_HAND_SPRITES + (uint8_t)(hand * 2))));
    LIFTED_RETURN();
}

/* $3961 (state $16): walk to x = $28, then wait (state $17). */
LIFTED(alexHandler_3961, 0x3961) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    uint8_t x = HI(alex->xPos);
    if (x == JANKEN_POSITION_X) {
        alex_stop();
        alex->ySpeed = 0;
        alex->state = ALEX_STATE_JANKEN_DANCE;
        /* QUIRK: loads the byte at ROM address $000A (inside setVdpAddress)
         * instead of the constant $0A. */
        alex->unknown3 = rd8(0x000A);
        alex_save_copy();
        ram8(_RAM_C25A_) = 0x01;
        alex->animationTimerResetValue = 0x14;
    } else if (x < JANKEN_POSITION_X) {
        alex->xSpeed = 0x0100;
        alex_animate(ANIM_WALK_RIGHT);
    } else {
        alex->xSpeed = 0xFF00;
        alex_animate(ANIM_WALK_LEFT);
    }
    LIFTED_RETURN();
}

/* $39B4 (state $19): lost the janken: turned to stone. Falls to the ground
 * (DE, the x offset of the landing probe while off screen, is still the
 * dispatch table offset: QUIRK), then after unknown6 frames Alex is "hit"
 * and dies. */
LIFTED(alexHandler_39B4, 0x39B4) {
    Entity *alex = ALEX;
    uint16_t de = cpu.de;
    alex_stop();
    if (!(alex->unknown3 & MOTION_LANDED)) alex_gravity(de);
    else alex->ySpeed = 0;
    alex_set_sprite(SPR_PETRIFIED);
    alex->unknown6 = alu_dec(alex->unknown6);
    if (!alex->unknown6) alex->flags |= ENTITY_FLAG_HIT;
    LIFTED_RETURN();
}

/* $39D4 (state $1A): held still by the $60 event entity. */
LIFTED(alexHandler_39D4, 0x39D4) {
    alex_stop();
    ALEX->ySpeed = 0;
    LIFTED_RETURN();
}
