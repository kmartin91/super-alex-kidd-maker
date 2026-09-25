/*
 * Hand-written ("lifted") replacements for generated routines.
 *
 *   LIFTED(copyBytesToVRAM, 0x0145) { ... }
 *
 * defines f_copyBytesToVRAM, which overrides the generated translation
 * everywhere (direct calls, tail calls and jump tables). The name must match
 * the one in src/gen/gen_funcs.h and the address its routine.
 *
 * A lifted routine keeps the original contract at its boundary:
 *  - it reads its inputs from and writes its outputs to `cpu` registers
 *    (see the "in:/out:" comment above the generated version);
 *  - it pops its return address like RET does (use LIFTED_RETURN());
 *  - memory, VDP and PSG effects must be identical.
 * Built with -DRT_SHADOW, the routine is instead registered for the shadow
 * test, which runs both versions from identical states and compares them.
 */
#ifndef GAME_LIFT_H
#define GAME_LIFT_H

#include "gen/gen_funcs.h"
#include "rt/runtime.h"

#ifdef RT_SHADOW
void rt_shadow_register(uint16_t addr, GameFn fn, const char *name);
#define LIFTED(name, addr)                                                        \
    static void lifted_##name(void);                                              \
    __attribute__((constructor)) static void register_##name(void) {              \
        rt_shadow_register(addr, lifted_##name, #name);                           \
    }                                                                             \
    static void lifted_##name(void)
#else
#define LIFTED(name, addr) void f_##name(void)
#endif

/* Pop the return address pushed by the caller's CALL, as RET does. */
#define LIFTED_RETURN() do { cpu.sp += 2; return; } while (0)

/* Call another routine (generated or lifted) from lifted code: set its input
 * registers in `cpu` first, read its outputs from `cpu` afterwards. Pushes a
 * return address on the emulated stack like CALL does; if the callee unwinds
 * past us (returns to our caller), the macro returns from the lifted routine. */
#define CALL_ROUTINE(fn) CALL(fn, 0x0000)

/* Jump to another routine that returns directly to our caller (JP target). */
#define TAIL_CALL(fn) do { fn(); return; } while (0)

/* rst $20 equivalent: call entry `index` of the jump table at ROM address
 * `table` (word table, entry 0 at `table`). Registers A, DE, HL are set as the
 * original dispatcher leaves them for the target. Returns true if the target
 * unwound past the caller, in which case the caller must return immediately:
 *     if (call_jump_table(0x2890, index)) return;                            */
static inline bool call_jump_table(uint16_t table, uint8_t index) {
    uint16_t entry = (uint16_t)(table + (uint8_t)(index * 2));
    cpu.a = rd8(entry);
    cpu.de = (uint8_t)(index * 2);
    cpu.hl = rd16(entry);
    uint16_t sp_ = cpu.sp;
    push16(0x0000);
    rt_dispatch(cpu.hl);
    if (cpu.sp < sp_) rt_stack_error(0, sp_);
    return cpu.sp > sp_;
}

#endif
