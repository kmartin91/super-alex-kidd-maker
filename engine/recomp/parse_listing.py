"""Parse the WLA-DX listing files of the reference disassembly into a ROM byte map.

Output: engine/build/recomp/romdb.json with, for every ROM offset, whether it is code or data,
plus the source line and file that produced it.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF = os.path.join(ROOT, "reference", "akmw", "src")
LINE = re.compile(r"^\s*(\d+) ([0-9A-F]{4}) ([0-9A-F]{4}) ([0-9A-F]{4}) ([0-9A-F]{4})   ((?:[0-9A-F]{2} )+)\s*(.*)$")

def main():
    rom = open(os.path.join(ROOT, "original.sms"), "rb").read()
    kind = [None] * len(rom)
    entries = []
    for dp, _, fns in os.walk(REF):
        for fn in fns:
            if not fn.endswith(".lst"):
                continue
            path = os.path.join(dp, fn)
            rel = os.path.relpath(path, REF)[:-4] + ".asm"
            for raw in open(path, encoding="latin-1"):
                m = LINE.match(raw.rstrip("\n"))
                if not m:
                    continue
                lineno, bank, slot, pc, off, hexs, src = m.groups()
                bank, pc, off = int(bank, 16), int(pc, 16), int(off, 16)
                data = bytes(int(x, 16) for x in hexs.split())
                rom_off = bank * 0x4000 + off
                s = src.strip()
                body = re.sub(r"^[A-Za-z_@.][\w.@]*:\s*", "", s)
                is_data = body.startswith(".") or body == ""
                k = "data" if is_data else "code"
                for i, b in enumerate(data):
                    o = rom_off + i
                    if rom[o] != b:
                        print("MISMATCH", rel, lineno, hex(o)); sys.exit(1)
                    kind[o] = k
                entries.append((rom_off, len(data), k, rel, int(lineno), s))
    os.makedirs(os.path.join(ROOT, "engine", "build", "recomp"), exist_ok=True)
    entries.sort()
    json.dump({"entries": entries}, open(os.path.join(ROOT, "engine", "build", "recomp", "romdb.json"), "w"))
    for b in range(len(rom) // 0x4000):
        seg = kind[b*0x4000:(b+1)*0x4000]
        print("bank %d: code=%5d data=%5d unlisted=%5d" % (b, seg.count("code"), seg.count("data"), seg.count(None)))
    print("instructions:", sum(1 for e in entries if e[2] == "code"))

main()
