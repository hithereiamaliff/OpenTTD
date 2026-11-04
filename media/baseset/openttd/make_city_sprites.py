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

W, H = 512, 300
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
//
   -1 * 0\t 0C "City fork graphics"
   -1 * 3\t 05 1C %02X
""" % len(sprites)
with open(f"{SRC}/city.nfo", "w") as f:
    f.write(header)
    for (x, y, w, h, xr, yr, name) in sprites:
        f.write(f"   -1 sprites/city.png 8bpp {x:4d} {y:4d} {w:3d} {h:3d} {xr:4d} {yr:4d} normal\n")
print(len(sprites), "sprites written")
