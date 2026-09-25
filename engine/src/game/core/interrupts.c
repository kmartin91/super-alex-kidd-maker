/*
 * Jump tables, VBlank interrupt, pause button (NMI), frame wait and the
 * per-frame invincibility timer ($001B-$0126, $02E6, $264F).
 *
 * Left generated on purpose (see docs/notes/core.md): `start` ($0000) and
 * `reset` ($009F), which set SP and hold the endless main loop.
 */
#include "game/core/core.h"

#if CORE_LIFT_FRAME_ROUTINES
/* JP (HL) through entry A (a byte offset) of the word table HL. The target
 * starts with A = low byte of its address, DE = the offset, HL = its address
 * and the flags of ADD HL,DE (some targets test them); its RET returns to our
 * caller. */
static void jump_through_table(void) {
    cpu.de = cpu.a;
    uint16_t entry = alu_add16(cpu.hl, cpu.de);
    cpu.a = rd8(entry);
    cpu.hl = rd16(entry);
    rt_dispatch(cpu.hl);
}

/* $0020 (rst $20): call entry A of the jump table HL (the flags of ADD A,A
 * reach the target). A tail jump: the target returns for us. */
LIFTED(jumpToAthPointer, 0x0020) {
    alu_add(cpu.a);
    jump_through_table();
}

/* $0021: jump through the table HL at byte offset A (tail jump). */
LIFTED(jumpToPointerAtA, 0x0021) {
    jump_through_table();
}
#endif

/* Address of the `ld a,(hl)` that polls v_interruptFlags in waitForInterrupt:
 * the runtime delivers the VBlank interrupt there. */
#define WAIT_LOOP_PC 0x02EA

/* $001B (rst $18): if the game state in A is initialised (bit 7), call entry
 * (A & $0F) of the jump table HL. Used by the interrupt handler to run the
 * state's own VBlank work. */
LIFTED(jumpToAthPointerIfBit7, 0x001B) {
    if (!(cpu.a & GAME_STATE_READY)) LIFTED_RETURN();
    cpu.a &= GAME_STATE_MASK;
    TAIL_CALL(f_jumpToAthPointer);
}

/* $0038: interrupt mode 1 vector. */
LIFTED(handleInterruptEntrypoint, 0x0038) {
    TAIL_CALL(f_handleInterrupt);
}

/* $0066: pause button (NMI). Requests the pause map when the game allows it:
 * Alex alive, map not disabled, and an initialised gameplay (or map) state. */
LIFTED(handlePauseInterrupt, 0x0066) {
    bool alex_alive = entity_at(v_alex)->state != ALEX_DEAD;
    bool in_gameplay = ram8(v_gameState) >= (GAME_STATE_READY | STATE_GAMEPLAY);
    if (alex_alive && !ram8(v_disallowMap) && in_gameplay) ram8(v_shouldOpenMap) = 1;
    cpu.iff1 = cpu.iff2; /* RETN */
    LIFTED_RETURN();
}

/* $00C0: VBlank interrupt handler. Runs the per-frame services requested in
 * v_interruptFlags, then clears it to release waitForInterrupt. Every register
 * and the slot-2 bank of the interrupted code are restored on exit. */
LIFTED(handleInterrupt, 0x00C0) {
    const Cpu interrupted = cpu;
    /* The original pushes both register sets after EXX / EX AF,AF': the
     * routines below run with the two sets swapped, as they did there. */
    op_exx();
    op_ex_af();

    io_in(VDP_CONTROL); /* reading the status acknowledges the interrupt */

    /* Soft reset on the frame the reset button goes down (active low). */
    uint8_t button = io_in(PORT_JOYPAD2) & RESET_BUTTON;
    uint8_t previous = ram8(v_resetButtonState);
    ram8(v_resetButtonState) = button;
    if ((button ^ previous) & previous) rt_soft_reset();
    cpu.c = previous; /* registers as the original leaves them for the calls below */
    cpu.hl = v_resetButtonState;

    uint8_t interrupted_bank = rd8(MAPPER_SLOT2);
    uint8_t requests = ram8(v_interruptFlags);

    if (requests & IRQ_UPLOAD_SPRITES) CALL_ROUTINE(f_updateSprites);
    CALL_ROUTINE(f_requestLevelTilesUpdateIfAlexTilesChanged);
    CALL_ROUTINE(f_readInput);
    CALL_ROUTINE(f_updatePalette);
    CALL_ROUTINE(f_updateInvincibility);
    if (requests & IRQ_RUN_STATE_HANDLER) {
        cpu.a = ram8(v_gameState);
        cpu.hl = GAME_STATE_IRQ_HANDLERS;
        CALL_ROUTINE(f_jumpToAthPointerIfBit7);
    }

    wr8(MAPPER_SLOT2, SLOT2_BANK2);
    CALL_ROUTINE(f_update); /* sound engine */
    ram8(v_interruptFlags) = 0;
    wr8(MAPPER_SLOT2, interrupted_bank);

    cpu.af = interrupted.af;
    cpu.bc = interrupted.bc;
    cpu.de = interrupted.de;
    cpu.hl = interrupted.hl;
    cpu.af_ = interrupted.af_;
    cpu.bc_ = interrupted.bc_;
    cpu.de_ = interrupted.de_;
    cpu.hl_ = interrupted.hl_;
    cpu.ix = interrupted.ix;
    cpu.iy = interrupted.iy;
    cpu.iff1 = cpu.iff2 = 1; /* EI */
    LIFTED_RETURN();
}

#if CORE_LIFT_FRAME_ROUTINES
/* $02E6: A = v_interruptFlags requests; waits until the VBlank interrupt has
 * served them (it clears the byte). At least one frame always passes. */
LIFTED(waitForInterrupt, 0x02E6) {
    ram8(v_interruptFlags) = cpu.a;
    do {
        rt_wait_vblank(WAIT_LOOP_PC);
    } while (ram8(v_interruptFlags) != 0);
    cpu.hl = v_interruptFlags;
    cpu.a = 0;
    cpu.f = FLAG_Z | FLAG_P; /* as left by the final OR A */
    LIFTED_RETURN();
}
#endif

/* $264F: counts down the time of the cane of flight and of the teleport
 * powder (called every frame). When it runs out: cut the sound effect, back
 * to the normal action state, and restore the red of Alex's clothes. */
LIFTED(updateInvincibility, 0x264F) {
    uint8_t action = ram8(v_alexActionState);
    if (action != ACTION_CANE_OF_FLIGHT && action != ACTION_TELEPORT_POWDER) LIFTED_RETURN();
    uint16_t timer = ram16(v_invincibilityTimer);
    if (timer != 0) {
        ram16(v_invincibilityTimer) = (uint16_t)(timer - 1);
        LIFTED_RETURN();
    }
    ram8(v_soundControl) = SOUND_FX_CUT;
    ram8(v_alexActionState) = ACTION_NORMAL;
    vdp_set_address(VDP_CRAM_WRITE(CRAM_ALEX_CLOTHES));
    vdp_write(ALEX_CLOTHES_RED);
    LIFTED_RETURN();
}
