"""Bridge between Super Alex Kidd Maker (maker/) and the game.

    python3 maker/tools/leveledit.py list ROM              levels as JSON [{level, name}]
    python3 maker/tools/leveledit.py export ROM N          editor model of level N (JSON)
    python3 maker/tools/leveledit.py build ROM MODDIR OUT [--start LEVEL:COLUMN]
                                                     compile MODDIR/level_NN.json into a mod patch
                                                     (--start: test build beginning at that screen)
    python3 maker/tools/leveledit.py icontypes ROM         "type:level" pairs for engine/build/entityicons
    python3 maker/tools/leveledit.py retheme ROM MODEL.json T   convert an editor model to level T's graphics

Editor model (one level):
    level, name, kind, columns, rows
    grid[row][col] = {"screen": s, "entities": e} or null   screen s, entity index e
    screens[s] = {"blocks": [192 metatile ids]}              shared by every cell using s
    entities[e] = [{"type", "x", "y", "data"}]               normal entities of index e
    specials[e] = [records]                                  special records, kept as they are
    metatileTable, metatiles[256] = [TL, TR, BL, BR words], metatileClasses[256]
    entityTypes = [{"id", "name"}]

Building a mod:
  * every edited level gets its own ROM bank (8 + level - 1, in a 512 KB ROM): its layout
    tables and screens are re-encoded there and its descriptor points to them, so screens
    may grow freely. The sub-area pointers hard-coded in the loader at $1735 are patched
    when level 3 or 17 moves (level 3's sub-area reads level 4's layout);
  * entity data must stay in bank 2 (hard-coded): all per-level tables and streams are
    repacked into $B527-$BFFF (identical streams shared), so levels may gain screens;
  * the output only contains bytes that differ from the user's ROM, in the AKMOD1 format
    read by the engine (engine/src/rt/mod.h).
"""
import json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levels
import parts

MOD_ROM_SIZE = 0x80000          # 512 KB: banks 8.. hold relocated levels
FIRST_LEVEL_BANK = 8
ENTITY_REGION = (0xB527, 0xC000)  # bank 2: per-level entity tables, streams, free space
SUB_AREA_PATCHES = {
    3: {"operands": (0x177E, 0x1781), "layout_level": 4},   # ld hl,$8AD6 / ld de,$8AD6
    17: {"operands": (0x1790, 0x1793), "layout_level": 17},  # ld hl,$BC53 / ld de,$BC53
}


def _read_rom(path):
    return open(path, "rb").read()


FRENCH_NAMES = {
    0x10: "Pilier à pointes A", 0x11: "Pilier à pointes B", 0x12: "Pilier à pointes C",
    0x13: "Pilier à pointes D", 0x15: "Plafond à pointes", 0x16: "Sol qui s'effondre",
    0x17: "Sol qui s'effondre (au poing)", 0x20: "Chauve-souris", 0x23: "Homme-poisson",
    0x25: "Ours des Blakwoods", 0x2A: "Singe", 0x2C: "Plante carnivore", 0x2D: "Oiseau monstre (gauche)",
    0x2E: "Poisson tueur (gauche)", 0x2F: "Grenouille monstre", 0x30: "Petit poisson (gauche)",
    0x31: "Hippocampe (gauche)", 0x33: "Oiseau monstre (droite)", 0x3D: "Flamme circulaire",
    0x3E: "Scorpion / flamme (gauche)", 0x40: "Nuage d'orage", 0x42: "Poisson sauteur",
    0x45: "Saint Nurari", 0x46: "Taureau de Namui", 0x50: "Vieux du village", 0x51: "Prisonnier",
    0x52: "Objet spécial", 0x53: "Roi de Nibana", 0x54: "Marcheur", 0x55: "Sauteur",
    0x57: "Flamme fixe", 0x63: "Cible (coup de poing)",
}


def entity_type_list(model):
    """Entity types that the original levels place as normal entities (safe to place),
    with the levels using them."""
    used = {}
    for level in model["levels"]:
        for stream in level["entities"]["screens"]:
            for r in stream["records"]:
                if r["kind"] == "entity":
                    used.setdefault(r["type"], set()).add(level["number"])
    out = []
    for t in sorted(used):
        name = levels.entity_type_name(t)
        base = name.split("[")[0].replace("_", " ").strip() if not name.startswith("$") else "Objet $%02X" % t
        base = FRENCH_NAMES.get(t, base[:1].upper() + base[1:])
        out.append({"id": t, "name": base, "levels": sorted(used[t])})
    return out


SPECIAL_NAMES = {0x44: "Boule de riz (fin du niveau)", 0x1D: "Boss", 0x1E: "Boss", 0x1F: "Boss",
                 0x4B: "Changeur de décor", 0x4C: "Entrée de zone bonus"}


def special_type_name(t):
    if t in SPECIAL_NAMES:
        return SPECIAL_NAMES[t]
    name = levels.entity_type_name(t)
    return name.split("[")[0].replace("_", " ") if not name.startswith("$") else "Objet $%02X" % t


SURPRISE_ITEMS = [(0x4D, "Vie supplémentaire (1up)"), (0x4E, "Bracelet de puissance"), (0x4F, "Fantôme (piège)")]
# Per-level tables that make up a level's look and sound (the rest is gameplay).
# Vehicles a level can start on: spawn state and song.
VEHICLES = {"bike": {"spawn": 7, "song": 0x85}, "boat": {"spawn": 1, "song": 0}, "peticopter": {"spawn": 9, "song": 0x88}}

THEME_TABLES = ("palette_ptr", "palette", "main_tileset_ptr", "tileset_loader", "sprite_tiles_loader",
                "tile_updater", "palette_updater")


def retheme(model, ed, theme):
    """Converts an editor model to the graphics of level `theme`: terrain stays
    terrain (re-tiled), boxes stay boxes, water stays water, the rest becomes the
    new theme's background."""
    target = model["levels"][theme - 1]
    tp = parts.learn_parts(model, target)
    tclasses = [e.get("class", "") for e in model["metatile_tables"][target["descriptor"]["metatile_table"]]["entries"]]
    old = ed["parts"]
    family = {}
    for i, t in enumerate(old["terrains"]):
        for m in t["members"]:
            family[m] = i
    kinds = {b["metatile"]: b["kind"] for b in old["blocks"]}
    new_block = {b["kind"]: b["metatile"] for b in tp["blocks"]}
    oclasses = ed["metatileClasses"]
    nterr = len(tp["terrains"])

    def convert(m):
        if m in family and nterr:
            return tp["terrains"][min(family[m], nterr - 1)]["fill"]
        k = kinds.get(m) or oclasses[m]
        if k in new_block:
            return new_block[k]
        if "solid" in oclasses[m] and nterr:
            return tp["terrains"][0]["fill"]
        return tp["eraser"]

    for scr in ed["screens"]:
        scr["blocks"] = [convert(m) for m in scr["blocks"]]
    # Re-tile every terrain cell of the map with the new theme's tables.
    tfam = {}
    for i, t in enumerate(tp["terrains"]):
        for m in t["members"]:
            tfam[m] = i
    rows, cols = ed["rows"], ed["columns"]

    def at(gx, gy):
        if gx < 0 or gy < 0 or gx >= cols * 16 or gy >= rows * 12:
            return None
        cell = ed["grid"][gy // 12][gx // 16]
        return None if cell is None else ed["screens"][cell["screen"]]["blocks"][(gy % 12) * 16 + gx % 16]

    for gy in range(rows * 12):
        for gx in range(cols * 16):
            m = at(gx, gy)
            if m is None or m not in tfam:
                continue
            f = tfam[m]
            same = lambda dx, dy: at(gx + dx, gy + dy) is None or tfam.get(at(gx + dx, gy + dy)) == f
            mask = (1 if same(0, -1) else 0) | (2 if same(1, 0) else 0) | (4 if same(0, 1) else 0) | (8 if same(-1, 0) else 0)
            cell = ed["grid"][gy // 12][gx // 16]
            ed["screens"][cell["screen"]]["blocks"][(gy % 12) * 16 + gx % 16] = tp["terrains"][f]["tiles"][mask]
    table = target["descriptor"]["metatile_table"]
    ed["theme"] = theme
    ed["metatileTable"] = table
    ed["metatiles"] = [list(e["words"]) for e in model["metatile_tables"][table]["entries"]]
    ed["metatileClasses"] = tclasses
    ed["parts"] = tp
    return ed


def extendable(level):
    """Levels whose shape can change: horizontal levels whose row holds exactly the
    playable screens (one entity list each), and vertical levels made of one
    column of screens then a row at the bottom (level 1)."""
    d = level["descriptor"]
    if level["kind"] == "vertical":
        return vertical_shape(level) is not None
    if level["kind"] != "horizontal":
        return False
    row = level["layout"]["rows"][d["start_screen_y"]]["screens"]
    return len(row) == d["width"] + 1 and len(level["entities"]["screens"]) == d["width"] + 1


def vertical_shape(level):
    """(column, bottom row) of a level made of one column of screens going down,
    then a row at the bottom starting under it, with one entity list per screen
    (column first); None for other shapes."""
    lay, d = level["layout"], level["descriptor"]
    if lay["cols_is_rows"] or len(lay["cols"]) != 1 or len(lay["rows"]) != 2 or d["start_screen_y"] != 1:
        return None
    column, bottom, top = lay["cols"][0]["screens"], lay["rows"][0]["screens"], lay["rows"][1]["screens"]
    if top != column[:1] or bottom[:1] != column[-1:] or d["height"] != len(column) - 1 or d["width"] != len(bottom) - 1:
        return None
    if len(level["entities"]["screens"]) != len(column) + len(bottom) - 1:
        return None
    return column, bottom


def cmd_list(model):
    return [{"level": l["number"], "name": l["name"], "canExtend": extendable(l)} for l in model["levels"]]


def level_start(level):
    """Where Alex appears: a grid cell of the editor and a pixel of its screen (the
    top left of his 16x24 box)."""
    d, t = level["descriptor"], level["tables"]
    col = 0 if level["kind"] == "vertical" else d["start_screen_x"] - 1
    row = d["start_screen_y"] if level["kind"] == "castle" else 0
    return {"col": col, "row": row, "x": t["start_x"], "y": t["start_y"]}


def export_level(model, lv):
    level = model["levels"][lv - 1]
    cells = level["map"]
    columns = max(c["x"] for c in cells) + 1
    rows = max(c["y"] for c in cells) + 1
    grid = [[None] * columns for _ in range(rows)]
    for c in cells:
        grid[c["y"]][c["x"]] = {"screen": c["screen"], "entities": c["entity_index"]}
    entities, specials = [], []
    for stream in level["entities"]["screens"]:
        entities.append([{"type": r["type"], "x": r["x"], "y": r["y"], "data": r["data"]}
                         for r in stream["records"] if r["kind"] == "entity"])
        specials.append([r for r in stream["records"] if r["kind"] not in ("entity", "end")])
    table = level["descriptor"]["metatile_table"]
    entries = model["metatile_tables"][table]["entries"]
    special_types = sorted(set(r["type"] for st in specials for r in st if "type" in r))
    notes = []
    used = {}
    for c in cells:
        used.setdefault(c["screen"], []).append((c["x"], c["y"]))
    shared = [s for s, where in used.items() if len(where) > 1]
    if shared:
        notes.append("Écrans partagés (modifier l'un modifie les autres) : %s" % ", ".join(map(str, sorted(shared))))
    if "sub_area" in level:
        notes.append("Ce niveau a une zone bonus qui n'est pas encore éditable.")
    return {
        "level": lv,
        "name": level["name"],
        "kind": level["kind"],
        "columns": columns,
        "rows": rows,
        "grid": grid,
        "screens": [{"blocks": list(s["metatiles"])} for s in level["screens"]],
        "entities": entities,
        "specials": specials,
        "metatileTable": table,
        "metatiles": [list(e["words"]) for e in entries],
        "metatileClasses": [e.get("class", "") for e in entries],
        "entityTypes": entity_type_list(model),
        "specialTypes": [{"id": t, "name": special_type_name(t)} for t in special_types],
        "canExtend": extendable(level),
        "start": level_start(level),
        "parts": parts.learn_parts(model, level),
        "theme": lv,
        "themeMusic": True,
        "surprises": None,
        "surpriseItems": [{"id": t, "name": n} for t, n in SURPRISE_ITEMS],
        "levelNames": {str(l["number"]): l["name"] for l in model["levels"]},
        "notes": notes,
    }


# ------------------------------------------------------------------------- build
def apply_edits(level, ed):
    """Copies the editor's screens, layout changes and entities into the decoded level."""
    can_extend = extendable(level)  # before the layout changes below
    n_old = len(level["screens"])
    if len(ed["screens"]) < n_old:
        raise ValueError("level %d: screens cannot be removed from the list (%d < %d)"
                         % (level["number"], len(ed["screens"]), n_old))
    for i, es in enumerate(ed["screens"]):
        blocks = [int(b) & 0xFF for b in es["blocks"]]
        if len(blocks) != levels.SCREEN_CELLS:
            raise ValueError("a screen must have %d blocks" % levels.SCREEN_CELLS)
        if i >= n_old:
            level["screens"].append({"ptr": None, "metatiles": blocks})
        elif blocks != level["screens"][i]["metatiles"]:
            level["screens"][i]["metatiles"] = blocks
            level["screens"][i].pop("rle_tokens", None)

    # Layout: only extendable (simple horizontal) levels may change their screen row.
    d = level["descriptor"]
    new_row = None
    if level["kind"] == "horizontal" and ed.get("grid"):
        new_row = [c["screen"] for c in ed["grid"][0] if c]
    old_row = level["layout"]["rows"][d["start_screen_y"]]["screens"]
    if level["kind"] == "vertical" and can_extend and ed.get("grid"):
        apply_vertical_shape(level, ed["grid"])
    if level["kind"] == "horizontal" and new_row is not None and new_row != old_row[:d["width"] + 1]:
        if not can_extend:
            raise ValueError("level %d: its screen layout cannot be changed" % level["number"])
        if len(new_row) < 2:
            raise ValueError("a level needs at least two screens")
        row_ptr = level["layout"]["rows"][d["start_screen_y"]]["ptr"]
        for tab in level["layout"]["rows"] + level["layout"]["cols"]:
            if tab["ptr"] == row_ptr:
                tab["screens"] = list(new_row)
        d["width"] = len(new_row) - 1

    # A bonus zone (Maker levels, engine/src/game/states/zone.c): one more row of
    # screens at the end of the layout, its entity lists after the level's.
    ents = ed["entities"]
    specials = ed.get("specials") or [None] * len(ents)
    if level["kind"] == "horizontal" and can_extend and ed.get("zone"):
        zone_row = [c["screen"] for c in ed["zone"]["grid"][0] if c]
        level["layout"]["rows"].append({"ptr": None, "screens": zone_row})
        ents = ents + ed["zone"]["entities"]
        specials = specials + (ed["zone"].get("specials") or [None] * len(ed["zone"]["entities"]))
    streams = level["entities"]["screens"]
    if len(ents) < len(streams) and not can_extend:
        raise ValueError("level %d: wrong number of entity lists" % level["number"])
    while len(streams) < len(ents):
        streams.append({"ptr": None, "records": []})
    del streams[len(ents):]
    for stream, lst, spec in zip(streams, ents, specials):
        if spec is None:
            spec = [r for r in stream["records"] if r["kind"] not in ("entity", "end")]
        # text / moves: the Maker's janken opponent set-up (written apart, backend.js).
        recs = [{k: v for k, v in r.items() if k not in ("text", "moves")} for r in spec]
        for r in recs:
            for k in ("type", "x", "y", "data"):
                if k in r:
                    r[k] = int(r[k]) & 0xFF
        for e in lst:
            recs.append({"kind": "entity", "type": int(e["type"]) & 0xFF, "y": int(e["y"]) & 0xFF,
                         "x": int(e["x"]) & 0xFF, "data": int(e["data"]) & 0xFF})
        stream["records"] = recs

    # Alex's start: a pixel of the start screen, which simple horizontal levels
    # can move to any of their screens.
    if ed.get("start"):
        level["tables"]["start_x"] = int(ed["start"]["x"]) & 0xFF
        level["tables"]["start_y"] = int(ed["start"]["y"]) & 0xFF
        col = int(ed["start"]["col"])
        if level["kind"] == "horizontal" and can_extend and 0 <= col <= d["width"]:
            d["start_screen_x"] = col + 1


def apply_vertical_shape(level, grid):
    """New column/bottom row of a vertical level from the editor grid: the first
    cell of every row is the column, the last row carries on to the right."""
    column = []
    for r, row in enumerate(grid):
        cells = [c for c in row if c]
        if not row or not row[0] or (r < len(grid) - 1 and len(cells) != 1):
            raise ValueError("level %d: a vertical level is one column of screens, then a row at the bottom" % level["number"])
        column.append(row[0]["screen"])
    bottom = [c["screen"] for c in grid[-1] if c]
    if len(column) < 2:
        raise ValueError("a level needs at least two screens")
    lay, d = level["layout"], level["descriptor"]
    lay["cols"][0]["screens"] = column
    lay["rows"][1]["screens"] = column[:1]
    lay["rows"][0]["screens"] = bottom
    d["height"] = len(column) - 1
    d["width"] = len(bottom) - 1


def relocate_layout(level, bank, extra=None):
    """Assigns fresh addresses in `bank` to the level's layout tables and screens.

    `extra` (optional) is another level whose layout must also be present in the bank
    (level 3's sub-area reads level 4's layout); returns its new top-table address."""
    cursor = [0x8000]

    def alloc(n):
        a = cursor[0]
        cursor[0] += n
        if cursor[0] > 0xC000:
            raise ValueError("level %d does not fit in one bank" % level["number"])
        return a

    def place(lvl, copy_screens):
        d, lay, scr = lvl["descriptor"], lvl["layout"], lvl["screens"]
        rows_ptr = alloc(2 * len(lay["rows"]))
        cols_ptr = rows_ptr if lay["cols_is_rows"] else alloc(2 * len(lay["cols"]))
        new_ptr = {}
        for tab in lay["rows"] + ([] if lay["cols_is_rows"] else lay["cols"]):
            if tab["ptr"] not in new_ptr:
                new_ptr[tab["ptr"]] = alloc(2 * len(tab["screens"]))
        for tab in lay["rows"] + lay["cols"]:
            tab["ptr"] = new_ptr.get(tab["ptr"], tab["ptr"])
        for s in scr:
            s["ptr"] = alloc(len(levels.encode_screen(s)))
        return rows_ptr, cols_ptr

    rows_ptr, cols_ptr = place(level, True)
    d = level["descriptor"]
    d["bank"] = bank
    d["bank_byte"] = 0x80 | bank
    d["rows_ptr"], d["cols_ptr"] = rows_ptr, cols_ptr
    extra_top = None
    if extra is not None:
        extra_top, _ = place(extra, True)
    return extra_top


def encode_layout(level):
    """Patches for a level's descriptor fields, layout tables and screens only."""
    d, lay, scr = level["descriptor"], level["layout"], level["screens"]
    bank = d["bank"]
    off = lambda a: bank * 0x4000 + (a - 0x8000)
    out = [(d["rom_offset"], struct.pack(
        "<BHHBBBBBH", d["bank_byte"], d["rows_ptr"], d["cols_ptr"], d["start_screen_x"],
        d["start_screen_y"], d["width"], d["height"], d["scroll_flags"], d["metatile_table_ptr"]))]
    out.append((off(d["rows_ptr"]), b"".join(struct.pack("<H", r["ptr"]) for r in lay["rows"])))
    if not lay["cols_is_rows"]:
        out.append((off(d["cols_ptr"]), b"".join(struct.pack("<H", c["ptr"]) for c in lay["cols"])))
    seen = set()
    for tab in lay["rows"] + ([] if lay["cols_is_rows"] else lay["cols"]):
        if tab["ptr"] in seen:
            continue
        seen.add(tab["ptr"])
        out.append((off(tab["ptr"]), b"".join(struct.pack("<H", scr[i]["ptr"]) for i in tab["screens"])))
    for s in scr:
        out.append((off(s["ptr"]), levels.encode_screen(s)))
    return out


def encode_layout_copy(level, bank, rows_ptr, cols_ptr):
    """Like encode_layout for a copy of `level` placed in `bank` (no descriptor)."""
    lay, scr = level["layout"], level["screens"]
    off = lambda a: bank * 0x4000 + (a - 0x8000)
    out = [(off(rows_ptr), b"".join(struct.pack("<H", r["ptr"]) for r in lay["rows"]))]
    if not lay["cols_is_rows"]:
        out.append((off(cols_ptr), b"".join(struct.pack("<H", c["ptr"]) for c in lay["cols"])))
    seen = set()
    for tab in lay["rows"] + ([] if lay["cols_is_rows"] else lay["cols"]):
        if tab["ptr"] in seen:
            continue
        seen.add(tab["ptr"])
        out.append((off(tab["ptr"]), b"".join(struct.pack("<H", scr[i]["ptr"]) for i in tab["screens"])))
    for s in scr:
        out.append((off(s["ptr"]), levels.encode_screen(s)))
    return out


def count_question_boxes(model, level):
    classes = [e.get("class", "") for e in model["metatile_tables"][level["descriptor"]["metatile_table"]]["entries"]]
    return sum(1 for c in level["map"] for m in level["screens"][c["screen"]]["metatiles"]
               if classes[m] == "question_box")


def assign_surprises(model, edited):
    """Gives each level with custom surprises its own run of the question-box item
    table, in slots no other level reads (a level reads one entry per box broken,
    starting at its index)."""
    items = model["globals"]["question_box_items"]
    wanted = {lv: [int(x) & 0xFF for x in ed["surprises"]] for lv, ed in edited.items() if ed.get("surprises")}
    if not wanted:
        return
    used = [False] * len(items)
    for level in model["levels"]:
        if level["number"] in wanted:
            continue
        start = level["tables"]["question_box_index"]
        for k in range(count_question_boxes(model, level)):
            if start + k < len(used):
                used[start + k] = True
    for lv, seq in sorted(wanted.items()):
        n = len(seq)
        pos = next((i for i in range(len(items) - n + 1) if not any(used[i:i + n])), None)
        if pos is None:
            raise ValueError("pas assez de place pour %d surprises dans le niveau %d" % (n, lv))
        items[pos:pos + n] = seq
        for k in range(n):
            used[pos + k] = True
        model["levels"][lv - 1]["tables"]["question_box_index"] = pos


def repack_entity_data(model):
    """Lays out every level's entity pointer table and streams again in bank 2
    (tables first, then streams, identical streams shared); returns patches."""
    lo, hi = ENTITY_REGION
    cursor = lo
    out = []
    for level in model["levels"]:
        ent = level["entities"]
        ent["table_ptr"] = cursor
        cursor += 2 * len(ent["screens"])
    placed = {}
    for level in model["levels"]:
        for stream in level["entities"]["screens"]:
            data = levels.encode_entity_stream(stream["records"])
            if data not in placed:
                if cursor + len(data) > hi:
                    raise ValueError("too many entities: bank 2 has room for %d bytes of entity data" % (hi - lo))
                placed[data] = cursor
                out.append((cursor, data))  # bank 2: ROM offset = CPU address
                cursor += len(data)
            stream["ptr"] = placed[data]
    for level in model["levels"]:
        ent = level["entities"]
        out.append((ent["table_ptr"], b"".join(struct.pack("<H", s["ptr"]) for s in ent["screens"])))
    out.append((levels.ENTITY_DESCRIPTOR_TABLE,
                b"".join(struct.pack("<H", l["entities"]["table_ptr"]) for l in model["levels"])))
    return out, cursor - lo


import copy


def shown_screens(level):
    """Blocks of the screens along a horizontal level's row, in order (the
    screen list itself may be ordered differently once screens are copied)."""
    d = level["descriptor"]
    row = level["layout"]["rows"][d["start_screen_y"]]["screens"][:d["width"] + 1]
    return [level["screens"][i]["metatiles"] for i in row]


def build(rom_bytes, mod_dir, out_path, start=None):
    """start = (level, column): test builds only, the level begins at that screen
    (simple horizontal levels)."""
    import copy
    model = levels.load(rom_bytes)
    original = copy.deepcopy(model)
    edited = {}
    for name in sorted(os.listdir(mod_dir)):
        if name.startswith("level_") and name.endswith(".json"):
            lv = int(name[6:8])
            edited[lv] = json.load(open(os.path.join(mod_dir, name)))
            apply_edits(model["levels"][lv - 1], edited[lv])
    # Themes: the level borrows another level's graphics tables (and music).
    for lv, ed in edited.items():
        theme = int(ed.get("theme") or lv)
        if theme != lv:
            level, src = model["levels"][lv - 1], model["levels"][theme - 1]
            for k in THEME_TABLES:
                level["tables"][k] = copy.deepcopy(src["tables"][k])
            if ed.get("themeMusic", True):
                level["tables"]["song"] = src["tables"]["song"]
            level["descriptor"]["metatile_table_ptr"] = src["descriptor"]["metatile_table_ptr"]
            level["descriptor"]["metatile_table"] = src["descriptor"]["metatile_table"]
    assign_surprises(model, edited)
    # Vehicles: the level starts on one (engine/src/game/states/gameplay.c; 7 is
    # the Maker's motorbike start), a wreck makes Alex jump off (no dive), and
    # the vehicle's song plays (the boat keeps the level's).
    for lv, ed in edited.items():
        v = VEHICLES.get(ed.get("vehicle"))
        if not v:
            continue
        t = model["levels"][lv - 1]["tables"]
        t["spawn_state"] = v["spawn"]
        t["vehicle_crash_to_water"] = 0
        if v["song"]:
            t["song"] = v["song"]

    if start:
        lv, col = start
        level = model["levels"][lv - 1]
        if lv not in edited:
            edited[lv] = export_level(model, lv)
        if level["kind"] == "horizontal" and 0 <= col <= level["descriptor"]["width"]:
            level["descriptor"]["start_screen_x"] = col + 1

    patches = []
    for lv in sorted(edited):
        patches += levels.encode_level_tables(lv, model["levels"][lv - 1]["tables"])
    if any(e.get("surprises") for e in edited.values()):
        patches.append((levels.QUESTION_BOX_ITEMS, bytes(model["globals"]["question_box_items"])))
    for lv in sorted(edited):
        level = model["levels"][lv - 1]
        bank = FIRST_LEVEL_BANK + lv - 1
        sub = SUB_AREA_PATCHES.get(lv)
        extra = None
        if sub and sub["layout_level"] != lv:
            extra = copy.deepcopy(model["levels"][sub["layout_level"] - 1])
        extra_top = relocate_layout(level, bank, extra)
        patches += encode_layout(level)
        if extra is not None:
            patches += encode_layout_copy(extra, bank, extra_top, extra_top)
        if sub:
            target = extra_top if extra is not None else level["descriptor"]["rows_ptr"]
            for operand in sub["operands"]:
                patches.append((operand, struct.pack("<H", target)))

    entity_bytes = 0
    if any(model["levels"][lv - 1]["entities"] != original["levels"][lv - 1]["entities"] for lv in edited):
        p, entity_bytes = repack_entity_data(model)
        patches += p

    size = MOD_ROM_SIZE if edited else len(rom_bytes)
    base = bytearray(rom_bytes) + b"\xFF" * (size - len(rom_bytes))
    patched = levels.apply_patches(base, patches)

    # Check: the patched ROM decodes to exactly the edited levels.
    check = levels.load(bytes(patched))
    for lv in edited:
        got, want = check["levels"][lv - 1], model["levels"][lv - 1]
        if [s["metatiles"] for s in got["screens"]] != [s["metatiles"] for s in want["screens"]] \
                and shown_screens(got) != shown_screens(want):
            raise ValueError("level %d: screens do not decode back identically" % lv)
        if got["descriptor"]["width"] != want["descriptor"]["width"]:
            raise ValueError("level %d: layout width does not decode back" % lv)
        norm = lambda st: [[r for r in s["records"] if r["kind"] != "end"] for s in st["entities"]["screens"]]
        if norm(got) != norm(want):
            raise ValueError("level %d: entities do not decode back identically" % lv)

    # Only the differences go into the mod file.
    records = []
    i = 0
    ref = bytearray(rom_bytes) + b"\xFF" * (size - len(rom_bytes))
    while i < size:
        if patched[i] == ref[i]:
            i += 1
            continue
        j = i
        while j < size and (patched[j] != ref[j] or (j + 8 < size and patched[j:j + 8] != ref[j:j + 8])):
            j += 1
        records.append((i, bytes(patched[i:j])))
        i = j
    with open(out_path, "wb") as f:
        f.write(b"AKMOD1\0\0")
        f.write(struct.pack("<I", size))
        for off, data in records:
            f.write(struct.pack("<II", off, len(data)))
            f.write(data)
    total = sum(len(d) for _, d in records)
    return {"levels": sorted(edited), "records": len(records), "bytes": total, "entity_bytes": entity_bytes}


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, rom_path = argv[1], argv[2]
    rom = _read_rom(rom_path)
    if cmd == "list":
        json.dump(cmd_list(levels.load(rom)), sys.stdout)
    elif cmd == "export":
        json.dump(export_level(levels.load(rom), int(argv[3])), sys.stdout)
    elif cmd == "retheme":
        ed = json.load(open(argv[3]))
        json.dump(retheme(levels.load(rom), ed, int(argv[4])), sys.stdout)
    elif cmd == "icontypes":
        model = levels.load(rom)
        spec = {}
        for level in model["levels"]:
            for stream in level["entities"]["screens"]:
                for r in stream["records"]:
                    if "type" in r and r["type"] not in spec:
                        spec[r["type"]] = level["number"]
        print(" ".join("%d:%d" % kv for kv in sorted(spec.items())))
        return 0
    elif cmd == "build":
        start = None
        if len(argv) > 6 and argv[5] == "--start":
            lv, col = argv[6].split(":")
            start = (int(lv), int(col))
        info = build(rom, argv[3], argv[4], start)
        json.dump(info, sys.stdout)
    else:
        print(__doc__)
        return 2
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
