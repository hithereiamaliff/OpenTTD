"""Draw the city fork's sprites for openttd.grf (8bpp, OpenTTD DOS palette).

Usage: python3 make_city_sprites.py media/baseset/openttd
Writes city.png and city.nfo next to this script; the build turns them into
openttd.grf (needs grfcodec; the generated openttd.grf is committed).

Block layout (Action 5 type 0x1C, see SPR_TRAFFIC_LIGHTS_BASE in table/sprites.h):
   0-3   traffic light post: front red, front amber, front green, back
   4-22  tile overlay in company colours, one per SlopeToSpriteOffset (0-18)
  23-24  icons: traffic lights, road traffic overlay
  25-29  icons: zone residential, commercial, industrial, park, clear zone
  30-34  city service buildings: police, fire, clinic, school, park
  35-39  icons: police, fire, clinic, school, park
  40     icon: network map of lines
  41-72  street life vehicles: car, van, lorry, motorbike; 8 directions each (N, NE, E, SE, S, SW, W, NW)
  73-78  street life people: light and dark skin; walking (two steps) and standing; clothes in company colours
"""
import math
import sys
from PIL import Image

SRC = sys.argv[1]  # media/baseset/openttd
pal = Image.open(f"{SRC}/oneway.png").getpalette()


def nearest(rgb):
    best = None
    for i in range(1, 0xE3):  # skip transparent and the animated colours
        c = pal[i * 3:i * 3 + 3]
        d = sum((a - b) ** 2 for a, b in zip(c, rgb))
        if best is None or d < best[0]:
            best = (d, i)
    return best[1]


T = 0
BLACK, DARK, BACK = 0x01, 0x02, 0x04
POLE, POLE_DARK = 0x09, 0x06
RED, RED_HI, RED_OFF = 0xB8, 0xBA, 0xB2
AMBER, AMBER_HI, AMBER_OFF = 0xBC, 0xBE, 0x69
GREEN, GREEN_HI, GREEN_OFF = 0xD0, 0xD1, 0x58
CC_FILL, CC_EDGE = 0xCA, 0xC8
GREY_ROAD, GREY_ROAD_EDGE = 0x07, 0x05
WHITE, LIGHT = 0x0E, 0x0C
BLUE, BLUE_DARK, BLUE_LIGHT = nearest((60, 120, 200)), nearest((30, 60, 120)), nearest((120, 170, 230))
BROWN, BROWN_DARK = nearest((150, 90, 40)), nearest((90, 50, 20))
YELLOW = nearest((252, 220, 0))
TREE, TREE_DARK = nearest((60, 150, 40)), nearest((30, 90, 20))

W, H = 512, 2440
sheet = Image.new("P", (W, H), T)
sheet.putpalette(pal)
px = sheet.load()
sprites = []  # (x, y, w, h, xrel, yrel, name)


def put(x0, y0, rows, colours):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != '.':
                px[x0 + x, y0 + y] = colours[ch]


POST = [".BB.", "BrrB", "BrrB", "BBBB", "BaaB", "BaaB", "BBBB", "BggB", "BggB", ".BB."] + [".Pp."] * 7


def light(x0, lit):
    col = {'B': BLACK, 'P': POLE, 'p': POLE_DARK,
           'r': RED if lit == 'r' else RED_OFF,
           'a': AMBER if lit == 'a' else AMBER_OFF,
           'g': GREEN if lit == 'g' else GREEN_OFF}
    put(x0, 4, POST, col)
    row, colour = {'r': (1, RED_HI), 'a': (4, AMBER_HI), 'g': (7, GREEN_HI)}[lit]
    px[x0 + 1, 4 + row] = colour
    sprites.append((x0, 4, 4, 17, -1, -16, f"light front {lit}"))


def light_back(x0):
    rows = [r.replace('r', 'k').replace('a', 'k').replace('g', 'k') for r in POST]
    rows = [".BB."] + ["BkkB"] * 8 + [".BB."] + [".Pp."] * 7
    put(x0, 4, rows, {'B': DARK, 'k': BACK, 'P': POLE, 'p': POLE_DARK})
    sprites.append((x0, 4, 4, 17, -1, -16, "light back"))


light(4, 'r')
light(12, 'a')
light(20, 'g')
light_back(28)

# Slope (bits: W=1, S=2, E=4, N=8, steep=16) for each sprite offset, see _slope_to_sprite_offset.
SLOPE_FOR_OFFSET = list(range(15)) + [0x1D, 0x17, 0x1B, 0x1E]
CORNER_STEP = 8  # pixels per height level at normal zoom


def corner_raises(slope):
    """Raise in pixels of the N, E, S and W corners of a slope."""
    w, s, e, n = (slope & 1) != 0, (slope & 2) != 0, (slope & 4) != 0, (slope & 8) != 0
    raises = {'n': n, 'e': e, 's': s, 'w': w}
    raises = {k: CORNER_STEP * v for k, v in raises.items()}
    if slope & 0x10:
        # Steep: the corner opposite the lowered one is two levels up.
        lowered = [k for k, v in raises.items() if v == 0][0]
        opposite = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e'}[lowered]
        raises[opposite] = 2 * CORNER_STEP
    return raises


def tile_pixels(r):
    """Pixels inside a tile outline with the given corner raises, by pixel centre.
    With no raises this reproduces the 64x31 ground tile exactly."""
    poly = [(32, -0.5 - r['n']), (64, 15.5 - r['e']), (32, 31.5 - r['s']), (0, 15.5 - r['w'])]
    top, bottom = min(p[1] for p in poly), max(p[1] for p in poly)
    pts = set()
    for y in range(math.floor(top), math.ceil(bottom) + 1):
        yc = y + 0.5
        xs = []
        for i in range(4):
            (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % 4]
            if (y1 <= yc < y2) or (y2 <= yc < y1):
                xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
        if len(xs) >= 2:
            xl, xr = min(xs), max(xs)
            for x in range(math.floor(xl) - 1, math.ceil(xr) + 1):
                if xl < x + 0.5 < xr:
                    pts.add((x, y))
    return pts


def overlay(x0, y0, slope):
    pts = tile_pixels(corner_raises(slope))
    minx, miny = min(p[0] for p in pts), min(p[1] for p in pts)
    maxx, maxy = max(p[0] for p in pts), max(p[1] for p in pts)
    for (x, y) in pts:
        edge = any((x + dx, y + dy) not in pts for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if edge:
            px[x0 + x - minx, y0 + y - miny] = CC_EDGE
        elif (x + y) % 2 == 0:
            px[x0 + x - minx, y0 + y - miny] = CC_FILL
    sprites.append((x0, y0, maxx - minx + 1, maxy - miny + 1, -31 + minx, miny, f"overlay slope 0x{slope:02X}"))
    return maxy - miny + 1


# 19 overlays in rows of 7, each cell 68x56.
for k, slope in enumerate(SLOPE_FOR_OFFSET):
    cx = 40 + (k % 7) * 66
    cy = 2 + (k // 7) * 50 if k < 14 else 102
    if k >= 14:
        cx = 40 + (k - 14) * 66
    overlay(cx, cy, slope)


def icon(x0, y0, rows, colours, name):
    put(x0, y0, rows, colours)
    sprites.append((x0, y0, len(rows[0]), len(rows), 0, 0, name))


icon(372, 104, [
    "..BBBBBB..",
    ".BBBBBBBB.",
    ".BBRRRRBB.",
    ".BRRRRRRB.",
    ".BRRRRRRB.",
    ".BBRRRRBB.",
    ".BBAAAABB.",
    ".BAAAAAAB.",
    ".BAAAAAAB.",
    ".BBAAAABB.",
    ".BBGGGGBB.",
    ".BGGGGGGB.",
    ".BGGGGGGB.",
    ".BBGGGGBB.",
    "..BBBBBB..",
    "....Pp....",
    "....Pp....",
    "....Pp....",
    "..PPPPpp..",
], {'B': BLACK, 'R': RED, 'A': AMBER, 'G': GREEN, 'P': POLE, 'p': POLE_DARK}, "icon traffic lights")

icon(386, 104, [
    "..EEEEEEEEEEEEEEEE..",
    ".EggggggyyyyyyrrrrE.",
    "EggggggyyyyyyrrrrrrE",
    "EggggggyyyyyyrrrrrrE",
    "EggggggyyyyyyrrrrrrE",
    ".EggggggyyyyyyrrrrE.",
    "..EEEEEEEEEEEEEEEE..",
    "....................",
    "..EEEEEEEEEEEEEEEE..",
    ".EwwwwwwwwwwwwwwwwE.",
    "EwwwwwwwwwwwwwwwwwwE",
    ".EwwwwwwwwwwwwwwwwE.",
    "..EEEEEEEEEEEEEEEE..",
], {'E': GREY_ROAD_EDGE, 'g': GREEN, 'y': AMBER, 'r': RED, 'w': GREY_ROAD}, "icon traffic overlay")

icon(410, 104, [
    ".......K.......",
    "......KgK......",
    ".....KgggK.....",
    "....KgggggK....",
    "...KgggggggK...",
    "..KgggggggggK..",
    ".KgggggggggggK.",
    "KKKKKKKKKKKKKKK",
    ".KwwwwwwwwwwwK.",
    ".KwBBwwwwwBBwK.",
    ".KwBBwwwwwBBwK.",
    ".KwwwwwwwwwwwK.",
    ".KwwwwwDDwwwwK.",
    ".KwwwwwDDwwwwK.",
    ".KwwwwwDDwwwwK.",
    ".KKKKKKKKKKKKK.",
], {'K': BLACK, 'g': GREEN, 'w': LIGHT, 'B': BLUE_LIGHT, 'D': BROWN_DARK}, "icon zone residential")

icon(428, 102, [
    ".KKKKKKKKK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbLbLbLbK.",
    ".KbbbbbbbK.",
    ".KbbbDbbbK.",
    ".KbbbDbbbK.",
    "KKKKKKKKKKK",
], {'K': BLACK, 'b': BLUE, 'L': BLUE_LIGHT, 'D': BLUE_DARK}, "icon zone commercial")

icon(442, 102, [
    "..............KK.",
    "..............KK.",
    "..........KK..KK.",
    "..........KK..KK.",
    "..........KK..KK.",
    "K....K....KK..KK.",
    "KK...KK...KK..KK.",
    "KyK..KyK..KyK.KK.",
    "KyyK.KyyK.KyyKKK.",
    "KyyyKKyyyKKyyyyK.",
    "KyyyyyyyyyyyyyyK.",
    "KyBByyBByyBByyyK.",
    "KyBByyBByyBByyyK.",
    "KyyyyyyyyyyyyyyK.",
    "KyyyyyyDDyyyyyyK.",
    "KyyyyyyDDyyyyyyK.",
    "KKKKKKKKKKKKKKKK.",
], {'K': BLACK, 'y': YELLOW, 'B': BROWN_DARK, 'D': BROWN}, "icon zone industrial")

icon(462, 102, [
    ".....TTTT.....",
    "...TTttttTT...",
    "..TttttttttT..",
    ".TttttTtttttT.",
    ".TtttttttTttT.",
    "TttTttttttttT.",
    "TtttttttttttTT",
    "TttttTttttTttT",
    ".TttttttttttT.",
    ".TTtttTtttttT.",
    "..TTttttttTT..",
    "....TTTTTT....",
    "......BB......",
    "......BB......",
    "......BB......",
    "....BBBBBB....",
], {'T': TREE_DARK, 't': TREE, 'B': BROWN_DARK}, "icon zone park")

icon(480, 104, [
    "RR..........RR",
    "RRR........RRR",
    ".RRR......RRR.",
    "..RRR....RRR..",
    "...RRR..RRR...",
    "....RRRRRR....",
    ".....RRRR.....",
    ".....RRRR.....",
    "....RRRRRR....",
    "...RRR..RRR...",
    "..RRR....RRR..",
    ".RRR......RRR.",
    "RRR........RRR",
    "RR..........RR",
], {'R': RED}, "icon zone clear")

assert len(sprites) == 30, len(sprites)

# ---------------------------------------------------------------------------
# City service buildings. Each is drawn in "tile coordinates": the tile's north
# corner is at x = 32, ground level at y = 0, like the overlays above. World
# position (x, y) in 1/16 tile units and height z in pixels project to
#   sx = 32 + 2 * (y - x),  sy = (x + y) - z.
# Visible faces of a box are its south-west face (x = x1, lit), its south-east
# face (y = y1, shaded) and its top.


def nearest_fixed(rgb):
    """Like nearest(), but never a company colour (0xC6-0xCD): those get
    recoloured to the owner's colour on buildings drawn in company colours."""
    best = None
    for i in range(1, 0xE3):
        if 0xC6 <= i <= 0xCD:
            continue
        c = pal[i * 3:i * 3 + 3]
        d = sum((a - b) ** 2 for a, b in zip(c, rgb))
        if best is None or d < best[0]:
            best = (d, i)
    return best[1]


def rgb_index(rgb, f=1.0):
    return nearest_fixed(tuple(max(0, min(255, int(c * f))) for c in rgb))


class Building:
    def __init__(self):
        self.pix = {}

    def set(self, x, y, colour):
        if colour is not None:
            self.pix[(x, y)] = colour

    def box(self, x0, y0, x1, y1, z0, z1, wall, top, sw=None, se=None, roof=None, shade=0.72):
        """Paint a box. wall/top: (r, g, b). sw/se/roof: optional functions
        (u, v) -> palette index or None, u along the face in tile units, v the
        height above z0 in pixels (for the roof: u, v are x, y in tile units)."""
        sw_col, se_col, top_col = rgb_index(wall), rgb_index(wall, shade), rgb_index(top)
        lo_x, hi_x = math.floor(32 + 2 * (y0 - x1)) - 1, math.ceil(32 + 2 * (y1 - x0)) + 1
        lo_y, hi_y = math.floor(x0 + y0 - z1) - 1, math.ceil(x1 + y1 - z0) + 1
        for py in range(lo_y, hi_y + 1):
            for px_ in range(lo_x, hi_x + 1):
                cx, cy = px_ + 0.5, py + 0.5
                # Top face.
                d, sm = (cx - 32) / 2, cy + z1
                x, y = (sm - d) / 2, (sm + d) / 2
                if x0 <= x <= x1 and y0 <= y <= y1:
                    c = roof(x, y) if roof else None
                    self.set(px_, py, c if c is not None else top_col)
                    continue
                # South-west face, x = x1.
                y = (cx - 32) / 2 + x1
                z = x1 + y - cy
                if y0 <= y <= y1 and z0 <= z <= z1:
                    c = sw(y - y0, z - z0) if sw else None
                    self.set(px_, py, c if c is not None else sw_col)
                    continue
                # South-east face, y = y1.
                x = y1 - (cx - 32) / 2
                z = x + y1 - cy
                if x0 <= x <= x1 and z0 <= z <= z1:
                    c = se(x - x0, z - z0) if se else None
                    self.set(px_, py, c if c is not None else se_col)

    def ground(self, fn, x0=0, y0=0, x1=16, y1=16):
        """Paint the ground (z = 0) where fn(x, y) returns a colour."""
        for py in range(-2, 34):
            for px_ in range(0, 65):
                cx, cy = px_ + 0.5, py + 0.5
                d = (cx - 32) / 2
                x, y = (cy - d) / 2, (cy + d) / 2
                if x0 <= x < x1 and y0 <= y < y1:
                    self.set(px_, py, fn(x, y))

    def ground_sloped(self, fn, slope):
        """Paint the ground of a tile with two neighbouring corners raised one level
        (SLOPE_NE, SLOPE_SE, SLOPE_SW or SLOPE_NW), where fn(x, y) returns a colour."""
        # Height z = a * x + b * y + c over the tile; the N corner is (0, 0), E (0, 16), S (16, 16), W (16, 0).
        a, b, c = {0x0C: (-0.5, 0, 8), 0x06: (0, 0.5, 0), 0x03: (0.5, 0, 0), 0x09: (0, -0.5, 8)}[slope]
        for py in range(-10, 34):
            for px_ in range(0, 65):
                cx, cy = px_ + 0.5, py + 0.5
                d = (cx - 32) / 2
                # On screen cx = 32 + 2 * (y - x) and cy = x + y - z.
                x = (cy - d * (1 - b) + c) / (2 - a - b)
                y = x + d
                if 0 <= x < 16 and 0 <= y < 16:
                    self.set(px_, py, fn(x, y))

    def pole(self, x, y, z0, z1, flag=True):
        """A flag pole with a flag in company colours on top."""
        sx, sy = round(32 + 2 * (y - x)), round(x + y)
        for z in range(z0, z1):
            self.set(sx, sy - z, rgb_index((120, 120, 120)))
        if flag:
            for fy in range(3):
                for fx in range(1, 5):
                    self.set(sx + fx, sy - z1 + 1 + fy + (fx // 3), 0xC9 if fy < 2 else 0xC7)

    def sign(self, x, y, z, rows, colours):
        """A sign facing the viewer, its bottom centre at world (x, y, z)."""
        sx, sy = round(32 + 2 * (y - x)), round(x + y - z)
        w, h = len(rows[0]), len(rows)
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch != '.':
                    self.set(sx - w // 2 + i, sy - h + j, colours[ch])

    def tree(self, x, y, size=4.5, height=9):
        sx, sy = 32 + 2 * (y - x), x + y
        trunk = rgb_index((90, 55, 25))
        for z in range(0, 4):
            self.set(round(sx), round(sy - z), trunk)
            self.set(round(sx) - 1, round(sy - z), rgb_index((70, 40, 20)))
        cx, cy = sx, sy - height
        for py in range(math.floor(cy - size) - 1, math.ceil(cy + size) + 1):
            for px_ in range(math.floor(cx - size * 1.1) - 1, math.ceil(cx + size * 1.1) + 1):
                dx, dy = (px_ + 0.5 - cx) / 1.1, py + 0.5 - cy
                r = math.hypot(dx, dy)
                if r <= size:
                    light = -(dx + dy) / size  # lit from the top left
                    f = 0.75 + 0.35 * light + (0.12 if (px_ * 7 + py * 13) % 5 == 0 else 0)
                    self.set(px_, py, rgb_index((70, 150, 45), f))

    def emit(self, x0, y0, name):
        """Copy into the sheet at (x0, y0) and record the sprite."""
        minx = min(p[0] for p in self.pix)
        miny = min(p[1] for p in self.pix)
        maxx = max(p[0] for p in self.pix)
        maxy = max(p[1] for p in self.pix)
        for (x, y), c in self.pix.items():
            px[x0 + x - minx, y0 + y - miny] = c
        sprites.append((x0, y0, maxx - minx + 1, maxy - miny + 1, -31 + minx, miny, name))
        return maxx - minx + 1


def windows(period, width, rows, glass, frame=None, start=1.0, stop=None):
    """Window pattern for a face: rows is a list of (v0, v1) pixel bands."""
    def fn(u, v):
        if stop is not None and u > stop:
            return None
        if u < start:
            return None
        if (u - start) % period < width:
            for (v0, v1) in rows:
                if v0 <= v < v1:
                    return glass
        return None
    return fn


def combine(*fns):
    def fn(u, v):
        for f in fns:
            c = f(u, v)
            if c is not None:
                return c
        return None
    return fn


GLASS = rgb_index((40, 60, 90))
GLASS_LIGHT = rgb_index((140, 180, 220))
ROOF_GREY = (95, 95, 100)

# Police station: grey walls, a blue-and-white chequered band, blue lamp on the roof.
police = Building()
band_blue, band_white = rgb_index((30, 50, 150)), rgb_index((235, 235, 240))
def chequer(u, v):
    if 13 <= v < 17:
        return band_white if (int(u / 1.5) + int((v - 13) / 2)) % 2 == 0 else band_blue
    return None
def police_door(u, v):
    return rgb_index((30, 40, 70)) if 5.5 <= u <= 8.5 and v < 7 else None
police.box(2, 2, 14, 14, 0, 18, (185, 190, 200), ROOF_GREY,
           sw=combine(chequer, windows(3, 1.6, [(3, 6), (8, 11)], GLASS)),
           se=combine(chequer, police_door, windows(3, 1.6, [(3, 6), (8, 11)], GLASS)),
           roof=lambda x, y: rgb_index((120, 120, 125)) if x < 2.7 or y < 2.7 or x > 13.3 or y > 13.3 else None)
police.box(7, 7, 9, 9, 18, 21, (40, 70, 200), (120, 160, 255))
police.pole(3.2, 3.2, 18, 29)
police.emit(4, 160, "police station")

# Fire station: red brick, two big doors at the front, a hose tower at the back.
fire = Building()
fire.box(2, 2, 5.5, 5.5, 0, 33, (160, 55, 40), ROOF_GREY)
door_red, door_line = rgb_index((215, 45, 30)), rgb_index((150, 25, 20))
def fire_doors(u, v):
    for (a, b) in ((1.0, 5.4), (6.6, 11.0)):
        if a <= u <= b and v < 10:
            return door_line if int(v) % 2 == 0 else door_red
    return None
fire.box(2, 2, 14, 14, 0, 15, (170, 60, 45), ROOF_GREY,
         sw=windows(3, 1.6, [(6, 10)], GLASS),
         se=combine(fire_doors, windows(3, 1.6, [(11, 13.5)], GLASS)))
fire.pole(3.6, 3.6, 33, 41)
fire.emit(74, 160, "fire station")

# Clinic: white walls, light blue windows, red crosses on the front and the roof.
clinic = Building()
cross_red = rgb_index((215, 30, 30))
def clinic_door(u, v):
    return GLASS_LIGHT if 5 <= u <= 7 and v < 6 else None
def roof_cross(x, y):
    if x < 2.7 or y < 2.7 or x > 13.3 or y > 13.3:
        return rgb_index((225, 225, 230))
    return None
clinic.box(2, 2, 14, 14, 0, 17, (238, 238, 240), (195, 195, 200),
           sw=windows(3, 1.6, [(3, 6), (9, 12)], GLASS_LIGHT),
           se=combine(clinic_door, windows(3, 1.6, [(3, 6)], GLASS_LIGHT, stop=3.5),
                      windows(3, 1.6, [(3, 6)], GLASS_LIGHT, start=8.5),
                      windows(3, 1.6, [(9, 12)], GLASS_LIGHT)),
           roof=roof_cross, shade=0.8)
clinic.sign(8.5, 8.5, 17, [
    ".KKKKKKKKK.",
    "KwwwwwwwwwK",
    "KwwwwRwwwwK",
    "KwwwwRwwwwK",
    "KwwRRRRRwwK",
    "KwwwwRwwwwK",
    "KwwwwRwwwwK",
    "KwwwwwwwwwK",
    ".KKKKKKKKK.",
    "....KK.....",
    "....KK.....",
], {'K': rgb_index((70, 70, 75)), 'w': WHITE, 'R': cross_red})
clinic.pole(3.2, 3.2, 17, 28)
clinic.emit(144, 160, "clinic")

# School: cream building at the back with a clock tower, a playground in front.
school = Building()
yard_line = rgb_index((240, 240, 240))
def playground(x, y):
    # Painted lines of a ball court on the concrete yard.
    if 10 <= x <= 14.5 and 9.5 <= y <= 14.5:
        if abs(x - 10) < 0.35 or abs(x - 14.5) < 0.35 or abs(y - 9.5) < 0.35 or abs(y - 14.5) < 0.35 or abs(y - 12) < 0.3:
            return yard_line
    return None
school.ground(lambda x, y: playground(x, y))
school.pole(4.5, 13.5, 0, 22)
school.box(2, 2, 14, 8.5, 0, 14, (225, 195, 130), (165, 75, 55),
           sw=windows(3, 2.0, [(4, 10)], GLASS),
           se=combine(lambda u, v: rgb_index((110, 60, 30)) if 5.5 <= u <= 7 and v < 7 else None,
                      windows(3, 2.0, [(4, 10)], GLASS)),
           roof=lambda x, y: rgb_index((185, 90, 65)) if (int(x) + int(y)) % 3 == 0 else None)
school.box(6.5, 4, 8.5, 6, 14, 22, (225, 195, 130), (165, 75, 55),
           sw=lambda u, v: rgb_index((250, 250, 250)) if 0.5 <= u <= 1.5 and 3 <= v < 6 else None)
school.tree(13, 3.5, size=3.5, height=7)
school.emit(214, 160, "school")

# Park: lawn with a gravel path, a pond, trees and a bench. The tile's own
# ground is grass; this sprite adds the rest.
park = Building()
gravel, gravel_edge = rgb_index((205, 185, 140)), rgb_index((160, 140, 100))
water, water_edge = rgb_index((60, 110, 190)), rgb_index((140, 180, 220))
def park_ground(x, y):
    d = math.hypot((x - 11.5) / 1.0, (y - 4.5) / 1.0)
    if d < 2.6:
        return water if d < 2.0 else water_edge
    if 7 <= y <= 9:
        return gravel_edge if y < 7.4 or y > 8.6 else gravel
    if 7 <= x <= 9 and y > 9:
        return gravel_edge if x < 7.4 or x > 8.6 else gravel
    return None
park.ground(park_ground)
bench, bench_dark = rgb_index((140, 90, 40)), rgb_index((90, 55, 25))
for (bx, by) in ((5.0, 6.6),):
    sx, sy = round(32 + 2 * (by - bx)), round(bx + by)
    for dx in range(-3, 3):
        park.set(sx + dx, sy - 2 + (dx + 3) // 2 - 1, bench)
        park.set(sx + dx, sy - 1 + (dx + 3) // 2 - 1, bench_dark)
for (tx, ty, size) in ((3.5, 3.0, 4.5), (4.0, 12.5, 5.0), (12.5, 12.5, 4.5), (11.0, 9.8, 3.5)):
    park.tree(tx, ty, size=size, height=size + 5)
park.emit(284, 160, "park")

# Icons for the city services toolbar.
icon(354, 160, [
    "...KKKKKKKK...",
    "..KbbbbbbbbK..",
    ".KbbbbYYbbbbK.",
    ".KbbbbYYbbbbK.",
    ".KbYYYYYYYYbK.",
    ".KbbYYYYYYbbK.",
    ".KbbbYYYYbbbK.",
    ".KbbYYbbYYbbK.",
    ".KbbYbbbbYbbK.",
    "..KbbbbbbbbK..",
    "..KbbbbbbbbK..",
    "...KbbbbbbK...",
    "....KbbbbK....",
    ".....KbbK.....",
    "......KK......",
], {'K': BLACK, 'b': rgb_index((30, 60, 170)), 'Y': YELLOW}, "icon police")

icon(372, 160, [
    "......R.......",
    "......RR......",
    ".....RRR......",
    "....RRORR.....",
    "....ROORR..R..",
    "...RROOORR.RR.",
    "..RROOOOORRRR.",
    "..RROOYOOORRR.",
    ".RROOYYYOOORR.",
    ".RROYYYYYOORR.",
    ".RROYYYYYYORR.",
    ".RROOYYYYOORR.",
    "..RROOYYOORR..",
    "...RRROORRR...",
    ".....RRRR.....",
], {'R': rgb_index((200, 30, 20)), 'O': rgb_index((250, 140, 20)), 'Y': YELLOW}, "icon fire")

icon(390, 160, [
    ".KKKKKKKKKKKK.",
    "KwwwwwwwwwwwwK",
    "KwwwwwRRwwwwwK",
    "KwwwwwRRwwwwwK",
    "KwwwwwRRwwwwwK",
    "KwwRRRRRRRRwwK",
    "KwwRRRRRRRRwwK",
    "KwwwwwRRwwwwwK",
    "KwwwwwRRwwwwwK",
    "KwwwwwRRwwwwwK",
    "KwwwwwwwwwwwwK",
    ".KKKKKKKKKKKK.",
], {'K': BLACK, 'w': WHITE, 'R': rgb_index((215, 30, 30))}, "icon clinic")

icon(408, 160, [
    "...........Y....",
    "......KK...Y....",
    "....KKKKKK.Y....",
    "..KKKKKKKKKKY...",
    "KKKKKKKKKKKKKK..",
    "..KKKKKKKKKKYY..",
    "....KKKKKK..YY..",
    "....KKKKKK......",
    "....KKKKKK......",
    "...KKKKKKKK.....",
    "................",
    ".BBBBBBBBBBBBBB.",
    ".BwwwwwwwwwwwwB.",
    ".BwwwwwwwwwwwwB.",
    ".BBBBBBBBBBBBBB.",
], {'K': BLACK, 'Y': YELLOW, 'B': rgb_index((150, 60, 40)), 'w': rgb_index((240, 230, 200))}, "icon school")

icon(428, 160, [
    "....TTTTT.....",
    "..TTtttttTT...",
    ".TtttttttttT..",
    ".TttTttttTttT.",
    "TtttttttttttT.",
    "TttttTttTtttT.",
    ".TttttttttttT.",
    "..TTTtttTTTT..",
    "......BB......",
    "......BB......",
    ".ww...BB......",
    ".wwwwwwwwwww..",
    ".w.......w....",
    ".w.......w....",
], {'T': TREE_DARK, 't': TREE, 'B': BROWN_DARK, 'w': BROWN}, "icon park")

icon(448, 160, [
    "..RRR...........",
    ".RRRRR......BB..",
    ".RRWRR....BBBB..",
    ".RRRRR...BB.....",
    "..RRRRRWWBB.....",
    ".....RRWWWB.....",
    "......RWWWB.....",
    ".......BBRRR....",
    "......BB..RRR...",
    ".....BB....RRR..",
    "..WWWB......RRWW",
    ".WWWWW.......WWW",
    ".WWWWW.......WWW",
    "..WWW.........W.",
], {'R': rgb_index((220, 40, 40)), 'B': rgb_index((40, 90, 220)), 'W': WHITE}, "icon network map")

# Street life vehicles, bodies in company colours so the game can give each its own colour.
sys.path.insert(0, SRC)
import street_life_sprites as SL

GLASS, GLASS_LIT = nearest((60, 75, 100)), nearest((120, 145, 175))
CARGO, CARGO_DARK, CARGO_TOP = nearest((205, 205, 200)), nearest((150, 150, 145)), nearest((230, 230, 225))
CHASSIS = nearest((45, 45, 45))
RIDER, RIDER_DARK = nearest((70, 60, 50)), nearest((40, 35, 30))
BUS_GLASS, BUS_GLASS_LIT = nearest((35, 45, 65)), nearest((75, 95, 125))
POD, POD_LIT, POD_TOP = nearest((130, 130, 135)), nearest((175, 175, 180)), nearest((210, 210, 215))


def vehicle_colour(role, face, light):
    base = SL.face_colour(role, face, None)
    if base == 'cc':
        if face == 'top':
            return 0xCC
        return 0xCA if light >= 0.95 else (0xC9 if light >= 0.8 else 0xC7)
    if base == 'glass':
        return GLASS_LIT if light >= 0.9 else GLASS
    if base == 'cargo':
        return CARGO_TOP if face == 'top' else (CARGO if light >= 0.9 else CARGO_DARK)
    if base == 'dark':
        return CHASSIS
    if base == 'wheel':
        return BLACK
    if base == 'rider':
        return RIDER if light >= 0.9 else RIDER_DARK
    if base == 'cc2':  # second company colour (the buses use two)
        if face == 'top':
            return 0x56
        return 0x55 if light >= 0.95 else (0x54 if light >= 0.8 else 0x52)
    if base == 'bus_glass':
        return BUS_GLASS_LIT if light >= 0.9 else BUS_GLASS
    if base == 'pod':
        return POD_TOP if face == 'top' else (POD_LIT if light >= 0.9 else POD)
    return 0xCA


slot = 0
for kind in ('car', 'van', 'lorry', 'motorbike'):
    for d in range(8):
        img, ox, oy = SL.rasterise(SL.render(kind, d), vehicle_colour, None, None)
        x0, y0 = 4 + (slot % 16) * 26, 226 + (slot // 16) * 24
        assert img.width <= 25 and img.height <= 23, (kind, d, img.size)
        sheet.paste(img, (x0, y0), img.point(lambda c: 255 if c else 0, '1'))
        sprites.append((x0, y0, img.width, img.height, -ox, -oy, f"{kind} {d}"))
        slot += 1

# Street life people: 3 pixels wide, 7 tall, feet at the origin.
HAIR, HAIR_DARK = nearest((60, 40, 25)), nearest((25, 20, 15))
SKINS = [nearest((236, 190, 150)), nearest((160, 105, 65))]
TROUSERS = nearest((55, 55, 75))
PERSON = {
    'walk1': [".h.", ".s.", "CcD", ".cD", ".c.", "T.T", "T.T"],
    'walk2': [".h.", ".s.", "CcD", ".cD", ".c.", ".T.", ".T."],
    'stand': [".h.", ".s.", "CcD", "Ccd", ".c.", ".T.", ".T."],
}
for skin_index, skin in enumerate(SKINS):
    for frame in ('walk1', 'walk2', 'stand'):
        slot = len(sprites) - 73
        x0, y0 = 4 + slot * 6, 276
        put(x0, y0, PERSON[frame], {'h': HAIR if skin_index == 0 else HAIR_DARK, 's': skin, 'C': 0xCB, 'c': 0xC9, 'D': 0xC7, 'd': skin, 'T': TROUSERS})
        sprites.append((x0, y0, 3, 7, -1, -7, f"person {skin_index} {frame}"))

assert len(sprites) == 79, len(sprites)

# City buses (built-in road vehicles), 8 directions each in OpenTTD's Direction order;
# bodies in the first company colour, roofs in the second (the engines use two company colours).
for kind in SL.BUSES:
    for d in range(8):
        slot = len(sprites) - 79
        img, ox, oy = SL.rasterise(SL.render_sorted(kind, d), vehicle_colour, None, None)
        x0, y0 = 4 + (slot % 16) * 28, 290 + (slot // 16) * 23
        assert img.width <= 27 and img.height <= 22, (kind, d, img.size)
        sheet.paste(img, (x0, y0), img.point(lambda c: 255 if c else 0, '1'))
        sprites.append((x0, y0, img.width, img.height, -ox, -oy, f"{kind} {d}"))

assert len(sprites) == 191, len(sprites)

# Depot yards: two ground markings (parking, charging bays) and the buildings
# and fittings of the facilities, in the order of the YardSprite enum in depot_yard.cpp.
ASPHALT, KERB = rgb_index((78, 78, 82)), rgb_index((165, 165, 160))
LINE_WHITE = rgb_index((236, 236, 236))
EV_GREEN = rgb_index((60, 150, 70))
CONCRETE = (175, 175, 170)
DARK_OPEN = rgb_index((38, 38, 42))
STEEL = (130, 132, 138)
CC_BAND = 0xC9  # company colour, recoloured on the owner's buildings


def yard_edge(x, y):
    return x < 0.6 or y < 0.6 or x > 15.4 or y > 15.4


def park_ground(x, y):
    """Four parking lanes along X, divided by white lines."""
    if yard_edge(x, y):
        return KERB
    for yy in (4, 8, 12):
        if abs(y - yy) < 0.3 and 1.2 <= x <= 14.8:
            return LINE_WHITE
    if abs(x - 1.2) < 0.25 and 0.6 <= y <= 15.4:
        return LINE_WHITE
    return ASPHALT


def bay_ground(x, y):
    """Two charging bays along X, painted green at the chargers' end."""
    if yard_edge(x, y):
        return KERB
    if abs(y - 8) < 0.3 and 1.2 <= x <= 12.8:
        return LINE_WHITE
    if 10.5 <= x <= 12.6 and (1.4 <= y <= 6.6 or 9.4 <= y <= 14.6):
        return EV_GREEN
    return ASPHALT


yard = []
g = Building()
g.ground(park_ground)
yard.append((g, "yard: park ground"))

g = Building()
g.ground(bay_ground)
yard.append((g, "yard: charging bay ground"))

# Solar roof over a bus park: four posts and a canopy of panels, its edge in company colour.
b = Building()
PANEL, PANEL_LINE = rgb_index((35, 55, 105)), rgb_index((120, 140, 185))
for (px_, py_) in ((1.2, 1.2), (1.2, 14.8), (14.8, 1.2), (14.8, 14.8)):
    b.box(px_ - 0.3, py_ - 0.3, px_ + 0.3, py_ + 0.3, 0, 12, STEEL, STEEL)
b.box(0.4, 0.4, 15.6, 15.6, 12, 13.5, (200, 200, 205), (200, 200, 205),
      sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND,
      roof=lambda x, y: PANEL_LINE if (x % 3.8 < 0.35 or y % 2.6 < 0.3) else PANEL)
yard.append((b, "yard: solar roof"))

# Multi-storey bus park: concrete decks with open sides, a stair tower in company colour.
b = Building()
def decks(u, v):
    if int(v) % 8 < 2:
        return None  # slab edge, wall colour
    if u % 3.0 < 0.45:
        return rgb_index((150, 150, 145))  # column
    return DARK_OPEN
b.box(1, 1, 15, 15, 0, 26, CONCRETE, (150, 150, 150), sw=decks, se=decks,
      roof=lambda x, y: LINE_WHITE if (abs(y - 5) < 0.25 or abs(y - 10) < 0.25) and 2 < x < 14 else rgb_index((105, 105, 108)))
b.box(11.5, 11.5, 15, 15, 26, 31, (190, 60, 50), (150, 150, 150), sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND)
yard.append((b, "yard: multi-storey park"))

# Chargers of a charging bay: two white pedestals with a company colour band and a screen.
b = Building()
def pedestal(u, v):
    if 6 <= v < 7.5:
        return CC_BAND
    if 3.5 <= v < 5.5 and 0.2 <= u <= 1.0:
        return rgb_index((30, 40, 60))
    return None
for py_ in (3.0, 11.0):
    b.box(13.8, py_, 14.9, py_ + 1.4, 0, 9, (235, 235, 238), (200, 200, 205), sw=pedestal, se=pedestal)
yard.append((b, "yard: chargers"))

# Fast chargers: a gantry over the bays with two pantographs, a cabinet at the side.
b = Building()
for py_ in (0.8, 15.2):
    b.box(14.6, py_ - 0.35, 15.3, py_ + 0.35, 0, 15, STEEL, STEEL)
b.box(13.2, 0.4, 15.4, 15.6, 13, 15, (210, 210, 210), (210, 210, 210), sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND)
for py_ in (4.0, 12.0):
    b.box(12.6, py_ - 0.9, 13.4, py_ + 0.9, 10.5, 13, (40, 40, 44), (40, 40, 44))
b.box(14.4, 7.0, 15.6, 9.0, 0, 8, (225, 225, 228), (190, 190, 195), sw=pedestal)
yard.append((b, "yard: fast chargers"))

# Substation: gravel inside a fence, two transformers with fins, a busbar on insulators, a control house.
b = Building()
GRAVEL = rgb_index((150, 140, 125))
b.ground(lambda x, y: KERB if yard_edge(x, y) else GRAVEL)
fins = lambda u, v: rgb_index((60, 62, 66)) if u % 1.0 < 0.35 and v > 1 else None
for py_ in (2.5, 8.5):
    b.box(3.5, py_, 7.5, py_ + 4, 0, 9, (95, 98, 104), (120, 122, 128), sw=fins, se=fins)
for py_ in (3.0, 7.0, 11.0, 14.0):
    b.pole(9.2, py_, 0, 12, flag=False)
b.box(8.9, 2.6, 9.5, 14.4, 12, 13, STEEL, STEEL)
b.box(10.5, 10.5, 14.8, 14.8, 0, 10, CONCRETE, (150, 150, 150),
      sw=lambda u, v: rgb_index((60, 70, 90)) if 1.5 <= u <= 2.5 and v < 6 else (CC_BAND if v >= 8 else None))
FENCE = rgb_index((150, 155, 160))
for (x0, y0, x1, y1) in ((0.3, 0.3, 15.7, 0.6), (0.3, 0.3, 0.6, 15.7)):
    b.box(x0, y0, x1, y1, 0, 5, STEEL, STEEL, sw=lambda u, v: FENCE if u % 0.8 < 0.3 else None)
yard.append((b, "yard: substation"))

# Repair bay: a workshop shed with a roller door and a company colour band.
b = Building()
b.ground(lambda x, y: KERB if yard_edge(x, y) else rgb_index((120, 120, 118)))
ROLLER = rgb_index((185, 185, 180))
def shed_se(u, v):
    if 1.5 <= u <= 8.0 and v < 12:
        return ROLLER if int(v) % 2 == 0 else rgb_index((150, 150, 146))
    if v >= 14:
        return CC_BAND
    return None
def shed_sw(u, v):
    if v >= 14:
        return CC_BAND
    if 6 <= v < 10 and u % 3 < 1.8:
        return GLASS
    return None
b.box(1.5, 1.5, 14.5, 14.5, 0, 17, (140, 155, 170), (95, 100, 108), sw=shed_sw, se=shed_se,
      roof=lambda x, y: rgb_index((120, 124, 130)) if int(x * 2) % 2 == 0 else None)
yard.append((b, "yard: repair bay"))

# Wash and fuel lane: a canopy over a lane with two pumps and a bus wash portal.
b = Building()
def lane_ground(x, y):
    if yard_edge(x, y):
        return KERB
    if (abs(y - 4.5) < 0.25 or abs(y - 11.5) < 0.25) and 1 <= x <= 15:
        return rgb_index((230, 200, 40))
    return ASPHALT
b.ground(lane_ground)
for (px_, py_) in ((4.5, 2.0), (4.5, 14.0), (12.0, 2.0), (12.0, 14.0)):
    b.box(px_ - 0.3, py_ - 0.3, px_ + 0.3, py_ + 0.3, 0, 12, STEEL, STEEL)
b.box(4.0, 1.5, 12.5, 14.5, 12, 14, (235, 235, 238), (225, 225, 228), sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND)
for py_ in (2.3, 13.0):
    b.box(8.0, py_, 9.0, py_ + 0.9, 0, 6, (200, 40, 35), (150, 30, 25))
b.box(1.2, 5.0, 2.6, 11.0, 0, 11, (60, 62, 70), (60, 62, 70))
b.box(1.5, 5.6, 2.3, 10.4, 1, 9, (50, 110, 200), (40, 90, 170))
yard.append((b, "yard: wash and fuel lane"))

# Staff building: two storeys with windows, a company colour band, a bench and a tree.
b = Building()
b.ground(lambda x, y: KERB if yard_edge(x, y) else rgb_index((120, 150, 90)))
def staff_face(u, v):
    if v >= 13:
        return CC_BAND
    if (2.5 <= v < 5.5 or 8 <= v < 11) and u % 2.4 < 1.4:
        return GLASS
    return None
def staff_se(u, v):
    if 4.2 <= u <= 5.8 and v < 6:
        return rgb_index((90, 60, 35))
    return staff_face(u, v)
b.box(2.5, 2.5, 12.5, 12.5, 0, 15, (230, 220, 195), (190, 180, 155), sw=staff_face, se=staff_se)
b.box(5, 5, 7.5, 7.5, 15, 17.5, (170, 170, 170), (140, 140, 140))
b.tree(14.0, 4.0, size=3.0, height=6)
yard.append((b, "yard: staff building"))

for i, (bld, name) in enumerate(yard):
    x0, y0 = (4 + (i % 7) * 72, 462) if i < 7 else (4 + (i - 7) * 72, 530)
    w = bld.emit(x0, y0, name)
    assert w <= 70, (name, w)

assert len(sprites) == 201, len(sprites)

# Charger over a bus stop: a gantry across the road with a pantograph over each lane,
# for a road along X and along Y; then the road toolbar icon.
for axis, x0 in (('x', 220), ('y', 292)):
    b = Building()
    def at(u, w):
        """u along the road, w across it."""
        return (u, w) if axis == 'x' else (w, u)
    for w in (0.7, 15.3):
        cx, cy = at(8.0, w)
        b.box(cx - 0.35, cy - 0.35, cx + 0.35, cy + 0.35, 0, 15, STEEL, STEEL)
    ax0, ay0 = at(7.3, 0.4)
    ax1, ay1 = at(8.7, 15.6)
    b.box(min(ax0, ax1), min(ay0, ay1), max(ax0, ax1), max(ay0, ay1), 13, 15, (210, 210, 210), (210, 210, 210),
          sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND)
    for w in (5.0, 11.0):
        px0, py0 = at(7.5, w - 0.9)
        px1, py1 = at(8.5, w + 0.9)
        b.box(min(px0, px1), min(py0, py1), max(px0, px1), max(py0, py1), 10.5, 13, (40, 40, 44), (40, 40, 44))
    b.emit(x0, 530, f"stop charger {axis}")

icon(366, 530, [
    "........KKKK....",
    ".......KYYYK....",
    "......KYYYK.....",
    ".....KYYYK......",
    "....KYYYYKKKK...",
    "...KYYYYYYYYK...",
    "...KKKKYYYYK....",
    "......KYYYK.....",
    ".....KYYYK......",
    "....KYYK........",
    "...KYYK.........",
    "...KYK..........",
    "...KK...........",
], {'K': BLACK, 'Y': YELLOW}, "icon stop charger")

assert len(sprites) == 204, len(sprites)

# Bus priority: ground markings for roads along X and along Y (the carriageway is
# about 3.6 to 12.4 across, lanes about 4 wide), then toolbar icons and the "B" lamps.
BUS_RED, BUS_RED_DARK = rgb_index((185, 45, 40)), rgb_index((150, 40, 35))
GIVE_WAY_YELLOW = rgb_index((240, 205, 30))


def across_along(axis, x, y):
    """The position across the road and along it."""
    return (y, x) if axis == 'x' else (x, y)


def lane_fn(axis, peak):
    def fn(x, y):
        w, u = across_along(axis, x, y)
        if peak and u % 2.2 >= 1.5:
            return None
        if 3.7 <= w <= 5.6 or 10.4 <= w <= 12.3:
            return BUS_RED
        if 5.6 < w <= 5.95 or 10.05 <= w < 10.4:
            return LINE_WHITE
        return None
    return fn


def bus_road_fn(axis):
    def fn(x, y):
        w, u = across_along(axis, x, y)
        if not 3.7 <= w <= 12.3:
            return None
        if abs(w - 8) < 0.3 and u % 3.2 < 1.6:
            return LINE_WHITE
        return BUS_RED_DARK
    return fn


def give_way_fn(axis):
    def fn(x, y):
        w, u = across_along(axis, x, y)
        tri = abs((u * 0.9) % 2 - 1)  # 0..1 zigzag
        for base, sign in ((4.1, 1), (11.9, -1)):
            if abs(w - (base + sign * 0.9 * tri)) < 0.28:
                return GIVE_WAY_YELLOW
        return None
    return fn


bus_ground = []
for axis in ('x', 'y'):
    bus_ground.append((lane_fn(axis, False), f"bus lane {axis}"))
for axis in ('x', 'y'):
    bus_ground.append((lane_fn(axis, True), f"peak bus lane {axis}"))
for axis in ('x', 'y'):
    bus_ground.append((bus_road_fn(axis), f"bus-only road {axis}"))
for axis in ('x', 'y'):
    bus_ground.append((give_way_fn(axis), f"give-way markings {axis}"))
for i, (fn, name) in enumerate(bus_ground):
    b = Building()
    b.ground(fn)
    x0, y0 = (4 + (i % 7) * 72, 600) if i < 7 else (4 + (i - 7) * 72, 640)
    b.emit(x0, y0, name)

R, W_, K_, Y_, G_ = BUS_RED, WHITE, BLACK, YELLOW, rgb_index((150, 150, 150))
icon(80, 640, [
    "..KKKKKKKKKKK...",
    ".KYYYYYYYYYYYK..",
    ".KYGGYGGYGGYYK..",
    ".KYGGYGGYGGYYK..",
    ".KYYYYYYYYYYYK..",
    ".KKKKKKKKKKKKK..",
    "..KK.......KK...",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "WWWWWWWWWWWWWWWW",
], {'K': K_, 'Y': Y_, 'G': GLASS, 'R': R, 'W': W_}, "icon bus priority")
icon(100, 640, [
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
], {'R': R, 'W': W_}, "icon bus lane")
icon(120, 640, [
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W............W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
    "W............W..",
    "W...RRRRRR...W..",
    "W...RRRRRR...W..",
], {'R': R, 'W': W_}, "icon peak bus lane")
icon(140, 640, [
    "RRRRRRRRRRRRR",
    "RRWWWWWWWWWRR",
    ".RRWWWWWWWRR.",
    ".RRWWWWWWWRR.",
    "..RRWWWWWRR..",
    "..RRWWWWWRR..",
    "...RRWWWRR...",
    "...RRWWWRR...",
    "....RRWRR....",
    ".....RRR.....",
    "......R......",
], {'R': rgb_index((210, 30, 30)), 'W': W_}, "icon give way")
icon(156, 640, [
    ".KKKKKKKK.",
    "KKWWWWWKKK",
    "KKWKKKKWKK",
    "KKWKKKKWKK",
    "KKWWWWWKKK",
    "KKWKKKKWKK",
    "KKWKKKKWKK",
    "KKWWWWWKKK",
    ".KKKKKKKK.",
    "....KK....",
    "....KK....",
], {'K': K_, 'W': W_}, "icon bus signal")
for name, letter in (("bus lamp lit", W_), ("bus lamp dark", G_)):
    x0 = 170 if name.endswith("lit") else 178
    put(x0, 640, [
        "KKKKK",
        "KLLKK",
        "KLKLK",
        "KLLKK",
        "KLKLK",
        "KLLKK",
        "KKKKK",
    ], {'K': K_, 'L': letter})
    sprites.append((x0, 640, 5, 7, -2, -23, name))

assert len(sprites) == 219, len(sprites)

# Busways and BRT: the kerbed busway in the middle of a road (along X and along Y),
# the deck under a stop raised onto a viaduct (X, Y), the two sides of a BRT station
# (road along X: far and near side; along Y: far and near side), then toolbar icons.
KERB_LIGHT, KERB_SHADE = rgb_index((200, 200, 196)), rgb_index((120, 120, 118))


def busway_fn(axis):
    def fn(x, y):
        w, u = across_along(axis, x, y)
        if 4.5 <= w < 4.85 or 11.15 < w <= 11.5:
            return KERB_LIGHT
        if 4.85 <= w < 5.05 or 10.95 < w <= 11.15:
            return KERB_SHADE
        if 5.05 <= w <= 10.95:
            if abs(w - 8) < 0.25 and u % 3.2 < 1.6:
                return LINE_WHITE
            return BUS_RED_DARK
        return None
    return fn


for i, axis in enumerate(('x', 'y')):
    b = Building()
    b.ground(busway_fn(axis))
    b.emit(4 + i * 72, 684, f"kerbed busway {axis}")

DECK_TOP = (196, 196, 190)
for i, axis in enumerate(('x', 'y')):
    b = Building()

    def at(u, w):
        """u along the road, w across it."""
        return (u, w) if axis == 'x' else (w, u)
    # Columns under the edges of the deck; the ones under the near edge show.
    for pu, pw in ((4.0, 1.4), (12.0, 1.4), (4.0, 14.6), (12.0, 14.6)):
        x0_, y0_ = at(pu - 0.9, pw - 0.9)
        x1_, y1_ = at(pu + 0.9, pw + 0.9)
        b.box(min(x0_, x1_), min(y0_, y1_), max(x0_, x1_), max(y0_, y1_), 0, 5, CONCRETE, CONCRETE)
    b.box(0, 0, 16, 16, 5, 8, CONCRETE, DECK_TOP)
    b.emit(148 + i * 72, 684, f"stop deck {axis}")

PLATFORM, PLATFORM_TOP = (170, 170, 165), (205, 205, 200)
ROOF_WHITE = (232, 232, 230)
GATE_GREY = (70, 72, 78)
POST_GREY = (110, 112, 118)


def brt_side(axis, near):
    """One side of a BRT station: a platform with a yellow edge, a glass wall at
    the back (far side only, so buses stay in view), a roof with a company colour
    edge and a row of fare gates at one end."""
    b = Building()

    def box(u0, u1, w0, w1, z0, z1, wall, top, **kw):
        if axis == 'x':
            b.box(u0, w0, u1, w1, z0, z1, wall, top, **kw)
        else:
            b.box(w0, u0, w1, u1, z0, z1, wall, top, **kw)

    yellow = rgb_index((240, 205, 30))
    if not near:
        # Platform on the far pavement; its face towards the road is the visible one.
        box(0, 16, 0.3, 3.4, 0, 2, PLATFORM, PLATFORM_TOP,
            se=(lambda u, v: yellow if v >= 1.3 else None) if axis == 'x' else None,
            sw=(lambda u, v: yellow if v >= 1.3 else None) if axis == 'y' else None)
        # Glass wall at the back with frames.
        glass = lambda u, v: rgb_index((60, 70, 85)) if (u % 3.2) < 0.35 or v < 0.4 else GLASS_LIGHT
        box(0.4, 15.6, 0.3, 0.7, 2, 10, (150, 175, 200), (150, 175, 200),
            se=glass if axis == 'x' else None, sw=glass if axis == 'y' else None)
        # Fare gates at the end of the platform.
        for g in (12.4, 13.5, 14.6):
            box(g, g + 0.5, 1.0, 2.7, 2, 4.5, GATE_GREY, (230, 200, 40))
        # Roof.
        box(0, 16, 0.0, 3.9, 10, 11.5, ROOF_WHITE, ROOF_WHITE,
            se=(lambda u, v: CC_BAND) if axis == 'x' else None, sw=(lambda u, v: CC_BAND) if axis == 'y' else None)
    else:
        # Platform on the near pavement, its yellow edge on top along the road.
        def edge(x, y):
            w = y if axis == 'x' else x
            return yellow if w < 13.0 else None
        box(0, 16, 12.6, 15.7, 0, 2, PLATFORM, PLATFORM_TOP, roof=edge)
        # Fare gates at the other end.
        for g in (1.0, 2.1, 3.2):
            box(g, g + 0.5, 13.4, 15.0, 2, 4.5, GATE_GREY, (230, 200, 40))
        # Posts and roof.
        for pu in (0.6, 8.0, 15.4):
            box(pu - 0.25, pu + 0.25, 15.0, 15.5, 2, 10, POST_GREY, POST_GREY)
        box(0, 16, 12.1, 16.0, 10, 11.5, ROOF_WHITE, ROOF_WHITE,
            se=(lambda u, v: CC_BAND) if axis == 'x' else None, sw=(lambda u, v: CC_BAND) if axis == 'y' else None)
    return b


for i, (axis, near) in enumerate((('x', False), ('x', True), ('y', False), ('y', True))):
    b = brt_side(axis, near)
    b.emit(4 + i * 72, 740, f"brt station {axis} {'near' if near else 'far'}")

K2 = rgb_index((200, 200, 196))
icon(292, 740, [
    "G..KRRRRRRRRK..G",
    "G..KRRRWRRRRK..G",
    "G..KRRRRRRRRK..G",
    "G..KRRRWRRRRK..G",
    "G..KRRRRRRRRK..G",
    "G..KRRRWRRRRK..G",
    "G..KRRRRRRRRK..G",
    "G..KRRRWRRRRK..G",
    "G..KRRRRRRRRK..G",
    "G..KRRRWRRRRK..G",
], {'K': K2, 'R': BUS_RED_DARK, 'W': W_, 'G': G_}, "icon busway")
icon(312, 740, [
    "WWWWWWWWWWWWWWWW",
    "CCCCCCCCCCCCCCCC",
    ".K....K.....K...",
    ".K.LL.K.LL..K...",
    ".K.LL.K.LL..K...",
    ".K....K.....K...",
    ".K.KY.KY.KY.K...",
    ".K.K..K..K..K...",
    "YYYYYYYYYYYYYYYY",
    "GGGGGGGGGGGGGGGG",
], {'W': W_, 'C': rgb_index((40, 90, 200)), 'K': K_, 'L': GLASS_LIGHT, 'Y': Y_, 'G': G_}, "icon brt station")

assert len(sprites) == 229, len(sprites)

# Road pricing: expressway markings (road along X, along Y), toll plazas (roof, far booth,
# near booth; road along X, then along Y), free-flow gantries (X, Y), then toolbar icons.
BARRIER, BARRIER_SHADE = rgb_index((205, 205, 200)), rgb_index((110, 110, 108))
EDGE_YELLOW = rgb_index((235, 200, 40))


def expressway_fn(axis):
    def fn(x, y):
        w, u = across_along(axis, x, y)
        if 7.55 <= w < 8.3:
            return BARRIER
        if 8.3 <= w < 8.6:
            return BARRIER_SHADE
        if abs(w - 5.6) < 0.2 or abs(w - 10.5) < 0.2:
            return LINE_WHITE if u % 3.2 < 1.6 else None
        if 3.85 <= w < 4.15 or 11.85 < w <= 12.15:
            return LINE_WHITE
        if 7.25 <= w < 7.55 or 8.6 <= w < 8.9:
            return EDGE_YELLOW
        return None
    return fn


for i, axis in enumerate(('x', 'y')):
    b = Building()
    b.ground(expressway_fn(axis))
    b.emit(4 + i * 72, 780, f"expressway {axis}")

BOOTH_WALL, BOOTH_ROOF = (228, 226, 216), (150, 150, 155)


def toll_part(axis, part):
    b = Building()

    def box(u0, u1, w0, w1, z0, z1, wall, top, **kw):
        if axis == 'x':
            b.box(u0, w0, u1, w1, z0, z1, wall, top, **kw)
        else:
            b.box(w0, u0, w1, u1, z0, z1, wall, top, **kw)

    band = lambda u, v: CC_BAND if v >= 4.6 else (GLASS_LIGHT if 2.0 <= v < 4.0 and (u % 2.0) < 1.4 else None)
    end_band = lambda u, v: CC_BAND if v >= 4.6 else (GLASS_LIGHT if 2.0 <= v < 4.0 and 0.5 < u < 1.9 else None)
    if part == 'roof':
        # A narrow roof across the road in company colour, white on top.
        box(6.0, 10.0, 0.0, 16.0, 12, 13.2, (235, 235, 232), (230, 230, 226),
            se=lambda u, v: CC_BAND, sw=lambda u, v: CC_BAND)
    elif part == 'far':
        box(3.0, 13.0, 0.8, 3.2, 0, 5.5, BOOTH_WALL, BOOTH_ROOF,
            se=band if axis == 'x' else end_band, sw=end_band if axis == 'x' else band)
        for pu in (6.4, 9.6):
            box(pu - 0.3, pu + 0.3, 1.7, 2.3, 5.5, 12, STEEL, STEEL)
    else:
        box(3.0, 13.0, 12.8, 15.2, 0, 5.5, BOOTH_WALL, BOOTH_ROOF,
            se=band if axis == 'x' else end_band, sw=end_band if axis == 'x' else band)
        for pu in (6.4, 9.6):
            box(pu - 0.3, pu + 0.3, 13.7, 14.3, 5.5, 12, STEEL, STEEL)
    return b


toll_sprites = []
for axis in ('x', 'y'):
    for part in ('roof', 'far', 'near'):
        toll_sprites.append((toll_part(axis, part), f"toll plaza {part} {axis}"))
for axis in ('x', 'y'):
    b = Building()

    def at(u, w):
        return (u, w) if axis == 'x' else (w, u)
    for w in (0.7, 15.3):
        cx, cy = at(8.0, w)
        b.box(cx - 0.35, cy - 0.35, cx + 0.35, cy + 0.35, 0, 15, STEEL, STEEL)
    ax0, ay0 = at(7.4, 0.4)
    ax1, ay1 = at(8.6, 15.6)
    b.box(min(ax0, ax1), min(ay0, ay1), max(ax0, ax1), max(ay0, ay1), 13, 15, (210, 210, 210), (210, 210, 210),
          sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND)
    for w in (4.5, 6.8, 9.2, 11.5):
        px0, py0 = at(7.6, w - 0.5)
        px1, py1 = at(8.4, w + 0.5)
        b.box(min(px0, px1), min(py0, py1), max(px0, px1), max(py0, py1), 11.8, 13, (40, 40, 44), (40, 40, 44))
    toll_sprites.append((b, f"free-flow gantry {axis}"))
for i, (bld, name) in enumerate(toll_sprites):
    x0, y0 = (4 + (i % 7) * 72, 810) if i < 7 else (4 + (i - 7) * 72, 870)
    w = bld.emit(x0, y0, name)
    assert w <= 70, (name, w)

R2 = rgb_index((210, 30, 30))
icon(150, 870, [
    "..KKKKK.........",
    "..KWWWK.........",
    "..KWGWK.........",
    "..KWWWKRRWWRRWW.",
    "..KWWWKRRWWRRWW.",
    "..KWWWK.........",
    "..KWWWK.........",
    "..KKKKK.........",
    "GGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGG",
], {'K': K_, 'W': W_, 'G': G_, 'R': R2}, "icon tolls")
icon(170, 870, [
    "CCCCCCCCCCCCCCCC",
    "CCCCCCCCCCCCCCCC",
    ".K.....KK.....K.",
    "KWK....KK....KWK",
    "KGK....KK....KGK",
    "KWK....KK....KWK",
    "KWK..........KWK",
    "KKK..........KKK",
    "GGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGG",
], {'K': K_, 'W': W_, 'G': G_, 'C': rgb_index((40, 90, 200))}, "icon toll plaza")
icon(190, 870, [
    "KKKKKKKKKKKKKKKK",
    "KWWWWWWWWWWWWWWK",
    "KKDDKKKDDKKKDDKK",
    "K..............K",
    "K..............K",
    "K..............K",
    "K..............K",
    "K..............K",
    "GGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGG",
], {'K': K_, 'W': W_, 'G': G_, 'D': rgb_index((40, 40, 44))}, "icon free flow")

assert len(sprites) == 242, len(sprites)

# Bus interchanges: a roof over each bus stop on posts at the three near corners (the far
# one would stand in front of the buses), white with a company colour edge; the roof of
# an integrated transport hub has glass panels and air-conditioning units on it.
AC_GREY = (150, 152, 158)
for i, hub in enumerate((False, True)):
    b = Building()
    for (px_, py_) in ((14.6, 1.4), (1.4, 14.6), (14.6, 14.6)):
        b.box(px_ - 0.35, py_ - 0.35, px_ + 0.35, py_ + 0.35, 0, 14, STEEL, STEEL)
    if hub:
        glass = lambda x, y: GLASS_LIGHT if (int(x * 2) % 4 == 1 and 2 < y < 14) else None
        b.box(0.3, 0.3, 15.7, 15.7, 14, 15.4, (225, 228, 232), (200, 215, 230), sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND, roof=glass)
        for (ax, ay) in ((4.0, 4.5), (9.0, 4.5), (4.0, 10.5), (9.0, 10.5)):
            b.box(ax, ay, ax + 2.4, ay + 2.0, 15.4, 17.2, AC_GREY, (185, 187, 192))
    else:
        ridge = lambda x, y: rgb_index((205, 205, 200)) if int(y) % 4 == 0 else None
        b.box(0.3, 0.3, 15.7, 15.7, 14, 15.4, (238, 238, 235), (232, 232, 228), sw=lambda u, v: CC_BAND, se=lambda u, v: CC_BAND, roof=ridge)
    b.emit(220 + i * 72, 870, "interchange roof hub" if hub else "interchange roof")

assert len(sprites) == 244, len(sprites)

# Platform screen doors on the edge of the railway platforms, half height like those on
# elevated MRT stations: glass panels between steel posts, and doors with a green light
# over them; on the far and on the near platform (railway along X, then along Y). They
# stand on the platform, 2 px above the ground.
PSD_FRAME = (78, 84, 92)
PSD_GLASS = (172, 200, 218)
PSD_DOOR = (204, 224, 234)
PSD_LIGHT = (70, 180, 90)
PSD_HEIGHT = 5


def psd_face(u, v):
    if v >= PSD_HEIGHT - 1.0:
        return rgb_index(PSD_FRAME)  # top rail
    m = u % 4.0
    if m < 0.3 or m > 3.7:
        return rgb_index(PSD_FRAME)  # posts
    if 1.3 <= m <= 2.9:
        if v >= PSD_HEIGHT - 1.9:
            return rgb_index(PSD_LIGHT)  # the light over the door
        if abs(m - 2.1) < 0.12:
            return rgb_index(PSD_FRAME)  # between the two leaves
        return rgb_index(PSD_DOOR)
    return rgb_index(PSD_GLASS)


psd_x = 0
for axis in ('x', 'y'):
    for near in (False, True):
        b = Building()
        w0 = 11.0 if near else 4.6
        if axis == 'x':
            b.box(0, w0, 16, w0 + 0.4, 2, 2 + PSD_HEIGHT, PSD_FRAME, PSD_FRAME, se=psd_face)
        else:
            b.box(w0, 0, w0 + 0.4, 16, 2, 2 + PSD_HEIGHT, PSD_FRAME, PSD_FRAME, sw=psd_face)
        psd_x += b.emit(psd_x, 934, f"screen doors {axis} {'near' if near else 'far'}") + 4

assert len(sprites) == 248, len(sprites)

# Icons of the map's air pollution view (a factory under a cloud of smoke) and land value view
# (a house with an arrow going up).
icon(300, 934, [
    "..........SSS...",
    "........SSSSSS..",
    ".......SSSSSSSS.",
    "........SSSSSS..",
    "..........KK....",
    "..........KK....",
    "..........KK....",
    "..RR......KK....",
    "..RR...RR.KK....",
    ".RRRR.RRRRKK....",
    ".RRRRRRRRRRRRR..",
    ".RYYRRYYRRYYRR..",
    ".RRRRRRRRRRRRR..",
    "................",
], {'S': rgb_index((150, 150, 150)), 'K': rgb_index((60, 60, 60)), 'R': rgb_index((170, 70, 50)), 'Y': rgb_index((240, 210, 80))}, "icon pollution map")
icon(320, 934, [
    "............G...",
    "...........GGG..",
    "....RR....GGGGG.",
    "...RRRR.....G...",
    "..RRRRRR....G...",
    ".RRRRRRRR...G...",
    "..WWWWWW....G...",
    "..WBBWWW........",
    "..WBBWDW........",
    "..WWWWDW........",
    "..WWWWWW........",
    "................",
    "................",
    "................",
], {'R': rgb_index((190, 50, 40)), 'W': rgb_index((235, 225, 200)), 'B': rgb_index((60, 110, 200)), 'D': rgb_index((120, 70, 30)), 'G': rgb_index((40, 170, 60))}, "icon land value map")


# ---------------------------------------------------------------------------
# Malaysian and Singaporean town buildings (city_houses.cpp), from sprite 250 on:
# three stages of construction, then the finished buildings in the order of
# CityHouseSprite in city_houses.cpp. Facades painted in the company colour
# range get a colour per building from the game (STRUCT recolour palettes).
# The game marks up to 200 pixels above a tile for redrawing, so no building is
# taller than that.

CC = list(range(0xC6, 0xCE))  # recoloured range, dark to light


def cc(level):
    return CC[max(0, min(7, int(level)))]


def grid(storey, glass, frame=None, period=1.6, frac=(0.2, 0.8), base=0.0, top=None, low=1.3, sill=0.6):
    """Windows on a face: a floor line every storey, a window in each bay."""
    def fn(u, v):
        if v < base or (top is not None and v >= top):
            return None
        lv = (v - base) % storey
        if frame is not None and lv < 0.9:
            return frame
        f = (u % period) / period
        if frac[0] <= f < frac[1] and low <= lv < storey - sill:
            return glass
        return None
    return fn


def bands(storey, colour, base=0.0, top=None, lo=0.0, hi=1.0):
    """A band at the same height in every storey (corridor parapets, balconies)."""
    def fn(u, v):
        if v < base or (top is not None and v >= top):
            return None
        lv = (v - base) % storey
        return colour if lo <= lv < hi else None
    return fn


def strip(u0, u1, colour, v0=0, v1=999):
    def fn(u, v):
        return colour if u0 <= u < u1 and v0 <= v < v1 else None
    return fn


def pillars(period, width, v1, colour, gap):
    """An open ground floor: pillars every period, dark space between."""
    def fn(u, v):
        if v >= v1:
            return None
        return colour if (u % period) < width else gap
    return fn


def dome(b, cx, cy, z0, r, h, rgb):
    """A dome over the circle of radius r (tile units) around (cx, cy), h pixels high."""
    sx, sy = 32 + 2 * (cy - cx), cx + cy - z0
    for py in range(math.floor(sy - h) - 1, math.ceil(sy + r) + 1):
        for px_ in range(math.floor(sx - 2 * r) - 1, math.ceil(sx + 2 * r) + 1):
            dx, dy = (px_ + 0.5 - sx) / (2 * r), (py + 0.5 - sy)
            if dy <= 0:
                inside = dx * dx + (dy / h) ** 2 <= 1
            else:
                inside = dx * dx + (dy / r) ** 2 <= 1
            if inside:
                f = 1.05 - 0.45 * (dx + 0.3) - (0.25 if dy > 0 else 0)
                b.set(px_, py, rgb_index(rgb, max(0.45, min(1.25, f))))


def post(b, x, y, z0, z1, colour):
    sx, sy = round(32 + 2 * (y - x)), round(x + y)
    for z in range(z0, z1):
        b.set(sx, sy - z, colour)


CONCRETE = (200, 196, 186)
PAVING_DARK = rgb_index((70, 70, 75))
FRAME = rgb_index((235, 235, 235))
DARK_GLASS = rgb_index((45, 60, 80))
SKY_GLASS = rgb_index((120, 170, 200))
TERRACOTTA = (176, 82, 52)
houses_x, houses_y = 4, 990


def house(b, name, w=72, h=240):
    """Place a finished sprite on the house rows of the sheet."""
    global houses_x, houses_y
    width = max(p[0] for p in b.pix) - min(p[0] for p in b.pix) + 1
    if houses_x + width > W - 2:
        houses_x, houses_y = 4, houses_y + h
    b.emit(houses_x, houses_y, name)
    houses_x += width + 4


# Construction: a slab with piles, a concrete frame, a taller frame with a crane.
c0 = Building()
c0.box(2, 2, 14, 14, 0, 3, CONCRETE, (170, 170, 165), roof=lambda x, y: rgb_index((120, 115, 110)) if (int(x * 1.5) + int(y * 1.5)) % 4 == 0 else None)
house(c0, "construction 0")
c1 = Building()
scaffold = rgb_index((210, 170, 60))
c1.box(3, 3, 13, 13, 0, 22, (150, 150, 150), (130, 130, 130),
       sw=combine(lambda u, v: scaffold if (u % 2.0) < 0.35 or (v % 5) < 0.8 else None, lambda u, v: rgb_index((60, 60, 65)) if (v % 5) >= 1.5 else None),
       se=combine(lambda u, v: scaffold if (u % 2.0) < 0.35 or (v % 5) < 0.8 else None, lambda u, v: rgb_index((60, 60, 65)) if (v % 5) >= 1.5 else None))
house(c1, "construction 1")
c2 = Building()
c2.box(3, 3, 13, 13, 0, 44, (150, 150, 150), (130, 130, 130),
       sw=combine(lambda u, v: scaffold if (u % 2.0) < 0.35 or (v % 5) < 0.8 else None, lambda u, v: rgb_index((60, 60, 65)) if (v % 5) >= 1.5 else None),
       se=combine(lambda u, v: scaffold if (u % 2.0) < 0.35 or (v % 5) < 0.8 else None, lambda u, v: rgb_index((60, 60, 65)) if (v % 5) >= 1.5 else None))
crane = rgb_index((235, 190, 40))
post(c2, 4, 12, 44, 64, crane)
for i in range(18):
    sx, sy = round(32 + 2 * (12 - 4)) - 8 + i, round(4 + 12) - 64 + (1 if i % 5 == 0 else 0)
    c2.set(sx, sy, crane)
for z in range(56, 64):
    c2.set(round(32 + 2 * (12 - 4)) + 7, round(4 + 12) - z, rgb_index((80, 80, 80)))
house(c2, "construction 2")

# Kampung house: a timber house on stilts with a rusty zinc roof and a verandah, and a coconut palm.
k = Building()
timber, timber_dark = (150, 100, 55), (110, 70, 35)
for (x, y) in ((4.3, 4.3), (11.7, 4.3), (4.3, 10.7), (11.7, 10.7), (8, 10.7), (11.7, 7.5)):
    k.box(x - 0.35, y - 0.35, x + 0.35, y + 0.35, 0, 5, timber_dark, timber_dark)
k.box(4, 4, 12, 11, 5, 13, timber, timber_dark,
      sw=combine(strip(2.5, 4.5, rgb_index((60, 35, 20)), 1.5, 6), lambda u, v: rgb_index((120, 78, 40)) if int(v) % 2 == 0 else None),
      se=combine(strip(1.5, 3.0, rgb_index((60, 35, 20)), 1.5, 6), strip(5.0, 6.5, rgb_index((60, 35, 20)), 1.5, 6), lambda u, v: rgb_index((120, 78, 40)) if int(v) % 2 == 0 else None))
k.box(12, 5, 14.5, 10, 5, 6, timber_dark, timber)
for (i, (y0, y1)) in enumerate(((3.2, 11.8), (4.2, 10.8), (5.2, 9.8), (6.2, 8.8))):
    k.box(3.2, y0, 12.8, y1, 13 + 2 * i, 15 + 2 * i, (150, 70, 45), (165, 85, 55), shade=0.62)
k.box(13, 5.5, 14.5, 9.5, 6, 11, timber, (165, 85, 55))
palm = rgb_index((60, 150, 50))
post(k, 1.5, 13.5, 0, 16, rgb_index((120, 95, 60)))
sx, sy = round(32 + 2 * (13.5 - 1.5)), round(1.5 + 13.5) - 16
for (dx, dy) in ((-5, 2), (-4, 1), (-3, 0), (-2, 0), (-1, 0), (1, 0), (2, 0), (3, 0), (4, 1), (5, 2), (-2, 1), (2, 1), (-1, -1), (1, -1), (0, -1), (-3, 2), (3, 2)):
    k.set(sx + dx, sy + dy, palm)
house(k, "kampung house")

# Terrace houses: a row of three two-storey link houses with tiled roofs, porches and gates.
t = Building()
t_wall = lambda u, v: None
door = rgb_index((90, 55, 30))
def terrace_front(u, v):
    unit = u % 4.0
    if v < 6.5 and 1.0 <= unit < 1.9:
        return door
    if v < 6.5 and 2.4 <= unit < 3.4 and 2 <= v < 5:
        return DARK_GLASS
    if 8 <= v < 12.5 and 0.8 <= unit < 3.2:
        return DARK_GLASS if v < 11.5 else FRAME
    if unit < 0.25:
        return rgb_index((205, 200, 190))
    return cc(6) if v < 7 else cc(7)
t.box(2, 3, 14, 12, 0, 14, (230, 225, 210), TERRACOTTA, sw=lambda u, v: cc(5) if v < 7 else cc(6), se=terrace_front)
for (i, (y0, y1)) in enumerate(((2.4, 12.6), (3.9, 11.1), (5.4, 9.6))):
    t.box(1.6, y0, 14.4, y1, 14 + 2 * i, 16 + 2 * i, TERRACOTTA, (190, 95, 60), shade=0.7)
for x0 in (2.2, 6.2, 10.2):
    t.box(x0, 12, x0 + 3.4, 13.6, 6.5, 7.3, (190, 95, 60), (200, 105, 70))
for x0 in (2.0, 6.0, 10.0):
    t.box(x0, 14.6, x0 + 1.4, 15.0, 0, 3, (120, 120, 125), (150, 150, 155))
    t.box(x0 + 2.8, 14.6, x0 + 4.0, 15.0, 0, 3, (120, 120, 125), (150, 150, 155))
house(t, "terrace houses")

# Walk-up flats: five storeys with open corridors along the front and water tanks on the roof.
f = Building()
f.box(2, 3, 14, 13, 0, 30, (226, 216, 190), (150, 150, 150),
      sw=combine(strip(0, 1.2, cc(5)), grid(6, DARK_GLASS, rgb_index((205, 195, 170)), period=2.0)),
      se=combine(bands(6, FRAME, lo=1.0, hi=2.0), bands(6, rgb_index((95, 85, 70)), lo=2.0, hi=3.4), grid(6, DARK_GLASS, period=2.0, low=3.4, sill=0.6), strip(5.4, 6.6, cc(4))),
      roof=lambda x, y: cc(6) if x < 2.6 or y < 3.6 or x > 13.4 or y > 12.4 else None)
f.box(4, 5, 6, 7, 30, 34, (190, 190, 195), (210, 210, 215))
f.box(10, 6, 12, 8, 30, 33, (190, 190, 195), (210, 210, 215))
house(f, "walk-up flats")

# HDB slab blocks: twelve storeys with corridors along the long side, a void deck on pillars,
# a colourful end wall and a lift motor room on the roof; two tiles long, along X and along Y.
def slab_part(axis, part):
    b = Building()
    storey, top = 4.6, 6 + 12 * 4.6
    corridor = combine(bands(storey, cc(7), base=6, lo=0.9, hi=1.9), bands(storey, rgb_index((80, 80, 85)), base=6, lo=1.9, hi=2.9),
                       grid(storey, DARK_GLASS, period=1.4, frac=(0.25, 0.65), base=6, low=2.9, sill=0.4),
                       pillars(2.3, 0.5, 6, rgb_index((215, 215, 210)), rgb_index((55, 55, 60))))
    end = combine(pillars(2.0, 0.6, 6, rgb_index((215, 215, 210)), rgb_index((55, 55, 60))),
                  strip(1.6, 4.2, cc(6), 6, top), strip(4.2, 6.4, cc(3), 6, top))
    back = combine(pillars(2.3, 0.5, 6, rgb_index((215, 215, 210)), rgb_index((55, 55, 60))), grid(storey, DARK_GLASS, rgb_index((210, 205, 195)), period=1.4, frac=(0.3, 0.7), base=6))
    if axis == 'x':
        x0, x1 = (1, 16) if part == 0 else (0, 15)
        b.box(x0, 4, x1, 12, 0, top, (232, 228, 215), (140, 140, 140), sw=end if part == 1 else back, se=corridor,
              roof=lambda x, y: cc(5) if y < 4.6 or y > 11.4 else None)
        if part == 0:
            b.box(6, 6, 9, 10, top, top + 6, (215, 210, 200), (150, 150, 150), sw=strip(0.5, 3.5, cc(6), 2, 5))
        else:
            b.box(5, 5.5, 8, 7.5, top, top + 4, (190, 190, 195), (215, 215, 220))
    else:
        y0, y1 = (1, 16) if part == 0 else (0, 15)
        b.box(4, y0, 12, y1, 0, top, (232, 228, 215), (140, 140, 140), sw=corridor, se=end if part == 1 else back,
              roof=lambda x, y: cc(5) if x < 4.6 or x > 11.4 else None)
        if part == 0:
            b.box(6, 6, 10, 9, top, top + 6, (215, 210, 200), (150, 150, 150), se=strip(0.5, 3.5, cc(6), 2, 5))
        else:
            b.box(5.5, 5, 7.5, 8, top, top + 4, (190, 190, 195), (215, 215, 220))
    return b
for axis in ('x', 'y'):
    for part in (0, 1):
        house(slab_part(axis, part), f"HDB slab {axis} {part}")

# HDB point block: a twenty-storey tower with colourful corner columns and a void deck.
pb = Building()
storey, top = 4.6, 6 + 19 * 4.6
pb_face = combine(pillars(2.5, 0.6, 6, rgb_index((215, 215, 210)), rgb_index((55, 55, 60))),
                  strip(0, 1.4, cc(6), 6, top), strip(8.6, 10, cc(4), 6, top),
                  bands(storey, cc(7), base=6, lo=0.0, hi=0.8),
                  grid(storey, DARK_GLASS, period=1.8, frac=(0.25, 0.75), base=6, low=1.4, sill=0.5))
pb.box(3, 3, 13, 13, 0, top, (236, 232, 222), (140, 140, 140), sw=pb_face, se=pb_face,
       roof=lambda x, y: cc(5) if x < 3.7 or y < 3.7 or x > 12.3 or y > 12.3 else None)
pb.box(6, 6, 10, 10, top, top + 7, (220, 215, 205), (150, 150, 150), sw=strip(0.5, 3.5, cc(6), 3, 6), se=strip(0.5, 3.5, cc(6), 3, 6))
house(pb, "HDB point block")

# Condominium: a slender glass tower with balconies on a podium with a pool deck.
co = Building()
pool, deck = rgb_index((70, 160, 215)), rgb_index((200, 185, 150))
def pool_deck(x, y):
    if 2 <= x <= 4.6 and 2 <= y <= 14:
        return pool if 2.3 <= x <= 4.3 and 2.3 <= y <= 13.7 else rgb_index((235, 235, 235))
    if x < 1.6 or y < 1.6 or x > 14.4 or y > 14.4:
        return rgb_index((150, 150, 150))
    return deck if (int(x * 2) + int(y * 2)) % 7 else rgb_index((70, 140, 60))
co.box(1, 1, 15, 15, 0, 10, (205, 205, 210), (190, 175, 145), sw=pillars(1.6, 0.9, 10, rgb_index((190, 190, 195)), rgb_index((50, 50, 55))),
       se=pillars(1.6, 0.9, 10, rgb_index((190, 190, 195)), rgb_index((50, 50, 55))), roof=pool_deck)
c_storey, c_top = 4.4, 10 + 26 * 4.4
c_face = combine(strip(0, 0.8, cc(5), 10, c_top), bands(c_storey, FRAME, lo=0.0, hi=1.0), bands(c_storey, rgb_index((160, 200, 220)), lo=1.0, hi=1.6),
                 grid(c_storey, SKY_GLASS, period=1.2, frac=(0.1, 0.9), low=1.6, sill=0.3))
co.box(6, 3, 13, 10, 10, c_top, (240, 240, 240), (170, 170, 175), sw=c_face, se=c_face)
co.box(7, 4, 12, 9, c_top, c_top + 6, (230, 230, 235), (150, 150, 150), sw=strip(0, 5, cc(6), 4, 6), se=strip(0, 5, cc(6), 4, 6))
house(co, "condominium")

# Shophouses: a row of three-storey shophouses with a five-foot way, shuttered windows and signboards.
sh = Building()
signs = [rgb_index((200, 40, 30)), rgb_index((240, 200, 40)), rgb_index((40, 110, 190))]
def shop_front(u, v):
    unit = int(u // 4.6)
    uu = u % 4.6
    if v < 6:
        if uu < 0.6:
            return rgb_index((225, 220, 205))
        return rgb_index((50, 45, 45)) if v < 5.2 else rgb_index((90, 80, 70))
    if 6 <= v < 7.6 and 0.8 <= uu < 3.8:
        return signs[unit % 3]
    if 9 <= v < 12.5 or 14 <= v < 17.5:
        if 0.8 <= uu < 1.8 or 2.8 <= uu < 3.8:
            return rgb_index((60, 110, 70)) if int(v) % 2 else rgb_index((80, 140, 90))
    if uu < 0.25:
        return rgb_index((240, 235, 225))
    return cc(7 - unit % 3)
sh.box(1, 2, 15, 13, 0, 19, (235, 225, 205), TERRACOTTA, sw=lambda u, v: cc(5) if v >= 6 else rgb_index((70, 60, 55)), se=shop_front)
for (i, (y0, y1)) in enumerate(((1.4, 13.6), (3.6, 11.4), (5.8, 9.2))):
    sh.box(0.6, y0, 15.4, y1, 19 + 2 * i, 21 + 2 * i, TERRACOTTA, (195, 100, 65), shade=0.7)
house(sh, "shophouses")

# Office block of the 1970s: concrete with ribbon windows and a plant room on the roof.
ob = Building()
ob_face = combine(bands(4.5, rgb_index((40, 50, 60)), lo=1.2, hi=3.2), lambda u, v: rgb_index((175, 170, 160)) if (u % 2.5) < 0.3 else None)
ob.box(3, 3, 13, 13, 0, 72, (205, 200, 188), (150, 150, 150), sw=ob_face, se=ob_face)
ob.box(5, 5, 10, 9, 72, 79, (190, 185, 175), (140, 140, 140))
house(ob, "office block")

# Glass office tower: a blue-green curtain wall with a set-back crown and a mast.
gt = Building()
mull = rgb_index((170, 205, 210))
gt_face = combine(lambda u, v: mull if (u % 1.0) < 0.18 else None, bands(4.5, rgb_index((90, 140, 150)), lo=0, hi=0.7))
gt.box(3.5, 3.5, 12.5, 12.5, 0, 146, (70, 125, 140), (120, 130, 135), sw=gt_face, se=gt_face, shade=0.65)
gt.box(5, 5, 11, 11, 146, 160, (85, 140, 155), (150, 160, 165), sw=gt_face, se=gt_face, shade=0.65)
post(gt, 8, 8, 160, 178, rgb_index((200, 200, 205)))
house(gt, "glass office tower")

# Helpers for buildings of two or four tiles: boxes, ground, trees and posts in the building's own
# coordinates (two tiles = 32 units), cut to one part's tile.
def part_box(b, part, gx0, gy0, gx1, gy1, z0, z1, wall, top, sw=None, se=None, roof=None, shade=0.72):
    """A box in the building's own coordinates (two tiles = 32 units), cut to one part's tile."""
    ox, oy = 16 * part[0], 16 * part[1]
    x0, x1 = max(gx0, ox), min(gx1, ox + 16)
    y0, y1 = max(gy0, oy), min(gy1, oy + 16)
    if x0 >= x1 or y0 >= y1:
        return
    b.box(x0 - ox, y0 - oy, x1 - ox, y1 - oy, z0, z1, wall, top,
          sw=(lambda u, v: sw(u + y0 - gy0, v)) if sw else None,
          se=(lambda u, v: se(u + x0 - gx0, v)) if se else None,
          roof=(lambda x, y: roof(x + ox, y + oy)) if roof else None, shade=shade)


def part_ground(b, part, fn):
    ox, oy = 16 * part[0], 16 * part[1]
    b.ground(lambda x, y: fn(x + ox, y + oy))


def part_tree(b, part, gx, gy, size=4.0, height=8):
    ox, oy = 16 * part[0], 16 * part[1]
    if ox <= gx < ox + 16 and oy <= gy < oy + 16:
        b.tree(gx - ox, gy - oy, size=size, height=height)


def part_post(b, part, gx, gy, z0, z1, colour):
    ox, oy = 16 * part[0], 16 * part[1]
    if ox <= gx < ox + 16 and oy <= gy < oy + 16:
        post(b, gx - ox, gy - oy, z0, z1, colour)


PARTS = ((0, 0), (1, 0), (0, 1), (1, 1))


# Shopping mall: two by two tiles, five storeys of shops with a big sign band and banners in its
# colours, a glass atrium at the front corner, a skylight and a rooftop car park with cars.
MALL_WALL = (234, 228, 214)
CAR_RGB = [(200, 40, 35), (240, 240, 240), (60, 90, 170), (60, 60, 65), (180, 180, 185),
           (240, 200, 60), (40, 120, 70), (150, 40, 40), (230, 230, 230), (90, 90, 100)]
CAR_COLOURS = [rgb_index(c) for c in CAR_RGB]
def mall_face(u, v):
    if v < 6:
        if (u % 4.0) < 0.4:
            return FRAME
        return SKY_GLASS if v < 5 else rgb_index((90, 90, 95))
    if v < 7:
        return rgb_index((245, 245, 245))
    if 24 <= v < 30:
        if 25.5 <= v < 28.5 and 3 <= u % 13 < 10 and int(u * 1.5) % 4 != 3:
            return rgb_index((250, 250, 250))
        return cc(4)
    if u % 9 < 1.6 and 9 <= v < 22:
        return cc(6)
    if 10 <= v < 20 and 3.5 <= u % 9 < 7:
        return rgb_index((200, 195, 185)) if int(v) % 4 else rgb_index((180, 175, 168))
    return None
def mall_roof(x, y):
    if 12 <= x <= 20 and 3 <= y <= 18:
        return SKY_GLASS if int(y) % 2 else rgb_index((200, 220, 230))
    if x < 11 and y < 22 and x > 2 and y > 2:
        # Rooftop car park: rows of cars on asphalt.
        if 4 <= x % 5 < 5:
            return rgb_index((230, 230, 230)) if int(y * 2) % 4 == 0 else rgb_index((80, 80, 85))
        if (x % 5) < 3.2 and (y % 2.0) < 1.4:
            k = int(x // 5) * 13 + int(y // 2)
            return CAR_COLOURS[(k * 7) % len(CAR_COLOURS)] if k % 5 != 2 else rgb_index((80, 80, 85))
        return rgb_index((80, 80, 85))
    return None
def atrium_face(u, v):
    if (u % 1.5) < 0.25 or v % 5 < 0.6:
        return FRAME
    return SKY_GLASS
for (px_, py_) in ((0, 0), (0, 1), (1, 0), (1, 1)):
    b = Building()
    part = (px_, py_)
    part_box(b, part, 1, 1, 31, 31, 0, 30, MALL_WALL, (170, 170, 172), sw=mall_face, se=mall_face, roof=mall_roof)
    part_box(b, part, 23, 23, 31, 31, 0, 38, (220, 235, 240), (200, 200, 205), sw=atrium_face, se=atrium_face,
             roof=lambda x, y: SKY_GLASS if (int(x) + int(y)) % 2 else rgb_index((200, 220, 230)))
    part_box(b, part, 31, 25, 32, 31, 5, 6, (245, 245, 245), (245, 245, 245))
    part_box(b, part, 25, 31, 31, 32, 5, 6, (245, 245, 245), (245, 245, 245))
    house(b, f"mall {px_} {py_}")

# Landmark tower: a tapering steel and glass tower with a spire.
lt = Building()
steel = lambda u, v: rgb_index((215, 220, 225)) if (u % 0.8) < 0.3 else None
lt_face = combine(steel, bands(4.0, rgb_index((120, 135, 150)), lo=0, hi=0.6))
for (i, (a, z0, z1)) in enumerate(((3, 0, 64), (4, 64, 120), (5, 120, 160), (6, 160, 176))):
    lt.box(a, a, 16 - a, 16 - a, z0, z1, (150, 160, 170), (190, 195, 200), sw=lt_face, se=lt_face, shade=0.7)
post(lt, 8, 8, 176, 196, rgb_index((230, 230, 235)))
house(lt, "landmark tower")

# Mixed development: a shopping podium with a roof garden and a residential tower over it.
md = Building()
def podium_face(u, v):
    if v < 6:
        return SKY_GLASS if (u % 3.0) > 0.5 else FRAME
    if 9 <= v < 14 and 2 <= u % 7 < 6:
        return cc(6)
    return None
md.box(1, 1, 15, 15, 0, 16, (222, 218, 208), (90, 150, 70), sw=podium_face, se=podium_face,
       roof=lambda x, y: rgb_index((70, 140, 60)) if (int(x) + int(y)) % 3 else rgb_index((95, 165, 80)))
m_face = combine(bands(4.4, FRAME, lo=0, hi=0.9), grid(4.4, SKY_GLASS, period=1.4, frac=(0.15, 0.85), low=0.9, sill=0.3), strip(0, 1.0, cc(5)),
                 lambda u, v: rgb_index((70, 150, 60)) if 52 <= v < 56 else None)
md.box(5, 4, 12, 11, 16, 106, (238, 238, 238), (160, 160, 165), sw=m_face, se=m_face)
house(md, "mixed development")

# Mosque: white walls with arched windows, a golden dome and a minaret.
mo = Building()
arch = lambda u, v: rgb_index((40, 110, 70)) if (u % 2.4) > 0.9 and 3 <= v < 9 and not (v > 8 and (u % 2.4) in (0.9, 2.4)) else None
mo.box(3, 3, 13, 13, 0, 13, (240, 238, 230), (225, 222, 210), sw=arch, se=arch,
       roof=lambda x, y: rgb_index((40, 110, 70)) if x < 3.6 or y < 3.6 or x > 12.4 or y > 12.4 else None)
dome(mo, 8, 8, 13, 3.6, 14, (220, 180, 50))
mo.set(round(32), round(16 - 13 - 14) - 1, rgb_index((230, 200, 60)))
mo.set(round(32), round(16 - 13 - 14) - 2, rgb_index((230, 200, 60)))
mo.box(1.6, 12, 3.4, 13.8, 0, 36, (240, 238, 230), (225, 222, 210), sw=strip(0, 2, rgb_index((40, 110, 70)), 28, 30), se=strip(0, 2, rgb_index((40, 110, 70)), 28, 30))
dome(mo, 2.5, 12.9, 36, 1.0, 4, (220, 180, 50))
house(mo, "mosque")

# Chinese temple: red walls, a two-tier orange roof with upturned ends, lanterns and an incense burner.
te = Building()
temple_red = (185, 45, 35)
te.box(3, 4, 13, 12, 0, 11, temple_red, (200, 120, 40),
       se=combine(strip(4, 6, rgb_index((60, 30, 20)), 0, 7), strip(1, 2, rgb_index((240, 200, 60)), 5, 8), strip(8, 9, rgb_index((240, 200, 60)), 5, 8)),
       sw=strip(2, 6, rgb_index((150, 35, 30)), 3, 8))
te.box(2, 3, 14, 13, 11, 13, (205, 125, 40), (215, 140, 50), shade=0.65)
te.box(3.5, 4.5, 12.5, 11.5, 13, 16, temple_red, (205, 125, 40))
te.box(3, 4, 13, 12, 16, 18, (205, 125, 40), (225, 150, 55), shade=0.65)
te.box(4, 7.3, 12, 8.7, 18, 19.5, (230, 170, 60), (240, 190, 70))
for (x, y, z) in ((2, 3, 13), (14, 13, 13), (2, 13, 13), (3, 4, 18), (13, 12, 18)):
    sx, sy = round(32 + 2 * (y - x)), round(x + y) - z
    te.set(sx, sy - 1, rgb_index((240, 200, 60)))
te.box(7.4, 13.6, 8.6, 14.8, 0, 3, (120, 90, 50), (150, 110, 60))
house(te, "Chinese temple")


# ---------------------------------------------------------------------------
# Bigger city services (city_services.cpp), from sprite 273 on: hospital, university
# and stadium (two by two tiles, four parts each in the order (0,0), (1,0), (0,1), (1,1)),
# a hawker centre, then their toolbar icons.

HOSP_WALL, HOSP_TEAL = (240, 242, 244), rgb_index((40, 150, 150))
def hosp_face(u, v):
    if 0 <= v % 5 < 0.8:
        return HOSP_TEAL if v > 6 else None
    if (u % 1.6) < 1.0 and 1.5 <= v % 5 < 4.2:
        return SKY_GLASS
    return None
def helipad(x, y):
    d = math.hypot(x - 15, y - 11)
    if d < 4.2:
        if d > 3.6:
            return rgb_index((240, 240, 240))
        if abs(x - 15) < 0.4 and abs(y - 11) < 2.0 or (abs(y - 11) < 0.4 and abs(x - 15) < 1.4):
            return rgb_index((240, 240, 240))
        if (abs(x - 13.6) < 0.4 or abs(x - 16.4) < 0.4) and abs(y - 11) < 2.0:
            return rgb_index((240, 240, 240))
        return rgb_index((70, 75, 80))
    return None
for part in PARTS:
    b = Building()
    part_ground(b, part, lambda x, y: rgb_index((150, 150, 150)) if 24 < y < 30 and 6 < x < 26 else None)
    part_box(b, part, 4, 4, 27, 20, 0, 46, HOSP_WALL, (150, 150, 155), sw=hosp_face, se=hosp_face, roof=helipad)
    part_box(b, part, 3, 20, 29, 27, 0, 15, HOSP_WALL, (165, 165, 170), sw=hosp_face,
             se=combine(lambda u, v: rgb_index((210, 30, 30)) if 11.5 <= u <= 14.5 and 9 <= v < 14 and (abs(u - 13) < 0.5 or abs(v - 11.5) < 0.8) else None, hosp_face))
    part_box(b, part, 10, 27, 18, 30, 5, 6.5, (215, 215, 220), (230, 60, 50))
    house(b, f"hospital {part[0]} {part[1]}")

UNI_WALL = (215, 190, 150)
def uni_face(u, v):
    if 0 <= v % 6 < 0.7:
        return rgb_index((185, 160, 120))
    if (u % 2.0) < 1.0 and 2 <= v % 6 < 5:
        return DARK_GLASS
    return None
def quad(x, y):
    if 13.5 <= x <= 15 and 4 <= y <= 30:
        return rgb_index((205, 185, 140))
    if 4 <= x <= 30 and 23 <= y <= 24.5:
        return rgb_index((205, 185, 140))
    return rgb_index((80, 150, 60)) if (int(x * 3) + int(y * 3)) % 5 else rgb_index((95, 165, 70))
for part in PARTS:
    b = Building()
    part_ground(b, part, quad)
    part_box(b, part, 3, 3, 12, 29, 0, 24, UNI_WALL, (165, 75, 55), sw=uni_face, se=uni_face,
             roof=lambda x, y: rgb_index((185, 90, 65)) if (int(x) + int(y)) % 3 == 0 else None)
    part_box(b, part, 6, 13, 10, 17, 24, 40, UNI_WALL, (165, 75, 55),
             sw=lambda u, v: rgb_index((250, 250, 250)) if 1.2 <= u <= 2.8 and 8 <= v < 12 else None,
             se=lambda u, v: rgb_index((250, 250, 250)) if 1.2 <= u <= 2.8 and 8 <= v < 12 else None)
    part_box(b, part, 17, 3, 29, 12, 0, 18, (225, 225, 220), (150, 150, 150), sw=uni_face, se=uni_face)
    for (tx, ty) in ((20, 17), (25, 20), (21, 28), (27, 27)):
        part_tree(b, part, tx, ty, size=3.6, height=8)
    house(b, f"university {part[0]} {part[1]}")

seat_cols = [cc(3), cc(5), cc(6), cc(4)]
def stand_face(u, v):
    if v < 2:
        return rgb_index((120, 120, 125))
    return seat_cols[int(v / 2) % 4] if (u % 1.0) < 0.8 else rgb_index((80, 80, 85))
def pitch(x, y):
    if 7 <= x <= 25 and 7 <= y <= 25:
        line = abs(x - 7.3) < 0.3 or abs(x - 24.7) < 0.3 or abs(y - 7.3) < 0.3 or abs(y - 24.7) < 0.3 or abs(y - 16) < 0.25 or abs(math.hypot(x - 16, y - 16) - 3) < 0.3
        if line:
            return rgb_index((245, 245, 245))
        return rgb_index((60, 150, 50)) if int((x - 7) / 2) % 2 else rgb_index((80, 170, 60))
    return None
for part in PARTS:
    b = Building()
    part_ground(b, part, pitch)
    part_box(b, part, 2, 2, 30, 6.5, 0, 14, (170, 170, 175), (200, 200, 205), se=stand_face, sw=stand_face)
    part_box(b, part, 2, 6.5, 6.5, 25.5, 0, 10, (170, 170, 175), (200, 200, 205), sw=stand_face, se=stand_face)
    part_box(b, part, 25.5, 6.5, 30, 25.5, 0, 10, (170, 170, 175), (200, 200, 205), sw=stand_face, se=stand_face)
    part_box(b, part, 2, 25.5, 30, 30, 0, 8, (170, 170, 175), (200, 200, 205), sw=stand_face, se=stand_face)
    part_box(b, part, 2, 2, 30, 4.5, 14, 16, (225, 225, 230), (235, 235, 240))
    for (mx, my) in ((3, 3), (29, 3), (3, 29), (29, 29)):
        part_post(b, part, mx, my, 0, 38, rgb_index((150, 150, 155)))
        part_box(b, part, mx - 1, my - 1, mx + 1, my + 1, 38, 41, (240, 240, 200), (255, 255, 220))
    house(b, f"stadium {part[0]} {part[1]}")

hc = Building()
stall_cols = [rgb_index((200, 40, 30)), rgb_index((240, 190, 40)), rgb_index((40, 120, 190)), rgb_index((60, 160, 80))]
for (i, x) in enumerate((3.0, 6.2, 9.4, 12.6)):
    hc.box(x, 2.5, x + 2.6, 4.5, 0, 5, (200, 195, 185), (230, 225, 215), se=lambda u, v, c=stall_cols[i]: c if 3 <= v < 5 else None)
for (x, y) in ((4.5, 8), (8.5, 8), (12.5, 8), (4.5, 12), (8.5, 12), (12.5, 12)):
    hc.box(x - 0.9, y - 0.9, x + 0.9, y + 0.9, 0, 2.5, (150, 120, 80), (180, 150, 100))
for (x, y) in ((2, 2), (14, 2), (2, 14), (14, 14), (8, 2), (8, 14), (2, 8), (14, 8)):
    hc.box(x - 0.35, y - 0.35, x + 0.35, y + 0.35, 0, 9, (220, 220, 220), (220, 220, 220))
for (i, (y0, y1)) in enumerate(((0.8, 15.2), (2.6, 13.4), (4.6, 11.4))):
    hc.box(0.8, y0, 15.2, y1, 9 + 2 * i, 11 + 2 * i, (150, 50, 40), (175, 70, 50), shade=0.65)
hc.box(6, 15.2, 10, 15.6, 6, 9, (240, 240, 240), (240, 240, 240), se=lambda u, v: rgb_index((200, 40, 30)) if 0.4 <= u <= 3.6 and 1 <= v < 2.4 else None)
house(hc, "hawker centre")

icon(4, 1930, [
    ".KKKKKKKKKKKK.",
    "KwwwwwwwwwwwwK",
    "KwRRwwwwRRwwwK",
    "KwRRwwwwRRwwwK",
    "KwRRRRRRRRwwwK",
    "KwRRRRRRRRwRwK",
    "KwRRwwwwRRRRRK",
    "KwRRwwwwRRwRwK",
    "KwwwwwwwwwwwwK",
    "KccccccccccccK",
    "KcwwcwwcwwcwwK",
    "KccccccccccccK",
    "KcwwcwwcwwcwwK",
    "KKKKKKKKKKKKKK",
    "..............",
], {'K': BLACK, 'w': WHITE, 'R': rgb_index((210, 30, 30)), 'c': rgb_index((40, 150, 150))}, "icon hospital")
icon(22, 1930, [
    "......KK......",
    "....KKbbKK....",
    "..KKbbbbbbKK..",
    "KKbbbbbbbbbbKK",
    "..KKbbbbbbKK.Y",
    "....KKbbKK...Y",
    "...KbKKKKbK..Y",
    "...KbbbbbbK.YY",
    "...KbbbbbbK.YY",
    "....KKKKKK....",
    "..............",
    "..ssssssssss..",
    "..sKsKsKsKss..",
    "..ssssssssss..",
    "..............",
], {'K': BLACK, 'b': rgb_index((40, 50, 90)), 'Y': YELLOW, 's': rgb_index((215, 190, 150))}, "icon university")
icon(40, 1930, [
    "..............",
    "...gggggggg...",
    ".ggGGGGGGGGgg.",
    "gGGGwGGGGwGGGg",
    "gGGGwGGGGwGGGg",
    "gGGGwwwwwwGGGg",
    "gGGGwGwwGwGGGg",
    "gGGGwwwwwwGGGg",
    "gGGGwGGGGwGGGg",
    "gGGGwGGGGwGGGg",
    ".ggGGGGGGGGgg.",
    "...gggggggg...",
    "..............",
    "..............",
    "..............",
], {'g': rgb_index((150, 150, 155)), 'G': rgb_index((70, 160, 60)), 'w': WHITE}, "icon stadium")
icon(58, 1930, [
    "..RRRRRRRRRR..",
    ".RRRRRRRRRRRR.",
    "RRRRRRRRRRRRRR",
    ".W..........W.",
    ".W..........W.",
    ".W.yyy..ooo.W.",
    ".WyyyyyoooooW.",
    ".W.yyy..ooo.W.",
    ".WbbbbbbbbbbW.",
    ".WbwwbbwwbbbW.",
    ".WbbbbbbbbbbW.",
    ".W..........W.",
    "..............",
    "..............",
    "..............",
], {'R': rgb_index((175, 60, 45)), 'W': rgb_index((200, 200, 200)), 'y': YELLOW, 'o': rgb_index((240, 140, 40)), 'b': rgb_index((150, 120, 80)), 'w': WHITE}, "icon hawker centre")

# ---------------------------------------------------------------------------
# Shops (city_houses.cpp), from sprite 290 on: a supermarket, a shopping complex (two tiles,
# along X: north and south part, then along Y), a hypermarket with its car park (two by two
# tiles: (0,0), (0,1), (1,0), (1,1), the order of the house parts). Their signs are in the
# building's colours, so each one looks like another chain.

ASPHALT = rgb_index((82, 82, 88))
BAY_LINE = rgb_index((232, 232, 232))
def car(b, part, x, y, k, along_x=True):
    """A parked car, 2.2 by 1.2 units, in one of the car colours."""
    c = CAR_RGB[k % len(CAR_RGB)]
    dx, dy = (1.1, 0.6) if along_x else (0.6, 1.1)
    if part is None:
        b.box(x - dx, y - dy, x + dx, y + dy, 0, 2, c, c)
        b.box(x - dx * 0.5, y - dy * 0.8, x + dx * 0.5, y + dy * 0.8, 2, 3, c, (150, 190, 210))
    else:
        part_box(b, part, x - dx, y - dy, x + dx, y + dy, 0, 2, c, c)
        part_box(b, part, x - dx * 0.5, y - dy * 0.8, x + dx * 0.5, y + dy * 0.8, 2, 3, c, (150, 190, 210))

# Supermarket: a two-storey store with a glass front, a canopy, a big sign band and a car park.
sm = Building()
def sm_ground(x, y):
    if x >= 10:
        if any(abs(y - l) < 0.18 for l in (1.4, 4.6, 7.8, 11.0, 14.2)) and x > 11.5:
            return BAY_LINE
        return ASPHALT
    return None
sm.ground(sm_ground)
def sm_front(u, v):
    if v < 6:
        if 5.2 <= u < 7.8:
            return SKY_GLASS if v < 5.4 else FRAME
        return DARK_GLASS if (u % 2.2) > 0.3 else FRAME
    if 8.5 <= v < 14:
        if 2.5 <= u < 10.5 and 10 <= v < 12.6 and int(u * 2) % 3 != 2:
            return rgb_index((250, 250, 250))
        return cc(5)
    return None
def sm_side(u, v):
    if 8.5 <= v < 14 and 0.8 <= u < 5.5:
        return cc(5) if not (1.6 <= u < 4.6 and 10 <= v < 12.6) else rgb_index((250, 250, 250))
    return None
sm.box(1.5, 1.5, 10, 14.5, 0, 15, (228, 224, 216), (150, 150, 153), sw=sm_front, se=sm_side,
       roof=lambda x, y: rgb_index((120, 120, 126)) if (3 < x < 5 and 3 < y < 6) or (5.5 < x < 7.5 and 9 < y < 12) else None)
sm.box(10, 4.4, 11.6, 11.8, 5.5, 6.5, (240, 240, 240), (245, 245, 245))
sm.box(10.3, 12.4, 11.3, 14.2, 0, 2, (170, 170, 175), (200, 200, 205))
for (i, y) in enumerate((3.0, 6.2, 9.4, 12.6)):
    if i != 2:
        car(sm, None, 13.4, y, i * 3 + 1, along_x=True)
house(sm, "supermarket")

# Shopping complex of the 1970s and 1980s: a podium of shops with rows of signboards, a rooftop
# car park and an office tower over one end with a tall sign; two tiles long, along X and along Y.
COMPLEX_SIGNS = [rgb_index(c) for c in ((200, 40, 30), (240, 200, 40), (40, 110, 190), (60, 160, 80), (240, 140, 40), (250, 250, 250))]
def complex_long(u, v):
    if v < 5:
        if (u % 3.0) < 0.35:
            return FRAME
        return SKY_GLASS if v < 4.2 else rgb_index((90, 90, 95))
    lv = (v - 5) % 4.75
    if v < 24 and lv < 1.8 and (u % 3.0) >= 0.4:
        return COMPLEX_SIGNS[(int(u // 3.0) + int((v - 5) // 4.75) * 2) % len(COMPLEX_SIGNS)]
    if v < 24 and 2.4 <= lv < 4.2 and (u % 1.5) < 1.0:
        return DARK_GLASS
    if v < 24 and (u % 9.0) < 0.8:
        return cc(5)
    return None
def complex_end(u, v):
    if v < 5:
        return SKY_GLASS if 3 <= u < 9 and v < 4.2 else None
    if 6 <= v < 23 and 4 <= u < 8:
        return cc(6) if not (5 <= u < 7 and int(v) % 3 == 0) else rgb_index((250, 250, 250))
    return None
def complex_roof(axis):
    def fn(x, y):
        a, c = (x, y) if axis == 'x' else (y, x)
        if a < 11:
            return None
        if (c % 6) < 0.25 or (c % 6) > 5.75:
            return BAY_LINE
        if 1 <= (c % 6) < 2.4 or 3.6 <= (c % 6) < 5:
            k = int(a // 2.6) * 5 + int(c // 6) * 2 + (1 if (c % 6) > 3 else 0)
            if (a % 2.6) < 2.0 and k % 4 != 1:
                return CAR_COLOURS[(k * 7) % len(CAR_COLOURS)]
        return ASPHALT
    return fn
tower_face = combine(bands(4.5, rgb_index((45, 55, 65)), lo=1.2, hi=3.2), lambda u, v: rgb_index((180, 175, 165)) if (u % 2.5) < 0.3 else None)
def complex_part(axis, part):
    b = Building()
    if axis == 'x':
        pp = (part, 0)
        part_box(b, pp, 1, 2, 31, 14, 0, 24, (226, 220, 206), (95, 95, 100), sw=complex_end, se=complex_long, roof=complex_roof('x'))
        part_box(b, pp, 2.5, 3.5, 9.5, 12.5, 24, 46, (205, 200, 188), (150, 150, 150), sw=tower_face,
                 se=combine(lambda u, v: cc(6) if 1 <= u < 2.6 and 3 <= v < 20 else None, tower_face))
    else:
        pp = (0, part)
        part_box(b, pp, 2, 1, 14, 31, 0, 24, (226, 220, 206), (95, 95, 100), sw=complex_long, se=complex_end, roof=complex_roof('y'))
        part_box(b, pp, 3.5, 2.5, 12.5, 9.5, 24, 46, (205, 200, 188), (150, 150, 150), se=tower_face,
                 sw=combine(lambda u, v: cc(6) if 1 <= u < 2.6 and 3 <= v < 20 else None, tower_face))
    return b
for axis in ('x', 'y'):
    for part in (0, 1):
        house(complex_part(axis, part), f"shopping complex {axis} {part}")

# Hypermarket: a big two-storey box with an entrance canopy and a sign band in its colours on the
# side facing its car park; rows of parked cars, lamp posts, a trolley shelter and a sign pylon.
def hyper_front(u, v):
    if v < 6:
        if 11 <= u < 17:
            return SKY_GLASS if v < 5.2 else FRAME
        return rgb_index((215, 212, 205)) if v > 0.8 else rgb_index((150, 150, 150))
    if 6 <= v < 8:
        return cc(3)
    if 12 <= v < 19:
        if 6 <= u < 22 and 13.5 <= v < 17.5 and int(u * 1.3) % 5 != 4:
            return rgb_index((250, 250, 250))
        return cc(5)
    return None
def hyper_side(u, v):
    if 6 <= v < 8:
        return cc(3)
    if 11 <= v < 18 and 3 <= u < 10:
        return cc(5) if not (4.5 <= u < 8.5 and 12.5 <= v < 16.5) else rgb_index((250, 250, 250))
    return None
def hyper_roof(x, y):
    if (int(x) % 4 == 1 and int(y) % 5 == 2):
        return rgb_index((110, 110, 115))
    if 6 <= x <= 9 and 5 <= y <= 27:
        return rgb_index((205, 220, 228)) if int(y) % 3 else rgb_index((180, 200, 210))
    return None
def hyper_ground(x, y):
    if x < 16.5:
        return None
    if 16.5 <= x < 18.5:
        return rgb_index((185, 182, 175))
    if any(abs(x - l) < 0.15 for l in (19.0, 22.5, 26.0, 29.5)) or ((x - 19.0) % 7.0 < 3.5 and (y % 2.4) < 0.15):
        return BAY_LINE
    return ASPHALT
for part in ((0, 0), (0, 1), (1, 0), (1, 1)):
    b = Building()
    part_ground(b, part, hyper_ground)
    part_box(b, part, 2, 2, 16, 30, 0, 20, (232, 230, 224), (190, 190, 194), sw=hyper_front, se=hyper_side, roof=hyper_roof)
    part_box(b, part, 16, 10, 18.2, 20, 6, 7, (240, 240, 240), (240, 240, 240))
    part_box(b, part, 17, 21.5, 18.4, 25, 0, 4, (170, 170, 175), (200, 200, 205))
    k = 0
    for xr in (20.75, 27.75):
        for (i, yc) in enumerate((2.4, 4.8, 7.2, 9.6, 12.0, 14.4, 16.8, 19.2, 21.6, 24.0, 26.4)):
            for side in (-1.05, 1.05):
                k += 1
                if (k * 5 + i) % 7 in (0, 3):
                    continue
                car(b, part, xr + side, yc, (k * 37 + i * 11 + (3 if side > 0 else 0)) % 97, along_x=True)
    for (lx, ly) in ((24.25, 6), (24.25, 16), (24.25, 26)):
        part_post(b, part, lx, ly, 0, 12, rgb_index((150, 150, 155)))
        part_box(b, part, lx - 0.4, ly - 0.4, lx + 0.4, ly + 0.4, 12, 13, (240, 240, 200), (255, 255, 220))
    part_post(b, part, 30.2, 30.2, 0, 18, rgb_index((140, 140, 145)))
    part_box(b, part, 29.4, 28.6, 31, 31, 18, 30, (230, 230, 230), (210, 210, 210),
             sw=lambda u, v: rgb_index((250, 250, 250)) if 0.6 <= u < 1.8 and 4 <= v < 9 else cc(5),
             se=lambda u, v: rgb_index((250, 250, 250)) if 0.3 <= u < 1.3 and 4 <= v < 9 else cc(5))
    house(b, f"hypermarket {part[0]} {part[1]}")

print("house rows end at", houses_y)

assert len(sprites) == 299, len(sprites)

# Icon of the map of states: two states split by a border, a capital's star, the sea.
icon(340, 934, [
    "................",
    ".KKKKKKKKKKKKKK.",
    ".KRRRRRRKGGGGBK.",
    ".KRRRYRRKGGGBBK.",
    ".KRRYYYRRKGGBBK.",
    ".KRRRYRRRKGGGBK.",
    ".KRRRRRRKGGGGBK.",
    ".KRRRRRKGGGGGBK.",
    ".KRRRRRRKGGGBBK.",
    ".KRRRRRRRKGGBBK.",
    ".KRRRRRRKGGGGBK.",
    ".KKKKKKKKKKKKKK.",
    "................",
    "................",
], {'K': rgb_index((40, 40, 40)), 'R': rgb_index((200, 60, 50)), 'G': rgb_index((60, 160, 70)), 'Y': rgb_index((250, 220, 60)), 'B': rgb_index((60, 110, 200))}, "icon states map")

assert len(sprites) == 300, len(sprites)

# The Wira Bandar wordmark for the main menu, in the style of OpenTTD's title letters: orange letters, lit on top,
# a dark outline and a brown shadow. Letters on a 5 by 7 grid, each cell 3 by 3 pixels.
LOGO_GLYPHS = {
    'W': ["X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X"],
    'I': ["XXX", ".X.", ".X.", ".X.", ".X.", ".X.", "XXX"],
    'R': ["XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"],
    'A': [".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    'B': ["XXXX.", "X...X", "X...X", "XXXX.", "X...X", "X...X", "XXXX."],
    'N': ["X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X", "X...X"],
    'D': ["XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX."],
}


def wordmark(text, cell=3, letter_gap=2, word_gap=9):
    """Rows for put(): F face, H lit top edge, K outline, S shadow."""
    face = set()
    x = 1
    for ch in text:
        if ch == ' ':
            x += word_gap
            continue
        glyph = LOGO_GLYPHS[ch]
        for gy, row in enumerate(glyph):
            for gx, c in enumerate(row):
                if c != 'X':
                    continue
                for dy in range(cell):
                    for dx in range(cell):
                        face.add((x + gx * cell + dx, 1 + gy * cell + dy))
        x += len(glyph[0]) * cell + letter_gap
    width = x - letter_gap + 3
    height = 7 * cell + 4
    grid = [['.'] * width for _ in range(height)]
    for (fx, fy) in face:
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if 0 <= fx + dx < width and 0 <= fy + dy < height and (fx + dx, fy + dy) not in face:
                    grid[fy + dy][fx + dx] = 'K'
    for (fx, fy) in face:
        sx, sy = fx + 2, fy + 2
        if sx < width and sy < height and grid[sy][sx] == '.':
            grid[sy][sx] = 'S'
    for (fx, fy) in face:
        grid[fy][fx] = 'H' if (fx, fy - 1) not in face else 'F'
    return [''.join(r) for r in grid]


logo_rows = wordmark("WIRA BANDAR")
icon(4, 2240, logo_rows, {'F': rgb_index((252, 176, 48)), 'H': rgb_index((252, 216, 152)), 'K': rgb_index((32, 4, 0)),
                          'S': rgb_index((112, 72, 24))}, "logo wira bandar")

assert len(sprites) == 301, len(sprites)

# Road markings on roads up a slope: all-day bus lane, peak bus lane, bus-only road, kerbed
# busway, expressway; each on SLOPE_NE, SLOPE_SE, SLOPE_SW, SLOPE_NW (roads along X climb
# to the north-east or south-west, along Y to the south-east or north-west).
SLOPED_MARKINGS = [
    (lambda axis: lane_fn(axis, False), "bus lane"),
    (lambda axis: lane_fn(axis, True), "peak bus lane"),
    (bus_road_fn, "bus-only road"),
    (busway_fn, "kerbed busway"),
    (expressway_fn, "expressway"),
]
for m, (make_fn, name) in enumerate(SLOPED_MARKINGS):
    for s, (slope, axis, slope_name) in enumerate(((0x0C, 'x', "NE"), (0x06, 'y', "SE"), (0x03, 'x', "SW"), (0x09, 'y', "NW"))):
        k = m * 4 + s
        b = Building()
        b.ground_sloped(make_fn(axis), slope)
        b.emit(4 + (k % 7) * 72, 2280 + (k // 7) * 50, f"{name} on slope {slope_name}")

assert len(sprites) == 321, len(sprites)
sheet.save(f"{SRC}/city.png", optimize=True)

header = """// This file is part of OpenTTD.
// OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
// OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
//
// City fork graphics, generated by make_city_sprites.py (edit that, not this file).
//   0-3:   traffic light post, front red/amber/green, back
//   4-22:  tile overlay in company colours, by SlopeToSpriteOffset
//   23-24: icons: traffic lights, traffic overlay
//   25-29: icons: zone residential, commercial, industrial, park, clear
//   30-34: city service buildings: police, fire, clinic, school, park
//   35-39: icons: police, fire, clinic, school, park
//   40:    icon: network map of lines
//   41-72: street life vehicles: car, van, lorry, motorbike; 8 directions each
//   73-78: street life people: light and dark skin; walking (two steps) and standing
//   79-190: city buses, 8 directions each: minibus, midibus, 12 m, double-deck, articulated front and rear;
//           the same electric; BRT front and rear
//   191-200: depot yards: park and charging bay ground, solar roof, multi-storey park, chargers, fast chargers,
//            substation, repair bay, wash and fuel lane, staff building
//   201-203: charger over a bus stop (road along X, along Y), icon of the stop charger tool
//   204-211: bus lanes (all day, peak), bus-only road, give-way markings; each for roads along X and along Y
//   212-218: icons: bus priority, bus lane, peak bus lane, give way, bus signal; "B" lamp lit and dark
//   219-222: kerbed busway (road along X, along Y), deck under a stop raised onto a viaduct (X, Y)
//   223-226: BRT station sides: road along X far and near side, along Y far and near side
//   227-228: icons: kerbed busway, BRT station
//   229-230: expressway lanes and barrier (road along X, along Y)
//   231-238: toll plazas: roof, far booth, near booth (road along X, then along Y); free-flow gantries (X, Y)
//   239-241: icons: tolls, toll plaza, free-flow gantry
//   242-243: roof over the bus stops of a bus interchange, and of an integrated transport hub
//   244-247: platform screen doors: railway along X far and near platform, along Y far and near platform
//   248-249: icons: air pollution and land value views of the map
//   250-252: a house under construction, three stages
//   253-272: Malaysian and Singaporean town buildings: kampung house, terrace houses, walk-up flats,
//            HDB slab block (along X: two parts, along Y: two parts), HDB point block, condominium,
//            shophouses, office block, glass office tower, shopping mall (four parts), landmark tower,
//            mixed development, mosque, Chinese temple
//   273-285: hospital, university, stadium (four parts each: (0,0), (1,0), (0,1), (1,1)), hawker centre
//   286-289: icons: hospital, university, stadium, hawker centre
//   290-298: shops: supermarket, shopping complex (along X: two parts, along Y: two parts),
//            hypermarket (four parts: (0,0), (0,1), (1,0), (1,1))
//   299:   icon: map of states
//   300:   the Wira Bandar wordmark of the main menu
//   301-320: road markings on roads up a slope: all-day bus lane, peak bus lane, bus-only road, kerbed busway,
//            expressway; each on SLOPE_NE, SLOPE_SE, SLOPE_SW, SLOPE_NW
//
   -1 * 0\t 0C "City fork graphics"
   -1 * 5\t 05 1C FF %02X %02X
""" % (len(sprites) & 0xFF, len(sprites) >> 8)
with open(f"{SRC}/city.nfo", "w") as f:
    f.write(header)
    for (x, y, w, h, xr, yr, name) in sprites:
        f.write(f"   -1 sprites/city.png 8bpp {x:4d} {y:4d} {w:3d} {h:3d} {xr:4d} {yr:4d} normal\n")
print(len(sprites), "sprites written")
