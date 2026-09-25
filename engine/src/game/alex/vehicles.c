/*
 * Vehicles: the Sukopako motorcycle, the boat and the Peticopter
 * ($2FA7-$317F, $36F1-$38C1).
 *
 * Motorcycle (v_alexActionState 7): always moving right; RIGHT accelerates by
 * 1/4 px/frame up to 4 px/frame, LEFT slows down to 1 px/frame. Button 1
 * jumps (16 frames of boost at -(2 + xSpeed/2) px/frame). Rocks (breakable
 * blocks) in front of the wheels are smashed; other solid tiles wreck the
 * bike (Alex jumps off, $43EB). The wheel animation speeds up with the bike.
 *
 * Boat (action state 8): like the motorcycle on water (max 2.5 px/frame);
 * button 2 shoots; a jump lands only on water: solid tiles wreck the boat.
 *
 * Peticopter (action state 9): button 1 starts the engine, then gives
 * 7 frames of thrust per press: up to 2 px/frame upwards (accel 1/4) while
 * climbing, braking the fall (1/4) while descending. Without thrust it glides
 * (rise braked by 1/8, then falls at up to 1 px/frame). Horizontally: accel
 * 1/4 up to 2 px/frame, friction 1/8, brake 1/4. The propeller animation
 * slows down between presses. Touching a solid ceiling or water, or being
 * hit, loses the peticopter.
 *
 * Losing a vehicle ($388E): in levels 1, 5 and 9 (table at $3904) Alex falls
 * (state $1B, then diving into the water), elsewhere he jumps off ($43EB).
 */
#include "alex.h"

#define VEHICLE_BODY 0x0C0C       /* interaction point */
#define VEHICLE_JUMP_HEAD 0x0102
#define VEHICLE_JUMP_BOOST 0x10
#define VEHICLE_ACCEL 0x0040
#define VEHICLE_DECEL 0xFFC0
#define VEHICLE_MIN_SPEED 0x0100
#define MOTORCYCLE_MAX_SPEED 0x0400
#define BOAT_MAX_SPEED 0x0280
#define JUMP_SPEED 0xFE00
#define PETICOPTER_THRUST_FRAMES 0x07
#define PETICOPTER_MAX_ANIMATION_DELAY 0x14
#define PETICOPTER_TOP 0x04       /* highest line */
#define VEHICLE_CRASH_LEVELS 0x3903 /* bank 0: 1 byte per level (index v_level) */

/* Arithmetic shift right by 1 of a 16-bit two's complement value. */
static uint16_t half(uint16_t v) { return (uint16_t)((v >> 1) | (v & 0x8000)); }

/* Jump boost of the motorcycle and the boat: button 1 held and boost frames
 * left: rise at -(2 + xSpeed/2); returns false when the boost is over. */
static bool vehicle_jump_boost(void) {
    Entity *alex = ALEX;
    if (!(ram8(v_inputData) & PAD_JUMP) || --alex->stateTimer == 0) return false;
    alex->ySpeed = (uint16_t)(JUMP_SPEED + half((uint16_t)-alex->xSpeed));
    alex_check_ceiling(VEHICLE_JUMP_HEAD);
    if (alex->unknown3 & MOTION_FALLING) alex->unknown8 |= ACTFLAG_GRAVITY;
    return true;
}

/* Button 2 on the boat and the peticopter: shoot ($444C), unless an attack
 * is already going on. */
static void shoot_if_pressed(void) {
    Entity *alex = ALEX;
    if (z80_test_ix(alex->unknown8, ACTFLAG_PUNCHING)) return;
    uint8_t pressed = ram8(v_inputDataChanges) & PAD_ACTION;
    z80_logic_flags(pressed, true);
    if (pressed) alex_call(f__LABEL_4453_);
}

/* Wheel/propeller animation delay: the faster, the shorter. */
static void set_animation_speed_from_x_speed(void) {
    ALEX->animationTimerResetValue = (uint8_t)(~HI(ALEX->xSpeed) + 7);
}

/* ------------------------------------------------------------ motorcycle */

/* $2FD5: the wheels hit rocks (breakable: smashed) or walls (wreck), then
 * the pad controls the speed. */
static void ride_motorcycle(void) {
    static const uint16_t wheel_probes[2] = {0x0214, 0x1218}; /* front top, front bottom */
    for (int i = 0; i < 2; i++) {
        cpu.de = wheel_probes[i];
        alex_call(f_getNearEntityTileAttrWithOffset);
        op_rlca();
        if (!(cpu.f & FLAG_C)) continue; /* not solid */
        op_rlca();
        if (!(cpu.f & FLAG_C)) {
            alex_call(f__LABEL_43F2_); /* solid wall: the bike is wrecked */
            return;
        }
        alex_break_block(cpu.hl, cpu.bc);
        break;
    }
    uint8_t held = ram8(v_inputData);
    if (held & PAD_RIGHT) alex_accelerate_right(VEHICLE_ACCEL, MOTORCYCLE_MAX_SPEED);
    else if (held & PAD_LEFT) alex_slow_down_to(VEHICLE_DECEL, VEHICLE_MIN_SPEED);
}

LIFTED(_LABEL_2FD5_, 0x2FD5) {
    ride_motorcycle();
    LIFTED_RETURN();
}

/* $302F: motorcycle in the air. */
static void update_motorcycle_jump(void) {
    Entity *alex = ALEX;
    alex_interact_with_tile(VEHICLE_BODY);
    if (!(alex->unknown8 & ACTFLAG_GRAVITY)) {
        if (vehicle_jump_boost()) {
            ride_motorcycle();
            return;
        }
        alex->unknown8 |= ACTFLAG_GRAVITY;
    }
    alex_gravity(VEHICLE_JUMP_HEAD);
    if (alex->unknown3 & MOTION_LANDED) {
        alex->unknown3 &= (uint8_t)~(MOTION_LANDED | MOTION_FALLING);
        alex->unknown8 &= (uint8_t)~ACTFLAG_GRAVITY;
        alex->state = ALEX_STATE_MOTORCYCLE;
    }
    ride_motorcycle();
}

LIFTED(alexHandler_302F, 0x302F) {
    update_motorcycle_jump();
    LIFTED_RETURN();
}

/* $2FA7 updateAlexRidingMotorcycle */
static void update_motorcycle(void) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    alex_interact_with_tile(VEHICLE_BODY);
    /* ground under the wheels (the probe keeps interactWithTile's flags) */
    if (!alex_probe_row(0x1805, 0x0F)) {
        alex->unknown3 |= MOTION_FALLING;
        alex->unknown8 |= ACTFLAG_GRAVITY;
    } else if (ram8(v_inputDataChanges) & PAD_JUMP) {
        alex->stateTimer = VEHICLE_JUMP_BOOST;
        alex->unknown3 &= (uint8_t)~MOTION_FALLING;
    } else {
        set_animation_speed_from_x_speed();
        alex_animate(ANIM_MOTORCYCLE);
        ride_motorcycle();
        return;
    }
    alex->state = ALEX_STATE_MOTORCYCLE_JUMP;
    alex_set_sprite(SPR_MOTORCYCLE_JUMP);
    update_motorcycle_jump();
}

LIFTED(updateAlexRidingMotorcycle, 0x2FA7) {
    update_motorcycle();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------------ boat */

/* $30C5: the hull hits solid tiles (wreck), then the pad controls the
 * speed. */
static void sail(void) {
    static const uint16_t hull_probes[2] = {0x0212, 0x1214};
    for (int i = 0; i < 2; i++) {
        cpu.de = hull_probes[i];
        alex_call(f_getNearEntityTileAttrWithOffset);
        op_rlca();
        if (cpu.f & FLAG_C) {
            alex_crash_vehicle();
            return;
        }
    }
    uint8_t held = ram8(v_inputData);
    if (held & PAD_RIGHT) alex_accelerate_right(VEHICLE_ACCEL, BOAT_MAX_SPEED);
    else if (held & PAD_LEFT) alex_slow_down_to(VEHICLE_DECEL, VEHICLE_MIN_SPEED);
}

LIFTED(_LABEL_30C5_, 0x30C5) {
    sail();
    LIFTED_RETURN();
}

/* $3107 updateAlexRidingBoatInAir */
static void update_boat_jump(void) {
    Entity *alex = ALEX;
    alex_interact_with_tile(VEHICLE_BODY);
    shoot_if_pressed();
    if (!(alex->unknown8 & ACTFLAG_GRAVITY)) {
        if (vehicle_jump_boost()) {
            sail();
            return;
        }
        alex->unknown8 |= ACTFLAG_GRAVITY;
    }
    alex_boat_gravity(VEHICLE_JUMP_HEAD);
    if (alex->state == ALEX_STATE_DIVING) return;
    if (alex->unknown3 & MOTION_LANDED) {
        alex->unknown3 &= (uint8_t)~(MOTION_LANDED | MOTION_FALLING);
        alex->unknown8 &= (uint8_t)~ACTFLAG_GRAVITY;
        alex->state = ALEX_STATE_BOAT;
    }
    sail();
}

LIFTED(updateAlexRidingBoatInAir, 0x3107) {
    update_boat_jump();
    LIFTED_RETURN();
}

/* $3094 updateAlexRidingBoat */
static void update_boat(void) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    alex_interact_with_tile(VEHICLE_BODY);
    shoot_if_pressed();
    if (ram8(v_inputDataChanges) & PAD_JUMP) {
        alex->stateTimer = VEHICLE_JUMP_BOOST;
        alex->unknown3 &= (uint8_t)~MOTION_FALLING;
        alex->state = ALEX_STATE_BOAT_JUMP;
        alex_set_sprite(SPR_BOAT_JUMP);
        update_boat_jump();
        return;
    }
    set_animation_speed_from_x_speed();
    alex_animate(ANIM_BOAT);
    sail();
}

LIFTED(updateAlexRidingBoat, 0x3094) {
    update_boat();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ peticopter */

/* $3742: horizontal flight. */
static void fly_horizontal(void) {
    Entity *alex = ALEX;
    uint8_t held;
    /* the flags of this test reach F' through the wall probe */
    if (z80_test_ix(alex->unknown3, MOTION_RIGHT)) {
        if (alex_probe_wall(0x0316, 0x0C, 2)) {
            alex_stop();
            held = ram8(v_inputData);
            if (!z80_test(held, PAD_LEFT)) return;
            alex->unknown3 &= (uint8_t)~(MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL);
            z80_logic_flags(alex->unknown3, true);
            return;
        }
        held = ram8(v_inputData);
        if (z80_test(held, PAD_RIGHT)) {
            alex->unknown3 |= MOTION_HORIZONTAL;
            alex_accelerate_right(0x0040, 0x0200);
        } else if (z80_test(held, PAD_LEFT)) {
            alex->unknown3 &= (uint8_t)~MOTION_FACING_RIGHT;
            alex_turn_left(0xFFC0);
        } else {
            alex_friction_right_if_moving(0xFFE0);
        }
        return;
    }
    if (alex_probe_wall(0x0302, 0x0C, 2)) {
        alex_stop();
        held = ram8(v_inputData);
        if (!z80_test(held, PAD_RIGHT)) return;
        alex->unknown3 |= MOTION_FACING_RIGHT | MOTION_RIGHT;
        z80_logic_flags(alex->unknown3, false);
        return;
    }
    held = ram8(v_inputData);
    if (z80_test(held, PAD_LEFT)) {
        alex->unknown3 |= MOTION_HORIZONTAL;
        alex_accelerate_left(0xFFC0, 0xFE00);
    } else if (z80_test(held, PAD_RIGHT)) {
        alex->unknown3 |= MOTION_FACING_RIGHT;
        alex_turn_right(0x0040);
    } else {
        alex_friction_left_if_moving(0x0020);
    }
}

LIFTED(sub_3742, 0x3742) {
    fly_horizontal();
    LIFTED_RETURN();
}

/* Button 1 (just pressed) restarts the thrust timer; returns true while
 * thrusting (and consumes one frame of it). */
static bool thrust(void) {
    Entity *alex = ALEX;
    if (alex->stateTimer == 0) {
        if (!(ram8(v_inputDataChanges) & PAD_JUMP)) return false;
        alex->stateTimer = PETICOPTER_THRUST_FRAMES;
    }
    alex->stateTimer--;
    alex->animationTimerResetValue = 0x02; /* propeller at full speed */
    return true;
}

/* $37D5: vertical flight. */
static void fly_vertical(void) {
    Entity *alex = ALEX;
    if (!z80_test_ix(alex->unknown3, MOTION_DOWN)) {
        /* Climbing. A solid ceiling breaks the peticopter. */
        if (alex_probe_row(0x0204, 0x10)) {
            alex_lose_vehicle();
            return;
        }
        bool thrusting = thrust();
        if (HI(alex->yPos) < PETICOPTER_TOP) {
            /* top of the screen: start descending */
            alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
            alex->unknown3 |= MOTION_DOWN;
            alex->ySpeed = 0;
            cpu.hl = 0;
        } else if (thrusting) {
            alex_accelerate_up(0xFFC0, 0xFE00);
        } else {
            alex_brake_rising(0x0020); /* glide */
        }
        return;
    }
    /* Descending: land on solid ground; water breaks the peticopter. */
    bool landed = false;
    cpu.de = 0x2004;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if (cpu.a & TILE_SOLID) {
        landed = true;
    } else {
        if (ram8(RAM_IN_WATER)) {
            alex_lose_vehicle();
            return;
        }
        landed = alex_probe_further_right(0x08) || alex_probe_further_right(0x08);
    }
    if (landed) {
        alex->ySpeed = 0;
        alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
        if (thrust()) alex_brake_sinking(0xFFC0);
        return;
    }
    if (thrust()) alex_brake_sinking(0xFFC0);
    else alex_accelerate_down(0x0020, 0x0100);
}

LIFTED(_LABEL_37D5_, 0x37D5) {
    fly_vertical();
    LIFTED_RETURN();
}

/* $36F1 updateAlexFlyingPeticopter */
static void update_peticopter(void) {
    Entity *alex = ALEX;
    if (!(alex->unknown8 & ACTFLAG_ENGINE_ON)) {
        if (!(ram8(v_inputDataChanges) & PAD_JUMP)) return; /* waiting for take-off */
        alex->unknown8 |= ACTFLAG_ENGINE_ON;
    }
    alex_interact_with_tile(0x040C); /* head, body and feet */
    alex_interact_with_tile(0x140C);
    alex_interact_with_tile(0x1C0C);
    shoot_if_pressed();
    fly_horizontal();
    fly_vertical();
    uint16_t animation = alex->unknown3 & MOTION_FACING_RIGHT ? ANIM_PETICOPTER_RIGHT : ANIM_PETICOPTER_LEFT;
    uint8_t delay = (uint8_t)(alex->animationTimerResetValue + 1);
    if (delay < PETICOPTER_MAX_ANIMATION_DELAY) alex->animationTimerResetValue = delay; /* propeller slows down */
    alex_animate(animation);
}

LIFTED(updateAlexFlyingPeticopter, 0x36F1) {
    update_peticopter();
    LIFTED_RETURN();
}

/* ------------------------------------------------------ losing a vehicle */

/* $389C: the vehicle is destroyed and Alex falls (state $1B). */
void alex_crash_vehicle(void) {
    Entity *alex = ALEX;
    ram8(v_shouldSpawnRidingBoat_RAM_C051_) = 0;
    ram8(v_alexActionState) = ACTION_NONE;
    alex_call(f__LABEL_4415_); /* smoke puff where the vehicle was */
    alex_stop();
    alex->ySpeed = 0;
    alex->unknown8 &= (uint8_t)~ACTFLAG_PUNCHING;
    alex->unknown3 |= MOTION_DOWN | MOTION_VERTICAL;
    alex->state = ALEX_STATE_VEHICLE_CRASH;
    ram8(v_soundControl) = SOUND_FALLING;
}

LIFTED(_LABEL_389C_, 0x389C) {
    alex_crash_vehicle();
    LIFTED_RETURN();
}

/* $388E: the vehicle is lost (hit, or peticopter in water/ceiling). */
void alex_lose_vehicle(void) {
    cpu.de = VEHICLE_CRASH_LEVELS;
    cpu.hl = (uint16_t)(VEHICLE_CRASH_LEVELS + ram8(v_level));
    if (rd8(cpu.hl)) alex_crash_vehicle();
    else alex_call(f__LABEL_43F2_); /* Alex jumps off */
}

LIFTED(_LABEL_388E_, 0x388E) {
    alex_lose_vehicle();
    LIFTED_RETURN();
}
