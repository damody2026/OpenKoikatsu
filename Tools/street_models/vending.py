"""Vending-machine banks: a row of three machines with a 'vending' anchor in front of the middle one.

The bank is placed on the first free spot near a search origin (usually the door): nothing of the model may stand
where the machines or the two people buying would be. The person at the anchor faces the machines; the companion
stands one machine to the side, in front of the next machine.
"""
import math
from mathutils import Vector

DIRS = {0: ((1, 0), (0, 1)), 90: ((0, 1), (-1, 0)), 180: ((-1, 0), (0, -1)), 270: ((0, -1), (1, 0))}
BODY = ('red', 'white', 'sky', 'yellow')
DRINKS = ('orange', 'green', 'yellow', 'pink', 'sky', 'red', 'white', 'purple')


def _footprint(x, y, front, n, pitch):
    """Ground box (lo, hi) covering the machines and the spot in front of them; front = machine front direction."""
    (fx, fy), (sx, sy) = DIRS[front]
    half = n * pitch / 2 + 0.1
    pts = []
    for depth in (-1.25, 0.55):           # from behind the machines to just behind the buyers
        for side in (-half, half):
            pts.append((x + fx * depth + sx * side, y + fy * depth + sy * side))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return (min(xs), min(ys)), (max(xs), max(ys))


def _blocked(m, lo, hi):
    for ob in m.objects:
        corners = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
        zl, zh = min(c.z for c in corners), max(c.z for c in corners)
        if zh - zl < 0.08 or zh < 0.08 or zl > 1.95:
            continue  # floors, rugs, markings, things overhead
        xl, xh = min(c.x for c in corners), max(c.x for c in corners)
        yl, yh = min(c.y for c in corners), max(c.y for c in corners)
        if xl < hi[0] and xh > lo[0] and yl < hi[1] and yh > lo[1]:
            return True
    for centre, size in m.interiors:
        c, s = Vector(centre), Vector(size)
        if c.x - s.x / 2 < hi[0] and c.x + s.x / 2 > lo[0] and c.y - s.y / 2 < hi[1] and c.y + s.y / 2 > lo[1]:
            return True
    return False


def machine(m, x, y, front, colour, seed):
    """One machine whose front faces `front` (degrees, 0 = +X), centred at (x, y) on the ground."""
    (fx, fy), (sx, sy) = DIRS[front]
    D, W, H = 0.72, 0.95, 1.85

    def at(depth, side, z):
        return (x + fx * depth + sx * side, y + fy * depth + sy * side, z)

    def size(d, w, h):
        return (abs(fx) * d + abs(sx) * w, abs(fy) * d + abs(sy) * w, h)

    m.box(colour, at(0, 0, H / 2), size(D, W, H), bevel=0.02, solid=True)
    m.box('white', at(D / 2 + 0.005, 0, H - 0.12), size(0.02, W - 0.08, 0.16))              # lit header
    m.box('glass_dark', at(D / 2 + 0.01, -0.08, 1.22), size(0.02, W - 0.3, 0.72))           # display window
    for row in range(3):                                                                     # sample cans
        for k in range(5):
            m.cyl(DRINKS[(seed + row * 5 + k) % len(DRINKS)], at(D / 2 + 0.03, -0.36 + k * 0.14, 0.95 + row * 0.24),
                  0.045, 0.14, 8)
    for k in range(5):                                                                       # buttons
        m.box('lamp', at(D / 2 + 0.02, -0.36 + k * 0.14, 0.86), size(0.02, 0.06, 0.03))
    m.box('metal', at(D / 2 + 0.02, W / 2 - 0.13, 1.05), size(0.03, 0.14, 0.42))            # coin / card panel
    m.box('black', at(D / 2 + 0.01, -0.05, 0.28), size(0.03, W - 0.35, 0.2))               # take-out slot


def bank(m, origin, front=0, n=3, pitch=1.1, seed=0, search=None):
    """Place n machines facing `front` near `origin`; adds anchor 'vending'. Returns the anchor or None."""
    (fx, fy), (sx, sy) = DIRS[front]
    offsets = search or [(d, s) for d in (0.0, -0.8, 0.8, -1.6, 1.6) for s in (3.6, -3.6, 4.6, -4.6, 5.6, -5.6, 6.6, -6.6, 7.8, -7.8, 9.0, -9.0)]
    for dd, ss in offsets:
        # (x, y) = middle machine; origin is where the buyers would stand
        bx = origin[0] + fx * (dd - 0.95) + sx * ss
        by = origin[1] + fy * (dd - 0.95) + sy * ss
        lo, hi = _footprint(bx, by, front, n, pitch)
        if _blocked(m, lo, hi):
            continue
        for i in range(n):
            side = (i - (n - 1) / 2) * pitch
            machine(m, bx + sx * side, by + sy * side, front, BODY[(seed + i) % len(BODY)], seed + i * 3)
        # The buyer stands in front of the middle machine facing it; the companion goes one machine to the side.
        yaw = (front + 180) % 360
        m.anchors['vending'] = ((round(bx + fx * 0.95, 2), round(by + fy * 0.95, 2)), yaw)
        print(f'VENDING {m.id}: anchor {m.anchors["vending"]}')
        return m.anchors['vending']
    print(f'VENDING {m.id}: no free spot near {origin}')
    return None
