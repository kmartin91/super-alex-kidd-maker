/*
 * Z80 register file and instruction semantics used by the translated game code.
 *
 * Flag behaviour mirrors the reference core in third_party/z80-superzazu so that
 * translated code and the reference interpreter stay bit-identical, with one
 * known exception: BIT n,(HL) takes X/Y from the tested value instead of the
 * internal MEMPTR register (the game never observes those two bits).
 */
#ifndef RT_CPU_H
#define RT_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "machine.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "register pairs assume a little-endian host"
#endif

typedef struct Cpu {
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };
    union { struct { uint8_t ixl, ixh; }; uint16_t ix; };
    union { struct { uint8_t iyl, iyh; }; uint16_t iy; };
    uint16_t sp;
    uint16_t af_, bc_, de_, hl_;
    uint8_t i, r;
    bool iff1, iff2;
    uint8_t im;
} Cpu;

extern Cpu cpu;
extern Machine mach;

enum {
    FLAG_C = 0x01, FLAG_N = 0x02, FLAG_P = 0x04, FLAG_X = 0x08,
    FLAG_H = 0x10, FLAG_Y = 0x20, FLAG_Z = 0x40, FLAG_S = 0x80,
};

/* ---------------------------------------------------------------- memory */
static inline uint8_t rd8(uint16_t a) { return machine_read(&mach, a); }
static inline void wr8(uint16_t a, uint8_t v) { machine_write(&mach, a, v); }
static inline uint16_t rd16(uint16_t a) {
    return (uint16_t)(rd8(a) | (rd8((uint16_t)(a + 1)) << 8));
}
static inline void wr16(uint16_t a, uint16_t v) {
    wr8(a, (uint8_t)v);
    wr8((uint16_t)(a + 1), (uint8_t)(v >> 8));
}
static inline void push16(uint16_t v) {
    cpu.sp -= 2;
    wr16(cpu.sp, v);
}
static inline uint16_t pop16(void) {
    uint16_t v = rd16(cpu.sp);
    cpu.sp += 2;
    return v;
}
static inline uint8_t io_in(uint8_t port) { return machine_in(&mach, port); }
static inline void io_out(uint8_t port, uint8_t v) { machine_out(&mach, port, v); }

/* ------------------------------------------------------------------ flags */
static inline uint8_t flag_sz(uint8_t v) { return (uint8_t)((v & FLAG_S) | (v ? 0 : FLAG_Z)); }
static inline uint8_t flag_xy(uint8_t v) { return v & (FLAG_X | FLAG_Y); }
static inline uint8_t flag_p(uint8_t v) { return __builtin_parity(v) ? 0 : FLAG_P; }
static inline uint8_t flag_szxyp(uint8_t v) { return flag_sz(v) | flag_xy(v) | flag_p(v); }

/* a + b + cy with full flags (same formulation as the reference core). */
static inline uint8_t add8_flags(uint8_t a, uint8_t b, int cy) {
    int r = a + b + cy;
    int carries = r ^ a ^ b;
    uint8_t res = (uint8_t)r;
    uint8_t f = flag_sz(res) | flag_xy(res);
    if (carries & 0x10) f |= FLAG_H;
    if (((carries >> 7) ^ (carries >> 8)) & 1) f |= FLAG_P;
    if (carries & 0x100) f |= FLAG_C;
    cpu.f = f;
    return res;
}
static inline uint8_t sub8_flags(uint8_t a, uint8_t b, int cy) {
    uint8_t res = add8_flags(a, (uint8_t)~b, !cy);
    cpu.f ^= FLAG_C | FLAG_H;
    cpu.f |= FLAG_N;
    return res;
}

/* ------------------------------------------------------------ 8-bit ALU */
static inline void alu_add(uint8_t v) { cpu.a = add8_flags(cpu.a, v, 0); }
static inline void alu_adc(uint8_t v) { cpu.a = add8_flags(cpu.a, v, cpu.f & FLAG_C); }
static inline void alu_sub(uint8_t v) { cpu.a = sub8_flags(cpu.a, v, 0); }
static inline void alu_sbc(uint8_t v) { cpu.a = sub8_flags(cpu.a, v, cpu.f & FLAG_C); }
static inline void alu_and(uint8_t v) { cpu.a &= v; cpu.f = flag_szxyp(cpu.a) | FLAG_H; }
static inline void alu_xor(uint8_t v) { cpu.a ^= v; cpu.f = flag_szxyp(cpu.a); }
static inline void alu_or(uint8_t v) { cpu.a |= v; cpu.f = flag_szxyp(cpu.a); }
static inline void alu_cp(uint8_t v) {
    sub8_flags(cpu.a, v, 0);
    cpu.f = (uint8_t)((cpu.f & ~(FLAG_X | FLAG_Y)) | flag_xy(v));
}
static inline uint8_t alu_inc(uint8_t v) {
    uint8_t c = cpu.f & FLAG_C;
    uint8_t r = add8_flags(v, 1, 0);
    cpu.f = (uint8_t)((cpu.f & ~FLAG_C) | c);
    return r;
}
static inline uint8_t alu_dec(uint8_t v) {
    uint8_t c = cpu.f & FLAG_C;
    uint8_t r = sub8_flags(v, 1, 0);
    cpu.f = (uint8_t)((cpu.f & ~FLAG_C) | c);
    return r;
}

/* ----------------------------------------------------------- 16-bit ALU */
static inline uint16_t add16_flags(uint16_t a, uint16_t b, int cy) {
    uint8_t lo = add8_flags((uint8_t)a, (uint8_t)b, cy);
    uint8_t hi = add8_flags((uint8_t)(a >> 8), (uint8_t)(b >> 8), cpu.f & FLAG_C);
    uint16_t r = (uint16_t)((hi << 8) | lo);
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (r ? 0 : FLAG_Z));
    return r;
}
static inline uint16_t sub16_flags(uint16_t a, uint16_t b, int cy) {
    uint8_t lo = sub8_flags((uint8_t)a, (uint8_t)b, cy);
    uint8_t hi = sub8_flags((uint8_t)(a >> 8), (uint8_t)(b >> 8), cpu.f & FLAG_C);
    uint16_t r = (uint16_t)((hi << 8) | lo);
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (r ? 0 : FLAG_Z));
    return r;
}
/* ADD HL/IX/IY,rr: S, Z and P/V are preserved. */
static inline uint16_t alu_add16(uint16_t a, uint16_t b) {
    uint8_t keep = cpu.f & (FLAG_S | FLAG_Z | FLAG_P);
    uint16_t r = add16_flags(a, b, 0);
    cpu.f = (uint8_t)((cpu.f & ~(FLAG_S | FLAG_Z | FLAG_P)) | keep);
    return r;
}
static inline uint16_t alu_adc16(uint16_t a, uint16_t b) {
    uint16_t r = add16_flags(a, b, cpu.f & FLAG_C);
    cpu.f = (uint8_t)((cpu.f & ~(FLAG_S | FLAG_Z)) | ((r >> 8) & FLAG_S) | (r ? 0 : FLAG_Z));
    return r;
}
static inline uint16_t alu_sbc16(uint16_t a, uint16_t b) {
    uint16_t r = sub16_flags(a, b, cpu.f & FLAG_C);
    cpu.f = (uint8_t)((cpu.f & ~(FLAG_S | FLAG_Z)) | ((r >> 8) & FLAG_S) | (r ? 0 : FLAG_Z));
    return r;
}

/* ------------------------------------------------- rotates on A (no S/Z/P) */
static inline void op_rlca(void) {
    uint8_t c = cpu.a >> 7;
    cpu.a = (uint8_t)((cpu.a << 1) | c);
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | flag_xy(cpu.a) | c);
}
static inline void op_rrca(void) {
    uint8_t c = cpu.a & 1;
    cpu.a = (uint8_t)((cpu.a >> 1) | (c << 7));
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | flag_xy(cpu.a) | c);
}
static inline void op_rla(void) {
    uint8_t c = cpu.a >> 7;
    cpu.a = (uint8_t)((cpu.a << 1) | (cpu.f & FLAG_C));
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | flag_xy(cpu.a) | c);
}
static inline void op_rra(void) {
    uint8_t c = cpu.a & 1;
    cpu.a = (uint8_t)((cpu.a >> 1) | ((cpu.f & FLAG_C) << 7));
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | flag_xy(cpu.a) | c);
}

/* ----------------------------------------------------- CB-prefixed shifts */
static inline uint8_t cb_result(uint8_t v, uint8_t c) {
    cpu.f = flag_szxyp(v) | c;
    return v;
}
static inline uint8_t alu_rlc(uint8_t v) { return cb_result((uint8_t)((v << 1) | (v >> 7)), v >> 7); }
static inline uint8_t alu_rrc(uint8_t v) { return cb_result((uint8_t)((v >> 1) | (v << 7)), v & 1); }
static inline uint8_t alu_rl(uint8_t v) { return cb_result((uint8_t)((v << 1) | (cpu.f & FLAG_C)), v >> 7); }
static inline uint8_t alu_rr(uint8_t v) { return cb_result((uint8_t)((v >> 1) | ((cpu.f & FLAG_C) << 7)), v & 1); }
static inline uint8_t alu_sla(uint8_t v) { return cb_result((uint8_t)(v << 1), v >> 7); }
static inline uint8_t alu_sra(uint8_t v) { return cb_result((uint8_t)((v >> 1) | (v & 0x80)), v & 1); }
static inline uint8_t alu_sll(uint8_t v) { return cb_result((uint8_t)((v << 1) | 1), v >> 7); }
static inline uint8_t alu_srl(uint8_t v) { return cb_result((uint8_t)(v >> 1), v & 1); }

/* BIT n,v; `xy` supplies the undocumented X/Y bits. */
static inline void alu_bit(int n, uint8_t v, uint8_t xy) {
    uint8_t r = v & (uint8_t)(1 << n);
    cpu.f = (uint8_t)((cpu.f & FLAG_C) | FLAG_H | (r & FLAG_S) | (r ? 0 : FLAG_Z | FLAG_P) | flag_xy(xy));
}

/* ------------------------------------------------------------------ misc */
static inline void op_cpl(void) {
    cpu.a = (uint8_t)~cpu.a;
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P | FLAG_C)) | FLAG_H | FLAG_N | flag_xy(cpu.a));
}
static inline void op_scf(void) {
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | FLAG_C | flag_xy(cpu.a));
}
static inline void op_ccf(void) {
    uint8_t c = cpu.f & FLAG_C;
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_P)) | (c ? FLAG_H : FLAG_C) | flag_xy(cpu.a));
}
static inline void op_neg(void) { cpu.a = sub8_flags(0, cpu.a, 0); }
static inline void op_daa(void) {
    uint8_t a = cpu.a, f = cpu.f, corr = 0, c = f & FLAG_C;
    if ((a & 0x0F) > 9 || (f & FLAG_H)) corr |= 0x06;
    if (a > 0x99 || c) { corr |= 0x60; c = FLAG_C; }
    uint8_t h;
    if (f & FLAG_N) {
        h = ((f & FLAG_H) && (a & 0x0F) < 6) ? FLAG_H : 0;
        a = (uint8_t)(a - corr);
    } else {
        h = (a & 0x0F) > 9 ? FLAG_H : 0;
        a = (uint8_t)(a + corr);
    }
    cpu.a = a;
    cpu.f = flag_szxyp(a) | h | (f & FLAG_N) | c;
}
static inline void op_rrd(void) {
    uint8_t v = rd8(cpu.hl), a = cpu.a;
    cpu.a = (uint8_t)((a & 0xF0) | (v & 0x0F));
    wr8(cpu.hl, (uint8_t)((v >> 4) | (a << 4)));
    cpu.f = (uint8_t)((cpu.f & FLAG_C) | flag_szxyp(cpu.a));
}
static inline void op_rld(void) {
    uint8_t v = rd8(cpu.hl), a = cpu.a;
    cpu.a = (uint8_t)((a & 0xF0) | (v >> 4));
    wr8(cpu.hl, (uint8_t)((v << 4) | (a & 0x0F)));
    cpu.f = (uint8_t)((cpu.f & FLAG_C) | flag_szxyp(cpu.a));
}
static inline void op_exx(void) {
    uint16_t t;
    t = cpu.bc; cpu.bc = cpu.bc_; cpu.bc_ = t;
    t = cpu.de; cpu.de = cpu.de_; cpu.de_ = t;
    t = cpu.hl; cpu.hl = cpu.hl_; cpu.hl_ = t;
}
static inline void op_ex_af(void) {
    uint16_t t = cpu.af; cpu.af = cpu.af_; cpu.af_ = t;
}
/* IN r,(C). Like the reference core, X/Y are left untouched. */
static inline uint8_t alu_in(uint8_t v) {
    cpu.f = (uint8_t)((cpu.f & (FLAG_C | FLAG_X | FLAG_Y)) | flag_sz(v) | flag_p(v));
    return v;
}

/* LD A,I / LD A,R. R is not cycle-accurate: the game only reads it as a
 * random source, so reads come from a deterministic generator instead. */
uint8_t rt_read_r(void);
static inline void op_ld_a_ir(uint8_t v) {
    cpu.a = v;
    cpu.f = (uint8_t)((cpu.f & (FLAG_C | FLAG_X | FLAG_Y)) | flag_sz(v) | (cpu.iff2 ? FLAG_P : 0));
}

/* ------------------------------------------------------ block transfers */
static inline void op_ldi(void) {
    uint8_t v = rd8(cpu.hl);
    wr8(cpu.de, v);
    cpu.hl++; cpu.de++; cpu.bc--;
    uint8_t n = (uint8_t)(v + cpu.a);
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_C)) | (n & FLAG_X) | ((n << 4) & FLAG_Y) |
                      (cpu.bc ? FLAG_P : 0));
}
static inline void op_ldd(void) {
    op_ldi();
    cpu.hl -= 2; cpu.de -= 2;
}
static inline void op_ldir(void) { do op_ldi(); while (cpu.bc); }
static inline void op_lddr(void) { do op_ldd(); while (cpu.bc); }
static inline void op_cpi(void) {
    uint8_t c = cpu.f & FLAG_C;
    uint8_t r = sub8_flags(cpu.a, rd8(cpu.hl), 0);
    cpu.hl++; cpu.bc--;
    uint8_t n = (uint8_t)(r - ((cpu.f & FLAG_H) ? 1 : 0));
    cpu.f = (uint8_t)((cpu.f & (FLAG_S | FLAG_Z | FLAG_H | FLAG_N)) | (n & FLAG_X) | ((n << 4) & FLAG_Y) |
                      (cpu.bc ? FLAG_P : 0) | c);
}
static inline void op_cpd(void) { op_cpi(); cpu.hl -= 2; }
static inline void op_cpir(void) { do op_cpi(); while (cpu.bc && !(cpu.f & FLAG_Z)); }
static inline void op_cpdr(void) { do op_cpd(); while (cpu.bc && !(cpu.f & FLAG_Z)); }
static inline void op_ini(void) {
    wr8(cpu.hl, io_in(cpu.c));
    cpu.hl++; cpu.b--;
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (cpu.b ? 0 : FLAG_Z) | FLAG_N);
}
static inline void op_ind(void) { op_ini(); cpu.hl -= 2; }
static inline void op_inir(void) { do op_ini(); while (cpu.b); }
static inline void op_indr(void) { do op_ind(); while (cpu.b); }
static inline void op_outi(void) {
    io_out(cpu.c, rd8(cpu.hl));
    cpu.hl++; cpu.b--;
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (cpu.b ? 0 : FLAG_Z) | FLAG_N);
}
static inline void op_outd(void) { op_outi(); cpu.hl -= 2; }
static inline void op_otir(void) { do op_outi(); while (cpu.b); }
static inline void op_otdr(void) { do op_outd(); while (cpu.b); }

#endif
