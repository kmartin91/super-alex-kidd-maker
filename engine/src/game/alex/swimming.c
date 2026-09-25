/*
 * Swimming ($3478-$36F0).
 *
 * Alex enters the water (splash) when the tile at his body is water. In the
 * water there is no gravity: the vertical direction flag (MOTION_DOWN) says
 * whether he sinks or rises.
 *  - Rising: drifts up at up to 1 px/frame (accel 1/16), 1.5 px/frame when
 *    UP is held (accel 1/8). DOWN brakes the rise (1/16) and turns him
 *    downwards. At the surface (no more water above his head) he sinks back
 *    at 0.5 px/frame.
 *  - Sinking: DOWN sinks faster (accel 1/16, up to 1 px/frame); otherwise
 *    the sinking is braked (by 1/8 with UP, 1/16 without) until he floats up.
 *    He stops on solid ground.
 *  - Horizontally: 1 px/frame (accel 1/16), 1.5 px/frame (accel 1/8) with
 *    button 1, friction 1/32; turning around brakes by 1/16 (1/8 with button
 *    1). Button 1 also doubles the animation speed.
 * A ladder tile 8 px above his head can be grabbed with UP.
 *
 * The flags left by each step can reach F' through the probes of the next
 * one (see physics.c), hence the z80_test() bit tests.
 */
#include "alex.h"

#define PROBE_BODY 0x080C
#define PROBE_FEET 0x110C
#define SPLASH_ANIMATION_DELAY 0x0A
#define STROKE_DELAY_SLOW 0x14
#define STROKE_DELAY_FAST 0x0A
#define SURFACE_SINK_SPEED 0x0080
#define LADDER_HOP_SPEED 0xF000   /* one-frame hop of 16 px onto the ladder */
#define TILE_SURFACE_EXIT 0x59    /* class $60 tile at the surface: UP there = hit */

/* $3478 clearEntities2to4AndMaybeReset0xC054: remove Alex's attack entities
 * (slots 2-4: fist projectiles, capsules), end his attack, and drop the item
 * in use unless he is invincible. Leaves HL = v_alexActionState. */
void alex_clear_attacks(void) {
    cpu.hl = 0xC320; /* v_entities.2 */
    alex_call(f_clearEntity);
    cpu.hl++;
    alex_call(f_clearEntity);
    cpu.hl++;
    alex_call(f_clearEntity);
    ALEX->unknown8 &= (uint8_t)~(ACTFLAG_PUNCHING | ACTFLAG_THROWING | ACTFLAG_ATTACK_BOX);
    cpu.hl = v_alexActionState;
    uint8_t action = ram8(v_alexActionState);
    cpu.f = z80_cp_flags(action, ACTION_INVINCIBLE);
    if (action == ACTION_INVINCIBLE) return;
    ram8(v_alexActionState) = ACTION_NONE;
    cpu.f = FLAGS_ZERO;
}

LIFTED(clearEntities2to4AndMaybeReset0xC054, 0x3478) {
    alex_clear_attacks();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------- vertical */

/* Stop the vertical motion against a solid tile; DOWN held (or not) gives
 * the new vertical direction when `down_sets` (else DOWN keeps it). */
static void stop_vertical(bool rising) {
    Entity *alex = ALEX;
    cpu.hl = 0;
    alex->ySpeed = 0;
    alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
    bool down = z80_test(ram8(v_inputData), PAD_DOWN);
    if (rising && down) alex->unknown3 |= MOTION_DOWN;          /* blocked above: DOWN sinks */
    if (!rising && !down) alex->unknown3 &= (uint8_t)~MOTION_DOWN; /* on the ground: float up */
}

/* Reached the surface; `compared` is the value the original tested (the
 * class of the tile above his head, or his head line when near the top). */
static void reach_surface(uint8_t compared) {
    Entity *alex = ALEX;
    if (compared == TILE_CLASS_DOOR) {
        cpu.hl--;
        uint8_t tile = rd8(cpu.hl);
        cpu.f = z80_cp_flags(tile, TILE_SURFACE_EXIT);
        if (tile == TILE_SURFACE_EXIT) {
            uint8_t up = ram8(v_inputData) & PAD_UP;
            z80_logic_flags(up, true);
            if (up) {
                alex->flags |= ENTITY_FLAG_HIT;
                return;
            }
        }
    }
    alex->unknown3 |= MOTION_DOWN | MOTION_VERTICAL;
    z80_logic_flags(alex->unknown3, false);
    cpu.hl = SURFACE_SINK_SPEED;
    alex->ySpeed = SURFACE_SINK_SPEED;
}

/* $355B: vertical swimming. */
static void swim_vertical(void) {
    Entity *alex = ALEX;
    if (z80_test_ix(alex->unknown3, MOTION_DOWN)) {
        /* Sinking: ground at y+15, x+3 / x+5, or x+21? */
        if (alex_probe_row(0x0F03, 0x02) || alex_probe_further_right(0x10)) {
            stop_vertical(false);
            return;
        }
        uint8_t held = ram8(v_inputData);
        if (z80_test(held, PAD_DOWN)) alex_accelerate_down(0x0010, 0x0100);
        else if (z80_test(held, PAD_UP)) alex_brake_sinking(0xFFE0);
        else alex_brake_sinking(0xFFF0);
        return;
    }
    /* Rising: at the top of the screen, or no water above: surface. */
    uint8_t head = add8_flags(HI(alex->yPos), HI(alex->ySpeed), 0);
    cpu.f = z80_cp_flags(head, 0x02);
    if (head < 0x02) {
        reach_surface(head);
        return;
    }
    cpu.de = 0x0103;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if (z80_test(cpu.a, TILE_SOLID)) {
        stop_vertical(true);
        return;
    }
    cpu.de = 0x010C;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if (z80_test(cpu.a, TILE_SOLID)) {
        stop_vertical(true);
        return;
    }
    uint8_t tile_class = cpu.a & TILE_CLASS_MASK;
    cpu.f = z80_cp_flags(tile_class, TILE_CLASS_WATER);
    if (tile_class != TILE_CLASS_WATER) {
        reach_surface(tile_class);
        return;
    }
    cpu.e = 0x09;
    alex_call(f__LABEL_7C94_);
    if (z80_test(rd8(cpu.hl), TILE_SOLID)) {
        stop_vertical(true);
        return;
    }
    uint8_t held = ram8(v_inputData);
    if (z80_test(held, PAD_DOWN)) {
        if (z80_test_ix(alex->unknown3, MOTION_VERTICAL)) alex_brake_rising(0x0010);
        else alex->unknown3 |= MOTION_DOWN;
    } else if (z80_test(held, PAD_UP)) {
        alex_accelerate_up(0xFFE0, 0xFE80);
    } else {
        alex_accelerate_up(0xFFF0, 0xFF00);
    }
}

LIFTED(sub_355B, 0x355B) {
    swim_vertical();
    LIFTED_RETURN();
}

/* ----------------------------------------------------------- horizontal */

/* $363E: horizontal swimming. */
static void swim_horizontal(void) {
    Entity *alex = ALEX;
    uint8_t held;
    if (z80_test_ix(alex->unknown3, MOTION_RIGHT)) {
        if (alex_probe_column(0x0317, 0x0A)) {
            alex_stop();
            if (ram8(v_inputData) & PAD_LEFT)
                alex->unknown3 &= (uint8_t)~(MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL);
            return;
        }
        held = ram8(v_inputData);
        if (held & PAD_RIGHT) {
            if (held & PAD_JUMP) alex_accelerate_right(0x0020, 0x0180);
            else alex_accelerate_right(0x0010, 0x0100);
        } else if (held & PAD_LEFT) {
            alex->unknown3 &= (uint8_t)~MOTION_FACING_RIGHT;
            alex_turn_left(held & PAD_JUMP ? 0xFFE0 : 0xFFF0);
        } else {
            alex_friction_right_if_moving(0xFFF8);
        }
        return;
    }
    if (alex_probe_column(0x0301, 0x0A)) {
        alex_stop();
        if (ram8(v_inputData) & PAD_RIGHT) alex->unknown3 |= MOTION_FACING_RIGHT | MOTION_RIGHT;
        return;
    }
    held = ram8(v_inputData);
    if (held & PAD_LEFT) {
        if (held & PAD_JUMP) alex_accelerate_left(0xFFE0, 0xFE80);
        else alex_accelerate_left(0xFFF0, 0xFF00);
    } else if (held & PAD_RIGHT) {
        alex->unknown3 |= MOTION_FACING_RIGHT;
        alex_turn_right(held & PAD_JUMP ? 0x0020 : 0x0010);
    } else {
        alex_friction_left_if_moving(0x0008);
    }
}

LIFTED(_LABEL_363E_, 0x363E) {
    swim_horizontal();
    LIFTED_RETURN();
}

/* ---------------------------------------------------------------- state */

/* $34B6 updateAlexSwiming */
void alex_update_swimming(void) {
    Entity *alex = ALEX;
    if (alex->unknown8 & ACTFLAG_STUNNED) {
        alex->ySpeed = 0;
        alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
        alex_tick_stun();
        return;
    }
    alex_interact_with_tile(PROBE_BODY);
    if (alex->state != ALEX_STATE_SWIMMING) return;
    if (HI(alex->yPos) >= 8) {
        /* ladder tile 8 px above the head, UP: hop onto it */
        cpu.d = (uint8_t)(HI(alex->yPos) - 8);
        cpu.e = (uint8_t)(HI(alex->xPos) + 0x0C);
        alex_call(f__LABEL_7C7A_);
        if ((cpu.a & TILE_CLASS_MASK) == TILE_CLASS_DOOR && (ram8(v_inputData) & PAD_UP)) {
            cpu.hl--;
            if (rd8(cpu.hl) == TILE_LADDER) {
                ram16(RAM_SPECIAL_TILE) = cpu.hl;
                alex->ySpeed = LADDER_HOP_SPEED;
                alex_start_climbing();
                return;
            }
        }
    }
    if (alex->unknown8 & ACTFLAG_PUNCHING) {
        if (alex_tick_punch()) alex_set_sprite(alex->unknown3 & MOTION_FACING_RIGHT ? SPR_SWIM_RIGHT : SPR_SWIM_LEFT);
    } else if (ram8(v_inputDataChanges) & PAD_ACTION) {
        alex_call(f__LABEL_44E2_); /* swimming punch */
    }
    alex_interact_with_floor(PROBE_FEET);
    swim_vertical();
    swim_horizontal();
    if (alex->unknown8 & ACTFLAG_PUNCHING) return;
    cpu.c = (ram8(v_inputData) & PAD_JUMP) ? STROKE_DELAY_FAST : STROKE_DELAY_SLOW;
    alex->animationTimerResetValue = cpu.c;
    alex_animate(alex->unknown3 & MOTION_FACING_RIGHT ? ANIM_SWIM_RIGHT : ANIM_SWIM_LEFT);
}

LIFTED(updateAlexSwiming, 0x34B6) {
    alex_update_swimming();
    LIFTED_RETURN();
}

/* $3498 splash: enter the water. The vertical direction follows the speed
 * Alex had (falling in: sinking). */
void alex_splash(void) {
    Entity *alex = ALEX;
    ram8(v_soundControl) = SOUND_SPLASH;
    alex->state = ALEX_STATE_SWIMMING;
    alex->animationTimerResetValue = SPLASH_ANIMATION_DELAY;
    alex_clear_attacks();
    alex->unknown3 |= MOTION_DOWN;
    if (alex->ySpeed & 0x8000) alex->unknown3 &= (uint8_t)~MOTION_DOWN;
    alex_update_swimming();
}

LIFTED(splash, 0x3498) {
    alex_splash();
    LIFTED_RETURN();
}
