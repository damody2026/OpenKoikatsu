"""Walk-in places for build_street_models.py: hollow shells with a doorway, furnished rooms and activity anchors.

Conventions (as the rest of the models): local metres, front (street side) toward +X, door gap centred on y = 0.
Activity anchors are where the pair stands when the activity starts; their yaw is the way the pair layout faces
(the game's pair layouts are authored for +X: seated/meal tables and drinks extend toward +Y of the anchor, the
girl reading stands 0.9 m to its left). Anchors need about 2 x 2.5 m of free floor toward +Y.
"""
import math
from mathutils import Matrix, Vector

T = 0.2  # wall thickness


class Put:
    """Places furniture parts around (x, y) turned by a quarter-turn count (0: front of the piece faces +X)."""

    def __init__(self, m, x, y, turns=0):
        self.m, self.x, self.y, self.k = m, x, y, turns % 4

    def _xy(self, lx, ly):
        for _ in range(self.k):
            lx, ly = -ly, lx
        return self.x + lx, self.y + ly

    def box(self, mat, c, s, solid=False, bevel=0.0):
        x, y = self._xy(c[0], c[1])
        sx, sy = (s[0], s[1]) if self.k % 2 == 0 else (s[1], s[0])
        return self.m.box(mat, (x, y, c[2]), (sx, sy, s[2]), bevel=bevel, solid=solid)

    def cyl(self, mat, c, r, h, seg=12, r2=None):
        x, y = self._xy(c[0], c[1])
        return self.m.cyl(mat, (x, y, c[2]), r, h, seg, r2)

    def sphere(self, mat, c, r, scale=(1, 1, 1), seg=10):
        x, y = self._xy(c[0], c[1])
        sx, sy = (scale[0], scale[1]) if self.k % 2 == 0 else (scale[1], scale[0])
        return self.m.sphere(mat, (x, y, c[2]), r, (sx, sy, scale[2]), seg)


def shell(m, D, W, H, outer, inner='plaster', floor='wood_light', door_w=1.9, door_h=2.5, base='stone_dark',
          front_windows=(), side_windows=0, window_h=1.4, window_z=1.6, frame='white', ceiling='white', roof=True):
    """Hollow building: floor, four walls (front split by the doorway), inner wall finish, ceiling (roof)."""
    hx, hy = D / 2, W / 2
    m.box(floor, (0, 0, 0.03), (D - 0.05, W - 0.05, 0.06))
    m.box(outer, (-hx + T / 2, 0, H / 2), (T, W, H), solid=True)
    for s in (-1, 1):
        m.box(outer, (0, s * (hy - T / 2), H / 2), (D, T, H), solid=True)
    seg = (W - door_w) / 2
    for s in (-1, 1):
        m.box(outer, (hx - T / 2, s * (door_w / 2 + seg / 2), H / 2), (T, seg, H), solid=True)
    m.box(outer, (hx - T / 2, 0, (H + door_h) / 2), (T, door_w, H - door_h))
    # inner finish a hair inside the walls
    m.box(inner, (-hx + T + 0.01, 0, H / 2), (0.02, W - 2 * T, H - 0.02))
    for s in (-1, 1):
        m.box(inner, (0, s * (hy - T - 0.01), H / 2), (D - 2 * T, 0.02, H - 0.02))
        m.box(inner, (hx - T - 0.01, s * (door_w / 2 + seg / 2), H / 2), (0.02, seg - 0.02, H - 0.02))
    m.box(inner, (hx - T - 0.01, 0, (H + door_h) / 2), (0.02, door_w, H - door_h))
    # skirting and cornice outside
    for s in (-1, 1):
        m.box(base, (0, s * (hy + 0.03), 0.25), (D + 0.06, 0.06, 0.5))
    m.box(base, (-hx - 0.03, 0, 0.25), (0.06, W + 0.06, 0.5))
    for s in (-1, 1):
        m.box(base, (hx + 0.03, s * (door_w / 2 + seg / 2), 0.25), (0.06, seg, 0.5))
    # doorway frame and step
    for s in (-1, 1):
        m.box(frame, (hx + 0.04, s * (door_w / 2 + 0.06), door_h / 2), (0.12, 0.12, door_h))
    m.box(frame, (hx + 0.04, 0, door_h + 0.06), (0.12, door_w + 0.24, 0.12))
    m.box('concrete', (hx + 0.45, 0, 0.04), (0.9, door_w + 0.8, 0.08))
    # windows on the outside and the inside of the same wall
    for y in front_windows:
        m.window(hx, y, window_z, 1.7, window_h, frame)
        m.window(hx - T, y, window_z, 1.7, window_h, frame, facing=-1)
    if side_windows:
        for i in range(side_windows):
            x = -hx + D * (i + 0.5) / side_windows
            for s in (-1, 1):
                m.side_window(x, s * hy, window_z, 1.3, window_h, frame, 'glass', s)
                m.side_window(x, s * (hy - T), window_z, 1.3, window_h, frame, 'glass', -s)
    # ceiling and roof are hidden while the player is inside (a multi-storey building has the next floor instead)
    if roof:
        m.roof = True
        m.box(ceiling, (0, 0, H - 0.05), (D - 2 * T, W - 2 * T, 0.1))
        m.box('white', (0, 0, H + 0.1), (D + 0.2, W + 0.2, 0.2))
        m.roof = False
    m.interiors.append(((0, 0, H / 2), (D - 2 * T, W - 2 * T, H)))
    m.door_at = Vector((hx + 2.4, 0, 0))
    return hx


def pendant(m, x, y, H, drop=0.9):
    m.cyl('black', (x, y, H - drop / 2), 0.01, drop, 6)
    m.cyl('black', (x, y, H - drop - 0.05), 0.22, 0.12, 14, 0.06)
    m.sphere('lamp', (x, y, H - drop - 0.12), 0.11, (1, 1, 1), 10)


def bookshelf(m, x, y, turns, length=2.0, height=2.1, depth=0.4, frame='wood_dark'):
    """Back against a wall; books face the room (+X of the piece)."""
    p = Put(m, x, y, turns)
    p.box(frame, (0, 0, height / 2), (depth, length, height), solid=True)
    p.box('wood', (depth / 2 + 0.005, 0, height / 2), (0.01, length - 0.06, height - 0.06))
    colors = ['red', 'navy', 'yellow', 'green', 'purple', 'orange', 'cloth_blue', 'rose', 'white', 'wood_light']
    shelves = 5
    for sh in range(shelves):
        z = 0.1 + sh * (height - 0.15) / shelves
        p.box(frame, (depth / 2 + 0.02, 0, z), (0.04, length - 0.04, 0.03))
        yy = -length / 2 + 0.08
        i = 0
        while yy < length / 2 - 0.12:
            w = 0.035 + ((sh * 7 + i * 13) % 4) * 0.012
            h = 0.22 + ((sh * 5 + i * 3) % 5) * 0.025
            p.box(colors[(sh * 3 + i) % len(colors)], (depth / 2 + 0.06, yy + w / 2, z + 0.02 + h / 2), (0.16, w, h))
            yy += w + 0.006
            i += 1


def counter(m, x, y, turns, length=3.0, top='wood_light', body='wood', register=True):
    """Service counter, customer side toward +X of the piece."""
    p = Put(m, x, y, turns)
    p.box(body, (0, 0, 0.5), (0.7, length, 1.0), solid=True)
    p.box(top, (0.02, 0, 1.03), (0.8, length + 0.06, 0.06))
    if register:
        p.box('black', (-0.05, length / 2 - 0.5, 1.2), (0.35, 0.4, 0.28))
        p.box('glass_dark', (0.05, length / 2 - 0.5, 1.42), (0.05, 0.36, 0.22))


def table(m, x, y, round_top=True, chairs=2, top='white', leg='black', seat='wood', r=0.45):
    if round_top:
        m.cyl(top, (x, y, 0.74), r, 0.04, 16)
    else:
        m.box(top, (x, y, 0.74), (r * 2, r * 2, 0.04))
    m.cyl(leg, (x, y, 0.37), 0.04, 0.72, 8)
    m.cyl(leg, (x, y, 0.02), 0.22, 0.03, 12)
    for i in range(chairs):
        a = math.tau * i / chairs
        cx, cy = x + math.cos(a) * (r + 0.32), y + math.sin(a) * (r + 0.32)
        m.cyl(seat, (cx, cy, 0.46), 0.2, 0.05, 12)
        m.cyl(leg, (cx, cy, 0.23), 0.025, 0.44, 6)
        bx, by = x + math.cos(a) * (r + 0.52), y + math.sin(a) * (r + 0.52)
        back = m.box(seat, (0, 0, 0), (0.04, 0.36, 0.42))
        back.matrix_world = Matrix.Translation((bx, by, 0.7)) @ Matrix.Rotation(a, 4, 'Z')


def plant(m, x, y, h=1.2):
    m.cyl('terracotta', (x, y, 0.2), 0.22, 0.4, 12, 0.17)
    m.sphere('leaf', (x, y, 0.4 + h * 0.45), 0.35, (1, 1, h), 10)


def sofa(m, x, y, turns, length=1.8, color='cloth_blue'):
    p = Put(m, x, y, turns)
    p.box(color, (0, 0, 0.22), (0.8, length, 0.44), bevel=0.04)
    p.box(color, (-0.32, 0, 0.62), (0.18, length, 0.5), bevel=0.04)
    for s in (-1, 1):
        p.box(color, (0, s * (length / 2 - 0.09), 0.5), (0.8, 0.18, 0.3), bevel=0.04)


def menu_board(m, x, y, turns, width=2.2, z=2.2, lines=5):
    p = Put(m, x, y, turns)
    p.box('black', (0, 0, z), (0.05, width, 0.9))
    for i in range(lines):
        p.box('white', (0.03, -width / 2 + 0.25 + i * (width - 0.5) / max(1, lines - 1), z + 0.25), (0.01, 0.3, 0.05))
        p.box('yellow', (0.03, -width / 2 + 0.25 + i * (width - 0.5) / max(1, lines - 1), z - 0.05), (0.01, 0.2, 0.05))


def fridge(m, x, y, turns, length=2.4, height=2.0):
    """Glass-door drinks fridge against a wall, doors toward +X of the piece."""
    p = Put(m, x, y, turns)
    p.box('metal', (0, 0, height / 2), (0.7, length, height), solid=True)
    p.box('glass', (0.36, 0, height / 2 + 0.05), (0.02, length - 0.1, height - 0.3))
    bottles = ['red', 'green', 'orange', 'sky', 'yellow', 'white', 'purple']
    for sh in range(4):
        z = 0.35 + sh * 0.42
        for i in range(int(length / 0.12) - 1):
            yy = -length / 2 + 0.15 + i * 0.12
            p.cyl(bottles[(i + sh * 2) % len(bottles)], (0.2, yy, z), 0.035, 0.24, 8)


def cake_case(m, x, y, turns, length=2.4):
    p = Put(m, x, y, turns)
    p.box('white', (0, 0, 0.45), (0.7, length, 0.9), solid=True)
    p.box('glass', (0.05, 0, 1.15), (0.6, length - 0.05, 0.5))
    for i in range(6):
        yy = -length / 2 + 0.25 + i * (length - 0.5) / 5
        p.cyl('pink' if i % 2 else 'cream', (0.05, yy, 0.98), 0.12, 0.1, 12)
        p.sphere('red', (0.05, yy, 1.06), 0.03)
        p.cyl('wood_light', (0.05, yy, 1.25), 0.1, 0.08, 12)


def pet_pen(m, x, y, sx=2.2, sy=1.6):
    m.box('sand', (x, y, 0.07), (sx, sy, 0.04))
    for s in (-1, 1):
        m.box('white', (x, y + s * sy / 2, 0.35), (sx, 0.04, 0.6))
        m.box('white', (x + s * sx / 2, y, 0.35), (0.04, sy, 0.6))
    def dog(px, py, col, a):
        m.sphere(col, (px, py, 0.28), 0.2, (1.3, 0.8, 0.8), 10)
        hx, hy = px + math.cos(a) * 0.26, py + math.sin(a) * 0.26
        m.sphere(col, (hx, hy, 0.45), 0.14, (1, 1, 1), 10)
        for e in (-1, 1):
            m.sphere('wood_dark', (hx + math.cos(a + e * 1.2) * 0.08, hy + math.sin(a + e * 1.2) * 0.08, 0.57), 0.05, (0.6, 1, 1.4), 6)
    dog(x - 0.5, y - 0.3, 'wood_light', 0.4)
    dog(x + 0.4, y + 0.3, 'white', 2.6)
    m.sphere('orange', (x + 0.5, y - 0.4, 0.25), 0.16, (1.2, 0.9, 0.9), 10)
    m.sphere('orange', (x + 0.62, y - 0.32, 0.38), 0.11, (1, 1, 1), 10)
    m.sphere('red', (x - 0.2, y + 0.5, 0.15), 0.07)


def desks(m, x0, x1, y0, y1, rows, cols):
    for r in range(rows):
        for c in range(cols):
            x = x0 + (x1 - x0) * (r + 0.5) / rows
            y = y0 + (y1 - y0) * (c + 0.5) / cols
            m.box('wood_light', (x, y, 0.72), (0.5, 0.65, 0.04))
            for dy in (-0.27, 0.27):
                m.box('metal', (x, y + dy, 0.36), (0.04, 0.04, 0.7))
            m.box('wood', (x - 0.42, y, 0.44), (0.4, 0.4, 0.04))
            m.box('wood', (x - 0.62, y, 0.68), (0.04, 0.4, 0.45))


def sign_on(m, front, text, z, width, letters='navy', board='sign'):
    m.box(board, (front + 0.08, 0, z), (0.12, width, 0.7), bevel=0.02)
    m.box('wood_dark', (front + 0.03, 0, z), (0.06, width + 0.12, 0.82))
    m.text(text, letters, (front + 0.16, 0, z), 0.5)


# ------------------------------------------------------------------------------------------------- walk-in shops
def walkin_restaurant(Model):
    m = Model('restaurant')
    D, W, H = 9.0, 8.0, 4.0
    front = shell(m, D, W, H, 'cream', 'plaster', 'wood', front_windows=(-2.6, 2.6), side_windows=2, frame='wood_dark')
    m.roof = True
    m.hip_roof('roof_red', (0, 0, H + 0.2), D, W, 2.0, 0.5)
    m.box('brick', (-2.5, 2.2, H + 1.8), (0.7, 0.7, 2.0))
    m.roof = False
    m.awning(front, 0, 3.2, 7.6, 1.5, 0.5, 10, ('roof_green', 'awning_b'))
    sign_on(m, front, '餐廳', 3.55, 2.6, 'roof_red', 'cream')
    # inside: kitchen counter at the back, tables along the sides, open floor in the middle for the meal
    counter(m, -D / 2 + 1.0, 0, 0, 4.5, 'wood_light', 'wood_dark')
    m.box('metal', (-D / 2 + 0.4, 0, 1.4), (0.3, 4.0, 0.9))
    for y in (-2.9, 2.9):
        table(m, -0.8, y, False, 2, 'white', 'wood_dark', 'wood', 0.5)
        table(m, 2.2, y, False, 2, 'white', 'wood_dark', 'wood', 0.5)
    for x in (-1.5, 1.5):
        pendant(m, x, 0, H, 1.0)
    plant(m, front - 0.6, -3.4)
    plant(m, front - 0.6, 3.4)
    for y in (-2.9, 2.9):
        m.table_umbrella(front + 2.2, y, 'roof_green' if y < 0 else 'awning_a')
    m.anchors['table'] = ((0.6, -0.9), 0)
    return m


def walkin_snack(Model):
    m = Model('snack')
    D, W, H = 9.0, 8.0, 3.6
    front = shell(m, D, W, H, 'pink', 'cream', 'tile', front_windows=(-2.6, 2.6), side_windows=2)
    m.roof = True
    m.cyl('wood_light', (0, 0, H + 1.0), 0.9, 0.9, 16, 1.1)
    m.sphere('pink', (0, 0, H + 1.7), 1.05, (1, 1, 0.7), 14)
    m.sphere('red', (0, 0, H + 2.45), 0.25)
    m.roof = False
    for i in range(14):
        m.sphere('rose', (front + 0.1, -W / 2 + (i + 0.5) * W / 14, H - 0.15), 0.22, (0.4, 1, 0.8), 8)
    m.awning(front, 0, 2.95, 7.8, 1.3, 0.45, 12, ('rose', 'awning_b'))
    sign_on(m, front, '點心店', 3.25, 3.0, 'rose', 'white')
    cake_case(m, -D / 2 + 1.0, -1.2, 0, 3.0)
    counter(m, -D / 2 + 1.0, 2.2, 0, 1.6, 'white', 'rose')
    menu_board(m, -D / 2 + 0.22, 0, 0, 3.0, 2.3)
    for y in (-2.8, 2.8):
        table(m, 1.8, y, True, 2, 'white', 'rose', 'pink', 0.42)
    pendant(m, 0, -1.5, H, 0.8); pendant(m, 0, 1.5, H, 0.8)
    m.anchors['table'] = ((0.4, -0.9), 0)
    m.anchors['counter'] = ((-1.8, 1.0), 180)
    return m


def walkin_drinkshop(Model):
    m = Model('drinkshop')
    D, W, H = 8.0, 9.0, 3.6
    front = shell(m, D, W, H, 'mint', 'white', 'tile', front_windows=(-2.8, 2.8), side_windows=2)
    m.roof = True
    m.cyl('sky', (0, 0, H + 1.3), 0.95, 2.0, 20, 1.15)
    m.cyl('sand', (0, 0, H + 0.85), 0.99, 0.9, 20, 1.05)
    m.cyl('white', (0, 0, H + 2.35), 1.2, 0.12, 20)
    m.cyl('rose', (0.3, 0.2, H + 3.0), 0.09, 1.6, 8)
    m.roof = False
    m.awning(front, 0, 2.95, 8.6, 1.2, 0.4, 10, ('mint', 'awning_b'))
    sign_on(m, front, '飲料店', 3.25, 3.0, 'green', 'white')
    counter(m, -D / 2 + 1.1, 0, 0, 5.5, 'white', 'mint')
    menu_board(m, -D / 2 + 0.22, 0, 0, 4.0, 2.4, 6)
    for i in range(6):  # tea dispensers on the back counter
        m.cyl('metal', (-D / 2 + 0.45, -2.2 + i * 0.85, 1.25), 0.15, 0.5, 12)
    fridge(m, 0.5, -W / 2 + T + 0.36, 1, 2.6)
    for y in (2.6,):
        table(m, 1.6, y, True, 3, 'white', 'metal', 'mint', 0.4)
    m.anchors['counter'] = ((-1.6, -1.2), 180)
    m.anchors['seat'] = ((0.6, 0.0), 0)
    return m


def walkin_bookstore(Model):
    m = Model('bookstore')
    D, W, H = 9.0, 9.0, 3.8
    front = shell(m, D, W, H, 'wood', 'cream', 'wood', front_windows=(-2.7, 2.7), side_windows=0, frame='wood_dark')
    m.roof = True
    m.gable_roof('roof_dark', (0, 0, H), D, W, 2.4, along_x=True, overhang=0.5)
    m.gable_end('wood_light', (front, 0, H), W, 2.4, along_x=True)
    m.gable_end('wood_light', (-front, 0, H), W, 2.4, along_x=True)
    m.roof = False
    sign_on(m, front, '書局', 3.2, 2.4, 'wood_dark', 'cream')
    # shelves along the back and sides, a low island in the middle, cashier by the door
    for y in (-2.8, -0.6, 1.6):
        bookshelf(m, -D / 2 + T + 0.2, y + 0.6, 0, 2.0)
    for x in (-1.6, 0.6):
        bookshelf(m, x, -W / 2 + T + 0.2, 1, 2.0)
        bookshelf(m, x, W / 2 - T - 0.2, 3, 2.0)
    # display table off the walkway from the door to the back shelves
    m.box('wood', (1.0, 1.4, 0.45), (1.2, 1.8, 0.9), solid=True)
    colors = ['red', 'navy', 'yellow', 'green', 'purple', 'orange']
    for i in range(8):
        m.box(colors[i % 6], (0.75 + (i % 2) * 0.5, 0.75 + (i // 2) * 0.43, 0.95), (0.3, 0.22, 0.06 + (i % 3) * 0.04))
    counter(m, front - 1.4, 3.0, 2, 1.6, 'wood_light', 'wood_dark')
    pendant(m, -1.0, 0, H, 0.8); pendant(m, 1.4, 0, H, 0.8)
    m.anchors['shelf'] = ((-2.7, -1.8), 180)
    return m


def walkin_library(Model):
    m = Model('library')
    D, W, H = 12.0, 10.0, 5.6
    front = shell(m, D, W, H, 'stone', 'cream', 'wood', door_w=2.2, door_h=3.0, front_windows=(-3.5, 3.5), side_windows=3,
                  window_h=2.6, window_z=2.4, frame='white')
    m.roof = True
    m.cyl('stone', (-1.0, 0, H + 1.0), 2.4, 1.6, 24)
    m.sphere('roof_green', (-1.0, 0, H + 1.8), 2.4, (1, 1, 0.75), 24)
    m.cyl('white', (-1.0, 0, H + 3.7), 0.15, 0.6, 8)
    m.roof = False
    # portico outside
    for i in range(3):
        m.box('stone', (front + 1.9 - i * 0.3, 0, 0.1 + i * 0.2), (3.8 - i * 0.6, 7.6 - i * 0.4, 0.2))
    for y in (-2.7, -0.9, 0.9, 2.7):
        m.cyl('white', (front + 2.2, y, 2.95), 0.26, 4.7, 16)
    m.box('white', (front + 1.5, 0, 5.6), (3.2, 7.2, 0.4))
    m.gable_roof('roof_green', (front + 1.5, 0, 5.8), 3.2, 7.2, 1.3, along_x=True, overhang=0.15)
    m.gable_end('white', (front + 3.05, 0, 5.8), 7.2, 1.3, along_x=True)
    m.text('圖書館', 'navy', (front + 3.15, 0, 6.3), 0.5)
    m.door_at = Vector((front + 4.4, 0, 0))
    # tall shelves in rows at the back half, reading tables in the front half
    for x in (-4.6, -3.0, -1.4):
        bookshelf(m, x, -2.4, 0, 3.2, 2.4)
        bookshelf(m, x, 2.4, 0, 3.2, 2.4)
    for x in (-4.6, -3.0, -1.4):
        bookshelf(m, x - 0.42, -2.4, 2, 3.2, 2.4)
        bookshelf(m, x - 0.42, 2.4, 2, 3.2, 2.4)
    for y in (-3.3, 3.3):
        m.box('wood', (2.6, y, 0.74), (2.4, 1.0, 0.06))
        for dx in (-1.0, 1.0):
            m.box('wood_dark', (2.6 + dx, y, 0.37), (0.06, 0.8, 0.72))
        for dx in (-0.7, 0.7):
            for s in (-1, 1):
                m.box('wood', (2.6 + dx, y + s * 0.75, 0.45), (0.45, 0.45, 0.05))
        m.lantern(2.6, y, 1.0, 0.1)
    counter(m, front - 1.2, 3.6, 2, 1.8, 'wood_light', 'wood_dark')
    for x in (-3.0, 1.0):
        pendant(m, x, 0, H, 1.6)
    m.anchors['shelf'] = ((-0.6, -0.45), 180)
    m.anchors['seat'] = ((1.0, -1.0), 0)
    return m


def walkin_petshop(Model):
    m = Model('petshop')
    D, W, H = 8.0, 9.0, 3.6
    front = shell(m, D, W, H, 'sky', 'white', 'tile', front_windows=(-2.8, 2.8), side_windows=2)
    m.roof = True
    m.box('white', (0.5, 0, H + 1.4), (0.2, 2.2, 2.0), bevel=0.05)
    m.sphere('wood_dark', (0.62, 0, H + 1.1), 0.45, (0.3, 1.1, 0.9), 12)
    for dy, dz in ((-0.55, 0.45), (-0.2, 0.7), (0.2, 0.7), (0.55, 0.45)):
        m.sphere('wood_dark', (0.62, dy, H + 1.3 + dz), 0.16, (0.3, 1, 1.2), 10)
    m.roof = False
    m.awning(front, 0, 2.95, 8.6, 1.2, 0.4, 10, ('cloth_blue', 'awning_b'))
    sign_on(m, front, '寵物店', 3.25, 3.0, 'cloth_blue', 'white')
    pet_pen(m, -2.4, -2.6, 2.4, 2.0)
    pet_pen(m, -2.4, 2.6, 2.4, 2.0)
    for i in range(3):  # glass cages on the back wall
        y = -2.4 + i * 2.4
        m.box('white', (-D / 2 + 0.5, y, 1.3), (0.6, 1.6, 0.04))
        m.box('glass', (-D / 2 + 0.79, y, 1.65), (0.02, 1.6, 0.66))
        m.sphere(['orange', 'white', 'wood_light'][i], (-D / 2 + 0.5, y, 1.45), 0.16, (1.2, 0.9, 0.9), 10)
    m.box('white', (-D / 2 + 0.5, 0, 0.65), (0.6, 7.0, 1.3), solid=True)
    counter(m, front - 1.4, 3.2, 2, 1.6, 'white', 'cloth_blue')
    for y in (-1.0, 1.0):  # food shelves
        m.box('metal', (1.0, y * 3.4, 0.8), (1.6, 0.4, 1.6), solid=True)
    m.anchors['pets'] = ((-0.4, -0.6), 180)
    return m


def walkin_cafe(Model):
    m = Model('cafe')
    D, W, H = 9.0, 10.0, 3.8
    front = shell(m, D, W, H, 'brick', 'plaster', 'wood', front_windows=(-3.0, 3.0), side_windows=2, frame='black')
    m.roof = True
    m.box('black', (0, 0, H + 0.45), (D + 0.3, W + 0.3, 0.5))
    m.roof = False
    m.awning(front, 0, 3.0, 9.4, 1.4, 0.4, 12, ('black', 'cream'))
    sign_on(m, front, '咖啡廳', 3.3, 3.0, 'cream', 'black')
    counter(m, -D / 2 + 1.2, -1.5, 0, 4.0, 'wood_light', 'black')
    m.box('metal', (-D / 2 + 0.5, -2.5, 1.35), (0.5, 0.7, 0.6))  # espresso machine
    m.box('black', (-D / 2 + 0.5, -2.5, 1.7), (0.4, 0.5, 0.1))
    menu_board(m, -D / 2 + 0.22, -1.5, 0, 3.0, 2.4)
    sofa(m, -D / 2 + 0.75, 3.2, 0, 2.6, 'cloth_indigo')
    m.box('wood_dark', (-D / 2 + 1.7, 3.2, 0.42), (0.6, 1.4, 0.04))
    for y in (-3.6,):
        table(m, 2.4, y, True, 2, 'wood_light', 'black', 'black', 0.4)
    table(m, 2.4, 3.4, True, 2, 'wood_light', 'black', 'black', 0.4)
    plant(m, front - 0.5, -4.4); plant(m, front - 0.5, 4.4)
    for x in (-1.0, 1.6):
        pendant(m, x, 0, H, 0.9)
    m.anchors['counter'] = ((-1.4, 0.4), 180)
    m.anchors['seat'] = ((0.8, -0.9), 0)
    return m


def walkin_conveni(Model):
    m = Model('conveni')
    D, W, H = 8.0, 10.0, 3.4
    front = shell(m, D, W, H, 'white', 'white', 'tile', door_w=2.0, front_windows=(-3.2, 3.2), side_windows=0)
    m.box('green', (front + 0.06, 0, H - 0.35), (0.06, W, 0.3))
    m.box('orange', (front + 0.06, 0, H - 0.6), (0.06, W, 0.2))
    m.box('red', (front + 0.06, 0, H - 0.78), (0.06, W, 0.14))
    sign_on(m, front, '便利商店', 2.85, 3.4, 'green', 'white')
    fridge(m, -D / 2 + T + 0.36, 0, 0, 7.0, 2.0)
    for y in (-2.4, 0.0, 2.4):  # snack gondolas
        p = Put(m, -0.8, y, 0)
        p.box('white', (0, 0, 0.75), (2.6, 0.5, 1.5), solid=True)
        for sh in range(3):
            for i in range(9):
                p.box(['red', 'yellow', 'orange', 'green', 'sky', 'purple'][(i + sh) % 6], (-1.15 + i * 0.29, -0.3, 0.35 + sh * 0.45), (0.22, 0.1, 0.25))
                p.box(['yellow', 'red', 'navy', 'orange', 'pink', 'green'][(i + sh) % 6], (-1.15 + i * 0.29, 0.3, 0.35 + sh * 0.45), (0.22, 0.1, 0.25))
    counter(m, front - 1.2, -3.2, 2, 2.4, 'white', 'white')
    m.box('yellow', (front - 1.6, -3.2, 1.3), (0.4, 0.8, 0.5))  # hot snack case
    m.anchors['aisle'] = ((0.9, -1.2), 180)
    return m


def walkin_lecture(Model):
    m = Model('lecture')
    D, W, H = 14.0, 16.0, 6.0
    front = shell(m, D, W, H, 'brick', 'plaster', 'wood', door_w=2.6, door_h=3.0, front_windows=(-5.2, -2.8, 2.8, 5.2),
                  side_windows=4, window_h=2.6, window_z=2.4, frame='white')
    m.roof = True
    m.gable_roof('roof_dark', (0, 0, H), D, W, 3.0, along_x=False, overhang=0.6)
    for s in (-1, 1):
        m.gable_end('brick', (0, s * W / 2, H), D, 3.0, along_x=False)
    m.roof = False
    # clock tower over the entrance (outside the hall, always visible)
    for s in (-1, 1):  # two piers with an arch between them over the path to the door
        m.box('brick', (front + 1.2, s * 1.45, 4.0), (2.4, 0.7, 8.0), solid=True)
    m.box('brick', (front + 1.2, 0, 5.75), (2.4, 2.2, 4.5))
    m.hip_roof('roof_dark', (front + 1.2, 0, 8.0), 2.4, 3.6, 2.0, 0.3)
    m.cyl('white', (front + 2.42, 0, 6.8), 0.9, 0.08, 24, axis='X')
    m.cyl('black', (front + 2.48, 0, 6.8), 0.08, 0.06, 8, axis='X')
    m.box('black', (front + 2.5, 0, 7.1), (0.04, 0.06, 0.55))
    m.box('black', (front + 2.5, 0.2, 6.8), (0.04, 0.42, 0.06))
    m.text('大學講堂', 'white', (front + 2.45, 0, 4.6), 0.5)
    m.door_at = Vector((front + 4.4, 0, 0))
    # lecture hall: blackboard and lectern at the back, rows of desks facing it
    m.box('green', (-D / 2 + T + 0.03, 0, 2.2), (0.04, 7.0, 2.0))
    m.box('wood_dark', (-D / 2 + T + 0.05, 0, 1.15), (0.1, 7.2, 0.08))
    m.box('wood', (-D / 2 + 1.8, 0, 0.55), (0.6, 1.0, 1.1), solid=True)
    m.box('wood_dark', (-D / 2 + 1.2, 0, 0.1), (2.0, 9.0, 0.2))
    desks(m, -D / 2 + 3.4, D / 2 - 3.4, -W / 2 + 1.2, -1.6, 4, 4)
    desks(m, -D / 2 + 3.4, D / 2 - 3.4, 1.6, W / 2 - 1.2, 4, 4)
    for x in (-3.0, 0.0, 3.0):
        pendant(m, x, 0, H, 2.0)
    m.anchors['seat'] = ((1.0, 0.0), 90)  # in the centre aisle; the pair layout runs toward the front
    return m


def walkin_cafeteria(Model):
    m = Model('cafeteria')
    D, W, H = 12.0, 14.0, 4.2
    front = shell(m, D, W, H, 'cream', 'white', 'tile', door_w=2.4, front_windows=(-4.6, -2.4, 2.4, 4.6), side_windows=3)
    m.roof = True
    m.box('roof_green', (0, 0, H + 0.4), (D + 0.6, W + 0.6, 0.4))
    m.roof = False
    m.awning(front, 0, 3.2, 6.0, 1.4, 0.4, 10, ('green', 'awning_b'))
    sign_on(m, front, '學生餐廳', 3.65, 3.6, 'green', 'white')
    counter(m, -D / 2 + 1.2, 0, 0, 10.0, 'metal', 'white', register=False)
    for i in range(8):
        m.box(['orange', 'green', 'yellow', 'red'][i % 4], (-D / 2 + 1.1, -4.2 + i * 1.2, 1.1), (0.4, 0.5, 0.08))
    m.box('metal', (-D / 2 + 0.4, 0, 1.6), (0.3, 11.0, 1.2))
    menu_board(m, -D / 2 + 0.22, 0, 0, 6.0, 3.0, 8)
    for x in (-1.6, 1.6):
        for y in (-4.6, 4.6):
            m.box('white', (x, y, 0.74), (1.0, 2.6, 0.05))
            m.box('metal', (x, y, 0.37), (0.06, 2.2, 0.7))
            for s in (-1, 1):
                m.box('orange', (x + s * 0.75, y, 0.45), (0.35, 2.4, 0.05))
    for x in (-2.0, 1.6):
        pendant(m, x, -4.6, H, 1.2); pendant(m, x, 4.6, H, 1.2)
    m.anchors['table'] = ((0.4, -1.3), 0)
    return m


# ------------------------------------------------------------------------------------------------- open-air places
def build_park(Model):
    m = Model('park')
    L, W = 24.0, 26.0  # depth (x) x width (y)
    m.box('grass', (0, 0, 0.01), (L, W, 0.02))
    # paths: a cross and a ring around the fountain, and the east path to the shrine
    m.box('sand', (0, 0, 0.025), (L, 2.4, 0.01))
    m.box('sand', (0, 0, 0.025), (2.4, W, 0.01))
    m.box('sand', (0, -11.0, 0.025), (L, 2.0, 0.01))
    m.cyl('sand', (0, 0, 0.026), 5.0, 0.012, 40)
    # fountain
    m.cyl('stone', (0, 0, 0.3), 3.0, 0.6, 36)
    m.cyl('water', (0, 0, 0.55), 2.75, 0.1, 36)
    m.cyl('stone', (0, 0, 1.0), 0.45, 1.6, 16)
    m.cyl('stone', (0, 0, 1.85), 1.2, 0.15, 24)
    m.cyl('water', (0, 0, 1.95), 1.05, 0.06, 24)
    m.sphere('sky', (0, 0, 2.35), 0.35, (1, 1, 1.6), 12)
    m.boxes.append((Vector((0, 0, 0.5)), Vector((5.6, 5.6, 1.0))))
    # benches facing the fountain, lamps, flower beds and trees around
    for (x, y, rot) in ((5.6, 4.0, math.pi), (-5.6, 4.0, 0.0), (5.6, -4.0, math.pi), (-5.6, -4.0, 0.0)):
        m.bench(x, y, rot)
    for (x, y) in ((4.5, 6.5), (-4.5, 6.5), (4.5, -6.5), (-4.5, -6.5)):
        m.street_lamp(x, y)
    for (x, y) in ((8.5, 8.5), (-8.5, 8.5), (8.5, -8.5), (-8.5, -8.5)):
        m.cyl('wood_dark', (x, y, 0.2), 1.6, 0.4, 24)
        for k in range(10):
            a = k / 10 * math.tau
            m.sphere(['red', 'yellow', 'pink', 'purple', 'white'][k % 5], (x + math.cos(a) * 1.1, y + math.sin(a) * 1.1, 0.5), 0.2)
        m.sphere('leaf', (x, y, 0.55), 0.7, (1, 1, 0.6), 10)
    for (x, y) in ((10.5, 2.5), (10.5, -4.0), (-10.5, 3.0), (-10.5, -3.5), (3.0, 11.5), (-3.0, 11.5), (6.0, -12.0), (-7.0, -11.8)):
        m.tree(x, y, 5.5, 1.6, 'leaf' if (x + y) % 2 else 'leaf_light')
    m.sphere('leaf', (0, 12.5, 0.4), 0.6, (6, 1, 0.8), 10)  # hedge along the back
    m.door_at = Vector((L / 2 + 0.8, 0, 0))
    m.anchors['bench'] = ((7.5, -2.0), 180)
    m.anchors['fountain'] = ((5.2, -0.8), 180)
    return m


def build_shrine(Model):
    m = Model('shrine')
    # torii at the front, stone path, lanterns, the hall on a platform at the back
    m.box('sand', (0, 0, 0.01), (14.0, 12.0, 0.02))
    m.box('stone', (2.0, 0, 0.03), (10.0, 1.6, 0.03))
    for s in (-1, 1):
        m.cyl('red', (6.2, s * 1.6, 2.2), 0.18, 4.4, 14)
        m.cyl('black', (6.2, s * 1.6, 0.15), 0.24, 0.3, 14)
    m.box('red', (6.2, 0, 3.6), (0.3, 4.2, 0.25))
    m.box('black', (6.2, 0, 4.3), (0.4, 5.0, 0.25))
    m.box('black', (6.2, 0, 4.48), (0.5, 5.4, 0.12))
    m.box('wood_light', (6.32, 0, 3.95), (0.06, 0.6, 0.45))
    m.text('神社', 'black', (6.37, 0, 3.95), 0.32)
    for x in (3.0, 0.5):
        for s in (-1, 1):
            m.box('stone', (x, s * 1.6, 0.45), (0.35, 0.35, 0.9))
            m.box('lamp', (x, s * 1.6, 1.05), (0.45, 0.45, 0.32))
            m.hip_roof('stone_dark', (x, s * 1.6, 1.22), 0.5, 0.5, 0.3, 0.12)
    # hall
    m.box('stone', (-4.0, 0, 0.3), (5.0, 7.0, 0.6), solid=True)
    m.box('wood_dark', (-4.0, 0, 1.0), (5.0, 7.0, 0.1))
    for x in (-6.0, -2.0):
        for y in (-3.0, -1.0, 1.0, 3.0):
            m.cyl('red', (x, y, 2.0), 0.15, 2.0, 12)
    m.box('wood', (-4.6, 0, 2.0), (3.4, 6.0, 2.0))
    m.box('white', (-2.88, 0, 1.8), (0.04, 4.0, 1.4))
    m.gable_roof('roof_green', (-4.0, 0, 3.0), 5.0, 7.0, 2.2, along_x=False, overhang=0.9)
    for s in (-1, 1):
        m.gable_end('wood', (-4.0, s * 3.5, 3.0), 5.0, 2.2, along_x=False)
    for i in range(3):  # steps up to the hall
        m.box('stone', (-1.2 + i * 0.3, 0, 0.1 + i * 0.18), (0.4, 2.4, 0.2 + i * 0.36))
    m.box('wood_dark', (-1.6, 0, 1.2), (0.6, 1.4, 0.5))  # offering box
    m.cyl('yellow', (-1.8, 0, 2.6), 0.15, 0.3, 10)   # bell
    m.cyl('red', (-1.8, 0, 1.9), 0.03, 1.2, 6)       # bell rope
    for (x, y) in ((4.0, -5.0), (4.0, 5.0), (-6.5, -5.0), (-6.5, 5.0)):
        m.tree(x, y, 6.0, 1.5, 'leaf')
    m.door_at = Vector((8.6, 0, 0))
    m.anchors['torii'] = ((4.6, -0.8), 180)
    m.anchors['pray'] = ((0.2, -0.5), 180)  # in front of the offering box, facing the hall
    return m
