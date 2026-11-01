# Regenerates data/maps/<id>/map.txt. Usage: python3 tools/make_maps.py data/maps
#
# Each map is a grid (walls, floors) plus props off the grid (world/props.h)
# and movers (simulation/movers.h); the format is in src/world/map_loader.h.
# Positions here are in cells (x across, z down the rows), as floats: a
# cell's centre is (cx + 0.5, cz + 0.5). They are written out in metres.
import os
import sys

CS = 0.5  # metres per cell

LEGEND = {
    "#": "wall", ".": "floor", "d": "wall_dark", "l": "wall_light", "R": "wall_red",
    "B": "wall_blue", "Y": "wall_yellow", "G": "wall_green", "P": "wall_purple",
    "C": "crate", "I": "pillar", ",": "rug_blue", ";": "rug_green", "'": "rug_pink",
    ":": "tile", "b": "board", "D": "door", "S": "steel", "~": "water", "+": "planks",
    "_": "cobble", "g": "grass", "s": "straw", "k": "counter",
}
# Legend keys are single characters other than "=" (the config syntax splits
# on it) and "#" (written "hash").
# Floor materials (everything else in a legend is solid).
FLOORS = {"floor", "rug_blue", "rug_green", "rug_pink", "tile", "planks", "cobble", "grass", "straw"}


class Canvas:
    def __init__(s, w, h, border="#", fill="."):
        s.w, s.h = w, h
        s.g = [[fill for _ in range(w)] for _ in range(h)]
        s.rect(0, 0, w - 1, h - 1, border, fill=False)

    def set(s, x, z, c):
        s.g[z][x] = c

    def get(s, x, z):
        return s.g[z][x]

    def rect(s, x0, z0, x1, z1, c, fill=True):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                if fill or x in (x0, x1) or z in (z0, z1):
                    s.g[z][x] = c

    def rows(s):
        return ["".join(r) for r in s.g]


class Map:
    def __init__(s, mid, w, h, name, desc, order, difficulty, fill="."):
        s.id, s.name, s.desc, s.order, s.difficulty = mid, name, desc, order, difficulty
        s.focus = None  # (x, z) in cells: where screenshots take a close look
        s.c = Canvas(w, h, fill=fill)
        s.legend = dict(LEGEND)
        s.materials = []  # (id, dict) of this map's own materials
        s.props = []
        s.movers = []

    def material(s, key, mid, **kw):
        s.legend[key] = mid
        s.materials.append((mid, kw))
        if not kw.get("solid", False):
            FLOORS.add(mid)

    def prop(s, kind, x, z, **kw):
        s.props.append((kind, x, z, kw))

    def mover(s, kind, x, z, **kw):
        s.movers.append((kind, x, z, kw))


def pts(points):
    """Cells -> 'x,z;x,z' in metres."""
    return ";".join(f"{x * CS:g},{z * CS:g}" for x, z in points)


def fmt(v):
    if isinstance(v, float):
        return f"{v:g}"
    v = str(v)
    return f'"{v}"' if " " in v else v


maps = []

# ---------------------------------------------------------------------------
# Office. The Chinese Room: twelve identical offices, and in two neighbours
# that share a closed door, one board asks in Chinese and the other holds the
# English rule book that answers it. Wobo, like Searle, sees only shapes: the
# boards look the same to its scanner whatever is written on them.
# ---------------------------------------------------------------------------
m = Map("office", 57, 17, "Office",
        "Twelve identical offices off one long hallway, a pantry at one end and the lifts at the other. "
        "Two of the offices share a door, and neither understands the other.",
        5, "Hard")
c = m.c
# Pantry, west.
c.rect(1, 1, 7, 2, "#"); c.rect(1, 14, 7, 15, "#")
c.rect(8, 1, 8, 15, "#")
for z in (7, 8, 9):
    c.set(8, z, ":")
c.rect(1, 3, 7, 13, ":")
# Offices: six above the hallway, six below.
for i in range(6):
    x0 = 9 + 6 * i
    c.rect(x0 + 5, 1, x0 + 5, 5, "#")
    c.rect(x0 + 5, 11, x0 + 5, 15, "#")
c.rect(9, 6, 44, 6, "#"); c.rect(9, 10, 44, 10, "#")
for i in range(6):
    x0 = 9 + 6 * i
    c.set(x0 + 2, 6, "."); c.set(x0 + 2, 10, ".")
    # Every top office has a blackboard on its back wall, and a desk facing it.
    c.rect(x0 + 1, 0, x0 + 4, 0, "b")
    m.prop("desk", x0 + 2.5, 4.0, w=1.2, d=0.5, yaw=0)
    m.prop("desk", x0 + 2.5, 13.0, w=1.2, d=0.5, yaw=0)
c.rect(9, 7, 44, 9, ":")
# The Chinese Room: offices 2 and 3 share a door.
c.set(26, 3, "D")
m.prop("door", 26.5, 3.5, yaw=0, slot=1)
for i in range(6):
    x0 = 9 + 6 * i
    text = {2: "你懂中文吗？|请回答。", 3: "Rule 1: on 你懂中文吗？|write back 懂。"}.get(i, "")
    kw = dict(yaw=90, w=1.9)
    if text:
        kw["text"] = text
    m.prop("blackboard", x0 + 3.0, 1.0, **kw)
# Lift lobby, east, at the end of the hallway.
c.rect(45, 1, 55, 1, "#"); c.rect(45, 15, 55, 15, "#")
c.rect(45, 2, 55, 14, ".")
for x in (46, 49, 52):
    c.rect(x, 1, x + 1, 1, "S")
    m.prop("elevator", x + 1.0, 2.0, yaw=90, w=1.0)
m.prop("bench", 50.5, 8.5, w=1.6, yaw=0)
m.prop("plant", 45.6, 2.6); m.prop("plant", 54.4, 2.6); m.prop("plant", 54.4, 14.4)
m.prop("desk", 53.5, 11.5, w=1.4, d=0.6, yaw=90, top="none")
# Pantry fittings.
m.prop("counter", 3.5, 3.5, w=2.5, d=0.5, top="coffee")
m.prop("fridge", 6.65, 3.6)
m.prop("counter", 1.5, 6.5, w=1.5, d=0.5, yaw=90, top="sink")
m.prop("table", 4.0, 10.0, r=0.45, seats=4, setting="none")
m.prop("cooler", 1.6, 12.4)
m.prop("plant", 6.4, 12.4)
m.focus = (27.0, 3.0)
maps.append(m)

# ---------------------------------------------------------------------------
# Art Gallery. The art gallery problem: how many guards does it take to see
# every wall? This one is a comb, the shape that shows floor(n/3) guards can
# be needed: no point sees into two of its five wings, so it has a guard post
# at the mouth of each, and a guard who walks between them.
# ---------------------------------------------------------------------------
m = Map("gallery", 60, 34, "Art Gallery",
        "Five wings off one long hall, each hung and furnished its own way, and a guard post at every mouth. "
        "Colour is what tells the wings apart.",
        2, "Medium", fill="#")
c = m.c
WINGS = [  # x0, x1, top z
    (2, 9, 3), (13, 22, 8), (26, 32, 2), (36, 47, 6), (51, 58, 11),
]
c.rect(1, 26, 58, 31, ".")
for x0, x1, top in WINGS:
    c.rect(x0, top, x1, 25, ".")
# The spine's floor, and a rug in each wing.
c.rect(1, 26, 58, 31, ":")
cols = "RBYGP"
COLOUR_MAT = {"R": "wall_red", "B": "wall_blue", "Y": "wall_yellow", "G": "wall_green", "P": "wall_purple"}


def hang(x, z, colour, along_z, facing_yaw, w_cells, visible):
    """Paint w_cells of wall (cells along x or z from (x, z)) and frame them if the face is seen."""
    for k in range(w_cells):
        if along_z:
            c.set(x, z + k, colour)
        else:
            c.set(x + k, z, colour)
    if visible:
        if along_z:
            m.prop("painting", x + 1.0, z + w_cells / 2.0, yaw=facing_yaw, w=w_cells * CS * 0.9, mat=COLOUR_MAT[colour])
        else:
            m.prop("painting", x + w_cells / 2.0, z + 1.0, yaw=facing_yaw, w=w_cells * CS * 0.9, mat=COLOUR_MAT[colour])


# Wing A: the long gallery. Paintings all down both walls, benches between.
for i, z in enumerate(range(5, 24, 4)):
    hang(1, z, cols[i % 5], True, 0, 2, True)
    hang(10, z + 2, cols[(i + 2) % 5], True, 180, 2, False)
m.prop("bench", 6.0, 11.0, w=1.2, yaw=90); m.prop("bench", 6.0, 19.0, w=1.2, yaw=90)
hang(4, 2, "R", False, 90, 4, True)
# Wing B: the sculpture court.
forms = [("pillar", "wall_red"), ("cube", "wall_blue"), ("orb", "wall_yellow"),
         ("stack", "wall_green"), ("arch", "wall_purple"), ("pillar", "wall_blue"),
         ("orb", "wall_red"), ("cube", "wall_green"), ("stack", "wall_yellow")]
k = 0
for z in (11.5, 16.5, 21.5):
    for x in (15.0, 18.0, 21.0):
        form, mat = forms[k]
        m.prop("sculpture", x, z, form=form, mat=mat, r=0.28, h=0.9 + 0.15 * (k % 3))
        k += 1
hang(16, 7, "Y", False, 90, 4, True)
# Wing C: a free-standing wall down the middle, hung on both sides.
c.rect(29, 6, 29, 20, "l")
for i, z in enumerate(range(7, 19, 4)):
    hang(29, z, cols[(i + 1) % 5], True, 0, 2, True)
hang(25, 4, "G", True, 0, 3, True)
hang(25, 14, "P", True, 0, 3, True)
m.prop("bench", 27.5, 22.5, w=1.0, yaw=0)
# Wing D: the rotunda. A ring of pillars round one big orb.
cx, cz = 42.0, 15.0
import math
for i in range(8):
    a = 2 * math.pi * i / 8
    m.prop("sculpture", cx + 4.2 * math.cos(a), cz + 4.2 * math.sin(a), form="pillar", mat="pillar", r=0.18, h=1.1)
m.prop("sculpture", cx, cz, form="orb", mat="wall_purple", r=0.55, h=1.0)
hang(35, 8, "B", True, 0, 3, True)
hang(40, 5, "Y", False, 90, 4, True)
hang(35, 20, "R", True, 0, 3, True)
# Wing E: the masterpiece, alone on its wall.
hang(52, 10, "B", False, 90, 6, True)
m.prop("bench", 54.5, 15.0, w=1.6, yaw=0)
m.prop("plant", 51.6, 24.4)
# The hall: a desk by the door, benches, plants.
hang(0, 27, "G", True, 0, 3, True)
m.prop("desk", 56.5, 29.5, w=1.4, d=0.6, yaw=90, top="none")
m.prop("plant", 1.6, 30.4); m.prop("plant", 57.4, 26.6)
m.prop("bench", 20.0, 29.5, w=1.6, yaw=0); m.prop("bench", 40.0, 29.5, w=1.6, yaw=0)
# Guard posts at the wings' mouths: five wings, five guards.
for x0, x1, top in WINGS:
    m.prop("guard_post", x0 + 0.4, 26.4)
route = []
for x0, x1, top in WINGS:
    mid = (x0 + x1 + 1) / 2.0
    route += [(mid, 29.0), (mid, top + 1.5), (mid, 29.0)]
m.mover("guard", 30.0, 29.0, route=pts(route))
m.focus = (18.0, 17.0)
maps.append(m)

# ---------------------------------------------------------------------------
# Dining Hall. The dining philosophers: round tables of five, a plate of
# spaghetti at every seat and one fork between each pair of neighbours, so
# no two neighbours can eat at once.
# ---------------------------------------------------------------------------
m = Map("dining", 46, 34, "Dining Hall",
        "Twelve round tables, all laid alike: five plates of spaghetti and five forks. "
        "The kitchen, the fireplace and the high table are all Wobo has to go on.",
        3, "Medium")
c = m.c
m.material("K", "brick", name="Brick", rgb="170, 84, 62", solid=True, height=0.9, pattern="grid")
# Kitchen along the north, behind a wall with a door and a serving counter.
c.rect(1, 7, 44, 7, "#")
c.set(5, 7, "."); c.set(6, 7, ".")
c.rect(17, 7, 28, 7, "k")
c.rect(1, 1, 44, 6, ":")
m.prop("counter", 12.0, 1.5, w=6.0, d=0.5, top="stove")
m.prop("counter", 30.0, 1.5, w=8.0, d=0.5, top="sink")
m.prop("fridge", 41.5, 1.6); m.prop("fridge", 43.0, 1.6)
m.prop("counter", 22.5, 4.0, w=4.0, d=0.8, top="food")
# The hall: tables of five.
for z in (12.5, 19.5, 26.5):
    for x in (9.0, 18.0, 27.0, 36.0):
        m.prop("table", x, z, r=0.5, seats=5, setting="spaghetti", yaw=-90)
# High table at the south end, fireplace on the east wall, doors on the west.
m.prop("desk", 22.5, 31.0, w=4.5, d=0.8, mat="table", top="none")
c.rect(44, 17, 44, 21, "K"); c.rect(45, 17, 45, 21, "K")
c.rect(0, 18, 0, 20, "D")
m.prop("door", 0.5, 19.5, yaw=0, w=1.5)
c.set(13, 23, "I"); c.set(32, 16, "I")
m.prop("plant", 1.6, 8.6); m.prop("plant", 43.4, 32.4); m.prop("plant", 1.6, 32.4)
m.focus = (18.0, 19.5)
maps.append(m)

# ---------------------------------------------------------------------------
# Barn. Buridan's ass: a donkey exactly between two identical piles of hay,
# with no reason to prefer either. The barn is as symmetric as the donkey's
# problem; Wobo, unlike the donkey, has a few things to break the tie.
# ---------------------------------------------------------------------------
m = Map("barn", 46, 30, "Barn",
        "Mirror-image stalls round a wide aisle, hay stacked alike at every corner, and a donkey "
        "that cannot decide. Only the tools left about tell one end from the other.",
        1, "Medium")
c = m.c
m.material("W", "barn_wall", name="Barn wall", rgb="160, 70, 56", solid=True, height=0.9, pattern="stripes")
m.material("w", "stall", name="Stall", rgb="168, 128, 84", solid=True, height=0.55)
m.material("E", "barn_door", name="Barn door", rgb="128, 56, 46", solid=True, height=0.9, pattern="chevron")
c.rect(0, 0, 45, 29, "W", fill=False)
c.rect(1, 1, 44, 28, "s")
for x in (9, 17, 28, 36):
    c.rect(x, 1, x, 8, "w")
    c.rect(x, 21, x, 28, "w")
c.rect(0, 12, 0, 17, "E"); c.rect(45, 12, 45, 17, "E")
m.prop("door", 0.5, 14.5, yaw=0, w=2.8)
# Hay stacked alike in the four corner stalls: the north-west one mirrored
# across both of the barn's middle lines.
NW_BALES = [(2.5, 2.0, 0, 2), (4.7, 2.0, 90, 2), (6.9, 2.0, 0, 2), (4.5, 7.0, 0, 1)]
for fx in (False, True):
    for fz in (False, True):
        for (x, z, yaw, layers) in NW_BALES:
            m.prop("hay_bale", 46.0 - x if fx else x, 30.0 - z if fz else z, w=1.0, d=0.5, layers=layers, yaw=yaw)
# Buridan's two piles, the same distance either side of the middle.
m.prop("hay_pile", 15.0, 14.5, r=0.55)
m.prop("hay_pile", 31.0, 14.5, r=0.55)
# What breaks the symmetry: the tractor, the cart (Buridan's), a trough, barrels.
m.prop("tractor", 32.0, 4.5, yaw=90)
m.prop("cart", 13.0, 25.0, yaw=0, text="Property of Buridan")
m.prop("trough", 4.0, 10.0, w=1.4, yaw=0)
m.prop("barrel", 33.0, 25.0); m.prop("barrel", 34.0, 26.0); m.prop("barrel", 33.2, 27.0)
m.prop("sign", 23.0, 12.0, yaw=90, text="Hay: help yourself")
m.mover("donkey", 23.0, 18.0, hay=pts([(16.6, 14.5), (29.4, 14.5)]), rest=pts([(23.0, 14.5)]),
        wander=pts([(6.0, 15.0), (40.0, 15.0), (23.0, 5.0), (23.0, 24.0), (12.0, 18.0), (34.0, 11.0)]))
m.focus = (14.0, 21.0)
maps.append(m)

# ---------------------------------------------------------------------------
# Königsberg. The seven bridges over the Pregel: four bits of land, seven
# bridges, and no walk that crosses each exactly once (Euler, 1736). A
# townsperson keeps trying anyway.
# ---------------------------------------------------------------------------
m = Map("konigsberg", 60, 40, "Königsberg",
        "A town on two banks and two islands, joined by seven bridges. Someone is trying to cross "
        "each of them exactly once, and never will.",
        4, "Hard", fill="_")
c = m.c
for key, mid, name, rgb in (("1", "house_red", "Red house", "214, 108, 92"),
                            ("2", "house_yellow", "Yellow house", "236, 200, 104"),
                            ("3", "house_blue", "Blue house", "120, 160, 210"),
                            ("4", "house_green", "Green house", "132, 182, 132"),
                            ("5", "house_white", "White house", "236, 230, 216"),
                            ("6", "cathedral", "Cathedral brick", "176, 92, 70")):
    m.material(key, mid, name=name, rgb=rgb, solid=True, height=1.2)
for mid, rgb in (("roof_slate", "84, 92, 112"), ("roof_tile", "176, 82, 60"), ("roof_copper", "96, 168, 140")):
    m.materials.append((mid, dict(name="Roof", rgb=rgb, solid=True, height=0.1)))
# The Pregel.
c.rect(1, 18, 16, 21, "~")
c.rect(15, 11, 16, 28, "~")
c.rect(15, 11, 58, 14, "~")
c.rect(15, 25, 58, 28, "~")
c.rect(33, 15, 35, 24, "~")
# Kneiphof (C) and Lomse (D) are green; the banks (A north, B south) cobbled.
c.rect(17, 15, 32, 24, "g")
c.rect(36, 15, 58, 24, "g")
BRIDGES = [  # north (or west) land, south (or east) land, x0, z0, x1, z1 (deck cells)
    ("A", "C", 19, 11, 20, 14), ("A", "C", 27, 11, 28, 14),
    ("C", "B", 18, 25, 19, 28), ("C", "B", 28, 25, 29, 28),
    ("C", "D", 33, 19, 35, 20),
    ("A", "D", 45, 11, 46, 14), ("D", "B", 52, 25, 53, 28),
]
walker_bridges = []
for a, b, x0, z0, x1, z1 in BRIDGES:
    c.rect(x0, z0, x1, z1, "+")
    span_x = (x1 - x0) > (z1 - z0)
    cxm, czm = (x0 + x1 + 1) / 2.0, (z0 + z1 + 1) / 2.0
    if span_x:
        m.prop("bridge", cxm, czm, yaw=0, w=(x1 - x0 + 1) * CS, d=(z1 - z0 + 1) * CS)
        end_a, end_b = (x0 - 1.0, czm), (x1 + 2.0, czm)
    else:
        m.prop("bridge", cxm, czm, yaw=90, w=(z1 - z0 + 1) * CS, d=(x1 - x0 + 1) * CS)
        end_a, end_b = (cxm, z0 - 1.0), (cxm, z1 + 2.0)
    walker_bridges.append(f"{a},{b},{end_a[0] * CS:g},{end_a[1] * CS:g},{end_b[0] * CS:g},{end_b[1] * CS:g}")
# Houses: rows along the north and south edges, more in the west.
HOUSES = ["house_red", "house_yellow", "house_blue", "house_green", "house_white"]
ROOFS = ["roof_tile", "roof_slate", "roof_tile", "roof_copper", "roof_slate"]


def house(x, z, w, d, k, h=1.1):
    m.prop("house", x, z, w=w * CS, d=d * CS, h=h, mat=HOUSES[k % 5], roof=ROOFS[(k * 2 + 1) % 5])


k = 0
for x in (3.5, 9.0, 21.0, 27.5, 34.0, 40.5, 48.0, 54.5):  # north edge
    house(x, 2.75, 4 + (k % 2), 3.5, k); k += 1
for x in (6.0, 13.0, 24.0, 31.0, 38.0, 46.0, 53.0):      # south edge
    house(x, 37.25, 4 + (k % 3 == 0), 3.5, k); k += 1
house(4.0, 12.0, 4, 3, k); k += 1
house(10.5, 13.5, 3, 4, k); k += 1
house(4.5, 26.0, 5, 3, k); k += 1
house(11.0, 31.5, 4, 3, k); k += 1
house(41.0, 33.0, 4, 3, k); k += 1
# The market square (B) with its fountain; the cathedral on Kneiphof.
m.prop("fountain", 25.0, 32.5, r=0.6)
m.prop("church", 24.5, 19.75, w=3.5, d=1.8, mat="cathedral", roof="roof_copper")
m.prop("sign", 29.5, 31.0, yaw=90, text="Königsberg, 1736")
# Trees on Lomse and the island, lamps along the banks.
for (x, z) in ((39.0, 17.0), (42.5, 22.5), (47.0, 18.0), (51.0, 22.0), (55.5, 17.5), (57.0, 22.5), (18.5, 16.5), (31.0, 23.5)):
    m.prop("tree", x, z)
house(50.0, 19.5, 3, 3, 3)
for x in (8.0, 24.0, 38.0, 52.0):
    m.prop("lamp", x, 10.2)
    m.prop("lamp", x + 3.0, 29.8)
m.mover("walker", 12.0, 7.0, land="A", bridges=";".join(walker_bridges))
m.focus = (27.0, 20.0)
maps.append(m)


# ---------------------------------------------------------------------------
# Library. Borges' Library of Babel: an endless honeycomb of identical
# hexagonal galleries, shelves on every wall, a ventilation shaft in the
# middle of each, and somewhere the Crimson Hexagon. "The Library is a sphere
# whose exact centre is any one of its hexagons": nineteen of them here, two
# rings round a middle one. Every gallery looks like every other, so this is
# the easy map: only the edge of the honeycomb and the crimson shelves tell
# Wobo where it is.
# ---------------------------------------------------------------------------
m = Map("library", 1, 1, "Library",
        "Nineteen hexagonal galleries, shelved alike from floor to ceiling. Only the outer walls, and "
        "one gallery said to be shelved in crimson, tell them apart.",
        0, "Easy")
m.material("H", "shelf", name="Bookshelf", rgb="122, 86, 54", solid=True, height=0.8, look="books")
m.material("X", "crimson_shelf", name="Crimson shelf", rgb="150, 30, 50", solid=True, height=0.8, look="books")
# Beyond the honeycomb: more of the library, out of sight.
m.material("V", "beyond", name="Beyond", rgb="36, 40, 66", solid=True, height=0.02)
RINGS = 2               # galleries round the middle one
HEX_PX, HEX_PZ = 13, 15  # pitch across and down, in cells (4.3 m sides); odd columns sit 7 cells lower
CRIMSON = (2, 0)        # (column, row) of the Crimson Hexagon: the outer ring's northern tip
DOOR_R = 1.6            # doorway half-width (cells) round the middle of a shared wall
WALL_T = 2.6            # how much nearer one gallery a cell must be to be its floor


def hex_centre(i, j):
    return (HEX_PX * i, HEX_PZ * j + (HEX_PZ // 2 if i % 2 else 0))


def hex_distance(a, b):
    (x1, z1), (x2, z2) = [(i, j - (i - (i & 1)) // 2) for i, j in (a, b)]
    return max(abs(x1 - x2), abs(z1 - z2), abs(x1 + z1 - x2 - z2))


SPAN = range(-1, 2 * RINGS + 2)
LATTICE = {(i, j): hex_centre(i, j) for i in SPAN for j in SPAN}
GALLERIES = {k: v for k, v in LATTICE.items() if hex_distance(k, (RINGS, RINGS)) <= RINGS}
# Canvas: the galleries' centres plus a hexagon's reach and a cell of margin.
reach_x, reach_z = round(HEX_PZ / 3 ** 0.5) + 2, HEX_PZ // 2 + 2
ox = min(x for x, _ in GALLERIES.values()) - reach_x
oz = min(z for _, z in GALLERIES.values()) - reach_z
lw = max(x for x, _ in GALLERIES.values()) - ox + reach_x
lh = max(z for _, z in GALLERIES.values()) - oz + reach_z
m.c = Canvas(lw, lh, border="V", fill="V")
for z in range(lh):
    for x in range(lw):
        p = (x + 0.5 + ox, z + 0.5 + oz)
        dist = lambda k: ((LATTICE[k][0] - p[0]) ** 2 + (LATTICE[k][1] - p[1]) ** 2) ** 0.5
        a, b = sorted(LATTICE, key=lambda k: (dist(k), k))[:2]
        if a not in GALLERIES:
            continue
        if dist(b) - dist(a) >= WALL_T:
            m.c.set(x, z, "+")
            continue
        m.c.set(x, z, "X" if a == CRIMSON else "H")
        # Doorways in the walls running north-south and north-east-south-west.
        dx, dz = LATTICE[b][0] - LATTICE[a][0], LATTICE[b][1] - LATTICE[a][1]
        if b in GALLERIES and (dx == 0 or dx * dz < 0):
            mid = ((LATTICE[a][0] + LATTICE[b][0]) / 2, (LATTICE[a][1] + LATTICE[b][1]) / 2)
            if (p[0] - mid[0]) ** 2 + (p[1] - mid[1]) ** 2 <= DOOR_R ** 2:
                m.c.set(x, z, "+")
for (x, z) in GALLERIES.values():
    m.prop("shaft", float(x - ox), float(z - oz), r=0.6)
# In the Crimson Hexagon, the catalogue of catalogues lies open on a stand.
cx, cz = hex_centre(*CRIMSON)
m.prop("lectern", cx - ox + 2.0, cz - oz + 1.5, yaw=45, text="Catalogue|of catalogues")
m.focus = (hex_centre(*CRIMSON)[0] - ox, hex_centre(*CRIMSON)[1] - oz + 2.0)
maps.append(m)

# ---------------------------------------------------------------------------

def check(m):
    rows = m.c.rows()
    w = max(len(r) for r in rows)
    for i, r in enumerate(rows):
        assert len(r) == w, f"{m.id}: row {i} has width {len(r)} != {w}"
    solid = lambda ch: m.legend[ch] not in FLOORS
    free = [(x, z) for z, r in enumerate(rows) for x, ch in enumerate(r) if not solid(ch)]
    seen = {free[0]}
    stack = [free[0]]
    while stack:
        x, z = stack.pop()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, z + dz)
            if 0 <= n[1] < len(rows) and 0 <= n[0] < w and not solid(rows[n[1]][n[0]]) and n not in seen:
                seen.add(n)
                stack.append(n)
    unreachable = set(free) - seen
    assert not unreachable, f"{m.id}: unreachable {sorted(unreachable)[:5]}"
    print(f"{m.id}: {w}x{len(rows)} cells, {len(free)} free, {len(m.props)} props, {len(m.movers)} movers")


for m in maps:
    check(m)
    rows = m.c.rows()
    used = sorted({ch for r in rows for ch in r})
    os.makedirs(os.path.join(sys.argv[1], m.id), exist_ok=True)
    with open(os.path.join(sys.argv[1], m.id, "map.txt"), "w") as f:
        f.write("# Generated by tools/make_maps.py (edit there and regenerate). Format: src/world/map_loader.h.\n")
        f.write(f"name = {m.name}\ndescription = {m.desc}\norder = {m.order}\n"
                f"difficulty = {m.difficulty}\ncell_size = {CS:g}\n")
        if m.focus:
            f.write(f"focus = {m.focus[0] * CS:g}, {m.focus[1] * CS:g}\n")
        for mid, kw in m.materials:
            f.write(f"\n[material.{mid}]\n")
            for key, v in kw.items():
                f.write(f"{key} = {str(v).lower() if isinstance(v, bool) else v}\n")
        f.write("\n[legend]\n")
        for ch in used:
            f.write(f"{'hash' if ch == '#' else ch} = {m.legend[ch]}\n")
        if m.props:
            f.write("\n[props]\n")
            for kind, x, z, kw in m.props:
                f.write(f"{kind} {x * CS:g} {z * CS:g}" + "".join(f" {k}={fmt(v)}" for k, v in kw.items()) + "\n")
        if m.movers:
            f.write("\n[movers]\n")
            for kind, x, z, kw in m.movers:
                f.write(f"{kind} {x * CS:g} {z * CS:g}" + "".join(f" {k}={fmt(v)}" for k, v in kw.items()) + "\n")
        f.write("\n[grid]\n" + "\n".join(rows) + "\n")
