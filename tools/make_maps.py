# Regenerates data/maps/*.txt. Usage: python3 tools/make_maps.py data/maps
import sys, os
out = sys.argv[1]
LEGEND = """[legend]
hash = wall
. = floor
d = wall_dark
l = wall_light
R = wall_red
B = wall_blue
Y = wall_yellow
G = wall_green
P = wall_purple
C = crate
I = pillar
, = rug_blue
; = rug_green
' = rug_pink
: = tile
"""
SOLID = set("#dlRBYGPCI")

class Canvas:
    def __init__(s, w, h, border="#"):
        s.w, s.h = w, h
        s.g = [["." for _ in range(w)] for _ in range(h)]
        s.rect(0, 0, w - 1, h - 1, border, fill=False)
    def set(s, x, z, c):
        s.g[z][x] = c
    def rect(s, x0, z0, x1, z1, c, fill=True):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                if fill or x in (x0, x1) or z in (z0, z1):
                    s.g[z][x] = c
    def rows(s):
        return ["".join(r) for r in s.g]

def rot180(rows):
    return [r[::-1] for r in rows[::-1]]

maps = {}

# 1. Workshop: distinctive landmarks, but its shelving aisles repeat.
c = Canvas(36, 24)
for z in (4, 8, 12):
    c.rect(3, z, 9, z, "C")
c.rect(15, 8, 18, 8, "#"); c.rect(15, 8, 15, 12, "#"); c.rect(18, 8, 18, 12, "#")
c.rect(16, 8, 17, 8, "Y")
c.rect(24, 1, 24, 14, "#"); c.set(24, 6, "."); c.set(24, 7, "."); c.set(24, 8, ".")
c.rect(35, 3, 35, 6, "B")
c.set(30, 10, "I")
c.rect(26, 2, 30, 5, ",")
c.rect(6, 23, 10, 23, "G")
c.rect(0, 16, 0, 19, "R")
c.set(8, 17, "I"); c.set(14, 19, "I")
c.rect(28, 19, 29, 19, "C")
c.rect(19, 17, 23, 20, ";")
c.rect(24, 21, 24, 22, "#"); c.rect(24, 21, 27, 21, "#")
maps["workshop"] = dict(order=4, difficulty="Hard", name="Workshop",
    desc="Shelving aisles that all look alike, and a few landmarks that don't. Keep Wobo away from the colours.",
    rows=c.rows())

# 2. Offices: twelve identical rooms off one hallway.
c = Canvas(41, 17)
for row_z0, door_z in ((1, 6), (11, 10)):
    for i in range(6):
        x0 = 1 + i * 6
        c.rect(x0 + 5, row_z0, x0 + 5, row_z0 + 4, "#")
    c.rect(1, door_z, 36, door_z, "#")
    for i in range(6):
        c.set(1 + i * 6 + 2, door_z, ".")
c.rect(1, 7, 39, 9, ":")
c.set(3, 13, "C")
c.set(39, 3, "I")
maps["offices"] = dict(order=1, difficulty="Easy", name="Office Block",
    desc="Twelve identical offices off one long hallway. Repetition is your best friend.",
    rows=c.rows())

# 3. Hall of Mirrors: 180-degree symmetric; one blue wall in one corner breaks the tie.
c = Canvas(37, 23)
c.rect(8, 1, 8, 6, "#"); c.rect(1, 6, 5, 6, "#")
c.rect(14, 4, 22, 4, "#"); c.rect(14, 4, 14, 8, "#")
c.rect(28, 1, 28, 5, "#"); c.rect(28, 8, 35, 8, "#"); c.set(31, 8, "."); c.set(32, 8, ".")
c.rect(4, 10, 10, 10, "#"); c.rect(10, 10, 10, 11, "#")
c.set(18, 11, "I")
c.rect(24, 12, 30, 12, "#")
c.set(20, 7, "I")
top = c.rows()
full = [list(r) for r in top]
rot = rot180(top)
for z in range(23):
    for x in range(37):
        if rot[z][x] in SOLID:
            full[z][x] = rot[z][x]
full[1][1] = "B"; full[1][2] = "B"; full[2][1] = "B"
rows = ["".join(r) for r in full]
maps["mirror"] = dict(order=2, difficulty="Easy", name="Hall of Mirrors",
    desc="Perfectly symmetric - except for one blue corner. Wobo will see double for a long time.",
    rows=rows)

# 4. Paint Gallery: identical bays told apart only by colour.
c = Canvas(39, 15)
cols = "RBGPYRB"
for i, x in enumerate(range(5, 38, 6)):
    col = cols[i % len(cols)]
    c.rect(x, 1, x, 4, col)
    c.rect(x, 10, x, 13, cols[(i + 3) % len(cols)])
for x in range(8, 36, 12):
    c.rect(x, 7, x + 1, 7, "#")
maps["gallery"] = dict(order=3, difficulty="Medium", name="Paint Gallery",
    desc="Bays that share one shape but not one colour. Scramble the colours to keep Wobo guessing.",
    rows=c.rows())

# 5. Greyscale Yard: one colour, many shapes.
c = Canvas(36, 22)
c.rect(9, 1, 9, 5, "l")
c.rect(3, 3, 5, 3, "d"); c.rect(3, 3, 3, 6, "d")
c.rect(17, 2, 18, 3, "d")
c.rect(26, 1, 26, 7, "l"); c.rect(26, 7, 31, 7, "l")
c.set(12, 8, "I"); c.set(21, 9, "I")
c.rect(13, 12, 16, 12, "d"); c.rect(16, 12, 16, 15, "d")
c.rect(1, 13, 3, 13, "l")
c.rect(26, 13, 28, 15, "d")
c.set(6, 18, "I")
c.rect(20, 18, 23, 18, "l"); c.rect(20, 17, 20, 18, "l")
c.rect(31, 16, 33, 16, "l")
c.rect(9, 18, 9, 20, "d")
maps["greyscale"] = dict(order=5, difficulty="Hard", name="Greyscale Yard",
    desc="Everything is grey, but every shape is different. Geometry gives Wobo away; colour won't help it.",
    rows=c.rows())

def check(name, rows):
    w = max(len(r) for r in rows)
    for i, r in enumerate(rows):
        assert len(r) == w, f"{name}: row {i} has width {len(r)} != {w}"
    free = [(x, z) for z, r in enumerate(rows) for x, ch in enumerate(r) if ch not in SOLID]
    seen = {free[0]}
    stack = [free[0]]
    while stack:
        x, z = stack.pop()
        for dx, dz in ((1,0),(-1,0),(0,1),(0,-1)):
            n = (x+dx, z+dz)
            if 0 <= n[1] < len(rows) and 0 <= n[0] < w and rows[n[1]][n[0]] not in SOLID and n not in seen:
                seen.add(n); stack.append(n)
    unreachable = set(free) - seen
    assert not unreachable, f"{name}: unreachable {sorted(unreachable)[:5]}"
    print(f"{name}: {w}x{len(rows)} cells, {len(free)} free")

for mid, m in maps.items():
    rows = m["rows"]
    check(mid, rows)
    with open(os.path.join(out, mid + ".txt"), "w") as f:
        f.write("# Generated by tools/make_maps.py (edit there and regenerate). Format: src/world/map_loader.h.\n")
        f.write(f"name = {m['name']}\ndescription = {m['desc']}\norder = {m['order']}\ndifficulty = {m['difficulty']}\ncell_size = 0.5\n\n")
        f.write(LEGEND)
        f.write("\n[grid]\n" + "\n".join(rows) + "\n")
