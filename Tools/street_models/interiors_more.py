"""More walk-in places for build_street_models.py: the school's ground-floor classroom, the dorm lounges, the lodge
lobby and the minshuku's tatami room. Upper floors and roofs hide with the ceiling while the player is inside."""
from mathutils import Vector

from interiors import T, shell, desks, pendant, sofa, plant, counter, sign_on


def upper_floors(m, D, W, z0, z1, wall, band='white', floor_h=3.1, frame='white', balcony=None):
    """Closed floors above a walk-in ground floor; hidden with the roof while the player is inside."""
    m.roof = True
    m.box(wall, (0, 0, (z0 + z1) / 2), (D, W, z1 - z0))
    z = z0
    while z < z1 - 0.5:
        m.floor_band(0, 0, D, W, z, band, 0.16)
        cols = max(2, int(W / 2.8))
        for i in range(cols):
            y = -W / 2 + W * (i + 0.5) / cols
            m.window(D / 2, y, z + 1.6, 1.4, 1.4, frame)
            if balcony:
                m.box('concrete', (D / 2 + 0.6, y, z + 0.05), (1.2, 2.4, 0.15))
                m.railing(D / 2 + 1.18, y - 1.2, D / 2 + 1.18, y + 1.2, z + 0.12, 1.0, balcony, 5)
        side = max(1, int(D / 2.8))
        for s in (-1, 1):
            for i in range(side):
                x = -D / 2 + D * (i + 0.5) / side
                m.side_window(x, s * W / 2, z + 1.6, 1.1, 1.2, frame, 'glass', s)
        z += floor_h
    m.roof = False


def walkin_school(Model):
    m = Model('school')
    D, W, H = 14.0, 16.0, 3.2
    front = shell(m, D, W, H, 'cream', 'plaster', 'wood', door_w=2.4, front_windows=(-5.6, -3.0, 3.0, 5.6), side_windows=4)
    upper_floors(m, D, W, H, 9.6, 'cream', floor_h=3.2)
    m.roof = True
    m.box('roof_red', (0, 0, 9.85), (D + 0.4, W + 0.4, 0.5))
    m.box('cream', (front - 1.0, 0, 11.4), (2.4, 3.2, 3.6))
    m.hip_roof('roof_red', (front - 1.0, 0, 13.2), 2.4, 3.2, 1.6, 0.3)
    m.cyl('white', (front + 0.22, 0, 11.6), 0.95, 0.08, 24, axis='X')
    m.cyl('black', (front + 0.28, 0, 11.6), 0.08, 0.06, 8, axis='X')
    m.box('cream', (front + 0.06, 0, 4.3), (0.1, 4.0, 1.0))
    m.text('學校', 'navy', (front + 0.14, 0, 4.3), 0.6)
    m.roof = False
    for y in (-3.2, 3.2):
        m.box('brick', (front + 4.5, y, 1.0), (0.7, 0.7, 2.0))
        m.box('stone', (front + 4.5, y, 2.05), (0.85, 0.85, 0.12))
    m.railing(front + 4.5, -7.8, front + 4.5, -3.6, 0, 1.4, 'black', 8)
    m.railing(front + 4.5, 3.6, front + 4.5, 7.8, 0, 1.4, 'black', 8)
    m.cyl('metal', (front + 3.0, -5.5, 4.0), 0.06, 8.0, 8)
    m.box('red', (front + 3.0, -6.2, 7.4), (0.02, 1.3, 0.85))
    for y in (-6.5, 6.5):
        m.tree(front + 2.0, y, 5.0, 1.5)
    # ground-floor classroom: blackboard at the back, desks in rows, the teacher's desk, lockers by the door
    m.box('green', (-D / 2 + T + 0.03, 0, 1.7), (0.04, 6.0, 1.4))
    m.box('wood_dark', (-D / 2 + T + 0.05, 0, 0.95), (0.1, 6.2, 0.08))
    m.box('wood', (-D / 2 + 1.6, 0, 0.4), (0.7, 1.4, 0.8), solid=True)
    desks(m, -D / 2 + 3.0, D / 2 - 2.6, -W / 2 + 1.0, -1.6, 4, 4)
    desks(m, -D / 2 + 3.0, D / 2 - 2.6, 1.6, W / 2 - 1.0, 4, 4)
    for i in range(6):
        m.box('metal', (D / 2 - 0.5, -W / 2 + 0.6 + i * 0.6, 0.9), (0.5, 0.55, 1.8))
    for x in (-3.0, 1.0):
        pendant(m, x, 0, H, 0.5)
    m.door_at = Vector((front + 2.6, 0, 0))
    m.anchors['seat'] = ((-1.0, 0.0), 90)  # centre aisle, pair layout toward the front
    return m


def walkin_dorm(Model, ident, title, accent, roof):
    m = Model(ident)
    D, W, H = 9.0, 15.0, 3.1
    front = shell(m, D, W, H, 'white', 'cream', 'wood', door_w=2.2, front_windows=(-5.2, -2.6, 2.6, 5.2), side_windows=2)
    upper_floors(m, D, W, H, 12.4, 'white', band=accent, balcony=accent)
    m.roof = True
    m.box(roof, (0, 0, 12.7), (D + 0.4, W + 0.4, 0.6))
    m.roof = False
    m.box(accent, (front + 1.0, 0, 2.85), (2.0, 3.6, 0.2))
    for y in (-1.6, 1.6):
        m.cyl('white', (front + 1.9, y, 1.4), 0.1, 2.8, 10)
    sign_on(m, front, title, 3.4, 3.6, 'navy', 'white')
    for i in range(5):
        y = 3.5 + i * 0.7
        m.cyl('black', (front + 1.6, y, 0.33), 0.32, 0.04, 14, axis='X')
        m.box('metal', (front + 1.6, y, 0.55), (0.8, 0.03, 0.03))
    # lounge: sofas round a low table facing a TV, mailboxes by the door, a vending machine
    colour = accent if accent in ('pink', 'sky') else 'cloth_blue'
    sofa(m, -2.6, -2.0, 0, 2.4, colour)
    sofa(m, -1.0, -4.2, 1, 2.0, colour)
    m.box('wood', (-1.4, -2.0, 0.4), (1.0, 1.6, 0.05))
    m.box('black', (-D / 2 + T + 0.1, 3.0, 1.4), (0.08, 1.8, 1.0))
    m.box('wood_dark', (-D / 2 + 0.6, 3.0, 0.35), (0.6, 2.0, 0.7), solid=True)
    for i in range(4):
        for j in range(3):
            m.box('metal', (D / 2 - T - 0.2, W / 2 - 1.0 - i * 0.4, 0.9 + j * 0.35), (0.3, 0.38, 0.33))
    m.box('red', (D / 2 - T - 0.5, -W / 2 + T + 0.6, 0.95), (0.8, 0.9, 1.9), solid=True)
    m.box('glass', (D / 2 - T - 0.08, -W / 2 + T + 0.6, 1.25), (0.02, 0.6, 0.8))
    plant(m, -D / 2 + 0.6, -W / 2 + 0.7)
    pendant(m, -1.4, -2.0, H, 0.5)
    pendant(m, 1.0, 2.5, H, 0.5)
    m.door_at = Vector((front + 2.6, 0, 0))
    m.anchors['lounge'] = ((0.8, 1.6), 180)
    return m


def walkin_lodge(Model):
    m = Model('lodge')
    D, W, H = 9.0, 11.0, 3.2
    front = shell(m, D, W, H, 'plaster', 'cream', 'wood_dark', door_w=2.2, front_windows=(-3.8, 3.8), side_windows=2, frame='wood_dark')
    upper_floors(m, D, W, H, 6.4, 'plaster', band='wood_dark', floor_h=3.2, frame='wood_dark')
    m.roof = True
    m.hip_roof('roof_dark', (0, 0, 6.4), D, W, 2.2, 0.7)
    m.box('wood_dark', (front + 0.06, 0, 4.0), (0.1, 2.6, 0.7))
    m.text('會館', 'yellow', (front + 0.13, 0, 4.0), 0.5)
    m.roof = False
    m.box('wood_dark', (front + 1.4, 0, 3.0), (2.8, 4.2, 0.18))
    m.gable_roof('roof_dark', (front + 1.4, 0, 3.08), 2.8, 4.2, 0.7, along_x=True, overhang=0.2)
    for y in (-1.9, 1.9):
        m.cyl('wood_dark', (front + 2.6, y, 1.5), 0.13, 3.0, 10)
    for y in (-4.5, 4.5):
        m.lantern(front + 0.3, y, 2.4, 0.24)
    # lobby: reception counter, a folding screen, sofas, plants
    counter(m, -D / 2 + 1.0, -2.5, 0, 3.0, 'wood_light', 'wood_dark')
    m.box('wood', (-D / 2 + 0.25, 2.5, 1.2), (0.06, 3.0, 2.2))
    for i in range(4):
        m.box('cream', (-D / 2 + 0.3, 1.3 + i * 0.8, 1.2), (0.02, 0.7, 2.0))
    sofa(m, 1.6, 4.4, 3, 2.2, 'cloth_indigo')
    sofa(m, 1.6, -4.4, 1, 2.2, 'cloth_indigo')
    plant(m, front - 0.6, -W / 2 + 0.6)
    plant(m, front - 0.6, W / 2 - 0.6)
    pendant(m, 0, 0, H, 0.5)
    m.door_at = Vector((front + 3.2, 0, 0))
    m.anchors['seat'] = ((0.2, 0.9), 0)
    return m


def walkin_minshuku(Model):
    m = Model('minshuku')
    D, W, H = 9.0, 10.0, 3.0
    front = shell(m, D, W, H, 'plaster', 'plaster', 'sand', door_w=1.8, door_h=2.2, front_windows=(-3.0, 3.0),
                  side_windows=0, frame='wood_dark', ceiling='wood_light')
    m.roof = True
    m.gable_roof('roof_dark', (0, 0, H), D, W, 1.0, along_x=False, overhang=0.7, thick=0.1)
    m.box('plaster', (-0.8, 0, H + 1.5), (D - 2.6, W - 1.0, 2.8))
    m.gable_roof('roof_dark', (-0.8, 0, H + 2.9), D - 2.6, W - 1.0, 1.6, along_x=False, overhang=0.6)
    for s in (-1, 1):
        m.gable_end('plaster', (-0.8, s * (W - 1.0) / 2, H + 2.9), D - 2.6, 1.6, along_x=False)
    m.roof = False
    for y in (-W / 2, -W / 6, W / 6, W / 2):
        m.box('wood_dark', (front + 0.04, y, H / 2), (0.08, 0.18, H))
    m.box('wood_light', (front + 0.6, 0, 0.35), (1.2, W - 0.4, 0.1))
    for i in range(3):
        m.box('cloth_indigo', (front + 0.14, -0.55 + i * 0.55, 1.75), (0.02, 0.5, 0.7))
    sign_on(m, front, '民宿', 2.65, 1.6, 'black', 'wood_light')
    for y in (-1.4, 1.4):
        m.lantern(front + 0.5, y, 2.1, 0.2)
    for i in range(4):
        m.cyl('stone', (front + 1.6 + i * 0.7, 0.2 * (-1) ** i, 0.03), 0.3, 0.06, 10)
    m.tree(front + 1.8, -4.6, 4.0, 1.1, 'leaf_light')
    # tatami room: mats with dark borders, an alcove with a scroll, shoji panels, a low table with cushions
    for i in range(4):
        for j in range(3):
            x = -D / 2 + 1.1 + i * 1.8
            y = -W / 2 + 1.6 + j * 3.0
            m.box('wood_dark', (x, y, 0.065), (1.8, 2.95, 0.02))
            m.box('mint', (x, y, 0.08), (1.72, 2.87, 0.02))
    m.box('wood', (-D / 2 + 0.5, 2.6, 0.2), (0.6, 2.0, 0.4), solid=True)
    m.box('white', (-D / 2 + 0.25, 2.6, 1.6), (0.02, 0.6, 1.4))
    m.box('wood_dark', (-D / 2 + 0.24, 2.6, 2.3), (0.03, 0.7, 0.05))
    m.sphere('green', (-D / 2 + 0.5, 2.0, 0.6), 0.2)
    for i in range(5):
        m.box('awning_b', (-D / 2 + T + 0.02, -W / 2 + 1.0 + i * 1.0, 1.4), (0.02, 0.9, 2.2))
        m.box('wood_dark', (-D / 2 + T + 0.03, -W / 2 + 1.0 + i * 1.0, 1.4), (0.02, 0.04, 2.2))
    m.box('wood_dark', (1.5, 3.0, 0.32), (1.0, 1.4, 0.06))
    for s in (-1, 1):
        m.box('red', (1.5 + s * 0.8, 3.0, 0.1), (0.5, 0.5, 0.08))
    m.cyl('white', (1.5, 3.2, 0.4), 0.06, 0.1, 10)
    pendant(m, 0, 0, H, 0.4)
    m.door_at = Vector((front + 3.6, 0, 0))
    m.anchors['seat'] = ((0.4, -0.6), 0)
    from multi_floor import futon
    m.anchors['bed1_0'] = futon(m, -2.3, -3.2)
    m.anchors['bed1_1'] = futon(m, -2.3, -2.0)
    return m
