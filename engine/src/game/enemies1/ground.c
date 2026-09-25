/*
 * Ground enemies and enemy deaths ($4DA6-$4E28, $52E0-$571B): the 8-hit
 * swordsman of The Blakwoods, the leaf-throwing monkey and its leaves, the
 * monster frog, death puffs, and the aiming helper
 * getVelocitiesToPursuitAlex.
 */
#include "game/enemies1/enemies1.h"

/* Animation / sprite descriptors. */
#define SWORDSMAN_WALK_LEFT_ANIMATION 0x8A35
#define SWORDSMAN_WALK_RIGHT_ANIMATION 0x8A3F
#define SWORDSMAN_ATTACK_LEFT_ANIMATION 0x8A3A
#define SWORDSMAN_ATTACK_RIGHT_ANIMATION 0x8A44
#define MONKEY_ANIMATION 0x84ED
#define MONKEY_LEAF_SPRITE 0x851D
#define BIG_DEATH_PUFF_ANIMATION 0x8175
#define SMOKE_PUFF_ANIMATION 0x8170
#define FROG_SITTING_SPRITE 0x854C
#define FROG_JUMPING_SPRITE 0x855A

/* Shared "an event is in progress" flag (boss fights, bonus levels...),
 * cleared when a big death puff ends. */
#define EVENT_IN_PROGRESS _RAM_C07F_

/* ------------------------------------------------------- aiming at Alex */

/* $4E0D: divides the 16-bit HL by E, shifting 8 quotient bits into L, then
 * rounds up when twice the remainder reaches E. With HL = n * 256 and n <= E
 * this is round(n * 256 / E), a fraction in 1/256 units.
 * QUIRK: the rounding test drops bit 8 of twice the remainder. */
static uint8_t divide_to_fraction(uint16_t dividend, uint8_t divisor) {
    uint16_t hl = dividend;
    unsigned quotient_bit = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t shifted = ((uint32_t)hl << 1) | quotient_bit;
        bool overflow = shifted > 0xFFFF;
        hl = (uint16_t)shifted;
        uint8_t remainder = (uint8_t)(hl >> 8);
        quotient_bit = overflow || remainder >= divisor;
        if (quotient_bit) hl = (uint16_t)((uint8_t)(remainder - divisor) << 8 | (hl & 0xFF));
    }
    uint8_t quotient = (uint8_t)((hl << 1) | quotient_bit);
    uint8_t twice_remainder = (uint8_t)((hl >> 8) << 1);
    if (twice_remainder >= divisor) quotient++;
    return quotient;
}

/* $4E0D: HL = dividend, E = divisor -> L = quotient (C is preserved). */
LIFTED(sub_4E0D, 0x4E0D) {
    cpu.l = divide_to_fraction(cpu.hl, cpu.e);
    LIFTED_RETURN();
}

/* $4DA6: velocity (HL = X speed, DE = Y speed, 8.8 fixed point) that moves
 * the entity in IX straight towards Alex: 1 px/frame on the major axis, the
 * proportional fraction on the other one. Only the monkey leaf ($29) uses the
 * exact fraction; for any other entity the minor speed is divided by 4 (the
 * major speed is not, so the aim is off). */
LIFTED(getVelocitiesToPursuitAlex, 0x4DA6) {
    Entity *e = entity_at(cpu.ix);
    bool alex_above = ENT_Y(alex()) < ENT_Y(e);
    uint8_t dy = (uint8_t)(ENT_Y(alex()) - ENT_Y(e));
    if (dy == 0) dy = 1;
    if (alex_above) dy = (uint8_t)-dy;
    bool alex_left = ENT_X(alex()) < ENT_X(e);
    uint8_t dx = (uint8_t)(ENT_X(alex()) - ENT_X(e));
    if (dx == 0) dx = 1;
    if (alex_left) dx = (uint8_t)-dx;

    bool x_is_major = dy < dx;
    uint8_t major = x_is_major ? dx : dy, minor = x_is_major ? dy : dx;
    uint16_t minor_speed = divide_to_fraction((uint16_t)(minor << 8), major);
    if (e->type != ENTITY_MONKEY_LEAF) minor_speed >>= 2;
    uint16_t major_speed = 0x0100;

    uint16_t y_speed = x_is_major ? minor_speed : major_speed;
    uint16_t x_speed = x_is_major ? major_speed : minor_speed;
    if (alex_above) y_speed = (uint16_t)-y_speed;
    if (alex_left) x_speed = (uint16_t)-x_speed;
    cpu.de = y_speed;
    cpu.hl = x_speed;
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ enemy deaths */

/* $559E: ordinary enemy killed: points, then it turns into a smoke puff. */
LIFTED(killEnemy, 0x559E) {
    Entity *e = entity_at(cpu.ix);
    call_routine(f_earnEntityPoints);
    ram8(v_soundControl) = SOUND_SMOKE_PUFF;
    e->type = ENTITY_SMOKE_PUFF;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    LIFTED_RETURN();
}

/* $5540: 8-hit enemy defeated: points, then a big death puff that leaves a
 * rice ball (unknown1 = 1). */
LIFTED(killOpponent, 0x5540) {
    Entity *e = entity_at(cpu.ix);
    ram8(v_soundControl) = SOUND_BOSS_DEFEATED;
    call_routine(f_earnEntityPoints);
    e->unknown1 = 1;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->type = ENTITY_BIG_DEATH_PUFF;
    LIFTED_RETURN();
}

/* $567D: smoke puff of a killed enemy: plays its animation once. */
LIFTED(updateSmokePuff, 0x567D) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->animationFrame = 0;
        e->animationTimer = 0x10;
        e->animationTimerResetValue = 0x10;
        e->xSpeed = 0;
        e->ySpeed = 0;
    }
    if (!entity_on_screen(e)) DESTROY_AND_RETURN();
    if (e->animationFrame == 1 && e->animationTimer == 1) DESTROY_AND_RETURN(); /* last frame */
    ANIMATE_AND_RETURN(SMOKE_PUFF_ANIMATION);
}

/* $5622: big death puff; at the end of its animation it becomes a rice ball
 * if unknown1 is set, else disappears. */
LIFTED(updateEntity0x43, 0x5622) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->animationFrame = 0;
        e->animationTimer = 0x10;
        e->animationTimerResetValue = 0x10;
        e->xSpeed = 0;
        e->ySpeed = 0;
    }
    if (!entity_on_screen(e)) DESTROY_AND_RETURN();
    if (e->animationFrame != 3 || e->animationTimer != 1) ANIMATE_AND_RETURN(BIG_DEATH_PUFF_ANIMATION);
    ram8(EVENT_IN_PROGRESS) = 0;
    if (e->unknown1 == 0) DESTROY_AND_RETURN();
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->type = ENTITY_RICE_BALL;
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ the swordsman */

/* The swordsman (types $25-$28) walks towards Alex, attacks when he is within
 * 32 pixels in front of it and takes 8 hits: each hit knocks it back for 8
 * frames (type $4A). Fields:
 *   unknown5        hits taken
 *   unknown2        hitbox index (swapped with $A8 during the attack test,
 *                   saved in unknown6)
 *   unknown9-11     type and speed to restore after a knock-back
 *   battleDecision  knock-back frames left */
#define SWORDSMAN_HIT_POINTS 8
#define SWORDSMAN_VULNERABLE_HITBOX 0xA8
#define SWORDSMAN_REACH 0x20
#define SWORDSMAN_SIGHT_OFFSET 0x38 /* right-facing swordsman measures from x + $38 */
#define SWORDSMAN_WALK_TIMER 0x12
#define SWORDSMAN_ATTACK_TIMER 0x20
#define SWORDSMAN_KNOCKBACK_FRAMES 8

/* $556A: stops the swordsman (used while Alex is dead). */
LIFTED(_LABEL_5571_, 0x556A) {
    entity_at(cpu.ix)->xSpeed = 0;
    LIFTED_RETURN();
}

/* $5350: turn to walk right. */
LIFTED(_LABEL_5357_, 0x5350) {
    Entity *e = entity_at(cpu.ix);
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->type = ENTITY_SWORDSMAN_WALK_RIGHT;
    LIFTED_RETURN();
}

/* $53BF: turn to walk left. */
LIFTED(_LABEL_53C6_, 0x53BF) {
    Entity *e = entity_at(cpu.ix);
    e->type = ENTITY_SWORDSMAN_WALK_LEFT;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    LIFTED_RETURN();
}

/* True when Alex is dead and the swordsman on screen: it then stands still. */
static bool swordsman_waits_for_respawn(const Entity *e) {
    return entity_on_screen(e) && alex()->state == ALEX_DEAD;
}

/* Hurts Alex on contact and tests Alex's attack against the swordsman's
 * vulnerable hitbox. */
static bool swordsman_is_hit(Entity *e) {
    hurt_alex_on_contact();
    e->unknown6 = e->unknown2;
    e->unknown2 = SWORDSMAN_VULNERABLE_HITBOX;
    if (alex_attack_hits()) return true;
    e->unknown2 = e->unknown6;
    return false;
}

static void swordsman_start(Entity *e, uint8_t timer) {
    e->animationTimer = timer;
    e->animationTimerResetValue = timer;
    e->animationFrame = 0;
}

/* $52E0: swordsman walking left (-0.375 px/frame). */
LIFTED(updateEntity0x25, 0x52E0) {
    Entity *e = entity_at(cpu.ix);
    if (swordsman_waits_for_respawn(e)) TAIL_CALL(f__LABEL_5571_);
    if (!(e->flags & EF_INITIALIZED)) {
        swordsman_start(e, SWORDSMAN_WALK_TIMER);
        if (!entity_on_screen(e)) ANIMATE_AND_RETURN(SWORDSMAN_WALK_LEFT_ANIMATION);
        e->flags |= EF_INITIALIZED;
        e->unknown1 = 0x81;
        e->xSpeed = 0xFFA0;
    }
    if (swordsman_is_hit(e)) TAIL_CALL(f__LABEL_54DF_);
    uint8_t alex_x = ENT_X(alex());
    if (alex_x >= ENT_X(e)) TAIL_CALL(f__LABEL_5357_); /* Alex behind: turn */
    if (ENT_X(e) - alex_x < SWORDSMAN_REACH) {
        e->type = ENTITY_SWORDSMAN_ATTACK_LEFT;
        e->flags &= (uint8_t)~EF_INITIALIZED;
        LIFTED_RETURN();
    }
    ANIMATE_AND_RETURN(SWORDSMAN_WALK_LEFT_ANIMATION);
}

/* $5359: swordsman walking right (+0.375 px/frame). */
LIFTED(updateEntity0x26, 0x5359) {
    Entity *e = entity_at(cpu.ix);
    if (swordsman_waits_for_respawn(e)) TAIL_CALL(f__LABEL_5571_);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        swordsman_start(e, SWORDSMAN_WALK_TIMER);
        e->xSpeed = 0x0060;
    }
    if (swordsman_is_hit(e)) TAIL_CALL(f__LABEL_54DF_);
    /* QUIRK: 8-bit sum, wraps near the right edge of the screen. */
    uint8_t sight = (uint8_t)(ENT_X(e) + SWORDSMAN_SIGHT_OFFSET);
    uint8_t alex_x = ENT_X(alex());
    if (alex_x < sight) TAIL_CALL(f__LABEL_53C6_);
    if (alex_x - sight < SWORDSMAN_REACH) {
        e->type = ENTITY_SWORDSMAN_ATTACK_RIGHT;
        e->flags &= (uint8_t)~EF_INITIALIZED;
        LIFTED_RETURN();
    }
    ANIMATE_AND_RETURN(SWORDSMAN_WALK_RIGHT_ANIMATION);
}

/* $53C8: swordsman attacking to the left (-0.25 px/frame). At the start of each
 * animation loop it decides again: turn if Alex is behind, walk if he is out
 * of reach. */
LIFTED(updateEntity0x27, 0x53C8) {
    Entity *e = entity_at(cpu.ix);
    if (swordsman_waits_for_respawn(e)) TAIL_CALL(f__LABEL_5571_);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        swordsman_start(e, SWORDSMAN_ATTACK_TIMER);
        e->xSpeed = 0xFFC0;
        ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_LEFT_ANIMATION);
    }
    if (!entity_on_screen(e)) ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_LEFT_ANIMATION);
    if (swordsman_is_hit(e)) TAIL_CALL(f__LABEL_54DF_);
    bool loop_start = e->animationTimer == SWORDSMAN_ATTACK_TIMER;
    if (e->animationFrame != 0) {
        if (loop_start) ram8(v_soundControl) = SOUND_SWORDSMAN_ATTACK;
        ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_LEFT_ANIMATION);
    }
    if (loop_start) {
        uint8_t alex_x = ENT_X(alex());
        if (alex_x >= ENT_X(e)) TAIL_CALL(f__LABEL_5357_);
        if (ENT_X(e) - alex_x >= SWORDSMAN_REACH) TAIL_CALL(f__LABEL_53C6_);
    }
    ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_LEFT_ANIMATION);
}

/* $544A: swordsman attacking to the right (+0.25 px/frame). */
LIFTED(updateEntity0x28, 0x544A) {
    Entity *e = entity_at(cpu.ix);
    if (swordsman_waits_for_respawn(e)) TAIL_CALL(f__LABEL_5571_);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        swordsman_start(e, SWORDSMAN_ATTACK_TIMER);
        e->xSpeed = 0x0040;
        ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_RIGHT_ANIMATION);
    }
    if (!entity_on_screen(e)) ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_RIGHT_ANIMATION);
    if (swordsman_is_hit(e)) TAIL_CALL(f__LABEL_54DF_);
    bool loop_start = e->animationTimer == SWORDSMAN_ATTACK_TIMER;
    if (loop_start) ram8(v_soundControl) = SOUND_SWORDSMAN_ATTACK;
    if (e->animationFrame == 0 && loop_start) {
        uint8_t sight = (uint8_t)(ENT_X(e) + SWORDSMAN_SIGHT_OFFSET);
        uint8_t alex_x = ENT_X(alex());
        if (alex_x < sight) TAIL_CALL(f__LABEL_53C6_);
        if (alex_x - sight >= SWORDSMAN_REACH) TAIL_CALL(f__LABEL_5357_);
    }
    ANIMATE_AND_RETURN(SWORDSMAN_ATTACK_RIGHT_ANIMATION);
}

/* $54D8: the swordsman was hit: remembers its type and speed, is knocked back
 * (1 px/frame away from its walking direction) for 8 frames as type $4A and
 * counts the hit. Continues into updateEntity0x4A. */
LIFTED(_LABEL_54DF_, 0x54D8) {
    Entity *e = entity_at(cpu.ix);
    e->unknown9 = e->type;
    e->unknown10 = ENT_LO(e, xSpeed);
    e->unknown11 = ENT_HI(e, xSpeed);
    bool was_moving_left = (e->unknown11 & 0x80) != 0;
    e->xSpeed = was_moving_left ? 0x0100 : 0xFF00;
    e->type = ENTITY_STAGGERED;
    e->battleDecision = SWORDSMAN_KNOCKBACK_FRAMES;
    e->unknown5++;
    ram8(v_soundControl) = SOUND_SMOKE_PUFF;
    TAIL_CALL(f_updateEntity0x4A);
}

/* $550E: knocked-back swordsman. Dies after 8 hits; otherwise goes back to its
 * previous type and speed when the knock-back ends. */
LIFTED(updateEntity0x4A, 0x550E) {
    Entity *e = entity_at(cpu.ix);
    if (swordsman_waits_for_respawn(e)) TAIL_CALL(f__LABEL_5571_);
    if (e->unknown5 >= SWORDSMAN_HIT_POINTS) TAIL_CALL(f_killOpponent);
    if (--e->battleDecision != 0) LIFTED_RETURN();
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->type = e->unknown9;
    ENT_LO(e, xSpeed) = e->unknown10;
    ENT_HI(e, xSpeed) = e->unknown11;
    LIFTED_RETURN();
}

/* ---------------------------------------------------------------- monkey */

#define MONKEY_THROW_TIMER 0x40

/* $5573: monkey sitting in a tree. Each time its animation loops it throws
 * a leaf (type $29) at Alex from the first free projectile slot. */
LIFTED(updateEntity0x2A, 0x5573) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = MONKEY_THROW_TIMER;
        e->animationTimerResetValue = MONKEY_THROW_TIMER;
        if (entity_on_screen(e)) e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        ANIMATE_AND_RETURN(MONKEY_ANIMATION);
    }
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
    if (e->animationFrame != 0 || e->animationTimer != MONKEY_THROW_TIMER) ANIMATE_AND_RETURN(MONKEY_ANIMATION);

    uint16_t slot = PROJECTILE_SLOT;
    for (int i = 0; i < PROJECTILE_SLOT_COUNT; i++, slot += ENTITY_SIZE) {
        Entity *leaf = entity_at(slot);
        if (leaf->type != 0) continue;
        ram8(v_soundControl) = SOUND_MONKEY_LEAF;
        leaf->type = ENTITY_MONKEY_LEAF;
        ENT_X(leaf) = ENT_X(e);
        ENT_Y(leaf) = ENT_Y(e);
        break;
    }
    cpu.iy = slot;
    ANIMATE_AND_RETURN(MONKEY_ANIMATION);
}

/* $55EC: leaf thrown by the monkey: flies straight at where Alex was when it
 * was thrown; hurts Alex, cannot be destroyed by attacks. */
LIFTED(updateMonkeyLeaf, 0x55EC) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->unknown3 = 0x12;
        call_routine(f_getVelocitiesToPursuitAlex);
        e->xSpeed = cpu.hl;
        e->ySpeed = cpu.de;
        e->flags |= EF_DESTROY_OFFSCREEN;
        e->spriteDescriptorPointer = MONKEY_LEAF_SPRITE;
        LIFTED_RETURN();
    }
    if (!entity_on_screen(e)) DESTROY_AND_RETURN();
    TAIL_CALL(f_tryToKillAlexIfColliding);
}

/* ----------------------------------------------------------- monster frog */

#define FROG_WAIT_FRAMES 0x10
#define FROG_JUMP_SPEED 0xFE80 /* -1.5 px/frame */
#define FROG_GRAVITY 0x0008

/* $56C5: monster frog sitting. Jumps again as soon as it has waited 16
 * frames and stands on the ground. battleDecision: frames to wait. */
LIFTED(updateEntity0x2F, 0x56C5) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->spriteDescriptorPointer = FROG_SITTING_SPRITE;
        if (!entity_on_screen(e)) LIFTED_RETURN();
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->battleDecision = FROG_WAIT_FRAMES;
    }
    if (!entity_on_screen(e)) LIFTED_RETURN();
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
    if (--e->battleDecision != 0) LIFTED_RETURN();
    e->battleDecision = 1;
    if (!(tile_attr_at(0x11, 0x08) & TILE_SOLID)) LIFTED_RETURN();
    e->type = ENTITY_MONSTER_FROG_JUMPING;
    e->ySpeed = FROG_JUMP_SPEED;
    e->spriteDescriptorPointer = FROG_JUMPING_SPRITE;
    LIFTED_RETURN();
}

/* $571C: monster frog in the air; lands on solid ground.
 * QUIRK: the ground test is skipped only while the speed's high byte is $FF,
 * so it also runs during the fast first part of the jump. */
LIFTED(updateMonsterFrogJumping, 0x571C) {
    Entity *e = entity_at(cpu.ix);
    if (!entity_on_screen(e)) LIFTED_RETURN();
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
    e->ySpeed = (uint16_t)(e->ySpeed + FROG_GRAVITY);
    if (ENT_HI(e, ySpeed) == 0xFF) LIFTED_RETURN();
    if (!(tile_attr_at(0x11, 0x08) & TILE_SOLID)) LIFTED_RETURN();
    e->ySpeed = 0;
    e->type = ENTITY_MONSTER_FROG;
    e->spriteDescriptorPointer = FROG_SITTING_SPRITE;
    e->battleDecision = FROG_WAIT_FRAMES;
    LIFTED_RETURN();
}
