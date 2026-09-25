#!/usr/bin/env python3
"""Level data codec for Alex Kidd in Miracle World (Sega Master System, USA/Europe rev 0).

Everything is read from the original ROM at run time; no game data is stored here.
The format is documented in docs/level-format.md.

    python3 maker/tools/levels.py dump N            readable summary of level N (1..17)
    python3 maker/tools/levels.py verify            decode + re-encode all levels, compare with the ROM
    python3 maker/tools/levels.py render N out.ppm  whole-level map drawn with the level's VRAM tiles
    python3 maker/tools/levels.py json [N]          the decoded model as JSON

Options: --rom PATH (default: original.sms at the repository root); dump: --no-metatiles;
render: --no-entities, --no-grid, --scale K, --vram DUMP (harness dump instead of ROM tiles).

Library use:
    import levels
    model = levels.load(open('original.sms', 'rb').read())
    patches = levels.encode(model)       # [(rom_offset, bytes), ...]

Python 3.7 compatible.
"""
import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_ROM = os.path.join(ROOT, "original.sms")
ROM_CRC32 = 0x17A40E29  # USA/Europe rev 0

LEVEL_COUNT = 17
SCREEN_W, SCREEN_H = 16, 12          # metatiles per screen
SCREEN_CELLS = SCREEN_W * SCREEN_H   # 192 bytes once decompressed

# --------------------------------------------------------------------------------------
# Fixed ROM locations (CPU addresses; banks 0/1 are fixed at $0000-$7FFF, slot 2 = $8000)
# --------------------------------------------------------------------------------------
LEVEL_DESCRIPTOR_TABLE = 0x66CF      # bank 1, 17 x dw, read by loadLevel ($65AA) as ($66CD)[v_level]
LEVEL_DESCRIPTOR_SIZE = 12
ENTITY_DESCRIPTOR_TABLE = 0xB505     # bank 2 (slot 2), 17 x dw, read by initGameplayState
ENTITY_BANK = 2
SCROLL_FLAGS_UPDATERS = 0x0D0A       # 17 x dw (code pointers)
PALETTE_UPDATERS = 0x0D2C            # 17 x dw (code pointers)
ENTITY_LOADERS = 0x0D4E              # 17 x dw (code pointers)
SHOP_DOORS = 0x0D70                  # 17 x (db offset, dw RAM name-table address)
STARTING_POSITIONS = 0x0DA3          # 17 x (db x, db y)
LEVEL_SONGS = 0x0DC5                 # 18 x db (last entry unused by v_level 1..17)
SPAWN_STATES = 0x0E1F                # 17 x db
QUESTION_BOX_INDEXES = 0x0E30        # 17 x db
TILESET_LOADERS = 0x0E7D             # 17 x dw (code pointers), jumped to by loadLevelTiles
LEVEL_PALETTES = 0x1112              # 17 x dw -> 32-byte palettes in bank 7
PALETTE_BANK = 7
SPRITE_TILES_LOADERS = 0x1142        # 17 x dw (code pointers), jumped to by loadLevelSpriteTiles
TILE_UPDATERS = 0x156D               # 17 x dw (code pointers)
MAIN_TILESET_POINTERS = 0x8480       # bank 3, 17 x dw -> compressed tiles in bank 3
CAMERA_HANDLERS = 0x3F33             # 17 x dw (code pointers), rst $20 from updateAlex $297A
VEHICLE_CRASH_TO_WATER = 0x3904      # 17 x db: 1 = a wrecked vehicle drops Alex into water
MAP_ARROW_POSITIONS = 0x1BA7         # 17 x (db x, db y), map screen arrow
SHOP_ITEM_TABLES = 0x1F89            # 17 x dw -> bank 6: 3 x (dw name-table dest, dw graphics)
SHOP_SOLD_TABLES = 0x1FAB            # 17 x dw -> bank 6: 3 x (db item, dw flag RAM, dw name-table)
QUESTION_BOX_ITEMS = 0x0DD7          # 72 x db entity types, indexed by v_questionMarkBoxIndex
FLOOR_PUZZLE_SEQUENCE = 0x3DE9       # level-17 floor symbol order, $FF-terminated
OCTOPUS_ARM_SETS = 0x70FB            # 2 x dw -> (dw pot address, 8 x (delay, y, x, follower))
DEMO_LEVELS = 0x0A7C                 # 4 x db levels shown by the attract-mode demos
MAIN_TILESET_BANK = 3
METATILE_BANK = 5                    # bank 5 is hard-wired by the name-table builders
METATILE_TABLES = {"A": 0x8000, "B": 0x8200}  # 256 x dw each (bank 5)
CASTLE_BLOCK_TABLES = {             # breakable-block lists copied to $D900 by initGameplayState
    11: (0x97DD, 5),                 # radactianCastleMetatileDeletes (bank 2), 5 rooms per row
    16: (0x9800, 7),                 # "craggLakeMetatileDeletes" (really Janken's castle), 7 per row
}
# Sub-areas entered through entity $4C (STATE_BONUS_LEVEL, loader at $1735).  The loader
# hard-codes the layout pointers and uses bank v_levelBankNumber (still $86):
#   v_level != 17: rows table $8AD6 (= level 4's), horizontal screen 6 -> screens 5..7 visible,
#                  width 7, graphics/palette/sprites of level v_level+1, entity index 8, 9, 10.
#                  Only level 3 places a $4C trigger, so this is Lake Fathom's land area.
#   v_level == 17: rows table $BC53 (level 17's own), horizontal screen 3 ($C07F == 0) or 4
#                  (otherwise) -> screen 2 or 3, width 0 (no scrolling), entity index 8.
SUB_AREAS = {
    3: {"loader": 0x1735, "layout_ptr": 0x8AD6, "layout_level": 4, "graphics_level": 4,
        "screens": [5, 6, 7], "entity_index": [8, 9, 10], "variants": None},
    17: {"loader": 0x1735, "layout_ptr": 0xBC53, "layout_level": 17, "graphics_level": 17,
         "screens": [2, 3], "entity_index": [8, 8], "variants": ["$C07F == 0", "$C07F != 0"]},
}

LEVEL_NAMES = {
    1: "Mt. Eternal (vertical descent)",
    2: "Mt. Eternal part 2",
    3: "Lake Fathom (underwater)",
    4: "Island of St. Nurari",
    5: "Lake Fathom part 2 (Peticopter)",
    6: "Village of Namui",
    7: "Mt. Kave",
    8: "The Blakwoods",
    9: "River (boat)",
    10: "Bingoo Lowland",
    11: "Radactian Castle",
    12: "City of Radactian",
    13: "Swamp (Peticopter, scrolls left)",
    14: "Kingdom of Nibana part 1",
    15: "Kingdom of Nibana part 2",
    16: "Janken's Castle",
    17: "Crag Lake (vertical)",
}

SCROLL_FLAG_NAMES = [(0x01, "down"), (0x02, "up"), (0x04, "left"), (0x08, "right"),
                     (0x10, "bit4"), (0x20, "drop_to_row1_first_screen"),
                     (0x40, "drop_to_row1_screen_x_div_4"), (0x80, "vertical/auto")]

SONG_NAMES = {0x82: "main theme", 0x83: "underwater", 0x84: "castle", 0x85: "bike",
              0x88: "peticopter"}
SPAWN_STATE_NAMES = {0: "on foot", 1: "riding the boat", 9: "flying the Peticopter"}

# Name-table word attribute byte (high byte).  Bits 0-4 are VDP bits; 5-7 are game flags.
ATTR_TILE_HI, ATTR_HFLIP, ATTR_VFLIP, ATTR_SPRITE_PALETTE, ATTR_PRIORITY = 0x01, 0x02, 0x04, 0x08, 0x10
ATTR_GAME_FLAGS = 0xE0
GAME_FLAG_CLASSES = {                # attribute bits 7-5 of one name-table word (see the spec)
    0x00: "passable",
    0x20: "water",
    0x40: "collectable: money bag tile (tile < $90) / shop item selector (tile >= $90)",
    0x60: "ladder (tile $3F) / shop door (tile >= $70) / deadly (any other tile)",
    0x80: "solid",
    0xA0: "solid floor trigger: tile $0D-$24 puzzle/ghost floor, $3F ladder top, > $3F enter-down hole",
    0xC0: "solid, breakable rock",
    0xE0: "solid, breakable box: tile 1-4 skull, 5-8 question, 9-12 star",
}


# --------------------------------------------------------------------------------------
# ROM access
# --------------------------------------------------------------------------------------
class Rom(object):
    def __init__(self, data):
        self.data = bytes(data)

    def offset(self, addr, bank=None):
        """ROM file offset of CPU address `addr` (slot 2 addresses need `bank`)."""
        if addr < 0x8000:
            return addr
        if bank is None:
            raise ValueError("slot-2 address %04X needs a bank" % addr)
        return (bank & 0x3F) * 0x4000 + (addr - 0x8000)

    def byte(self, addr, bank=None):
        return self.data[self.offset(addr, bank)]

    def word(self, addr, bank=None):
        o = self.offset(addr, bank)
        return self.data[o] | (self.data[o + 1] << 8)

    def bytes(self, addr, n, bank=None):
        o = self.offset(addr, bank)
        return self.data[o:o + n]


def h16(v):
    return "$%04X" % v


def h8(v):
    return "$%02X" % v


# --------------------------------------------------------------------------------------
# Screen RLE ("run/literal" byte stream, decoded by $6BE8 into v_decompressedLevelLayoutData)
# --------------------------------------------------------------------------------------
def rle_decode(data, start=0):
    """Returns (values, encoded_length, tokens). tokens: list of ('R', n) / ('L', n)."""
    out = []
    tokens = []
    o = start
    while True:
        b = data[o]
        if b == 0:
            return out, o + 1 - start, tokens
        if b & 0x80:
            n = b & 0x7F
            out.extend(data[o + 1:o + 1 + n])
            tokens.append(("L", n))
            o += 1 + n
        else:
            out.extend([data[o + 1]] * b)
            tokens.append(("R", b))
            o += 2


def _run_length(d, i):
    j = i
    while j < len(d) and d[j] == d[i] and j - i < 127:
        j += 1
    return j - i


def rle_encode(values):
    """Canonical encoder (reproduces 166 of the 174 original screens byte for byte).

    Runs of >= 3 are always run tokens.  A run of exactly 2 is a run token when no literal
    is pending, or when it is followed by another run of >= 2 or by the end of data;
    otherwise the two bytes join the pending literal.  Literals are split at 127 bytes.
    """
    d = list(values)
    out = bytearray()
    lit = []

    def flush():
        while lit:
            chunk = lit[:127]
            del lit[:127]
            out.append(0x80 | len(chunk))
            out.extend(chunk)

    i, n = 0, len(d)
    while i < n:
        r = _run_length(d, i)
        if r >= 3:
            flush()
            out.extend((r, d[i]))
            i += r
        elif r == 2:
            nxt = _run_length(d, i + 2) if i + 2 < n else 0
            if not lit or nxt >= 2 or i + 2 >= n:
                flush()
                out.extend((2, d[i]))
                i += 2
            else:
                lit.extend(d[i:i + 2])
                i += 2
        else:
            lit.append(d[i])
            i += 1
    flush()
    out.append(0)
    return bytes(out)


def rle_encode_tokens(values, tokens):
    """Re-encode following an explicit token list; returns None if it does not fit `values`."""
    d = list(values)
    out = bytearray()
    i = 0
    for kind, n in tokens:
        if n <= 0 or n > 127 or i + n > len(d):
            return None
        if kind == "R":
            if any(v != d[i] for v in d[i:i + n]):
                return None
            out.extend((n, d[i]))
        else:
            out.append(0x80 | n)
            out.extend(d[i:i + n])
        i += n
    if i != len(d):
        return None
    out.append(0)
    return bytes(out)


def encode_screen(screen):
    """Bytes for one screen: explicit tokenisation if recorded and still valid, else canonical."""
    toks = screen.get("rle_tokens")
    if toks:
        b = rle_encode_tokens(screen["metatiles"], [(t[0], int(t[1:])) for t in toks])
        if b is not None:
            return b
    return rle_encode(screen["metatiles"])


# --------------------------------------------------------------------------------------
# Metatiles
# --------------------------------------------------------------------------------------
def decode_word(w):
    hi = w >> 8
    return {
        "word": w,
        "tile": w & 0x1FF,
        "hflip": bool(hi & ATTR_HFLIP),
        "vflip": bool(hi & ATTR_VFLIP),
        "sprite_palette": bool(hi & ATTR_SPRITE_PALETTE),
        "priority": bool(hi & ATTR_PRIORITY),
        "game_flags": hi & ATTR_GAME_FLAGS,
    }


def load_metatile_table(rom, name):
    base = METATILE_TABLES[name]
    entries = []
    for i in range(256):
        ptr = rom.word(base + 2 * i, METATILE_BANK)
        raw = rom.bytes(ptr, 8, METATILE_BANK)
        words = [raw[2 * k] | (raw[2 * k + 1] << 8) for k in range(4)]
        flags = [w >> 8 & ATTR_GAME_FLAGS for w in words]
        entries.append({
            "id": i,
            "ptr": ptr,
            "rom_offset": rom.offset(ptr, METATILE_BANK),
            "words": words,                      # TL, TR, BL, BR
            "decoded": [decode_word(w) for w in words],
            "game_flags": flags,
            "word_kinds": [classify_word(w) for w in words],
            "class": classify_metatile(words),
        })
    # entries whose 8 bytes overlap another entry (pointer to a 1-byte stub): never used by a level
    starts = sorted(set(e["ptr"] for e in entries))
    for e in entries:
        nxt = [p for p in starts if e["ptr"] < p < e["ptr"] + 8]
        e["overlaps_next_entry"] = bool(nxt)
    return {"name": name, "table_ptr": base, "rom_offset": rom.offset(base, METATILE_BANK),
            "entries": entries}


def classify_word(w):
    """Behaviour of one name-table word, as decided by the collision/interaction code."""
    f, t = w >> 8 & ATTR_GAME_FLAGS, w & 0xFF
    if f == 0x00:
        return "empty" if (w & 0x1FF) == 0 else "background"
    if f == 0x20:
        return "water"
    if f == 0x40:
        return "money" if t < 0x90 else "shop_item"
    if f == 0x60:
        return "ladder" if t == 0x3F else ("shop_door" if t >= 0x70 else "deadly")
    if f == 0x80:
        return "solid"
    if f == 0xA0:
        if t < 0x0D:
            return "solid"
        if t <= 0x24:
            return "ghost_floor" if 0x1D <= t <= 0x20 else "puzzle_floor"
        if t == 0x3F:
            return "ladder_top"
        if t > 0x3F:
            return "enter_down"
        return "invalid"                 # $25-$3E overrun the floor jump table
    if f == 0xC0:
        return "breakable"
    # $E0: box; the type comes from the top-left word's tile
    if 1 <= t <= 4:
        return "skull_box"
    if 5 <= t <= 8:
        return "question_box"
    if 9 <= t <= 12:
        return "star_box"
    return "box"


def classify_metatile(words):
    kinds = [classify_word(w) for w in words]
    if all(k == "empty" for k in kinds):
        return "empty"
    kinds = ["background" if k == "empty" else k for k in kinds]
    if kinds[0].endswith("_box"):
        return kinds[0]                  # boxes are identified by their top-left word
    if len(set(kinds)) == 1:
        return kinds[0]
    if kinds[0] == kinds[1] and kinds[2] == kinds[3]:
        return "%s/%s" % (kinds[0], kinds[2])
    return "mixed(%s)" % ",".join(kinds)


# --------------------------------------------------------------------------------------
# Level descriptor, layout tables and screens (bank given by the descriptor, normally 6)
# --------------------------------------------------------------------------------------
def _read_ptr_table(rom, addr, bank, known):
    """Reads dw entries from `addr` until the next known structure start.  Every value read
    is added to `known`, so the last table before the screens stops at the first screen."""
    out = []
    pos = addr
    while True:
        if out and any(k > addr and pos >= k for k in known):
            return out
        v = rom.word(pos, bank)
        out.append(v)
        known.add(v)
        pos += 2


def load_level_layout(rom, lv, desc):
    bank = desc["bank"]
    rows_ptr, cols_ptr = desc["rows_ptr"], desc["cols_ptr"]
    # The level's tables are stored as: rows table, cols table (if different), row tables,
    # column tables, then the screens.  A table ends where the next known structure starts.
    known = set([rows_ptr, cols_ptr])
    top = {}
    for t in sorted(set([rows_ptr, cols_ptr])):
        top[t] = _read_ptr_table(rom, t, bank, known)
    sub = {}
    for s in sorted(set(v for t in top for v in top[t])):
        sub[s] = _read_ptr_table(rom, s, bank, known)
    screen_ptrs = sorted(set(v for s in sub for v in sub[s]))
    screens = []
    index_of = {}
    for p in screen_ptrs:
        vals, ln, toks = rle_decode(rom.data, rom.offset(p, bank))
        if len(vals) != SCREEN_CELLS:
            raise ValueError("level %d screen %04X decodes to %d bytes" % (lv, p, len(vals)))
        scr = {"ptr": p, "rom_offset": rom.offset(p, bank), "encoded_size": ln,
               "metatiles": vals}
        if rle_encode(vals) != rom.data[scr["rom_offset"]:scr["rom_offset"] + ln]:
            scr["rle_tokens"] = ["%s%d" % t for t in toks]
        index_of[p] = len(screens)
        screens.append(scr)
    layout = {
        "rows": [{"ptr": r, "screens": [index_of[p] for p in sub[r]]} for r in top[rows_ptr]],
        "cols": [{"ptr": c, "screens": [index_of[p] for p in sub[c]]} for c in top[cols_ptr]],
        "cols_is_rows": rows_ptr == cols_ptr,
    }
    return layout, screens


def load_descriptor(rom, lv):
    ptr = rom.word(LEVEL_DESCRIPTOR_TABLE + 2 * (lv - 1))
    d = rom.bytes(ptr, LEVEL_DESCRIPTOR_SIZE)
    mt = d[10] | (d[11] << 8)
    tname = [k for k, v in METATILE_TABLES.items() if v == mt]
    return {
        "ptr": ptr,
        "rom_offset": rom.offset(ptr),
        "bank_byte": d[0],
        "bank": d[0] & 0x3F,
        "rows_ptr": d[1] | (d[2] << 8),
        "cols_ptr": d[3] | (d[4] << 8),
        "start_screen_x": d[5],
        "start_screen_y": d[6],
        "width": d[7],
        "height": d[8],
        "scroll_flags": d[9],
        "scroll_flag_names": [n for bit, n in SCROLL_FLAG_NAMES if d[9] & bit],
        "metatile_table_ptr": mt,
        "metatile_table": tname[0] if tname else None,
    }


def level_kind(lv, desc):
    f = desc["scroll_flags"]
    if lv in (1, 17):
        return "vertical"
    if lv == 13:
        return "reverse"
    if f & 0x80:
        return "castle"
    if f & 0x60:
        return "drop"
    return "horizontal"


def derive_map(lv, desc, layout):
    """Physical placement of the reachable screens, with their entity-descriptor index.

    Returns a list of {"x", "y", "screen", "entity_index", "part"}.  The engine itself only
    knows the rows/cols tables; this reproduces how it walks them (see the spec)."""
    kind = level_kind(lv, desc)
    rows, cols = layout["rows"], layout["cols"]
    w, hgt = desc["width"], desc["height"]
    sx, sy = desc["start_screen_x"], desc["start_screen_y"]
    cells = []
    if kind == "horizontal":
        for i in range(w + 1):
            cells.append({"x": i, "y": 0, "screen": rows[sy]["screens"][i], "entity_index": i,
                          "part": "main"})
    elif kind == "reverse":
        for i in range(sx):
            cells.append({"x": i, "y": 0, "screen": rows[sy]["screens"][i], "entity_index": i,
                          "part": "main"})
    elif kind == "vertical":
        col = cols[sx - 1]["screens"]
        for v in range(hgt + 1):
            cells.append({"x": 0, "y": v, "screen": col[v], "entity_index": v, "part": "vertical"})
        if desc["scroll_flags"] & 0x80:      # level 1: continues to the right on row 0
            row = rows[0]["screens"]
            for i in range(1, w + 1):
                cells.append({"x": i, "y": hgt, "screen": row[i], "entity_index": hgt + i,
                              "part": "horizontal"})
    elif kind == "drop":
        for i in range(w + 1):
            cells.append({"x": i, "y": 0, "screen": rows[0]["screens"][i], "entity_index": i,
                          "part": "upper"})
        # bit 5: the drop lands on row 1 screen 0 (entity index 6) and updater $6574 sets
        # width 1; bit 6: row 1 screen x/4 (index $10 + x/4) and updater $6532 sets width 3.
        if desc["scroll_flags"] & 0x20:
            lower_w, first_idx = 1, 6
        else:
            lower_w, first_idx = 3, 16
        for i in range(lower_w + 1):
            cells.append({"x": i, "y": 1, "screen": rows[1]["screens"][i],
                          "entity_index": first_idx + i, "part": "lower"})
    elif kind == "castle":
        for r, row in enumerate(rows):
            for c, s in enumerate(row["screens"]):
                cells.append({"x": c, "y": r, "screen": s, "entity_index": r * w + c,
                              "part": "room"})
    return kind, cells


# --------------------------------------------------------------------------------------
# Entity descriptors (bank 2)
# --------------------------------------------------------------------------------------
def parse_entity_stream(rom, ptr):
    """Parses one per-screen entity descriptor.  Returns (records, length)."""
    recs = []
    pos = ptr
    while True:
        c = rom.byte(pos, ENTITY_BANK)
        if c == 0:
            recs.append({"kind": "end"})
            pos += 1
            break
        if c & 0x80:
            if c & 0x01:
                t, y, x, dat = rom.bytes(pos + 1, 4, ENTITY_BANK)
                recs.append({"kind": "extra_slot", "code": c, "type": t, "y": y, "x": x, "data": dat})
                pos += 5
            elif c & 0x02:
                recs.append({"kind": "octopus_arms", "code": c, "set": rom.byte(pos + 1, ENTITY_BANK)})
                pos += 2
            elif c & 0x04:
                t, y, x, dat = rom.bytes(pos + 1, 4, ENTITY_BANK)
                recs.append({"kind": "fixed_slot", "code": c, "type": t, "y": y, "x": x, "data": dat})
                pos += 5
            else:
                n = rom.byte(pos + 1, ENTITY_BANK)
                raw = list(rom.bytes(pos + 2, n, ENTITY_BANK))
                rec = {"kind": "ram_block", "code": c, "bytes": raw}
                if n >= 1 and n == 1 + 3 * raw[0]:
                    rec["patches"] = [{"addr": raw[1 + 3 * k] | (raw[2 + 3 * k] << 8),
                                       "value": raw[3 + 3 * k]} for k in range(raw[0])]
                recs.append(rec)
                pos += 2 + n
            continue
        n = c
        for k in range(n):
            t, y, x, dat = rom.bytes(pos + 1 + 4 * k, 4, ENTITY_BANK)
            recs.append({"kind": "entity", "type": t, "y": y, "x": x, "data": dat})
        pos += 1 + 4 * n
        break
    return recs, pos - ptr


def encode_entity_stream(recs):
    out = bytearray()
    ents = [r for r in recs if r["kind"] == "entity"]
    for r in recs:
        k = r["kind"]
        if k == "extra_slot":
            out.extend((r.get("code", 0x81), r["type"], r["y"], r["x"], r["data"]))
        elif k == "fixed_slot":
            out.extend((r.get("code", 0x84), r["type"], r["y"], r["x"], r["data"]))
        elif k == "octopus_arms":
            out.extend((r.get("code", 0x82), r["set"]))
        elif k == "ram_block":
            out.extend((r.get("code", 0x88), len(r["bytes"])))
            out.extend(r["bytes"])
    if ents:
        if len(ents) > 127:
            raise ValueError("too many entities in one screen")
        out.append(len(ents))
        for r in ents:
            out.extend((r["type"], r["y"], r["x"], r["data"]))
    else:
        out.append(0)
    return bytes(out)


def load_entities(rom, lv, next_table):
    table = rom.word(ENTITY_DESCRIPTOR_TABLE + 2 * (lv - 1), ENTITY_BANK)
    n = (next_table - table) // 2
    ptrs = [rom.word(table + 2 * i, ENTITY_BANK) for i in range(n)]
    streams = []
    for p in ptrs:
        recs, ln = parse_entity_stream(rom, p)
        streams.append({"ptr": p, "rom_offset": rom.offset(p, ENTITY_BANK), "size": ln, "records": recs})
    return {"table_ptr": table, "rom_offset": rom.offset(table, ENTITY_BANK), "screens": streams}


def entity_table_bounds(rom):
    """Start of each level's per-screen pointer table, plus the end of the last one."""
    starts = [rom.word(ENTITY_DESCRIPTOR_TABLE + 2 * i, ENTITY_BANK) for i in range(LEVEL_COUNT)]
    # The per-level tables are stored back to back; the last one ends where the first
    # descriptor stream begins (the lowest first entry of any table).
    first_stream = min(rom.word(s, ENTITY_BANK) for s in starts)
    order = sorted(starts)
    ends = {}
    for i, s in enumerate(order):
        ends[s] = order[i + 1] if i + 1 < len(order) else first_stream
    return starts, ends


# --------------------------------------------------------------------------------------
# Castle breakable-block tables (levels 11 and 16)
# --------------------------------------------------------------------------------------
def load_castle_blocks(rom, lv):
    addr, per_row = CASTLE_BLOCK_TABLES[lv]
    rows = []
    pos = addr
    while True:
        row = []
        for _ in range(per_row):
            n = rom.byte(pos, 2)
            row.append(list(rom.bytes(pos + 1, n, 2)))
            pos += 1 + n
        rows.append(row)
        if rom.byte(pos, 2) == 0xFF:
            pos += 1
            break
    return {"ptr": addr, "rom_offset": rom.offset(addr, 2), "rooms_per_row": per_row,
            "rows": rows, "size": pos - addr}


def encode_castle_blocks(cb):
    out = bytearray()
    for row in cb["rows"]:
        for cells in row:
            out.append(len(cells))
            out.extend(cells)
    out.append(0xFF)
    return bytes(out)


# --------------------------------------------------------------------------------------
# Per-level tables in banks 0/1/3/7
# --------------------------------------------------------------------------------------
def load_level_tables(rom, lv):
    i = lv - 1
    return {
        "start_x": rom.byte(STARTING_POSITIONS + 2 * i),
        "start_y": rom.byte(STARTING_POSITIONS + 2 * i + 1),
        "song": rom.byte(LEVEL_SONGS + i),
        "spawn_state": rom.byte(SPAWN_STATES + i),
        "question_box_index": rom.byte(QUESTION_BOX_INDEXES + i),
        "shop_door_offset": rom.byte(SHOP_DOORS + 3 * i),
        "shop_door_nametable_addr": rom.word(SHOP_DOORS + 3 * i + 1),
        "palette_ptr": rom.word(LEVEL_PALETTES + 2 * i),
        "palette": list(rom.bytes(rom.word(LEVEL_PALETTES + 2 * i), 32, PALETTE_BANK)),
        "main_tileset_ptr": rom.word(MAIN_TILESET_POINTERS + 2 * i, MAIN_TILESET_BANK),
        "tileset_loader": rom.word(TILESET_LOADERS + 2 * i),
        "sprite_tiles_loader": rom.word(SPRITE_TILES_LOADERS + 2 * i),
        "tile_updater": rom.word(TILE_UPDATERS + 2 * i),
        "palette_updater": rom.word(PALETTE_UPDATERS + 2 * i),
        "scroll_flags_updater": rom.word(SCROLL_FLAGS_UPDATERS + 2 * i),
        "entity_loader": rom.word(ENTITY_LOADERS + 2 * i),
        "camera_handler": rom.word(CAMERA_HANDLERS + 2 * i),
        "vehicle_crash_to_water": rom.byte(VEHICLE_CRASH_TO_WATER + i),
        "map_arrow_x": rom.byte(MAP_ARROW_POSITIONS + 2 * i),
        "map_arrow_y": rom.byte(MAP_ARROW_POSITIONS + 2 * i + 1),
        "shop_items_ptr": rom.word(SHOP_ITEM_TABLES + 2 * i),
        "shop_sold_ptr": rom.word(SHOP_SOLD_TABLES + 2 * i),
    }


def encode_level_tables(lv, t):
    i = lv - 1
    out = [
        (STARTING_POSITIONS + 2 * i, bytes((t["start_x"], t["start_y"]))),
        (LEVEL_SONGS + i, bytes((t["song"],))),
        (SPAWN_STATES + i, bytes((t["spawn_state"],))),
        (QUESTION_BOX_INDEXES + i, bytes((t["question_box_index"],))),
        (SHOP_DOORS + 3 * i, struct.pack("<BH", t["shop_door_offset"], t["shop_door_nametable_addr"])),
        (LEVEL_PALETTES + 2 * i, struct.pack("<H", t["palette_ptr"])),
        (0x4000 * MAIN_TILESET_BANK + MAIN_TILESET_POINTERS - 0x8000 + 2 * i,
         struct.pack("<H", t["main_tileset_ptr"])),
        (TILESET_LOADERS + 2 * i, struct.pack("<H", t["tileset_loader"])),
        (SPRITE_TILES_LOADERS + 2 * i, struct.pack("<H", t["sprite_tiles_loader"])),
        (TILE_UPDATERS + 2 * i, struct.pack("<H", t["tile_updater"])),
        (PALETTE_UPDATERS + 2 * i, struct.pack("<H", t["palette_updater"])),
        (SCROLL_FLAGS_UPDATERS + 2 * i, struct.pack("<H", t["scroll_flags_updater"])),
        (ENTITY_LOADERS + 2 * i, struct.pack("<H", t["entity_loader"])),
        (CAMERA_HANDLERS + 2 * i, struct.pack("<H", t["camera_handler"])),
        (VEHICLE_CRASH_TO_WATER + i, bytes((t["vehicle_crash_to_water"],))),
        (MAP_ARROW_POSITIONS + 2 * i, bytes((t["map_arrow_x"], t["map_arrow_y"]))),
        (SHOP_ITEM_TABLES + 2 * i, struct.pack("<H", t["shop_items_ptr"])),
        (SHOP_SOLD_TABLES + 2 * i, struct.pack("<H", t["shop_sold_ptr"])),
    ]
    return out


def load_globals(rom):
    arms = []
    for k in range(2):
        p = rom.word(OCTOPUS_ARM_SETS + 2 * k)
        segs = [dict(zip(("delay", "y", "x", "follower"), rom.bytes(p + 2 + 4 * j, 4))) for j in range(8)]
        arms.append({"ptr": p, "pot_nametable_addr": rom.word(p), "segments": segs})
    seq = []
    pos = FLOOR_PUZZLE_SEQUENCE
    while rom.byte(pos) != 0xFF:
        seq.append(rom.byte(pos))
        pos += 1
    return {
        "question_box_items": list(rom.bytes(QUESTION_BOX_ITEMS, 72)),
        "floor_puzzle_sequence": seq,
        "octopus_arm_sets": arms,
        "demo_levels": list(rom.bytes(DEMO_LEVELS, 4)),
    }


def encode_globals(g):
    out = [(QUESTION_BOX_ITEMS, bytes(g["question_box_items"])),
           (FLOOR_PUZZLE_SEQUENCE, bytes(g["floor_puzzle_sequence"] + [0xFF])),
           (DEMO_LEVELS, bytes(g["demo_levels"]))]
    for k, a in enumerate(g["octopus_arm_sets"]):
        out.append((OCTOPUS_ARM_SETS + 2 * k, struct.pack("<H", a["ptr"])))
        b = bytearray(struct.pack("<H", a["pot_nametable_addr"]))
        for sgm in a["segments"]:
            b.extend((sgm["delay"], sgm["y"], sgm["x"], sgm["follower"]))
        out.append((a["ptr"], bytes(b)))
    return out


# --------------------------------------------------------------------------------------
# Top level
# --------------------------------------------------------------------------------------
def load(rom_bytes):
    """Decodes the 17 levels into a JSON-serialisable dict."""
    rom = Rom(rom_bytes)
    model = {
        "rom": {"size": len(rom.data), "crc32": "%08X" % (zlib.crc32(rom.data) & 0xFFFFFFFF)},
        "metatile_tables": {k: load_metatile_table(rom, k) for k in sorted(METATILE_TABLES)},
        "globals": load_globals(rom),
        "levels": [],
    }
    starts, ends = entity_table_bounds(rom)
    for lv in range(1, LEVEL_COUNT + 1):
        desc = load_descriptor(rom, lv)
        layout, screens = load_level_layout(rom, lv, desc)
        kind, cells = derive_map(lv, desc, layout)
        level = {
            "number": lv,
            "name": LEVEL_NAMES[lv],
            "kind": kind,
            "descriptor": desc,
            "layout": layout,
            "screens": screens,
            "map": cells,
            "entities": load_entities(rom, lv, ends[starts[lv - 1]]),
            "tables": load_level_tables(rom, lv),
            "graphics": load_graphics(rom_bytes, rom, lv),
        }
        if lv in CASTLE_BLOCK_TABLES:
            level["castle_blocks"] = load_castle_blocks(rom, lv)
        model["levels"].append(level)
    for lv, sa in SUB_AREAS.items():
        owner = model["levels"][sa["layout_level"] - 1]
        row = owner["layout"]["rows"][0]["screens"]
        model["levels"][lv - 1]["sub_area"] = {
            "loader": sa["loader"], "layout_ptr": sa["layout_ptr"], "layout_level": sa["layout_level"],
            "graphics_level": sa["graphics_level"], "variants": sa["variants"],
            "cells": [{"x": i, "y": 0, "level": sa["layout_level"], "screen": row[k],
                       "row_index": k, "entity_index": sa["entity_index"][i]}
                      for i, k in enumerate(sa["screens"])],
        }
    return model


def _slot2_offset(addr, bank):
    return (bank & 0x3F) * 0x4000 + (addr - 0x8000)


def encode(model):
    """Produces [(rom_offset, bytes)] for every level structure described by `model`.

    Addresses (ptr fields) are honoured as they are in the model: the caller is responsible
    for choosing non-overlapping locations when data grows."""
    out = []
    lvls = model["levels"]
    out.append((LEVEL_DESCRIPTOR_TABLE, b"".join(struct.pack("<H", l["descriptor"]["ptr"]) for l in lvls)))
    out.append((_slot2_offset(ENTITY_DESCRIPTOR_TABLE, ENTITY_BANK),
                b"".join(struct.pack("<H", l["entities"]["table_ptr"]) for l in lvls)))
    for level in model["levels"]:
        lv = level["number"]
        d = level["descriptor"]
        bank = d["bank"]
        out.append((d["rom_offset"], struct.pack(
            "<BHHBBBBBH", d["bank_byte"], d["rows_ptr"], d["cols_ptr"], d["start_screen_x"],
            d["start_screen_y"], d["width"], d["height"], d["scroll_flags"], d["metatile_table_ptr"])))
        lay = level["layout"]
        scr = level["screens"]
        out.append((_slot2_offset(d["rows_ptr"], bank),
                    b"".join(struct.pack("<H", r["ptr"]) for r in lay["rows"])))
        if not lay["cols_is_rows"]:
            out.append((_slot2_offset(d["cols_ptr"], bank),
                        b"".join(struct.pack("<H", c["ptr"]) for c in lay["cols"])))
        seen = set()
        for tab in lay["rows"] + ([] if lay["cols_is_rows"] else lay["cols"]):
            if tab["ptr"] in seen:
                continue
            seen.add(tab["ptr"])
            out.append((_slot2_offset(tab["ptr"], bank),
                        b"".join(struct.pack("<H", scr[i]["ptr"]) for i in tab["screens"])))
        for s in scr:
            out.append((_slot2_offset(s["ptr"], bank), encode_screen(s)))
        ent = level["entities"]
        out.append((_slot2_offset(ent["table_ptr"], ENTITY_BANK),
                     b"".join(struct.pack("<H", s["ptr"]) for s in ent["screens"])))
        done = set()
        for s in ent["screens"]:
            if s["ptr"] in done:
                continue
            done.add(s["ptr"])
            out.append((_slot2_offset(s["ptr"], ENTITY_BANK), encode_entity_stream(s["records"])))
        out.extend(encode_level_tables(lv, level["tables"]))
        if "castle_blocks" in level:
            out.append((level["castle_blocks"]["rom_offset"], encode_castle_blocks(level["castle_blocks"])))
    out.extend(encode_globals(model["globals"]))
    for name, tab in model["metatile_tables"].items():
        out.append((tab["rom_offset"], b"".join(struct.pack("<H", e["ptr"]) for e in tab["entries"])))
        for e in tab["entries"]:
            out.append((e["rom_offset"], b"".join(struct.pack("<H", w) for w in e["words"])))
    return out


def header_checksum(rom_bytes):
    """SEGA header checksum ($7FFA): 16-bit sum of the ROM except the header at $7FF0-$7FFF,
    over the size given by the low nibble of $7FFF ($F = 128 KB here)."""
    sizes = {0xC: 0x8000, 0xE: 0x10000, 0xF: 0x20000, 0x0: 0x40000, 0x1: 0x80000, 0x2: 0x100000}
    end = sizes.get(rom_bytes[0x7FFF] & 0x0F, len(rom_bytes))
    return (sum(rom_bytes[0:0x7FF0]) + sum(rom_bytes[0x8000:end])) & 0xFFFF


def apply_patches(rom_bytes, patches, fix_checksum=True):
    """Returns a patched copy of the ROM (bytearray).  With fix_checksum the SEGA header
    checksum is recomputed (the export BIOS of real consoles checks it)."""
    out = bytearray(rom_bytes)
    for off, b in patches:
        if off + len(b) > len(out):
            out.extend(b"\xFF" * (off + len(b) - len(out)))
        out[off:off + len(b)] = b
    if fix_checksum:
        c = header_checksum(out)
        out[0x7FFA], out[0x7FFB] = c & 0xFF, c >> 8
    return out


# --------------------------------------------------------------------------------------
# VRAM contents of a level (tiles + palette), rebuilt from the ROM
# --------------------------------------------------------------------------------------
# Helper routines (bank 0) whose effect is reproduced natively.
FN_SET_VDP_ADDRESS = 0x0008          # rst $08: VDP address/register write from DE
FN_LOAD_ATH_POINTER = 0x0010         # rst $10: HL = word at HL + 2*A
FN_JUMP_TO_ATH_POINTER = 0x0020      # rst $20: jump to word at HL + 2*A
FN_MEMCPY_TO_VRAM = 0x0030           # rst $30: copy B bytes (0 = 256) from HL to VRAM DE
FN_COPY_BYTES_TO_VRAM = 0x0145       # copy BC bytes from HL to VRAM DE
FN_FILL_VRAM = 0x0184                # write L to BC bytes of VRAM from DE
FN_DECOMPRESS_TILES = 0x0293         # 4-plane RLE tiles from HL to VRAM DE
FN_COPY_MIRRORED = 0x02C5            # copy BC bytes from HL, bit-reversed, at the current VRAM address
RAM_V_LEVEL = 0xC023

# Tile animations (levelTileUpdatersPointers entries, run every 18 frames with bank 5 mapped):
# updater -> [(frame pointer table in bank 0, frame count, bytes per frame, VRAM address)]
# Frames are raw tile data in bank 5; 6-frame tables play 0,1,2,3,2,1.
TILE_ANIMATIONS = {
    0x15D2: [(0x1620, 6, 0x40, 0x1100)],                          # updateWaterTilesA: tiles 136-137
    0x15DF: [(0x162C, 6, 0x40, 0x08C0)],                          # updateSwampTiles: tiles 70-71
    0x15EC: [(0x1638, 4, 0x60, 0x09E0)],                          # updateLavaTilesA: tiles 79-81
    0x15F9: [(0x1640, 4, 0x60, 0x08A0), (0x1620, 6, 0x40, 0x1100)],  # updateWaterTilesB
    0x1612: [(0x1648, 4, 0x60, 0x0B40)],                          # updateLavaTilesB: tiles 90-92
    0x161F: [],                                                   # doNotUpdateTiles
}


def decompress_tiles(data, src, vram, dest):
    """decompressTilesToVram ($0293): 4 bitplanes, each an RLE stream written to every 4th
    byte (plane p starts at dest+p).  Control byte n: 0 = end of plane; bit 7 set = copy
    n&$7F literal bytes; else repeat the next byte n times.  Returns bytes consumed."""
    o = src
    for plane in range(4):
        d = dest + plane
        while True:
            c = data[o]
            o += 1
            if c == 0:
                break
            n = c & 0x7F
            if c & 0x80:
                for k in range(n):
                    vram[d & 0x3FFF] = data[o + k]
                    d += 4
                o += n
            else:
                for k in range(n):
                    vram[d & 0x3FFF] = data[o]
                    d += 4
                o += 1
    return o - src


class LoaderInterpreter(object):
    """Executes the straight-line tile/palette loader routines of the ROM (the per-level
    routines behind tilesetLoadersPointers / spriteTilesLoadersPointers) with a tiny Z80
    subset, reproducing their VRAM writes.  Raises on any unsupported opcode."""

    def __init__(self, rom, level, vram=None):
        self.rom = rom
        self.ram = {RAM_V_LEVEL: level}
        self.bank = 2
        self.vram = vram if vram is not None else bytearray(0x4000)
        self.vaddr = 0
        self.r = dict(a=0, b=0, c=0, d=0, e=0, h=0, l=0)
        self.log = []

    def mem(self, addr):
        if addr >= 0xC000:
            return self.ram.get(addr, 0)
        if addr >= 0x8000:
            return self.rom.data[self.rom.offset(addr, self.bank)]
        return self.rom.data[addr]

    def src_offset(self, addr):
        return self.rom.offset(addr, self.bank) if addr >= 0x8000 else addr

    def pair(self, hi, lo):
        return (self.r[hi] << 8) | self.r[lo]

    def setpair(self, hi, lo, v):
        self.r[hi], self.r[lo] = (v >> 8) & 0xFF, v & 0xFF

    def run(self, addr, depth=0):
        if depth > 16:
            raise RuntimeError("loader recursion too deep")
        pc = addr
        while True:
            if self.native(pc):
                return
            op = self.mem(pc)
            if op in (0x01, 0x11, 0x21):
                v = self.mem(pc + 1) | (self.mem(pc + 2) << 8)
                self.setpair(*{0x01: ("b", "c"), 0x11: ("d", "e"), 0x21: ("h", "l")}[op] + (v,))
                pc += 3
            elif op in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x3E):
                self.r["bcdehla"[(op - 0x06) // 8] if op != 0x3E else "a"] = self.mem(pc + 1)
                pc += 2
            elif op == 0x3A:
                self.r["a"] = self.mem(self.mem(pc + 1) | (self.mem(pc + 2) << 8))
                pc += 3
            elif op == 0x32:
                a = self.mem(pc + 1) | (self.mem(pc + 2) << 8)
                if a == 0xFFFF:
                    self.bank = self.r["a"] & 0x3F
                else:
                    self.ram[a] = self.r["a"]
                pc += 3
            elif op == 0xCD:
                self.run(self.mem(pc + 1) | (self.mem(pc + 2) << 8), depth + 1)
                pc += 3
            elif op == 0xC3:
                pc = self.mem(pc + 1) | (self.mem(pc + 2) << 8)
            elif op == 0xC9:
                return
            elif op in (0xCF, 0xD7, 0xE7, 0xF7):
                target = op - 0xC7
                if target == FN_JUMP_TO_ATH_POINTER:
                    pc = self.load_ath_pointer()
                    continue
                self.native(target)
                pc += 1
            else:
                raise RuntimeError("unsupported opcode %02X at %04X in loader" % (op, pc))

    def load_ath_pointer(self):
        hl = self.pair("h", "l") + 2 * self.r["a"]
        return self.mem(hl) | (self.mem(hl + 1) << 8)

    def native(self, pc):
        de, hl, bc = self.pair("d", "e"), self.pair("h", "l"), self.pair("b", "c")
        if pc == FN_SET_VDP_ADDRESS:
            self.vaddr = de & 0x3FFF
        elif pc == FN_LOAD_ATH_POINTER:
            self.setpair("h", "l", self.load_ath_pointer())
        elif pc == FN_MEMCPY_TO_VRAM:
            n = self.r["b"] or 256
            self.copy(hl, de & 0x3FFF, n, "raw")
        elif pc == FN_COPY_BYTES_TO_VRAM:
            self.copy(hl, de & 0x3FFF, bc or 0x10000, "raw")
        elif pc == FN_FILL_VRAM:
            n = bc or 0x10000
            for k in range(n):
                self.vram[(de + k) & 0x3FFF] = self.r["l"]
            self.log.append({"op": "fill", "vram": de & 0x3FFF, "size": n, "value": self.r["l"]})
        elif pc == FN_DECOMPRESS_TILES:
            n = decompress_tiles(self.rom.data, self.src_offset(hl), self.vram, de & 0x3FFF)
            self.log.append({"op": "rle_tiles", "src": hl, "bank": self.bank if hl >= 0x8000 else None,
                             "rom_offset": self.src_offset(hl), "vram": de & 0x3FFF, "size": n})
        elif pc == FN_COPY_MIRRORED:
            src = self.src_offset(hl)
            for k in range(bc):
                b = self.rom.data[src + k]
                self.vram[(self.vaddr + k) & 0x3FFF] = int("{:08b}".format(b)[::-1], 2)
            self.log.append({"op": "mirrored", "src": hl, "bank": self.bank if hl >= 0x8000 else None,
                             "rom_offset": src, "vram": self.vaddr, "size": bc})
            self.vaddr += bc
        else:
            return False
        return True

    def copy(self, src_addr, dest, n, kind):
        src = self.src_offset(src_addr)
        self.vram[dest:dest + n] = self.rom.data[src:src + n]
        self.log.append({"op": kind, "src": src_addr, "bank": self.bank if src_addr >= 0x8000 else None,
                         "rom_offset": src, "vram": dest, "size": n})
        self.vaddr = dest + n


# Fixed VRAM loads performed by initGameplayState ($0ABD) around the per-level loaders.
# (source CPU address, bank, VRAM address, size or None for RLE, mirrored-copy flag)
INIT_FIXED_LOADS_BEFORE = [
    (0xB0A9, 7, 0x21A0, 0x60, False),    # rice ball
    (0x9349, 7, 0x26C0, 0x100, False),   # money bags
]
INIT_BULLET_LOADS = [                    # only when levelSpawnStates[level] != 0
    (0x9B29, 7, 0x2200, 0x20, False),
    (0x9429, 7, 0x2220, 0x1C0, False),
]
INIT_FIXED_LOADS_MIDDLE = [
    (0xB2B1, 5, 0x1600, None, False),    # 4bpp text characters (RLE) -> tiles $B0..
    (0x8000, 3, 0x0020, 0x480, False),   # item boxes -> tiles 1..36
    (0x84A2, 3, 0x04A0, None, False),    # bag of gold coins + cloud (RLE, loadLevelTiles)
]
INIT_FIXED_LOADS_AFTER = [
    (0xAFC9, 7, 0x2400, 0xE0, False),    # ghost (right) ...
    (0xAFC9, 7, 0x24E0, 0xE0, True),     # ... and mirrored (left)
    (0xB191, 5, 0x25C0, 0x80, False),    # 1up
    (0xB0B1, 5, 0x2640, 0x60, False),    # power bracelet
    (0xB0F1, 5, 0x26A0, 0x20, True),
]


def load_graphics(rom_bytes, rom, lv):
    """References to everything the level loads into VRAM/CRAM (no pixel data)."""
    _, _, log = level_vram(rom_bytes, lv, with_log=True)
    upd = rom.word(TILE_UPDATERS + 2 * (lv - 1))
    anims = []
    for table, n, size, vram in TILE_ANIMATIONS.get(upd, []):
        anims.append({"frame_table": table, "frames": [rom.word(table + 2 * k) for k in range(n)],
                      "bank": METATILE_BANK, "size": size, "vram": vram,
                      "tiles": [vram // 32 + k for k in range(size // 32)]})
    return {
        "palette_ptr": rom.word(LEVEL_PALETTES + 2 * (lv - 1)),
        "palette_bank": PALETTE_BANK,
        "tile_loads": log,
        "background_loads": [e for e in log if e["vram"] < 0x2000],
        "tile_animations": anims,
    }


def level_vram(rom_bytes, lv, with_log=False):
    """Rebuilds the level's VRAM tiles and CRAM as initGameplayState leaves them
    (Alex's own tiles and animated-tile frames excepted)."""
    rom = Rom(rom_bytes)
    it = LoaderInterpreter(rom, lv)

    def fixed(loads):
        for addr, bank, dest, size, mirrored in loads:
            off = rom.offset(addr, bank)
            if size is None:
                n = decompress_tiles(rom.data, off, it.vram, dest)
                it.log.append({"op": "rle_tiles", "src": addr, "bank": bank, "rom_offset": off,
                               "vram": dest, "size": n})
            elif mirrored:
                for k in range(size):
                    it.vram[dest + k] = int("{:08b}".format(rom.data[off + k])[::-1], 2)
                it.log.append({"op": "mirrored", "src": addr, "bank": bank, "rom_offset": off,
                               "vram": dest, "size": size})
            else:
                it.vram[dest:dest + size] = rom.data[off:off + size]
                it.log.append({"op": "raw", "src": addr, "bank": bank, "rom_offset": off,
                               "vram": dest, "size": size})

    # loadLevelSpriteTiles: bank 7, spriteTilesLoadersPointers[level]
    it.bank = 7
    it.run(rom.word(SPRITE_TILES_LOADERS + 2 * (lv - 1)))
    fixed(INIT_FIXED_LOADS_BEFORE)
    if rom.byte(SPAWN_STATES + lv - 1):
        fixed(INIT_BULLET_LOADS)
    fixed(INIT_FIXED_LOADS_MIDDLE)
    # loadLevelTiles: bank 3 still mapped, tilesetLoadersPointers[level]
    it.bank = 3
    it.run(rom.word(TILESET_LOADERS + 2 * (lv - 1)))
    fixed(INIT_FIXED_LOADS_AFTER)
    pal = rom.word(LEVEL_PALETTES + 2 * (lv - 1))
    cram = list(rom.bytes(pal, 32, PALETTE_BANK))
    if with_log:
        return it.vram, cram, it.log
    return it.vram, cram


# --------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------
_FONT = {  # 3x5 glyphs for hex digits
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111", "A": "111101111101101", "B": "110101110101110",
    "C": "111100100100111", "D": "110101101101110", "E": "111100111100111", "F": "111100111100100",
}


def sms_rgb(v):
    return ((v & 3) * 85, (v >> 2 & 3) * 85, (v >> 4 & 3) * 85)


class Canvas(object):
    def __init__(self, w, h, bg=(40, 40, 40)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(bg) * (w * h))

    def put(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            o = (y * self.w + x) * 3
            self.px[o:o + 3] = bytes(rgb)

    def rect(self, x, y, w, h, rgb):
        for i in range(w):
            self.put(x + i, y, rgb)
            self.put(x + i, y + h - 1, rgb)
        for j in range(h):
            self.put(x, y + j, rgb)
            self.put(x + w - 1, y + j, rgb)

    def fill(self, x, y, w, h, rgb):
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, rgb)

    def text(self, x, y, s, rgb, bg=None):
        if bg is not None:
            self.fill(x - 1, y - 1, 4 * len(s) + 1, 7, bg)
        for k, ch in enumerate(s.upper()):
            g = _FONT.get(ch)
            if not g:
                continue
            for j in range(5):
                for i in range(3):
                    if g[j * 3 + i] == "1":
                        self.put(x + 4 * k + i, y + j, rgb)

    def scaled(self, k):
        if k == 1:
            return self
        c = Canvas(self.w * k, self.h * k)
        for y in range(c.h):
            src = (y // k) * self.w
            row = bytearray()
            for x in range(self.w):
                o = (src + x) * 3
                row.extend(self.px[o:o + 3] * k)
            c.px[y * c.w * 3:(y + 1) * c.w * 3] = row
        return c

    def save_ppm(self, path):
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (self.w, self.h))
            f.write(bytes(self.px))


def tile_pixels(vram, idx):
    """8x8 colour indices (0..15) of VRAM tile `idx` (4bpp planar, 32 bytes)."""
    base = (idx & 0x1FF) * 32
    rows = []
    for r in range(8):
        p0, p1, p2, p3 = vram[base + 4 * r:base + 4 * r + 4]
        rows.append([((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) |
                     (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3) for x in range(8)])
    return rows


def draw_word(canvas, vram, cram_rgb, word, x0, y0, cache):
    key = word & 0xFFF
    if key not in cache:
        pix = tile_pixels(vram, word & 0x1FF)
        hi = word >> 8
        pal = 16 if hi & ATTR_SPRITE_PALETTE else 0
        img = []
        for r in range(8):
            rr = 7 - r if hi & ATTR_VFLIP else r
            row = pix[rr]
            if hi & ATTR_HFLIP:
                row = row[::-1]
            img.append([cram_rgb[pal + c] for c in row])
        cache[key] = img
    img = cache[key]
    for r in range(8):
        y = y0 + r
        if 0 <= y < canvas.h:
            o = (y * canvas.w + x0) * 3
            canvas.px[o:o + 24] = b"".join(bytes(c) for c in img[r])


def render_screen(canvas, level, metatiles, vram, cram, screen_idx, x0, y0, cache):
    cram_rgb = [sms_rgb(v) for v in cram]
    scr = level["screens"][screen_idx]["metatiles"]
    for cy in range(SCREEN_H):
        for cx in range(SCREEN_W):
            e = metatiles["entries"][scr[cy * SCREEN_W + cx]]
            w = e["words"]
            px, py = x0 + cx * 16, y0 + cy * 16
            draw_word(canvas, vram, cram_rgb, w[0], px, py, cache)
            draw_word(canvas, vram, cram_rgb, w[1], px + 8, py, cache)
            draw_word(canvas, vram, cram_rgb, w[2], px, py + 8, cache)
            draw_word(canvas, vram, cram_rgb, w[3], px + 8, py + 8, cache)


def render_level(model, lv, vram, cram, entities=True, grid=True, sub_vram=None):
    """Draws the level map.  If the level has a sub-area and `sub_vram` = (vram, cram) is
    given, the sub-area screens are drawn on an extra row below, with their own graphics."""
    level = model["levels"][lv - 1]
    mt = model["metatile_tables"][level["descriptor"]["metatile_table"]]
    cells = [dict(c, level=lv) for c in level["map"]]
    maxy = max(c["y"] for c in cells) + 1
    if sub_vram is not None and "sub_area" in level:
        cells += [dict(c, y=maxy, sub=True) for c in level["sub_area"]["cells"]]
    maxx = max(c["x"] for c in cells) + 1
    maxy = max(c["y"] for c in cells) + 1
    sw, sh = SCREEN_W * 16, SCREEN_H * 16
    canvas = Canvas(maxx * sw, maxy * sh)
    caches = {False: {}, True: {}}
    ents = level["entities"]["screens"]
    for c in cells:
        x0, y0 = c["x"] * sw, c["y"] * sh
        sub = c.get("sub", False)
        v, cr = sub_vram if sub else (vram, cram)
        render_screen(canvas, model["levels"][c["level"] - 1], mt, v, cr, c["screen"], x0, y0, caches[sub])
        if grid:
            canvas.rect(x0, y0, sw, sh, (255, 255, 255))
            canvas.text(x0 + 3, y0 + 3, "%X" % c["screen"], (255, 255, 255), (0, 0, 0))
            canvas.text(x0 + 3, y0 + 10, "%X" % c["entity_index"], (255, 255, 0), (0, 0, 0))
        if entities and c["entity_index"] < len(ents):
            for r in ents[c["entity_index"]]["records"]:
                if "type" not in r:
                    continue
                col = {"entity": (255, 0, 255), "extra_slot": (0, 255, 255),
                       "fixed_slot": (255, 128, 0)}[r["kind"]]
                ex, ey = x0 + r["x"], y0 + r["y"]
                canvas.rect(ex - 8, ey - 8, 16, 16, col)
                canvas.text(ex - 7, ey - 7, "%02X" % r["type"], col, (0, 0, 0))
    return canvas


def read_vram_dump(path):
    """Harness dump: 16 KB VRAM + 32 bytes CRAM (+ optional 8 KB RAM)."""
    d = open(path, "rb").read()
    return bytearray(d[:0x4000]), list(d[0x4000:0x4020])


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------
def _read_rom(path):
    data = open(path, "rb").read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    if crc != ROM_CRC32:
        sys.stderr.write("warning: ROM CRC32 %08X is not the expected %08X (rev 0)\n" % (crc, ROM_CRC32))
    return data


def _strip_tokens(model):
    m = json.loads(json.dumps(model))
    for level in m["levels"]:
        for scr in level["screens"]:
            scr.pop("rle_tokens", None)
    return m


# Level-data regions whose every byte should be produced by encode() (for the coverage report)
LEVEL_DATA_REGIONS = [
    ("level descriptors (bank 1)", 0x66CF, 0x66CF + 34 + 17 * LEVEL_DESCRIPTOR_SIZE),
    ("layouts + screens (bank 6)", 0x18000, None),
    ("entity descriptors (bank 2)", 0xB505, None),
    ("metatile tables (bank 5)", 0x14000, None),
]


def cmd_verify(rom_bytes):
    """Decode -> JSON -> encode -> compare with the ROM.  Returns True on success."""
    ok = True
    model = json.loads(json.dumps(load(rom_bytes)))    # the model must be plain JSON data
    patches = encode(model)
    bad = [(off, len(b)) for off, b in patches if rom_bytes[off:off + len(b)] != b]
    for off, n in bad:
        print("MISMATCH: %d bytes at ROM %05X differ" % (n, off))
    ok &= not bad
    # which part of encode() output belongs to which level (for the per-level summary)
    print("encode(): %d blocks, %d bytes, %d mismatching blocks" % (
        len(patches), sum(len(b) for _, b in patches), len(bad)))
    total_screens = canonical = 0
    for level in model["levels"]:
        lv = level["number"]
        scr = level["screens"]
        n_can = sum(1 for x in scr if not x.get("rle_tokens"))
        total_screens += len(scr)
        canonical += n_can
        ents = level["entities"]["screens"]
        n_rec = sum(1 for e in ents for r in e["records"] if r["kind"] != "end")
        print("level %2d %-34s descriptor+layout ok, %2d screens (%2d canonical RLE), "
              "%2d entity streams / %3d records%s" % (
                  lv, level["name"], len(scr), n_can, len(ents), n_rec,
                  ", castle block table ok" if "castle_blocks" in level else ""))
    print("RLE: %d/%d screens re-encoded byte-identically by the canonical encoder; the other %d "
          "are reproduced through their recorded tokenisation (rle_tokens)." % (
              canonical, total_screens, total_screens - canonical))
    # Canonical encoder alone: decode(encode(x)) == x, in place, and size <= original
    plain = _strip_tokens(model)
    patched = bytearray(rom_bytes)
    worse = []
    for level in plain["levels"]:
        for scr in level["screens"]:
            b = rle_encode(scr["metatiles"])
            if len(b) > scr["encoded_size"]:
                worse.append((level["number"], scr["ptr"], len(b), scr["encoded_size"]))
            else:
                patched[scr["rom_offset"]:scr["rom_offset"] + len(b)] = b
    reloaded = load(bytes(patched))
    diff_screens = 0
    for a, b in zip(reloaded["levels"], model["levels"]):
        for sa, sb in zip(a["screens"], b["screens"]):
            if sa["metatiles"] != sb["metatiles"]:
                diff_screens += 1
    for lvn, ptr, n, orig in worse:
        print("  canonical encoding of level %d screen %04X is larger (%d > %d)" % (lvn, ptr, n, orig))
    non = [(l["number"], x["ptr"], len(rle_encode(x["metatiles"])), x["encoded_size"])
           for l in model["levels"] for x in l["screens"] if x.get("rle_tokens")]
    print("  canonical-only re-encoding: decode(encode(x)) == x for all %d screens: %s; "
          "size <= original for all: %s" % (total_screens, "yes" if diff_screens == 0 else
                                            "NO (%d differ)" % diff_screens, "yes" if not worse else "NO"))
    print("  non-canonical originals (level, screen, canonical size / original size): " +
          ", ".join("L%d %04X %d/%d" % t for t in non))
    ok &= diff_screens == 0 and not worse
    # coverage of the level data regions
    cov = bytearray(len(rom_bytes))
    for off, b in patches:
        for i in range(off, off + len(b)):
            cov[i] = 1
    print("coverage of level-data regions (bytes not produced by encode()):")
    ends = {
        "layouts + screens (bank 6)": max(x["rom_offset"] + x["encoded_size"]
                                          for l in model["levels"] for x in l["screens"]),
        "entity descriptors (bank 2)": max(e["rom_offset"] + e["size"] for l in model["levels"]
                                           for e in l["entities"]["screens"]),
        "metatile tables (bank 5)": max(e["rom_offset"] + 8 for t in model["metatile_tables"].values()
                                        for e in t["entries"]),
    }
    for name, lo, hi in LEVEL_DATA_REGIONS:
        hi = hi if hi is not None else ends[name]
        holes = []
        i = lo
        while i < hi:
            if not cov[i]:
                j = i
                while j < hi and not cov[j]:
                    j += 1
                holes.append("%05X+%d" % (i, j - i))
                i = j
            else:
                i += 1
        print("  %-30s %05X-%05X: %s" % (name, lo, hi - 1, ", ".join(holes) if holes else "fully covered"))
    print("VERIFY %s" % ("PASSED" if ok else "FAILED"))
    return ok


def _entity_record_text(r):
    k = r["kind"]
    if "type" in r:
        prefix = {"entity": "", "extra_slot": "[$81 slot 28/29] ", "fixed_slot": "[$84 slot 5] "}[k]
        return "%s%s y=%s x=%s data=%s" % (prefix, entity_type_name(r["type"]), h8(r["y"]), h8(r["x"]),
                                           h8(r["data"]))
    if k == "octopus_arms":
        return "[$82] octopus arms, set %d" % r["set"]
    if k == "ram_block":
        if "patches" in r:
            return "[$%02X] name-table patches: %s" % (r["code"], ", ".join(
                "%s<-mt %s" % (h16(p["addr"]), h8(p["value"])) for p in r["patches"]))
        return "[$%02X] RAM block: %s" % (r["code"], " ".join("%02X" % b for b in r["bytes"]))
    return k


def cmd_dump(model, lv, show_metatiles=True):
    level = model["levels"][lv - 1]
    d = level["descriptor"]
    t = level["tables"]
    print("Level %d: %s  [%s]" % (lv, level["name"], level["kind"]))
    print("  descriptor @%s: bank %s, rows %s, cols %s, start screen (%d,%d), width %d, height %d,"
          " scroll flags %s (%s), metatile table %s (%s)" % (
              h16(d["ptr"]), h8(d["bank_byte"]), h16(d["rows_ptr"]), h16(d["cols_ptr"]),
              d["start_screen_x"], d["start_screen_y"], d["width"], d["height"], h8(d["scroll_flags"]),
              ",".join(d["scroll_flag_names"]), d["metatile_table"], h16(d["metatile_table_ptr"])))
    print("  Alex start x=%s y=%s, song %s (%s), spawn %s, ?-box index %d, shop door x=%s at %s" % (
        h8(t["start_x"]), h8(t["start_y"]), h8(t["song"]), SONG_NAMES.get(t["song"], "?"),
        SPAWN_STATE_NAMES.get(t["spawn_state"], t["spawn_state"]), t["question_box_index"],
        h8(t["shop_door_offset"]), h16(t["shop_door_nametable_addr"])))
    print("  palette %s, main tileset %s; code: tileset loader %s, sprite loader %s, tile updater %s,"
          " palette updater %s, scroll updater %s, entity loader %s, camera %s" % (
              h16(t["palette_ptr"]), h16(t["main_tileset_ptr"]), h16(t["tileset_loader"]),
              h16(t["sprite_tiles_loader"]), h16(t["tile_updater"]), h16(t["palette_updater"]),
              h16(t["scroll_flags_updater"]), h16(t["entity_loader"]), h16(t["camera_handler"])))
    lay = level["layout"]
    print("  layout tables (entries are screen numbers):")
    for i, r in enumerate(lay["rows"]):
        print("    rows[%d] @%s: %s" % (i, h16(r["ptr"]), " ".join("%2d" % k for k in r["screens"])))
    if lay["cols_is_rows"]:
        print("    cols table = rows table")
    else:
        for i, c in enumerate(lay["cols"]):
            print("    cols[%d] @%s: %s" % (i, h16(c["ptr"]), " ".join("%2d" % k for k in c["screens"])))
    print("  screens: " + ", ".join("%d @%s (%d B%s)" % (i, h16(x["ptr"]), x["encoded_size"],
                                                         ", non-canonical RLE" if x.get("rle_tokens") else "")
                                    for i, x in enumerate(level["screens"])))
    print("  map (cells are screen/entity index):")
    maxx = max(c["x"] for c in level["map"]) + 1
    maxy = max(c["y"] for c in level["map"]) + 1
    grid = {(c["x"], c["y"]): c for c in level["map"]}
    for y in range(maxy):
        line = []
        for x in range(maxx):
            c = grid.get((x, y))
            line.append("  .  " if c is None else "%2d/%-2d" % (c["screen"], c["entity_index"]))
        print("    " + " ".join(line))
    if "sub_area" in level:
        sa = level["sub_area"]
        print("  sub-area (entity $4C, loader %s): layout %s of level %d, graphics of level %d: %s" % (
            h16(sa["loader"]), h16(sa["layout_ptr"]), sa["layout_level"], sa["graphics_level"],
            ", ".join("screen %d/entity %d" % (c["screen"], c["entity_index"]) for c in sa["cells"])))
    print("  entities (per entity index):")
    for i, st in enumerate(level["entities"]["screens"]):
        recs = [_entity_record_text(r) for r in st["records"] if r["kind"] != "end"]
        print("    %2d @%s: %s" % (i, h16(st["ptr"]), "; ".join(recs) if recs else "-"))
    if "castle_blocks" in level:
        cb = level["castle_blocks"]
        print("  castle breakable-block table @%s (%d rooms/row):" % (h16(cb["ptr"]), cb["rooms_per_row"]))
        for r, row in enumerate(cb["rows"]):
            print("    row %d: %s" % (r, " | ".join(" ".join("%02X" % p for p in cells) or "-" for cells in row)))
    if show_metatiles:
        for i, x in enumerate(level["screens"]):
            print("  screen %d metatile ids:" % i)
            for y in range(SCREEN_H):
                print("    " + " ".join("%02X" % v for v in x["metatiles"][y * SCREEN_W:(y + 1) * SCREEN_W]))


def entity_type_name(t):
    n = ENTITY_TYPES.get(t)
    return "%s[%s]" % (n[0], h8(t)) if n else h8(t)


# Entity types (entityTypeJumpTable at $2892, index = type - 1).  (name, updater address,
# placed by level descriptors?, meaning of the data byte for placed types)
ENTITY_TYPES = {
    0x01: ("alex", 0x2958, False, ""),
    0x02: ("vehicle_bullet", 0x4489, False, ""),
    0x03: ("vehicle_explosion", 0x443F, False, ""),
    0x04: ("bullet_impact", 0x44CD, False, ""),
    0x05: ("capsule_a_thrown", 0x4689, False, ""),
    0x06: ("capsule_a_hatched", 0x46C2, False, ""),
    0x07: ("capsule_b_thrown", 0x4719, False, ""),
    0x08: ("capsule_b_barrier", 0x4885, False, ""),
    0x09: ("capsule_a_helper", 0x4768, False, ""),
    0x0A: ("capsule_a_helper2", 0x4863, False, ""),
    0x0B: ("janken_thought_cloud", 0x761F, False, ""),
    0x0C: ("battle_sprite_helper", 0x7982, False, ""),
    0x0D: ("gooseka_head", 0x799A, False, ""),
    0x0E: ("chokkinna_head", 0x7A89, False, ""),
    0x0F: ("parplin_head", 0x7B2E, False, ""),
    0x10: ("spiked_pillar_a", 0x49EB, True, "initial delay (frames)"),
    0x11: ("spiked_pillar_b", 0x4A26, True, "initial delay (frames)"),
    0x12: ("spiked_pillar_c", 0x4A32, True, "initial delay (frames)"),
    0x13: ("spiked_pillar_d", 0x4A3E, True, "initial delay (frames)"),
    0x14: ("spiked_pillar_active", 0x497D, False, ""),
    0x15: ("spiked_ceiling_band", 0x4B1C, True, "band width in tiles"),
    0x16: ("collapsing_floor", 0x4A4A, True, "hole half-width in tiles"),
    0x17: ("collapsing_floor_punch", 0x4AE7, True, "hole half-width in tiles"),
    0x18: ("static_sprite", 0x0966, False, ""),
    0x19: ("janken_projectile", 0x74C7, False, ""),
    0x1A: ("chokkinna_spell", 0x789E, False, ""),
    0x1B: ("bracelet_shockwave", 0x4914, False, ""),
    0x1C: ("janken_the_great", 0x7143, True, "opponent id (see spec)"),
    0x1D: ("gooseka", 0x778F, True, "opponent id: 2 = first fight, 3 = rematch"),
    0x1E: ("chokkinna", 0x780F, True, "opponent id: 4 = first fight, 5 = rematch"),
    0x1F: ("parplin", 0x78A1, True, "opponent id: 6 = first fight, 7 = rematch"),
    0x20: ("bat_left", 0x4EE8, True, "ignored"),
    0x21: ("item_select_arrow", 0x2439, False, ""),
    0x22: ("merman_bubbles", 0x4E96, False, ""),
    0x23: ("merman", 0x4E29, True, "hits already taken (dies at 3)"),
    0x24: ("octopus_arm", 0x4C27, False, "(placed only through $82 records)"),
    0x25: ("blakwoods_bear", 0x52E0, True, "ignored"),
    0x26: ("blakwoods_bear_walk_right", 0x5359, False, ""),
    0x27: ("blakwoods_bear_attack_left", 0x53C8, False, ""),
    0x28: ("blakwoods_bear_attack_right", 0x544A, False, ""),
    0x29: ("monkey_leaf", 0x55EC, False, ""),
    0x2A: ("monkey", 0x5573, True, "ignored"),
    0x2B: ("smoke_puff", 0x567D, False, ""),
    0x2C: ("plant", 0x4FEA, True, "ignored"),
    0x2D: ("monster_bird_left", 0x5030, True, "ignored"),
    0x2E: ("killer_fish_left", 0x5158, True, "ignored"),
    0x2F: ("monster_frog", 0x56C5, True, "ignored"),
    0x30: ("small_fish_left", 0x50DA, True, "ignored"),
    0x31: ("sea_horse_left", 0x57C7, True, "overwritten with its base y"),
    0x32: ("sea_horse_right", 0x587C, False, ""),
    0x33: ("monster_bird_right", 0x5081, True, "ignored"),
    0x34: ("small_fish_right", 0x512B, False, ""),
    0x35: ("killer_fish_right", 0x51EC, False, ""),
    0x36: ("bat_right", 0x4F7B, False, ""),
    0x37: ("monster_frog_jumping", 0x571C, False, ""),
    0x38: ("debris_top_left", 0x5901, False, ""),
    0x39: ("debris_bottom_left", 0x598F, False, ""),
    0x3A: ("debris_top_right", 0x59C1, False, ""),
    0x3B: ("debris_bottom_right", 0x59F4, False, ""),
    0x3C: ("money_bag", 0x5A2A, False, ""),
    0x3D: ("circular_flame", 0x5D8D, True, "sub-pixel accumulator (ignored)"),
    0x3E: ("scorpion_or_flame_left", 0x5E0D, True, "0 = scorpion (killable), else walking flame"),
    0x3F: ("scorpion_or_flame_right", 0x5E74, False, ""),
    0x40: ("storm_cloud", 0x5EB3, True, "ignored"),
    0x41: ("lightning", 0x5EFE, False, ""),
    0x42: ("leaping_fish", 0x5F45, True, "ignored (always at y=$BF)"),
    0x43: ("boss_defeat_smoke", 0x5622, False, ""),
    0x44: ("rice_ball", 0x5BCA, True, "ignored (ends the level)"),
    0x45: ("saint_nurari", 0x60B5, True, "ignored"),
    0x46: ("namui_bull", 0x5C2F, True, "ignored"),
    0x47: ("namui_bull_state2", 0x5CA9, False, ""),
    0x48: ("namui_bull_state3", 0x5CF0, False, ""),
    0x49: ("namui_bull_state4", 0x5D2F, False, ""),
    0x4A: ("blakwoods_bear_hurt", 0x550E, False, ""),
    0x4B: ("nametable_changer", 0x61C6, True, "1 = punch counter, 2 = on touch, else on punch"),
    0x4C: ("sub_area_trigger", 0x6279, True, "value stored in $C07F (sub-area variant)"),
    0x4D: ("extra_life", 0x5A8F, False, ""),
    0x4E: ("power_bracelet", 0x5ADF, False, ""),
    0x4F: ("ghost", 0x5B30, False, ""),
    0x50: ("village_elder", 0x6077, True, "ignored"),
    0x51: ("captive", 0x5FAA, True, "0 = Princess Lora, else Egle"),
    0x52: ("special_item", 0x6106, True, "item id 0..9 (0 crown, 1 telepathy ball, 2 letter, 3 Hirotta "
                                         "stone, 4 moonlight stone, 5 extra life, 6 bracelet, 7 teleport "
                                         "powder, 8 sun stone)"),
    0x53: ("king_of_nibana", 0x616F, True, "ignored"),
    0x54: ("ground_walker", 0x62A8, True, "ignored"),
    0x55: ("hopping_walker", 0x6361, True, "ignored"),
    0x56: ("map_arrow", 0x1B41, False, ""),
    0x57: ("static_flame", 0x63F4, True, "ignored"),
    0x58: ("map_jankens_castle", 0x1B8E, False, ""),
    0x60: ("cragg_lake_final_room", 0x3E28, False, ""),
    0x61: ("cragg_lake_room_variant", 0x3EBA, False, ""),
    0x62: ("alex_eating_rice_ball", 0x39DB, False, ""),
    0x63: ("punch_target_cc08", 0x3EFC, True, "ignored"),
}


def main(argv):
    args = list(argv[1:])
    rom_path = DEFAULT_ROM
    opts = {}
    pos = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--rom":
            rom_path = args[i + 1]
            i += 2
        elif a in ("--vram", "--scale"):
            opts[a] = args[i + 1]
            i += 2
        elif a.startswith("--"):
            opts[a] = True
            i += 1
        else:
            pos.append(a)
            i += 1
    if not pos:
        print(__doc__)
        return 2
    rom_bytes = _read_rom(rom_path)
    cmd = pos[0]
    if cmd == "verify":
        return 0 if cmd_verify(rom_bytes) else 1
    model = load(rom_bytes)
    if cmd == "dump":
        cmd_dump(model, int(pos[1]), show_metatiles="--no-metatiles" not in opts)
        return 0
    if cmd == "json":
        if len(pos) > 1:
            json.dump(model["levels"][int(pos[1]) - 1], sys.stdout, indent=1)
        else:
            json.dump(model, sys.stdout, indent=1)
        sys.stdout.write("\n")
        return 0
    if cmd == "render":
        lv = int(pos[1])
        if "--vram" in opts:
            vram, cram = read_vram_dump(opts["--vram"])
        else:
            vram, cram = level_vram(rom_bytes, lv)
        sub = None
        if "sub_area" in model["levels"][lv - 1]:
            sub = level_vram(rom_bytes, model["levels"][lv - 1]["sub_area"]["graphics_level"])
        c = render_level(model, lv, vram, cram, entities="--no-entities" not in opts,
                         grid="--no-grid" not in opts, sub_vram=sub)
        c.scaled(int(opts.get("--scale", 1))).save_ppm(pos[2])
        print("wrote %s (%dx%d)" % (pos[2], c.w, c.h))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
