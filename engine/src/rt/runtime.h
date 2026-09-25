/*
 * Control-flow runtime for the translated game code.
 *
 * Every Z80 routine becomes a C function. CALL still pushes the real return
 * address on the emulated stack so RAM stays identical to the original, and
 * the caller checks SP afterwards: a routine that pops its caller's return
 * address (return-to-grandparent) leaves SP above the expected value, which
 * makes each intermediate caller return in turn.
 */
#ifndef RT_RUNTIME_H
#define RT_RUNTIME_H

#include <stdint.h>

#include "cpu.h"

#ifdef RT_SHADOW
void rt_shadow_call(uint16_t addr, void (*generated)(void));
#endif

typedef void (*GameFn)(void);

typedef struct GameFnEntry {
    uint16_t addr;
    GameFn fn;
    const char *name;
} GameFnEntry;

/* Generated: every routine entry point, sorted by address. */
extern const GameFnEntry game_functions[];
extern const int game_function_count;
void game_entry_start(void);   /* $0000 */
void game_entry_reset(void);   /* $009F: soft reset target */
void game_entry_irq(void);     /* $0038 */
void game_entry_nmi(void);     /* $0066 */

_Noreturn void rt_fatal(const char *fmt, ...);
_Noreturn void rt_stack_error(uint16_t ret_addr, uint16_t sp_expected);
const GameFnEntry *rt_lookup(uint16_t addr);
void rt_dispatch(uint16_t addr);
_Noreturn void rt_soft_reset(void);
void rt_wait_vblank(uint16_t pc);
void rt_assert_bank(uint8_t bank, uint16_t addr);

/* Runs the game from power-on. Returns only when rt_quit() is called. */
void rt_run(void);
_Noreturn void rt_quit(void);

/* Called once per frame, just before the VBlank interrupt is delivered. */
extern void (*rt_frame_hook)(void);
extern int rt_nmi_pending;

#define CALL(fn, ret_addr)                                 \
    do {                                                   \
        uint16_t sp_ = cpu.sp;                             \
        push16(ret_addr);                                  \
        fn();                                              \
        if (cpu.sp != sp_) {                               \
            if (cpu.sp > sp_) return;                      \
            rt_stack_error(ret_addr, sp_);                 \
        }                                                  \
    } while (0)

/* JP (HL) after the routine pushed its own continuation address. */
#define CALL_INDIRECT(target, sp_expected)                 \
    do {                                                   \
        uint16_t sp_ = (sp_expected);                      \
        rt_dispatch(target);                               \
        if (cpu.sp != sp_) {                               \
            if (cpu.sp > sp_) return;                      \
            rt_stack_error(0, sp_);                        \
        }                                                  \
    } while (0)

#define RET() do { cpu.sp += 2; return; } while (0)

#endif
