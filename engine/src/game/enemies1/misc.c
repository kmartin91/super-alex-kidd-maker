/*
 * Screen objects outside the enemies range ($1B41-$1B96, $39DB, $3E28-$3F32):
 * map arrow, Janken's castle on the map, level start icon, event trigger,
 * toll, secret wall.
 */
#include "game/enemies1/enemies1.h"

#define MAP_ARROW_ANIMATION 0x8A18
#define MAP_ARROW_POSITIONS 0x1BA5   /* (x, y) per level, level 1 at +2 */
#define JANKENS_CASTLE_SPRITE 0x8073
#define LEVEL_START_ICON_ANIMATION 0x9750
#define EVENT_TRIGGER_STATE_HANDLERS 0x3E31
#define TOLL_PRICE 0x3EF9            /* 3 BCD bytes */
#define TOLL_KEEPER_SLOT ENTITY_SLOT(27)
#define TOLL_KEEPER_SLOT_2 ENTITY_SLOT(28)
#define SECRET_WALL_ENTRY 0xCC08     /* name table mirror entry replaced */
#define SECRET_WALL_METATILE 0x8503  /* background metatile put there */

/* $1B41: blinking arrow showing Alex's position on the map. On the level
 * start screen it points at the level; on the map opened from the pause menu
 * it is 24 pixels higher, and on a bonus level at a fixed place. */
LIFTED(updateArrow, 0x1B41) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->animationTimer = 8;
        e->animationTimerResetValue = 8;
        uint16_t position = (uint16_t)(MAP_ARROW_POSITIONS + (uint8_t)(ram8(v_level) * 2));
        ENT_X(e) = rd8(position);
        uint8_t y = rd8((uint16_t)(position + 1));
        if ((ram8(v_gameState) & 0x7F) != STATE_LEVEL_STARTING) {
            y = (uint8_t)(y - 0x18);
            if (ram8(v_currentLevelIsBonusLevel) != 0) {
                ENT_X(e) = 0x5E;
                ENT_Y(e) = 0x46;
                ANIMATE_AND_RETURN(MAP_ARROW_ANIMATION);
            }
        }
        ENT_Y(e) = y;
    }
    ANIMATE_AND_RETURN(MAP_ARROW_ANIMATION);
}

/* $1B8E: Janken's castle drawn on the map. */
LIFTED(updateJankensCastle, 0x1B8E) {
    entity_at(cpu.ix)->spriteDescriptorPointer = JANKENS_CASTLE_SPRITE;
    LIFTED_RETURN();
}

/* $39DB: animated icon at (216, 128) on the level start screen. */
LIFTED(updateEntity0x62, 0x39DB) {
    Entity *e = entity_at(cpu.ix);
    e->animationTimerResetValue = 0x19;
    ENT_X(e) = 0xD8;
    ENT_Y(e) = 0x80;
    ANIMATE_AND_RETURN(LEVEL_START_ICON_ANIMATION);
}

/* $3E28: invisible event trigger, a state machine whose handlers are in the
 * table at $3E31 (indexed by Entity.state):
 *   0  setup (null sprite), next state;
 *   1  when Alex stands idle on it: Alex is frozen (state $1A) and a sprite
 *      appears in slot 27, 24 pixels to the right;
 *   2  30 frames later a second sprite appears in slot 28;
 *   3  60 frames later: Alex is released, name table change $89 draws a
 *      block at $7BB4, both sprites and the trigger disappear. */
LIFTED(updateEntity0x60, 0x3E28) {
    cpu.a = entity_at(cpu.ix)->state;
    cpu.hl = EVENT_TRIGGER_STATE_HANDLERS;
    TAIL_CALL(f_jumpToAthPointer);
}

/* $3EBA: invisible toll. When Alex touches it while slot 27 is occupied (e.g.
 * by the sprites of an event trigger $60) and he has enough money, the price
 * (BCD at $3EF9) is paid and slots 27 and 28 are cleared.
 * QUIRK: slot 27 is tested twice (slot 28 was probably meant). */
LIFTED(updateEntity0x61, 0x3EBA) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (!entity_on_screen(e)) LIFTED_RETURN();
    cpu.iy = v_alex;
    call_routine(f_checkEntityCollision);
    if (cpu.f & FLAG_C) LIFTED_RETURN();
    if (entity_at(TOLL_KEEPER_SLOT)->type == 0) LIFTED_RETURN();
    cpu.hl = TOLL_PRICE;
    cpu.bc = v_money;
    call_routine(f_subtractBCDToA); /* carry: not enough money */
    if (cpu.f & FLAG_C) LIFTED_RETURN();
    cpu.hl = TOLL_PRICE;
    cpu.bc = v_money;
    call_routine(f_subtractBCD);
    cpu.hl = TOLL_KEEPER_SLOT;
    call_routine(f_clearEntity);
    cpu.hl = TOLL_KEEPER_SLOT_2;
    TAIL_CALL(f_clearEntity);
}

/* $3EFC: invisible wall section of Mt. Kave. Activated once on screen; when
 * Alex's attack touches it, a background metatile replaces the wall (the
 * passage opens) and the entity disappears. */
LIFTED(updateEntity0x63, 0x3EFC) {
    Entity *e = entity_at(cpu.ix);
    if (e->flags & EF_INITIALIZED) {
        if (!alex_attack_hits()) LIFTED_RETURN();
        if (ram8(v_nametableChangeRequest) != 0) LIFTED_RETURN();
        ram8(v_nametableChangeRequest) = NT_CHANGE_METATILE;
        ram16(nametableChangeSourceMetatile) = SECRET_WALL_METATILE;
        ram16(v_nametableChangeDestination) = SECRET_WALL_ENTRY;
        DESTROY_AND_RETURN();
    }
    e->spriteDescriptorPointer = NULL_SPRITE;
    if (ENT_SCREEN_X(e) != 0) LIFTED_RETURN();
    e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
    LIFTED_RETURN();
}
