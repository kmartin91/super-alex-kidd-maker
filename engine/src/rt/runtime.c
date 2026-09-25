#include "runtime.h"
#include "shadow.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

Cpu cpu;
Machine mach;
void (*rt_frame_hook)(void);
int rt_nmi_pending;

static jmp_buf reset_point;
static jmp_buf quit_point;
uint32_t rt_r_state = 0x2545F491u;

/* Address of the `ld a,(hl)` polling v_interruptFlags in waitForInterrupt. */
#define WAIT_LOOP_PC 0x02EA

void rt_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n  AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X slot2=%d iff1=%d vdp_r0=%02X vdp_r1=%02X\n",
            cpu.af, cpu.bc, cpu.de, cpu.hl, cpu.ix, cpu.iy, cpu.sp, mach.slot[2], cpu.iff1,
            mach.vdp.reg[0], mach.vdp.reg[1]);
    fprintf(stderr, "  stack:");
    for (int i = 0; i < 12 && cpu.sp + i * 2 < 0xDFF0; i++) fprintf(stderr, " %04X", rd16((uint16_t)(cpu.sp + i * 2)));
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

void rt_stack_error(uint16_t ret_addr, uint16_t sp_expected) {
    rt_fatal("stack imbalance returning to %04X: SP=%04X expected %04X", ret_addr, cpu.sp, sp_expected);
}

const GameFnEntry *rt_lookup(uint16_t addr) {
    int lo = 0, hi = game_function_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t a = game_functions[mid].addr;
        if (a == addr) return &game_functions[mid];
        if (a < addr) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

void rt_dispatch(uint16_t addr) {
    const GameFnEntry *e = rt_lookup(addr);
    if (!e) rt_fatal("indirect jump to %04X: no routine starts there", addr);
    e->fn();
}

void rt_assert_bank(uint8_t bank, uint16_t addr) {
    if (mach.slot[2] != bank)
        rt_fatal("routine %04X needs bank %d in slot 2, found %d", addr, bank, mach.slot[2]);
}

uint8_t rt_read_r(void) {
    /* xorshift32; the game masks the value, only the low 7 bits vary on hardware. */
    rt_r_state ^= rt_r_state << 13;
    rt_r_state ^= rt_r_state >> 17;
    rt_r_state ^= rt_r_state << 5;
    return (uint8_t)((cpu.r & 0x80) | (rt_r_state & 0x7F));
}

void rt_soft_reset(void) {
    longjmp(reset_point, 1);
}

void rt_quit(void) {
    longjmp(quit_point, 1);
}

void rt_wait_vblank(uint16_t pc) {
    if (rt_frame_hook) rt_frame_hook();
    rt_shadow_frame_index++;
    /* With interrupts disabled the original spins here forever. This happens in
     * the shipped game when Alex is hit on the frame the pause map closes
     * (exitMapState runs with DI). The port delivers the interrupt instead. */
    if (!cpu.iff1 || !(mach.vdp.reg[1] & 0x20)) {
        static int warned;
        if (!warned++) fprintf(stderr, "warning: VBlank wait with interrupts disabled at %04X (the original freezes here)\n", pc);
    }

    if (rt_nmi_pending) {
        rt_nmi_pending = 0;
        cpu.iff1 = 0;
        uint16_t sp_ = cpu.sp;
        push16(pc);
        game_entry_nmi();
        if (cpu.sp != sp_) rt_stack_error(pc, sp_);
    }

    mach.vdp.status |= 0x80;
    cpu.iff1 = cpu.iff2 = 0;
    uint16_t sp_ = cpu.sp;
    push16(pc);
    game_entry_irq();
    if (cpu.sp != sp_) rt_stack_error(pc, sp_);
}

void rt_run(void) {
    if (setjmp(quit_point)) return;
    if (setjmp(reset_point)) {
        game_entry_reset();
    } else {
        game_entry_start();
    }
    rt_fatal("game code returned to the top level");
}
