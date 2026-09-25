/*
 * Start-up delay, keyboard detection and controller reading ($0341-$03CE).
 */
#include "game/core/core.h"

/* SC-3000 keyboard rows (PPI port C value) and the bits of PORT_JOYPAD1 that
 * the game reads in each, mapped to the pad bits they stand for. */
#define KEYBOARD_ROW_PAD 7   /* the joypad itself */
#define KEYBOARD_ROW_DOWN 4
#define KEYBOARD_ROW_LEFT 5
#define KEYBOARD_ROW_RIGHT_UP 6
#define KEYBOARD_ROW_BUTTON1 2
#define KEYBOARD_ROW_BUTTON2 3
#define KEY_ARROWS 0x20      /* bit of the arrow key in rows 4-6 */
#define KEY_UP 0x40          /* up arrow, row 6 */
#define KEY_BUTTON 0x10      /* bit of the button key in rows 2-3 */

/* $0343: busy-wait B tenths of a second (0 = 256). Only CPU time: nothing
 * observable changes, the registers end as the loop leaves them. */
LIFTED(sleepTenthsOfSecond, 0x0343) {
    cpu.b = 0;
    cpu.a = 0;
    cpu.f = FLAG_Z | FLAG_P;
    LIFTED_RETURN();
}

/* $0341: busy-wait about a second (at power-on). */
LIFTED(sleepOneSecond, 0x0341) {
    cpu.b = 10;
    TAIL_CALL(f_sleepTenthsOfSecond);
}

/* $0350: detect the SC-3000 keyboard PPI: it reads back the row just
 * written. Sets or clears INPUT_FROM_KEYBOARD in v_inputFlags (bit 1 kept). */
LIFTED(configurePPI, 0x0350) {
    io_out(PORT_KEYBOARD_CONTROL, 0x92);
    uint8_t flags = (ram8(v_inputFlags) & 0x02) | INPUT_FROM_KEYBOARD;
    ram8(v_inputFlags) = flags;
    io_out(PORT_KEYBOARD_ROW, 0);
    if (io_in(PORT_KEYBOARD_ROW) != 0) ram8(v_inputFlags) = flags & ~INPUT_FROM_KEYBOARD;
    cpu.hl = v_inputFlags;
    LIFTED_RETURN();
}

/* Reads keyboard row `row`; returns the port bits (active low). */
static uint8_t read_keyboard_row(uint8_t row) {
    io_out(PORT_KEYBOARD_ROW, row);
    return io_in(PORT_JOYPAD1);
}

/* The keyboard as a pad: arrows and two keys mapped onto the pad bits
 * (all active low, like the pad port). */
static uint8_t read_keyboard_as_pad(void) {
    uint8_t pad = read_keyboard_row(KEYBOARD_ROW_PAD);
    if (!(read_keyboard_row(KEYBOARD_ROW_DOWN) & KEY_ARROWS)) pad &= ~JOY_DOWN;
    if (!(read_keyboard_row(KEYBOARD_ROW_LEFT) & KEY_ARROWS)) pad &= ~JOY_LEFT;
    uint8_t row = read_keyboard_row(KEYBOARD_ROW_RIGHT_UP);
    if (!(row & KEY_ARROWS)) pad &= ~JOY_RIGHT;
    if (!(row & KEY_UP)) pad &= ~JOY_UP;
    if (!(read_keyboard_row(KEYBOARD_ROW_BUTTON1) & KEY_BUTTON)) pad &= ~JOY_BTN1;
    if (!(read_keyboard_row(KEYBOARD_ROW_BUTTON2) & KEY_BUTTON)) pad &= ~JOY_BTN2;
    return pad;
}

/* $0367: read the controls (from the VBlank interrupt). v_inputData = buttons
 * held, v_inputDataChanges = buttons pressed since the previous frame (JOY_*
 * bits, active high). Returns C = buttons held. */
LIFTED(readInput, 0x0367) {
    uint8_t port = (ram8(v_inputFlags) & INPUT_FROM_KEYBOARD) ? read_keyboard_as_pad() : io_in(PORT_JOYPAD1);
    uint8_t held = (uint8_t)~port;
    uint8_t pressed = (uint8_t)((held ^ ram8(v_inputData)) & held);
    ram8(v_inputData) = held;
    ram8(v_inputDataChanges) = pressed;
    cpu.c = held;
    cpu.a = pressed;
    cpu.hl = v_inputDataChanges;
    LIFTED_RETURN();
}
