/*
 * Camera: how each level scrolls with Alex ($3F55-$4181). Run by updateAlex
 * after the state handler, unless Alex is dead or stunned.
 *
 * The handler depends on the level (table at $3F31):
 *   levels 1, 17      vertical sections (scroll down), then horizontal
 *   level 13          horizontal, threshold $80 (peticopter flown left)
 *   levels 5, 9       horizontal; a vehicle crash drops Alex into the water
 *                     below: the screen scrolls down while he dives
 *   levels 11, 16     flip-screen: at an edge of the screen the view scrolls
 *                     by a whole screen while Alex is frozen (state $10)
 *   the others        horizontal
 *
 * Horizontal scrolling ($411D): the screen scrolls at Alex's speed once he
 * passes a threshold column (camera look-ahead: $60 plus 1/16 of his speed in
 * 8.8, i.e. $80 at full walking speed to the right, $40 to the left). When
 * the level cannot scroll that way (v_scrollFlags), Alex stops at the edge
 * of the screen (x < 4 or x >= $F4). Vehicles are kept near x = $44: past it
 * the screen scrolls 1 px/frame faster than the vehicle.
 * v_horizontalScrollSpeed is negative when the view moves right.
 */
#include "alex.h"

#define SCROLL_DOWN 0x01
#define SCROLL_UP 0x02
#define SCROLL_LEFT 0x04
#define SCROLL_RIGHT 0x08
#define SCROLL_ANY 0x0F
#define VEHICLE_STATES 0x3FAA  /* bank 0: 1 byte per Alex state, 1 = vehicle */
#define VEHICLE_SCREEN_X 0x44
#define SCREEN_LEFT_LIMIT 0x04
#define SCREEN_RIGHT_LIMIT 0xF4
#define SCREEN_BOTTOM 0xA8
#define SCREEN_TOP 0x04
#define FLIP_SCROLL_SPEED 0x0400

/* $411D (_LABEL_4124_): horizontal scrolling with threshold column
 * `threshold`. */
static void scroll_horizontally(uint8_t threshold) {
    Entity *alex = ALEX;
    uint8_t flags = ram8(v_scrollFlags);
    uint8_t x = HI(alex->xPos);
    uint16_t speed = alex->xSpeed;
    if (speed & 0x8000) {
        /* moving left */
        if (!(flags & SCROLL_LEFT)) {
            if (x >= SCREEN_LEFT_LIMIT) return;
            alex_stop();
            ram16(v_horizontalScrollSpeed) = 0;
            return;
        }
        if (x >= threshold) return;
    } else {
        if (!(flags & SCROLL_RIGHT)) {
            if (x < SCREEN_RIGHT_LIMIT) return;
            alex_stop();
            ram16(v_horizontalScrollSpeed) = 0;
            return;
        }
        if (x < threshold) return;
    }
    ram16(v_horizontalScrollSpeed) = (uint16_t)-speed;
}

LIFTED(_LABEL_4124_, 0x411D) {
    scroll_horizontally(cpu.b);
    LIFTED_RETURN();
}

/* Threshold column depending on the speed: $60 + (xSpeed >> 4) & $FF. */
static uint8_t look_ahead_threshold(void) {
    return (uint8_t)(0x60 + (uint8_t)(ALEX->xSpeed >> 4));
}

/* $4157 (_LABEL_415E_): vehicles and special states (state >= 7) are kept
 * inside the screen vertically: rising, they stop at the top line (and start
 * falling). */
static void clamp_vertically(void) {
    Entity *alex = ALEX;
    if (alex->state < ALEX_STATE_CANE_FLIGHT) return;
    uint8_t y = HI(alex->yPos);
    if (alex->ySpeed & 0x8000) {
        if (y >= SCREEN_TOP) return;
        alex->unknown3 |= MOTION_FALLING;
    } else {
        /* QUIRK: meant to stop them at the bottom line ($A8), but compares
         * the off-screen flag (0 here) instead of y, so it never does. */
        uint8_t offscreen = HI(alex->isOffScreenFlags);
        if (offscreen || offscreen < SCREEN_BOTTOM) return;
    }
    alex->ySpeed = 0;
    alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
}

LIFTED(_LABEL_415E_, 0x4157) {
    clamp_vertically();
    LIFTED_RETURN();
}

/* $40E0 (_LABEL_40E7_): levels with vertical sections. While the level
 * scrolls vertically, the view follows Alex going down once he is below line
 * $50; when it cannot scroll down, he stops at the bottom ($A8). */
static void follow_vertically(void) {
    Entity *alex = ALEX;
    uint8_t flags = ram8(v_scrollFlags);
    if (!(flags & (SCROLL_UP | SCROLL_DOWN))) {
        clamp_vertically();
        return;
    }
    uint8_t y = HI(alex->yPos);
    if (alex->ySpeed & 0x8000) return; /* going up: nothing */
    if (flags & SCROLL_DOWN) {
        if (y < 0x50) return;
        if (HI(alex->isOffScreenFlags)) return;
        ram16(v_verticalScrollSpeed) = alex->ySpeed;
        return;
    }
    if (y < SCREEN_BOTTOM) return;
    alex->ySpeed = 0;
    alex->unknown3 &= (uint8_t)~MOTION_VERTICAL;
}

LIFTED(_LABEL_40E7_, 0x40E0) {
    follow_vertically();
    LIFTED_RETURN();
}

/* $3F55 (_LABEL_3F5C_): levels 1 and 17. */
static void camera_vertical_sections(void) {
    follow_vertically();
    scroll_horizontally(look_ahead_threshold());
}

LIFTED(_LABEL_3F5C_, 0x3F55) {
    camera_vertical_sections();
    LIFTED_RETURN();
}

/* $3F66 (_LABEL_3F6D_): level 13. */
static void camera_level_13(void) {
    clamp_vertically();
    scroll_horizontally(0x80);
}

LIFTED(_LABEL_3F6D_, 0x3F66) {
    camera_level_13();
    LIFTED_RETURN();
}

/* $3F6E (_LABEL_3F75_): the usual horizontal camera. */
static void camera_horizontal(void) {
    Entity *alex = ALEX;
    clamp_vertically();
    if (!rd8((uint16_t)(VEHICLE_STATES + entity_at(cpu.ix)->state))) {
        scroll_horizontally(look_ahead_threshold());
        return;
    }
    /* on a vehicle: keep it near x = $44 */
    if (HI(alex->xPos) < VEHICLE_SCREEN_X) {
        scroll_horizontally(0x40);
        return;
    }
    if (!(ram8(v_scrollFlags) & SCROLL_RIGHT)) return;
    ram16(v_horizontalScrollSpeed) = (uint16_t)(0xFF00 - alex->xSpeed);
}

LIFTED(_LABEL_3F75_, 0x3F6E) {
    camera_horizontal();
    LIFTED_RETURN();
}

/* $3FCA (_LABEL_3FD1_): levels 5 and 9. After a vehicle crash (state $1B),
 * Alex drops into the water below: the screen scrolls down 3 px/frame while
 * he dives (state $13). While bit 1 of v_levelData_C0B7_ is set the dive has
 * not started yet: the view keeps moving right and Alex falls along at
 * 1 px/frame (stopping at line $90). */
static void camera_crash_into_water(void) {
    Entity *alex = ALEX;
    uint8_t state = alex->state;
    if (state == ALEX_STATE_VEHICLE_CRASH) {
        if (ram8(v_levelData_C0B7_) & 0x02) {
            if (HI(alex->yPos) >= 0x90) alex->ySpeed = 0;
            ram16(v_horizontalScrollSpeed) = 0xFF00;
            alex->xSpeed = 0x0100;
            return;
        }
        entity_at(cpu.ix)->state = ALEX_STATE_DIVING;
        push16(cpu.ix);
        alex_call(f__LABEL_6671_);
        cpu.ix = pop16();
        ram8(v_scrollFlags) |= SCROLL_DOWN;
    } else if (state != ALEX_STATE_DIVING) {
        camera_horizontal();
        return;
    }
    if (HI(alex->yPos) < 0x08) alex->ySpeed = 0x0300;
    ram16(v_verticalScrollSpeed) = 0x0300;
}

LIFTED(_LABEL_3FD1_, 0x3FCA) {
    camera_crash_into_water();
    LIFTED_RETURN();
}

/* $401E (_LABEL_4025_): levels 11 and 16, flip-screen. When Alex reaches the
 * bottom of the screen (falling, or sinking), the view scrolls down one
 * screen; at the bottom while rising... up; at the right edge ($F0, $E8 when
 * swimming) it scrolls right or left depending on his direction. Alex is
 * frozen meanwhile (state $10) and moves slowly to stay in place relative to
 * the scrolling screen. */
static void camera_flip_screen(void) {
    Entity *alex = ALEX;
    uint8_t flags = ram8(v_scrollFlags) & SCROLL_ANY;
    if (flags) {
        /* a flip is going on: keep its scroll speed */
        if (flags & SCROLL_DOWN) ram16(v_verticalScrollSpeed) = FLIP_SCROLL_SPEED;
        else if (flags & SCROLL_UP) ram16(v_verticalScrollSpeed) = (uint16_t)-FLIP_SCROLL_SPEED;
        else if (flags & SCROLL_LEFT) ram16(v_horizontalScrollSpeed) = FLIP_SCROLL_SPEED;
        else ram16(v_horizontalScrollSpeed) = (uint16_t)-FLIP_SCROLL_SPEED;
        return;
    }
    if (HI(alex->yPos) >= SCREEN_BOTTOM) {
        uint8_t motion = alex->unknown3;
        bool down;
        if (motion & MOTION_DOWN) down = true;
        else if (!(motion & MOTION_FALLING)) down = false;
        else if (motion & MOTION_LANDED) down = true;
        else down = alex->ySpeed != 0;
        alex_freeze_for_screen_flip();
        if (down) {
            push16(cpu.ix);
            alex_call(f__LABEL_6671_);
            cpu.ix = pop16();
            ram8(v_scrollFlags) |= SCROLL_DOWN;
            alex->ySpeed = 0x0080;
            ram16(v_verticalScrollSpeed) = FLIP_SCROLL_SPEED;
        } else {
            ram8(v_scrollFlags) |= SCROLL_UP;
            alex->ySpeed = 0xFF80;
            ram16(v_verticalScrollSpeed) = (uint16_t)-FLIP_SCROLL_SPEED;
        }
        return;
    }
    uint8_t edge = alex->state == ALEX_STATE_SWIMMING ? 0xE8 : 0xF0;
    if (HI(alex->xPos) < edge) return;
    bool right = z80_test_ix(alex->unknown3, MOTION_RIGHT);
    alex_freeze_for_screen_flip();
    bool swimming = ram8(v_alexStateTemporaryCopy) == ALEX_STATE_SWIMMING;
    if (right) {
        ram8(v_scrollFlags) |= SCROLL_RIGHT;
        alex->xSpeed = swimming ? 0x0060 : 0x0040;
        ram16(v_horizontalScrollSpeed) = (uint16_t)-FLIP_SCROLL_SPEED;
    } else {
        ram8(v_scrollFlags) |= SCROLL_LEFT;
        alex->xSpeed = swimming ? 0xFFA0 : 0xFFC0;
        ram16(v_horizontalScrollSpeed) = FLIP_SCROLL_SPEED;
    }
}

LIFTED(_LABEL_4025_, 0x401E) {
    camera_flip_screen();
    LIFTED_RETURN();
}

/* The camera handler of the current level (table at $3F31). */
void alex_update_camera(void) {
    uint8_t level = ram8(v_level);
    switch (level) {
    case 1: case 17: camera_vertical_sections(); break;
    case 5: case 9: camera_crash_into_water(); break;
    case 11: case 16: camera_flip_screen(); break;
    case 13: camera_level_13(); break;
    case 2: case 3: case 4: case 6: case 7: case 8: case 10: case 12: case 14: case 15:
        camera_horizontal();
        break;
    default: /* not a level: dispatch as the original */
        cpu.a = level;
        cpu.hl = 0x3F31;
        alex_call(f_jumpToAthPointer);
        break;
    }
}
