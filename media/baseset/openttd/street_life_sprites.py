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
    if kind in BUSES:
        return bus_boxes(kind)
    raise ValueError(kind)


# City fork buses (built-in road vehicles); the order is the sprite order in city.png.
BUSES = ['minibus', 'midibus', 'bus12', 'decker', 'artic_front', 'artic_rear',
         'e_minibus', 'e_midibus', 'e_bus12', 'e_decker', 'e_artic_front', 'e_artic_rear',
         'brt_front', 'brt_rear']


def bus_boxes(kind):
    """A bus (or one section of an articulated bus): skirt in company colour, a band of windows
    with pillars, a roof in the second company colour, air-con or battery pods on top, wheels.
    Heights are stretched like the original vehicle sprites, which are drawn taller than they are."""
    electric = kind.startswith('e_') or kind.startswith('brt')
    base = kind[2:] if kind.startswith('e_') else kind
    brt = kind.startswith('brt')
    section = 'front' if base.endswith('front') else ('rear' if base.endswith('rear') else None)
    # Length (a tile is 16; a full-length road vehicle is 8), half width.
    length, half_w = {'minibus': (5.0, 1.05), 'midibus': (6.0, 1.1)}.get(base, (6.0, 1.15) if section else (8.0, 1.15))
    la = length / 2
    skirt, roof = ('roof', 'body') if brt else ('body', 'roof')
    boxes = []

    def wheels(a):
        boxes.extend([(a - 0.55, a + 0.55, -half_w + 0.05, -half_w + 0.4, 0.0, 0.9, 'wheel'),
                      (a - 0.55, a + 0.55, half_w - 0.4, half_w - 0.05, 0.0, 0.9, 'wheel')])

    def window_band(a0, a1, z0, z1, front_glass):
        """Panes with pillars between them; the front and back pillars are left to the body."""
        pane, pillar = 1.25, 0.3
        n = max(1, round((a1 - a0 - pillar) / (pane + pillar)))
        step = (a1 - a0) / n
        for i in range(n):
            p0 = a0 + i * step
            boxes.append((p0, p0 + pillar, -half_w, half_w, z0, z1, 'pillar'))
            boxes.append((p0 + pillar, p0 + step, -half_w, half_w, z0, z1, 'windows' if front_glass or i < n - 1 else 'windows_back'))

    a0, a1 = -la, la
    if section == 'front':
        a0 += 0.35  # room for the joint
        boxes.append((-la, a0, -half_w + 0.15, half_w - 0.15, 0.9, 4.4, 'bellows'))
        wheels(la - 1.2)
        wheels(-la + 1.4)
    elif section == 'rear':
        a1 -= 0.35
        boxes.append((a1, la, -half_w + 0.15, half_w - 0.15, 0.9, 4.4, 'bellows'))
        wheels(-la + 1.5)
    elif base == 'minibus':
        wheels(la - 1.0)
        wheels(-la + 1.0)
    else:
        wheels(la - 1.5)
        wheels(-la + 1.7)

    boxes.append((a0 + 0.3, a1 - 0.3, -half_w + 0.45, half_w - 0.45, 0.3, 0.9, 'bellows'))  # underbody
    if base == 'decker':
        boxes.append((a0, a1, -half_w, half_w, 0.9, 2.4, skirt))
        window_band(a0, a1, 2.4, 4.1, True)
        boxes.append((a0, a1, -half_w, half_w, 4.1, 4.7, skirt))
        window_band(a0, a1, 4.7, 6.5, True)
        boxes.append((a0, a1, -half_w, half_w, 6.5, 7.1, roof))
        top = 7.1
    else:
        glass_bottom, glass_top = {'minibus': (2.1, 3.9), 'midibus': (2.4, 4.4)}.get(base, (2.0, 4.9) if brt else (2.5, 4.7))
        boxes.append((a0, a1, -half_w, half_w, 0.9, glass_bottom, skirt))
        window_band(a0, a1, glass_bottom, glass_top, section != 'rear')
        boxes.append((a0, a1, -half_w, half_w, glass_top, glass_top + 0.6, roof))
        top = glass_top + 0.6

    # On the roof: air-con for diesel buses, battery packs (and air-con) for electric ones.
    if electric and base == 'decker':
        boxes.append((a0 + 0.3, a0 + 2.0, -half_w + 0.25, half_w - 0.25, top, top + 0.7, 'pod'))
    elif electric:
        pods_from, pods_to = (a0 + 0.4, a1 - 1.6) if section != 'front' else (a0 + 0.2, a1 - 1.8)
        if base == 'minibus':
            pods_from, pods_to = a0 + 0.4, a1 - 1.4
        boxes.append((pods_from, pods_to, -half_w + 0.2, -0.1, top, top + 0.75, 'pod'))
        boxes.append((pods_from, pods_to, 0.1, half_w - 0.2, top, top + 0.75, 'pod'))
        if section != 'rear':
            boxes.append((a1 - 1.4, a1 - 0.5, -half_w + 0.3, half_w - 0.3, top, top + 0.5, 'pod'))
    elif base != 'decker':
        ac_from = a1 - 2.2 if section != 'rear' else a0 + 0.8
        boxes.append((ac_from, ac_from + 1.3, -half_w + 0.3, half_w - 0.3, top, top + 0.6, 'pod'))
    return boxes


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
    elif role == 'windows':
        base = 'bus_glass' if face in ('left', 'right', 'front', 'back') else 'cc'
    elif role == 'windows_back':
        base = 'bus_glass' if face in ('left', 'right') else 'cc'
    elif role == 'pillar':
        base = 'cc2'
    elif role == 'roof':
        base = 'cc2'
    elif role == 'pod':
        base = 'pod'
    elif role == 'bellows':
        base = 'dark'
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


def render_sorted(kind, d):
    """Like render, but orders whole boxes back to front (for the many stacked boxes of a bus):
    box A is behind box B when a plane between them faces the viewer, who looks from (1, 1, 2)."""
    fx, fy = DIRS[d]
    fl = math.hypot(fx, fy)
    fx, fy = fx / fl, fy / fl
    rx, ry = -fy, fx
    view_a, view_b = fx + fy, rx + ry  # The viewer's direction in the bus's own axes; z is 2.
    eps = 1e-6
    boxes = boxes_for(kind)

    def corner(a, b, z):
        return (a * fx + b * rx, a * fy + b * ry, z)

    def screen_bounds(box):
        a0, a1, b0, b1, z0, z1 = box[:6]
        pts = [project(*corner(a, b, z)) for a in (a0, a1) for b in (b0, b1) for z in (z0, z1)]
        return (min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts))

    def behind(p, q):
        """Is box p behind box q?"""
        if view_a > eps and p[1] <= q[0] + eps: return True
        if view_a < -eps and p[0] >= q[1] - eps: return True
        if view_b > eps and p[3] <= q[2] + eps: return True
        if view_b < -eps and p[2] >= q[3] - eps: return True
        return p[5] <= q[4] + eps

    bounds = [screen_bounds(b) for b in boxes]
    n = len(boxes)
    after = [set() for _ in range(n)]  # after[i]: boxes drawn after box i
    indegree = [0] * n
    for i in range(n):
        for j in range(n):
            if i == j: continue
            bi, bj = bounds[i], bounds[j]
            if bi[1] <= bj[0] or bj[1] <= bi[0] or bi[3] <= bj[2] or bj[3] <= bi[2]: continue  # no overlap on screen
            if behind(boxes[i], boxes[j]) and not behind(boxes[j], boxes[i]) and j not in after[i]:
                after[i].add(j)
                indegree[j] += 1
    order = []
    ready = sorted([i for i in range(n) if indegree[i] == 0], key=lambda i: boxes[i][4])
    while ready:
        i = ready.pop(0)
        order.append(i)
        for j in sorted(after[i]):
            indegree[j] -= 1
            if indegree[j] == 0: ready.append(j)
    assert len(order) == n, (kind, d, 'cycle in box order')

    faces = []
    for i in order:
        a0, a1, b0, b1, z0, z1, role = boxes[i][:7]
        sides = [((fx, fy), [(a1, b0), (a1, b1)], 'front'),
                 ((-fx, -fy), [(a0, b1), (a0, b0)], 'back'),
                 ((rx, ry), [(a1, b1), (a0, b1)], 'right'),
                 ((-rx, -ry), [(a0, b0), (a1, b0)], 'left')]
        for (nx, ny), ((pa, pb), (qa, qb)), name in sides:
            if nx + ny <= 1e-6:
                continue  # faces away from the viewer
            pts = [corner(pa, pb, z0), corner(qa, qb, z0), corner(qa, qb, z1), corner(pa, pb, z1)]
            light = 0.78 + 0.22 * (nx - ny) / max(1e-6, abs(nx) + abs(ny))
            faces.append((0, pts, role, name, light))
        top = [corner(a0, b0, z1), corner(a1, b0, z1), corner(a1, b1, z1), corner(a0, b1, z1)]
        faces.append((0, top, role, 'top', 1.12))
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
