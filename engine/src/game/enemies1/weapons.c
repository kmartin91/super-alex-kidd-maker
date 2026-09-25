/*
 * Alex's weapons and items ($443F-$497C): vehicle missiles, Magic Capsules A
 * and B, the Power Bracelet shockwave.
 *
 * These objects always live in fixed slots: slot 2 (and 3) for shots and
 * capsule helpers, slot 4 for the thrown capsule. Alex's Entity.unknown8 holds
 * the weapon state (ALEX_ATTACKING / ALEX_WEAPON_OUT / ALEX_ATTACK_UNSPENT) and
 * v_alexActionState the item in use (3 = capsule A, 4 = capsule B,
 * 5 = Power Bracelet, 8 = boat, 9 = peticopter).
 */
#include "game/enemies1/enemies1.h"

/* Animation / sprite descriptors (bank 2 data). */
#define WRECK_PUFF_ANIMATION 0x8372
#define SHOT_EXPLOSION_SPRITE 0x8380
#define BARRIER_SPRITE 0x8CD2
#define HELPER_WALK_LEFT_ANIMATION 0x8522
#define HELPER_WALK_RIGHT_ANIMATION 0x8537
#define SHOCKWAVE_LEFT_SPRITE 0x8093
#define SHOCKWAVE_RIGHT_SPRITE 0x8098
#define ALEX_PUNCHING_LEFT_SPRITE 0x8DD1
#define ALEX_PUNCHING_RIGHT_SPRITE 0x8DE9

#define SHOT_COOLDOWN_FRAMES 5
#define HELPERS_PER_CAPSULE 8
#define HELPER_FIRST_DELAY 0x0A
#define HELPER_DELAY 0x1E
#define BARRIER_FRAMES 0x04B0   /* 1200 frames = 20 seconds */
#define HELPER_JUMP_SPEED 0xFD  /* ySpeed high byte: -3 px/frame */
#define HELPER_SPEED_RIGHT 0x02 /* xSpeed high byte */
#define HELPER_SPEED_LEFT 0xFE
#define SHOCKWAVE_SPEED 0x0400  /* 4 px/frame */
#define ITEM_GRAVITY 0x0040
#define ITEM_MAX_FALL 0x04      /* pixel speed from which the fraction is dropped */
#define GROUND_ROW_Y 0x10       /* probe offsets below a helper / capsule */
#define GROUND_ROW_X 0x04

/* unknown3 bits of thrown items and helpers. */
enum {
    ITEM_MOVING_RIGHT = 0x02, /* copied from Alex's ALEX_MOVING_RIGHT */
    ITEM_LANDED = 0x40,
    ITEM_FALLING = 0x80,
};

/* --------------------------------------------------- vehicle missiles ($02-$04) */

/* Background test of the missile: true when it hits a wall. A breakable
 * block is broken first; the missile then explodes as well, because
 * _LABEL_4578_ always leaves a name table change pending (so the "keep
 * flying" outcome of that test is dead code). */
static bool vehicle_shot_hits_wall(Entity *e) {
    /* The missile always lives in slot 2, whose screen X is read directly. */
    Entity *shot = entity_at(WEAPON_SLOT);
    if ((ENT_SCREEN_X(shot) | ENT_SCREEN_Y(e)) != 0) return false;
    uint8_t attr = tile_attr_at(0x04, 0x04);
    if (!(attr & TILE_SOLID)) return false;
    if (!(attr & TILE_BREAKABLE)) return true;
    call_routine(f__LABEL_4578_); /* breaks the block (uses BC/HL left by the probe) */
    return ram8(v_nametableChangeRequest) != 0;
}

/* $4489: missile fired by the boat or the peticopter (slot 2), spawned by
 * _LABEL_4453_ with a lifetime of 20 frames in unknown7. It explodes on
 * walls (breaking breakable blocks), on enemies (EF_HIT is set by the hit
 * test) or at the end of its lifetime; the explosion becomes the cooldown
 * entity $04. */
LIFTED(updateEntity0x02, 0x4489) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_HIT) && !vehicle_shot_hits_wall(e)) {
        if (--e->unknown7 != 0) LIFTED_RETURN(); /* still flying */
    }
    e->unknown7 = SHOT_COOLDOWN_FRAMES;
    e->type = ENTITY_VEHICLE_SHOT_COOLDOWN;
    ram8(v_soundControl) = SOUND_VEHICLE_SHOT_EXPLODES;
    entity_at(WEAPON_SLOT)->xSpeed = 0;
    entity_at(WEAPON_SLOT)->spriteDescriptorPointer = SHOT_EXPLOSION_SPRITE;
    LIFTED_RETURN();
}

/* $443F: puff left where Alex's vehicle was destroyed; animates for the
 * number of frames in unknown7. */
LIFTED(updateEntity0x03, 0x443F) {
    cpu.hl = WRECK_PUFF_ANIMATION;
    call_routine(f_handleEntityAnimation);
    if (--entity_at(cpu.ix)->unknown7 == 0) DESTROY_AND_RETURN();
    LIFTED_RETURN();
}

/* $44CD: missile explosion; after unknown7 frames Alex may fire again. */
LIFTED(updateEntity0x04, 0x44CD) {
    if (--entity_at(cpu.ix)->unknown7 != 0) LIFTED_RETURN();
    alex()->unknown8 &= (uint8_t)~(ALEX_ATTACKING | ALEX_ATTACK_UNSPENT);
    DESTROY_AND_RETURN();
}

/* ----------------------------------------------------------- magic capsules */

/* $4689: Magic Capsule A thrown in an arc (slot 4). When it lands it opens
 * (type $06). It stops moving horizontally when it hits a wall. */
LIFTED(updateEntity0x05, 0x4689) {
    Entity *e = entity_at(cpu.ix);
    Entity *capsule = entity_at(ITEM_SLOT);
    if ((ENT_SCREEN_X(capsule) | ENT_SCREEN_Y(e)) != 0) TAIL_CALL(f__LABEL_485A_);
    cpu.de = (uint16_t)(GROUND_ROW_Y << 8 | GROUND_ROW_X);
    call_routine(f__LABEL_4944_); /* gravity and landing */
    if (e->unknown3 & ITEM_LANDED) {
        e->type = ENTITY_CAPSULE_A_OPEN;
        e->unknown10 = HELPERS_PER_CAPSULE;
        e->unknown11 = HELPER_FIRST_DELAY;
        LIFTED_RETURN();
    }
    /* QUIRK: when thrown to the left the wall probe is 66 pixels to the right
     * of the capsule ($42), probably meant to be a negative offset. */
    uint8_t probe_x = (e->unknown3 & ITEM_MOVING_RIGHT) ? 0x0E : 0x42;
    if (tile_attr_at(0x06, probe_x) & TILE_SOLID) capsule->xSpeed = 0;
    LIFTED_RETURN();
}

/* $46C2: opened Magic Capsule A. Releases up to 8 helpers (type $09), one
 * every 30 frames, into slots 2 and 3 whenever one of them is free. */
LIFTED(updateEntity0x06, 0x46C2) {
    Entity *e = entity_at(cpu.ix);
    Entity *capsule = entity_at(ITEM_SLOT);
    if ((ENT_SCREEN_X(capsule) | ENT_SCREEN_Y(e)) != 0) TAIL_CALL(f__LABEL_485A_);
    capsule->xSpeed = 0;
    capsule->ySpeed = 0;
    if (--e->unknown11 != 0) LIFTED_RETURN();

    uint16_t slot = WEAPON_SLOT;
    if (entity_at(slot)->type != 0) {
        slot = WEAPON_SLOT_2;
        if (entity_at(slot)->type != 0) {
            cpu.iy = slot;
            e->unknown11 = 1; /* both slots busy: try again next frame */
            LIFTED_RETURN();
        }
    }
    cpu.iy = slot;
    Entity *helper = entity_at(slot);
    ram8(v_soundControl) = SOUND_HELPER_APPEARS;
    e->unknown11 = HELPER_DELAY;
    helper->type = ENTITY_CAPSULE_HELPER_WALKING;
    ENT_X(helper) = ENT_X(capsule);
    ENT_Y(helper) = ENT_Y(capsule);
    helper->unknown3 = capsule->unknown3 & ITEM_MOVING_RIGHT;
    if (--e->unknown10 == 0) DESTROY_AND_RETURN();
    LIFTED_RETURN();
}

/* $4719: Magic Capsule B thrown in an arc (slot 4). When it lands it becomes
 * the barrier around Alex (type $08) for 20 seconds. */
LIFTED(updateEntity0x07, 0x4719) {
    Entity *e = entity_at(cpu.ix);
    Entity *capsule = entity_at(ITEM_SLOT);
    if ((ENT_SCREEN_X(capsule) | ENT_SCREEN_Y(e)) != 0) TAIL_CALL(f__LABEL_485A_);
    cpu.de = (uint16_t)(GROUND_ROW_Y << 8 | GROUND_ROW_X);
    call_routine(f__LABEL_4944_);
    if (e->unknown3 & ITEM_LANDED) {
        e->flags &= (uint8_t)~EF_DESTROY_OFFSCREEN;
        ram8(v_soundControl) = SOUND_MAGIC_CAPSULE_B;
        e->type = ENTITY_CAPSULE_B_BARRIER;
        capsule->spriteDescriptorPointer = BARRIER_SPRITE;
        capsule->stateTimer = (uint8_t)BARRIER_FRAMES; /* 16-bit counter in stateTimer/unknown8 */
        capsule->unknown8 = (uint8_t)(BARRIER_FRAMES >> 8);
        capsule->xSpeed = 0;
        capsule->ySpeed = 0;
        LIFTED_RETURN();
    }
    uint8_t probe_x = (e->unknown3 & ITEM_MOVING_RIGHT) ? 0x0E : 0x42; /* QUIRK: see $05 */
    if (tile_attr_at(0x06, probe_x) & TILE_SOLID) capsule->xSpeed = 0;
    LIFTED_RETURN();
}

/* $4885: Magic Capsule B barrier. Follows Alex (Alex cannot be hurt and the
 * barrier kills the enemies it touches, see the hit tests of action state 4)
 * until its 16-bit timer runs out; then the item is used up. */
LIFTED(updateEntity0x08, 0x4885) {
    Entity *barrier = entity_at(ITEM_SLOT);
    uint16_t frames_left = (uint16_t)(ENT_WORD(barrier->stateTimer, barrier->unknown8) - 1);
    barrier->stateTimer = (uint8_t)frames_left;
    barrier->unknown8 = (uint8_t)(frames_left >> 8);
    if (frames_left == 0) {
        call_routine(f_handler_LABEL_99D3_);
        alex()->unknown8 &= (uint8_t)~(ALEX_ATTACKING | ALEX_WEAPON_OUT | ALEX_ATTACK_UNSPENT);
        ram8(v_alexActionState) = 0;
        DESTROY_AND_RETURN();
    }
    ENT_X(barrier) = (uint8_t)(ENT_X(alex()) - 4);
    ENT_SCREEN_X(barrier) = ENT_SCREEN_X(alex());
    ENT_Y(barrier) = (uint8_t)(ENT_Y(alex()) - 3);
    ENT_SCREEN_Y(barrier) = ENT_SCREEN_Y(alex());
    LIFTED_RETURN();
}

/* ---------------------------------------------------- capsule A helpers */

/* First occupied enemy slot (7..16). When there is none, *slot is the slot
 * after the last one (17). */
static bool find_first_enemy(uint16_t *slot) {
    *slot = FIRST_ENEMY_SLOT;
    for (int i = 0; i < ENEMY_SLOT_COUNT; i++, *slot += ENTITY_SIZE)
        if (rd8(*slot) != 0) return true;
    return false;
}

/* $4830: HL = first occupied enemy slot, flag Z clear; Z set when none. */
LIFTED(sub_4830, 0x4830) {
    uint16_t slot;
    bool found = find_first_enemy(&slot);
    cpu.hl = slot;
    cpu.f = found ? 0 : FLAG_Z;
    LIFTED_RETURN();
}

/* $4768: capsule A helper walking. It heads for the first enemy found when it
 * appeared and jumps when that enemy is close in front of it (becoming type
 * $0A); it also falls when the ground ends. A helper that hits something
 * (EF_HIT) or leaves the screen vanishes. unknown9 bit 0: a target is being
 * tracked, stateTimer/unknown8: the target's slot. */
LIFTED(updateEntity0x09, 0x4768) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->animationTimerResetValue = 5;
        uint16_t target;
        if (!find_first_enemy(&target)) {
            /* No enemy: walk in the direction the capsule was thrown. */
            if (e->unknown3 & ITEM_MOVING_RIGHT) {
                ENT_HI(e, xSpeed) = HELPER_SPEED_RIGHT;
                ANIMATE_AND_RETURN(HELPER_WALK_RIGHT_ANIMATION);
            }
            ENT_HI(e, xSpeed) = HELPER_SPEED_LEFT;
            ANIMATE_AND_RETURN(HELPER_WALK_LEFT_ANIMATION);
        }
        cpu.iy = target;
        e->unknown9 |= 0x01;
        e->stateTimer = (uint8_t)target;
        e->unknown8 = (uint8_t)(target >> 8);
        /* QUIRK: on this first frame the walking animations are swapped. */
        if (ENT_X(entity_at(target)) >= ENT_X(e)) {
            e->unknown3 |= ITEM_MOVING_RIGHT;
            ENT_HI(e, xSpeed) = HELPER_SPEED_RIGHT;
            ANIMATE_AND_RETURN(HELPER_WALK_LEFT_ANIMATION);
        }
        e->unknown3 &= (uint8_t)~ITEM_MOVING_RIGHT;
        ENT_HI(e, xSpeed) = HELPER_SPEED_LEFT;
        ANIMATE_AND_RETURN(HELPER_WALK_RIGHT_ANIMATION);
    }

    if (e->flags & EF_HIT) TAIL_CALL(f__LABEL_4854_);
    e->ySpeed = 0;
    call_routine(f__LABEL_4846_); /* vanishes (and returns NC) when off screen */
    if (!(cpu.f & FLAG_C)) LIFTED_RETURN();

    if (e->unknown9 & 0x01) {
        uint16_t target = ENT_WORD(e->stateTimer, e->unknown8);
        if (rd8(target) == 0) {
            e->unknown9 &= (uint8_t)~0x01; /* target gone */
        } else {
            cpu.iy = target;
            uint8_t distance = (uint8_t)(ENT_X(entity_at(target)) - ENT_X(e));
            bool close = (e->unknown3 & ITEM_MOVING_RIGHT) ? distance < 0x10 : distance >= 0xD0;
            if (close) {
                ram8(v_soundControl) = SOUND_HELPER_JUMPS;
                e->unknown9 &= (uint8_t)~0x01;
                e->type = ENTITY_CAPSULE_HELPER_FALLING;
                ENT_HI(e, ySpeed) = HELPER_JUMP_SPEED;
                LIFTED_RETURN();
            }
        }
    }

    if (!(tile_attr_at(GROUND_ROW_Y, GROUND_ROW_X) & TILE_SOLID)) {
        e->type = ENTITY_CAPSULE_HELPER_FALLING; /* walked off a ledge */
        e->unknown3 |= ITEM_FALLING;
        LIFTED_RETURN();
    }
    if (e->unknown3 & ITEM_MOVING_RIGHT) ANIMATE_AND_RETURN(HELPER_WALK_RIGHT_ANIMATION);
    ANIMATE_AND_RETURN(HELPER_WALK_LEFT_ANIMATION);
}

/* $483F: helper still in play? Returns with carry set when the helper is on
 * screen above the bottom area (Y < $AC); otherwise removes it (see $484D)
 * and returns with carry clear. */
LIFTED(_LABEL_4846_, 0x483F) {
    Entity *e = entity_at(cpu.ix);
    if (entity_on_screen(e) && ENT_Y(e) < 0xAC) {
        set_carry(true);
        LIFTED_RETURN();
    }
    TAIL_CALL(f__LABEL_4854_);
}

/* $484D: removes a helper; if the capsule (slot 4) is already gone, the item
 * is used up (see $4853). Returns with carry clear. */
LIFTED(_LABEL_4854_, 0x484D) {
    if (entity_at(ITEM_SLOT)->type != 0) TAIL_CALL(f_sub_485E);
    TAIL_CALL(f__LABEL_485A_);
}

/* $4853: ends the item in use (Alex may attack again, action state reset),
 * then removes the current entity. Returns with carry clear. */
LIFTED(_LABEL_485A_, 0x4853) {
    alex()->unknown8 &= (uint8_t)~(ALEX_ATTACKING | ALEX_WEAPON_OUT | ALEX_ATTACK_UNSPENT);
    ram8(v_alexActionState) = 0;
    TAIL_CALL(f_sub_485E);
}

/* $485E: removes the current entity, returns with carry clear. */
LIFTED(sub_485E, 0x485E) {
    call_routine(f_destroyCurrentEntity);
    set_carry(false);
    LIFTED_RETURN();
}

/* $4863: capsule A helper in the air (jumping at an enemy or falling). Lands
 * back into the walking state. */
LIFTED(updateEntity0x0A, 0x4863) {
    Entity *e = entity_at(cpu.ix);
    if (e->flags & EF_HIT) TAIL_CALL(f__LABEL_4854_);
    call_routine(f__LABEL_4846_);
    if (!(cpu.f & FLAG_C)) LIFTED_RETURN();
    cpu.de = (uint16_t)(GROUND_ROW_Y << 8 | GROUND_ROW_X);
    call_routine(f__LABEL_4944_);
    if (!(e->unknown3 & ITEM_LANDED)) LIFTED_RETURN();
    e->unknown3 &= (uint8_t)~(ITEM_LANDED | ITEM_FALLING);
    e->type = ENTITY_CAPSULE_HELPER_WALKING;
    LIFTED_RETURN();
}

/* $493D: gravity for thrown items and helpers. DE = offset (Y, X) of the
 * ground probe below the object. Sets ITEM_FALLING once the vertical speed
 * becomes positive; while falling, lands on solid ground: the speed is cut so
 * that the next move ends exactly on the tile, and ITEM_LANDED is set. */
LIFTED(_LABEL_4944_, 0x493D) {
    Entity *e = entity_at(cpu.ix);
    uint8_t probe_y = cpu.d, probe_x = cpu.e;
    uint32_t speed = (uint32_t)e->ySpeed + ITEM_GRAVITY;
    e->ySpeed = (uint16_t)speed;
    if (speed > 0xFFFF) e->unknown3 |= ITEM_FALLING; /* crossed from rising to falling */
    if (!(e->unknown3 & ITEM_FALLING)) LIFTED_RETURN();

    uint8_t pixels = ENT_HI(e, ySpeed);
    if (pixels >= ITEM_MAX_FALL) ENT_LO(e, ySpeed) = 0;
    /* Probe where the object will be after this frame's move. */
    if (!(tile_attr_at((uint8_t)(probe_y + pixels), probe_x) & TILE_SOLID)) LIFTED_RETURN();
    uint8_t depth_in_tile = cpu.b & 7;
    ENT_HI(e, ySpeed) = (uint8_t)(ENT_HI(e, ySpeed) - depth_in_tile);
    ENT_LO(e, ySpeed) = 0;
    e->unknown3 |= ITEM_LANDED;
    LIFTED_RETURN();
}

/* ---------------------------------------------------- Power Bracelet shockwave */

/* Puts the shockwave in slot 2 at Alex's height (+8), moving at `speed`. */
static void spawn_shockwave(uint8_t x, uint16_t sprite, uint16_t speed) {
    Entity *shot = entity_at(WEAPON_SLOT);
    ENT_X(shot) = x;
    ENT_Y(shot) = (uint8_t)(ENT_Y(alex()) + 8);
    shot->spriteDescriptorPointer = sprite;
    shot->xSpeed = speed;
    shot->ySpeed = 0;
    shot->type = ENTITY_SHOCKWAVE;
    alex()->unknown8 |= ALEX_ATTACKING | ALEX_WEAPON_OUT | ALEX_ATTACK_UNSPENT;
}

/* $48E1: A = X: shockwave going left. */
LIFTED(sub_48E1, 0x48E1) {
    spawn_shockwave(cpu.a, SHOCKWAVE_LEFT_SPRITE, (uint16_t)-SHOCKWAVE_SPEED);
    LIFTED_RETURN();
}

/* $48E9: A = X: shockwave going right. */
LIFTED(sub_48E9, 0x48E9) {
    spawn_shockwave(cpu.a, SHOCKWAVE_RIGHT_SPRITE, SHOCKWAVE_SPEED);
    LIFTED_RETURN();
}

/* $48EF: A = X, HL = sprite descriptor, DE = horizontal speed. */
LIFTED(sub_48EF, 0x48EF) {
    spawn_shockwave(cpu.a, cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* $48BE: Alex punches with the Power Bracelet (action state 5, IX = Alex):
 * a shockwave is fired in front of him unless he is too close to the screen
 * edge, and the punching sprite is shown. */
LIFTED(_LABEL_48C5_, 0x48BE) {
    Entity *e = entity_at(cpu.ix);
    uint8_t x = ENT_X(alex());
    uint16_t sprite;
    if (!(e->unknown3 & ALEX_FACING_RIGHT)) {
        if (x >= 8) spawn_shockwave((uint8_t)(x - 8), SHOCKWAVE_LEFT_SPRITE, (uint16_t)-SHOCKWAVE_SPEED);
        sprite = ALEX_PUNCHING_LEFT_SPRITE;
    } else {
        if (x + 0x10 <= 0xFF) spawn_shockwave((uint8_t)(x + 0x10), SHOCKWAVE_RIGHT_SPRITE, SHOCKWAVE_SPEED);
        sprite = ALEX_PUNCHING_RIGHT_SPRITE;
    }
    ram8(v_soundControl) = SOUND_SHOCK_WAVE;
    cpu.hl = sprite;
    TAIL_CALL(f_loadAlexSpriteDescriptor);
}

/* $4914: Power Bracelet shockwave (slot 2). Flies straight, breaks every
 * breakable block on its way and vanishes on other walls or near the screen
 * edges; then Alex may attack again. */
LIFTED(updateShockwave, 0x4914) {
    Entity *e = entity_at(cpu.ix);
    Entity *shot = entity_at(WEAPON_SLOT);
    bool vanish = (ENT_SCREEN_X(shot) | ENT_SCREEN_Y(e)) != 0 || ENT_X(shot) < 0x0C || ENT_X(shot) >= 0xF4;
    if (!vanish) {
        uint8_t attr = tile_attr_at(0x04, 0x04);
        if (!(attr & TILE_SOLID)) LIFTED_RETURN();
        if (attr & TILE_BREAKABLE) TAIL_CALL(f__LABEL_4578_); /* break it and keep going */
    }
    alex()->unknown8 &= (uint8_t)~(ALEX_ATTACKING | ALEX_WEAPON_OUT | ALEX_ATTACK_UNSPENT);
    DESTROY_AND_RETURN();
}
