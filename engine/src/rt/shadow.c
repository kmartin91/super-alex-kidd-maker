#include "shadow.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "regs.h"

/* RAM below the caller's stack pointer inside this zone is dead after a return. */
#define STACK_ZONE_LO 0xDF00

struct MachineSnapshot {
    Cpu cpu;
    Machine mach;
    uint32_t r_state;
    int nmi_pending;
    long frame_index;
};

extern uint32_t rt_r_state;

long rt_shadow_frame_index;
int rt_shadow_max_depth = 2;
const char *rt_shadow_fail_dir;

#define MAX_LIFTED 1024
static ShadowStats lifted[MAX_LIFTED];
static int lifted_count;
static int depth;
static int fail_files;

int rt_shadow_depth(void) { return depth; }
int rt_shadow_registered_count(void) { return lifted_count; }
const ShadowStats *rt_shadow_stats(int i) { return &lifted[i]; }

MachineSnapshot *rt_snapshot_new(void) { return malloc(sizeof(MachineSnapshot)); }
void rt_snapshot_free(MachineSnapshot *s) { free(s); }

void rt_snapshot_take(MachineSnapshot *s) {
    s->cpu = cpu;
    s->mach = mach;
    s->r_state = rt_r_state;
    s->nmi_pending = rt_nmi_pending;
    s->frame_index = rt_shadow_frame_index;
}

void rt_snapshot_restore(const MachineSnapshot *s) {
    cpu = s->cpu;
    mach = s->mach;
    rt_r_state = s->r_state;
    rt_nmi_pending = s->nmi_pending;
    rt_shadow_frame_index = s->frame_index;
}

static const GameSignature *signature(uint16_t addr) {
    int lo = 0, hi = game_signature_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (game_signatures[mid].addr == addr) return &game_signatures[mid];
        if (game_signatures[mid].addr < addr) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

void rt_shadow_register(uint16_t addr, GameFn fn, const char *name) {
    const GameFnEntry *e = rt_lookup(addr);
    if (!e) {
        fprintf(stderr, "LIFTED(%s, 0x%04X): no routine starts at that address\n", name, addr);
        exit(2);
    }
    /* Generated names are f_<name>; lifted names drop the prefix. */
    char expected[128];
    snprintf(expected, sizeof(expected), "f_%s", name);
    extern const char *rt_function_c_name(uint16_t addr);
    const char *cname = rt_function_c_name(addr);
    if (cname && strcmp(cname, expected) != 0) {
        fprintf(stderr, "LIFTED(%s, 0x%04X): the routine at that address is %s\n", name, addr, cname);
        exit(2);
    }
    if (lifted_count >= MAX_LIFTED) exit(2);
    ShadowStats *s = &lifted[lifted_count++];
    s->addr = addr;
    s->name = name;
    s->lifted = fn;
}

static ShadowStats *find(uint16_t addr) {
    for (int i = 0; i < lifted_count; i++)
        if (lifted[i].addr == addr) return &lifted[i];
    return NULL;
}

typedef struct RegDesc {
    uint32_t bit;
    const char *name;
    int kind; /* 0: byte field offset, 1: flag bit in F */
    size_t off;
    uint8_t flag;
} RegDesc;

#define R8(bit, name, field) {bit, name, 0, offsetof(Cpu, field), 0}
#define RF(bit, name, flag) {bit, name, 1, 0, flag}
static const RegDesc reg_desc[] = {
    R8(RB_A, "A", a), R8(RB_B, "B", b), R8(RB_C, "C", c), R8(RB_D, "D", d), R8(RB_E, "E", e),
    R8(RB_H, "H", h), R8(RB_L, "L", l), R8(RB_IXH, "IXH", ixh), R8(RB_IXL, "IXL", ixl),
    R8(RB_IYH, "IYH", iyh), R8(RB_IYL, "IYL", iyl),
    RF(RB_FS, "flag S", FLAG_S), RF(RB_FZ, "flag Z", FLAG_Z), RF(RB_FH, "flag H", FLAG_H),
    RF(RB_FP, "flag P/V", FLAG_P), RF(RB_FN, "flag N", FLAG_N), RF(RB_FC, "flag C", FLAG_C),
};

static uint8_t alt_byte(const Cpu *c, uint32_t bit) {
    switch (bit) {
    case RB_A_: return (uint8_t)(c->af_ >> 8);
    case RB_F_: return (uint8_t)c->af_;
    case RB_B_: return (uint8_t)(c->bc_ >> 8);
    case RB_C_: return (uint8_t)c->bc_;
    case RB_D_: return (uint8_t)(c->de_ >> 8);
    case RB_E_: return (uint8_t)c->de_;
    case RB_H_: return (uint8_t)(c->hl_ >> 8);
    case RB_L_: return (uint8_t)c->hl_;
    default: return 0;
    }
}

/* Returns a description of the first difference, or NULL. */
static const char *diff(const MachineSnapshot *entry, const MachineSnapshot *a, const MachineSnapshot *b,
                        uint32_t out_mask) {
    static char buf[256];
    if (a->cpu.sp != b->cpu.sp) {
        snprintf(buf, sizeof(buf), "SP generated %04X lifted %04X", a->cpu.sp, b->cpu.sp);
        return buf;
    }
    for (size_t i = 0; i < sizeof(reg_desc) / sizeof(reg_desc[0]); i++) {
        const RegDesc *r = &reg_desc[i];
        if (!(out_mask & r->bit)) continue;
        uint8_t va, vb;
        if (r->kind == 0) {
            va = *((const uint8_t *)&a->cpu + r->off);
            vb = *((const uint8_t *)&b->cpu + r->off);
        } else {
            va = a->cpu.f & r->flag;
            vb = b->cpu.f & r->flag;
        }
        if (va != vb) {
            snprintf(buf, sizeof(buf), "register %s generated %02X lifted %02X", r->name, va, vb);
            return buf;
        }
    }
    static const struct { uint32_t bit; const char *name; } alts[] = {
        {RB_A_, "A'"}, {RB_F_, "F'"}, {RB_B_, "B'"}, {RB_C_, "C'"},
        {RB_D_, "D'"}, {RB_E_, "E'"}, {RB_H_, "H'"}, {RB_L_, "L'"},
    };
    for (size_t i = 0; i < sizeof(alts) / sizeof(alts[0]); i++) {
        if (!(out_mask & alts[i].bit)) continue;
        uint8_t va = alt_byte(&a->cpu, alts[i].bit), vb = alt_byte(&b->cpu, alts[i].bit);
        if (va != vb) {
            snprintf(buf, sizeof(buf), "register %s generated %02X lifted %02X", alts[i].name, va, vb);
            return buf;
        }
    }
    if (a->cpu.iff1 != b->cpu.iff1 || a->cpu.iff2 != b->cpu.iff2) {
        snprintf(buf, sizeof(buf), "interrupt enable generated %d lifted %d", a->cpu.iff1, b->cpu.iff1);
        return buf;
    }
    uint16_t dead_hi = entry->cpu.sp;
    for (int i = 0; i < 0x2000; i++) {
        uint16_t addr = (uint16_t)(0xC000 + i);
        if (addr >= STACK_ZONE_LO && addr < dead_hi) continue;
        if (a->mach.ram[i] != b->mach.ram[i]) {
            snprintf(buf, sizeof(buf), "RAM[%04X] generated %02X lifted %02X", addr, a->mach.ram[i], b->mach.ram[i]);
            return buf;
        }
    }
    const Vdp *va = &a->mach.vdp, *vb = &b->mach.vdp;
    for (int i = 0; i < 0x4000; i++)
        if (va->vram[i] != vb->vram[i]) {
            snprintf(buf, sizeof(buf), "VRAM[%04X] generated %02X lifted %02X", i, va->vram[i], vb->vram[i]);
            return buf;
        }
    for (int i = 0; i < 32; i++)
        if (va->cram[i] != vb->cram[i]) {
            snprintf(buf, sizeof(buf), "CRAM[%02X] generated %02X lifted %02X", i, va->cram[i], vb->cram[i]);
            return buf;
        }
    for (int i = 0; i < 16; i++)
        if (va->reg[i] != vb->reg[i]) {
            snprintf(buf, sizeof(buf), "VDP register %d generated %02X lifted %02X", i, va->reg[i], vb->reg[i]);
            return buf;
        }
    if (va->addr != vb->addr || va->code != vb->code || va->latch != vb->latch || va->read_buf != vb->read_buf ||
        (va->latch && va->latch_byte != vb->latch_byte) || va->status != vb->status) {
        snprintf(buf, sizeof(buf), "VDP port state generated addr=%04X code=%d latch=%d lifted addr=%04X code=%d latch=%d",
                 va->addr, va->code, va->latch, vb->addr, vb->code, vb->latch);
        return buf;
    }
    const Psg *pa = &a->mach.psg, *pb = &b->mach.psg;
    if (memcmp(pa->tone, pb->tone, sizeof(pa->tone)) || memcmp(pa->vol, pb->vol, sizeof(pa->vol)) ||
        pa->noise != pb->noise || pa->latch != pb->latch) {
        snprintf(buf, sizeof(buf), "PSG registers differ");
        return buf;
    }
    if (memcmp(a->mach.slot, b->mach.slot, sizeof(a->mach.slot))) {
        snprintf(buf, sizeof(buf), "mapper slot 2 generated %d lifted %d", a->mach.slot[2], b->mach.slot[2]);
        return buf;
    }
    if (a->r_state != b->r_state) return "random source (LD A,R) consumed differently";
    if (a->nmi_pending != b->nmi_pending) return "pending NMI differs";
    if (a->frame_index != b->frame_index) {
        snprintf(buf, sizeof(buf), "frames waited generated %ld lifted %ld", a->frame_index - entry->frame_index,
                 b->frame_index - entry->frame_index);
        return buf;
    }
    return NULL;
}

static void save_failure(const ShadowStats *s, const MachineSnapshot *entry) {
    if (!rt_shadow_fail_dir || fail_files >= 64) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/shadow_%04X_%s_%ld.bin", rt_shadow_fail_dir, s->addr, s->name, s->mismatches);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint16_t addr = s->addr;
    fwrite(&addr, sizeof(addr), 1, f);
    fwrite(entry, sizeof(*entry), 1, f);
    fclose(f);
    fail_files++;
    fprintf(stderr, "    entry state saved to %s (replay with: shadow ROM --replay %s)\n", path, path);
}

static void run_generated(uint16_t addr, GameFn generated) {
    (void)addr;
    generated();
}

void rt_shadow_call(uint16_t addr, GameFn generated) {
    ShadowStats *s = find(addr);
    if (!s || depth >= rt_shadow_max_depth) {
        run_generated(addr, generated);
        return;
    }
    MachineSnapshot *entry = rt_snapshot_new(), *a = rt_snapshot_new(), *b = rt_snapshot_new();
    rt_snapshot_take(entry);
    depth++;
    run_generated(addr, generated);
    rt_snapshot_take(a);
    rt_snapshot_restore(entry);
    s->lifted();
    rt_snapshot_take(b);
    depth--;

    const GameSignature *sig = signature(addr);
    const char *d = diff(entry, a, b, sig ? sig->live : 0xFFFFFFFFu);
    s->calls++;
    if (d) {
        s->mismatches++;
        if (s->mismatches <= 3) {
            fprintf(stderr, "SHADOW MISMATCH %s (%04X) call #%ld: %s\n", s->name, addr, s->calls, d);
            fprintf(stderr, "    entry: AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X slot2=%d\n",
                    entry->cpu.af, entry->cpu.bc, entry->cpu.de, entry->cpu.hl, entry->cpu.ix, entry->cpu.iy,
                    entry->cpu.sp, entry->mach.slot[2]);
            save_failure(s, entry);
        }
    }
    /* Continue on the original trajectory. */
    rt_snapshot_restore(a);
    rt_snapshot_free(entry);
    rt_snapshot_free(a);
    rt_snapshot_free(b);
}

int rt_shadow_report(void) {
    int bad = 0;
    printf("%-44s %8s %10s\n", "lifted routine", "calls", "mismatches");
    for (int i = 0; i < lifted_count; i++) {
        const ShadowStats *s = &lifted[i];
        printf("%04X %-39s %8ld %10ld%s\n", s->addr, s->name, s->calls, s->mismatches,
               s->calls == 0 ? "   (not exercised)" : "");
        if (s->mismatches) bad++;
    }
    return bad;
}

int rt_shadow_replay(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 2;
    uint16_t addr;
    MachineSnapshot *entry = rt_snapshot_new();
    if (fread(&addr, sizeof(addr), 1, f) != 1 || fread(entry, sizeof(*entry), 1, f) != 1) {
        fclose(f);
        return 2;
    }
    fclose(f);
    /* The ROM pointer inside the snapshot belongs to the recording process. */
    entry->mach.rom = mach.rom;
    ShadowStats *s = find(addr);
    const GameFnEntry *e = rt_lookup(addr);
    if (!s || !e) {
        fprintf(stderr, "no lifted routine at %04X in this build\n", addr);
        return 2;
    }
    extern GameFn rt_generated_fn(uint16_t addr);
    GameFn generated = rt_generated_fn(addr);
    MachineSnapshot *a = rt_snapshot_new(), *b = rt_snapshot_new();
    rt_snapshot_restore(entry);
    depth++;
    generated();
    rt_snapshot_take(a);
    rt_snapshot_restore(entry);
    s->lifted();
    rt_snapshot_take(b);
    depth--;
    const GameSignature *sig = signature(addr);
    const char *d = diff(entry, a, b, sig ? sig->live : 0xFFFFFFFFu);
    printf("%s (%04X): %s\n", s->name, addr, d ? d : "identical");
    return d ? 1 : 0;
}
