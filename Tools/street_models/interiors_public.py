"""Public restrooms (separate men's and women's rooms) and the aquarium for build_street_models.py.

The restroom is two rooms side by side, each with its own doorway on the front (+X): women's at -Y, men's at +Y
(Blender local; the game mirrors Y, which does not matter for a symmetric front). Anchors: 'women' and 'men'
inside each room, 'wait' outside between the doors. The aquarium bench is modelled where the seated pair layout
puts its two seats, so that activity spawns no props of its own.
"""
import math

from mathutils import Matrix, Vector

from interiors import T, pendant, plant, counter, sign_on


def _person(m, x, y, z, color, skirt):
    """Pictogram on a +X wall: head, body (a triangle-ish skirt for women), legs."""
    m.cyl(color, (x, y, z + 0.34), 0.07, 0.02, 14, axis='X')
    if skirt:
        m.box(color, (x, y, z + 0.12), (0.02, 0.2, 0.22))
        m.box(color, (x, y, z), (0.02, 0.26, 0.06))
    else:
        m.box(color, (x, y, z + 0.12), (0.02, 0.16, 0.24))
    for s in (-1, 1):
        m.box(color, (x, y + s * 0.04, z - 0.12), (0.02, 0.05, 0.2))


def _toilet(m, x, y, facing):
    """Western toilet against a wall; `facing` = +1 faces +X, -1 faces -X."""
    m.box('white', (x, y, 0.2), (0.45, 0.38, 0.4))
    m.box('white', (x - facing * 0.25, y, 0.5), (0.18, 0.4, 0.45))
    m.box('white', (x + facing * 0.02, y, 0.42), (0.4, 0.36, 0.04))


def _stall(m, x0, x1, y0, y1, door_side_x, open_door=False):
    """Partition walls and a door for one stall spanning [x0, x1] x [y0, y1]; door on x = door_side_x."""
    for y in (y0, y1):
        m.box('sky', ((x0 + x1) / 2, y, 1.05), (x1 - x0, 0.04, 1.9))
    if not open_door:
        m.box('sky', (door_side_x, (y0 + y1) / 2, 1.05), (0.04, (y1 - y0) - 0.1, 1.9))
        m.box('metal', (door_side_x + 0.03, (y0 + y1) / 2 + 0.25, 1.0), (0.03, 0.06, 0.1))


def _sinks(m, x, y0, y1, facing=1):
    n = max(1, int((y1 - y0) / 0.75))
    m.box('white', (x, (y0 + y1) / 2, 0.85), (0.5, y1 - y0, 0.08))
    m.box('wood_dark', (x, (y0 + y1) / 2, 0.42), (0.45, y1 - y0, 0.78))
    for i in range(n):
        y = y0 + (y1 - y0) * (i + 0.5) / n
        m.cyl('white', (x + facing * 0.02, y, 0.88), 0.17, 0.04, 14)
        m.cyl('metal', (x - facing * 0.15, y, 1.0), 0.02, 0.22, 6)
    m.box('glass', (x - facing * 0.24, (y0 + y1) / 2, 1.5), (0.02, y1 - y0 - 0.1, 0.8))


def restroom(Model, ident, title):
    m = Model(ident)
    D, W, H = 5.0, 7.0, 3.0
    hx, hy = D / 2, W / 2
    door_w = 1.0
    m.box('tile', (0, 0, 0.03), (D - 0.05, W - 0.05, 0.06))
    # outer walls (back, sides), the dividing wall, and the front split by two doorways at y = -1.75 and +1.75
    m.box('cream', (-hx + T / 2, 0, H / 2), (T, W, H), solid=True)
    for s in (-1, 1):
        m.box('cream', (0, s * (hy - T / 2), H / 2), (D, T, H), solid=True)
    m.box('cream', (0, 0, H / 2), (D, T, H), solid=True)
    doors = (-1.75, 1.75)
    edges = [-hy, doors[0] - door_w / 2, doors[0] + door_w / 2, doors[1] - door_w / 2, doors[1] + door_w / 2, hy]
    for a, b in ((edges[0], edges[1]), (edges[2], edges[3]), (edges[4], edges[5])):
        m.box('cream', (hx - T / 2, (a + b) / 2, H / 2), (T, b - a, H), solid=True)
    for y in doors:
        m.box('cream', (hx - T / 2, y, (H + 2.2) / 2), (T, door_w, H - 2.2))
        for s in (-1, 1):
            m.box('white', (hx + 0.04, y + s * (door_w / 2 + 0.05), 1.1), (0.1, 0.1, 2.2))
        m.box('white', (hx + 0.04, y, 2.25), (0.1, door_w + 0.2, 0.1))
    # inner tile finish, a band outside, the roof (hidden inside)
    for s in (-1, 1):
        m.box('tile', (0, s * (hy - T - 0.01), 1.2), (D - 2 * T, 0.02, 2.4))
        m.box('tile', (0, s * (T / 2 + 0.01), 1.2), (D - 2 * T, 0.02, 2.4))
    m.box('tile', (-hx + T + 0.01, 0, 1.2), (0.02, W - 2 * T, 2.4))
    m.roof = True
    m.box('white', (0, 0, H - 0.05), (D - 2 * T, W - 2 * T, 0.1))
    m.box('roof_green', (0, 0, H + 0.12), (D + 0.5, W + 0.5, 0.24))
    m.roof = False
    # signs: red women's at -Y, blue men's at +Y, and the name board in the middle
    for y, color, skirt, word in ((doors[0], 'red', True, '女'), (doors[1], 'navy', False, '男')):
        m.box('white', (hx + 0.06, y, 2.65), (0.06, 1.0, 0.5))
        m.text(word, color, (hx + 0.1, y + 0.18, 2.65), 0.36)
        _person(m, hx + 0.1, y - 0.22, 2.6, color, skirt)
    m.box('wood_dark', (hx + 0.06, 0, 2.6), (0.06, 1.4, 0.45))
    m.text(title, 'white', (hx + 0.1, 0, 2.6), 0.26)
    # women's room (y < 0): three stalls along the back, sinks by the door
    for i in range(3):
        y0 = -hy + T + i * 0.95
        _stall(m, -hx + T, -hx + T + 1.4, y0, y0 + 0.95, -hx + T + 1.4)
        _toilet(m, -hx + T + 0.4, y0 + 0.47, 1)
    _sinks(m, 0.9, -hy + T + 0.2, -hy + T + 1.9, -1)
    m.box('rose', (-0.2, -T / 2 - 0.02, 1.5), (1.0, 0.02, 0.6))  # hand-dryer panel
    # men's room (y > 0): urinals on the divider, one stall at the back, sinks by the door
    for i in range(3):
        y = T / 2 + 0.05
        x = -1.2 + i * 0.75
        m.box('white', (x, y + 0.18, 0.75), (0.4, 0.3, 0.6))
        m.box('sky', (x + 0.38, y + 0.3, 1.0), (0.02, 0.5, 0.9))
    _stall(m, -hx + T, -hx + T + 1.4, hy - T - 1.1, hy - T, -hx + T + 1.4)
    _toilet(m, -hx + T + 0.4, hy - T - 0.55, 1)
    _sinks(m, 0.9, hy - T - 1.9, hy - T - 0.2, -1)
    for y in (-1.75, 1.75):
        pendant(m, -0.3, y, H, 0.3)
    plant(m, hx + 0.6, 0)
    m.interiors.append(((0, -hy / 2, H / 2), (D - 2 * T, hy - 1.5 * T, H)))
    m.interiors.append(((0, hy / 2, H / 2), (D - 2 * T, hy - 1.5 * T, H)))
    m.door_at = Vector((hx + 2.2, 0, 0))
    m.anchors['women'] = ((-0.2, -1.6), 0)
    m.anchors['men'] = ((-0.2, 1.6), 0)
    m.anchors['wait'] = ((hx + 1.2, 0.0), 180)
    return m


def _bench_for_seated(m, ax, ay, yaw_deg):
    """Two benches exactly where the seated pair layout at anchor (ax, ay, yaw) puts its seats (the game's
    seat offsets are -0.12 m and +1.67 m toward the layout's side, which in Blender is yaw - 90 degrees)."""
    side = math.radians(yaw_deg - 90)
    face = math.radians(yaw_deg)
    sx, sy = math.cos(side), math.sin(side)
    for off in (-0.12, 1.67):
        cx, cy = ax + sx * off, ay + sy * off
        seat = m.box('wood', (0, 0, 0), (0.56, 0.48, 0.07))
        seat.matrix_world = Matrix.Translation((cx, cy, 0.40)) @ Matrix.Rotation(face, 4, 'Z')
        for dx in (-0.22, 0.22):
            for dy in (-0.18, 0.18):
                leg = m.box('black', (0, 0, 0), (0.06, 0.06, 0.38))
                leg.matrix_world = Matrix.Translation((cx, cy, 0.19)) @ Matrix.Rotation(face, 4, 'Z') @ Matrix.Translation((dx, dy, 0))


def aquarium(Model):
    m = Model('aquarium')
    D, W, H = 14.0, 20.0, 6.0
    hx, hy = D / 2, W / 2
    door_w = 3.0
    m.box('navy', (0, 0, 0.03), (D - 0.05, W - 0.05, 0.06))
    m.box('cloth_indigo', (-hx + T / 2, 0, H / 2), (T, W, H), solid=True)
    for s in (-1, 1):
        m.box('sky', (0, s * (hy - T / 2), H / 2), (D, T, H), solid=True)
    seg = (W - door_w) / 2
    for s in (-1, 1):
        m.box('glass_dark', (hx - T / 2, s * (door_w / 2 + seg / 2), H / 2), (T, seg, H), solid=True)
    m.box('sky', (hx - T / 2, 0, (H + 3.0) / 2), (T, door_w, H - 3.0))
    # facade: a wave band, the name, a big fish sign
    m.box('white', (hx + 0.05, 0, H - 0.6), (0.1, W, 0.5))
    for i in range(12):
        m.sphere('sky', (hx + 0.12, -hy + 0.8 + i * (W - 1.6) / 11, H - 0.85), 0.35, (0.3, 1, 0.5), 10)
    m.box('navy', (hx + 0.1, 0, 4.0), (0.08, 5.0, 1.0))
    m.text('海洋水族館', 'white', (hx + 0.16, 0, 4.0), 0.6)
    m.sphere('orange', (hx + 0.4, -4.5, 4.2), 0.6, (0.3, 1.6, 0.8), 14)
    m.box('orange', (hx + 0.4, -5.6, 4.2), (0.1, 0.5, 0.7))
    m.roof = True
    m.box('navy', (0, 0, H - 0.05), (D - 2 * T, W - 2 * T, 0.1))
    m.cyl('sky', (0, 0, H + 0.5), 4.0, 1.0, 32, 3.0)
    m.box('white', (0, 0, H + 0.1), (D + 0.4, W + 0.4, 0.2))
    m.roof = False
    m.interiors.append(((0, 0, H / 2), (D - 2 * T, W - 2 * T, H)))

    def tank(x0, y0, y1, z0, z1, depth, fish=14, seed=0):
        """A wall tank facing +X: dark-blue water with sand, rocks, coral and fish in front of it, a frame."""
        cy = (y0 + y1) / 2
        m.box('water', (x0 - depth / 2, cy, (z0 + z1) / 2), (depth, y1 - y0, z1 - z0))
        m.box('sand', (x0 + 0.02, cy, z0 + 0.12), (0.06, y1 - y0, 0.24))
        for k in range(max(2, int((y1 - y0) / 1.5))):
            y = y0 + 0.5 + k * (y1 - y0 - 1.0) / max(1, int((y1 - y0) / 1.5) - 1)
            m.sphere('rock', (x0 + 0.08, y, z0 + 0.3), 0.35, (0.3, 1.2, 0.8), 8)
            m.cyl(['rose', 'orange', 'purple', 'red'][(k + seed) % 4], (x0 + 0.1, y + 0.4, z0 + 0.55), 0.06, 0.6, 6)
            m.sphere('green', (x0 + 0.1, y - 0.4, z0 + 0.5), 0.18, (0.4, 0.6, 2.0), 8)
        cols = ['orange', 'yellow', 'red', 'white', 'sky', 'pink', 'purple', 'black']
        for f in range(fish):
            a = (f * 7919 + seed * 104729) % 1000 / 1000.0
            b = (f * 6007 + seed * 7727) % 1000 / 1000.0
            y = y0 + 0.3 + a * (y1 - y0 - 0.6)
            z = z0 + 0.8 + b * (z1 - z0 - 1.2)
            size = 0.08 + ((f * 31) % 5) * 0.03
            m.sphere(cols[f % len(cols)], (x0 + 0.12, y, z), size, (0.35, 1.6, 0.8), 8)
            m.box(cols[f % len(cols)], (x0 + 0.12, y + size * 1.8, z), (0.02, size * 0.8, size * 1.2))
        for z in (z0, z1):
            m.box('black', (x0 + 0.05, cy, z), (0.12, y1 - y0 + 0.2, 0.14))
        for y in (y0, y1):
            m.box('black', (x0 + 0.05, y, (z0 + z1) / 2), (0.12, 0.14, z1 - z0))

    # the big tank across the back, two side tanks, a jellyfish column
    tank(-hx + T + 0.5, -hy + 2.0, hy - 2.0, 0.4, 5.2, 0.5, 34, 1)
    m.boxes.append((Vector((-hx + T + 0.3, 0, 2.8)), Vector((0.6, W - 4.0, 5.6))))
    for s in (-1, 1):
        y = s * (hy - T - 0.3)
        # side tanks face the room (built facing +X then turned by the side they are on)
        m.box('water', (-1.0, y, 1.7), (5.0, 0.5, 2.2))
        m.box('black', (-1.0, y - s * 0.27, 1.7), (5.1, 0.06, 2.3))
        for f in range(10):
            m.sphere(['yellow', 'orange', 'sky', 'white', 'red'][f % 5], (-3.2 + f * 0.48, y - s * 0.32, 1.0 + (f * 37 % 10) / 10.0 * 1.4), 0.1, (1.6, 0.35, 0.8), 8)
        m.boxes.append((Vector((-1.0, y, 1.7)), Vector((5.0, 0.6, 2.2))))
    m.cyl('water', (2.4, -6.0, 1.8), 0.9, 3.4, 24)
    m.cyl('black', (2.4, -6.0, 0.05), 1.0, 0.1, 24)
    m.cyl('black', (2.4, -6.0, 3.55), 1.0, 0.1, 24)
    for j in range(7):
        z = 0.6 + j * 0.42
        jx, jy = 2.4 + math.cos(j * 2.1) * 0.45, -6.0 + math.sin(j * 2.1) * 0.45
        m.sphere('pink' if j % 2 else 'white', (jx, jy, z + 0.2), 0.16, (1, 1, 0.6), 10)
        m.cyl('pink', (jx, jy, z), 0.02, 0.3, 4)
    m.boxes.append((Vector((2.4, -6.0, 1.8)), Vector((2.0, 2.0, 3.6))))
    # ticket counter by the door and a little gift shelf
    counter(m, hx - 1.6, 6.5, 2, 2.4, 'white', 'navy')
    m.box('wood', (hx - 1.0, -7.8, 0.8), (0.5, 1.8, 1.6), solid=True)
    for i in range(6):
        m.sphere(['sky', 'pink', 'yellow'][i % 3], (hx - 0.72, -8.5 + i * 0.28, 1.2 + (i % 2) * 0.35), 0.1, (0.6, 1.2, 0.8), 8)
    for (x, y) in ((-1.0, -3.0), (-1.0, 3.0), (3.0, 0.0)):
        m.sphere('lamp', (x, y, H - 0.3), 0.15)
    plant(m, hx - 0.6, -hy + 0.8)
    plant(m, hx - 0.6, hy - 0.8)
    # benches facing the big tank, laid out for the seated pair at the 'bench' anchor
    _bench_for_seated(m, -3.2, 0.9, 180)
    m.door_at = Vector((hx + 2.4, 0, 0))
    m.anchors['bench'] = ((-3.2, 0.9), 180)
    m.anchors['tank'] = ((-2.8, -3.4), 180)
    return m
