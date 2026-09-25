"""Interprocedural register liveness for every routine.

For each routine F computes:
  USE(F)      registers read before being written (its inputs)
  MUSTDEF(F)  registers written on every path to its return
  LIVEOUT(F)  registers some caller reads after F returns (its outputs)

LIVEOUT is what a hand-written replacement must reproduce; everything else is
scratch. Results go to engine/build/recomp/signatures.json and into generated-code comments.
"""
import json, os, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze, z80

REGS = ["A", "B", "C", "D", "E", "H", "L", "IXH", "IXL", "IYH", "IYL",
        "fS", "fZ", "fH", "fP", "fN", "fC",
        "A'", "F'", "B'", "C'", "D'", "E'", "H'", "L'"]
# Alternate flags are tracked individually (F' in REGS stands for "any of them"
# in reports); they live above the reported registers.
ALT_FLAGS = ["f'S", "f'Z", "f'H", "f'P", "f'N", "f'C"]
NSLOTS = 24
# Stack words pushed by the routine itself: high byte, low byte, and the six
# flags individually when the low byte came from F.
SLOT_PARTS = ("h", "l", "fS", "fZ", "fH", "fP", "fN", "fC")
SLOTS = ["S%d%s" % (i, h) for i in range(NSLOTS) for h in SLOT_PARTS]
BIT = {r: 1 << i for i, r in enumerate(REGS + ALT_FLAGS + SLOTS)}
ALL = (1 << len(REGS)) - 1
ALTF = 0
for _n in ALT_FLAGS:
    ALTF |= BIT[_n]
REAL = ALL | ALTF  # every register-like bit (no stack slots)


def slot_index(depth_after_push):
    return min(max((-depth_after_push) // 2 - 1, 0), NSLOTS - 1)


def slot_all(i):
    v = 0
    for part in SLOT_PARTS:
        v |= BIT["S%d%s" % (i, part)]
    return v


def slot_lo_all(i):
    return slot_all(i) & ~BIT["S%dh" % i]


FLAG_NAMES = ("fS", "fZ", "fH", "fP", "fN", "fC")


def push_uses(i, rr, out):
    """Registers a PUSH rr reads, given which parts of its stack slot are live."""
    rh, rl = pair_halves(rr)
    u = rh if out & BIT["S%dh" % i] else 0
    if rr == "af":
        if out & BIT["S%dl" % i]:
            u |= FLAGS
        for fl in FLAG_NAMES:
            if out & BIT["S%d%s" % (i, fl)]:
                u |= BIT[fl]
    elif out & slot_lo_all(i):
        u |= rl
    return u


def pop_slot(i, rr, out):
    """Slot parts a POP rr makes live, given which registers are live after it."""
    rh, rl = pair_halves(rr)
    v = BIT["S%dh" % i] if out & rh else 0
    if rr == "af":
        for fl in FLAG_NAMES:
            if out & BIT[fl]:
                v |= BIT["S%d%s" % (i, fl)]
    elif out & rl:
        v |= slot_lo_all(i)
    return v


def pair_halves(rr):
    """(hi mask, lo mask) of a 16-bit register pair."""
    if rr == "af":
        return BIT["A"], FLAGS_ALL
    hi, lo = {"bc": ("B", "C"), "de": ("D", "E"), "hl": ("H", "L"), "ix": ("IXH", "IXL"), "iy": ("IYH", "IYL")}[rr]
    return BIT[hi], BIT[lo]


def M(*names):
    v = 0
    for n in names:
        v |= BIT[n]
    return v


FLAGS = M("fS", "fZ", "fH", "fP", "fN", "fC")
FLAGS_ALL = FLAGS
FLAGS_NO_C = M("fS", "fZ", "fH", "fP", "fN")
R8 = {"a": "A", "b": "B", "c": "C", "d": "D", "e": "E", "h": "H", "l": "L",
      "ixh": "IXH", "ixl": "IXL", "iyh": "IYH", "iyl": "IYL"}
R16 = {"bc": M("B", "C"), "de": M("D", "E"), "hl": M("H", "L"), "ix": M("IXH", "IXL"),
       "iy": M("IYH", "IYL"), "af": M("A") | FLAGS, "sp": 0}
COND = {"nz": M("fZ"), "z": M("fZ"), "nc": M("fC"), "c": M("fC"),
        "po": M("fP"), "pe": M("fP"), "p": M("fS"), "m": M("fS")}

SWAP_EXX = [("B", "B'"), ("C", "C'"), ("D", "D'"), ("E", "E'"), ("H", "H'"), ("L", "L'")]
SWAP_DEHL = [("D", "H"), ("E", "L")]


def swap(v, pairs):
    out = v
    for a, b in pairs:
        ba, bb = BIT[a], BIT[b]
        out &= ~(ba | bb)
        if v & ba:
            out |= bb
        if v & bb:
            out |= ba
    return out


_FPAIRS = [("fS", "f'S"), ("fZ", "f'Z"), ("fH", "f'H"), ("fP", "f'P"), ("fN", "f'N"), ("fC", "f'C")]


def swap_af(v):
    out = swap(v & ~BIT["F'"], [("A", "A'")] + _FPAIRS)
    if out & ALTF:
        out |= BIT["F'"]
    return out


def swap_af_must(v):
    return swap_af(v)


def report(v):
    """Reported register mask: the 25 REGS bits, F' standing for any alternate flag."""
    return (v & ALL) | (BIT["F'"] if v & ALTF else 0)


def names(v):
    v = report(v)
    return [r for r in REGS if v & BIT[r]]


def opnd_use(a):
    """Registers read to evaluate operand `a` as a source (or to address it)."""
    k = a[0]
    if k == "r":
        return BIT[R8[a[1]]] if a[1] in R8 else 0
    if k == "rr":
        return R16.get(a[1], 0)
    if k == "ind":
        return R16.get(a[1], 0)
    if k == "idx":
        return R16[a[1]]
    return 0


def addr_use(a):
    return R16.get(a[1], 0) if a[0] in ("ind", "idx") else 0


def reg_def(a):
    if a[0] == "r":
        return BIT[R8[a[1]]] if a[1] in R8 else 0
    if a[0] == "rr":
        return R16.get(a[1], 0)
    return 0


RENAME = "rename"


def use_def(ins):
    """(use, def) for straight-line semantics, or (RENAME, fn) for exchanges."""
    op, args = ins.op, ins.args
    if op == "exx":
        return RENAME, lambda v, must=False: swap(v, SWAP_EXX)
    if op == "ex":
        if args[0] == ("rr", "af"):
            return RENAME, lambda v, must=False: swap_af_must(v) if must else swap_af(v)
        if args[0] == ("rr", "de"):
            return RENAME, lambda v, must=False: swap(v, SWAP_DEHL)
        r = R16[args[1][1]]
        return r, r  # ex (sp),hl
    if op == "ld":
        dst, src = args
        if src == ("r", "i") or src == ("r", "r"):
            return 0, BIT["A"] | FLAGS_NO_C
        if dst in (("r", "i"), ("r", "r")):
            return BIT["A"], 0
        if dst[0] in ("r", "rr"):
            return opnd_use(src), reg_def(dst)
        return addr_use(dst) | opnd_use(src), 0
    if op == "push":
        return R16[args[0][1]], 0
    if op == "pop":
        return 0, R16[args[0][1]]
    if op in ("add", "adc", "sub", "sbc", "and", "xor", "or", "cp"):
        if args[0][0] == "rr":
            d = R16[args[0][1]]
            u = d | R16[args[1][1]]
            if op == "add":
                return u, d | M("fH", "fN", "fC")
            return u | BIT["fC"], d | FLAGS
        src = args[1]
        u = opnd_use(src) | BIT["A"]
        if src == ("r", "a") and op in ("xor", "sub"):
            u = 0
        if src == ("r", "a") and op == "sbc":
            u = BIT["fC"]
        if op in ("adc", "sbc"):
            u |= BIT["fC"]
        return u, (0 if op == "cp" else BIT["A"]) | FLAGS
    if op in ("inc", "dec"):
        a = args[0]
        if a[0] == "rr":
            return R16[a[1]], R16[a[1]]
        return opnd_use(a), reg_def(a) | FLAGS_NO_C
    if op in ("rlca", "rrca"):
        return BIT["A"], M("A", "fH", "fN", "fC")
    if op in ("rla", "rra"):
        return M("A", "fC"), M("A", "fH", "fN", "fC")
    if op in ("rlc", "rrc", "sla", "sra", "sll", "srl", "rl", "rr"):
        a = args[0]
        u = opnd_use(a) | (BIT["fC"] if op in ("rl", "rr") else 0)
        d = reg_def(a) | FLAGS
        if len(args) > 1:
            d |= reg_def(args[1])
        return u, d
    if op == "bit":
        return opnd_use(args[1]), FLAGS_NO_C
    if op in ("set", "res"):
        a = args[1]
        d = reg_def(a)
        if len(args) > 2:
            d |= reg_def(args[2])
        return opnd_use(a), d
    if op == "daa":
        return M("A", "fC", "fH", "fN"), M("A", "fS", "fZ", "fH", "fP", "fC")
    if op == "cpl":
        return BIT["A"], M("A", "fH", "fN")
    if op == "neg":
        return BIT["A"], BIT["A"] | FLAGS
    if op == "scf":
        return 0, M("fC", "fH", "fN")
    if op == "ccf":
        return BIT["fC"], M("fC", "fH", "fN")
    if op in ("rrd", "rld"):
        return M("A", "H", "L"), BIT["A"] | FLAGS_NO_C
    if op in ("ldi", "ldd", "ldir", "lddr"):
        r = M("B", "C", "D", "E", "H", "L")
        return r, r | M("fH", "fP", "fN")
    if op in ("cpi", "cpd", "cpir", "cpdr"):
        return M("A", "B", "C", "H", "L"), M("B", "C", "H", "L") | FLAGS_NO_C
    if op in ("ini", "ind", "inir", "indr", "outi", "outd", "otir", "otdr"):
        return M("B", "C", "H", "L"), M("B", "H", "L", "fZ", "fN")
    if op == "out":
        port, v = args
        return (BIT["C"] if port[0] == "portc" else 0) | opnd_use(v), 0
    if op == "in":
        dst, port = args
        if port[0] == "port":
            return 0, BIT["A"]
        return BIT["C"], (reg_def(dst) if dst[1] != "f" else 0) | FLAGS_NO_C
    if op in ("di", "ei", "im", "nop", "halt", "rst", "call", "ret", "retn", "reti", "jp", "jr", "djnz"):
        u = 0
        if args and args[0][0] == "cc":
            u |= COND[args[0][1]]
        if op == "djnz":
            return BIT["B"], BIT["B"]
        if op == "jp" and args and args[0][0] == "ind":
            u |= R16[args[0][1]]
        return u, 0
    raise ValueError("no use/def for %s" % ins.text())


DISPATCHERS = {0x0020, 0x0021, 0x001B}  # rst $20 family: HL = table, A = index
DISPATCH_DEF = M("A", "D", "E", "H", "L") | FLAGS


class Analysis:
    def __init__(self, prog):
        self.p = prog
        self.funcs = prog.functions
        self.entries = set(self.funcs)
        self.find_tables()
        self.resolve_dispatch()
        self.use = {e: 0 for e in self.funcs}
        self.mustdef = {e: REAL for e in self.funcs}
        self.liveout = {e: 0 for e in self.funcs}
        self.maydef = {e: 0 for e in self.funcs}

    # ----------------------------------------------------------- jump tables
    def find_tables(self):
        p = self.p
        self.tables = {}
        imms = set()
        for ins in p.instrs.values():
            for a in ins.args:
                if a[0] == "nn":
                    imms.add(a[1])
        # handler table of readSoundRequest: `sounds` + $60 (computed, not an immediate)
        sounds = [a for (b, a), n in p.labels.items() if n == "sounds"]
        extra = [sounds[0] + 0x60] if sounds else []
        bases = set(n for n in imms if n < 0xC000) | set(extra)
        labelled = set(a for (b, a) in p.labels if a < 0xC000)
        for nn in list(imms) + extra:
            if nn >= 0xC000:
                continue
            targets = []
            a = nn
            # A table ends where another referenced table or a label begins. Tables
            # indexed from 1 are referenced one word early: that word is unused.
            while a < 0xBFFF and len(targets) < 128:
                if a != nn and (a in bases or a in labelled):
                    if a == nn + 2:
                        targets = []
                    else:
                        break
                w = p.read(a) | (p.read(a + 1) << 8)
                if w not in self.entries or w == 0:
                    if a == nn:
                        a += 2
                        continue
                    break
                targets.append(w)
                a += 2
            if targets:
                self.tables[nn] = targets
        self.sound_table = extra[0] if extra else None

    def tables_loaded_in(self, func):
        out = []
        for a in func.body:
            ins = self.p.instrs[a]
            for arg in ins.args:
                if arg[0] == "nn" and arg[1] in self.tables:
                    out.extend(self.tables[arg[1]])
        return out

    def resolve_dispatch(self):
        """Targets of each dispatch: rst $20 call sites and custom jp (hl) sites."""
        p = self.p
        self.dispatch_targets = {}
        self.unresolved = []
        by_addr = p.instrs
        preds = defaultdict(list)
        for i in by_addr.values():
            if i.flow in (z80.FLOW_NEXT, z80.FLOW_CALL, z80.FLOW_CALLC, z80.FLOW_JPC, z80.FLOW_RETC):
                preds[i.next].append(i)

        def preceding_table(a, reg="hl"):
            for _ in range(12):
                ps = preds.get(a)
                if not ps:
                    return None
                x = ps[0]
                if x.op == "ld" and x.args[0] == ("rr", reg) and x.args[1][0] == "nn":
                    return x.args[1][1]
                a = x.addr
            return None

        for ins in by_addr.values():
            if ins.flow in (z80.FLOW_CALL, z80.FLOW_CALLC, z80.FLOW_JP, z80.FLOW_JPC) and ins.target in DISPATCHERS:
                t = preceding_table(ins.addr)
                if t in self.tables:
                    self.dispatch_targets[ins.addr] = list(self.tables[t])
                else:
                    self.unresolved.append(ins.addr)

        # Custom jp (hl) sites: a table loaded just before, or a RAM pointer whose
        # writers load it from a table just before storing it.
        pointer_tables = defaultdict(list)
        for i in by_addr.values():
            if i.op == "ld" and i.args[0][0] == "mem" and i.args[1] == ("rr", "hl"):
                t = preceding_table(i.addr)
                if t in self.tables:
                    pointer_tables[i.args[0][1]].extend(self.tables[t])
        for f in self.funcs.values():
            for a in f.body:
                i = by_addr[a]
                if i.flow != z80.FLOW_JPIND or a == 0x0029:
                    continue
                targets = []
                t = preceding_table(a)
                if t in self.tables:
                    targets = self.tables[t]
                ps = preds.get(a)
                src = ps[0] if ps else None
                if not targets and src and src.op == "ld" and src.args[0] == ("rr", "hl") and src.args[1][0] == "mem":
                    targets = pointer_tables.get(src.args[1][1], [])
                if a == 0x98DC and self.sound_table in self.tables:
                    targets = self.tables[self.sound_table]
                if targets:
                    self.dispatch_targets[a] = sorted(set(targets))
                else:
                    self.unresolved.append(a)

    # --------------------------------------------------------------- summaries
    def call_effect(self, ins):
        """(use, mustdef, targets) of a call-like instruction."""
        if ins.addr in self.dispatch_targets and ins.target in DISPATCHERS:
            ts = self.dispatch_targets[ins.addr]
            u = 0
            md = REAL
            for t in ts:
                u |= self.use[t] & ~DISPATCH_DEF
                md &= self.mustdef[t]
            u |= M("A", "H", "L")
            md |= DISPATCH_DEF if ins.target != 0x001B else 0
            if ins.target == 0x001B:
                md = md & FLAGS_NO_C
            return u, md, ts
        t = ins.target
        return self.use[t], self.mustdef[t], [t]

    def successors(self, f, ins):
        return [s for s in analyze.successors(ins)]

    def backward(self, f, live_at_ret):
        """Backward liveness over f's body. Returns (live_in by addr, call-site live-after)."""
        p = self.p
        live_in = {a: 0 for a in f.body}
        after_call = {}
        changed = True
        order = sorted(f.body, reverse=True)
        while changed:
            changed = False
            for a in order:
                ins = p.instrs[a]
                out = 0
                fl = ins.flow
                # successors' live-in
                for s in analyze.successors(ins):
                    if fl == z80.FLOW_JPIND:
                        continue  # synthetic call continuation handled below
                    if s in f.body:
                        out |= live_in[s]
                    else:
                        out |= self.use[s] | (live_at_ret & ~self.mustdef[s])
                if fl == z80.FLOW_RET:
                    out |= live_at_ret
                if fl == z80.FLOW_RETC:
                    out |= live_at_ret
                if a in analyze.WAIT_POINTS_LIVE:
                    pass
                u, d = use_def(ins)
                if fl in (z80.FLOW_CALL, z80.FLOW_CALLC):
                    after_call[a] = out
                    cu, cmd, _ = self.call_effect(ins)
                    if fl == z80.FLOW_CALL:
                        v = cu | (out & ~cmd)
                    else:
                        v = cu | out
                    v |= u
                elif fl == z80.FLOW_JPIND:
                    ts = self.dispatch_targets.get(a)
                    tu = 0
                    tmd = REAL
                    for t in (ts or []):
                        tu |= self.use[t]
                        tmd &= self.mustdef[t]
                    if ts is None:
                        tu = REAL
                        tmd = 0
                    if a in p.synthetic_calls:
                        cont = p.synthetic_calls[a]
                        k = live_in[cont] if cont in f.body else 0
                        after_call[a] = k
                        v = u | tu | (k & ~tmd)
                    else:
                        after_call[a] = live_at_ret
                        v = u | tu | (live_at_ret & ~tmd)
                elif u == RENAME:
                    v = d(out)
                elif ins.op == "push":
                    i = slot_index(f.stack.get(a, 0) - 2)
                    v = (out & ~slot_all(i)) | push_uses(i, ins.args[0][1], out)
                elif ins.op == "pop":
                    i = slot_index(f.stack.get(a, 0))
                    rh, rl = pair_halves(ins.args[0][1])
                    v = (out & ~(rh | rl)) | pop_slot(i, ins.args[0][1], out)
                elif ins.op == "ex" and ins.args[0] == ("ind", "sp"):
                    v = out | u | slot_all(slot_index(f.stack.get(a, 0)))
                else:
                    v = u | (out & ~d)
                if v != live_in[a]:
                    live_in[a] = v
                    changed = True
        return live_in, after_call

    def forward_mustdef(self, f):
        """Registers defined on every path from entry to each return/tail exit."""
        p = self.p
        IN = {a: None for a in f.body}
        IN[f.entry] = 0
        result = REAL
        work = [f.entry]
        exits = []
        while work:
            a = work.pop()
            v = IN[a]
            ins = p.instrs[a]
            u, d = use_def(ins)
            fl = ins.flow
            if fl in (z80.FLOW_CALL, z80.FLOW_CALLC):
                _, cmd, _ = self.call_effect(ins)
                out = v | d | (cmd if fl == z80.FLOW_CALL else 0)
            elif u == RENAME:
                out = d(v, True)
            else:
                out = v | d
            if fl in (z80.FLOW_RET, z80.FLOW_RETC):
                exits.append(out if fl == z80.FLOW_RET else (v | d))
            if fl == z80.FLOW_JPIND and a not in p.synthetic_calls:
                ts = self.dispatch_targets.get(a) or []
                md = REAL
                for t in ts:
                    md &= self.mustdef[t]
                exits.append(out | (md if ts else 0))
            for s in analyze.successors(ins):
                if fl == z80.FLOW_JPIND:
                    ts = self.dispatch_targets.get(a) or []
                    md = REAL
                    for t in ts:
                        md &= self.mustdef[t]
                    nv = out | (md if ts else 0)
                elif s in f.body:
                    nv = out
                else:
                    exits.append(out | self.mustdef[s])
                    continue
                if s not in f.body:
                    continue
                if IN[s] is None:
                    IN[s] = nv
                    work.append(s)
                elif IN[s] & nv != IN[s]:
                    IN[s] &= nv
                    work.append(s)
        for e in exits:
            result &= e
        return result if exits else REAL

    def solve_maydef(self):
        """Registers a routine (or anything it calls) may modify."""
        EXX_ALL = M("B", "C", "D", "E", "H", "L", "B'", "C'", "D'", "E'", "H'", "L'")
        for _ in range(50):
            changed = False
            for e, f in self.funcs.items():
                v = 0
                for a in f.body:
                    ins = self.p.instrs[a]
                    u, d = use_def(ins)
                    if u == RENAME:
                        if ins.op == "exx":
                            v |= EXX_ALL
                        elif ins.args[0] == ("rr", "af"):
                            v |= M("A", "A'", "F'") | FLAGS | ALTF
                        else:
                            v |= M("D", "E", "H", "L")
                    else:
                        v |= d
                    if ins.flow in (z80.FLOW_CALL, z80.FLOW_CALLC):
                        _, _, ts = self.call_effect(ins)
                        for t in ts:
                            v |= self.maydef[t]
                        if ins.target in DISPATCHERS:
                            v |= DISPATCH_DEF
                    if ins.flow == z80.FLOW_JPIND:
                        for t in self.dispatch_targets.get(a, []):
                            v |= self.maydef[t]
                    for s_ in analyze.successors(ins):
                        if s_ not in f.body and ins.flow != z80.FLOW_JPIND:
                            v |= self.maydef[s_]
                if v != self.maydef[e]:
                    self.maydef[e] = v
                    changed = True
            if not changed:
                break

    # ------------------------------------------------------ preserved registers
    TRACK = ["A", "B", "C", "D", "E", "H", "L", "IXH", "IXL", "IYH", "IYL",
             "fS", "fZ", "fH", "fP", "fN", "fC",
             "A'", "B'", "C'", "D'", "E'", "H'", "L'"] + ALT_FLAGS

    def preserved_transfer(self, f, a, st):
        """Forward transfer: st maps a register (or stack slot part) to the
        register whose entry value it holds, or None."""
        ins = self.p.instrs[a]
        op, args = ins.op, ins.args
        st = dict(st)

        def kill(mask):
            for r in self.TRACK:
                if mask & BIT[r]:
                    st[r] = None

        def swap_keys(pairs):
            for x, y in pairs:
                st[x], st[y] = st.get(x), st.get(y)

        if op == "exx":
            swap_keys(SWAP_EXX)
            return st
        if op == "ex" and args[0] == ("rr", "af"):
            swap_keys([("A", "A'")] + _FPAIRS)
            return st
        if op == "ex" and args[0] == ("rr", "de"):
            swap_keys(SWAP_DEHL)
            return st
        if op == "ld" and args[0][0] == "r" and args[1][0] == "r" and args[0][1] in R8 and args[1][1] in R8:
            st[R8[args[0][1]]] = st.get(R8[args[1][1]])
            return st
        if op == "push":
            i = slot_index(f.stack.get(a, 0) - 2)
            rr = args[0][1]
            if rr == "af":
                st["S%dh" % i] = st.get("A")
                for fl in FLAG_NAMES:
                    st["S%d%s" % (i, fl)] = st.get(fl)
                st["S%dl" % i] = None
            else:
                hi, lo = {"bc": ("B", "C"), "de": ("D", "E"), "hl": ("H", "L"),
                          "ix": ("IXH", "IXL"), "iy": ("IYH", "IYL")}[rr]
                st["S%dh" % i] = st.get(hi)
                st["S%dl" % i] = st.get(lo)
                for fl in FLAG_NAMES:
                    st["S%d%s" % (i, fl)] = None
            return st
        if op == "pop":
            i = slot_index(f.stack.get(a, 0))
            rr = args[0][1]
            if rr == "af":
                st["A"] = st.get("S%dh" % i)
                for fl in FLAG_NAMES:
                    st[fl] = st.get("S%d%s" % (i, fl))
            else:
                hi, lo = {"bc": ("B", "C"), "de": ("D", "E"), "hl": ("H", "L"),
                          "ix": ("IXH", "IXL"), "iy": ("IYH", "IYL")}[rr]
                st[hi] = st.get("S%dh" % i)
                st[lo] = st.get("S%dl" % i)
            return st
        if ins.flow in (z80.FLOW_CALL, z80.FLOW_CALLC):
            _, _, ts = self.call_effect(ins)
            keep = REAL
            for t in ts:
                keep &= self.preserved[t]
            if ins.target in DISPATCHERS:
                keep &= ~DISPATCH_DEF
            u, d = use_def(ins)
            kill((REAL & ~keep) if ins.flow == z80.FLOW_CALL else ((REAL & ~keep) | (d if u != RENAME else 0)))
            return st
        u, d = use_def(ins)
        if u == RENAME:
            return st
        if op == "ex":
            kill(d)
            for k in list(st):
                if k.startswith("S"):
                    st[k] = None
            return st
        kill(d)
        return st

    def solve_preserved(self):
        self.preserved = {e: REAL for e in self.funcs}
        for _ in range(50):
            changed = False
            for e, f in self.funcs.items():
                init = {r: r for r in self.TRACK}
                IN = {f.entry: init}
                work = [f.entry]
                exits = []
                while work:
                    a = work.pop()
                    st = IN[a]
                    ins = self.p.instrs[a]
                    out = self.preserved_transfer(f, a, st)
                    fl = ins.flow
                    if fl == z80.FLOW_RET:
                        exits.append(out)
                    if fl == z80.FLOW_RETC:
                        exits.append(st)
                    if fl == z80.FLOW_JPIND and a not in self.p.synthetic_calls:
                        keep = REAL
                        for t in self.dispatch_targets.get(a) or []:
                            keep &= self.preserved[t]
                        if not self.dispatch_targets.get(a):
                            keep = 0
                        exits.append({r: (v if keep & BIT[r] else None) for r, v in out.items() if r in BIT})
                    for s_ in analyze.successors(ins):
                        nst = out
                        if fl == z80.FLOW_JPIND:
                            keep = REAL
                            for t in self.dispatch_targets.get(a) or []:
                                keep &= self.preserved[t]
                            nst = {r: (v if (r not in self.TRACK or keep & BIT[r]) else None) for r, v in out.items()}
                        elif s_ not in f.body:
                            keep = self.preserved.get(s_, 0)
                            exits.append({r: (v if keep & BIT[r] else None) for r, v in out.items() if r in self.TRACK})
                            continue
                        if s_ not in IN:
                            IN[s_] = nst
                            work.append(s_)
                        else:
                            old = IN[s_]
                            merged = {k: (old.get(k) if old.get(k) == nst.get(k) else None) for k in set(old) | set(nst)}
                            if merged != old:
                                IN[s_] = merged
                                work.append(s_)
                v = REAL
                for ex in exits:
                    for r in self.TRACK:
                        if ex.get(r) != r:
                            v &= ~BIT[r]
                if not exits:
                    v = REAL
                if v != self.preserved[e]:
                    self.preserved[e] = v
                    changed = True
            if not changed:
                break

    def outputs(self, e):
        return self.liveout[e] & self.maydef[e] & ~self.preserved[e]

    def solve(self):
        self.solve_maydef()
        self.solve_preserved()
        # 1) USE / MUSTDEF summaries (independent of callers).
        for _ in range(50):
            changed = False
            for e in sorted(self.funcs):
                f = self.funcs[e]
                md = self.forward_mustdef(f)
                if md != self.mustdef[e]:
                    self.mustdef[e] = md
                    changed = True
                live_in, _ = self.backward(f, 0)
                u = live_in[e] & REAL
                if u != self.use[e]:
                    self.use[e] = u
                    changed = True
            if not changed:
                break
        # 2) LIVEOUT: what callers read after each routine returns.
        wait_live = {}
        for _ in range(50):
            changed = False
            new = defaultdict(int)
            for e in sorted(self.funcs):
                f = self.funcs[e]
                live_in, after = self.backward(f, self.liveout[e])
                for a, live in after.items():
                    live &= REAL
                    ins = self.p.instrs[a]
                    if ins.flow == z80.FLOW_JPIND:
                        for t in self.dispatch_targets.get(a, []):
                            new[t] |= live
                        continue
                    _, _, ts = self.call_effect(ins)
                    for t in ts:
                        new[t] |= live
                # tail calls propagate the caller's outputs
                for a in f.body:
                    ins = self.p.instrs[a]
                    for s in analyze.successors(ins):
                        if s not in f.body and ins.flow != z80.FLOW_JPIND:
                            new[s] |= self.liveout[e]
                for a in analyze.WAIT_POINTS_LIVE:
                    if a in f.body:
                        new[0x0038] |= live_in[a] & REAL
                        new[0x0066] |= live_in[a] & REAL
            for e in self.funcs:
                if new[e] != self.liveout[e]:
                    self.liveout[e] = new[e]
                    changed = True
            if not changed:
                break


def build():
    prog = analyze.build()
    an = Analysis(prog)
    an.solve()
    return prog, an


def main():
    prog, an = build()
    out = {}
    for e, f in sorted(prog.functions.items()):
        out["%04X" % e] = {
            "name": f.name,
            "in": names(an.use[e]),
            "out": names(an.outputs(e)),
            "preserves": names(an.liveout[e] & ~an.outputs(e)),
            "modifies": names(an.maydef[e]),
            "size": len(f.body),
        }
    path = os.path.join(analyze.ROOT, "engine", "build", "recomp", "signatures.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(out, open(path, "w"), indent=1)
    print("unresolved dispatch sites:", ["%04X" % a for a in an.unresolved])
    print("dispatch sites resolved:", len(an.dispatch_targets))
    for e in (0x0008, 0x0010, 0x0020, 0x0145, 0x02E6, 0x03ED, 0x074C, 0x0759):
        s = out["%04X" % e]
        print("%04X %-28s in=%s out=%s" % (e, s["name"], " ".join(s["in"]), " ".join(s["out"])))
    from collections import Counter
    widths = Counter(len(s["out"]) for s in out.values())
    print("output-count histogram:", sorted(widths.items()))
    big = sorted(out.items(), key=lambda kv: -len(kv[1]["out"]))[:10]
    print("widest outputs:")
    for k, s in big:
        print("  %s %-36s out=%s" % (k, s["name"], " ".join(s["out"])))


if __name__ == "__main__":
    main()
