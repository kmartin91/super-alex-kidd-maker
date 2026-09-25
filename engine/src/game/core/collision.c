/*
 * Terrain lookup helpers and entity collision tests ($7C82-$7DC1).
 *
 * Hitboxes: 4 bytes [X offset, width, Y offset, height] in the table at
 * HITBOXES (bank 2), selected by Entity.unknown2 (a byte offset, copied from
 * the entity's sprite descriptor). Alex's body box is followed by his fist's.
 *
 * A collision test returns carry CLEAR when the boxes overlap, and then sets
 * ENTITY_HIT in the flags of the entity IX (the one being updated); the
 * callers also mark the other entity IY themselves when needed.
 */
#include "game/core/core.h"

/* RAM copy of the name table (32x28 entries of 2 bytes). */
#define NAMETABLE_COPY 0xC800
#define NAMETABLE_ROWS_PIXELS 0xE0 /* 28 rows of 8 lines */

/* --------------------------------------------------------------- terrain */

/* $7C82: tile attributes at name-table pixel (X = E, Y = D): E becomes the
 * column's byte offset, then sub_7C56 adds the vertical scroll and reads the
 * attribute byte (HL = its address). Returns C = X. */
LIFTED(_LABEL_7C89_, 0x7C82) {
    uint8_t x = cpu.e;
    cpu.c = x;
    cpu.e = (uint8_t)((x >> 3) * 2);
    cpu.a = cpu.d;
    TAIL_CALL(f_sub_7C56);
}

/* $7C8D: HL points at an attribute byte in the name-table copy; move it along
 * the same row to pixel column C + E. Returns C = that column. */
LIFTED(_LABEL_7C94_, 0x7C8D) {
    uint8_t row_part = cpu.l & 0xC0; /* the row within the 256-byte page */
    uint8_t x = (uint8_t)(cpu.c + cpu.e);
    cpu.d = row_part;
    cpu.c = x;
    cpu.l = (uint8_t)((((x >> 3) * 2) | row_part) + 1);
    LIFTED_RETURN();
}

/* $7C9C: same column as the attribute pointer L, pixel row B + D (wrapping at
 * the bottom of the 28-row name table). Returns B = that row, HL = address of
 * the attribute byte in the name-table copy, DE = $C800 + column offset;
 * flags S, Z, P describe the row's pixel offset (row * 8). */
LIFTED(_LABEL_7CA3_, 0x7C9C) {
    uint8_t column = cpu.l & 0x3F;
    uint8_t y = (uint8_t)(cpu.b + cpu.d);
    cpu.b = y;
    if (y >= NAMETABLE_ROWS_PIXELS) y = (uint8_t)(y - NAMETABLE_ROWS_PIXELS);
    uint8_t row_y = y & 0xF8;
    cpu.de = (uint16_t)((NAMETABLE_COPY & 0xFF00) | column);
    cpu.hl = (uint16_t)(row_y * 8 + cpu.de);
    cpu.f = (uint8_t)((cpu.f & ~(FLAG_S | FLAG_Z | FLAG_P)) | (row_y & FLAG_S) | (row_y ? 0 : FLAG_Z) | flag_p(row_y));
    LIFTED_RETURN();
}

/* ------------------------------------------------------------- collisions */

/* Registers the original leaves: B and C hold the last box values it
 * compared, the carry flag is clear on a hit. */
typedef struct Overlap {
    uint8_t b, c;
    bool hit;
} Overlap;

static Overlap overlap_result(Overlap r) {
    cpu.b = r.b;
    cpu.c = r.c;
    core_set_carry(!r.hit);
    return r;
}

/* checkEntityCollisionSub: the second box B is given by its right edge
 * `b_right` and the address of its width byte; box A (entity `a`) by its
 * address. `r` holds B and C on entry (kept on the first early exit).
 * QUIRK: each bound is computed with 8-bit adds/subtracts where only the last
 * carry/borrow is checked, so boxes near the screen edges can wrap. */
static Overlap overlap_from_right_edge(Overlap r, uint8_t b_right, uint16_t b_width, uint16_t a_box, Entity *a,
                                       const Entity *b) {
    r.hit = false;
    /* Horizontal: 0 <= B.right - A.left <= B.width + A.width. */
    uint8_t a_left_offset = rd8(a_box);
    uint8_t a_x = (uint8_t)(a->xPos >> 8);
    uint8_t d = (uint8_t)(b_right - a_left_offset);
    if (d < a_x) return r;
    r.c = (uint8_t)(d - a_x);
    if ((uint8_t)(rd8(b_width) + rd8((uint16_t)(a_box + 1))) < r.c) return r;

    /* Vertical, the same way. */
    r.b = rd8((uint16_t)(b_width + 1));
    uint8_t b_bottom = (uint8_t)((uint8_t)(rd8((uint16_t)(b_width + 2)) + r.b) + (uint8_t)(b->yPos >> 8));
    uint8_t a_y = (uint8_t)(a->yPos >> 8);
    d = (uint8_t)(b_bottom - rd8((uint16_t)(a_box + 2)));
    if (d < a_y) return r;
    r.c = (uint8_t)(d - a_y);
    if ((uint8_t)(rd8((uint16_t)(b_width + 2)) + rd8((uint16_t)(a_box + 3))) < r.c) return r;

    a->flags |= ENTITY_HIT;
    r.hit = true;
    return r;
}

/* Right edge of box `box` placed at `x`: an 8-bit sum where only the carry of
 * the final add is detected (then clamped to 255 if `clamp`). */
static uint8_t box_right_edge(uint16_t box, uint8_t x, bool clamp) {
    uint8_t offset_plus_width = (uint8_t)(rd8(box) + rd8((uint16_t)(box + 1)));
    unsigned right = offset_plus_width + x;
    return (clamp && right > 0xFF) ? 0xFF : (uint8_t)right;
}

/* checkEntityCollision on slots `ix` (A) and `iy` (B). */
static Overlap check_collision(uint16_t ix, uint16_t iy) {
    Overlap r = {cpu.b, cpu.c, false};
    Entity *a = entity_at(ix);
    const Entity *b = entity_at(iy);
    if (b->isOffScreenFlags != 0) return overlap_result(r); /* B not on this screen */
    uint16_t b_box = (uint16_t)(HITBOXES + b->unknown2);
    uint16_t a_box = (uint16_t)(HITBOXES + a->unknown2);
    r.b = rd8(b_box);
    r.c = (uint8_t)HITBOXES; /* BC was loaded with the table address */
    uint8_t b_right = box_right_edge(b_box, (uint8_t)(b->xPos >> 8), true);
    return overlap_result(overlap_from_right_edge(r, b_right, (uint16_t)(b_box + 1), a_box, a, b));
}

/* $7CBB: do the hitboxes of entities IX and IY overlap? Carry clear if so
 * (and IX is marked ENTITY_HIT). An IY that is off the screen never collides.
 * Returns B and C as the original leaves them. */
LIFTED(checkEntityCollision, 0x7CBB) {
    check_collision(cpu.ix, cpu.iy);
    LIFTED_RETURN();
}

/* $7CB5: as checkEntityCollision, but an empty IY slot never collides. */
LIFTED(_LABEL_7CBC_, 0x7CB5) {
    if (entity_at(cpu.iy)->type == 0) {
        core_set_carry(true);
        LIFTED_RETURN();
    }
    check_collision(cpu.ix, cpu.iy);
    LIFTED_RETURN();
}

/* $7CDF: second half of checkEntityCollision. A = right edge of IY's box,
 * DE = address of its width byte, HL = IX's box. */
LIFTED(checkEntityCollisionSub_LABEL_7CE6_, 0x7CDF) {
    Overlap r = {cpu.b, cpu.c, false};
    overlap_result(overlap_from_right_edge(r, cpu.a, cpu.de, cpu.hl, entity_at(cpu.ix), entity_at(cpu.iy)));
    LIFTED_RETURN();
}

/* $7D31: does Alex's punch reach entity IX? Only while Alex is punching;
 * uses the fist box that follows his body box. Carry clear on a hit (IX gets
 * ENTITY_HIT). Returns IY = Alex when the test is made. QUIRK: unlike
 * checkEntityCollision the fist's right edge is not clamped at 255. */
LIFTED(checkAlexPunchHit, 0x7D31) {
    const Entity *alex = entity_at(v_alex);
    if (!(alex->unknown8 & ALEX_PUNCHING)) {
        core_set_carry(true);
        LIFTED_RETURN();
    }
    cpu.iy = v_alex;
    uint16_t fist_box = (uint16_t)(HITBOXES + (uint8_t)(alex->unknown2 + 4));
    uint16_t target_box = (uint16_t)(HITBOXES + entity_at(cpu.ix)->unknown2);
    Overlap r = {rd8(fist_box), (uint8_t)HITBOXES, false};
    uint8_t fist_right = box_right_edge(fist_box, (uint8_t)(alex->xPos >> 8), false);
    overlap_result(overlap_from_right_edge(r, fist_right, (uint16_t)(fist_box + 1), target_box, entity_at(cpu.ix), alex));
    LIFTED_RETURN();
}

/* $7D04: is entity IX hit by one of Alex's attacks? Depends on the item or
 * vehicle in use (v_alexActionState, ALEX_HIT_CHECKERS): his punch (also
 * always when swimming), what the magic capsules or the power bracelet
 * release, the boat's or peticopter's shot, or the motorcycle itself. Carry
 * clear on a hit. */
LIFTED(isAlexAttackingEntity, 0x7D04) {
    if (entity_at(v_alex)->state == ALEX_SWIMMING) TAIL_CALL(f_checkAlexPunchHit);
    cpu.a = ram8(v_alexActionState);
    cpu.hl = ALEX_HIT_CHECKERS;
    TAIL_CALL(f_jumpToAthPointer);
}

/* $7D5A (boat, peticopter): their shot in slot 2 (if any) against IX; on a
 * hit the shot is marked ENTITY_HIT too. */
LIFTED(_LABEL_7D61_, 0x7D5A) {
    cpu.iy = ENTITY_SLOT(2);
    if (entity_at(cpu.iy)->type == 0) {
        core_set_carry(true);
        LIFTED_RETURN();
    }
    if (check_collision(cpu.ix, cpu.iy).hit) entity_at(cpu.iy)->flags |= ENTITY_HIT;
    LIFTED_RETURN();
}

/* $7D67 (magic capsule A): slots 3 then 2 against IX; the one that hits is
 * marked ENTITY_HIT too. */
LIFTED(_LABEL_7D6E_, 0x7D67) {
    cpu.iy = ENTITY_SLOT(3);
    if (!check_collision(cpu.ix, cpu.iy).hit) {
        cpu.iy = ENTITY_SLOT(2);
        if (!check_collision(cpu.ix, cpu.iy).hit) LIFTED_RETURN();
    }
    entity_at(cpu.iy)->flags |= ENTITY_HIT;
    LIFTED_RETURN();
}

/* $7D7D (magic capsule B): slot 4 against IX. */
LIFTED(_LABEL_7D84_, 0x7D7D) {
    cpu.iy = ENTITY_SLOT(4);
    check_collision(cpu.ix, cpu.iy);
    LIFTED_RETURN();
}

/* $7D84 (motorcycle): Alex himself against IX. */
LIFTED(_LABEL_7D8B_, 0x7D84) {
    cpu.iy = v_alex;
    check_collision(cpu.ix, cpu.iy);
    LIFTED_RETURN();
}

/* $7D8B (power bracelet): its shock wave in slot 2 against IX. */
LIFTED(_LABEL_7D92_, 0x7D8B) {
    cpu.iy = ENTITY_SLOT(2);
    check_collision(cpu.ix, cpu.iy);
    LIFTED_RETURN();
}

/* $7D92: entity IX touching Alex kills him, unless he is already dead or the
 * item in use protects him (ALEX_DAMAGE_HANDLERS: teleport powder, magic
 * capsule B, motorcycle). */
LIFTED(tryToKillAlexIfColliding, 0x7D92) {
    if (entity_at(v_alex)->state >= ALEX_DEAD) LIFTED_RETURN();
    cpu.a = ram8(v_alexActionState);
    cpu.hl = ALEX_DAMAGE_HANDLERS;
    TAIL_CALL(f_jumpToAthPointer);
}

/* $7DB5: Alex against IX; on contact both are marked ENTITY_HIT (for Alex:
 * he dies on his next update). */
LIFTED(killAlexIfColliding, 0x7DB5) {
    cpu.iy = v_alex;
    if (check_collision(cpu.ix, cpu.iy).hit) entity_at(v_alex)->flags |= ENTITY_HIT;
    LIFTED_RETURN();
}

/* $7DC1: the protected action states: nothing happens. */
LIFTED(doNotKillAlex, 0x7DC1) {
    LIFTED_RETURN();
}
