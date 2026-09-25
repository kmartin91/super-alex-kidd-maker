/*
 * Text boxes / dialogues (game state 7, $7DC2-$7F48).
 *
 * v_textBoxMessageIndex selects a message of textPointers ($7F49, bank 7;
 * 1 = shop welcome ... $16 = shop sold out). Messages are "typed" one
 * character per frame, frame included: a message is a list of segments
 *   count, flags, count characters
 * The VBlank handler writes the character under the cursor (the pseudo
 * entity v_textboxCursor, $CFE0, whose position is in pixels) into VRAM and
 * advances the text pointer, except when bit 7 of the count is set (the same
 * character is repeated count & $7F times, used for the box borders). After
 * each character the main loop moves the cursor by 8 pixels as the segment's
 * flags say: bit 5 right, bits 6+5 left, bit 7 down, bits 7+6 up. A segment
 * with none of these bits ends the message: the game then waits for button 1
 * or 2 (animating Saint Nurari or the village elder when one of them speaks),
 * restores the name table saved in RAM and resumes the janken battle ($89),
 * the shop ($85) or the gameplay ($8A).
 */
#include "states.h"

#define TEXT_POINTERS 0x7F49 /* bank 0 table, messages in bank 7 */
#define TEXT_ATTRIBUTE 0x10  /* name-table attribute of the characters (in front of sprites) */

/* v_textBoxFlags: cursor move after each character. */
#define TEXT_MOVE_MASK 0xE0
#define TEXT_MOVE_VERTICAL 0x80
#define TEXT_MOVE_BACKWARDS 0x40 /* left / up */
#define TEXT_REPEAT 0x80         /* in v_textBoxCounter */

/* v_nextMapNametableUpdateTimer is reused here: 1 = print a character at VBlank. */
#define v_textBoxPrintRequest v_nextMapNametableUpdateTimer

/* Reads the next segment header (count, flags) at `p`. */
static void load_text_segment(uint16_t p) {
    ram8(v_textBoxCounter) = rd8(p);
    ram8(v_textBoxFlags) = rd8((uint16_t)(p + 1));
    ram16(v_currentMapOrTextNametablePointer) = (uint16_t)(p + 2);
}

/* _LABEL_7ED3_: opens the text box (first call of the state). */
static void open_text_box(void) {
    map_bank(BANK(2));
    CALL_HELPER(f_updateEntities);
    wait_frame(IRQ_SPRITES);
    ram8(v_gameState) |= STATE_INITIALIZED;
    if (ram8(v_hasBattleStarted)) ram8(v_soundControl) = SOUND_CASTLE_SONG;

    map_bank(BANK(7));
    load_text_segment(load_ath_pointer(TEXT_POINTERS - 2, ram8(v_textBoxMessageIndex)));
    ram16(_RAM_C074_) = 0x0100;
    /* Cursor at the top-left corner of the box. */
    cpu.ix = v_textboxCursor;
    Entity *cursor = entity_at(v_textboxCursor);
    Y_PIXEL(cursor) = 0x11;
    X_PIXEL(cursor) = 0x09;
    ram8(v_textBoxMessageIndex) = 0;
    ram8(v_textBoxPrintRequest) = 0;
    ram8(v_soundControl) = SOUND_TEXTBOX;
}

/* _LABEL_7E5E_ and what follows: the message is complete; waits for a
 * button, then closes the box and resumes the previous state. */
static void wait_and_close_text_box(void) {
    ram8(v_soundControl) = SOUND_FX_1;
    map_bank(BANK(2));
    bool speaker = ram8(v_shouldShowNuraiOrOldMan) != 0;
    if (speaker) {
        cpu.ix = ram16(v_nuraiOrOldManEntityTemporaryPointer);
        Entity *e = entity_at(cpu.ix);
        e->animationTimer = 0x0A;
        e->animationTimerResetValue = 0x0A;
    }
    for (;;) {
        wait_frame(IRQ_SPRITES);
        if (ram8(v_inputData) & (JOY_BTN1 | JOY_BTN2)) break;
        if (ram8(v_shouldShowNuraiOrOldMan)) {
            /* The speaker keeps talking while the game is frozen. */
            cpu.hl = ram16(v_nuraiOrOldManEntityAnimationDescriptorTemporaryPointer);
            CALL_HELPER(f_handleEntityAnimation);
            ram16(v_spriteTerminatorPointer) = v_tempSprites;
            CALL_HELPER(f_updateEntitySprites);
        }
    }

    ram8(v_shouldShowNuraiOrOldMan) = 0;
    fill_bytes(v_tempSprites, 0xE0, 6); /* hide the speaker's sprites */
    cpu.bc = 0; /* as left by the LDIR: C is an input of updateEntities */
    cpu.ix = v_alex;
    uint8_t next = STATE_JANKEN_GAME | STATE_INITIALIZED;
    if (!ram8(v_hasBattleStarted))
        next = ram8(v_shopFlags) ? (STATE_SHOP | STATE_INITIALIZED) : (STATE_GAMEPLAY | STATE_INITIALIZED);
    ram8(v_gameState) = next;
    CALL_HELPER(f_updateEntities);
    wait_frame(IRQ_SPRITES);

    /* Put the name table back (the box was drawn over it in VRAM only). */
    disable_interrupts();
    CALL_HELPER(f_disableDisplay);
    cpu.hl = v_nametable;
    cpu.de = VDP_VRAM_WRITE(0x3800);
    cpu.bc = 0x0700;
    CALL_HELPER(f_copyBytesToVRAM);
    CALL_HELPER(f_enableDisplay);
    enable_interrupts();
}

/* $7DC2: main-loop handler of state 7. */
LIFTED(updateTextBoxState, 0x7DC2) {
    enter_state_handler();
    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        open_text_box();
        LIFTED_RETURN();
    }

    wait_frame(IRQ_SPRITES_AND_STATE); /* one character is printed */
    ram8(v_textBoxPrintRequest) = 1;

    uint8_t counter = ram8(v_textBoxCounter);
    if (counter & TEXT_REPEAT) {
        counter = (uint8_t)(((counter & 0x7F) - 1) | TEXT_REPEAT);
        ram8(v_textBoxCounter) = counter;
        /* The repeated character is skipped once the repetition is over. */
        if ((counter & 0x7F) == 0) load_text_segment((uint16_t)(ram16(v_currentMapOrTextNametablePointer) + 1));
    } else {
        ram8(v_textBoxCounter) = --counter;
        if (counter == 0) load_text_segment(ram16(v_currentMapOrTextNametablePointer));
    }

    uint8_t move = ram8(v_textBoxFlags) & TEXT_MOVE_MASK;
    if (move == 0) {
        wait_and_close_text_box();
        LIFTED_RETURN();
    }
    Entity *cursor = entity_at(cpu.ix);
    int8_t step = (move & TEXT_MOVE_BACKWARDS) ? -8 : 8;
    if (move & TEXT_MOVE_VERTICAL) Y_PIXEL(cursor) = (uint8_t)(Y_PIXEL(cursor) + step);
    else X_PIXEL(cursor) = (uint8_t)(X_PIXEL(cursor) + step);
    LIFTED_RETURN();
}

/* $7F22: VBlank handler of state 7: prints the current character at the
 * cursor (IX = v_textboxCursor). */
LIFTED(handleInterruptTextBoxState, 0x7F22) {
    if (ram8(v_textBoxPrintRequest) == 0) LIFTED_RETURN();
    /* Name-table entry under the cursor: RAM copy address -> VRAM address. */
    cpu.de = 0x0100;
    CALL_ROUTINE(f_getNearEntityTileAttrWithOffset);
    uint16_t entry = (uint16_t)((cpu.hl ^ 0xB000) - 1); /* $C8xx -> $78xx (write command) */
    vdp_set_address(entry);
    uint16_t text = ram16(v_currentMapOrTextNametablePointer);
    vdp_write(rd8(text));
    vdp_write(TEXT_ATTRIBUTE);
    if (!(ram8(v_textBoxCounter) & TEXT_REPEAT)) ram16(v_currentMapOrTextNametablePointer) = (uint16_t)(text + 1);
    LIFTED_RETURN();
}
