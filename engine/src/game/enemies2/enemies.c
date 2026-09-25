/*
 * Level enemies at $5C2F-$63F4 (except the story characters, see story.c):
 *  - the eight-punch bull of the Village of Namui ($46-$49),
 *  - circular flames, patrolling flames/scorpions,
 *  - lightning clouds of Bingoo Lowland ($40/$41),
 *  - water leapers ($42), walking and hopping monsters ($54/$55),
 *  - static fire hazards ($57).
 *
 * Common conventions: Entity.flags bit 0 = initialized; an enemy waits while
 * off screen before its setup; killEnemy/destroyCurrentEntity end it.
 */
#include "game/enemies2/enemies2.h"

/* ------------------------------------------------ Namui bull ($46-$49)
 * Walks left ($46) and right ($48) between x = $18 and x = $D8. Each punch
 * (hit count in battleDecision) knocks it back for a moment ($47/$49) and
 * makes it walk faster; the eighth punch defeats it. While alive it keeps
 * v_storyEventCounter ($C07F) at 1, which silences the village elder. */
#define BULL_HITS_TO_DEFEAT 8
#define BULL_LEFT_TURN_X 0x18
#define BULL_RIGHT_TURN_X 0xD8
#define BULL_WALK_SPEED 0xFF80
#define BULL_UNKNOWN1_INIT 0x82
#define BULL_WALK_ANIMATION_LEFT 0x8453
#define BULL_WALK_ANIMATION_RIGHT 0x8405
#define BULL_KNOCKED_SPRITE_LEFT 0x8458
#define BULL_KNOCKED_SPRITE_RIGHT 0x840A
/* Per hit count (1..7): knock-back duration, then walking speed increase. */
#define BULL_KNOCKBACK_TABLE 0x5D75

/* $5C2F: bull walking left. */
LIFTED(updateEntity0x46, 0x5C2F) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        ram8(v_storyEventCounter) = 1;
        e->animationTimer = 1;
        e->animationTimerResetValue = 0x10;
        e->battleDecision = 0; /* punches taken */
        if (is_offscreen(e)) ANIMATE(BULL_WALK_ANIMATION_LEFT);
        e->flags |= EF_INITIALIZED;
        e->unknown1 = BULL_UNKNOWN1_INIT;
        e->xSpeed = BULL_WALK_SPEED;
    }
    if (x_pixel(e) < BULL_LEFT_TURN_X) {
        e->type = ENTITY_BULL_WALKING_RIGHT;
        TAIL_CALL(f__LABEL_5CA0_);
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (carry_set()) ANIMATE(BULL_WALK_ANIMATION_LEFT);

    if (++e->battleDecision >= BULL_HITS_TO_DEFEAT) TAIL_CALL(f__LABEL_5D7B_);
    ram8(v_soundControl) = SOUND_SMOKE_PUFF;
    e->type = ENTITY_BULL_KNOCKED_RIGHT;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->spriteDescriptorPointer = BULL_KNOCKED_SPRITE_LEFT;
    TAIL_CALL(f__LABEL_5CA0_);
}

/* $5C99: turn around (reverse the horizontal speed, see reverse_x_speed).
 * Returns A = new low byte of the speed. */
LIFTED(_LABEL_5CA0_, 0x5C99) {
    cpu.a = reverse_x_speed(entity_at(cpu.ix));
    LIFTED_RETURN();
}

/* Knock-back setup shared by $47 and $49: duration and speed-up for this hit. */
static void start_knockback(Entity *e) {
    uint16_t entry = (uint16_t)(BULL_KNOCKBACK_TABLE + (uint8_t)(e->battleDecision * 2));
    e->flags |= EF_INITIALIZED;
    e->unknown6 = rd8(entry);                    /* knock-back frames */
    e->unknown5 = rd8((uint16_t)(entry + 1));    /* speed increase */
}

/* Knock-back lasts unknown6 frames on screen (ends at once off screen). */
static bool knockback_over(Entity *e) {
    return is_offscreen(e) || --e->unknown6 == 0;
}

/* $5CA9: bull knocked back to the right after a punch; then walks left
 * again, faster. */
LIFTED(updateEntity0x47, 0x5CA9) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        start_knockback(e);
        LIFTED_RETURN();
    }
    if (!knockback_over(e)) LIFTED_RETURN();
    e->type = ENTITY_BULL_WALKING_LEFT;
    CALL_ROUTINE(f__LABEL_5CA0_);
    e->xSpeed = (uint16_t)(e->xSpeed - e->unknown5);
    LIFTED_RETURN();
}

/* $5CF0: bull walking right. */
LIFTED(updateEntity0x48, 0x5CF0) {
    Entity *e = entity_at(cpu.ix);
    if (x_pixel(e) >= BULL_RIGHT_TURN_X) {
        e->type = ENTITY_BULL_WALKING_LEFT;
        TAIL_CALL(f__LABEL_5CA0_);
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (carry_set()) ANIMATE(BULL_WALK_ANIMATION_RIGHT);

    if (++e->battleDecision >= BULL_HITS_TO_DEFEAT) TAIL_CALL(f__LABEL_5D7B_);
    ram8(v_soundControl) = SOUND_SMOKE_PUFF;
    e->type = ENTITY_BULL_KNOCKED_LEFT;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->spriteDescriptorPointer = BULL_KNOCKED_SPRITE_RIGHT;
    TAIL_CALL(f__LABEL_5CA0_);
}

/* $5D2F: bull knocked back to the left; then walks right again, faster. */
LIFTED(updateEntity0x49, 0x5D2F) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        start_knockback(e);
        LIFTED_RETURN();
    }
    if (!knockback_over(e)) LIFTED_RETURN();
    e->type = ENTITY_BULL_WALKING_RIGHT;
    CALL_ROUTINE(f__LABEL_5CA0_);
    e->xSpeed = (uint16_t)(e->xSpeed + e->unknown5);
    LIFTED_RETURN();
}

/* $5D74 (jumps into the tail of killOpponent at $5555): the bull is
 * defeated: points, fanfare, and the generic "defeated" updater with
 * unknown1 = 0 (killOpponent uses 1). */
LIFTED(_LABEL_5D7B_, 0x5D74) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f_earnEntityPoints);
    ram8(v_soundControl) = SOUND_BOSS_DEFEATED;
    e->unknown1 = 0;
    e->flags &= (uint8_t)~EF_INITIALIZED;
    e->type = ENTITY_DEFEATED;
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ fire hazards */
#define FLAME_ANIMATION 0x85A6
#define FLAME_ANIMATION_DELAY 0x10

/* $5D8D: flame circling around its spawn point (unknownAnimate computes the
 * position on the orbit). Fields: unknown11/unknown10 = orbit centre x/y
 * (pixels), data = centre x fraction, unknown9 = radius $20,
 * stateTimer/unknown8 = 16-bit angle (+2 per frame), state/unknown7 = the
 * centre's off-screen counters (same convention as isOffScreenFlags). */
LIFTED(updateCircularFlame, 0x5D8D) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->animationTimer = FLAME_ANIMATION_DELAY;
        e->animationTimerResetValue = FLAME_ANIMATION_DELAY;
        e->unknown11 = x_pixel(e);
        e->unknown10 = y_pixel(e);
        e->unknown9 = 0x20;
        e->stateTimer = 0;
        e->unknown8 = 0;
        e->state = (uint8_t)e->isOffScreenFlags;
        e->unknown7 = (uint8_t)(e->isOffScreenFlags >> 8);
        ANIMATE(FLAME_ANIMATION);
    }
    if (!is_offscreen(e)) CALL_ROUTINE(f_tryToKillAlexIfColliding);

    /* Keep the orbit centre fixed in the level while the screen scrolls
     * (the scroll speed is the apparent entity speed, <= 0 since the screen
     * only scrolls right). Like the engine's own movement code, wrapping past
     * the left edge (no carry) counts one more screen to the left. */
    uint16_t scroll_speed = ram16(v_horizontalScrollSpeed);
    if (scroll_speed != 0) {
        uint32_t centre = (uint32_t)((e->unknown11 << 8) | e->data) + scroll_speed;
        e->unknown11 = (uint8_t)(centre >> 8);
        e->data = (uint8_t)centre;
        if (centre <= 0xFFFF) e->state++;
    }
    uint16_t angle = (uint16_t)(((e->unknown8 << 8) | e->stateTimer) + 2);
    e->stateTimer = (uint8_t)angle;
    e->unknown8 = (uint8_t)(angle >> 8);
    CALL_ROUTINE(f_unknownAnimate);
    /* Once the flame is a screen to the left, let the engine destroy it. */
    if ((uint8_t)e->isOffScreenFlags == 1) e->flags |= EF_DESTROY_OFFSCREEN;
    ANIMATE(FLAME_ANIMATION);
}

/* Patrolling flame (data != 0, cannot be punched) or scorpion (data == 0).
 * It turns around at walls and at the edge of its platform. */
#define PATROL_SPEED_LEFT 0xFF80
#define PATROL_SPEED_RIGHT 0x0080
#define SCORPION_LEFT_ANIMATION 0x826B
#define SCORPION_RIGHT_ANIMATION 0x8286
#define TILE_SOLID 0x80

/* Tile checks in front of the patroller: returns true when it must turn
 * (wall at mid height, or no floor under its front edge). */
static bool patrol_blocked(uint8_t front_x_offset) {
    cpu.de = (uint16_t)(0x0900 | front_x_offset); /* 9 px down, front edge */
    call_leaf(f_getNearEntityTileAttrWithOffset);
    if (cpu.a & TILE_SOLID) return true;
    cpu.d = 8; /* the tile 8 px further down, same column (B, L from above) */
    call_leaf(f__LABEL_7CA3_);
    return !(rd8(cpu.hl) & TILE_SOLID);
}

static uint16_t patrol_animation(const Entity *e, uint16_t scorpion_animation) {
    return e->data ? FLAME_ANIMATION : scorpion_animation;
}

/* $5E0D: flame / scorpion moving left. */
LIFTED(updateflameOrScorpionLeft, 0x5E0D) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        if (!is_offscreen(e)) {
            e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
            e->animationTimer = FLAME_ANIMATION_DELAY;
            e->animationTimerResetValue = FLAME_ANIMATION_DELAY;
            e->xSpeed = PATROL_SPEED_LEFT;
        }
        ANIMATE(patrol_animation(e, SCORPION_LEFT_ANIMATION));
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (e->data == 0) { /* scorpions can be punched */
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (!carry_set()) TAIL_CALL(f_killEnemy);
    }
    if (patrol_blocked(0x00)) {
        e->type = ENTITY_FLAME_OR_SCORPION_RIGHT;
        e->xSpeed = PATROL_SPEED_RIGHT;
    }
    ANIMATE(patrol_animation(e, SCORPION_LEFT_ANIMATION));
}

/* $5E74: flame / scorpion moving right (always initialized by the left one). */
LIFTED(updateflameOrScorpionRight, 0x5E74) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (e->data == 0) {
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (!carry_set()) TAIL_CALL(f_killEnemy);
    }
    if (patrol_blocked(0x10)) {
        e->type = ENTITY_FLAME_OR_SCORPION_LEFT;
        e->xSpeed = PATROL_SPEED_LEFT;
    }
    ANIMATE(patrol_animation(e, SCORPION_RIGHT_ANIMATION));
}

/* ------------------------------------------- lightning cloud ($40 / $41)
 * Drifts left at 1 px/frame for 16 frames ($40), then brakes (+8/256 px per
 * frame) until it stops, and strikes: lightning sound and the strike
 * animation until its frame $13, then starts over as $40. unknown6 counts
 * the drift frames, then flags that the sound was played. */
#define CLOUD_SPRITE 0x85D5
#define CLOUD_STRIKE_ANIMATION 0x85E9
#define CLOUD_DRIFT_SPEED 0xFF00
#define CLOUD_DRIFT_FRAMES 0x10
#define CLOUD_BRAKING 0x0008
#define CLOUD_LAST_STRIKE_FRAME 0x13

/* $5EB3: cloud drifting. */
LIFTED(updateEntity0x40, 0x5EB3) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->spriteDescriptorPointer = CLOUD_SPRITE;
        if (is_offscreen(e)) LIFTED_RETURN();
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->unknown6 = 0;
        e->xSpeed = CLOUD_DRIFT_SPEED;
        e->animationTimer = 1;
        e->animationTimerResetValue = 1;
        e->animationFrame = 0;
        LIFTED_RETURN();
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (++e->unknown6 < CLOUD_DRIFT_FRAMES) LIFTED_RETURN();
    e->type = ENTITY_LIGHTNING_CLOUD_STRIKING;
    e->unknown6 = 0;
    LIFTED_RETURN();
}

/* $5EFE: cloud braking, then striking. */
LIFTED(updateEntity0x41, 0x5EFE) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    e->xSpeed = (uint16_t)(e->xSpeed + CLOUD_BRAKING);
    if ((e->xSpeed >> 8) == 0xFF) LIFTED_RETURN(); /* still moving left */
    if (e->unknown6 == 0) {
        e->unknown6 = 1;
        ram8(v_soundControl) = SOUND_LIGHTNING;
    }
    if (e->animationFrame == CLOUD_LAST_STRIKE_FRAME && e->animationTimer == 1) {
        e->type = ENTITY_LIGHTNING_CLOUD_DRIFTING;
        e->flags &= (uint8_t)~EF_INITIALIZED;
        e->unknown6 = 0;
        LIFTED_RETURN();
    }
    ANIMATE(CLOUD_STRIKE_ANIMATION);
}

/* ------------------------------------------------------ water leaper ($42)
 * Lake Fathom creature that jumps out of the water at the bottom of the screen: up at 1 px/frame and
 * left at 0.5 px/frame, pulled down by a random gravity (2 or 4 /256 px,
 * in unknown6); switches to its falling sprite once moving down. */
#define LEAPER_RISING_SPRITE 0x82BD
#define LEAPER_FALLING_SPRITE 0x82CB

/* $5F45 */
LIFTED(updateEntity0x42, 0x5F45) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->spriteDescriptorPointer = LEAPER_RISING_SPRITE;
        if (is_offscreen(e)) LIFTED_RETURN();
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->xSpeed = 0xFF80;
        e->ySpeed = 0xFF00;
        e->unknown6 = 2;
        if ((rt_read_r() & 0x07) >= 4) e->unknown6 = 4;
        LIFTED_RETURN();
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f_killEnemy);
    e->ySpeed = (uint16_t)(e->ySpeed + e->unknown6);
    if (e->ySpeed & 0x8000) LIFTED_RETURN(); /* still rising */
    e->spriteDescriptorPointer = LEAPER_FALLING_SPRITE;
    LIFTED_RETURN();
}

/* ------------------------------------------- walking monster ($54)
 * Walks towards the side where Alex was when it appeared, turns around at
 * walls, and falls (accelerating by $10/256 px per frame) when there is no
 * ground under it. unknown6 = x offset of the wall probe (2: left side,
 * $0E: right side); battleDecision:unknown5 = fall speed. */
#define WALKER_ANIMATION 0x8585
#define WALKER_FALL_START 0x0030
#define WALKER_GRAVITY 0x0010
#define PROBE_LEFT 0x02
#define PROBE_RIGHT 0x0E

static void walker_reset_fall(Entity *e) {
    e->battleDecision = 0;
    e->unknown5 = (uint8_t)WALKER_FALL_START;
    e->ySpeed = 0;
}

/* isEntityCollidingWithTerrainAtOffset (A = 8, D = 1, E = x offset):
 * wall check at the given side. */
#define CHECK_WALL(x_offset)                                    \
    do {                                                        \
        cpu.d = 1;                                              \
        cpu.e = (x_offset);                                     \
        cpu.a = 8;                                              \
        CALL_ROUTINE(f_isEntityCollidingWithTerrainAtOffset);   \
    } while (0)

/* $62A8 */
LIFTED(updateEntity0x54, 0x62A8) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = 8;
        e->animationTimerResetValue = 8;
        if (is_offscreen(e)) {
            walker_reset_fall(e);
            ANIMATE(WALKER_ANIMATION);
        }
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->battleDecision = 0;
        e->unknown5 = (uint8_t)WALKER_FALL_START;
        e->unknown6 = PROBE_RIGHT;
        e->xSpeed = 0x0060;
        if (x_pixel(entity_at(SLOT_ALEX)) < x_pixel(e)) {
            e->unknown6 = PROBE_LEFT;
            e->xSpeed = 0xFFA0;
        }
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f_killEnemy);

    CHECK_WALL(e->unknown6);
    if (carry_set()) {
        reverse_x_speed(e);
        e->unknown6 = (e->xSpeed >> 8) == 0xFF ? PROBE_LEFT : PROBE_RIGHT;
        ANIMATE(WALKER_ANIMATION);
    }
    /* Ground under its trailing edge ($11 px down): it only falls once it is
     * completely off a ledge. */
    cpu.e = e->unknown6 ^ 0x0C;
    cpu.d = 0x11;
    CALL_ROUTINE(f_getNearEntityTileAttrWithOffset);
    if (cpu.a & TILE_SOLID) {
        walker_reset_fall(e);
        ANIMATE(WALKER_ANIMATION);
    }
    uint16_t fall = (uint16_t)(((e->battleDecision << 8) | e->unknown5) + WALKER_GRAVITY);
    e->battleDecision = (uint8_t)(fall >> 8);
    e->unknown5 = (uint8_t)fall;
    e->ySpeed = fall;
    ANIMATE(WALKER_ANIMATION);
}

/* ------------------------------------------- hopping monster ($55)
 * Hops left/right in 1 px/frame jumps (gravity $10/256 px), bouncing again
 * whenever it lands and turning around at walls. unknown6 = x offset of the
 * probe on the side opposite to its direction (xor $0C gives the front). */
#define HOPPER_LEFT_ANIMATION 0x84A1
#define HOPPER_RIGHT_ANIMATION 0x84C2
#define HOPPER_JUMP_SPEED 0xFF00
#define HOPPER_GRAVITY 0x0010

static uint16_t hopper_animation(const Entity *e) {
    return (e->xSpeed >> 8) == 0xFF ? HOPPER_LEFT_ANIMATION : HOPPER_RIGHT_ANIMATION;
}

/* $6361 */
LIFTED(updateEntity0x55, 0x6361) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = 8;
        e->animationTimerResetValue = 8;
        if (is_offscreen(e)) ANIMATE(hopper_animation(e));
        e->xSpeed = 0xFF80;
        e->ySpeed = HOPPER_JUMP_SPEED;
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->unknown6 = PROBE_RIGHT;
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f_killEnemy);

    CHECK_WALL(e->unknown6 ^ 0x0C);
    if (carry_set()) {
        reverse_x_speed(e);
        e->unknown6 ^= 0x0C;
    }
    cpu.de = 0x1108; /* floor under the middle: 17 px down, 8 px right */
    CALL_ROUTINE(f_getNearEntityTileAttrWithOffset);
    if (cpu.a & TILE_SOLID) e->ySpeed = HOPPER_JUMP_SPEED;
    e->ySpeed = (uint16_t)(e->ySpeed + HOPPER_GRAVITY);
    ANIMATE(hopper_animation(e));
}

/* $63F4: static fire hazard ($57): animated flame that kills on touch. */
LIFTED(updateEntity0x57, 0x63F4) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = FLAME_ANIMATION_DELAY;
        e->animationTimerResetValue = FLAME_ANIMATION_DELAY;
        if (is_offscreen(e)) ANIMATE(FLAME_ANIMATION);
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
    }
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    ANIMATE(FLAME_ANIMATION);
}
