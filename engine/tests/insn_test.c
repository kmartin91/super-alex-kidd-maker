/*
 * Instruction-level differential test. For every distinct instruction encoding
 * in the ROM, the translated C statement and the reference interpreter execute
 * from identical random machine states; registers, flags, RAM, VDP and PSG
 * must match afterwards. Covers code that gameplay tests never reach.
 *
 * usage: insn_test ROM [trials-per-instruction]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "insn_tests.h"
#include "rt/runtime.h"
#include "z80.h"

static Machine ref_mach;
static z80 ref;
static uint32_t rng = 12345;

static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

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

/* Pointer-ish register values: mostly RAM, sometimes ROM or the mapper registers. */
static uint16_t rnd_ptr(void) {
    switch (rnd() % 8) {
    case 0: return (uint16_t)rnd();
    case 1: return (uint16_t)(0xFFF0 + rnd() % 16);
    case 2: return (uint16_t)(rnd() % 0xC000);
    default: return (uint16_t)(0xC000 + rnd() % 0x2000);
    }
}

static void randomize(const InsnTest *t) {
    for (int i = 0; i < 0x2000; i++) mach.ram[i] = (uint8_t)rnd();
    vdp_reset(&mach.vdp);
    psg_reset(&mach.psg);
    for (int i = 0; i < 16; i++) mach.vdp.reg[i] = (uint8_t)rnd();
    mach.vdp.addr = (uint16_t)(rnd() & 0x3FFF);
    mach.vdp.code = (uint8_t)(rnd() & 3);
    mach.slot[0] = 0;
    mach.slot[1] = 1;
    mach.slot[2] = 2;
    /* The mapper registers mirror into RAM; keep them consistent like the game
     * does (the reference core writes BIT n,(HL) operands back to memory). */
    mach.ram[0x1FFD] = 0;
    mach.ram[0x1FFE] = 1;
    mach.ram[0x1FFF] = 2;
    mach.joy = (uint8_t)rnd();

    memset(&cpu, 0, sizeof(cpu));
    cpu.af = (uint16_t)rnd();
    cpu.bc = rnd_ptr();
    cpu.de = rnd_ptr();
    cpu.hl = rnd_ptr();
    cpu.ix = rnd_ptr();
    cpu.iy = rnd_ptr();
    cpu.sp = (uint16_t)(0xC010 + rnd() % 0x1FE0);
    cpu.af_ = (uint16_t)rnd();
    cpu.bc_ = (uint16_t)rnd();
    cpu.de_ = (uint16_t)rnd();
    cpu.hl_ = (uint16_t)rnd();
    cpu.i = (uint8_t)rnd();
    cpu.iff1 = cpu.iff2 = rnd() & 1;
    /* Keep repeated block instructions short. */
    const char *s = t->text;
    if (!strncmp(s, "ld", 2) || !strncmp(s, "cp", 2)) {
        if (strstr(s, "ir") || strstr(s, "dr")) {
            cpu.bc = (uint16_t)(1 + rnd() % 64);
            /* Block copies onto $FFFD would remap the bank holding the copy loop
             * (DE may also wrap downwards past $0000), which the game never does. */
            cpu.de = (uint16_t)(0xC100 + rnd() % 0x1E00);
        }
    }
    if (!strncmp(s, "otir", 4) || !strncmp(s, "inir", 4) || !strncmp(s, "otdr", 4) || !strncmp(s, "indr", 4))
        cpu.b = (uint8_t)(1 + rnd() % 64);

    ref_mach = mach;
    ref.a = cpu.a; ref_set_f(cpu.f);
    ref.b = cpu.b; ref.c = cpu.c; ref.d = cpu.d; ref.e = cpu.e; ref.h = cpu.h; ref.l = cpu.l;
    ref.ix = cpu.ix; ref.iy = cpu.iy; ref.sp = cpu.sp;
    ref.a_ = (uint8_t)(cpu.af_ >> 8); ref.f_ = (uint8_t)cpu.af_;
    ref.b_ = (uint8_t)(cpu.bc_ >> 8); ref.c_ = (uint8_t)cpu.bc_;
    ref.d_ = (uint8_t)(cpu.de_ >> 8); ref.e_ = (uint8_t)cpu.de_;
    ref.h_ = (uint8_t)(cpu.hl_ >> 8); ref.l_ = (uint8_t)cpu.hl_;
    ref.i = cpu.i;
    ref.iff1 = cpu.iff1; ref.iff2 = cpu.iff2;
    ref.iff_delay = 0;
    ref.halted = 0;
    ref.pc = t->addr;
}

static int check(const InsnTest *t, int trial) {
    char why[128] = "";
    uint8_t fmask = (t->flags & INSN_MASK_XY) ? 0xD7 : 0xFF;
#define DIFF(name, a, b) \
    if ((a) != (b) && !why[0]) snprintf(why, sizeof(why), "%s port=%04X ref=%04X", name, (unsigned)(a), (unsigned)(b))
    DIFF("A", cpu.a, ref.a);
    DIFF("F", cpu.f & fmask, ref_get_f() & fmask);
    DIFF("BC", cpu.bc, (uint16_t)(ref.b << 8 | ref.c));
    DIFF("DE", cpu.de, (uint16_t)(ref.d << 8 | ref.e));
    DIFF("HL", cpu.hl, (uint16_t)(ref.h << 8 | ref.l));
    DIFF("IX", cpu.ix, ref.ix);
    DIFF("IY", cpu.iy, ref.iy);
    DIFF("SP", cpu.sp, ref.sp);
    DIFF("AF'", cpu.af_, (uint16_t)(ref.a_ << 8 | ref.f_));
    DIFF("BC'", cpu.bc_, (uint16_t)(ref.b_ << 8 | ref.c_));
    DIFF("DE'", cpu.de_, (uint16_t)(ref.d_ << 8 | ref.e_));
    DIFF("HL'", cpu.hl_, (uint16_t)(ref.h_ << 8 | ref.l_));
    DIFF("IFF1", cpu.iff1, ref.iff1 || ref.iff_delay);
    for (int i = 0; i < 0x2000; i++) DIFF("RAM", mach.ram[i], ref_mach.ram[i]);
    for (int i = 0; i < 3; i++) DIFF("slot", mach.slot[i], ref_mach.slot[i]);
    DIFF("VDP", memcmp(&mach.vdp, &ref_mach.vdp, sizeof(Vdp)), 0);
    DIFF("PSG", memcmp(&mach.psg, &ref_mach.psg, sizeof(Psg)), 0);
#undef DIFF
    if (why[0]) {
        printf("FAIL %04X %-24s trial %d: %s\n", t->addr, t->text, trial, why);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM [trials]\n", argv[0]);
        return 2;
    }
    int trials = argc > 2 ? atoi(argv[2]) : 2000;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t rom[0x20000];
    if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) return 2;
    fclose(f);
    machine_init(&mach, rom, sizeof(rom));
    machine_init(&ref_mach, rom, sizeof(rom));
    z80_init(&ref);
    ref.read_byte = ref_read;
    ref.write_byte = ref_write;
    ref.port_in = ref_in;
    ref.port_out = ref_out;

    int failures = 0;
    for (int i = 0; i < insn_test_count; i++) {
        const InsnTest *t = &insn_tests[i];
        int failed = 0;
        for (int k = 0; k < trials && !failed; k++) {
            randomize(t);
            uint16_t sp_before = ref.sp;
            int taken = t->run();
            /* The reference executes one iteration of LDIR/OTIR/... per step. */
            do z80_step(&ref);
            while (t->kind == INSN_PLAIN && ref.pc == t->addr);
            if (t->kind == INSN_PLAIN) {
                failed = check(t, k);
            } else {
                int ref_taken = ref.pc != (uint16_t)(t->addr + t->size);
                if (t->kind == INSN_COND_ONLY) ref_taken = ref.sp != sp_before; /* RET cc / CALL cc */
                if (t->kind == INSN_COND_ONLY) {
                    /* Only the condition matters: reset side effects of the call/ret. */
                    if (taken != ref_taken) {
                        printf("FAIL %04X %-24s trial %d: condition port=%d ref=%d\n", t->addr, t->text, k, taken, ref_taken);
                        failed = 1;
                    }
                } else {
                    if (taken != ref_taken) {
                        printf("FAIL %04X %-24s trial %d: branch port=%d ref=%d\n", t->addr, t->text, k, taken, ref_taken);
                        failed = 1;
                    } else {
                        failed = check(t, k);
                    }
                }
            }
        }
        failures += failed;
    }
    printf("%s: %d/%d instruction encodings match the reference (%d trials each)\n",
           failures ? "FAILED" : "OK", insn_test_count - failures, insn_test_count, trials);
    return failures ? 1 : 0;
}
