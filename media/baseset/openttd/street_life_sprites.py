"""Render the street life vehicles (cars, vans, lorries, motorbikes) as 8bpp sprites.

Used by make_city_sprites.py. Each vehicle is a few boxes in world units (a
tile is 16 units), turned to one of OpenTTD's eight directions and projected
like the game does at normal zoom (screen x = (y - x) * 2, screen y = x + y - z),
drawn four times larger and reduced to the most common colour per pixel. The
centre of the vehicle on the ground is the sprite's origin.
"""
import math
from PIL import Image, ImageDraw

# Forward vector (world x, y) for OpenTTD's Direction N, NE, E, SE, S, SW, W, NW.
DIRS = [(-1, -1), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1), (0, -1)]
SS = 4  # supersampling


def project(x, y, z):
    return ((y - x) * 2.0, (x + y) - z)


def boxes_for(kind):
    """Boxes as (a0, a1, b0, b1, z0, z1, role): a along the heading (front positive), b to the right."""
    wheels = lambda a, b, r: [(a - r, a + r, -b, -b + 0.5, 0.0, 1.0, 'wheel'), (a - r, a + r, b - 0.5, b, 0.0, 1.0, 'wheel')]
    if kind == 'car':
        return wheels(-1.15, 0.95, 0.45) + wheels(1.15, 0.95, 0.45) + [
            (-1.85, 1.85, -0.95, 0.95, 0.5, 1.9, 'body'),
            (-1.05, 0.75, -0.85, 0.85, 1.9, 3.1, 'cabin')]
    if kind == 'van':
        return wheels(-1.3, 1.0, 0.45) + wheels(1.35, 1.0, 0.45) + [
            (-2.1, 2.1, -1.0, 1.0, 0.5, 2.0, 'body'),
            (-2.1, 1.0, -1.0, 1.0, 2.0, 4.0, 'box'),
            (1.0, 1.7, -0.95, 0.95, 2.0, 3.4, 'cabin')]
    if kind == 'lorry':
        return wheels(-1.8, 1.15, 0.5) + wheels(-0.8, 1.15, 0.5) + wheels(1.9, 1.15, 0.5) + [
            (-2.9, 2.7, -1.1, 1.1, 0.5, 1.6, 'chassis'),
            (-2.9, 0.9, -1.2, 1.2, 1.6, 4.6, 'cargo'),
            (1.1, 2.7, -1.1, 1.1, 1.6, 3.9, 'cabin_solid')]
    if kind == 'motorbike':
        return [(-1.3, -0.7, -0.25, 0.25, 0.0, 1.0, 'wheel'), (0.7, 1.3, -0.25, 0.25, 0.0, 1.0, 'wheel'),
                (-1.0, 1.0, -0.3, 0.3, 0.6, 1.6, 'body'),
                (-0.5, 0.3, -0.4, 0.4, 1.6, 3.6, 'rider')]
    raise ValueError(kind)


# Colours in RGB; 'cc*' are company-colour shades replaced by palette indices later.
SHADE = {'top': 1.15, 'left': 1.0, 'right': 0.7, 'front': 0.85}


def face_colour(role, face, n):
    """RGB colour or company-colour shade name for one face of one box."""
    if role in ('body', 'box'):
        base = 'cc'
    elif role == 'cabin':
        base = 'glass' if face != 'top' else 'cc'
    elif role == 'cabin_solid':
        base = 'glass' if face == 'front' else 'cc'
    elif role == 'wheel':
        base = 'wheel'
    elif role == 'cargo':
        base = 'cargo'
    elif role == 'chassis':
        base = 'dark'
    elif role == 'rider':
        base = 'rider'
    else:
        base = 'cc'
    return base


def render(kind, d):
    fx, fy = DIRS[d]
    fl = math.hypot(fx, fy)
    fx, fy = fx / fl, fy / fl
    rx, ry = -fy, fx  # right-hand perpendicular in world coords (x towards SW, y towards SE)
    faces = []
    for (a0, a1, b0, b1, z0, z1, role) in boxes_for(kind):
        def corner(a, b, z):
            return (a * fx + b * rx, a * fy + b * ry, z)
        # Side faces: outward normal and four corners.
        sides = [((fx, fy), [(a1, b0), (a1, b1)], 'front'),
                 ((-fx, -fy), [(a0, b1), (a0, b0)], 'back'),
                 ((rx, ry), [(a1, b1), (a0, b1)], 'right'),
                 ((-rx, -ry), [(a0, b0), (a1, b0)], 'left')]
        for (nx, ny), ((pa, pb), (qa, qb)), name in sides:
            if nx + ny <= 1e-6:
                continue  # faces away from the viewer
            pts = [corner(pa, pb, z0), corner(qa, qb, z0), corner(qa, qb, z1), corner(pa, pb, z1)]
            depth = sum(p[0] + p[1] for p in pts) / 4 + (z0 + z1) / 2 * 0.01
            # Light from the upper left of the screen: faces towards +x (screen left) are lit, +y darker.
            light = 0.78 + 0.22 * (nx - ny) / max(1e-6, abs(nx) + abs(ny))
            faces.append((depth, pts, role, name, light))
        top = [corner(a0, b0, z1), corner(a1, b0, z1), corner(a1, b1, z1), corner(a0, b1, z1)]
        faces.append((sum(p[0] + p[1] for p in top) / 4 + z1 * 0.5, top, role, 'top', 1.12))
    faces.sort(key=lambda f: f[0])
    return faces


def rasterise(faces, palette_map, shade_cc, rgb_index):
    """Draw faces at SS times the size, then take the most common colour per pixel."""
    pts2d = [project(*p) for f in faces for p in f[1]]
    minx = math.floor(min(p[0] for p in pts2d)) - 1
    miny = math.floor(min(p[1] for p in pts2d)) - 1
    maxx = math.ceil(max(p[0] for p in pts2d)) + 1
    maxy = math.ceil(max(p[1] for p in pts2d)) + 1
    w, h = maxx - minx, maxy - miny
    big = Image.new("I", (w * SS, h * SS), 0)
    dr = ImageDraw.Draw(big)
    for depth, pts, role, name, light in faces:
        poly = [((project(*p)[0] - minx) * SS, (project(*p)[1] - miny) * SS) for p in pts]
        colour = palette_map(role, name, light)
        dr.polygon(poly, fill=colour)
    small = Image.new("P", (w, h), 0)
    sp = small.load()
    bp = big.load()
    for y in range(h):
        for x in range(w):
            counts = {}
            for yy in range(SS):
                for xx in range(SS):
                    c = bp[x * SS + xx, y * SS + yy]
                    counts[c] = counts.get(c, 0) + 1
            best = max(counts.items(), key=lambda kv: (kv[1], kv[0] != 0))
            # Keep a pixel when at least 6 of 16 samples are covered.
            covered = SS * SS - counts.get(0, 0)
            if covered >= 6:
                nonzero = {k: v for k, v in counts.items() if k != 0}
                sp[x, y] = max(nonzero.items(), key=lambda kv: kv[1])[0]
    # The centre of the vehicle (world 0, 0, 0) is at (-minx, -miny) in the sprite.
    return small, -minx, -miny
