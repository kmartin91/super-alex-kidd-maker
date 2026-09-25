"""Control-flow analysis of the Alex Kidd ROM.

Discovers every reachable instruction starting from the hardware entry points,
the reference disassembly's code labels and the jump tables, then splits the
code into functions suitable for translation to C.
"""
import os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z80

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF_SRC = os.path.join(ROOT, "reference", "akmw", "src")
REF_SYM = os.path.join(ROOT, "reference", "akmw", "build", "rev0.sym")
ROM_PATH = os.path.join(ROOT, "original.sms")

CODE_BANK_FOR_SLOT2 = 2  # Only bank 2 ever holds code mapped at $8000.


def cpu_bank(addr):
    if addr < 0x4000:
        return 0
    if addr < 0x8000:
        return 1
    if addr < 0xC000:
        return CODE_BANK_FOR_SLOT2
    return None


def c_ident(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


class Program:
    def __init__(self):
        self.rom = open(ROM_PATH, "rb").read()
        self.labels = {}        # (bank, cpu addr) -> name
        self.addr_names = {}    # code cpu addr -> preferred name
        self.ram_names = {}     # ram addr -> name
        self.load_symbols()
        self.instrs = {}        # addr -> Instr
        self.owner = {}         # byte addr -> instruction start (for overlap detection)
        self.problems = []

    # ------------------------------------------------------------------ symbols
    def load_symbols(self):
        section = None
        for line in open(REF_SYM):
            line = line.strip()
            if line.startswith("["):
                section = line
                continue
            if not line or line.startswith(";"):
                continue
            if section == "[labels]":
                loc, name = line.split(None, 1)
                bank, addr = loc.split(":")
                bank, addr = int(bank, 16), int(addr, 16)
                if addr >= 0xC000:
                    self.ram_names.setdefault(addr, name)
                    continue
                self.labels.setdefault((bank, addr), name)
            elif section == "[definitions]":
                val, name = line.split(None, 1)
                val = int(val, 16)
                if 0xC000 <= val <= 0xFFFF and not name.startswith("_sizeof_"):
                    self.ram_names.setdefault(val, name)

    def name_at(self, addr, bank=None):
        if bank is None:
            bank = cpu_bank(addr)
        return self.labels.get((bank, addr))

    def read(self, addr, bank=None):
        if bank is None:
            bank = cpu_bank(addr)
        if addr >= 0xC000:
            raise ValueError("code read from RAM at %04X" % addr)
        return self.rom[bank * 0x4000 + (addr & 0x3FFF)]

    # ------------------------------------------------------------- reference
    def reference_code_labels(self):
        """Names the reference disassembly places directly before an instruction."""
        names = set()
        for dp, _, fns in os.walk(REF_SRC):
            for fn in fns:
                if not fn.endswith(".asm"):
                    continue
                pending = []
                parent = None
                for raw in open(os.path.join(dp, fn), encoding="latin-1"):
                    s = raw.split(";", 1)[0].strip()
                    if not s:
                        continue
                    m = re.match(r"^([A-Za-z_@.][\w.@]*):\s*(.*)$", s)
                    if m:
                        lab, rest = m.groups()
                        if lab.startswith("@"):
                            lab = (parent or "") + lab
                        else:
                            parent = lab
                        pending.append(lab)
                        s = rest
                        if not s:
                            continue
                    if re.match(r"^[-+]+:?$", s):
                        continue
                    is_code = not s.startswith(".")
                    if is_code:
                        names.update(pending)
                    pending = []
        return names

    def dw_names(self):
        """Identifiers the reference disassembly emits inside .dw directives."""
        names = set()
        for dp, _, fns in os.walk(REF_SRC):
            for fn in fns:
                if not fn.endswith(".asm"):
                    continue
                for raw in open(os.path.join(dp, fn), encoding="latin-1"):
                    s = raw.split(";", 1)[0]
                    m = re.search(r"\.dw\s+(.*)$", s, re.I)
                    if m:
                        names.update(re.findall(r"[A-Za-z_][\w@.]*", m.group(1)))
        return names

    # ------------------------------------------------------------------ tracing
    def trace(self, seeds):
        work = list(seeds)
        while work:
            a = work.pop()
            while True:
                if a in self.instrs:
                    break
                if a >= 0xC000:
                    self.problems.append("jump into RAM at %04X" % a)
                    break
                ins = z80.decode(lambda x: self.read(x), a)
                for i in range(ins.size):
                    o = (a + i) & 0xFFFF
                    if o in self.owner and self.owner[o] != a:
                        self.problems.append("overlapping instructions at %04X / %04X" % (self.owner[o], a))
                    self.owner[o] = a
                self.instrs[a] = ins
                f = ins.flow
                if f in (z80.FLOW_JP, z80.FLOW_JPC, z80.FLOW_CALL, z80.FLOW_CALLC):
                    work.append(ins.target)
                if f in (z80.FLOW_JP, z80.FLOW_RET, z80.FLOW_JPIND, z80.FLOW_HALT):
                    break
                a = ins.next


INTRA_NEXT = (z80.FLOW_NEXT, z80.FLOW_JPC, z80.FLOW_CALL, z80.FLOW_CALLC, z80.FLOW_RETC)
INTRA_TARGET = (z80.FLOW_JP, z80.FLOW_JPC)


# jp (hl) sites that behave like a call: the routine pushed a continuation
# address first (`ld hl, cont / push hl / ... / jp (hl)`). Filled by build().
SYNTHETIC_CALLS = {}

# The VBlank interrupt is delivered at waitForInterrupt's polling loop.
WAIT_POINTS_LIVE = {0x02EA}


def successors(ins):
    out = []
    if ins.flow in INTRA_NEXT:
        out.append(ins.next)
    if ins.flow in INTRA_TARGET:
        out.append(ins.target)
    if ins.flow == z80.FLOW_JPIND and ins.addr in SYNTHETIC_CALLS:
        out.append(SYNTHETIC_CALLS[ins.addr])
    return out


def find_synthetic_calls(p):
    """Detect `ld hl,nn / push hl` followed (in straight-line code) by `jp (hl)`."""
    found = {}
    for ins in p.instrs.values():
        if not (ins.op == "ld" and ins.args[0] == ("rr", "hl") and ins.args[1][0] == "nn"):
            continue
        nxt = p.instrs.get(ins.next)
        if not (nxt and nxt.op == "push" and nxt.args[0] == ("rr", "hl")):
            continue
        cont = ins.args[1][1]
        a = nxt.next
        for _ in range(32):
            i = p.instrs.get(a)
            if i is None or i.op in ("push", "pop", "call", "rst", "ret"):
                break
            if i.flow == z80.FLOW_JPIND:
                found[i.addr] = cont
                break
            if i.flow != z80.FLOW_NEXT:
                break
            a = i.next
    return found


class Function:
    def __init__(self, entry, name):
        self.entry = entry
        self.name = name
        self.body = set()       # instruction addresses
        self.tail_calls = set() # entries reached by jump / fallthrough
        self.stack = {}         # addr -> stack delta before executing the instruction
        self.anomalies = []


def build(verbose=False):
    p = Program()
    name_to_addr = {}
    for (bank, addr), name in p.labels.items():
        name_to_addr.setdefault(name, (bank, addr))

    def resolve(n):
        if n in name_to_addr:
            bank, addr = name_to_addr[n]
            if addr < 0xC000 and cpu_bank(addr) == bank:
                return addr
        return None

    ref_code = set(filter(None, (resolve(n) for n in p.reference_code_labels())))
    vectors = [0x0000, 0x0038, 0x0066]
    p.trace(vectors)
    p.reached_from_vectors = set(p.instrs)
    p.trace(sorted(ref_code))

    # Indirect targets: .dw entries and 16-bit immediates naming instruction starts.
    indirect = set()
    for n in p.dw_names():
        a = resolve(n)
        if a is not None and a in p.instrs:
            indirect.add(a)
    for ins in list(p.instrs.values()):
        if ins.op == "ld" and len(ins.args) == 2 and ins.args[1][0] == "nn":
            v = ins.args[1][1]
            if v in p.instrs and v in ref_code:
                indirect.add(v)
    p.indirect_targets = indirect
    SYNTHETIC_CALLS.clear()
    SYNTHETIC_CALLS.update(find_synthetic_calls(p))
    p.trace(sorted(SYNTHETIC_CALLS.values()))
    p.synthetic_calls = dict(SYNTHETIC_CALLS)

    entries = set(vectors) | indirect
    for ins in p.instrs.values():
        if ins.flow in (z80.FLOW_CALL, z80.FLOW_CALLC):
            entries.add(ins.target)

    preds = defaultdict(set)
    for ins in p.instrs.values():
        for s in successors(ins):
            preds[s].add(ins.addr)

    def bodies(entries):
        funcs = {}
        for e in sorted(entries):
            f = Function(e, p.name_at(e) or "sub_%04X" % e)
            work = [e]
            while work:
                a = work.pop()
                if a in f.body:
                    continue
                if a != e and a in entries:
                    f.tail_calls.add(a)
                    continue
                f.body.add(a)
                work.extend(successors(p.instrs[a]))
            funcs[e] = f
        return funcs

    # Iteratively promote shared code to its own function.
    while True:
        funcs = bodies(entries)
        owners = defaultdict(set)
        for f in funcs.values():
            for a in f.body:
                owners[a].add(f.entry)
        promote = set()
        for a, own in owners.items():
            if len(own) < 2:
                continue
            ps = [q for q in preds[a] if q in owners]
            if not ps or any(owners[q] != own for q in ps):
                promote.add(a)
        if not promote:
            break
        entries |= promote

    # Code never reached from any function (reference-only labels): own functions.
    covered = set()
    for f in funcs.values():
        covered |= f.body
    orphans = sorted(a for a in p.instrs if a not in covered)
    while orphans:
        entries.add(orphans[0])
        funcs = bodies(entries)
        covered = set()
        for f in funcs.values():
            covered |= f.body
        orphans = sorted(a for a in p.instrs if a not in covered)

    for f in funcs.values():
        stack_analysis(p, f)
    p.functions = funcs
    return p


def stack_analysis(p, f):
    """Track push/pop depth; flag returns taken with an unbalanced stack."""
    work = [(f.entry, 0)]
    while work:
        a, d = work.pop()
        if a not in f.body:
            if d != 0:
                f.anomalies.append("tail jump to %04X with stack delta %d" % (a, d))
            continue
        if a in f.stack:
            if f.stack[a] != d:
                f.anomalies.append("inconsistent stack at %04X: %d vs %d" % (a, f.stack[a], d))
            continue
        f.stack[a] = d
        ins = p.instrs[a]
        nd = d
        if ins.op == "push":
            nd -= 2
        elif ins.op == "pop":
            nd += 2
        elif ins.op == "ex" and ins.args[0] == ("ind", "sp"):
            if d >= 0:
                f.anomalies.append("ex (sp) touches return address at %04X" % a)
        elif ins.op in ("ld", "inc", "dec", "add") and ("rr", "sp") in ins.args:
            f.anomalies.append("explicit SP manipulation at %04X: %s" % (a, ins.text()))
        if nd > 0:
            f.anomalies.append("pops past return address at %04X (%s)" % (a, ins.text()))
            nd = 0
        if ins.flow in (z80.FLOW_RET, z80.FLOW_RETC) and d != 0:
            f.anomalies.append("return at %04X with stack delta %d" % (a, d))
        if ins.flow == z80.FLOW_JPIND and d != (-2 if a in SYNTHETIC_CALLS else 0):
            f.anomalies.append("indirect jump at %04X with stack delta %d" % (a, d))
        for s in successors(ins):
            if ins.flow == z80.FLOW_JPIND:
                work.append((s, nd + 2))
            else:
                work.append((s, nd))


def main():
    p = build()
    total = len(p.instrs)
    print("instructions:", total, "(from vectors directly: %d)" % len(p.reached_from_vectors))
    print("functions:", len(p.functions), "indirect targets:", len(p.indirect_targets))
    unnamed = [f for f in p.functions.values() if f.name.startswith("sub_")]
    print("unnamed functions:", len(unnamed))
    for f in sorted(p.functions.values(), key=lambda f: f.entry):
        if f.anomalies:
            print("%04X %-40s %s" % (f.entry, f.name, "; ".join(sorted(set(f.anomalies)))))
    for pr in sorted(set(p.problems)):
        print("PROBLEM", pr)
    jps = sorted(i.addr for i in p.instrs.values() if i.flow == z80.FLOW_JPIND)
    print("jp (hl) sites:", " ".join("%04X" % a for a in jps))
    print("synthetic calls:", {"%04X" % k: "%04X" % v for k, v in p.synthetic_calls.items()})


if __name__ == "__main__":
    main()
