"""Walk-in SPA hotel lobby and fruit shop for build_street_models.py."""
import math

from mathutils import Vector

from interiors import T, shell, sign_on, sofa, plant, fridge, counter


def walkin_hotel(Model):
    """SPA hotel: the room tower stays closed; the glass lobby in front of it is walk-in (reception, lobby bar,
    sofas, a chandelier). Pool deck, loungers and palms on the front right as before."""
    m = Model('hotel')
    depth, width, tower_h = 8.0, 13.0, 19.0
    tc = -1.0
    tfront = tc + depth / 2
    m.box('white', (tc, 0, tower_h / 2), (depth, width, tower_h), solid=True)
    m.box('stone_dark', (tc, 0, 0.25), (depth + 0.1, width + 0.1, 0.5))
    for f in range(2, 6):
        z = f * 3.2
        m.floor_band(tc, 0, depth, width, z, 'navy', 0.14)
        for i in range(6):
            y = -width / 2 + width * (i + 0.5) / 6
            m.box('glass_dark', (tfront + 0.03, y, z + 1.55), (0.05, 1.9, 2.4))
            m.box('white', (tfront + 0.55, y, z + 0.05), (1.1, 2.0, 0.1))
            m.railing(tfront + 1.08, y - 1.0, tfront + 1.08, y + 1.0, z + 0.1, 0.95, 'glass', 4)
    for s in (-1, 1):
        for f in range(6):
            for i in range(3):
                m.side_window(tc - depth / 2 + depth * (i + 0.5) / 3, s * width / 2, 1.8 + f * 3.2, 1.1, 1.2, 'white', 'glass_dark', s)
    m.box('navy', (tc, 0, tower_h + 0.4), (depth + 0.4, width + 0.4, 0.8))
    m.text('SPA HOTEL', 'yellow', (tfront + 0.12, 0, tower_h - 1.2), 1.1)
    # glass lobby: x tfront..tfront+5, y -6..2, the door in the middle of its front
    lx0, lx1, ly0, ly1, H = tfront, tfront + 5.0, -6.0, 2.0, 4.4
    cx, cy = (lx0 + lx1) / 2, (ly0 + ly1) / 2
    door_w = 2.4
    m.box('tile', (cx, cy, 0.03), (lx1 - lx0, ly1 - ly0, 0.06))
    for y in (ly0, ly1):
        m.box('white', (cx, y, H / 2), (lx1 - lx0, 0.2, H), solid=True)
    for (y0, y1) in ((ly0, cy - door_w / 2), (cy + door_w / 2, ly1)):
        m.box('glass_dark', (lx1 - 0.05, (y0 + y1) / 2, H / 2), (0.08, y1 - y0, H), solid=True)
        n = max(1, int((y1 - y0) / 1.2))
        for k in range(n + 1):
            m.box('white', (lx1, y0 + k * (y1 - y0) / n, H / 2), (0.12, 0.08, H))
    m.box('white', (lx1 - 0.05, cy, (H + 2.8) / 2), (0.12, door_w, H - 2.8))
    m.roof = True
    m.box('white', (cx, cy, H + 0.15), (lx1 - lx0 + 0.3, ly1 - ly0 + 0.3, 0.3))
    m.box('white', (cx, cy, H - 0.05), (lx1 - lx0 - 0.4, ly1 - ly0 - 0.4, 0.1))
    m.roof = False
    m.interiors.append(((cx, cy, H / 2), (lx1 - lx0 - 0.3, ly1 - ly0 - 0.3, H)))
    # reception against the tower, the lobby bar along the north wall, sofas, plants, a chandelier
    m.box('wood_dark', (lx0 + 0.5, cy - 1.4, 0.55), (0.7, 2.4, 1.1), solid=True)
    m.box('wood_light', (lx0 + 0.52, cy - 1.4, 1.12), (0.8, 2.5, 0.06))
    m.box('navy', (lx0 + 0.12, cy - 1.4, 2.4), (0.04, 2.6, 0.8))
    m.text('RECEPTION', 'yellow', (lx0 + 0.16, cy - 1.4, 2.4), 0.25)
    m.box('wood_dark', (cx - 0.3, ly1 - 0.6, 0.55), (2.6, 0.7, 1.1), solid=True)
    m.box('black', (cx - 0.3, ly1 - 0.58, 1.12), (2.7, 0.8, 0.06))
    m.box('wood', (cx - 0.3, ly1 - 0.15, 1.6), (2.6, 0.25, 1.4))
    for i in range(8):
        m.cyl(['red', 'yellow', 'green', 'sky', 'purple', 'orange', 'white', 'rose'][i], (cx - 1.4 + i * 0.32, ly1 - 0.15, 1.95), 0.05, 0.32, 8)
    for i in range(3):
        m.cyl('black', (cx - 1.2 + i * 0.9, ly1 - 1.3, 0.4), 0.18, 0.06, 12)
        m.cyl('metal', (cx - 1.2 + i * 0.9, ly1 - 1.3, 0.2), 0.03, 0.4, 6)
    sofa(m, lx1 - 1.2, ly0 + 0.8, 1, 2.2, 'navy')
    m.box('glass', (lx1 - 1.2, ly0 + 1.8, 0.4), (1.2, 0.8, 0.04))
    plant(m, lx1 - 0.5, ly0 + 0.5)
    plant(m, lx0 + 0.5, ly1 - 0.5)
    m.cyl('metal', (cx, cy, H - 0.4), 0.02, 0.6, 6)
    for k in range(8):
        a = k / 8 * math.tau
        m.sphere('lamp', (cx + math.cos(a) * 0.5, cy + math.sin(a) * 0.5, H - 0.8), 0.09)
    # porte-cochere and the pool deck
    m.box('white', (lx1 + 2.0, cy, 3.6), (4.0, 5.0, 0.35))
    for y in (cy - 2.2, cy + 2.2):
        m.cyl('white', (lx1 + 3.7, y, 1.8), 0.2, 3.6, 12)
    m.text('溫泉SPA飯店', 'navy', (lx1 + 4.02, cy, 3.6), 0.28)
    px, py = lx1 - 1.0, 4.4
    m.box('tile', (px, py, 0.05), (7.0, 4.6, 0.1))
    m.box('water', (px, py, 0.08), (5.6, 3.0, 0.06))
    for (x, y, sx, sy) in ((px, py - 1.55, 5.8, 0.12), (px, py + 1.55, 5.8, 0.12), (px - 2.85, py, 0.12, 3.2), (px + 2.85, py, 0.12, 3.2)):
        m.box('white', (x, y, 0.13), (sx, sy, 0.08))
    for i in range(3):
        x = px - 2.0 + i * 2.0
        m.box('white', (x, py + 2.05, 0.3), (1.6, 0.55, 0.08))
        m.box('cloth_blue', (x - 0.55, py + 2.05, 0.45), (0.5, 0.5, 0.06))
    for x in (px - 3.6, px + 3.6):
        m.palm(x, py + 2.3)
    m.door_at = Vector((lx1 + 2.6, cy, 0))
    m.anchors['pool'] = ((px, py + 0.8), 0)
    m.anchors['bar'] = ((cx - 0.3, ly1 - 1.9), 90)
    m.anchors['lobby'] = ((cx + 0.2, cy - 0.2), 0)
    return m


def walkin_fruitshop(Model):
    m = Model('fruitshop')
    D, W, H = 8.0, 9.0, 3.4
    front = shell(m, D, W, H, 'cream', 'cream', 'wood_light', door_w=3.4, front_windows=(), side_windows=2, frame='wood_dark')
    m.roof = True
    m.gable_roof('roof_green', (0, 0, H), D, W, 1.6, along_x=False, overhang=0.4)
    for s in (-1, 1):
        m.gable_end('cream', (0, s * W / 2, H), D, 1.6, along_x=False)
    m.roof = False
    m.awning(front, 0, 2.95, 8.8, 1.8, 0.5, 9, ('green', 'awning_b'))
    sign_on(m, front, '水果店', 3.1, 3.0, 'red', 'yellow')
    fruits = [('red', 0.13), ('orange', 0.14), ('yellow', 0.12), ('green', 0.13), ('purple', 0.08), ('red', 0.1)]

    def crates(x, y0, y1, z, shift=0):
        n = 3
        for c in range(n):
            y = y0 + (y1 - y0) * (c + 0.5) / n
            m.box('wood_light', (x, y, z + 0.1), (0.6, 0.9 if abs(y1 - y0) > 1 else 0.5, 0.2))
            col, r = fruits[(c + shift) % len(fruits)]
            for i in range(2):
                for j in range(2):
                    m.sphere(col, (x - 0.13 + j * 0.26, y - 0.12 + i * 0.24, z + 0.22 + r), r, (1, 1, 1), 8)

    # stands outside on both sides of the opening, fruit racks along the inside walls, a juice fridge and a till
    for s in (-1, 1):
        m.box('wood', (front + 0.9, s * 3.0, 0.4), (0.8, 2.4, 0.8), solid=True)
        crates(front + 0.9, s * 3.0 - 1.1, s * 3.0 + 1.1, 0.8, 1 if s > 0 else 0)
    for s in (-1, 1):
        y = s * (W / 2 - T - 0.45)
        m.box('wood', (-0.6, y, 0.45), (4.4, 0.8, 0.9), solid=True)
        for k in range(4):
            crates(-2.3 + k * 1.15, y - 0.3, y + 0.3, 0.9, k)
    fridge(m, -D / 2 + T + 0.36, 0, 0, 3.0, 1.9)
    counter(m, front - 1.2, 2.4, 2, 1.2, 'wood_light', 'wood_dark')
    for i in range(3):
        m.sphere('green', (front + 2.4, -3.8 + i * 0.6, 0.25), 0.27, (1.25, 1, 1), 10)
    m.lantern(front + 0.2, -4.2, 2.6)
    m.lantern(front + 0.2, 4.2, 2.6)
    m.door_at = Vector((front + 2.6, 0, 0))
    m.anchors['stand'] = ((0.6, 0.9), 0)
    return m
