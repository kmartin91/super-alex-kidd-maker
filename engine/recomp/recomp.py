"""Static recompiler: translates every Z80 routine of the ROM into a C function.

Usage: python3 engine/recomp/recomp.py   (writes engine/src/gen/*)

The output is mechanical but exact; it is the baseline that hand-written,
readable C replaces routine by routine (see engine/src/game/).
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze, z80, liveness

ROOT = analyze.ROOT
OUT = os.path.join(ROOT, "engine", "src", "gen")

WAIT_POINTS = {0x02EA}           # waitForInterrupt@loop: frame boundary
SOFT_RESET = {(0x00C0, 0x009F)}  # handleInterrupt -> reset (console reset button)

R8 = {n: "cpu." + n for n in ("a", "b", "c", "d", "e", "h", "l", "ixh", "ixl", "iyh", "iyl")}
R16 = {n: "cpu." + n for n in ("af", "bc", "de", "hl", "sp", "ix", "iy")}
COND = {
    "nz": "!(cpu.f & FLAG_Z)", "z": "(cpu.f & FLAG_Z)",
    "nc": "!(cpu.f & FLAG_C)", "c": "(cpu.f & FLAG_C)",
    "po": "!(cpu.f & FLAG_P)", "pe": "(cpu.f & FLAG_P)",
    "p": "!(cpu.f & FLAG_S)", "m": "(cpu.f & FLAG_S)",
}
ALU8 = {"add": "alu_add", "adc": "alu_adc", "sub": "alu_sub", "sbc": "alu_sbc",
        "and": "alu_and", "xor": "alu_xor", "or": "alu_or", "cp": "alu_cp"}
SIMPLE = {"rlca": "op_rlca();", "rrca": "op_rrca();", "rla": "op_rla();", "rra": "op_rra();",
          "daa": "op_daa();", "cpl": "op_cpl();", "neg": "op_neg();", "scf": "op_scf();",
          "ccf": "op_ccf();", "rrd": "op_rrd();", "rld": "op_rld();", "exx": "op_exx();",
          "di": "cpu.iff1 = cpu.iff2 = 0;", "ei": "cpu.iff1 = cpu.iff2 = 1;", "nop": ";"}
for _op in ("ldi", "ldd", "ldir", "lddr", "cpi", "cpd", "cpir", "cpdr",
            "ini", "ind", "inir", "indr", "outi", "outd", "otir", "otdr"):
    SIMPLE[_op] = "op_%s();" % _op
SHIFTS = ("rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl")


class TranslateError(Exception):
    pass


def h2(v):
    return "0x%02X" % v


def h4(v):
    return "0x%04X" % v


def ea(arg):
    k = arg[0]
    if k == "ind":
        return R16[arg[1]]
    if k == "idx":
        d = arg[2]
        return "(uint16_t)(%s %s %d)" % (R16[arg[1]], "+" if d >= 0 else "-", abs(d))
    if k == "mem":
        return h4(arg[1])
    raise TranslateError("not a memory operand: %r" % (arg,))


def is_mem(arg):
    return arg[0] in ("ind", "idx", "mem")


def get8(arg):
    k = arg[0]
    if k == "r":
        return R8[arg[1]]
    if k == "n":
        return h2(arg[1])
    return "rd8(%s)" % ea(arg)


def set8(arg, expr):
    if arg[0] == "r":
        return "%s = %s;" % (R8[arg[1]], expr)
    return "wr8(%s, %s);" % (ea(arg), expr)


def rmw8(arg, fn, extra=None):
    """Read-modify-write of an 8-bit operand; `fn` maps a C expression to a new value."""
    if arg[0] == "r":
        return "%s = %s;" % (R8[arg[1]], fn(R8[arg[1]]))
    s = "{ uint16_t ea = %s; uint8_t v = %s; wr8(ea, v);" % (ea(arg), fn("rd8(ea)"))
    if extra:
        s += " %s = v;" % R8[extra[1]]
    return s + " }"


def translate_plain(ins):
    """Statements for a non-control-flow instruction."""
    op, args = ins.op, ins.args
    if op in SIMPLE and not args:
        return SIMPLE[op]
    if op == "ld":
        dst, src = args
        if dst == ("r", "i"):
            return "cpu.i = cpu.a;"
        if dst == ("r", "r"):
            return "cpu.r = cpu.a;"
        if src == ("r", "i"):
            return "op_ld_a_ir(cpu.i);"
        if src == ("r", "r"):
            return "op_ld_a_ir(rt_read_r());"
        if dst[0] == "rr" or src[0] == "rr" or src[0] == "nn":
            if dst[0] == "rr" and src[0] == "nn":
                return "%s = %s;" % (R16[dst[1]], h4(src[1]))
            if dst[0] == "rr" and src[0] == "mem":
                return "%s = rd16(%s);" % (R16[dst[1]], h4(src[1]))
            if dst[0] == "mem" and src[0] == "rr":
                return "wr16(%s, %s);" % (h4(dst[1]), R16[src[1]])
            if dst[0] == "rr" and src[0] == "rr":
                return "%s = %s;" % (R16[dst[1]], R16[src[1]])
            raise TranslateError("ld form")
        return set8(dst, get8(src))
    if op == "push":
        return "push16(%s);" % R16[args[0][1]]
    if op == "pop":
        return "%s = pop16();" % R16[args[0][1]]
    if op == "ex":
        a, b = args
        if a == ("rr", "af"):
            return "op_ex_af();"
        if a == ("rr", "de"):
            return "{ uint16_t t = cpu.de; cpu.de = cpu.hl; cpu.hl = t; }"
        if a == ("ind", "sp"):
            r = R16[b[1]]
            return "{ uint16_t t = rd16(cpu.sp); wr16(cpu.sp, %s); %s = t; }" % (r, r)
        raise TranslateError("ex form")
    if op in ALU8:
        if args[0][0] == "rr":
            fn = {"add": "alu_add16", "adc": "alu_adc16", "sbc": "alu_sbc16"}[op]
            d = R16[args[0][1]]
            return "%s = %s(%s, %s);" % (d, fn, d, R16[args[1][1]])
        return "%s(%s);" % (ALU8[op], get8(args[1]))
    if op in ("inc", "dec"):
        a = args[0]
        if a[0] == "rr":
            return "%s%s;" % (R16[a[1]], "++" if op == "inc" else "--")
        fn = "alu_inc" if op == "inc" else "alu_dec"
        return rmw8(a, lambda x: "%s(%s)" % (fn, x))
    if op in SHIFTS:
        extra = args[1] if len(args) > 1 else None
        return rmw8(args[0], lambda x: "alu_%s(%s)" % (op, x), extra)
    if op == "bit":
        n, a = args[0][1], args[1]
        if a[0] == "r":
            return "alu_bit(%d, %s, %s);" % (n, R8[a[1]], R8[a[1]])
        if a[0] == "idx":
            return "{ uint16_t ea = %s; alu_bit(%d, rd8(ea), (uint8_t)(ea >> 8)); }" % (ea(a), n)
        return "{ uint8_t v = rd8(%s); alu_bit(%d, v, v); }" % (ea(a), n)
    if op in ("res", "set"):
        n, a = args[0][1], args[1]
        extra = args[2] if len(args) > 2 else None
        if op == "res":
            return rmw8(a, lambda x: "(uint8_t)(%s & 0x%02X)" % (x, 0xFF ^ (1 << n)), extra)
        return rmw8(a, lambda x: "(uint8_t)(%s | 0x%02X)" % (x, 1 << n), extra)
    if op == "out":
        port, v = args
        val = get8(v)
        if port[0] == "port":
            return "io_out(%s, %s);" % (h2(port[1]), val)
        return "io_out(cpu.c, %s);" % val
    if op == "in":
        dst, port = args
        if port[0] == "port":
            return "cpu.a = io_in(%s);" % h2(port[1])
        if dst == ("r", "f"):
            return "alu_in(io_in(cpu.c));"
        return "%s = alu_in(io_in(cpu.c));" % R8[dst[1]]
    if op == "im":
        return "cpu.im = %d;" % args[0][1]
    if op == "halt":
        return 'rt_fatal("HALT at %04X");' % ins.addr
    raise TranslateError("unhandled instruction %s" % ins.text())


def reg_list(mask):
    """Human-readable register list, pairing halves (B C -> BC)."""
    regs = liveness.names(mask)
    out = []
    pairs = [("B", "C", "BC"), ("D", "E", "DE"), ("H", "L", "HL"), ("IXH", "IXL", "IX"), ("IYH", "IYL", "IY"),
             ("B'", "C'", "BC'"), ("D'", "E'", "DE'"), ("H'", "L'", "HL'")]
    used = set()
    for hi, lo, name in pairs:
        if hi in regs and lo in regs:
            out.append(name)
            used |= {hi, lo}
    for r in regs:
        if r in used:
            continue
        out.append(r)
    return " ".join(out) or "-"


class FunctionEmitter:
    def __init__(self, prog, func, fnames, sig=None):
        self.p = prog
        self.f = func
        self.fnames = fnames
        self.sig = sig
        self.order = sorted(func.body)
        self.labels = set()
        self.lines = []

    def comment_name(self, v):
        return self.p.ram_names.get(v) or self.p.name_at(v)

    def jump_to(self, target):
        """Statement transferring control to `target` (goto or tail call)."""
        if target in self.f.body:
            self.labels.add(target)
            return "goto L_%04X;" % target
        if (self.f.entry, target) in SOFT_RESET:
            return "rt_soft_reset();"
        if target in self.fnames:
            return "{ %s(); return; }" % self.fnames[target]
        raise TranslateError("jump from %s to %04X: not in body and not an entry" % (self.f.name, target))

    def flow_after(self, ins, idx):
        """Fallthrough to ins.next unless it is the next emitted instruction."""
        nxt = self.order[idx + 1] if idx + 1 < len(self.order) else None
        if ins.next == nxt:
            return None
        return self.jump_to(ins.next)

    def emit(self):
        p, f = self.p, self.f
        body_lines = []
        for idx, a in enumerate(self.order):
            ins = p.instrs[a]
            stmts = []
            if a in WAIT_POINTS:
                stmts.append("rt_wait_vblank(0x%04X);" % a)
            fl = ins.flow
            if fl == z80.FLOW_NEXT or fl == z80.FLOW_HALT:
                stmts.append(translate_plain(ins))
                if fl == z80.FLOW_NEXT:
                    j = self.flow_after(ins, idx)
                    if j:
                        stmts.append(j)
            elif fl == z80.FLOW_JP:
                stmts.append(self.jump_to(ins.target))
            elif fl == z80.FLOW_JPC:
                if ins.op == "djnz":
                    stmts.append("if (--cpu.b) %s" % self.jump_to(ins.target))
                else:
                    stmts.append("if (%s) %s" % (COND[ins.args[0][1]], self.jump_to(ins.target)))
                j = self.flow_after(ins, idx)
                if j:
                    stmts.append(j)
            elif fl in (z80.FLOW_CALL, z80.FLOW_CALLC):
                callee = self.fnames[ins.target]
                call = "CALL(%s, 0x%04X);" % (callee, ins.next)
                if fl == z80.FLOW_CALLC:
                    call = "if (%s) %s" % (COND[ins.args[0][1]], call)
                stmts.append(call)
                j = self.flow_after(ins, idx)
                if j:
                    stmts.append(j)
            elif fl == z80.FLOW_RET:
                if ins.op in ("retn", "reti"):
                    stmts.append("cpu.iff1 = cpu.iff2;")
                stmts.append("RET();")
            elif fl == z80.FLOW_RETC:
                stmts.append("if (%s) RET();" % COND[ins.args[0][1]])
                j = self.flow_after(ins, idx)
                if j:
                    stmts.append(j)
            elif fl == z80.FLOW_JPIND:
                target = R16[ins.args[0][1]]
                if a in p.synthetic_calls:
                    cont = p.synthetic_calls[a]
                    stmts.append("CALL_INDIRECT(%s, (uint16_t)(cpu.sp + 2));" % target)
                    stmts.append(self.jump_to(cont))
                else:
                    stmts.append("rt_dispatch(%s); return;" % target)
            else:
                raise TranslateError("flow %s" % fl)
            body_lines.append((a, ins, stmts))

        out = []
        if self.sig:
            use, outs, live = self.sig
            kept = live & ~outs
            out.append("/* %04X %s  in: %s  out: %s%s */" % (f.entry, f.name, reg_list(use), reg_list(outs),
                                                         ("  preserves: " + reg_list(kept)) if kept else ""))
        else:
            out.append("/* %04X */" % f.entry)
        out.append("void g_%s(void) {" % self.fnames[f.entry][2:])
        if f.entry >= 0x8000:
            out.append("    rt_assert_bank(2, 0x%04X);" % f.entry)
        if self.order[0] != f.entry:
            self.labels.add(f.entry)
            out.append("    goto L_%04X;" % f.entry)
        for a, ins, stmts in body_lines:
            if a in self.labels:
                nm = p.name_at(a)
                out.append("L_%04X:%s" % (a, ("  /* %s */" % nm) if nm and a != f.entry else ""))
            asm = ins.text(self.comment_name)
            first = True
            for s in stmts:
                if first:
                    out.append("    %-60s // %04X  %s" % (s, a, asm))
                    first = False
                else:
                    out.append("    %s" % s)
        out.append("}")
        fn, gn = self.fnames[f.entry], "g_" + self.fnames[f.entry][2:]
        out.append("#ifdef RT_SHADOW")
        out.append("void %s(void) { rt_shadow_call(0x%04X, %s); }" % (fn, f.entry, gn))
        out.append("#else")
        out.append("__attribute__((weak)) void %s(void) { %s(); }" % (fn, gn))
        out.append("#endif")
        return out


def write_instruction_tests(prog):
    """One test per distinct instruction encoding: the translated statement and
    the reference interpreter must agree on random machine states."""
    seen = {}
    for a in sorted(prog.instrs):
        ins = prog.instrs[a]
        key = (ins.raw, analyze.cpu_bank(a) == 2)
        if ins.raw not in seen:
            seen[ins.raw] = ins
    lines = [
        "/* Generated by engine/recomp/recomp.py. Do not edit. */",
        '#include "rt/runtime.h"',
        '#include "insn_tests.h"',
        "",
    ]
    table = []
    n = 0
    skipped = 0
    for raw, ins in sorted(seen.items(), key=lambda kv: kv[1].addr):
        fl = ins.flow
        flags = []
        if ins.op == "ld" and ("r", "r") in ins.args:
            skipped += 1
            continue
        if ins.op == "bit" and ins.args[1][0] == "ind":
            flags.append("INSN_MASK_XY")
        if fl in (z80.FLOW_NEXT,):
            body = translate_plain(ins)
            kind = "INSN_PLAIN"
        elif fl == z80.FLOW_JPC:
            if ins.op == "djnz":
                body = "return --cpu.b != 0;"
            else:
                body = "return %s ? 1 : 0;" % COND[ins.args[0][1]]
            kind = "INSN_BRANCH"
        elif fl in (z80.FLOW_RETC, z80.FLOW_CALLC):
            body = "return %s ? 1 : 0;" % COND[ins.args[0][1]]
            kind = "INSN_COND_ONLY"
        else:
            skipped += 1
            continue
        if kind == "INSN_PLAIN":
            lines.append("static int t%d(void) { %s return 0; } /* %04X %s */" % (n, body, ins.addr, ins.text()))
        else:
            lines.append("static int t%d(void) { %s } /* %04X %s */" % (n, body, ins.addr, ins.text()))
        table.append('    {0x%04X, %d, %s, %s, t%d, "%s"},' % (
            ins.addr, ins.size, kind, "|".join(flags) or "0", n, ins.text().replace('"', "'")))
        n += 1
    lines += ["", "const InsnTest insn_tests[] = {"] + table + ["};", "const int insn_test_count = %d;" % n, ""]
    open(os.path.join(ROOT, "engine", "tests", "gen_insn_tests.c"), "w").write("\n".join(lines))
    print("instruction tests: %d distinct encodings (%d control-flow/special skipped)" % (n, skipped))


def main():
    prog, an = liveness.build()
    funcs = prog.functions
    sigs = {e: (liveness.report(an.use[e]), liveness.report(an.outputs(e)), liveness.report(an.liveout[e])) for e in funcs}
    fnames = {}
    used = set()
    for e in sorted(funcs):
        n = "f_" + analyze.c_ident(funcs[e].name)
        if n in used:
            n = "%s_%04X" % (n, e)
        used.add(n)
        fnames[e] = n

    os.makedirs(OUT, exist_ok=True)
    header = [
        "/* Generated by engine/recomp/recomp.py from the original ROM. Do not edit. */",
        "#ifndef GEN_FUNCS_H",
        "#define GEN_FUNCS_H",
        "",
    ]
    header.append("/* f_*: active routine (hand-written in src/game/ when it exists, else the")
    header.append(" * generated one); g_*: generated translation, always available for testing. */")
    for e in sorted(funcs):
        header.append("void %s(void); void g_%s(void); /* %04X */" % (fnames[e], fnames[e][2:], e))
    header += ["", "#endif", ""]
    open(os.path.join(OUT, "gen_funcs.h"), "w").write("\n".join(header))

    per_bank = {0: [], 1: [], 2: []}
    errors = 0
    for e in sorted(funcs):
        try:
            per_bank[analyze.cpu_bank(e)].extend(FunctionEmitter(prog, funcs[e], fnames, sigs[e]).emit() + [""])
        except TranslateError as ex:
            errors += 1
            print("ERROR in %s (%04X): %s" % (funcs[e].name, e, ex))
    for bank, lines in per_bank.items():
        src = [
            "/* Generated by engine/recomp/recomp.py from the original ROM (bank %d). Do not edit. */" % bank,
            '#include "rt/runtime.h"',
            '#include "gen/gen_funcs.h"',
            "",
            "#pragma GCC diagnostic ignored \"-Wunused-label\"",
            "",
        ] + lines
        open(os.path.join(OUT, "gen_bank%d.c" % bank), "w").write("\n".join(src))

    table = [
        "/* Generated by engine/recomp/recomp.py. Do not edit. */",
        '#include "rt/runtime.h"',
        '#include "gen/gen_funcs.h"',
        "",
        "const GameFnEntry game_functions[] = {",
    ]
    for e in sorted(funcs):
        table.append('    {0x%04X, %s, "%s"},' % (e, fnames[e], funcs[e].name))
    table += [
        "};",
        "const int game_function_count = %d;" % len(funcs),
        "",
        "/* Generated translations by address (for the shadow test). */",
        "static const struct { uint16_t addr; GameFn gen; const char *cname; } generated[] = {",
    ]
    for e in sorted(funcs):
        table.append('    {0x%04X, g_%s, "%s"},' % (e, fnames[e][2:], fnames[e]))
    table += [
        "};",
        "static int generated_index(uint16_t addr) {",
        "    int lo = 0, hi = (int)(sizeof(generated) / sizeof(generated[0])) - 1;",
        "    while (lo <= hi) {",
        "        int mid = (lo + hi) / 2;",
        "        if (generated[mid].addr == addr) return mid;",
        "        if (generated[mid].addr < addr) lo = mid + 1; else hi = mid - 1;",
        "    }",
        "    return -1;",
        "}",
        "GameFn rt_generated_fn(uint16_t addr) { int i = generated_index(addr); return i < 0 ? 0 : generated[i].gen; }",
        "const char *rt_function_c_name(uint16_t addr) { int i = generated_index(addr); return i < 0 ? 0 : generated[i].cname; }",
        "",
        "void game_entry_start(void) { %s(); }" % fnames[0x0000],
        "void game_entry_reset(void) { %s(); }" % fnames[0x009F],
        "void game_entry_irq(void) { %s(); }" % fnames[0x0038],
        "void game_entry_nmi(void) { %s(); }" % fnames[0x0066],
        "",
    ]
    open(os.path.join(OUT, "gen_dispatch.c"), "w").write("\n".join(table))
    sig = [
        "/* Generated by engine/recomp/recomp.py. Do not edit.",
        " * Register masks use the RB_* bit order of rt/regs.h: {addr, in, out, live}. */",
        '#include "rt/regs.h"',
        "",
        "const GameSignature game_signatures[] = {",
    ]
    for e in sorted(funcs):
        sig.append("    {0x%04X, 0x%08X, 0x%08X, 0x%08X}, /* %s */" % (e, sigs[e][0], sigs[e][1], sigs[e][2], funcs[e].name))
    sig += ["};", "const int game_signature_count = %d;" % len(funcs), ""]
    open(os.path.join(OUT, "gen_signatures.c"), "w").write("\n".join(sig))
    write_instruction_tests(prog)
    print("functions: %d, instructions: %d, errors: %d" % (len(funcs), len(prog.instrs), errors))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
