"""Mario-Maker-style building parts learnt from a level's own screens.

For each level (= theme: its graphics decide how metatiles look), derives:
  * eraser    the level's background metatile (sky, water...)
  * terrains  families of solid metatiles that the original screens place next
              to each other, with an auto-tiling table: for each 4-neighbour mask
              (bit 0 up, 1 right, 2 down, 3 left = same family), the metatile the
              original level uses most for that situation
  * blocks    single special blocks: boxes, money, breakable rock, water, danger,
              ladder, invisible floor...
  * stamps    decorations made of several metatiles (clouds, bushes, trees...)
Only metatiles that the level itself uses are offered (they are the only ones
guaranteed to have their graphics loaded), plus the item boxes, which every
level loads.
"""
from collections import Counter, defaultdict

import levels

UP, RIGHT, DOWN, LEFT = 1, 2, 4, 8
# Metatile 20 is the unbreakable rock in every level (a red ball, a grey stone...):
# levels stack it on their ground, but it is an object, not ground to paint.
ROCK = 20
BLOCK_KINDS = [
    ("question_box", "Boîte ?"), ("star_box", "Boîte étoile (argent)"), ("skull_box", "Boîte tête de mort"),
    ("money", "Argent"), ("breakable", "Roche cassable"), ("water", "Eau"), ("deadly", "Danger (mortel)"),
    ("ladder", "Échelle"), ("ladder_top", "Haut d'échelle"),
]


def level_grid(level):
    """Metatile ids of the level's reachable map as a dict (x, y) -> id, in cells."""
    grid = {}
    for cell in level["map"]:
        blocks = level["screens"][cell["screen"]]["metatiles"]
        for i, m in enumerate(blocks):
            grid[(cell["x"] * 16 + i % 16, cell["y"] * 12 + i // 16)] = m
    return grid


def classes_of(model, level):
    entries = model["metatile_tables"][level["descriptor"]["metatile_table"]]["entries"]
    return [e.get("class", "") for e in entries]


def is_solid_class(c):
    return c == "solid" or c.startswith("solid") or c.endswith("solid") or "solid" in c.split("(")[-1]


def learn_parts(model, level):
    grid = level_grid(level)
    cls = classes_of(model, level)
    used = Counter(grid.values())

    # Eraser: the most common non-solid, non-special background block.
    backgrounds = [(n, m) for m, n in used.items() if cls[m] in ("background", "empty", "water")]
    eraser = max(backgrounds)[1] if backgrounds else 0

    # Terrain families: solid blocks joined when they touch often enough.
    solid = set(m for m in used if "solid" in cls[m] and not cls[m].endswith("_box") and m != ROCK)
    parent = {m: m for m in solid}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    touch = Counter()
    for (x, y), m in grid.items():
        if m not in solid:
            continue
        for dx, dy in ((1, 0), (0, 1)):
            n = grid.get((x + dx, y + dy))
            if n in solid and n != m:
                touch[tuple(sorted((m, n)))] += 1
    for (a, b), k in touch.items():
        if k >= 2:
            parent[find(a)] = find(b)
    families = defaultdict(set)
    for m in solid:
        families[find(m)].add(m)

    terrains = []
    for members in families.values():
        count = sum(used[m] for m in members)
        if count < 6:
            continue
        by_mask = defaultdict(Counter)
        for (x, y), m in grid.items():
            if m not in members:
                continue
            mask = 0
            for bit, (dx, dy) in ((UP, (0, -1)), (RIGHT, (1, 0)), (DOWN, (0, 1)), (LEFT, (-1, 0))):
                n = grid.get((x + dx, y + dy))
                if n is None or n in members:  # outside the map counts as "more of the same"
                    mask |= bit
            by_mask[mask][m] += 1
        table = {}
        for mask in range(16):
            if by_mask[mask]:
                table[mask] = by_mask[mask].most_common(1)[0][0]
        # Missing situations borrow the closest known one (fewest differing sides,
        # preferring to keep the top side, which usually carries grass or edges).
        for mask in range(16):
            if mask in table:
                continue
            best = min(table, key=lambda k: (bin(k ^ mask).count("1"), (k ^ mask) & UP != 0, -sum(by_mask[k].values())))
            table[mask] = table[best]
        fill = table[UP | RIGHT | DOWN | LEFT]
        top = table[RIGHT | DOWN | LEFT]
        terrains.append({"members": sorted(members), "tiles": [table[m] for m in range(16)],
                         "preview": top, "fill": fill, "count": count})
    terrains.sort(key=lambda t: -t["count"])
    for i, t in enumerate(terrains):
        t["name"] = "Terrain %d" % (i + 1)

    # Single special blocks.
    blocks = []
    all_classes = model["metatile_tables"][level["descriptor"]["metatile_table"]]["entries"]
    for kind, label in BLOCK_KINDS:
        candidates = [m for m, _ in used.most_common() if cls[m] == kind]
        if not candidates and kind.endswith("_box"):
            # Boxes use tiles 1-36, loaded in every level.
            candidates = [i for i, e in enumerate(all_classes) if e.get("class") == kind]
        if candidates:
            blocks.append({"kind": kind, "name": label, "metatile": candidates[0]})
    blocks.insert(0, {"kind": "rock", "name": "Rocher", "metatile": ROCK})

    # Decorations: connected groups of non-background, non-solid, non-special blocks.
    deco_ok = lambda m: m != eraser and cls[m] in ("background",) and used[m] > 0
    seen = set()
    shapes = Counter()
    for pos, m in grid.items():
        if pos in seen or not deco_ok(m):
            continue
        comp, stack = [], [pos]
        seen.add(pos)
        while stack:
            p = stack.pop()
            comp.append(p)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (p[0] + dx, p[1] + dy)
                if q not in seen and q in grid and deco_ok(grid[q]):
                    seen.add(q)
                    stack.append(q)
        xs, ys = [p[0] for p in comp], [p[1] for p in comp]
        w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if not (1 < len(comp) and w <= 8 and h <= 8):
            continue
        cells = tuple(tuple(grid[(min(xs) + i, min(ys) + j)] if (min(xs) + i, min(ys) + j) in comp else -1
                            for i in range(w)) for j in range(h))
        shapes[cells] += 1
    # Keep decorations that look like objects: repeated in the level, or made of
    # several different blocks (a single repeated backdrop block is not one).
    def interesting(c, n):
        flat = [m for row in c for m in row if m >= 0]
        return len(set(flat)) >= 2 and (n >= 2 or (len(flat) >= 4 and len(set(flat)) >= 3))
    ranked = sorted(((c, n) for c, n in shapes.items() if interesting(c, n)),
                    key=lambda cn: (-cn[1], -sum(m >= 0 for row in cn[0] for m in row)))
    stamps = [{"w": len(c[0]), "h": len(c), "cells": [list(row) for row in c], "count": n}
              for c, n in ranked[:12]]
    return {"eraser": eraser, "terrains": terrains, "blocks": blocks, "stamps": stamps}


if __name__ == "__main__":
    import sys
    rom = open(sys.argv[1] if len(sys.argv) > 1 else "original.sms", "rb").read()
    model = levels.load(rom)
    for lv in range(1, 18):
        p = learn_parts(model, model["levels"][lv - 1])
        print("level %2d: eraser %3d, %d terrains %s, blocks %s, %d stamps" % (
            lv, p["eraser"], len(p["terrains"]), [len(t["members"]) for t in p["terrains"]],
            [b["kind"] for b in p["blocks"]], len(p["stamps"])))
