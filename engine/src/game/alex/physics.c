/*
 * Alex's physics: terrain probes, gravity and landing, horizontal and
 * vertical acceleration, friction and braking ($39ED-$3C3D, $3E04).
 *
 * Coordinates: Entity.xPos/yPos high bytes are the screen position of the
 * top-left corner of Alex's 16x24 box. Terrain is read from the RAM copy of
 * the name table ($C800, 32x28 entries of 2 bytes); the attribute byte of an
 * entry has bit 7 set for solid tiles (see TILE_* in alex.h).
 *
 * Probes take an offset packed as dy << 8 | dx from Alex's corner. The Z80
 * helpers they use keep a running "probe position" in B (y) / C (x) / HL
 * (entry pointer), so the probes are written close to the original.
 *
 * EX AF,AF': _LABEL_39ED_, isEntityCollidingWithTerrainAtOffset and
 * _LABEL_3A41_ park their A parameter and the caller's flags in AF' while
 * reading the first tile; when that tile is solid they return with AF' still
 * holding them. Nothing reads that value back, but it is part of the machine
 * state, so callers set cpu.f to the flags the original instruction sequence
 * leaves before the call (usually FLAGS_ZERO, from `or a` on isOffScreen).
 *
 * Movement rules (8.8 fixed point, per frame):
 *  - Gravity adds GRAVITY ($0040) to ySpeed. Falling speed is capped at
 *    4 px/frame: once ySpeed reaches $04xx its fraction is cleared.
 *  - Walking accelerates by $0040 up to $0200 (2 px/frame); releasing the pad
 *    applies friction $0020; pushing the other way brakes by $0040 and turns
 *    around once the speed crosses zero. In the air the same rules use $0010
 *    (accel/brake) and $0008 (friction), so jumps keep their momentum.
 *  - Speed limits are compared as unsigned numbers (QUIRK): the clamp is only
 *    right while the speed already has the sign of the acceleration.
 *
 * The flags these helpers leave are those of the original too: a few state
 * handlers chain them into a probe call (and so into F').
 */
#include "alex.h"

#define GRAVITY 0x0040
#define TERMINAL_FALL_SPEED_PX 4 /* fraction cleared from this speed on */
#define WATER_CHECK_OFFSET_Y 0x10 /* boat: water/ground probe below the hull */
#define BOAT_SECOND_PROBE_DX 0x0F
#define OFFSCREEN_Y_BIAS 0x40     /* offscreen probes work on y + $40 */
#define STUN_SHAKE_SPEED 0x0080

/* Solid tile at absolute screen position (y, x)? (_LABEL_7C7A_, then RLCA). */
static bool solid_at(uint8_t y, uint8_t x) {
    cpu.d = y;
    cpu.e = x;
    alex_call(f__LABEL_7C7A_);
    op_rlca();
    return cpu.f & FLAG_C;
}

/* Solid tile dx pixels right of the last probe (_LABEL_7C94_, then RLCA)? */
bool alex_probe_further_right(uint8_t dx) {
    cpu.e = dx;
    alex_call(f__LABEL_7C94_);
    cpu.a = rd8(cpu.hl);
    op_rlca();
    return cpu.f & FLAG_C;
}

/* ------------------------------------------------------------------ probes */

/* $3A03 isEntityCollidingWithTerrainAtOffset: solid tile at `offset`, or dy2
 * pixels below it? Returns with A = the rotated attribute and its flags. */
bool alex_probe_column(uint16_t offset, uint8_t dy2) {
    cpu.af_ = (uint16_t)(dy2 << 8 | cpu.f); /* EX AF,AF' (see header) */
    cpu.de = offset;
    alex_call(f_getNearEntityTileAttrWithOffset);
    op_rlca();
    if (cpu.f & FLAG_C) return true;
    op_ex_af();
    cpu.d = cpu.a;
    alex_call(f__LABEL_7CA3_);
    cpu.a = rd8(cpu.hl);
    op_rlca();
    return cpu.f & FLAG_C;
}

LIFTED(isEntityCollidingWithTerrainAtOffset, 0x3A03) {
    alex_probe_column(cpu.de, cpu.a);
    LIFTED_RETURN();
}

/* $39ED: wall probe. Like alex_probe_column, then extra_rows more tiles
 * 8 pixels apart below. Leaves DE = the BC it was called with. */
bool alex_probe_wall(uint16_t offset, uint8_t dy2, uint8_t extra_rows) {
    uint16_t saved_bc = (uint16_t)(extra_rows << 8 | cpu.c); /* push bc */
    bool hit = alex_probe_column(offset, dy2);
    cpu.de = saved_bc;
    if (hit) return true;
    cpu.a = extra_rows;
    do {
        op_ex_af();
        cpu.d = 8;
        alex_call(f__LABEL_7CA3_);
        cpu.a = rd8(cpu.hl);
        op_rlca();
        if (cpu.f & FLAG_C) return true;
        op_ex_af();
        cpu.a = alu_dec(cpu.a);
    } while (!(cpu.f & FLAG_Z));
    alu_or(cpu.a);
    return false;
}

LIFTED(_LABEL_39ED_, 0x39ED) {
    alex_probe_wall(cpu.de, cpu.a, cpu.b);
    LIFTED_RETURN();
}

/* $3A11: wall probe used while Alex is above the screen: tests the column at
 * x + dx at y + dy (+ the vertical speed), $0C and 0, in absolute terms
 * biased by $40. Keeps the probe column in A' (with the flags of its
 * addition in F'). */
bool alex_probe_wall_offscreen(uint16_t offset) {
    Entity *alex = ALEX;
    uint8_t x = add8_flags(HI(alex->xPos), (uint8_t)offset, 0);
    cpu.af_ = (uint16_t)(x << 8 | cpu.f);
    uint8_t y = (uint8_t)(HI(alex->yPos) + OFFSCREEN_Y_BIAS + (offset >> 8) + HI(alex->ySpeed));
    z80_cp_flags(y, 0xC0);
    if (y >= 0xC0) return false;
    if (y >= 0x0F) {
        if (solid_at(y, x)) return true;
        y = 0x0C;
    }
    if (solid_at(y, x)) return true;
    return solid_at(0x00, x);
}

LIFTED(_LABEL_3A11_, 0x3A11) {
    alex_probe_wall_offscreen(cpu.de);
    LIFTED_RETURN();
}

/* $3A41: ground/ceiling probe: solid tile at `offset`, or dx2 pixels right of
 * it? */
bool alex_probe_row(uint16_t offset, uint8_t dx2) {
    cpu.af_ = (uint16_t)(dx2 << 8 | cpu.f); /* EX AF,AF' (see header) */
    cpu.de = offset;
    alex_call(f_getNearEntityTileAttrWithOffset);
    op_rlca();
    if (cpu.f & FLAG_C) return true;
    op_ex_af();
    return alex_probe_further_right(cpu.a);
}

LIFTED(_LABEL_3A41_, 0x3A41) {
    alex_probe_row(cpu.de, cpu.a);
    LIFTED_RETURN();
}

/* $3A4F: ground probe while Alex is above the screen (absolute coordinates
 * biased by $40): at `offset` and 8 pixels right of it. */
bool alex_probe_row_offscreen(uint16_t offset) {
    Entity *alex = ALEX;
    uint8_t y = (uint8_t)(HI(alex->yPos) + OFFSCREEN_Y_BIAS + (offset >> 8));
    uint8_t x = (uint8_t)(HI(alex->xPos) + (uint8_t)offset);
    if (solid_at(y, x)) return true;
    return alex_probe_further_right(0x08);
}

LIFTED(_LABEL_3A4F_, 0x3A4F) {
    alex_probe_row_offscreen(cpu.de);
    LIFTED_RETURN();
}

/* ----------------------------------------------------- gravity and landing */

/* $3ACE (_LABEL_3AD5_): land on the tile found by the last probe (its y in
 * B): this frame's vertical speed is cut so that Alex's feet end exactly on
 * the tile's top edge. */
void alex_land(uint8_t probe_y) {
    Entity *alex = ALEX;
    alex->ySpeed = (uint16_t)((uint8_t)(HI(alex->ySpeed) - (probe_y & 7)) << 8);
    alex->unknown3 |= MOTION_LANDED;
}

LIFTED(_LABEL_3AD5_, 0x3ACE) {
    alex_land(cpu.b);
    LIFTED_RETURN();
}

/* $3A7E: while rising, stop at a ceiling: probes the row at `head_offset`
 * (and footWidth to the right); on a hit Alex starts falling from speed 0. */
void alex_check_ceiling(uint16_t head_offset) {
    Entity *alex = ALEX;
    if (HI(alex->isOffScreenFlags)) return;
    cpu.f = FLAGS_ZERO;
    if (!alex_probe_row(head_offset, alex->unknown9)) return;
    alex->unknown3 |= MOTION_FALLING;
    alex->ySpeed = 0;
    cpu.hl = 0;
}

LIFTED(_LABEL_3A7E_, 0x3A7E) {
    alex_check_ceiling(cpu.de);
    LIFTED_RETURN();
}

/* Adds gravity; returns the pixel part of the new speed, or -1 when Alex is
 * still rising (ceiling checked). */
static int apply_gravity(uint16_t head_offset) {
    Entity *alex = ALEX;
    uint32_t speed = (uint32_t)alex->ySpeed + GRAVITY;
    alex->ySpeed = (uint16_t)speed;
    if (speed > 0xFFFF) alex->unknown3 |= MOTION_FALLING; /* crossed zero */
    if (!(alex->unknown3 & MOTION_FALLING)) {
        alex_check_ceiling(head_offset);
        return -1;
    }
    uint8_t speed_px = (uint8_t)(speed >> 8);
    if (speed_px >= TERMINAL_FALL_SPEED_PX) LO(alex->ySpeed) = 0;
    return speed_px;
}

/* $3A68: one frame of gravity for Alex in the air. When falling, the ground
 * is probed where the feet (feetOffset = unknown11 below the corner) will be
 * after this frame's move, at x + dx and x + dx + footWidth (unknown9). */
void alex_gravity(uint16_t head_offset) {
    Entity *alex = ALEX;
    int speed_px = apply_gravity(head_offset);
    if (speed_px < 0) return;
    uint8_t feet_dy = (uint8_t)(alex->unknown11 + speed_px);
    if (HI(alex->isOffScreenFlags)) {
        /* only the carry of the second addition counts (8-bit arithmetic) */
        unsigned y = (uint8_t)(HI(alex->yPos) + OFFSCREEN_Y_BIAS) + feet_dy;
        if (y < 0x100) return; /* feet still above the screen */
        if (solid_at((uint8_t)y, (uint8_t)(HI(alex->xPos) + (uint8_t)head_offset)) ||
            alex_probe_further_right(alex->unknown9))
            alex_land(cpu.b);
        return;
    }
    cpu.f = FLAGS_ZERO;
    if (alex_probe_row((uint16_t)(feet_dy << 8 | (uint8_t)head_offset), alex->unknown9)) alex_land(cpu.b);
}

LIFTED(_LABEL_3A68_, 0x3A68) {
    alex_gravity(cpu.de);
    LIFTED_RETURN();
}

/* $3AE1 (_LABEL_3AE8_): gravity for the jumping boat: it lands on water
 * tiles; touching a solid tile wrecks it. */
void alex_boat_gravity(uint16_t head_offset) {
    int speed_px = apply_gravity(head_offset);
    if (speed_px < 0) return;
    cpu.de = (uint16_t)((uint8_t)(WATER_CHECK_OFFSET_Y + speed_px) << 8 | (uint8_t)head_offset);
    alex_call(f_getNearEntityTileAttrWithOffset);
    for (int probe = 0; probe < 2; probe++) {
        if (probe == 1) {
            cpu.e = BOAT_SECOND_PROBE_DX;
            alex_call(f__LABEL_7C94_);
            cpu.a = rd8(cpu.hl);
        }
        uint8_t attr = cpu.a;
        if (attr & TILE_SOLID) {
            alex_crash_vehicle();
            return;
        }
        if ((attr & TILE_CLASS_MASK) == TILE_CLASS_WATER) {
            alex_land(cpu.b);
            return;
        }
    }
}

LIFTED(_LABEL_3AE8_, 0x3AE1) {
    alex_boat_gravity(cpu.de);
    LIFTED_RETURN();
}

/* ------------------------------------------------------ horizontal motion */

/* $3B4F resetEntityUnknown3AndAlexSpeed: stop moving horizontally. Leaves
 * HL = 0 (callers store it in ySpeed too); flags are untouched. */
void alex_stop(void) {
    ALEX->unknown3 &= (uint8_t)~MOTION_HORIZONTAL;
    ALEX->xSpeed = 0;
    cpu.hl = 0;
}

LIFTED(resetEntityUnknown3AndAlexSpeed, 0x3B4F) {
    alex_stop();
    LIFTED_RETURN();
}

/* $3B56 */
void alex_set_x_speed(uint16_t speed) { ALEX->xSpeed = speed; }

LIFTED(sub_3B56, 0x3B56) {
    alex_set_x_speed(cpu.hl);
    LIFTED_RETURN();
}

/* Adds accel to a speed and clamps it: the result may not go beyond `limit`
 * in the direction of `beyond_is_below` (unsigned comparison, QUIRK). The
 * flags are those of the original's `or a; sbc hl,bc`. */
static uint16_t accelerate(uint16_t speed, uint16_t accel, uint16_t limit, bool beyond_is_below) {
    speed = (uint16_t)(speed + accel);
    cpu.f &= (uint8_t)~FLAG_C;
    alu_sbc16(speed, limit);
    bool below = cpu.f & FLAG_C;
    return below == beyond_is_below ? limit : speed;
}

/* $3B24 accelerateAlexLeft: accel and max_speed are negative. */
void alex_accelerate_left(uint16_t accel, uint16_t max_speed) {
    Entity *alex = ALEX;
    alex->unknown3 = (uint8_t)((alex->unknown3 | MOTION_HORIZONTAL) & ~(MOTION_FACING_RIGHT | MOTION_RIGHT));
    alex->xSpeed = accelerate(alex->xSpeed, accel, max_speed, true);
}

LIFTED(accelerateAlexLeft, 0x3B24) {
    alex_accelerate_left(cpu.de, cpu.bc);
    LIFTED_RETURN();
}

/* $3B77 accelerateAlexRight */
void alex_accelerate_right(uint16_t accel, uint16_t max_speed) {
    Entity *alex = ALEX;
    alex->unknown3 |= MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL;
    alex->xSpeed = accelerate(alex->xSpeed, accel, max_speed, false);
}

LIFTED(accelerateAlexRight, 0x3B77) {
    alex_accelerate_right(cpu.de, cpu.bc);
    LIFTED_RETURN();
}

/* $3B49 applyFrictionMovingLeft: friction (positive) slows a leftward
 * motion; stops when the speed crosses zero. Returns (and leaves in the carry
 * flag) whether Alex stopped. */
bool alex_friction_left(uint16_t friction) {
    uint16_t speed = alu_add16(ALEX->xSpeed, friction);
    if (cpu.f & FLAG_C) {
        alex_stop();
        return true;
    }
    alex_set_x_speed(speed);
    return false;
}

LIFTED(applyFrictionMovingLeft, 0x3B49) {
    alex_friction_left(cpu.de);
    LIFTED_RETURN();
}

/* $3B44 (_LABEL_3B4B_) */
void alex_friction_left_if_moving(uint16_t friction) {
    if (z80_test_ix(ALEX->unknown3, MOTION_HORIZONTAL)) alex_friction_left(friction);
}

LIFTED(_LABEL_3B4B_, 0x3B44) {
    alex_friction_left_if_moving(cpu.de);
    LIFTED_RETURN();
}

/* $3B9A applyFrictionMovingRight: friction (negative) slows a rightward
 * motion. Returns (carry) true while Alex keeps moving right... or when the
 * speed became exactly 0 (then he is stopped). */
bool alex_friction_right(uint16_t friction) {
    cpu.f &= (uint8_t)~FLAG_C;
    uint16_t speed = alu_adc16(ALEX->xSpeed, friction);
    bool carry = cpu.f & FLAG_C;
    if (!carry || speed == 0) alex_stop();
    else alex_set_x_speed(speed);
    return carry;
}

LIFTED(applyFrictionMovingRight, 0x3B9A) {
    alex_friction_right(cpu.de);
    LIFTED_RETURN();
}

/* $3B95 (_LABEL_3B9C_) */
void alex_friction_right_if_moving(uint16_t friction) {
    if (z80_test_ix(ALEX->unknown3, MOTION_HORIZONTAL)) alex_friction_right(friction);
}

LIFTED(_LABEL_3B9C_, 0x3B95) {
    alex_friction_right_if_moving(cpu.de);
    LIFTED_RETURN();
}

/* $3B5A leftBrake: moving left while pushing right. Brakes by `brake`
 * (positive), facing right; once stopped Alex also moves right. */
void alex_turn_right(uint16_t brake) {
    Entity *alex = ALEX;
    uint8_t motion = alex->unknown3;
    if (z80_test(motion, MOTION_HORIZONTAL)) {
        motion |= MOTION_FACING_RIGHT;
        uint16_t speed = alu_add16(alex->xSpeed, brake);
        if (!(cpu.f & FLAG_C)) {
            alex->xSpeed = speed;
            alex->unknown3 = motion;
            return;
        }
    }
    motion = (uint8_t)((motion | MOTION_FACING_RIGHT | MOTION_RIGHT) & ~MOTION_HORIZONTAL);
    z80_logic_flags(motion, true);
    alex->xSpeed = 0;
    alex->unknown3 = motion;
}

LIFTED(leftBrake, 0x3B5A) {
    alex_turn_right(cpu.de);
    LIFTED_RETURN();
}

/* $3BAA rightBrake: moving right while pushing left. Brakes by `brake`
 * (negative), facing left; once stopped Alex also moves left. */
void alex_turn_left(uint16_t brake) {
    Entity *alex = ALEX;
    uint8_t motion = alex->unknown3;
    if (z80_test(motion, MOTION_HORIZONTAL)) {
        motion &= (uint8_t)~MOTION_FACING_RIGHT;
        z80_logic_flags(motion, true);
        uint16_t speed = alu_adc16(alex->xSpeed, brake);
        if (speed != 0 && (cpu.f & FLAG_C)) {
            alex->xSpeed = speed;
            alex->unknown3 = motion;
            return;
        }
    }
    motion &= (uint8_t)~(MOTION_FACING_RIGHT | MOTION_RIGHT | MOTION_HORIZONTAL);
    z80_logic_flags(motion, true);
    alex->xSpeed = 0;
    alex->unknown3 = motion;
}

LIFTED(rightBrake, 0x3BAA) {
    alex_turn_left(cpu.de);
    LIFTED_RETURN();
}

/* $3BC8 (_LABEL_3BCF_): vehicles: slow down by decel (negative), not below
 * min_speed (the motorcycle and the boat never stop). */
void alex_slow_down_to(uint16_t decel, uint16_t min_speed) {
    ALEX->xSpeed = accelerate(ALEX->xSpeed, decel, min_speed, true);
}

LIFTED(_LABEL_3BCF_, 0x3BC8) {
    alex_slow_down_to(cpu.de, cpu.bc);
    LIFTED_RETURN();
}

/* -------------------------------------- vertical motion (swimming, flying) */

/* $3BDA (_LABEL_3BE1_): accelerate upwards (accel and max_speed negative). */
void alex_accelerate_up(uint16_t accel, uint16_t max_speed) {
    Entity *alex = ALEX;
    alex->unknown3 |= MOTION_VERTICAL;
    alex->ySpeed = accelerate(alex->ySpeed, accel, max_speed, true);
}

LIFTED(_LABEL_3BE1_, 0x3BDA) {
    alex_accelerate_up(cpu.de, cpu.bc);
    LIFTED_RETURN();
}

/* $3BF0 (_LABEL_3BF7_): slow a rising motion by `brake` (positive); once
 * stopped the vertical direction becomes "down". */
void alex_brake_rising(uint16_t brake) {
    Entity *alex = ALEX;
    uint8_t motion = alex->unknown3;
    if (z80_test(motion, MOTION_VERTICAL)) {
        uint16_t speed = alu_add16(alex->ySpeed, brake);
        if (!(cpu.f & FLAG_C)) {
            alex->ySpeed = speed;
            alex->unknown3 = motion;
            return;
        }
    }
    motion = (uint8_t)((motion | MOTION_DOWN) & ~MOTION_VERTICAL);
    z80_logic_flags(motion, true);
    alex->ySpeed = 0;
    alex->unknown3 = motion;
}

LIFTED(_LABEL_3BF7_, 0x3BF0) {
    alex_brake_rising(cpu.de);
    LIFTED_RETURN();
}

/* $3C0B (_LABEL_3C12_): accelerate downwards up to max_speed. */
void alex_accelerate_down(uint16_t accel, uint16_t max_speed) {
    Entity *alex = ALEX;
    alex->unknown3 |= MOTION_VERTICAL;
    alex->ySpeed = accelerate(alex->ySpeed, accel, max_speed, false);
}

LIFTED(_LABEL_3C12_, 0x3C0B) {
    alex_accelerate_down(cpu.de, cpu.bc);
    LIFTED_RETURN();
}

/* $3C21 (_LABEL_3C28_): slow a sinking motion by `brake` (negative); once
 * stopped the vertical direction becomes "up". */
void alex_brake_sinking(uint16_t brake) {
    Entity *alex = ALEX;
    uint8_t motion = alex->unknown3;
    if (z80_test(motion, MOTION_VERTICAL)) {
        z80_logic_flags(motion, false); /* or a */
        uint16_t speed = alu_adc16(alex->ySpeed, brake);
        if (speed != 0 && (cpu.f & FLAG_C)) {
            alex->ySpeed = speed;
            alex->unknown3 = motion;
            return;
        }
    }
    motion &= (uint8_t)~(MOTION_DOWN | MOTION_VERTICAL);
    z80_logic_flags(motion, true);
    alex->ySpeed = 0;
    alex->unknown3 = motion;
}

LIFTED(_LABEL_3C28_, 0x3C21) {
    alex_brake_sinking(cpu.de);
    LIFTED_RETURN();
}

/* ---------------------------------------------------------------- stun */

/* $3E04 tickJitter: after punching a skull box Alex shakes left and right
 * (half a pixel per frame, direction flipping every 2 frames) until the stun
 * timer (unknown6) runs out. */
void alex_tick_stun(void) {
    Entity *alex = ALEX;
    alex->unknown6 = alu_dec(alex->unknown6);
    if (alex->unknown6 == 0) {
        alex->unknown3 &= (uint8_t)~MOTION_HORIZONTAL;
        alex->xSpeed = 0;
        alex->unknown8 &= (uint8_t)~ACTFLAG_STUNNED;
        return;
    }
    alex->xSpeed = z80_test_ix(alex->unknown6, 0x02) ? STUN_SHAKE_SPEED : (uint16_t)-STUN_SHAKE_SPEED;
}

LIFTED(tickJitter, 0x3E04) {
    alex_tick_stun();
    LIFTED_RETURN();
}
