/*
 * Circular motion ("unknownAnimate", $04CE-$074B) and the octopus arm ($4C27).
 *
 * The octopus of Lake Fathom has arms made of a chain of segments. Each segment
 * is placed on a circle of radius 8 around a centre: the root segment orbits a
 * fixed point of the level, every following segment orbits the segment updated
 * just before it. The angle swings back and forth, which makes the arm wave.
 *
 * Fields of an orbiting entity:
 *   stateTimer + bit 0 of unknown8   angle, 9 bits (512 steps per turn,
 *                                    0 = right, 128 = down, 256 = left)
 *   unknown9                         radius in pixels
 *   unknown11 / state                centre X pixel / its horizontal screen offset
 *   unknown10 / unknown7             centre Y pixel / its vertical screen offset
 */
#include "game/enemies1/enemies1.h"

/* 65 (sin, cos) byte pairs scaled to 255, for angles 0..45 degrees in 64
 * steps: one octant. The other octants are obtained by symmetry. */
#define ORBIT_TABLE 0x06CA
#define ORBIT_TABLE_SHIFTED (ORBIT_TABLE + 2) /* starts at the second pair */

/* How one octant is derived from the table: which table start is used, whether
 * the table is read backwards, which axis gets the first byte of the pair and
 * the direction of each axis. */
typedef struct OctantRule {
    uint16_t table;
    bool mirrored;
    bool first_is_x;
    int8_t x_dir, y_dir;
} OctantRule;

static const OctantRule octant_rules[8] = {
    /* 0 ($04FE) */ {ORBIT_TABLE, false, false, +1, +1},
    /* 1 ($0535) */ {ORBIT_TABLE_SHIFTED, true, true, +1, +1},
    /* 2 ($0571) */ {ORBIT_TABLE_SHIFTED, false, true, -1, +1},
    /* 3 ($05A8) */ {ORBIT_TABLE, true, false, -1, +1},
    /* 4 ($05E4) */ {ORBIT_TABLE, false, false, -1, -1},
    /* 5 ($061B) */ {ORBIT_TABLE_SHIFTED, true, true, -1, -1},
    /* 6 ($0657) */ {ORBIT_TABLE_SHIFTED, false, true, +1, -1},
    /* 7 ($068E) */ {ORBIT_TABLE, true, false, +1, -1},
};

/* X = centre X +/- offset. The horizontal screen offset counts screens to the
 * left, so crossing the right edge decrements it. */
static void place_x(Entity *e, int8_t dir, uint8_t offset) {
    uint8_t cx = e->unknown11;
    if (dir > 0) {
        ENT_X(e) = (uint8_t)(cx + offset);
        ENT_SCREEN_X(e) = (uint8_t)(e->state - (cx + offset > 0xFF));
    } else {
        ENT_X(e) = (uint8_t)(cx - offset);
        ENT_SCREEN_X(e) = (uint8_t)(e->state + (cx < offset));
    }
}

/* Y = centre Y +/- offset, carrying into the vertical screen offset. */
static void place_y(Entity *e, int8_t dir, uint8_t offset) {
    uint8_t cy = e->unknown10;
    if (dir > 0) {
        ENT_Y(e) = (uint8_t)(cy + offset);
        ENT_SCREEN_Y(e) = (uint8_t)(e->unknown7 + (cy + offset > 0xFF));
    } else {
        ENT_Y(e) = (uint8_t)(cy - offset);
        ENT_SCREEN_Y(e) = (uint8_t)(e->unknown7 - (cy < offset));
    }
}

/* Places the entity on its circle. `pair_offset` = 2 * (angle & 63) is the
 * byte offset of the (sin, cos) pair inside the octant. */
static void orbit_place(Entity *e, uint8_t octant, uint8_t pair_offset) {
    const OctantRule *rule = &octant_rules[octant];
    if (rule->mirrored) pair_offset = (uint8_t)(~pair_offset + 0x7F); /* $7E - offset */
    uint16_t pair = (uint16_t)(rule->table + pair_offset);
    uint8_t first = rd8(pair), second = rd8((uint16_t)(pair + 1));
    uint8_t radius = e->unknown9;
    uint8_t first_offset = (uint8_t)((radius * first) >> 8);
    uint8_t second_offset = (uint8_t)((radius * second) >> 8);

    if (rule->first_is_x) {
        place_x(e, rule->x_dir, first_offset);
        place_y(e, rule->y_dir, second_offset);
    } else {
        place_y(e, rule->y_dir, first_offset);
        place_x(e, rule->x_dir, second_offset);
    }

    /* The original computes the first product in the alternate register set
     * (EXX) and leaves it there: B' = 0, C' kept, DE' = table byte, HL' = product. */
    cpu.bc_ = (uint16_t)(cpu.bc_ & 0x00FF);
    cpu.de_ = first;
    cpu.hl_ = (uint16_t)(radius * first);
}

/* $04CE: places the entity in IX on its circle from its current angle. */
LIFTED(unknownAnimate, 0x04CE) {
    Entity *e = entity_at(cpu.ix);
    e->unknown8 &= 0x01; /* the angle is 9 bits */
    uint16_t angle = ENT_WORD(e->stateTimer, e->unknown8);
    orbit_place(e, (uint8_t)(angle >> 6), (uint8_t)((angle & 63) * 2));
    LIFTED_RETURN();
}

/* $04FE-$068E: one routine per octant (0 to 7), entered from unknownAnimate
 * with C = 2 * (angle & 63). */
LIFTED(unknownAnimateState1Updater, 0x04FE) { orbit_place(entity_at(cpu.ix), 0, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState2Updater, 0x0535) { orbit_place(entity_at(cpu.ix), 1, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState3Updater, 0x0571) { orbit_place(entity_at(cpu.ix), 2, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState4Updater, 0x05A8) { orbit_place(entity_at(cpu.ix), 3, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState5Updater, 0x05E4) { orbit_place(entity_at(cpu.ix), 4, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState6Updater, 0x061B) { orbit_place(entity_at(cpu.ix), 5, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState7Updater, 0x0657) { orbit_place(entity_at(cpu.ix), 6, cpu.c); LIFTED_RETURN(); }
LIFTED(unknownAnimateState8Updater, 0x068E) { orbit_place(entity_at(cpu.ix), 7, cpu.c); LIFTED_RETURN(); }

/* $074C: HL = L * E (8 x 8 -> 16 bits). Leaves B = D = 0. */
LIFTED(multiply, 0x074C) {
    cpu.hl = (uint16_t)(cpu.l * cpu.e);
    cpu.b = 0;
    cpu.d = 0;
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ octopus arm */

/* Position of the previously updated arm segment, the centre of the next one. */
#define ARM_PREVIOUS_Y _RAM_C0F4_        /* low byte: Y pixel, high byte: X pixel */
#define ARM_PREVIOUS_SCREEN_X _RAM_C0FB_
#define ARM_PREVIOUS_SCREEN_Y _RAM_C0FF_

#define ARM_SEGMENT_SPRITE 0x84E3
#define ARM_RADIUS 8
#define ARM_START_ANGLE 0xC0     /* 135 degrees: down-left */
#define ARM_SWING_SPEED 2        /* angle steps per frame */
#define ARM_SWING_FRAMES 0x60    /* frames before the swing reverses */
#define OCTOPUS_HIT_POINTS 3
#define ARM_SEGMENTS_MAX 8       /* slots scanned when the octopus is defeated */

/* Octopus arm fields besides the orbit ones:
 *   data            hits taken
 *   unknown4        0 for the root segment, else a chained segment
 *   unknown5        frames to wait before swinging
 *   unknown6        swing direction: 0 = increasing angle, $FF = decreasing
 *   battleDecision  frames since the swing direction changed
 *   animationTimerResetValue / unknown1: fraction bytes of the root centre X / Y
 *   (the root centre follows the scrolling). */
static void swing_arm(Entity *e);

/* $4C27: octopus arm segment. Hurts Alex on contact. Hitting the root segment
 * three times defeats the octopus: every arm segment turns into a smoke puff
 * and the name table change that removes the octopus is requested. A chained
 * segment hit three times just dies. */
LIFTED(updateOctopusArm, 0x4C27) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->data = 0;
        e->stateTimer = ARM_START_ANGLE;
        e->unknown8 = 0;
        e->unknown9 = ARM_RADIUS;
        e->xSpeed = 0;
        e->ySpeed = 0;
        e->spriteDescriptorPointer = ARM_SEGMENT_SPRITE;
        e->state = ENT_SCREEN_X(e);
        e->unknown7 = ENT_SCREEN_Y(e);
    }

    if (entity_on_screen(e)) {
        hurt_alex_on_contact();
        if (alex_attack_hits() && (alex()->unknown8 & ALEX_ATTACK_UNSPENT)) {
            alex()->unknown8 &= (uint8_t)~ALEX_ATTACK_UNSPENT; /* one damage per attack */
            e->data++;
            if (e->data >= OCTOPUS_HIT_POINTS) {
                if (e->unknown4 != 0) TAIL_CALL(f_killEnemy); /* chained segment */

                /* Root segment: the octopus is defeated. The arm segments
                 * follow the root in the next slots. */
                uint16_t slot = cpu.ix;
                for (int i = 0; i < ARM_SEGMENTS_MAX; i++, slot += ENTITY_SIZE) {
                    Entity *segment = entity_at(slot);
                    if (segment->type == ENTITY_OCTOPUS_ARM) {
                        segment->type = ENTITY_SMOKE_PUFF;
                        segment->flags &= (uint8_t)~EF_INITIALIZED;
                    }
                }
                ram8(v_soundControl) = SOUND_BOSS_DEFEATED;
                /* QUIRK: IX is left on slot 7, so the entity loop resumes
                 * after slot 7 whatever slot the arm was in. */
                cpu.ix = ENTITY_SLOT(7);
                bool late = ram8(v_level) >= 5 || (ram8(v_currentScreenNumber) & 0x7F) >= 3;
                ram8(v_nametableChangeRequest) =
                    late ? NT_CHANGE_OCTOPUS_DEFEATED_LATE : NT_CHANGE_OCTOPUS_DEFEATED;
                LIFTED_RETURN();
            }
        }
    }

    swing_arm(e);
    if (ENT_SCREEN_X(e) == 1) DESTROY_AND_RETURN(); /* scrolled out to the left */
    LIFTED_RETURN();
}

/* $4CD7 (inside updateOctopusArm): swing, follow the centre, place the segment
 * and publish its position for the next segment. */
static void swing_arm(Entity *e) {
    uint16_t angle_step = 0;
    if (e->unknown5 != 0) {
        e->unknown5--;
    } else {
        uint8_t direction = e->unknown6;
        angle_step = direction ? (uint16_t)-ARM_SWING_SPEED : ARM_SWING_SPEED;
        e->battleDecision++;
        if (e->battleDecision >= ARM_SWING_FRAMES) {
            e->unknown6 = (uint8_t)~direction;
            e->battleDecision = 0;
        }
    }

    if (e->unknown4 != 0) {
        /* Chained segment: orbit the previous segment. */
        uint16_t previous = ram16(ARM_PREVIOUS_Y);
        e->unknown11 = (uint8_t)(previous >> 8);
        e->unknown10 = (uint8_t)previous;
        e->unknown7 = ram8(ARM_PREVIOUS_SCREEN_Y);
        e->state = ram8(ARM_PREVIOUS_SCREEN_X);
    } else if (ram16(v_horizontalScrollSpeed) != 0) {
        /* Root segment: the centre scrolls with the level (8.8 fixed point). */
        uint32_t x = (uint32_t)ram16(v_horizontalScrollSpeed) + ENT_WORD(e->animationTimerResetValue, e->unknown11);
        e->unknown11 = (uint8_t)(x >> 8);
        e->animationTimerResetValue = (uint8_t)x;
        /* QUIRK: assumes the screen only scrolls to the right (negative speed). */
        if (x <= 0xFFFF) e->state++;
    } else if (ram16(v_verticalScrollSpeed) != 0) {
        uint16_t y = ENT_WORD(e->unknown1, e->unknown10);
        uint16_t speed = ram16(v_verticalScrollSpeed);
        uint16_t new_y = (uint16_t)(y - speed);
        e->unknown10 = (uint8_t)(new_y >> 8);
        e->unknown1 = (uint8_t)new_y;
        if (y < speed) { /* moved above the screen top: screens are 224 lines */
            e->unknown10 = (uint8_t)((new_y >> 8) - 0x40);
            e->unknown7--;
        }
    }

    uint16_t angle = (uint16_t)(ENT_WORD(e->stateTimer, e->unknown8) + angle_step);
    e->stateTimer = (uint8_t)angle;
    e->unknown8 = (uint8_t)(angle >> 8);
    call_routine(f_unknownAnimate);

    ram16(ARM_PREVIOUS_Y) = ENT_WORD(ENT_Y(e), ENT_X(e));
    ram8(ARM_PREVIOUS_SCREEN_Y) = ENT_SCREEN_Y(e);
    ram8(ARM_PREVIOUS_SCREEN_X) = ENT_SCREEN_X(e);
}
