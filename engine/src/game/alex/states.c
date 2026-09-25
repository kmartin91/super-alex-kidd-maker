/*
 * Alex's updater and his ground/air states ($2958-$2FA6).
 *
 * updateAlex runs once per frame (entity type 1, slot 0):
 *   1. clears the scroll speeds (the camera code sets them again),
 *   2. if Alex was hit (flags bit 7): dies, or only loses his vehicle,
 *   3. runs the handler of Alex's state (table at $2982, see ALEX_STATE_*),
 *   4. unless dead or stunned, runs the level's camera handler (camera.c).
 *
 * Ground states (idle, walking, crouched) first look at the tile at Alex's
 * centre (tiles.c), then probe the ground under his feet: 25 px below his
 * corner, at x+4 and x+12. No ground: he falls. Otherwise the floor tile is
 * checked for special effects, and water under his body makes him swim.
 *
 * Jumping (button 1): 22 frames of "boost" while the button is held, during
 * which the vertical speed is forced to -(2 + |xSpeed|/4) px/frame (running
 * jumps are higher); then gravity (physics.c) takes over. Releasing the button
 * or bumping the ceiling ends the boost early. In the air Alex steers with the
 * air acceleration/friction constants below.
 */
#include "alex.h"

/* Walking (8.8 fixed point per frame). */
#define WALK_ACCEL 0x0040
#define WALK_MAX_SPEED 0x0200
#define WALK_FRICTION 0x0020
#define WALK_BRAKE 0x0040
/* Steering in the air. */
#define AIR_ACCEL 0x0010
#define AIR_MAX_SPEED 0x0200
#define AIR_FRICTION 0x0008
#define AIR_BRAKE 0x0010
/* Jump. */
#define JUMP_BOOST_FRAMES 0x16
#define JUMP_SPEED 0xFE00       /* -2 px/frame, minus |xSpeed| / 4 */
/* Crouching slides at 1/8 px/frame, then stops with friction 1/8. */
#define CROUCH_SLIDE_SPEED 0x0020
#define CROUCH_FRICTION 0x0020
#define DEATH_RISE_SPEED 0xFF38 /* the ghost floats up at -0.78 px/frame */
#define DEATH_FREEZE_FRAMES 0x1E

#define IDLE_ANIMATION_DELAY 5
#define ALEX_HEIGHT 0x18        /* feet offset (unknown11) on foot */
#define ALEX_FOOT_WIDTH 0x08    /* distance between the two ground probes */

/* Probe offsets (dy << 8 | dx from Alex's top-left corner). */
#define PROBE_CENTRE 0x0C08
#define PROBE_UNDER_FEET 0x1808
#define PROBE_GROUND 0x1904     /* 1 px below the feet */
#define PROBE_HEAD 0x0004
#define PROBE_JUMP_HEAD 0x0104
#define PROBE_WALL_LEFT 0x0102
#define PROBE_WALL_RIGHT 0x010E
#define PROBE_WALL_LEFT_OFFSCREEN 0x1702
#define PROBE_WALL_RIGHT_OFFSCREEN 0x170E
#define PROBE_AIR_WALL_LEFT_OFFSCREEN 0x1802
#define PROBE_AIR_WALL_RIGHT_OFFSCREEN 0x180E
#define PROBE_CROUCH_WALL_LEFT 0x0902
#define PROBE_CROUCH_WALL_RIGHT 0x090E
#define WALL_PROBE_HEIGHT 0x0E  /* second point of the wall probes, 14 px lower */
#define CROUCH_WALL_HEIGHT 0x0D

#define STATE_LIFE_LOST 0x06

/* -------------------------------------------------------- small transitions */

/* $2C04 leadAlexIdleSpriteDescriptor */
void alex_load_idle_sprite(void) {
    alex_set_sprite(ALEX->unknown3 & MOTION_FACING_RIGHT ? SPR_IDLE_RIGHT : SPR_IDLE_LEFT);
}

LIFTED(leadAlexIdleSpriteDescriptor, 0x2C04) {
    alex_load_idle_sprite();
    LIFTED_RETURN();
}

/* $2BFA setAlexIdleStateAndLoadIdleAnimationDescriptor */
void alex_set_idle(void) {
    ALEX->animationTimerResetValue = IDLE_ANIMATION_DELAY;
    ALEX->state = ALEX_STATE_IDLE;
    alex_load_idle_sprite();
}

LIFTED(setAlexIdleStateAndLoadIdleAnimationDescriptor, 0x2BFA) {
    alex_set_idle();
    LIFTED_RETURN();
}

/* $2B0B crouch */
void alex_crouch(void) {
    ALEX->state = ALEX_STATE_CROUCHED;
    alex_set_sprite(ALEX->unknown3 & MOTION_FACING_RIGHT ? SPR_CROUCH_RIGHT : SPR_CROUCH_LEFT);
}

LIFTED(crouch, 0x2B0B) {
    alex_crouch();
    LIFTED_RETURN();
}

/* $2B23 / $2B30: start walking facing left / right (the direction of motion
 * is not changed). */
static void walk_facing_left(void) {
    ALEX->unknown3 &= (uint8_t)~MOTION_FACING_RIGHT;
    ALEX->state = ALEX_STATE_WALKING;
}

static void walk_facing_right(void) {
    ALEX->unknown3 |= MOTION_FACING_RIGHT;
    ALEX->state = ALEX_STATE_WALKING;
}

LIFTED(sub_2B23, 0x2B23) {
    walk_facing_left();
    LIFTED_RETURN();
}

LIFTED(sub_2B30, 0x2B30) {
    walk_facing_right();
    LIFTED_RETURN();
}

/* $2B1F walkLeft / $2B2C walkRight */
void alex_walk_left(void) {
    ALEX->unknown3 &= (uint8_t)~MOTION_RIGHT;
    walk_facing_left();
}

void alex_walk_right(void) {
    ALEX->unknown3 |= MOTION_RIGHT;
    walk_facing_right();
}

LIFTED(walkLeft, 0x2B1F) {
    alex_walk_left();
    LIFTED_RETURN();
}

LIFTED(walkRight, 0x2B2C) {
    alex_walk_right();
    LIFTED_RETURN();
}

/* $2B39 walk: keep walking in the current direction of motion. */
void alex_walk(void) {
    if (ALEX->unknown3 & MOTION_RIGHT) walk_facing_right();
    else walk_facing_left();
}

LIFTED(walk, 0x2B39) {
    alex_walk();
    LIFTED_RETURN();
}

/* $2CBC: enter the in-air state with motion flags `motion`, and run it. */
static void enter_air(uint8_t motion) {
    ALEX->unknown3 = motion;
    ALEX->state = ALEX_STATE_IN_AIR;
    alex_set_sprite(motion & MOTION_FACING_RIGHT ? SPR_AIR_RIGHT : SPR_AIR_LEFT);
    alex_update_in_air();
}

LIFTED(sub_2CBC, 0x2CBC) {
    enter_air(cpu.a);
    LIFTED_RETURN();
}

/* $2CA1 fall: walked off a ledge: gravity at once. */
void alex_fall(void) {
    Entity *alex = ALEX;
    uint8_t motion = (uint8_t)((alex->unknown3 & ~MOTION_LANDED) | MOTION_FALLING);
    alex->unknown8 |= ACTFLAG_GRAVITY;
    enter_air(motion);
}

LIFTED(fall, 0x2CA1) {
    alex_fall();
    LIFTED_RETURN();
}

/* $2CAE jump */
void alex_jump(void) {
    Entity *alex = ALEX;
    ram8(v_soundControl) = SOUND_JUMP;
    alex->stateTimer = JUMP_BOOST_FRAMES;
    enter_air(alex->unknown3 & (uint8_t)~(MOTION_LANDED | MOTION_FALLING));
}

LIFTED(jump, 0x2CAE) {
    alex_jump();
    LIFTED_RETURN();
}

/* --------------------------------------------------------------- the ground */

/* Is there ground under Alex's feet? (on screen: also sets up the flags the
 * probe saves in F'). */
static bool ground_under_feet(void) {
    Entity *alex = ALEX;
    if (HI(alex->isOffScreenFlags)) return alex_probe_row_offscreen(PROBE_GROUND);
    cpu.f = FLAGS_ZERO;
    return alex_probe_row(PROBE_GROUND, ALEX_FOOT_WIDTH);
}

/* Common start of the ground states: returns false when the state handler
 * must stop (state changed, or Alex started falling). */
static bool stand_on_ground(uint8_t state) {
    Entity *alex = ALEX;
    alex_interact_with_tile(PROBE_CENTRE);
    if (alex->state != state) return false;
    bool offscreen = HI(alex->isOffScreenFlags) != 0;
    if (!ground_under_feet()) {
        alex_fall();
        return false;
    }
    if (!offscreen) {
        alex_interact_with_floor(PROBE_UNDER_FEET);
        if (alex->state != state) return false;
    }
    return true;
}

/* $2A9E updateAlexIdle */
static void update_idle(void) {
    Entity *alex = ALEX;
    alex_stop();
    alex->ySpeed = 0;
    if (alex->unknown8 & ACTFLAG_STUNNED) {
        alex_tick_stun();
        return;
    }
    if (!stand_on_ground(ALEX_STATE_IDLE)) return;
    if (ram8(RAM_IN_WATER)) {
        alex_splash();
        return;
    }
    if ((alex->unknown8 & ACTFLAG_PUNCHING) && !(alex->unknown8 & ACTFLAG_THROWING)) {
        if (alex_tick_punch()) alex_load_idle_sprite(); /* end of the punch pose */
        return;
    }
    uint8_t pressed = ram8(v_inputDataChanges);
    if (pressed & PAD_ACTION) {
        alex_handle_action();
        return;
    }
    if (pressed & PAD_JUMP) {
        alex_jump();
        return;
    }
    uint8_t held = ram8(v_inputData);
    if (held & PAD_LEFT) alex_walk_left();
    else if (held & PAD_RIGHT) alex_walk_right();
    else if (held & PAD_DOWN) alex_crouch();
}

LIFTED(updateAlexIdle, 0x2A9E) {
    update_idle();
    LIFTED_RETURN();
}

/* Wall ahead (on screen: 3 points, at the head, 14 px lower and 8 more)? */
static bool wall_ahead(uint16_t probe, uint16_t probe_offscreen) {
    if (HI(ALEX->isOffScreenFlags)) return alex_probe_wall_offscreen(probe_offscreen);
    cpu.f = FLAGS_ZERO;
    return alex_probe_wall(probe, WALL_PROBE_HEIGHT, 1);
}

/* Walking against a wall: Alex stops and may crouch or turn around. */
static void walk_into_wall(uint8_t away_pad, uint8_t toward_pad, uint16_t walk_animation) {
    alex_stop();
    uint8_t held = ram8(v_inputData);
    if (held & PAD_DOWN) alex_crouch();
    else if (held & away_pad) away_pad == PAD_RIGHT ? alex_walk_right() : alex_walk_left();
    else if (held & toward_pad) alex_animate(walk_animation); /* walking in place */
    else alex_set_idle();
}

static void walk_left_step(void) {
    Entity *alex = ALEX;
    if (wall_ahead(PROBE_WALL_LEFT, PROBE_WALL_LEFT_OFFSCREEN)) {
        walk_into_wall(PAD_RIGHT, PAD_LEFT, ANIM_WALK_LEFT);
        return;
    }
    uint8_t held = ram8(v_inputData);
    if (held & PAD_LEFT) {
        alex->unknown3 |= MOTION_HORIZONTAL;
        alex_accelerate_left((uint16_t)-WALK_ACCEL, (uint16_t)-WALK_MAX_SPEED);
        alex_animate(ANIM_WALK_LEFT);
    } else if (held & PAD_RIGHT) {
        /* turning around: face right at once, brake, then walk right */
        alex->unknown3 |= MOTION_FACING_RIGHT;
        alex_turn_right(WALK_BRAKE);
        alex_animate(ANIM_WALK_RIGHT);
    } else if (held & PAD_DOWN) {
        alex_crouch();
    } else if (!(alex->unknown3 & MOTION_HORIZONTAL)) {
        alex_set_idle();
    } else if (!alex_friction_left(WALK_FRICTION)) {
        alex_animate(ANIM_WALK_LEFT);
    } else {
        alex_set_idle();
    }
}

static void walk_right_step(void) {
    Entity *alex = ALEX;
    if (wall_ahead(PROBE_WALL_RIGHT, PROBE_WALL_RIGHT_OFFSCREEN)) {
        walk_into_wall(PAD_LEFT, PAD_RIGHT, ANIM_WALK_RIGHT);
        return;
    }
    uint8_t held = ram8(v_inputData);
    if (held & PAD_RIGHT) {
        alex->unknown3 |= MOTION_HORIZONTAL;
        alex_accelerate_right(WALK_ACCEL, WALK_MAX_SPEED);
        alex_animate(ANIM_WALK_RIGHT);
    } else if (held & PAD_LEFT) {
        /* turning around; QUIRK: keeps the walking-right animation while braking */
        alex->unknown3 |= MOTION_HORIZONTAL;
        alex->unknown3 &= (uint8_t)~MOTION_FACING_RIGHT;
        alex_turn_left((uint16_t)-WALK_BRAKE);
        alex_animate(ANIM_WALK_RIGHT);
    } else if (held & PAD_DOWN) {
        alex_crouch();
    } else if (!(alex->unknown3 & MOTION_HORIZONTAL)) {
        alex_set_idle();
    } else if (alex_friction_right((uint16_t)-WALK_FRICTION)) {
        alex_animate(ANIM_WALK_RIGHT);
    } else {
        alex_set_idle();
    }
}

/* $2B41 updateAlexWalking */
static void update_walking(void) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    if (alex->unknown8 & ACTFLAG_STUNNED) {
        alex->state = ALEX_STATE_IDLE;
        alex_tick_stun();
        return;
    }
    if (!stand_on_ground(ALEX_STATE_WALKING)) return;
    uint8_t pressed = ram8(v_inputDataChanges);
    if (pressed & PAD_ACTION) {
        /* punching stops Alex */
        alex->state = ALEX_STATE_IDLE;
        alex_stop();
        alex_handle_action();
        return;
    }
    if (pressed & PAD_JUMP) {
        alex_jump();
        return;
    }
    if (alex->unknown3 & MOTION_RIGHT) walk_right_step();
    else walk_left_step();
}

LIFTED(updateAlexWalking, 0x2B41) {
    update_walking();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------------ the air */

/* Wall probe in the air, 7 px below where Alex's corner will be (dx 2 or
 * 14), 3 points 8 px apart. */
static bool air_wall(uint8_t dx, uint16_t probe_offscreen) {
    Entity *alex = ALEX;
    if (HI(alex->isOffScreenFlags)) return alex_probe_wall_offscreen(probe_offscreen);
    uint8_t dy = add8_flags(HI(alex->ySpeed), 7, 0); /* the flags reach F' */
    return alex_probe_wall((uint16_t)(dy << 8 | dx), 8, 1);
}

static bool punch_pose(void) { return (ALEX->unknown8 & (ACTFLAG_PUNCHING | ACTFLAG_THROWING)) == ACTFLAG_PUNCHING; }

/* $2D7F: horizontal control in the air. */
static void air_steer(void) {
    Entity *alex = ALEX;
    uint8_t held;
    if (alex->unknown3 & MOTION_RIGHT) {
        if (air_wall(0x0E, PROBE_AIR_WALL_RIGHT_OFFSCREEN)) {
            alex_stop();
            if (ram8(v_inputData) & PAD_LEFT) alex->unknown3 &= (uint8_t)~MOTION_RIGHT;
            return;
        }
        held = ram8(v_inputData);
        if (held & PAD_RIGHT) {
            alex->unknown3 |= MOTION_HORIZONTAL;
            alex_accelerate_right(AIR_ACCEL, AIR_MAX_SPEED);
        } else if (held & PAD_LEFT) {
            alex->unknown3 &= (uint8_t)~MOTION_FACING_RIGHT;
            alex_set_sprite(punch_pose() ? SPR_PUNCH_LEFT : SPR_AIR_LEFT);
            alex_turn_left((uint16_t)-AIR_BRAKE);
        } else {
            alex_friction_right_if_moving((uint16_t)-AIR_FRICTION);
        }
        return;
    }
    if (air_wall(0x02, PROBE_AIR_WALL_LEFT_OFFSCREEN)) {
        alex_stop();
        if (ram8(v_inputData) & PAD_RIGHT) alex->unknown3 |= MOTION_RIGHT;
        return;
    }
    held = ram8(v_inputData);
    if (held & PAD_LEFT) {
        alex->unknown3 |= MOTION_HORIZONTAL;
        alex_accelerate_left((uint16_t)-AIR_ACCEL, (uint16_t)-AIR_MAX_SPEED);
    } else if (held & PAD_RIGHT) {
        alex->unknown3 |= MOTION_FACING_RIGHT;
        alex_set_sprite(punch_pose() ? SPR_PUNCH_RIGHT : SPR_AIR_RIGHT);
        alex_turn_right(AIR_BRAKE);
    } else {
        alex_friction_left_if_moving(AIR_FRICTION);
    }
}

/* Arithmetic shift right of a 16-bit two's complement value. */
static uint16_t sra16(uint16_t v, int n) {
    uint16_t sign = (v & 0x8000) ? (uint16_t)~(0xFFFFu >> n) : 0;
    return (uint16_t)((v >> n) | sign);
}

/* $2CD0 updateAlexInAir */
void alex_update_in_air(void) {
    Entity *alex = ALEX;
    alex_interact_with_tile(PROBE_CENTRE);
    if (alex->state != ALEX_STATE_IN_AIR) return;
    if (ram8(RAM_IN_WATER)) {
        alex_splash();
        return;
    }
    if (alex->unknown8 & ACTFLAG_PUNCHING) {
        if (alex_tick_punch()) alex_set_sprite(alex->unknown3 & MOTION_FACING_RIGHT ? SPR_AIR_RIGHT : SPR_AIR_LEFT);
    } else if (ram8(v_inputDataChanges) & PAD_ACTION) {
        alex_handle_action();
    }

    if (!(alex->unknown8 & ACTFLAG_GRAVITY)) {
        if ((ram8(v_inputData) & PAD_JUMP) && --alex->stateTimer != 0) {
            /* Jump boost: rise at 2 px/frame + a quarter of the running speed. */
            uint16_t run = alex->xSpeed;
            if (!(run & 0x8000)) run = (uint16_t)-run;
            alex->ySpeed = (uint16_t)(JUMP_SPEED + sra16(run, 2));
            alex_check_ceiling(PROBE_JUMP_HEAD);
            if (alex->unknown3 & MOTION_FALLING) alex->unknown8 |= ACTFLAG_GRAVITY; /* bumped */
            air_steer();
            return;
        }
        alex->unknown8 |= ACTFLAG_GRAVITY;
    }
    alex_gravity(PROBE_JUMP_HEAD);
    if (alex->unknown3 & MOTION_LANDED) {
        alex->unknown8 &= (uint8_t)~ACTFLAG_GRAVITY;
        ram8(v_soundControl) = SOUND_FX_1;
        if (punch_pose()) {
            alex->unknown3 &= (uint8_t)~MOTION_HORIZONTAL;
            alex->state = ALEX_STATE_IDLE;
            alex_set_idle();
            return;
        }
        if (!(alex->unknown3 & MOTION_HORIZONTAL)) {
            alex_set_idle();
            return;
        }
        alex_walk();
    }
    air_steer();
}

LIFTED(updateAlexInAir, 0x2CD0) {
    alex_update_in_air();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------- crouching */

/* $2F22: DOWN released with room above: stand up (walking if moving). */
static void stand_up(void) {
    if (ALEX->unknown3 & MOTION_HORIZONTAL) alex_walk();
    else alex_set_idle();
}

/* $2E60 updateAlexCrouched. Crouching Alex slides on (1/8 px/frame) when he
 * crouches while walking; LEFT/RIGHT without DOWN start a slide (the pad
 * decides the facing). */
static void update_crouched(void) {
    Entity *alex = ALEX;
    alex->ySpeed = 0;
    if (alex->unknown8 & ACTFLAG_STUNNED) {
        alex_tick_stun();
        return;
    }
    alex_interact_with_tile(PROBE_CENTRE);
    if (alex->state != ALEX_STATE_CROUCHED) return;
    if (!ground_under_feet()) {
        alex_fall();
        return;
    }
    if (!(ram8(v_inputData) & PAD_DOWN)) {
        if (HI(alex->isOffScreenFlags)) {
            stand_up();
            return;
        }
        cpu.f = FLAGS_ZERO;
        if (!alex_probe_row(PROBE_HEAD, ALEX_FOOT_WIDTH)) {
            stand_up();
            return;
        }
        /* low ceiling: stay down */
    }
    if (alex->unknown3 & MOTION_HORIZONTAL) {
        /* sliding: stop at walls, else slow down (flags: BIT 1,(ix+20), carry
         * of the ground probe) */
        cpu.f = FLAG_C;
        alu_bit(1, alex->unknown3, ALEX_EA_HIGH);
        if (alex->unknown3 & MOTION_RIGHT) {
            if (alex_probe_column(PROBE_CROUCH_WALL_RIGHT, CROUCH_WALL_HEIGHT)) alex_stop();
            else if (!alex_friction_right((uint16_t)-CROUCH_FRICTION)) alex_stop();
        } else {
            if (alex_probe_column(PROBE_CROUCH_WALL_LEFT, CROUCH_WALL_HEIGHT)) alex_stop();
            else alex_friction_left_if_moving(CROUCH_FRICTION);
        }
        return;
    }
    uint8_t held = ram8(v_inputData);
    cpu.c = held;
    uint8_t motion = alex->unknown3 & (uint8_t)~MOTION_HORIZONTAL;
    bool facing_right = alex->unknown3 & MOTION_FACING_RIGHT;
    if (held & PAD_LEFT) {
        if (!(held & PAD_DOWN)) {
            cpu.f = 0;
            alu_bit(1, held, held);
            bool wall = alex_probe_column(PROBE_CROUCH_WALL_LEFT, CROUCH_WALL_HEIGHT);
            motion = cpu.a; /* QUIRK: the probe's A replaces the motion flags */
            if (!wall) {
                alex->xSpeed = (uint16_t)-CROUCH_SLIDE_SPEED;
                motion |= MOTION_HORIZONTAL;
            }
        }
        alex->unknown3 = motion & (uint8_t)~(MOTION_FACING_RIGHT | MOTION_RIGHT);
        if (facing_right) alex_set_sprite(SPR_CROUCH_LEFT);
        return;
    }
    if (!(held & PAD_RIGHT)) return;
    if (!(held & PAD_DOWN)) {
        cpu.f = 0;
        alu_bit(1, held, held);
        bool wall = alex_probe_column(PROBE_CROUCH_WALL_RIGHT, CROUCH_WALL_HEIGHT);
        motion = cpu.a; /* QUIRK: as above */
        if (!wall) {
            alex->xSpeed = CROUCH_SLIDE_SPEED;
            motion |= MOTION_HORIZONTAL;
        }
    }
    alex->unknown3 = motion | MOTION_FACING_RIGHT | MOTION_RIGHT;
    if (!facing_right) alex_set_sprite(SPR_CROUCH_RIGHT);
}

LIFTED(updateAlexCrouched, 0x2E60) {
    update_crouched();
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ death, spawn */

/* $2F41: Alex was hit. In the peticopter or on the boat he only loses the
 * vehicle; otherwise he dies: the game freezes for 30 frames, then his ghost
 * floats up (updateAlexDead). */
static void hit(void) {
    Entity *alex = ALEX;
    alex->flags &= (uint8_t)~ENTITY_FLAG_HIT;
    uint8_t state = alex->state;
    if (state == ALEX_STATE_PETICOPTER || state == ALEX_STATE_BOAT || state == ALEX_STATE_BOAT_JUMP) {
        alex_lose_vehicle();
        return;
    }
    alex->xSpeed = 0;
    alex->unknown8 = 0;
    ram8(v_alexActionState) = ACTION_NONE;
    alex->unknown3 &= (uint8_t)~MOTION_HORIZONTAL;
    alex->ySpeed = DEATH_RISE_SPEED;
    ram8(v_alexStateBeforeHit) = state;
    alex->state = ALEX_STATE_DEAD;
    alex->animationTimerResetValue = 5;
    cpu.b = DEATH_FREEZE_FRAMES;
    do {
        cpu.a = 0x01;
        alex_call(f_waitForInterrupt);
    } while (--cpu.b);
    ram8(v_soundControl) = SOUND_DEAD;
}

LIFTED(_LABEL_2F41_, 0x2F41) {
    hit();
    LIFTED_RETURN();
}

/* $2F8A updateAlexDead: the ghost rises; once it has gone through the top of
 * the screen and reaches lines $A3-$A7 of the wrapped area, the life is
 * lost. */
static void update_dead(void) {
    Entity *alex = ALEX;
    alex_animate(ANIM_DEAD);
    uint8_t y = HI(alex->yPos);
    if (y >= 0xA8 || y < 0xA3) return;
    if (HI(alex->isOffScreenFlags) != 0xFF) return;
    alex_call(f_destroyCurrentEntity);
    ram8(v_gameState) = STATE_LIFE_LOST;
}

LIFTED(updateAlexDead, 0x2F8A) {
    update_dead();
    LIFTED_RETURN();
}

static void set_body(uint8_t height, uint8_t foot_width) {
    ALEX->unknown11 = height;
    ALEX->unknown9 = foot_width;
}

/* $29C2 updateAlexSpawning: first frame of a life. Alex appears on foot,
 * walking in from the left (next screen), or on his vehicle. */
static void spawn(void) {
    Entity *alex = ALEX;
    alex->flags |= 0x01;
    alex->flags = 0x00; /* QUIRK: bit 0 is set, then the whole byte cleared */
    alex->unknown3 = MOTION_FACING_RIGHT | MOTION_RIGHT;
    alex->animationTimer = 1;
    uint8_t action = ram8(v_alexActionState);
    if (ram8(v_shouldAlexStartWalkingtoNextScreen)) {
        alex->animationTimerResetValue = 5;
        set_body(ALEX_HEIGHT, ALEX_FOOT_WIDTH);
        alex->state = ALEX_STATE_AUTO_WALK;
        HI(alex->yPos) = 0x98;
        alex_set_sprite(SPR_IDLE_RIGHT);
    } else if (ram8(v_shouldSpawnRidingBoat_RAM_C051_)) {
        alex->xSpeed = 0x0040;
        HI(alex->yPos) = 0x90;
        alex->animationTimerResetValue = 4;
        set_body(0x10, 0x0F);
        alex->state = ALEX_STATE_BOAT;
        ram8(v_alexActionState) = ACTION_BOAT;
        alex_set_sprite(SPR_BOAT);
    } else if (action == ACTION_MOTORCYCLE) {
        ram8(v_soundControl) = SOUND_BIKE_SONG;
        alex->xSpeed = 0x0040;
        alex->animationTimerResetValue = 4;
        set_body(ALEX_HEIGHT, 0x0F);
        alex->state = ALEX_STATE_MOTORCYCLE;
        alex_set_sprite(SPR_MOTORCYCLE);
    } else if (action == ACTION_PETICOPTER) {
        alex->state = ALEX_STATE_PETICOPTER;
        ram8(v_soundControl) = SOUND_PETICOPTER_SONG;
        HI(alex->yPos) = (uint8_t)(HI(alex->yPos) - 0x10);
        alex->animationTimerResetValue = 4;
        set_body(ALEX_HEIGHT, ALEX_FOOT_WIDTH);
        if (ram8(v_level) == 0x0D) {
            alex->unknown3 = 0; /* level 13 is flown to the left */
            alex_set_sprite(SPR_PETICOPTER_LEFT);
        } else {
            alex_set_sprite(SPR_PETICOPTER_RIGHT);
        }
    } else {
        set_body(ALEX_HEIGHT, ALEX_FOOT_WIDTH);
        alex_set_idle();
    }
}

LIFTED(updateAlexSpawning, 0x29C2) {
    spawn();
    LIFTED_RETURN();
}

/* $29BA updateAlexSpawningAtCenter */
LIFTED(updateAlexSpawningAtCenter, 0x29BA) {
    HI(ALEX->xPos) = 0x80;
    HI(ALEX->yPos) = 0x60;
    spawn();
    LIFTED_RETURN();
}

/* $2A6E: back from the item menu: Alex hovers in state 7 (keeps flying with
 * the Cane of Flight, otherwise falls on the next frame). */
LIFTED(_LABEL_2A6E_, 0x2A6E) {
    ALEX->animationTimerResetValue = 5;
    set_body(ALEX_HEIGHT, ALEX_FOOT_WIDTH);
    ALEX->state = ALEX_STATE_CANE_FLIGHT;
    alex_set_sprite(SPR_IDLE_RIGHT);
    LIFTED_RETURN();
}

/* ---------------------------------------------------------------- updater */

/* $2958 updateAlex */
LIFTED(updateAlex, 0x2958) {
    Entity *alex = ALEX;
    ram16(v_horizontalScrollSpeed) = 0;
    ram16(v_verticalScrollSpeed) = 0;
    if (alex->flags & ENTITY_FLAG_HIT) hit();
    /* State handler: dispatched through the original table at $2982 (the
     * handlers see the registers and flags of that dispatch). */
    cpu.a = alex->state;
    cpu.hl = 0x2982;
    alex_call(f_jumpToAthPointer);
    if (alex->state == ALEX_STATE_DEAD) LIFTED_RETURN();
    if (alex->unknown8 & ACTFLAG_STUNNED) LIFTED_RETURN();
    alex_update_camera();
    LIFTED_RETURN();
}
