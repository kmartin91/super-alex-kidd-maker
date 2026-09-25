/*
 * Score, money and high score: 3-byte packed BCD numbers stored least
 * significant byte first ($03CF-$0482), the digit drawing routine, the
 * points an entity is worth ($5761) and the unused division ($0759).
 *
 * Score and money are kept divided by 10 (a "0" tile is drawn after the six
 * digits): SCORE_VALUES holds 20, 40, 60, 80, 100, 120, 200 and 1000 (200 to
 * 10000 points), MONEY_BAG_VALUES 1 and 2 (10 and 20 baums).
 */
#include "game/core/core.h"

#define BCD_BYTES 3
#define BCD_MAX 0x99 /* every byte of 999999 */

/* Tile numbers of the digits 0-9 and of a blank. */
#define TILE_DIGIT_0 0xC0
#define TILE_BLANK 0x00

/* One byte of a BCD addition, exactly as ADD/ADC then DAA on the Z80 (also
 * for non-BCD inputs). `carry` is the carry in and out. */
static uint8_t bcd_add_byte(uint8_t x, uint8_t y, bool *carry) {
    unsigned cin = *carry;
    unsigned sum = x + y + cin;
    bool half = ((x & 0x0F) + (y & 0x0F) + cin) > 0x0F;
    bool c = sum > 0xFF;
    uint8_t a = (uint8_t)sum, correction = 0;
    if ((a & 0x0F) > 9 || half) correction |= 0x06;
    if (a > 0x99 || c) {
        correction |= 0x60;
        c = true;
    }
    *carry = c;
    return (uint8_t)(a + correction);
}

/* One byte of a BCD subtraction (SUB/SBC then DAA). `borrow` in and out. */
static uint8_t bcd_sub_byte(uint8_t x, uint8_t y, bool *borrow) {
    unsigned bin = *borrow;
    bool half = (x & 0x0F) < (y & 0x0F) + bin;
    bool c = x < y + bin;
    uint8_t a = (uint8_t)(x - y - bin), correction = 0;
    if ((a & 0x0F) > 9 || half) correction |= 0x06;
    if (a > 0x99 || c) {
        correction |= 0x60;
        c = true;
    }
    *borrow = c;
    return (uint8_t)(a - correction);
}

/* dst += src (3 BCD bytes). Returns the carry out of the top byte. */
static bool bcd_add(uint16_t dst, uint16_t src) {
    bool carry = false;
    for (int i = 0; i < BCD_BYTES; i++) wr8((uint16_t)(dst + i), bcd_add_byte(rd8((uint16_t)(dst + i)), rd8((uint16_t)(src + i)), &carry));
    return carry;
}

static void set_bcd_max(uint16_t dst) {
    for (int i = 0; i < BCD_BYTES; i++) wr8((uint16_t)(dst + i), BCD_MAX);
}

/* $040B: (BC) += (HL), 3 BCD bytes. Returns carry = overflow past 999999,
 * BC = address of the top byte. */
LIFTED(sumBCD, 0x040B) {
    bool carry = bcd_add(cpu.bc, cpu.hl);
    cpu.a = rd8((uint16_t)(cpu.bc + 2));
    cpu.bc += 2;
    cpu.hl += 2;
    core_set_carry(carry);
    LIFTED_RETURN();
}

/* $03CF: collect a money bag, L = 0 (small bag) or 3 (big bag). Money stops
 * at 999999. Nothing is earned while the demo plays. Returns BC as sumBCD
 * leaves it (or with C = $99 after the cap, unchanged in the demo). */
LIFTED(takeMoney, 0x03CF) {
    if (ram8(v_inputFlags) & INPUT_DEMO_PLAYING) LIFTED_RETURN();
    if (bcd_add(v_money, (uint16_t)(MONEY_BAG_VALUES + cpu.l))) {
        set_bcd_max(v_money);
        cpu.b = (uint8_t)((v_money + 2) >> 8);
        cpu.c = BCD_MAX;
    } else {
        cpu.bc = v_money + 2;
    }
    LIFTED_RETURN();
}

void core_add_score(uint8_t score_index) {
    if (ram8(v_inputFlags) & INPUT_DEMO_PLAYING) return;
    /* QUIRK: on overflow past 999999 it is the high score that is set to
     * 999999, while the score keeps its wrapped value. */
    if (bcd_add(v_score, (uint16_t)(SCORE_VALUES + score_index))) set_bcd_max(v_highScore);
}

/* $03ED: add points, L = index into SCORE_VALUES (SCORE_200 = 0, ... 3 bytes
 * per entry). Nothing is earned while the demo plays. */
LIFTED(addScore, 0x03ED) {
    core_add_score(cpu.l);
    LIFTED_RETURN();
}

/* $5761: add the points the entity IX is worth (ENTITY_POINTS, by type). */
LIFTED(earnEntityPoints, 0x5761) {
    uint8_t type = entity_at(cpu.ix)->type;
    core_add_score(rd8((uint16_t)(ENTITY_POINTS - 1 + type)));
    LIFTED_RETURN();
}

/* $041C: (BC) -= (HL), 3 BCD bytes (paying in a shop). QUIRK: only C is
 * incremented between bytes, so the number must not cross a 256-byte page
 * (v_money does not). Returns C = low byte of the top byte's address. */
LIFTED(subtractBCD, 0x041C) {
    bool borrow = false;
    uint16_t dst = cpu.bc, src = cpu.hl;
    for (int i = 0; i < BCD_BYTES; i++) {
        uint16_t d = core_inc_low(dst, (uint8_t)i);
        wr8(d, bcd_sub_byte(rd8(d), rd8((uint16_t)(src + i)), &borrow));
    }
    cpu.c = (uint8_t)(cpu.c + 2);
    cpu.hl += 2;
    LIFTED_RETURN();
}

/* $042D: compare (BC) with (HL) as 3 BCD bytes without storing anything:
 * carry set when (BC) < (HL) (not enough money). Same page quirk as
 * subtractBCD. Returns HL and C advanced to the top byte. */
LIFTED(subtractBCDToA, 0x042D) {
    bool borrow = false;
    uint16_t a = cpu.bc, b = cpu.hl;
    uint8_t top = 0;
    for (int i = 0; i < BCD_BYTES; i++) top = bcd_sub_byte(rd8(core_inc_low(a, (uint8_t)i)), rd8((uint16_t)(b + i)), &borrow);
    cpu.a = top;
    cpu.c = (uint8_t)(cpu.c + 2);
    cpu.hl += 2;
    core_set_carry(borrow);
    LIFTED_RETURN();
}

/* $043B: copy the score to the high score when it is at least as high. */
LIFTED(updateHighScore, 0x043B) {
    /* 3-byte binary comparison (valid for BCD): borrow = score < high score. */
    bool borrow = false;
    for (int i = 0; i < BCD_BYTES; i++) {
        uint8_t s = ram8(v_score + i), h = ram8(v_highScore + i);
        borrow = s < h + borrow;
    }
    if (!borrow)
        for (int i = BCD_BYTES - 1; i >= 0; i--) ram8(v_highScore + i) = ram8(v_score + i);
    LIFTED_RETURN();
}

/* One digit tile and its attribute byte; leading zeros are blanks. */
static void draw_digit(uint8_t digit, bool *leading) {
    if (digit) *leading = false;
    vdp_write(digit == 0 && *leading ? TILE_BLANK : (uint8_t)(TILE_DIGIT_0 + digit));
    vdp_write(ram8(v_nametableCopyFlags));
}

/* $0456: draw C BCD bytes (0 = 256) as 2*C digit tiles to the name table at
 * VDP command DE, from the most significant byte at HL down. Leading zeros
 * are drawn as blanks (all of them for a value of 0); the attribute byte of
 * every tile is v_nametableCopyFlags. The source must be RAM (all callers):
 * the original takes the digits out by rotating the byte in place with RLD,
 * restored by a third RLD, and the partly rotated values are visible in
 * between (to the attribute read, if the number includes that byte). */
LIFTED(drawBCDDigits, 0x0456) {
    uint16_t src = cpu.hl;
    bool leading = true;
    unsigned count = cpu.c ? cpu.c : 256;
    vdp_set_address(cpu.de);
    while (count--) {
        uint8_t packed = rd8(src);
        wr8(src, (uint8_t)(packed << 4));
        draw_digit(packed >> 4, &leading);
        wr8(src, (uint8_t)(packed >> 4));
        draw_digit(packed & 0x0F, &leading);
        wr8(src--, packed);
    }
    cpu.hl = src;
    cpu.bc = 0;
    cpu.e = (uint8_t)((cpu.e & 0x7F) | (leading ? 0x80 : 0)); /* E bit 7 held the "leading" flag */
    cpu.a = TILE_DIGIT_0;
    LIFTED_RETURN();
}

/* $0454: draw a 6-digit number (score, money) from its top byte at HL. */
LIFTED(drawThreeBcdBytes, 0x0454) {
    cpu.c = BCD_BYTES;
    TAIL_CALL(f_drawBCDDigits);
}

/* $0759 (never called): HL = HL / E, A = remainder, by 16 steps of
 * shift-and-subtract long division (E = 0 gives HL = $FFFF, A = old L). */
LIFTED(divideHLByE, 0x0759) {
    uint16_t quotient = cpu.hl;
    uint8_t remainder = 0, divisor = cpu.e;
    bool bit = false; /* quotient bit shifted in next (carry flag) */
    for (int step = 0; step < 17; step++) {
        bool top = quotient & 0x8000;
        quotient = (uint16_t)((quotient << 1) | bit);
        if (step == 16) break;
        /* 9-bit remainder: `top` is shifted in, overflow means >= divisor. */
        bool overflow = remainder & 0x80;
        remainder = (uint8_t)((remainder << 1) | top);
        bit = overflow || remainder >= divisor;
        if (bit) remainder = (uint8_t)(remainder - divisor);
    }
    cpu.hl = quotient;
    cpu.a = remainder;
    cpu.b = 0;
    LIFTED_RETURN();
}
