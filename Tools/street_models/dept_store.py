"""The ten-storey department store for build_street_models.py.

Every floor is a walkable room (4 m storeys) with its own theme. Each floor is built at height 0 and then raised
into place, and its pieces carry level k, so in game the floors above the player hide and the camera looks into his
floor. An elevator at the back left has a 'liftK:label' anchor in front of it on every floor (the game's elevator
panel uses them); activity anchors sit in the open middle of each floor.
"""
import math

import lift

from mathutils import Matrix, Vector

from interiors import T, plant, counter, bookshelf, table, sofa
from interiors_public import _bench_for_seated

FH = 4.0          # storey height
FLOORS = 10
D, W = 18.0, 20.0  # depth (x, front at +x) and width (y)
HX, HY = D / 2, W / 2

THEMES = [
    ('1F', '化妝品・服務台', 'tile', 'white'),
    ('2F', '女裝', 'wood_light', 'pink'),
    ('3F', '男裝', 'wood', 'navy'),
    ('4F', '鞋包配件', 'tile', 'wood_light'),
    ('5F', '家居生活', 'wood_light', 'mint'),
    ('6F', '書店文具', 'wood', 'cream'),
    ('7F', '電器', 'concrete', 'sky'),
    ('8F', '玩具', 'tile', 'yellow'),
    ('9F', '美食街', 'tile', 'orange'),
    ('10F', '景觀餐廳', 'wood_dark', 'cream'),
]


def _raise(m, objects_from, boxes_from, z):
    """Lift everything made since the marks by z (objects keep any rotation they were given)."""
    for o in m.objects[objects_from:]:
        o.matrix_world = Matrix.Translation((0, 0, z)) @ o.matrix_world
    for i in range(boxes_from, len(m.boxes)):
        c, s = m.boxes[i]
        m.boxes[i] = (Vector((c.x, c.y, c.z + z)), s)


def _rack(m, x, y, length, colors):
    for s in (-1, 1):
        m.cyl('metal', (x, y + s * length / 2, 0.8), 0.025, 1.6, 6)
    m.box('metal', (x, y, 1.6), (0.04, length, 0.04))
    for i in range(int(length / 0.22)):
        m.box(colors[i % len(colors)], (x, y - length / 2 + 0.15 + i * 0.22, 1.1), (0.45, 0.06, 0.9))
    m.boxes.append((Vector((x, y, 0.9)), Vector((0.6, length, 1.8))))


def _mannequin(m, x, y, color):
    m.cyl('metal', (x, y, 0.05), 0.2, 0.1, 12)
    m.cyl('metal', (x, y, 0.5), 0.02, 0.9, 6)
    m.box(color, (x, y, 1.25), (0.25, 0.42, 0.6))
    m.sphere('white', (x, y, 1.7), 0.12)


def _floor_contents(m, k):
    """Floor k's furniture at height 0; returns {anchor name: ((x, y), yaw)} for this floor."""
    anchors = {}
    if k == 1:
        for (x, y) in ((2.5, -6.5), (2.5, 6.5), (-2.5, -6.5), (-2.5, 6.5)):
            m.box('white', (x, y, 0.5), (1.4, 2.4, 1.0), solid=True)
            m.box('glass', (x, y, 1.05), (1.3, 2.3, 0.08))
            for i in range(8):
                m.cyl(['rose', 'pink', 'red', 'purple', 'white', 'yellow'][i % 6], (x - 0.4 + (i % 2) * 0.8, y - 0.9 + (i // 2) * 0.6, 1.2), 0.05, 0.22, 8)
            m.box('glass', (x - 0.75, y, 1.6), (0.04, 0.8, 0.9))
        m.cyl('wood_light', (6.0, -4.5, 0.55), 1.1, 1.1, 24)
        m.boxes.append((Vector((6.0, -4.5, 0.55)), Vector((2.2, 2.2, 1.1))))
        m.box('navy', (6.0, -4.5, 1.5), (0.06, 1.4, 0.5))
        m.text('服務台', 'white', (6.04, -4.5, 1.5), 0.3)
        anchors['beauty'] = ((1.0, -1.0), 180)
        anchors['hall'] = ((1.5, 0.0), 0)  # walkway: door -> hall -> elevator
    elif k in (2, 3):
        colors = ['pink', 'rose', 'white', 'purple', 'sky'] if k == 2 else ['navy', 'concrete', 'black', 'wood', 'cloth_blue']
        for y in (-6.5, -4.0, 4.0, 6.5):
            _rack(m, 2.0, y, 3.0, colors)
        for i, (x, y) in enumerate(((5.5, -2.5), (5.5, 2.5), (-1.5, -7.0), (-1.5, 7.0))):
            _mannequin(m, x, y, colors[i % len(colors)])
        m.box('glass', (-4.2, 0.0, 1.1), (0.08, 1.6, 2.0))
        m.box('wood_dark', (-4.26, 0.0, 1.1), (0.06, 1.8, 2.2))
        for y in (-3.5, -2.2, 2.2, 3.5):
            m.box('wood', (-7.6, y, 1.1), (1.6, 0.05, 2.2))
        for y in (-2.85, 2.85):
            m.box('cloth_indigo' if k == 3 else 'rose', (-6.8, y, 1.1), (0.04, 1.2, 2.0))
        anchors['mirror%d' % k] = ((-2.0, 0.0), 180)
    elif k == 4:
        for y in (-HY + T + 0.25, HY - T - 0.25):
            m.box('wood', (0.0, y, 1.0), (10.0, 0.4, 2.0), solid=True)
            for sh in range(4):
                for i in range(20):
                    m.box(['black', 'wood', 'red', 'white', 'navy'][(i + sh) % 5], (-4.7 + i * 0.5, y - (0.24 if y > 0 else -0.24), 0.35 + sh * 0.45), (0.3, 0.14, 0.14))
        for (x, y) in ((4.5, -4.0), (4.5, 4.0)):
            m.box('wood_light', (x, y, 0.45), (1.2, 2.0, 0.9), solid=True)
            for i in range(4):
                m.box(['rose', 'navy', 'orange', 'black'][i], (x, y - 0.75 + i * 0.5, 1.05), (0.4, 0.3, 0.3))
        _bench_for_seated(m, 1.0, 1.0, 0)
        anchors['shoes'] = ((1.0, 1.0), 0)
    elif k == 5:
        sofa(m, -6.0, -5.5, 0, 2.6, 'cloth_blue')
        sofa(m, -6.0, 5.5, 0, 2.6, 'rose')
        m.box('white', (-5.5, 0.0, 0.3), (2.2, 1.8, 0.6), solid=True)
        m.box('sky', (-5.5, 0.0, 0.62), (2.1, 1.7, 0.06))
        m.box('wood_dark', (-6.65, 0.0, 0.6), (0.1, 1.9, 1.2))
        for (x, y) in ((4.5, -6.0), (4.5, 6.0)):
            m.cyl('metal', (x, y, 0.75), 0.02, 1.4, 6)
            m.cyl('lamp', (x, y, 1.5), 0.2, 0.3, 12, 0.12)
            m.cyl('wood_light', (x, y, 0.05), 0.3, 0.1, 12)
        _bench_for_seated(m, 1.0, 1.0, 0)
        anchors['sofa'] = ((1.0, 1.0), 0)
    elif k == 6:
        for y in (-5.0, -2.6, 2.6, 5.0):
            bookshelf(m, -HX + T + 0.2, y, 0, 2.2, 2.2)
        for x in (-2.0, 1.5):
            bookshelf(m, x, -HY + T + 0.2, 1, 2.2, 2.0)
            bookshelf(m, x, HY - T - 0.2, 3, 2.2, 2.0)
        anchors['books'] = ((-6.4, 1.0), 180)
    elif k == 7:
        for i in range(5):
            for j in range(2):
                m.box('black', (-HX + T + 0.08, -5.0 + i * 2.5, 1.0 + j * 1.3), (0.06, 2.2, 1.2))
                m.box(['sky', 'green', 'orange', 'purple', 'rose'][(i + j) % 5], (-HX + T + 0.12, -5.0 + i * 2.5, 1.0 + j * 1.3), (0.02, 2.0, 1.05))
        for (x, y) in ((4.0, -5.5), (4.0, 5.5)):
            m.box('white', (x, y, 0.45), (1.4, 2.6, 0.9), solid=True)
            for i in range(5):
                m.box('black', (x, y - 1.0 + i * 0.5, 0.93), (0.4, 0.2, 0.02))
        anchors['phones'] = ((1.0, 0.0), 0)
    elif k == 8:
        bx, by = -5.0, 0.0
        m.sphere('wood_light', (bx, by, 0.9), 0.9, (1, 1.1, 1.0), 14)
        m.sphere('wood_light', (bx + 0.1, by, 2.1), 0.6, (1, 1, 1), 14)
        for s in (-1, 1):
            m.sphere('wood_light', (bx + 0.1, by + s * 0.45, 2.6), 0.22)
            m.sphere('wood_light', (bx + 0.5, by + s * 0.7, 0.4), 0.3)
            m.sphere('black', (bx + 0.62, by + s * 0.2, 2.2), 0.06)
        m.sphere('red', (bx + 0.7, by, 2.0), 0.08)
        m.boxes.append((Vector((bx, by, 1.2)), Vector((2.0, 2.2, 2.4))))
        for y in (-HY + T + 0.3, HY - T - 0.3):
            m.box('white', (0.0, y, 1.0), (12.0, 0.5, 2.0), solid=True)
            for i in range(24):
                m.sphere(['red', 'yellow', 'sky', 'green', 'orange', 'purple'][i % 6], (-5.6 + i * 0.5, y - (0.35 if y > 0 else -0.35), 0.6 + (i % 3) * 0.55), 0.14)
        m.box('sky', (4.5, -5.5, 0.25), (3.0, 3.0, 0.5))
        for i in range(30):
            m.sphere(['red', 'yellow', 'green', 'sky', 'orange'][i % 5], (3.2 + (i % 6) * 0.5, -6.8 + (i // 6) * 0.55, 0.55), 0.12)
        anchors['bear'] = ((-1.0, 0.0), 180)
    elif k == 9:
        stalls = ['拉麵', '咖哩', '壽司', '炸雞', '甜點', '飲料']
        for i, name in enumerate(stalls):
            y = -7.5 + i * 3.0
            m.box('white', (-HX + 1.0, y, 0.55), (0.8, 2.4, 1.1), solid=True)
            m.box(['red', 'yellow', 'navy', 'orange', 'pink', 'green'][i], (-HX + T + 0.06, y, 2.6), (0.06, 2.4, 0.7))
            m.text(name, 'white', (-HX + T + 0.1, y, 2.6), 0.4)
        for (x, y) in ((4.0, -5.0), (4.0, 5.0), (6.5, -5.0), (6.5, 5.0)):
            table(m, x, y, False, 4, 'white', 'metal', 'orange', 0.45)
        anchors['food'] = ((1.0, 2.0), 0)
    elif k == 10:
        for (x, y) in ((6.0, -6.0), (6.0, 6.0), (3.0, -6.0), (3.0, 6.0), (-1.0, -6.0), (-1.0, 6.0)):
            table(m, x, y, True, 2, 'white', 'wood_dark', 'wood_dark', 0.45)
        counter(m, -HX + 1.2, 0.0, 0, 6.0, 'wood_light', 'wood_dark')
        anchors['view'] = ((5.5, 2.0), 0)
    return anchors


def department_store(Model):
    LIFT = (-HX + 1.0, HY - 1.6)  # the shaft, back left (inside the walls)
    m = Model('department')
    door_w = 3.2
    for k in range(1, FLOORS + 1):
        z = (k - 1) * FH
        m.level = k
        o0, b0 = len(m.objects), len(m.boxes)
        floor_mat, accent = THEMES[k - 1][2], THEMES[k - 1][3]
        # floor slab (also the ceiling of the floor below), walls with inner finish, the glass front
        if k == 1:
            m.box(floor_mat, (0, 0, 0.03), (D - 0.05, W - 0.05, 0.06))
        else:
            lift.slab_with_hole(m, floor_mat, 0, 0, D - 0.05, W - 0.05, -0.1, 0.26, LIFT)
        m.box('white', (-HX + T / 2, 0, FH / 2), (T, W, FH), solid=True)
        for s in (-1, 1):
            m.box('white', (0, s * (HY - T / 2), FH / 2), (D, T, FH), solid=True)
        m.box(accent, (-HX + T + 0.01, 0, FH / 2), (0.02, W - 2 * T, FH - 0.3))
        for s in (-1, 1):
            m.box(accent if k % 2 else 'cream', (0, s * (HY - T - 0.01), FH / 2), (D - 2 * T, 0.02, FH - 0.3))
        if k == 1:
            seg = (W - door_w) / 2
            for s in (-1, 1):
                m.box('glass_dark', (HX - 0.1, s * (door_w / 2 + seg / 2), FH / 2), (0.12, seg, FH), solid=True)
            m.box('white', (HX - 0.1, 0, 3.4), (0.14, door_w, 1.2))
        else:
            m.box('glass_dark', (HX - 0.1, 0, FH / 2), (0.12, W, FH), solid=True)
        for i in range(9):
            m.box('white', (HX + 0.02, -HY + i * W / 8, FH / 2), (0.1, 0.12, FH))
        m.box('white', (HX + 0.05, 0, 0.1), (0.2, W + 0.2, 0.2))
        for s in (-1, 1):
            m.box('glass_dark', (0, s * (HY + 0.02), FH / 2 + 0.3), (D - 2.0, 0.04, 1.6))
        # elevator (back left), doors toward the room, the floor's directory board beside it
        spot = lift.shaft(m, LIFT[0], LIFT[1], FH, 1)
        m.box(accent, (-HX + T + 0.04, HY - 3.6, 2.2), (0.04, 1.8, 0.8))
        m.text(THEMES[k - 1][0] + ' ' + THEMES[k - 1][1], 'black', (-HX + T + 0.08, HY - 3.6, 2.2), 0.22)
        # escalator (front right) for looks: a steel ramp up to the next floor
        if k < FLOORS:
            ang = -math.atan2(FH, 5.2)
            ramp = m.box('metal', (0, 0, 0), (5.4, 1.0, 0.25))
            ramp.matrix_world = Matrix.Translation((-HX + 3.6, -HY + 1.2, FH / 2)) @ Matrix.Rotation(ang, 4, 'Y')
            rail = m.box('black', (0, 0, 0), (5.6, 0.08, 0.9))
            rail.matrix_world = Matrix.Translation((-HX + 3.6, -HY + 1.75, FH / 2 + 0.5)) @ Matrix.Rotation(ang, 4, 'Y')
            m.boxes.append((Vector((-HX + 3.6, -HY + 1.2, 1.0)), Vector((5.6, 1.2, 2.0))))
        for (x, y) in ((-2.0, -3.0), (-2.0, 3.0), (3.5, 0.0)):
            m.sphere('lamp', (x, y, FH - 0.3), 0.14)
        plant(m, HX - 0.8, -HY + 0.8)
        anchors = _floor_contents(m, k)
        _raise(m, o0, b0, z)
        for name, ((x, y), yaw) in anchors.items():
            m.anchors[name] = ((x, y, z), yaw)
        m.interiors.append(((0, 0, z + FH / 2), (D - 2 * T, W - 2 * T, FH)))
        m.anchors['lift%d:%s %s' % (k, THEMES[k - 1][0], THEMES[k - 1][1])] = ((spot[0], spot[1], z), 0)
    m.level = 0
    top = FLOORS * FH
    # roof garden (hidden while inside)
    m.roof = True
    o0, b0 = len(m.objects), len(m.boxes)
    m.box('white', (0, 0, 0.15), (D + 0.4, W + 0.4, 0.3))
    m.box('grass', (-1.0, 0, 0.32), (D - 4.0, W - 4.0, 0.04))
    for (x, y) in ((-5.0, -6.0), (-5.0, 6.0), (3.0, -6.0), (3.0, 6.0)):
        m.tree(x, y, 3.5, 1.0)
    _raise(m, o0, b0, top)
    m.roof = False
    # always visible: the entrance canopy and a vertical sign on the front corner
    m.box('white', (HX + 1.6, 0, 3.6), (3.2, 7.0, 0.3))
    for y in (-3.0, 3.0):
        m.cyl('white', (HX + 3.0, y, 1.8), 0.15, 3.6, 10)
    m.box('red', (HX + 0.3, HY - 0.6, 24.0), (0.5, 1.4, 22.0))
    for i, ch in enumerate('大學城百貨'):
        m.text(ch, 'white', (HX + 0.56, HY - 0.6, 32.0 - i * 3.6), 2.0)
    m.text('DEPARTMENT STORE', 'navy', (HX + 1.78, 0, 3.6), 0.22)
    m.door_at = Vector((HX + 3.6, 0, 0))
    return m
