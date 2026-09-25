# Lifting guide: from generated C to readable C

The game already runs entirely from `engine/src/gen/*.c`, a mechanical translation
of every Z80 routine (`g_<name>`). "Lifting" rewrites a routine as readable,
idiomatic C that behaves **identically**. A lifted routine replaces the
generated one everywhere automatically (the generated `f_<name>` symbol is weak).

Every lifted routine is verified by the **shadow test**: during real gameplay,
each call runs the generated version and the lifted version from the same
machine snapshot, then compares everything observable. Zero mismatches is the
only acceptable result.

## 1. Anatomy of a lifted routine

```c
#include "game/lift.h"
#include "game/ram.h"
#include "game/vdp_io.h"

/* $0010 (rst $10): HL = table of words, A = index -> HL = table[A], BC = 2*A. */
LIFTED(loadAthPointer, 0x0010) {
    uint8_t offset = (uint8_t)(cpu.a * 2);
    uint16_t entry = (uint16_t)(cpu.hl + offset);
    cpu.bc = offset;
    cpu.a = rd8(entry);
    cpu.hl = rd16(entry);
    LIFTED_RETURN();
}
```

- `LIFTED(name, addr)`: `name` is the generated name without `f_` (see
  `engine/src/gen/gen_funcs.h`), `addr` the routine's address. A wrong pair is
  rejected at startup of the shadow binary.
- End every path that returns to the caller with `LIFTED_RETURN()` (it pops the
  return address like RET). A routine that ends with a jump to another routine
  uses `TAIL_CALL(f_other)` instead (no pop: the other routine returns for us).
- Look at the comment above `g_<name>` in `engine/src/gen/gen_bank*.c`:
  `/* 0145 copyBytesToVRAM  in: BC DE HL  out: BC HL A fS fZ ...  preserves: ... */`
  - **in**: registers the routine reads (its parameters).
  - **out**: registers it changes that some caller reads afterwards. The lifted
    version must leave exactly the same values there (flags too: `cpu.f`).
  - **preserves**: registers callers read afterwards that the original leaves
    intact. Never modify them.
  - Any other register is scratch; do not bother reproducing it.
  - Rule of thumb: compute with C locals, write `cpu.*` only for outputs.
- The analysis behind these lists is conservative: an output may be listed that
  no caller really needs. Reproduce it anyway (it is usually trivial, e.g. a
  loop counter left at 0, or flags after a final `dec a`).

## 2. Calling other routines

Until all routines are lifted, calls go through the original register ABI:

```c
cpu.hl = 0x8A3A;            /* inputs */
CALL_ROUTINE(f_handleEntityAnimation);
uint8_t result = cpu.a;     /* outputs */
```

- `CALL_ROUTINE(f_x)` works whether `x` is lifted or not, keeps the emulated
  stack balanced, and returns from *your* routine if the callee unwinds past it
  (a few original routines return to their caller's caller).
- `rst $20` jump tables: `if (call_jump_table(table_addr, index)) return;`
- `jp (hl)` to a routine: `rt_dispatch(addr); return;` (tail jump through a table).
- Waiting for the next frame: `cpu.a = flags; CALL_ROUTINE(f_waitForInterrupt);`
- Inside your own module, you may of course call your own static C helpers
  directly with normal C parameters. Do not call other modules' C helpers: they
  are being written in parallel. Only `f_<name>` routines are shared.

## 3. Memory, hardware and data

- RAM variables: `engine/src/game/ram.h` (generated from the reference symbols):
  `ram8(v_gameState)`, `ram16(v_horizontalScroll)`, `ram_ptr(addr)`.
  Entities: `Entity *e = entity_at(cpu.ix);` then `e->xSpeed`, `e->state`...
  Sound channels: `SoftwareChannel *ch = channel_at(cpu.ix);`
  Unnamed variables (`_RAM_C074_`): give them a meaningful local name/comment in
  your code and record the meaning in your notes file.
- ROM data (tables, level data, graphics): read with `rd8(addr)`/`rd16(addr)` at
  the same CPU addresses as the original, after the same mapper writes
  (`wr8(0xFFFF, bank)`). The mapper state is compared by the test, so do the
  same writes in the same order. **Never copy ROM data into source files.**
- VDP: `engine/src/game/vdp_io.h` (`vdp_set_address`, `vdp_write`, ...), or
  `io_out(0xBE/0xBF, v)` directly. PSG: `io_out(0x7F, v)`. The exact sequence of
  port writes matters (VDP address/latch state is compared).
- `ld a,r` (random source): `cpu.a = rt_read_r()` or a local; call it exactly as
  many times as the original.
- `di`/`ei`: `cpu.iff1 = cpu.iff2 = 0/1;` (compared by the test).

## 4. Readability requirements

The whole point is code a fangame author can modify:
- Name things after what they mean in the game (`alex_is_on_ground`,
  `enemy_speed`), not after registers. Use the Entity/SoftwareChannel fields.
- Replace magic numbers with named constants (`#define` / `enum` at the top of
  your file) when the meaning is known: entity types, states, sound ids, tile
  flags, joypad bits (`JOY_LEFT`...), VDP registers.
- Structured control flow (`if`/`for`/`while`/`switch`). `goto` only when the
  original's control flow genuinely cannot be expressed otherwise.
- A comment above each routine: what it does in game terms, its parameters and
  results (in registers until the whole call graph is lifted).
- Keep the original routine name in `LIFTED(...)` and mention its address; you
  may add a better name in the comment.
- Mark original bugs/quirks you keep for fidelity with `/* QUIRK: ... */`.

## 5. Workflow and verification

Work only in your module directory `engine/src/game/wip/<module>/` (C files and
an optional header) and your notes file `docs/notes/<module>.md`. Do not edit
anything else (other agents work in parallel): not `src/gen`, `src/rt`,
`src/game/*.h`, the Makefile, `recomp/`, or other modules. Never run
`engine/recomp/recomp.py`.

Build and test with a private build directory:

```sh
cd engine
make BUILD=build-<module> LIFTED_EXTRA="$(ls src/game/wip/<module>/*.c | tr '\n' ' ')" build-<module>/shadow
./build-<module>/shadow ../original.sms --mode idle --frames 3000
./build-<module>/shadow ../original.sms --mode play --seed 1 --frames 8000 --fail-dir /tmp/<module>-fails
./build-<module>/shadow ../original.sms --mode play --seed 7 --level 5 --lives --frames 6000
./build-<module>/shadow ../original.sms --script my_scenario.txt --frames 2000
./build-<module>/shadow ../original.sms --replay /tmp/<module>-fails/<file>.bin   # re-run one failing call
```

- The report lists every lifted routine with its number of compared calls and
  mismatches. A mismatch prints the first difference (register, RAM address,
  VRAM...) and saves the entry state for `--replay`.
- Scenario options (see `engine/tests/scenario.h`): `--mode idle|play`, `--seed`,
  `--level N` (1-17), `--lives`, `--script FILE` with lines
  `<frame> keys <U D L R 1 2|->` (button 1 = jump, 2 = punch),
  `<frame> pause`, `<frame> poke <addr> <val>`, `<frame> peek <addr> <len>`,
  `<frame> shot` (PPM screenshot into `--shots DIR`). A game starts with
  `100 keys 1` / `106 keys -`; gameplay begins around frame 600.
- Every lifted routine must end with **calls > 0 and 0 mismatches** over your
  scenarios. For routines that random play never reaches (shops, janken
  battles, items, bonus levels, bosses...), write scripts (pokes into RAM to set
  up the situation are fine) until they are exercised. Only genuinely dead code
  may stay unexercised; say so in your notes.
- Finally, check the whole game with your module against the independent
  reference emulator:
  `make BUILD=build-<module> LIFTED_EXTRA=... build-<module>/lockstep` then
  `./build-<module>/lockstep ../original.sms --relaxed --mode play --seed 3 --frames 20000`.

## 6. References

- `reference/akmw/src/`: the reference disassembly with names and comments
  (read it to understand what routines do; it is byte-identical to the ROM).
- `build/signatures.json`: in/out/preserves of every routine.
- `docs/level-format.md` (when present): level data format.
- The generated code in `engine/src/gen/` shows the exact semantics, instruction
  by instruction, with the original assembly in comments.
