"""Table-free Z80 instruction decoder (x/y/z/p/q decomposition).

decode(read, addr) -> Instr, where read(addr) returns the byte at a CPU address.

Operands are tuples:
  ('r', name)          8-bit register: a b c d e h l i r ixh ixl iyh iyl
  ('rr', name)         16-bit register: af af' bc de hl sp ix iy
  ('ind', rr)          (bc) (de) (hl) (sp)
  ('idx', rr, d)       (ix+d) (iy+d)
  ('n', v)             8-bit immediate
  ('nn', v)            16-bit immediate
  ('mem', v)           (nn)
  ('port', v)          (n) port
  ('portc',)           (c) port
  ('cc', name)         condition: nz z nc c po pe p m
  ('bit', v)           bit number / im mode / rst vector
"""

R8 = ["b", "c", "d", "e", "h", "l", "(hl)", "a"]
RP = ["bc", "de", "hl", "sp"]
RP2 = ["bc", "de", "hl", "af"]
CC = ["nz", "z", "nc", "c", "po", "pe", "p", "m"]
ALU = ["add", "adc", "sub", "sbc", "and", "xor", "or", "cp"]
ROT = ["rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl"]
IM = [0, 0, 1, 2, 0, 0, 1, 2]
BLI = {
    (4, 0): "ldi", (4, 1): "cpi", (4, 2): "ini", (4, 3): "outi",
    (5, 0): "ldd", (5, 1): "cpd", (5, 2): "ind", (5, 3): "outd",
    (6, 0): "ldir", (6, 1): "cpir", (6, 2): "inir", (6, 3): "otir",
    (7, 0): "lddr", (7, 1): "cpdr", (7, 2): "indr", (7, 3): "otdr",
}

# Control-flow kinds
FLOW_NEXT = "next"        # falls through
FLOW_JP = "jp"            # unconditional jump to target
FLOW_JPC = "jpc"          # conditional jump (target + fallthrough)
FLOW_CALL = "call"        # call target, then fallthrough
FLOW_CALLC = "callc"      # conditional call
FLOW_RET = "ret"          # return
FLOW_RETC = "retc"        # conditional return (+ fallthrough)
FLOW_JPIND = "jpind"      # jp (hl)/(ix)/(iy)
FLOW_HALT = "halt"


class Instr:
    __slots__ = ("addr", "raw", "op", "args", "flow", "target", "m1")

    def __init__(self, addr, raw, op, args, flow=FLOW_NEXT, target=None, m1=1):
        self.addr = addr
        self.raw = raw
        self.op = op
        self.args = args
        self.flow = flow
        self.target = target
        self.m1 = m1  # opcode fetches (R register increments)

    @property
    def size(self):
        return len(self.raw)

    @property
    def next(self):
        return (self.addr + len(self.raw)) & 0xFFFF

    def text(self, name=None):
        def fmt(a):
            k = a[0]
            if k in ("r", "rr"):
                return a[1]
            if k == "ind":
                return "(%s)" % a[1]
            if k == "idx":
                d = a[2]
                return "(%s%s$%02X)" % (a[1], "+" if d >= 0 else "-", abs(d))
            if k == "n":
                return "$%02X" % a[1]
            if k == "nn":
                if name and name(a[1]):
                    return name(a[1])
                return "$%04X" % a[1]
            if k == "mem":
                if name and name(a[1]):
                    return "(%s)" % name(a[1])
                return "($%04X)" % a[1]
            if k == "port":
                return "($%02X)" % a[1]
            if k == "portc":
                return "(c)"
            if k == "cc":
                return a[1]
            if k == "bit":
                return "%d" % a[1] if a[1] < 8 else "$%02X" % a[1]
            raise ValueError(a)
        s = self.op
        if self.args:
            s += " " + ", ".join(fmt(a) for a in self.args)
        return s


def s8(v):
    return v - 256 if v & 0x80 else v


def _r8(i, idx, d=None):
    """Register operand i (0..7) under an optional index prefix."""
    name = R8[i]
    if idx is None:
        return ("ind", "hl") if i == 6 else ("r", name)
    if i == 6:
        return ("idx", idx, d)
    if i == 4:
        return ("r", idx + "h")
    if i == 5:
        return ("r", idx + "l")
    return ("r", name)


def decode(read, addr):
    start = addr
    raw = []

    def fetch():
        nonlocal addr
        b = read(addr & 0xFFFF)
        raw.append(b)
        addr += 1
        return b

    def mk(op, args, flow=FLOW_NEXT, target=None, m1=1):
        return Instr(start & 0xFFFF, bytes(raw), op, args, flow, target, m1)

    op = fetch()
    idx = None
    m1 = 1
    if op in (0xDD, 0xFD):
        idx = "ix" if op == 0xDD else "iy"
        op = fetch()
        m1 = 2
        if op in (0xDD, 0xFD, 0xED):
            # Prefix acting as NOP; decode as a lone prefix byte.
            raw.pop()
            addr -= 1
            return mk("nop", [], m1=1)

    if op == 0xCB:
        if idx:
            d = s8(fetch())
            op2 = fetch()
            x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
            mem = ("idx", idx, d)
            extra = [("r", R8[z])] if z != 6 and x != 1 else []
            if x == 0:
                return mk(ROT[y], [mem] + extra, m1=2)
            if x == 1:
                return mk("bit", [("bit", y), mem], m1=2)
            return mk("res" if x == 2 else "set", [("bit", y), mem] + extra, m1=2)
        op2 = fetch()
        x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
        r = _r8(z, None)
        if x == 0:
            return mk(ROT[y], [r], m1=2)
        if x == 1:
            return mk("bit", [("bit", y), r], m1=2)
        return mk("res" if x == 2 else "set", [("bit", y), r], m1=2)

    if op == 0xED:
        op2 = fetch()
        x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
        p, q = y >> 1, y & 1
        if x == 1:
            if z == 0:
                return mk("in", ([("r", R8[y])] if y != 6 else [("r", "f")]) + [("portc",)], m1=2)
            if z == 1:
                return mk("out", [("portc",), ("r", R8[y]) if y != 6 else ("n", 0)], m1=2)
            if z == 2:
                return mk("sbc" if q == 0 else "adc", [("rr", "hl"), ("rr", RP[p])], m1=2)
            if z == 3:
                nn = fetch() | (fetch() << 8)
                if q == 0:
                    return mk("ld", [("mem", nn), ("rr", RP[p])], m1=2)
                return mk("ld", [("rr", RP[p]), ("mem", nn)], m1=2)
            if z == 4:
                return mk("neg", [], m1=2)
            if z == 5:
                return mk("retn" if y != 1 else "reti", [], FLOW_RET, m1=2)
            if z == 6:
                return mk("im", [("bit", IM[y])], m1=2)
            if z == 7:
                return mk(["ld", "ld", "ld", "ld", "rrd", "rld", "nop", "nop"][y],
                          [[("r", "i"), ("r", "a")], [("r", "r"), ("r", "a")],
                           [("r", "a"), ("r", "i")], [("r", "a"), ("r", "r")],
                           [], [], [], []][y], m1=2)
        if x == 2 and y >= 4 and z <= 3:
            return mk(BLI[(y, z)], [], m1=2)
        return mk("nop", [], m1=2)  # invalid ED = NOP NOP

    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    hl = idx or "hl"

    def rp(i):
        return ("rr", hl if i == 2 else RP[i])

    def rp2(i):
        return ("rr", hl if i == 2 else RP2[i])

    def disp_if_needed(i):
        return s8(fetch()) if (idx and i == 6) else None

    if x == 0:
        if z == 0:
            if y == 0:
                return mk("nop", [], m1=m1)
            if y == 1:
                return mk("ex", [("rr", "af"), ("rr", "af'")], m1=m1)
            if y == 2:
                d = s8(fetch())
                return mk("djnz", [("nn", (addr + d) & 0xFFFF)], FLOW_JPC, (addr + d) & 0xFFFF, m1=m1)
            d = s8(fetch())
            t = (addr + d) & 0xFFFF
            if y == 3:
                return mk("jr", [("nn", t)], FLOW_JP, t, m1=m1)
            return mk("jr", [("cc", CC[y - 4]), ("nn", t)], FLOW_JPC, t, m1=m1)
        if z == 1:
            if q == 0:
                nn = fetch() | (fetch() << 8)
                return mk("ld", [rp(p), ("nn", nn)], m1=m1)
            return mk("add", [("rr", hl), rp(p)], m1=m1)
        if z == 2:
            if q == 0:
                if p == 0:
                    return mk("ld", [("ind", "bc"), ("r", "a")], m1=m1)
                if p == 1:
                    return mk("ld", [("ind", "de"), ("r", "a")], m1=m1)
                nn = fetch() | (fetch() << 8)
                if p == 2:
                    return mk("ld", [("mem", nn), ("rr", hl)], m1=m1)
                return mk("ld", [("mem", nn), ("r", "a")], m1=m1)
            if p == 0:
                return mk("ld", [("r", "a"), ("ind", "bc")], m1=m1)
            if p == 1:
                return mk("ld", [("r", "a"), ("ind", "de")], m1=m1)
            nn = fetch() | (fetch() << 8)
            if p == 2:
                return mk("ld", [("rr", hl), ("mem", nn)], m1=m1)
            return mk("ld", [("r", "a"), ("mem", nn)], m1=m1)
        if z == 3:
            return mk("inc" if q == 0 else "dec", [rp(p)], m1=m1)
        if z in (4, 5):
            d = disp_if_needed(y)
            return mk("inc" if z == 4 else "dec", [_r8(y, idx, d)], m1=m1)
        if z == 6:
            d = disp_if_needed(y)
            n = fetch()
            return mk("ld", [_r8(y, idx, d), ("n", n)], m1=m1)
        return mk(["rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf"][y], [], m1=m1)

    if x == 1:
        if y == 6 and z == 6:
            return mk("halt", [], FLOW_HALT, m1=m1)
        # With an index prefix, (hl) becomes (ix+d) and the *other* operand stays h/l.
        if idx and (y == 6 or z == 6):
            d = s8(fetch())
            if y == 6:
                return mk("ld", [("idx", idx, d), ("r", R8[z])], m1=m1)
            return mk("ld", [("r", R8[y]), ("idx", idx, d)], m1=m1)
        return mk("ld", [_r8(y, idx), _r8(z, idx)], m1=m1)

    if x == 2:
        d = disp_if_needed(z)
        return mk(ALU[y], [("r", "a"), _r8(z, idx, d)], m1=m1)

    # x == 3
    if z == 0:
        return mk("ret", [("cc", CC[y])], FLOW_RETC, m1=m1)
    if z == 1:
        if q == 0:
            return mk("pop", [rp2(p)], m1=m1)
        if p == 0:
            return mk("ret", [], FLOW_RET, m1=m1)
        if p == 1:
            return mk("exx", [], m1=m1)
        if p == 2:
            return mk("jp", [("ind", hl)], FLOW_JPIND, m1=m1)
        return mk("ld", [("rr", "sp"), ("rr", hl)], m1=m1)
    if z == 2:
        nn = fetch() | (fetch() << 8)
        return mk("jp", [("cc", CC[y]), ("nn", nn)], FLOW_JPC, nn, m1=m1)
    if z == 3:
        if y == 0:
            nn = fetch() | (fetch() << 8)
            return mk("jp", [("nn", nn)], FLOW_JP, nn, m1=m1)
        if y == 2:
            return mk("out", [("port", fetch()), ("r", "a")], m1=m1)
        if y == 3:
            return mk("in", [("r", "a"), ("port", fetch())], m1=m1)
        if y == 4:
            return mk("ex", [("ind", "sp"), ("rr", hl)], m1=m1)
        if y == 5:
            return mk("ex", [("rr", "de"), ("rr", "hl")], m1=m1)
        if y == 6:
            return mk("di", [], m1=m1)
        return mk("ei", [], m1=m1)
    if z == 4:
        nn = fetch() | (fetch() << 8)
        return mk("call", [("cc", CC[y]), ("nn", nn)], FLOW_CALLC, nn, m1=m1)
    if z == 5:
        if q == 0:
            return mk("push", [rp2(p)], m1=m1)
        nn = fetch() | (fetch() << 8)
        return mk("call", [("nn", nn)], FLOW_CALL, nn, m1=m1)
    if z == 6:
        return mk(ALU[y], [("r", "a"), ("n", fetch())], m1=m1)
    return mk("rst", [("bit", y * 8)], FLOW_CALL, y * 8, m1=m1)
