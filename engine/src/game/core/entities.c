/*
 * Entity framework: the per-frame update of every entity slot, movement with
 * wrapping to the neighbouring screens, the RAM sprite table, animation,
 * clearing and spawning ($09D9, $2694-$2891, $5B89).
 *
 * An entity is a 32-byte slot (Entity in game/ram.h). Positions are 8.8 fixed
 * point relative to the screen; Entity.isOffScreenFlags holds the entity's
 * screen offset from the visible one: low byte horizontally, high byte
 * vertically (0/0 = on screen, $FF above, +1 below).
 */
#include "game/core/core.h"

/* Byte offsets of the two halves of Entity.isOffScreenFlags. */
#define ENTITY_SCREEN_X 0x09
#define ENTITY_SCREEN_Y 0x0A
#define ENTITY_ANIMATION_TIMER 0x05

/* The visible screen is 192 lines: an entity leaving it vertically continues
 * on the neighbouring screen, its Y taken modulo $C0. */
#define SCREEN_HEIGHT 0xC0
/* An entity on the screen above is drawn when its Y there is at least this
 * (its sprites may reach down into the visible screen). */
#define ABOVE_SCREEN_VISIBLE_Y 0xA8

/* First RAM sprite slot used by entities (the first six are the states'). */
#define FIRST_ENTITY_SPRITE 0xC706
#define LAST_SPRITE_INDEX 0x3F

/* Item drops (money bags, lives...) always go to slot 27 or 28. */
#define ITEM_SLOT_A ENTITY_SLOT(27)
#define ITEM_SLOT_B ENTITY_SLOT(28)

/* Clears a slot: every byte 0 except the animation timer, 1 (so the first
 * animation step happens on the next update). Only the low byte of the
 * address advances (slots never cross a page). */
void core_clear_entity(uint16_t slot) {
    for (uint8_t i = 0; i < ENTITY_SIZE; i++) wr8(core_inc_low(slot, i), 0);
    wr8(core_inc_low(slot, ENTITY_ANIMATION_TIMER), 1);
}

/* destroyCurrentEntity on slot IX, with its register results. */
static void destroy_entity(uint16_t slot) {
    core_clear_entity(slot);
    cpu.a = 0;
    cpu.c = 0;
    cpu.hl = core_inc_low(slot, ENTITY_SIZE - 1);
}

/* $278D: clear the entity slot at HL. Returns A = C = 0, L advanced to the
 * last byte of the slot. */
LIFTED(clearEntity, 0x278D) {
    core_clear_entity(cpu.hl);
    cpu.a = 0;
    cpu.c = 0;
    cpu.l = (uint8_t)(cpu.l + ENTITY_SIZE - 1);
    LIFTED_RETURN();
}

/* $278A: clear the entity IX (the one being updated). */
LIFTED(destroyCurrentEntity, 0x278A) {
    destroy_entity(cpu.ix);
    LIFTED_RETURN();
}

/* $09D9: clear the 30 entity slots. */
LIFTED(clearEntities, 0x09D9) {
    for (int i = 0; i < ENTITY_SLOTS; i++) core_clear_entity(ENTITY_SLOT(1 + i));
    cpu.hl = ENTITY_SLOT(1 + ENTITY_SLOTS);
    cpu.b = 0;
    cpu.a = 0;
    cpu.c = 0;
    LIFTED_RETURN();
}

/* Moves the entity by its X speed plus the scrolling of the frame. Crossing
 * the left or right edge of the 256-pixel strip moves it to the neighbouring
 * screen, or destroys it (ENTITY_DIES_OFFSCREEN). Returns true if destroyed. */
static bool move_entity_horizontally(uint16_t slot) {
    Entity *e = entity_at(slot);
    uint16_t delta = (uint16_t)(ram16(v_horizontalScrollSpeed) + e->xSpeed);
    if (delta == 0) return false;
    uint32_t sum = (uint32_t)e->xPos + delta;
    bool moving_left = delta & 0x8000;
    bool wrapped = moving_left ? sum <= 0xFFFF : sum > 0xFFFF;
    if (wrapped) {
        if (e->flags & ENTITY_DIES_OFFSCREEN) {
            destroy_entity(slot);
            return true;
        }
        /* Left edge: +1, right edge: -1. */
        ram8(slot + ENTITY_SCREEN_X) += moving_left ? 1 : -1;
    }
    e->xPos = (uint16_t)sum;
    return false;
}

/* Moves the entity by its Y speed minus the scrolling of the frame; leaving
 * the 192-line screen moves it to the screen above/below, or destroys it
 * (ENTITY_DIES_OFFSCREEN). */
static void move_entity_vertically(uint16_t slot) {
    Entity *e = entity_at(slot);
    uint16_t delta = (uint16_t)(e->ySpeed - ram16(v_verticalScrollSpeed));
    if (delta == 0) return;
    uint32_t sum = (uint32_t)e->yPos + delta;
    uint16_t y = (uint16_t)sum;
    if (delta & 0x8000) {
        if (sum > 0xFFFF) { /* still at Y >= 0 */
            e->yPos = y;
            return;
        }
        if (e->flags & ENTITY_DIES_OFFSCREEN) {
            destroy_entity(slot);
            return;
        }
        e->yPos = (uint16_t)(y + (SCREEN_HEIGHT << 8)); /* onto the screen above */
        ram8(slot + ENTITY_SCREEN_Y)--;
        return;
    }
    if ((y >> 8) < SCREEN_HEIGHT) {
        e->yPos = y;
        return;
    }
    if (e->flags & ENTITY_DIES_OFFSCREEN) {
        destroy_entity(slot);
        return;
    }
    e->yPos = (uint16_t)(y - (SCREEN_HEIGHT << 8)); /* onto the screen below */
    ram8(slot + ENTITY_SCREEN_Y)++;
}

/* $27D0: horizontal move of entity IX (A is returned unchanged, or 0 if the
 * entity was destroyed). */
LIFTED(_LABEL_27D0_, 0x27D0) {
    move_entity_horizontally(cpu.ix);
    LIFTED_RETURN();
}

/* $273A: vertical move of entity IX. */
LIFTED(_LABEL_273A_, 0x273A) {
    move_entity_vertically(cpu.ix);
    LIFTED_RETURN();
}

/* Appends the entity's sprites to the RAM sprite table at
 * v_spriteTerminatorPointer, from its sprite descriptor:
 *   [count, hitbox offset, count Y offsets, count (X offset, tile) pairs]
 * The hitbox offset is copied to Entity.unknown2. A sprite pushed past the
 * left/right edge by its offset is hidden (Y = $E0) instead. Entities off the
 * screen are skipped, except those just above it (low enough to show). */
static void add_entity_sprites(uint16_t slot) {
    Entity *e = entity_at(slot);
    if (e->type == 0) return;
    bool from_screen_above = e->isOffScreenFlags != 0;
    uint8_t y = (uint8_t)(e->yPos >> 8);
    if (from_screen_above) {
        if (e->isOffScreenFlags != 0xFF00 || y < ABOVE_SCREEN_VISIBLE_Y) return;
        y = (uint8_t)(y + (0x100 - SCREEN_HEIGHT));
    } else if (y >= SCREEN_HEIGHT) {
        return;
    }

    uint16_t descriptor = e->spriteDescriptorPointer;
    uint8_t count = rd8(descriptor);
    e->unknown2 = rd8((uint16_t)(descriptor + 1));
    uint16_t data = (uint16_t)(descriptor + 2);

    /* Y bytes (the Y slot index only advances in its page). */
    uint16_t first = ram16(v_spriteTerminatorPointer), out = first;
    uint8_t n = count;
    do {
        uint8_t sprite_y = (uint8_t)(y + rd8(data++));
        /* $D0 would end the sprite list: use the line above. */
        if (!from_screen_above && sprite_y == SPRITE_LIST_END) sprite_y--;
        wr8(out, sprite_y);
        out = core_inc_low(out, 1);
    } while (--n);
    ram16(v_spriteTerminatorPointer) = out;

    /* (X, tile) pairs at $80 + 2 * Y slot. Like the original, the tile copy
     * advances the full address (LDI), the rest only its low byte. */
    uint16_t xt = (uint16_t)((first & 0xFF00) | (uint8_t)((first << 1) | 0x80));
    uint8_t x = (uint8_t)(e->xPos >> 8);
    n = count;
    do {
        int8_t offset = (int8_t)rd8(data++);
        int sprite_x = x + offset;
        if (sprite_x < 0 || sprite_x > 0xFF) {
            /* Off the side: hide the sprite through its Y byte. */
            wr8((uint16_t)((xt & 0xFF00) | ((xt & 0x7F) >> 1)), SPRITE_HIDDEN_Y);
            xt = (uint16_t)((xt & 0xFF00) | (xt & 0x7E) | 0x80);
        } else {
            wr8(xt, (uint8_t)sprite_x);
        }
        xt = core_inc_low(xt, 1);
        wr8(xt++, rd8(data++));
    } while (--n);
}

/* $26D7: add entity IX's sprites to the sprite table. */
LIFTED(updateEntitySprites, 0x26D7) {
    add_entity_sprites(cpu.ix);
    LIFTED_RETURN();
}

/* $2694: update every entity slot of the current array (v_entitydataArrayPointer,
 * v_entitydataArrayLength entries): run its type's updater, then move it with
 * the scrolling and add its sprites. Rebuilds the RAM sprite table. */
LIFTED(updateEntities, 0x2694) {
    const uint8_t caller_c = cpu.c;
    ram16(v_spriteTerminatorPointer) = FIRST_ENTITY_SPRITE;
    cpu.ix = ram16(v_entitydataArrayPointer);
    uint8_t remaining = ram8(v_entitydataArrayLength);
    do {
        uint8_t type = entity_at(cpu.ix)->type & 0x7F;
        if (type != 0) {
            /* The updater (entry `type` of ENTITY_UPDATERS, through rst $20)
             * sees B = slots left and the caller's C, as in the original. It
             * may move IX: the rest of the loop follows it. */
            cpu.b = remaining;
            cpu.c = caller_c;
            cpu.a = type;
            cpu.hl = ENTITY_UPDATERS;
            CALL_ROUTINE(f_jumpToAthPointer);
            if (entity_at(cpu.ix)->type != 0) {
                move_entity_horizontally(cpu.ix);
                move_entity_vertically(cpu.ix);
                add_entity_sprites(cpu.ix);
            }
        }
        cpu.ix += ENTITY_SIZE;
    } while (--remaining);

    /* Keep at most 64 sprites, then end the list. */
    uint16_t end = ram16(v_spriteTerminatorPointer);
    if ((end & 0xFF) > LAST_SPRITE_INDEX) {
        end = (uint16_t)((end & 0xFF00) | LAST_SPRITE_INDEX);
        ram16(v_spriteTerminatorPointer) = end;
    }
    wr8(end, SPRITE_LIST_END);
    cpu.b = 0;
    cpu.c = caller_c;
    cpu.de = ENTITY_SIZE;
    LIFTED_RETURN();
}

/* $280E: advance the animation of entity IX. HL = animation descriptor
 * [frame count, sprite descriptor word per frame]. When the animation timer
 * runs out it is reloaded and the next frame (looping) is selected; the
 * frame's sprite descriptor is stored in the entity. Returns DE = the
 * descriptor (D = 0: only E and H hold it), H = its high byte. */
LIFTED(handleEntityAnimation, 0x280E) {
    Entity *e = entity_at(cpu.ix);
    uint8_t frames = rd8(cpu.hl);
    uint8_t frame = e->animationFrame;
    if (--e->animationTimer == 0) {
        e->animationTimer = e->animationTimerResetValue;
        frame++;
        if (frame >= frames) frame = 0;
    }
    e->animationFrame = frame;
    uint16_t entry = (uint16_t)(cpu.hl + 1 + (uint8_t)(frame * 2));
    uint16_t descriptor = rd16(entry);
    e->spriteDescriptorPointer = descriptor;
    cpu.a = (uint8_t)(frame * 2);
    cpu.d = 0;
    cpu.e = (uint8_t)descriptor;
    cpu.h = (uint8_t)(descriptor >> 8);
    cpu.l = (uint8_t)(entry + 1);
    LIFTED_RETURN();
}

/* $5B89: spawn an entity of type C at pixel (X = E, Y = D) in one of the two
 * item slots: the free one, else the one with the lower value at +$17 (the
 * items' remaining lifetime). The entity starts uninitialised, with a random
 * 0-7 in unknown6 and the scroll fractions as position fractions. Returns
 * IY = the slot. */
LIFTED(spawnEntityAt, 0x5B89) {
    uint16_t slot = ITEM_SLOT_A;
    if (entity_at(ITEM_SLOT_A)->type != 0) {
        slot = ITEM_SLOT_B;
        if (entity_at(ITEM_SLOT_B)->type != 0 &&
            entity_at(ITEM_SLOT_A)->battleDecision < entity_at(ITEM_SLOT_B)->battleDecision)
            slot = ITEM_SLOT_A;
    }
    Entity *e = entity_at(slot);
    e->unknown6 = rt_read_r() & 0x07; /* LD A,R */
    e->type = cpu.c;
    e->flags &= ~ENTITY_INITIALISED;
    e->xPos = (uint16_t)((cpu.e << 8) | ram8(v_horizontalScroll));
    e->yPos = (uint16_t)((cpu.d << 8) | ram8(v_verticalScroll));
    cpu.iy = slot;
    cpu.a = ram8(v_verticalScroll);
    LIFTED_RETURN();
}
