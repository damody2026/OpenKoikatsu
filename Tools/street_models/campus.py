"""The university campus (Docs/STREET_SOCIAL_CAMPUS_PLAN.md): one five-storey building per college.

Each building: 1F lobby along the front with up to four department offices behind it (sign on each door), 2F-5F a
corridor along the front with four classrooms behind it. An elevator in one back corner, a staircase in the other;
every floor has a 'liftK:label' stop and a 'stairK' landing. Classroom anchors 'classK_R' (K floor 2..5, R room
1..4), office anchors 'office_<dept>'.
"""
import math

from mathutils import Vector

from interiors import T, desks, pendant, plant, shell
from multi_floor import ground_lift, stack
from dept_store import _raise

D, W, FH, FLOORS = 14.0, 28.0, 3.4, 5
ROOM_Y = (-11.0, -5.5, 0.0, 5.5, 11.0)  # room boundaries across the building
CORRIDOR = 2.6                           # corridor depth along the front


def _partitions(m, wall, labels=None):
    """Four rooms behind the front corridor, each with a doorway to it; optional door signs."""
    hx = D / 2
    back, front = -hx + T, hx - CORRIDOR
    for y in ROOM_Y:
        m.box(wall, ((back + front) / 2, y, FH / 2 - 0.1), ((front - back), 0.15, FH - 0.2), solid=True)
    for r in range(4):
        y0, y1 = ROOM_Y[r], ROOM_Y[r + 1]
        mid = (y0 + y1) / 2
        door = 1.3
        for a, b in ((y0, mid - door / 2), (mid + door / 2, y1)):
            m.box(wall, (front, (a + b) / 2, FH / 2 - 0.1), (0.15, b - a, FH - 0.2), solid=True)
        m.box(wall, (front, mid, FH - 0.45), (0.15, door, 0.5))
        if labels and r < len(labels) and labels[r]:
            m.box('sign', (front + 0.1, mid, 2.45), (0.04, 1.6, 0.36))
            m.text(labels[r], 'navy', (front + 0.13, mid, 2.45), 0.16)


def _classroom(m, r, k):
    """Blackboard on the back wall and desks facing it; returns the anchor."""
    hx = D / 2
    y0, y1 = ROOM_Y[r], ROOM_Y[r + 1]
    mid = (y0 + y1) / 2
    m.box('green', (-hx + T + 0.03, mid, 1.7), (0.04, 3.6, 1.2))
    m.box('wood_dark', (-hx + T + 0.05, mid, 1.05), (0.08, 3.8, 0.06))
    m.box('wood', (-hx + 1.4, mid, 0.4), (0.6, 1.2, 0.8), solid=True)
    desks(m, -hx + 2.8, hx - CORRIDOR - 1.4, y0 + 0.9, y1 - 0.9, 4, 2)
    return {'class%d_%d' % (k, r + 1): ((-hx + 3.2, mid), 180)}


def _office(m, r, dept):
    hx = D / 2
    y0, y1 = ROOM_Y[r], ROOM_Y[r + 1]
    mid = (y0 + y1) / 2
    m.box('wood', (-hx + 2.4, mid, 0.5), (0.8, 3.0, 1.0), solid=True)          # counter
    m.box('white', (-hx + 2.4, mid, 1.03), (0.9, 3.1, 0.06))
    for dy in (-1.0, 1.0):
        m.box('black', (-hx + 1.6, mid + dy, 1.05), (0.3, 0.45, 0.35))         # monitors
    m.box('metal', (-hx + T + 0.3, mid, 1.0), (0.5, 3.6, 2.0), solid=True)     # filing cabinets
    plant(m, hx - CORRIDOR - 0.6, y1 - 0.6)
    return {'office_' + dept: ((-hx + 3.6, mid), 180)}


def _stairs(m, k, mat):
    """A staircase block in the -Y back corner (for looks and collision); landing anchor in front of it."""
    hx, hy = D / 2, W / 2
    x0, y = -hx + T + 1.6, -hy + T + 1.4
    for i in range(10):
        m.box(mat, (x0 + 1.6 - i * 0.3, y, (i + 0.5) * FH / 10 / 2), (0.3, 2.2, (i + 1) * FH / 10))
    m.boxes.append((Vector((x0, y, FH / 2)), Vector((3.2, 2.4, FH))))
    m.box('metal', (x0, y + 1.2, 1.0), (3.2, 0.05, 1.0))
    return {'stair%d' % k: ((x0 + 2.6, y + 0.4), 0)}


def college_building(Model, ident, title, code, wall, accent, depts, lobby='大廳・系辦公室'):
    """depts: up to four (dept_id, office label) for the 1F offices; empty slots become classrooms."""
    m = Model(ident)
    hx, hy = D / 2, W / 2
    m.level = 1
    front = shell(m, D, W, FH, wall, 'plaster', 'tile', door_w=3.0, door_h=2.8, front_windows=(-11.0, -7.0, 7.0, 11.0), side_windows=4, roof=False)
    labels = ['%dF %s' % (k, lobby if k == 1 else '教室') for k in range(1, FLOORS + 1)]
    lift_xy = (-hx + T + 0.8, hy - T - 1.1)
    ground_lift(m, lift_xy, FH, labels[0])
    m.level = 1
    office_labels = [d[1] for d in depts] + [''] * (4 - len(depts))
    _partitions(m, 'plaster', [l if l else '教室 %s-1%02d' % (code, r + 1) for r, l in enumerate(office_labels)])
    for r in range(4):
        if r < len(depts):
            m.anchors.update(_office(m, r, depts[r][0]))
        else:
            m.anchors.update(_classroom(m, r, 1))
    m.anchors.update(_stairs(m, 1, 'stone'))
    m.anchors['hall'] = ((hx - 1.3, 0.0), 180)
    m.anchors['seat'] = ((hx - 1.3, 3.0), 180)
    for y in (-8.0, 0.0, 8.0):
        pendant(m, hx - 1.3, y, FH, 0.5)

    def floor_contents(mm, k):
        _partitions(mm, 'plaster', ['%s-%d%02d' % (code, k, r + 1) for r in range(4)])
        out = {}
        for r in range(4):
            out.update(_classroom(mm, r, k))
        out.update(_stairs(mm, k, 'stone'))
        return out

    top = stack(m, D, W, FH, FLOORS, wall, 'plaster', 'tile', 'white', lift_xy, labels, floor_contents, roof_mat='concrete', window_cols=8)
    # Exterior: name band over the entrance, a canopy, the code on the corner.
    m.level = 0
    m.box(accent, (front + 0.06, 0, FH - 0.2), (0.12, 10.0, 0.6))
    m.text(title, 'white', (front + 0.14, 0, FH - 0.2), 0.42)
    m.box('white', (front + 1.3, 0, 2.9), (2.6, 5.0, 0.15))
    for y in (-2.3, 2.3):
        m.box('metal', (front + 2.5, y, 1.45), (0.12, 0.12, 2.9))
    m.box(accent, (front + 0.06, -hy + 1.6, top - 1.2), (0.12, 2.6, 1.8))
    m.text(code, 'white', (front + 0.14, -hy + 1.6, top - 1.2), 1.1)
    for y in (-9.0, 9.0):
        m.tree(front + 3.2, y, 5.0, 1.5)
    m.door_at = Vector((front + 3.0, 0, 0))
    return m


COLLEGES = {
    'campus_e1': ('工程一館 E1', 'E1', 'brick', 'red', [('me', '機械工程系 系辦'), ('mse', '材料系 系辦'), ('ce', '營建系 系辦'), ('che', '化工系 系辦')]),
    'campus_ee': ('電資館 EE', 'EE', 'cream', 'navy', [('ee', '電子系 系辦'), ('eecs_ee', '電機系 系辦'), ('csie', '資工系 系辦'), ('photonics', '光電所 所辦')]),
    'campus_ma': ('管理大樓 MA', 'MA', 'cream', 'green', [('im_ind', '工管系 系辦'), ('ba', '企管系 系辦'), ('im', '資管系 系辦'), ('finance', '財金所 所辦')]),
    'campus_t4': ('第四教學大樓 T4', 'T4', 'white', 'orange', [('design', '設計系 系辦'), ('arch', '建築系 系辦')]),
    'campus_t2': ('第二教學大樓 T2', 'T2', 'cream', 'purple', [('afl', '應外系 系辦'), ('dle', '數位學習所 所辦')]),
    'campus_ib': ('國際大樓 IB', 'IB', 'white', 'sky', [('ast', '應科所 所辦'), ('bme', '醫工所 所辦'), ('color', '色彩所 所辦')]),
    'campus_t1': ('第一教學大樓 T1', 'T1', 'brick', 'navy', [('general', '通識中心 辦公室')]),
    'campus_rb': ('綜合研究大樓 RB', 'RB', 'stone', 'mint', [('smart_mfg', '智慧製造所 所辦'), ('ai', 'AI 所 所辦')]),
}


def build_gate(Model):
    """The main gate on the south edge: two stone pillars and a name beam over the road (open, no collision on it),
    a guard booth to one side, flower beds and the campus map board."""
    m = Model('campus_gate')
    m.box('sand', (0, 0, 0.012), (12.0, 22.0, 0.02))
    for y in (-6.5, 6.5):
        m.box('stone', (0, y, 2.6), (1.4, 1.4, 5.2), solid=True)
        m.box('concrete', (0, y, 5.35), (1.7, 1.7, 0.3))
    m.box('stone', (0, 0, 5.0), (1.0, 14.4, 0.9))
    m.text('星街科技大學', 'white', (0.52, 0, 5.0), 0.62)
    # guard booth
    m.box('white', (-1.5, -9.6, 1.4), (2.4, 2.0, 2.8), solid=True)
    m.box('glass', (-0.28, -9.6, 1.6), (0.05, 1.6, 1.0))
    m.box('navy', (-1.5, -9.6, 2.9), (2.7, 2.3, 0.2))
    # campus map board
    m.box('wood_dark', (2.2, 9.6, 1.2), (0.2, 2.6, 1.8), solid=True)
    m.box('white', (2.32, 9.6, 1.3), (0.04, 2.3, 1.4))
    m.text('校園地圖', 'navy', (2.36, 9.6, 1.85), 0.18)
    for (x, y) in ((3.5, -8.0), (3.5, 6.0)):
        m.cyl('wood_dark', (x, y, 0.2), 1.3, 0.4, 24)
        m.sphere('leaf', (x, y, 0.5), 0.9, (1, 1, 0.5), 10)
    for y in (-10.5, 10.5):
        m.tree(-3.5, y, 5.5, 1.6)
        m.street_lamp(4.5, y * 0.7)
    m.door_at = Vector((5.0, 0, 0))
    m.anchors['gate'] = ((3.0, 1.5), 180)
    m.anchors['guard'] = ((0.6, -9.6), 0)
    return m


def build_sports(Model):
    """Sports ground west of the classrooms: a 200 m style track round a football pitch, basketball courts and stands."""
    m = Model('campus_sports')
    L, W = 34.0, 56.0  # x depth, y width
    m.box('grass', (0, 0, 0.01), (L, W, 0.02))
    # track: straights along y, bends approximated by rings of segments
    tw, inner_r = 3.6, 7.5
    straight = 16.0
    for x in (-(inner_r + tw / 2), inner_r + tw / 2):
        m.box('track', (x, 0, 0.016), (tw, straight * 2, 0.03))
    for side in (-1, 1):
        cy = side * straight
        for k in range(18):
            a0 = k / 18 * math.pi
            a = a0 + math.pi / 36
            r = inner_r + tw / 2
            x, y = math.cos(a) * r, cy + side * math.sin(a) * r
            m.box('track', (x, y, 0.017), (tw, 2.0 * r * math.pi / 18 + 0.3, 0.03), rot_z=(a if side > 0 else -a))
    # pitch markings and goals
    m.box('white', (0, 0, 0.03), (inner_r * 2 - 1.0, 0.08, 0.01))
    m.cyl('white', (0, 0, 0.03), 2.2, 0.01, 32)
    m.cyl('grass', (0, 0, 0.035), 2.1, 0.012, 32)
    for y in (-straight - 3.5, straight + 3.5):
        m.box('white', (0, y, 1.1), (5.0, 0.12, 0.12))
        for x in (-2.5, 2.5):
            m.cyl('white', (x, y, 0.55), 0.06, 1.1, 8)
    # basketball courts on the -x side, stands on the +x side
    for y in (-14.0, 0.0, 14.0):
        m.box('concrete', (L / 2 - 3.2, y, 0.02), (5.0, 12.0, 0.03))
        m.box('white', (L / 2 - 3.2, y, 0.04), (5.0, 0.06, 0.01))
        for yy in (y - 5.6, y + 5.6):
            m.cyl('metal', (L / 2 - 3.2, yy, 1.5), 0.07, 3.0, 8)
            m.box('white', (L / 2 - 3.2, yy - 0.2 if yy > y else yy + 0.2, 3.0), (1.2, 0.05, 0.8))
            m.cyl('orange', (L / 2 - 3.2, yy - 0.5 if yy > y else yy + 0.5, 2.7), 0.25, 0.03, 16)
    for i in range(4):
        m.box('concrete', (-L / 2 + 1.2 + i * 0.7, 0, 0.25 + i * 0.4), (0.7, 30.0, 0.5 + i * 0.8), solid=(i == 3))
        m.box('sky' if i % 2 else 'cloth_blue', (-L / 2 + 1.05 + i * 0.7, 0, 0.52 + i * 0.8), (0.4, 30.0, 0.06))
    m.box('white', (-L / 2 + 2.0, 0, 4.4), (4.0, 30.0, 0.12))
    for y in (-26.0, 26.0):
        m.tree(-L / 2 + 2.0, y, 6.0, 1.8)
        m.street_lamp(L / 2 - 0.8, y * 0.8, 6.0)
    m.door_at = Vector((L / 2 + 1.0, 0, 0))
    m.anchors['field'] = ((0.0, 4.0), 90)
    m.anchors['track'] = ((inner_r + tw / 2, -6.0), 90)
    m.anchors['court'] = ((L / 2 - 3.2, 2.0), 180)
    m.anchors['seat'] = ((-L / 2 + 3.4, 3.0), 0)
    return m


ADMIN = ('行政大樓 AD', 'AD', 'white', 'navy', [('registrar', '註冊組'), ('aa', '教務處'), ('student', '學務處')])


def builders(Model):
    out = {ident: (lambda i=ident, a=args: college_building(Model, i, *a)) for ident, args in COLLEGES.items()}
    out['campus_ad'] = lambda: college_building(Model, 'campus_ad', *ADMIN, lobby='大廳・註冊組・教務處・學務處')
    out['campus_gate'] = lambda: build_gate(Model)
    out['campus_sports'] = lambda: build_sports(Model)
    return out
