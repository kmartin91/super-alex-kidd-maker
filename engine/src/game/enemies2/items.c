/*
 * Block debris and pick-up items ($5901-$5C2E): the four pieces of a broken
 * block, money bags, extra life, power bracelet, the ghost released by a
 * skull box, and the rice ball that ends a level.
 */
#include "game/enemies2/enemies2.h"

/* Debris pieces fly out at 0.5 px/frame and fall with this gravity. */
#define DEBRIS_GRAVITY 0x0030
#define DEBRIS_SPEED_LEFT 0xFF80
#define DEBRIS_SPEED_RIGHT 0x0080
#define DEBRIS_SPEED_UP 0xFF80
#define DEBRIS_ANIMATION_DELAY 8
/* Animation descriptors of the debris, one per block kind (Entity.unknown6). */
#define DEBRIS_ANIMATIONS 0x5D85

#define ITEM_LIFESPAN 0xF0        /* frames before an uncollected item vanishes */
#define BIG_MONEY_BAG_SPRITE 0x8359
#define SMALL_MONEY_BAG_SPRITE 0x8367
#define BIG_MONEY_BAG_VALUE 3     /* takeMoney index */
#define SMALL_MONEY_BAG_VALUE 0
#define FIRST_SMALL_MONEY_BAG_KIND 4
#define LIFE_SPRITE 0x8C0E
#define POWER_BRACELET_SPRITE 0x8C1C
#define RICE_BALL_SPRITE 0x8CC7

#define GHOST_IDLE_TIME 0x80
#define GHOST_ANIMATION_DELAY 0x18
#define GHOST_LEFT_ANIMATION 0x8C2A
#define GHOST_RIGHT_ANIMATION 0x8C6C

static void start_debris(Entity *e, uint16_t x_speed) {
    e->xSpeed = x_speed;
    e->ySpeed = DEBRIS_SPEED_UP;
    e->animationTimer = DEBRIS_ANIMATION_DELAY;
    e->animationTimerResetValue = DEBRIS_ANIMATION_DELAY;
}

/* $5901: top-left piece of a broken block (entity $38, spawned in slot 23 at
 * the block position with the block kind in unknown6). On its first frame it
 * also spawns the three other pieces in slots 24-26; they position themselves
 * relative to this one. Leaves IY at slot 26. */
LIFTED(updateDebrisTopLeft, 0x5901) {
    Entity *e = entity_at(cpu.ix);
    if (e->flags & EF_INITIALIZED) TAIL_CALL(f_updateDebris);

    e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
    start_debris(e, DEBRIS_SPEED_LEFT);
    uint8_t kind = e->unknown6;
    for (int i = 0; i < 3; i++) {
        Entity *piece = entity_at(SLOT_DEBRIS_2 + i * 0x20);
        piece->type = (uint8_t)(ENTITY_DEBRIS_BOTTOM_LEFT + i);
        piece->unknown6 = kind;
        piece->flags = (uint8_t)((piece->flags & ~EF_INITIALIZED) | EF_DESTROY_OFFSCREEN);
    }
    cpu.iy = SLOT_DEBRIS_2 + 2 * 0x20;
    TAIL_CALL(f_sub_5985);
}

/* $5964: common debris movement: destroyed at the right edge or once off
 * screen, otherwise falls with gravity and animates. */
LIFTED(updateDebris, 0x5964) {
    Entity *e = entity_at(cpu.ix);
    if (x_pixel(e) >= 0xF8 || is_offscreen(e)) TAIL_CALL(f_destroyCurrentEntity);
    e->ySpeed = (uint16_t)(e->ySpeed + DEBRIS_GRAVITY);
    TAIL_CALL(f_sub_5985);
}

/* $5985: animate a debris piece with the animation of its block kind. */
LIFTED(sub_5985, 0x5985) {
    Entity *e = entity_at(cpu.ix);
    ANIMATE(rom_word_table(DEBRIS_ANIMATIONS, e->unknown6));
}

/* One-time setup of the three other pieces: place them at an offset from the
 * top-left piece (slot 23) and throw them left or right. */
static void init_debris_piece(Entity *e, uint8_t dx, uint8_t dy, uint16_t x_speed) {
    const Entity *top_left = entity_at(SLOT_BLOCK_DEBRIS);
    e->flags |= EF_INITIALIZED;
    set_x_pixel(e, (uint8_t)(x_pixel(top_left) + dx));
    set_y_pixel(e, (uint8_t)(y_pixel(top_left) + dy));
    start_debris(e, x_speed);
}

/* $598F: bottom-left debris piece. */
LIFTED(updateDebrisBottomLeft, 0x598F) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) init_debris_piece(e, 0, 8, DEBRIS_SPEED_LEFT);
    TAIL_CALL(f_updateDebris);
}

/* $59C1: top-right debris piece. */
LIFTED(updateDebrisTopRight, 0x59C1) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) init_debris_piece(e, 8, 0, DEBRIS_SPEED_RIGHT);
    TAIL_CALL(f_updateDebris);
}

/* $59F4: bottom-right debris piece. */
LIFTED(updateDebrisBottomRight, 0x59F4) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) init_debris_piece(e, 8, 8, DEBRIS_SPEED_RIGHT);
    TAIL_CALL(f_updateDebris);
}

/* Pick-up items: the lifespan countdown lives in Entity.battleDecision. */
#define ITEM_TOUCH_CHECK(e)                                                   \
    do {                                                                      \
        if (is_offscreen(e)) TAIL_CALL(f_destroyCurrentEntity);               \
        cpu.iy = SLOT_ALEX;                                                   \
        CALL_ROUTINE(f_checkEntityCollision);                                 \
        if (carry_set()) { /* not touching Alex: age */                       \
            if (--(e)->battleDecision == 0) TAIL_CALL(f_destroyCurrentEntity); \
            LIFTED_RETURN();                                                  \
        }                                                                     \
    } while (0)

/* $5A2A: money bag left by a star block. unknown6 < 4: big bag (takeMoney
 * index 3), otherwise small bag (index 0). */
LIFTED(updateMoneyBag, 0x5A2A) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->xSpeed = 0;
        e->ySpeed = 0;
        e->battleDecision = ITEM_LIFESPAN;
        if (e->unknown6 < FIRST_SMALL_MONEY_BAG_KIND) {
            e->spriteDescriptorPointer = BIG_MONEY_BAG_SPRITE;
            e->unknown5 = BIG_MONEY_BAG_VALUE;
        } else {
            e->spriteDescriptorPointer = SMALL_MONEY_BAG_SPRITE;
            e->unknown5 = SMALL_MONEY_BAG_VALUE;
        }
        LIFTED_RETURN();
    }
    ITEM_TOUCH_CHECK(e);
    /* Collected. */
    cpu.l = e->unknown5;
    CALL_ROUTINE(f_takeMoney);
    ram8(v_soundControl) = SOUND_COINS;
    TAIL_CALL(f_destroyCurrentEntity);
}

/* $5A8F: extra life (from a question-mark box): +1 life (BCD). */
LIFTED(updateLife, 0x5A8F) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->battleDecision = ITEM_LIFESPAN;
        e->spriteDescriptorPointer = LIFE_SPRITE;
        e->xSpeed = 0;
        e->ySpeed = 0;
        LIFTED_RETURN();
    }
    ITEM_TOUCH_CHECK(e);
    /* Collected. */
    ram8(v_soundControl) = SOUND_POWERUP;
    ram8(v_lives) = bcd_increment(ram8(v_lives));
    TAIL_CALL(f_destroyCurrentEntity);
}

/* $5ADF: power bracelet (from a question-mark box): Alex's punch shoots a
 * shock wave; counts the bracelets picked up. */
LIFTED(updatePowerBracelet, 0x5ADF) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->battleDecision = ITEM_LIFESPAN;
        e->spriteDescriptorPointer = POWER_BRACELET_SPRITE;
        e->xSpeed = 0;
        e->ySpeed = 0;
        LIFTED_RETURN();
    }
    ITEM_TOUCH_CHECK(e);
    /* Collected. */
    ram8(v_soundControl) = SOUND_POWERUP;
    ram8(v_hasPowerBracelet) = 1;
    ram8(v_powerBraceletsPickedUpCounter)++;
    TAIL_CALL(f_destroyCurrentEntity);
}

/* $5B30: ghost released by a skull box (or by King High Stone when Alex has
 * no letter). Floats in place for GHOST_IDLE_TIME frames (countdown in
 * unknown6), then chases Alex forever, facing him. */
LIFTED(updateGhost, 0x5B30) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->unknown6 = GHOST_IDLE_TIME;
        e->animationTimer = GHOST_ANIMATION_DELAY;
        e->animationTimerResetValue = GHOST_ANIMATION_DELAY;
    }
    if (is_offscreen(e)) TAIL_CALL(f_destroyCurrentEntity);

    /* Bit 1 doubles as "chasing" (and destroy-when-off-screen). */
    if (!(e->flags & EF_DESTROY_OFFSCREEN)) {
        if (--e->unknown6 != 0) ANIMATE(GHOST_LEFT_ANIMATION);
        e->flags |= EF_DESTROY_OFFSCREEN;
    }
    if (entity_at(SLOT_ALEX)->state == ALEX_DEAD) LIFTED_RETURN();

    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_getVelocitiesToPursuitAlex);
    e->xSpeed = cpu.hl;
    e->ySpeed = cpu.de;
    bool alex_on_right = x_pixel(entity_at(SLOT_ALEX)) >= x_pixel(e);
    ANIMATE(alex_on_right ? GHOST_RIGHT_ANIMATION : GHOST_LEFT_ANIMATION);
}

/* $5BCA: rice ball at the end of a level: touching it scores 1000 points and
 * completes the level. */
LIFTED(updateRiceBall, 0x5BCA) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->spriteDescriptorPointer = RICE_BALL_SPRITE;
        LIFTED_RETURN();
    }
    if (is_offscreen(e)) LIFTED_RETURN();
    cpu.iy = SLOT_ALEX;
    CALL_ROUTINE(f_checkEntityCollision);
    if (carry_set()) LIFTED_RETURN();

    cpu.l = SCORE_1000;
    CALL_ROUTINE(f_addScore);
    CALL_ROUTINE(f_destroyCurrentEntity);
    ram8(v_gameState) = STATE_LEVEL_COMPLETED;
    LIFTED_RETURN();
}

/* $5C20 (unused, no caller in the ROM): put a rice ball in slot 27 at
 * pixel position (E, D). */
LIFTED(unused_LABEL_5C27_, 0x5C20) {
    Entity *rice_ball = entity_at(SLOT_THOUGHT_OPPONENT);
    rice_ball->type = ENTITY_RICE_BALL;
    set_y_pixel(rice_ball, cpu.d);
    set_x_pixel(rice_ball, cpu.e);
    cpu.iy = SLOT_THOUGHT_OPPONENT;
    LIFTED_RETURN();
}
