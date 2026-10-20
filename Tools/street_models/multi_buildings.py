"""Multi-storey versions of the school, the dorms, the lodge and the hotel: every floor is walkable and reached by
the elevator (the game shows the elevator panel inside). Ground floors as in interiors_more / interiors_resort."""
import math

from mathutils import Vector

from interiors import T, shell, desks, pendant, sofa, plant, counter, sign_on
from multi_floor import stack, ground_lift, classroom_floor, dorm_floor, dorm_stairs, hotel_floor, lodge_floor


def school(Model):
    m = Model('school')
    D, W, FH = 14.0, 16.0, 3.2
    m.level = 1
    front = shell(m, D, W, FH, 'cream', 'plaster', 'wood', door_w=2.4, front_windows=(-5.6, -3.0, 3.0, 5.6), side_windows=4, roof=False)
    labels = ['1F 教室', '2F 教室', '3F 音樂教室']
    lift = (-D / 2 + T + 0.8, W / 2 - T - 1.1)
    ground_lift(m, lift, FH, labels[0])
    m.level = 1
    m.box('green', (-D / 2 + T + 0.03, 0, 1.7), (0.04, 6.0, 1.4))
    m.box('wood_dark', (-D / 2 + T + 0.05, 0, 0.95), (0.1, 6.2, 0.08))
    m.box('wood', (-D / 2 + 1.6, 0, 0.4), (0.7, 1.4, 0.8), solid=True)
    desks(m, -D / 2 + 3.0, D / 2 - 2.6, -W / 2 + 1.0, -1.6, 4, 4)
    desks(m, -D / 2 + 3.0, D / 2 - 2.6, 1.6, W / 2 - 1.4, 4, 3)
    for i in range(6):
        m.box('metal', (D / 2 - 0.5, -W / 2 + 0.6 + i * 0.6, 0.9), (0.5, 0.55, 1.8))
    m.anchors['seat'] = ((-1.0, 0.0), 90)
    top = stack(m, D, W, FH, 3, 'cream', 'plaster', 'wood', 'white', lift, labels, lambda mm, k: classroom_floor(mm, k, D, W), roof_mat='roof_red')
    m.roof = True
    m.box('cream', (front - 1.0, 0, top + 1.8), (2.4, 3.2, 3.6))
    m.hip_roof('roof_red', (front - 1.0, 0, top + 3.6), 2.4, 3.2, 1.6, 0.3)
    m.cyl('white', (front + 0.22, 0, top + 2.0), 0.95, 0.08, 24, axis='X')
    m.cyl('black', (front + 0.28, 0, top + 2.0), 0.08, 0.06, 8, axis='X')
    m.roof = False
    m.level = 2
    m.box('cream', (front + 0.06, 0, FH + 1.1), (0.1, 4.0, 1.0))
    m.text('學校', 'navy', (front + 0.14, 0, FH + 1.1), 0.6)
    m.level = 0
    for y in (-3.2, 3.2):
        m.box('brick', (front + 4.5, y, 1.0), (0.7, 0.7, 2.0))
        m.box('stone', (front + 4.5, y, 2.05), (0.85, 0.85, 0.12))
    m.railing(front + 4.5, -7.8, front + 4.5, -3.6, 0, 1.4, 'black', 8)
    m.railing(front + 4.5, 3.6, front + 4.5, 7.8, 0, 1.4, 'black', 8)
    m.cyl('metal', (front + 3.0, -5.5, 4.0), 0.06, 8.0, 8)
    m.box('red', (front + 3.0, -6.2, 7.4), (0.02, 1.3, 0.85))
    for y in (-6.5, 6.5):
        m.tree(front + 2.0, y, 5.0, 1.5)
    m.door_at = Vector((front + 2.6, 0, 0))
    return m


def dorm(Model, ident, title, accent, roof):
    m = Model(ident)
    D, W, FH = 9.0, 15.0, 3.1
    m.level = 1
    front = shell(m, D, W, FH, 'white', 'cream', 'wood', door_w=2.2, front_windows=(-5.2, -2.6, 2.6, 5.2), side_windows=2, roof=False)
    labels = ['1F 交誼廳', '2F 寢室', '3F 寢室', '4F 寢室']
    lift = (2.0, W / 2 - T - 1.1)
    ground_lift(m, lift, FH, labels[0])
    m.level = 1
    colour = accent if accent in ('pink', 'sky') else 'cloth_blue'
    sofa(m, -2.6, -2.0, 0, 2.4, colour)
    sofa(m, -1.0, -4.2, 1, 2.0, colour)
    m.box('wood', (-1.4, -2.0, 0.4), (1.0, 1.6, 0.05))
    m.box('black', (-D / 2 + T + 0.1, 3.0, 1.4), (0.08, 1.8, 1.0))
    m.box('wood_dark', (-D / 2 + 0.6, 3.0, 0.35), (0.6, 2.0, 0.7), solid=True)
    m.box('red', (D / 2 - T - 0.5, -W / 2 + T + 0.6, 0.95), (0.8, 0.9, 1.9), solid=True)
    m.box('glass', (D / 2 - T - 0.08, -W / 2 + T + 0.6, 1.25), (0.02, 0.6, 0.8))
    pendant(m, -1.4, -2.0, FH, 0.5)
    m.anchors['lounge'] = ((0.8, 1.6), 180)
    m.anchors.update(dorm_stairs(m, 1, D, W, FH))
    top = stack(m, D, W, FH, 4, 'white', 'cream', 'wood', 'white', lift, labels, lambda mm, k: dorm_floor(mm, k, D, W, accent),
                roof_mat=roof, balcony=accent, window_cols=4)
    m.level = 0
    m.box(accent, (front + 1.0, 0, 2.85), (2.0, 3.6, 0.2))
    for y in (-1.6, 1.6):
        m.cyl('white', (front + 1.9, y, 1.4), 0.1, 2.8, 10)
    sign_on(m, front, title, 2.75, max(3.4, 0.55 * len(title) + 0.4), 'navy', 'white')
    for i in range(5):
        y = -6.6 + i * 0.7
        m.cyl('black', (front + 1.6, y, 0.33), 0.32, 0.04, 14, axis='X')
        m.box('metal', (front + 1.6, y, 0.55), (0.8, 0.03, 0.03))
    m.door_at = Vector((front + 2.6, 0, 0))
    return m


def lodge(Model):
    m = Model('lodge')
    D, W, FH = 9.0, 11.0, 3.2
    m.level = 1
    front = shell(m, D, W, FH, 'plaster', 'cream', 'wood_dark', door_w=2.2, front_windows=(-3.8, 3.8), side_windows=2, frame='wood_dark', roof=False)
    labels = ['1F 大廳', '2F 休息室']
    lift = (-D / 2 + T + 0.8, 3.8)
    ground_lift(m, lift, FH, labels[0])
    m.level = 1
    counter(m, -D / 2 + 1.0, -2.5, 0, 3.0, 'wood_light', 'wood_dark')
    sofa(m, 1.6, 4.4, 3, 2.2, 'cloth_indigo')
    sofa(m, 1.6, -4.4, 1, 2.2, 'cloth_indigo')
    plant(m, front - 0.6, -W / 2 + 0.6)
    pendant(m, 0, 0, FH, 0.5)
    m.anchors['seat'] = ((0.2, 0.9), 0)
    top = stack(m, D, W, FH, 2, 'plaster', 'cream', 'wood', 'wood_dark', lift, labels, lambda mm, k: lodge_floor(mm, k, D, W), roof=False)
    m.roof = True
    m.hip_roof('roof_dark', (0, 0, top), D, W, 2.2, 0.7)
    m.roof = False
    m.level = 2
    m.box('wood_dark', (front + 0.06, 0, FH + 0.8), (0.1, 2.6, 0.7))
    m.text('會館', 'yellow', (front + 0.13, 0, FH + 0.8), 0.5)
    m.level = 0
    m.box('wood_dark', (front + 1.4, 0, 3.0), (2.8, 4.2, 0.18))
    m.gable_roof('roof_dark', (front + 1.4, 0, 3.08), 2.8, 4.2, 0.7, along_x=True, overhang=0.2)
    for y in (-1.9, 1.9):
        m.cyl('wood_dark', (front + 2.6, y, 1.5), 0.13, 3.0, 10)
    for y in (-4.5, 4.5):
        m.lantern(front + 0.3, y, 2.4, 0.24)
    m.door_at = Vector((front + 3.2, 0, 0))
    return m


def hotel(Model):
    """Glass lobby in front (walk-in), the tower behind it: the ground floor of the tower is closed, its floors 2-6
    are corridors of guest rooms reached by the lobby elevator. Pool deck on the front right."""
    m = Model('hotel')
    depth, width, FH, floors = 8.0, 13.0, 3.2, 6
    tc = -1.0
    tfront = tc + depth / 2
    labels = ['1F 大廳', '2F 客房', '3F 客房', '4F 客房', '5F 客房', '6F 景觀客房']
    m.level = 1
    shaft_x, shaft_y = depth / 2 - 0.8, -0.8          # tower-local; world x = tfront - 0.8
    solid_x1 = tc + depth / 2 - 1.6
    m.box('white', ((tc - depth / 2 + solid_x1) / 2, 0, FH / 2), (solid_x1 - (tc - depth / 2), width, FH), solid=True)
    for (y0, y1) in ((-width / 2, shaft_y - 1.1), (shaft_y + 1.1, width / 2)):
        m.box('white', (solid_x1 + 0.8, (y0 + y1) / 2, FH / 2), (1.6, y1 - y0, FH), solid=True)
    m.box('stone_dark', (tc, 0, 0.25), (depth + 0.1, width + 0.1, 0.5))
    stack(m, depth, width, FH, floors, 'white', 'cream', 'wood_light', 'navy', (shaft_x, shaft_y), labels,
          lambda mm, k: hotel_floor(mm, k, depth, width), roof=False, balcony='glass', dx=tc, lift_facing=-1)
    top = floors * FH
    m.roof = True
    m.box('navy', (tc, 0, top + 0.4), (depth + 0.4, width + 0.4, 0.8))
    m.text('SPA HOTEL', 'yellow', (tfront + 0.12, 0, top - 1.0), 1.0)
    m.roof = False
    # glass lobby: x tfront..tfront+5, y -6..2, door in the middle of its front; its elevator on the tower wall
    lx0, lx1, ly0, ly1, H = tfront, tfront + 5.0, -6.0, 2.0, 4.4
    cx, cy = (lx0 + lx1) / 2, (ly0 + ly1) / 2
    door_w = 2.4
    m.level = 1
    m.box('tile', (cx, cy, 0.03), (lx1 - lx0, ly1 - ly0, 0.06))
    for y in (ly0, ly1):
        m.box('white', (cx, y, H / 2), (lx1 - lx0, 0.2, H), solid=True)
    for (y0, y1) in ((ly0, cy - door_w / 2), (cy + door_w / 2, ly1)):
        m.box('glass_dark', (lx1 - 0.05, (y0 + y1) / 2, H / 2), (0.08, y1 - y0, H), solid=True)
        n = max(1, int((y1 - y0) / 1.2))
        for k in range(n + 1):
            m.box('white', (lx1, y0 + k * (y1 - y0) / n, H / 2), (0.12, 0.08, H))
    m.box('white', (lx1 - 0.05, cy, (H + 2.8) / 2), (0.12, door_w, H - 2.8))
    m.interiors.append(((cx, cy, H / 2), (lx1 - lx0 - 0.3, ly1 - ly0 - 0.3, H)))
    ground_lift(m, (tc + shaft_x, shaft_y), FH, labels[0])
    m.level = 1
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
    m.cyl('metal', (cx, cy, H - 0.4), 0.02, 0.6, 6)
    for k in range(8):
        a = k / 8 * math.tau
        m.sphere('lamp', (cx + math.cos(a) * 0.5, cy + math.sin(a) * 0.5, H - 0.8), 0.09)
    m.roof = True
    m.box('white', (cx, cy, H + 0.15), (lx1 - lx0 + 0.3, ly1 - ly0 + 0.3, 0.3))
    m.box('white', (cx, cy, H - 0.05), (lx1 - lx0 - 0.4, ly1 - ly0 - 0.4, 0.1))
    m.roof = False
    m.level = 0
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
