/*
 * Differential test: runs the translated game and an independent Z80
 * interpreter executing the original ROM side by side, feeding both the same
 * input, and compares the complete machine state at every frame boundary.
 *
 * Both sides use the same interrupt model: the VBlank interrupt (and pause
 * NMI) is delivered when the CPU reaches the polling loop in waitForInterrupt.
 *
 * usage: lockstep ROM [scenario options, see scenario.h] [--cov FILE] [--relaxed]
 *
 * Scenario pokes (level select, infinite lives) are applied to both machines.
 * --cov accumulates the set of executed ROM addresses across runs and reports
 * routine coverage. --relaxed compares only observable state (RAM outside the
 * stack area, VRAM, CRAM, VDP, PSG, mapper), for builds with lifted routines
 * whose scratch registers and stack usage legitimately differ.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/runtime.h"
#include "scenario.h"
#include "z80.h"

#define WAIT_LOOP_PC 0x02EA

static Machine ref_mach;
static z80 ref;
static uint32_t ref_r_state = 0x2545F491u;
static long frame_no;
static long ref_steps_total;
static const char *cov_path;
static bool relaxed;
static uint8_t executed[0x10000];
static uint16_t last_di_pc;

static uint8_t ref_read(void *ud, uint16_t a) { return machine_read(&ref_mach, a); }
static void ref_write(void *ud, uint16_t a, uint8_t v) { machine_write(&ref_mach, a, v); }
static uint8_t ref_in(z80 *z, uint8_t port) { return machine_in(&ref_mach, port); }
static void ref_out(z80 *z, uint8_t port, uint8_t v) { machine_out(&ref_mach, port, v); }

static uint8_t ref_get_f(void) {
    return (uint8_t)(ref.cf | ref.nf << 1 | ref.pf << 2 | ref.xf << 3 | ref.hf << 4 | ref.yf << 5 |
                     ref.zf << 6 | ref.sf << 7);
}
static void ref_set_f(uint8_t v) {
    ref.cf = v & 1; ref.nf = (v >> 1) & 1; ref.pf = (v >> 2) & 1; ref.xf = (v >> 3) & 1;
    ref.hf = (v >> 4) & 1; ref.yf = (v >> 5) & 1; ref.zf = (v >> 6) & 1; ref.sf = (v >> 7) & 1;
}

static uint8_t ref_read_r(void) {
    ref_r_state ^= ref_r_state << 13;
    ref_r_state ^= ref_r_state >> 17;
    ref_r_state ^= ref_r_state << 5;
    return (uint8_t)((ref.r & 0x80) | (ref_r_state & 0x7F));
}

static void report_coverage(void);

static _Noreturn void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "MISMATCH at frame %ld: ", frame_no);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fprintf(stderr, "  port: AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X\n",
            cpu.af, cpu.bc, cpu.de, cpu.hl, cpu.ix, cpu.iy, cpu.sp);
    fprintf(stderr, "  ref : AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X PC=%04X\n",
            ref.a, ref_get_f(), ref.b, ref.c, ref.d, ref.e, ref.h, ref.l, ref.ix, ref.iy, ref.sp, ref.pc);
    exit(1);
}

/* Executes one reference instruction, substituting LD A,R with the shared random source. */
static void ref_step(void) {
    if (ref_read(NULL, ref.pc) == 0xED && ref_read(NULL, (uint16_t)(ref.pc + 1)) == 0x5F) {
        uint8_t v = ref_read_r();
        ref.a = v;
        uint8_t f = ref_get_f();
        f = (uint8_t)((f & (FLAG_C | FLAG_X | FLAG_Y)) | (v & FLAG_S) | (v ? 0 : FLAG_Z) | (ref.iff2 ? FLAG_P : 0));
        ref_set_f(f);
        ref.pc += 2;
        return;
    }
    executed[ref.pc] = 1;
    if (ref_read(NULL, ref.pc) == 0xF3) last_di_pc = ref.pc;
    z80_step(&ref);
    if (++ref_steps_total % 100000000 == 0) fprintf(stderr, "ref: %ld steps\n", ref_steps_total);
}

/* Runs the reference until it returns to `pc` with stack pointer `sp`. */
static void ref_run_until_return(uint16_t pc, uint16_t sp) {
    long n = 0;
    do {
        ref_step();
        if (++n > 5000000) fail("reference never returned from interrupt");
    } while (!(ref.pc == pc && ref.sp == sp));
}

/* Advances the reference to its next frame boundary. */
static void ref_run_to_boundary(void) {
    long n = 0;
    if (ref.pc == WAIT_LOOP_PC) ref_step();
    while (ref.pc != WAIT_LOOP_PC) {
        ref_step();
        if (++n > 50000000) fail("reference did not reach a frame boundary (PC=%04X)", ref.pc);
    }
}

static void ref_interrupt(uint16_t vector, bool nmi) {
    uint16_t pc = ref.pc, sp = ref.sp;
    if (nmi) {
        ref.iff1 = 0;
    } else {
        ref_mach.vdp.status |= 0x80;
        ref.iff1 = ref.iff2 = 0;
    }
    ref.sp -= 2;
    ref_write(NULL, ref.sp, (uint8_t)pc);
    ref_write(NULL, (uint16_t)(ref.sp + 1), (uint8_t)(pc >> 8));
    ref.pc = vector;
    ref_run_until_return(pc, sp);
}

static void compare(void) {
    if (ref.pc != WAIT_LOOP_PC) fail("reference PC %04X", ref.pc);
#define CMP(name, mine, theirs) \
    if ((mine) != (theirs)) fail("%s: port %04X ref %04X", name, (unsigned)(mine), (unsigned)(theirs))
    if (relaxed) goto memory;
    CMP("A", cpu.a, ref.a);
    CMP("F", cpu.f & 0xD7, ref_get_f() & 0xD7);
    CMP("BC", cpu.bc, (ref.b << 8) | ref.c);
    CMP("DE", cpu.de, (ref.d << 8) | ref.e);
    CMP("HL", cpu.hl, (ref.h << 8) | ref.l);
    CMP("IX", cpu.ix, ref.ix);
    CMP("IY", cpu.iy, ref.iy);
    CMP("SP", cpu.sp, ref.sp);
    CMP("A'", cpu.af_ >> 8, ref.a_);
    CMP("BC'", cpu.bc_, (ref.b_ << 8) | ref.c_);
    CMP("DE'", cpu.de_, (ref.d_ << 8) | ref.e_);
    CMP("HL'", cpu.hl_, (ref.h_ << 8) | ref.l_);
    CMP("IFF1", cpu.iff1, ref.iff1);
memory:
    for (int i = 0; i < 0x2000; i++) {
        uint8_t a = mach.ram[i], b = ref_mach.ram[i];
        if (relaxed && 0xC000 + i >= 0xDF00 && 0xC000 + i < 0xDFF0) continue;
        if (a != b) {
            /* Flag bytes pushed by PUSH AF may differ in the undocumented bits. */
            if (((a ^ b) & ~0x28) == 0 && 0xC000 + i >= 0xDE00) continue;
            fail("RAM[%04X]: port %02X ref %02X", 0xC000 + i, a, b);
        }
    }
    for (int i = 0; i < 0x4000; i++)
        if (mach.vdp.vram[i] != ref_mach.vdp.vram[i])
            fail("VRAM[%04X]: port %02X ref %02X", i, mach.vdp.vram[i], ref_mach.vdp.vram[i]);
    for (int i = 0; i < 32; i++) CMP("CRAM", mach.vdp.cram[i], ref_mach.vdp.cram[i]);
    for (int i = 0; i < 16; i++) CMP("VDP reg", mach.vdp.reg[i], ref_mach.vdp.reg[i]);
    CMP("VDP addr", mach.vdp.addr, ref_mach.vdp.addr);
    CMP("VDP code", mach.vdp.code, ref_mach.vdp.code);
    for (int i = 0; i < 3; i++) CMP("PSG tone", mach.psg.tone[i], ref_mach.psg.tone[i]);
    for (int i = 0; i < 4; i++) CMP("PSG vol", mach.psg.vol[i], ref_mach.psg.vol[i]);
    CMP("PSG noise", mach.psg.noise, ref_mach.psg.noise);
    for (int i = 0; i < 3; i++) CMP("mapper slot", mach.slot[i], ref_mach.slot[i]);
#undef CMP
}

static void poke_both(uint16_t addr, uint8_t v) {
    wr8(addr, v);
    ref_write(NULL, addr, v);
}

static void report_coverage(void) {
    if (!cov_path) return;
    FILE *f = fopen(cov_path, "rb");
    if (f) {
        uint8_t old[0x10000];
        if (fread(old, 1, sizeof(old), f) == sizeof(old))
            for (int i = 0; i < 0x10000; i++) executed[i] |= old[i];
        fclose(f);
    }
    f = fopen(cov_path, "wb");
    if (f) {
        fwrite(executed, 1, sizeof(executed), f);
        fclose(f);
    }
    int hit = 0;
    for (int i = 0; i < game_function_count; i++) hit += executed[game_functions[i].addr];
    printf("coverage: %d/%d routines entered (cumulative in %s)\n", hit, game_function_count, cov_path);
}

static void frame_hook(void) {
    ref_run_to_boundary();
    compare();
    if (!ref.iff1) {
        printf("STOP: frame %ld, the original deadlocks here (interrupts disabled by DI at %04X)\n",
               frame_no, last_di_pc);
        report_coverage();
        rt_quit();
    }
    if (frame_no % 600 == 0)
        fprintf(stderr, "frame %ld ok (state %02X)\n", frame_no, mach.ram[0x001F]);
    if (frame_no + 1 >= sc_max_frames) {
        frame_no++;
        printf("OK: %ld frames identical (mode %s, seed %u, level %d)%s\n", frame_no, sc_mode, sc_seed,
               sc_start_level, relaxed ? " [relaxed]" : "");
        report_coverage();
        rt_quit();
    }
    uint8_t joy;
    bool nmi;
    scenario_frame(frame_no, poke_both, &joy, &nmi);
    frame_no++;
    mach.joy = ref_mach.joy = joy;
    if (nmi) {
        rt_nmi_pending = 1;
        ref_interrupt(0x0066, true);
    }
    ref_interrupt(0x0038, false);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM [--frames N] [--seed S] [--mode idle|play] [--script F] [--level N] [--lives] [--shots DIR --every K] [--cov F] [--relaxed]\n", argv[0]);
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--relaxed")) relaxed = true;
        else if (!strcmp(argv[i], "--cov") && i + 1 < argc) cov_path = argv[++i];
        else if (!scenario_parse_arg(argc, argv, &i)) {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    scenario_init();
    uint32_t size;
    uint8_t *rom = scenario_load_rom(argv[1], &size);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    machine_init(&mach, rom, size);
    machine_init(&ref_mach, rom, size);

    z80_init(&ref);
    ref.read_byte = ref_read;
    ref.write_byte = ref_write;
    ref.port_in = ref_in;
    ref.port_out = ref_out;

    memset(&cpu, 0, sizeof(cpu));
    cpu.a = ref.a;
    cpu.f = ref_get_f();
    cpu.sp = ref.sp;

    rt_frame_hook = frame_hook;
    rt_run();
    return 0;
}
