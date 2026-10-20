"""Walkable upper floors for the big buildings (school, dorms, lodge, hotel tower) in build_street_models.py.

The ground floor is the existing walk-in shell (level 1, built with roof=False); `stack` adds floors 2..N above it,
each a room with outer walls, windows inside and out, an elevator with a 'liftK:label' anchor, and contents from a
callback. Floors are built at height 0 and raised (dept_store._raise), so furniture helpers stay simple.
"""
import math

from mathutils import Matrix, Vector

from interiors import T, desks, pendant, plant, sofa, table, bookshelf
from dept_store import _raise
import lift


def elevator(m, x, y, H, label, facing=1):
    """Elevator column at (x, y), doors toward +X (facing=1) or -X; returns the anchor spot in front."""
    return lift.shaft(m, x, y, H, facing)


def stack(m, D, W, FH, floors, wall, inner, floor_mat, frame, lift_xy, labels, contents, roof_mat='white', balcony=None, window_cols=None, dx=0.0, lift_facing=1, roof=True):
    """Floors 2..floors on top of a ground floor of height FH. lift_xy: elevator column position (local)."""
    hx, hy = D / 2, W / 2
    for k in range(2, floors + 1):
        z = (k - 1) * FH
        m.level = k
        o0, b0 = len(m.objects), len(m.boxes)
        lift.slab_with_hole(m, floor_mat, 0, 0, D - 0.05, W - 0.05, -0.1, 0.26, lift_xy)
        m.box(wall, (-hx + T / 2, 0, FH / 2), (T, W, FH), solid=True)
        m.box(wall, (hx - T / 2, 0, FH / 2), (T, W, FH), solid=True)
        for s in (-1, 1):
            m.box(wall, (0, s * (hy - T / 2), FH / 2), (D, T, FH), solid=True)
        m.box(inner, (-hx + T + 0.01, 0, FH / 2), (0.02, W - 2 * T, FH - 0.3))
        m.box(inner, (hx - T - 0.01, 0, FH / 2), (0.02, W - 2 * T, FH - 0.3))
        for s in (-1, 1):
            m.box(inner, (0, s * (hy - T - 0.01), FH / 2), (D - 2 * T, 0.02, FH - 0.3))
        m.floor_band(0, 0, D, W, 0.0, frame, 0.16)
        cols = window_cols or max(2, int(W / 2.8))
        for i in range(cols):
            y = -hy + W * (i + 0.5) / cols
            m.window(hx, y, 1.6, 1.4, 1.4, frame)
            m.window(hx - T, y, 1.6, 1.4, 1.4, frame, facing=-1)
            if balcony:
                m.box('concrete', (hx + 0.6, y, 0.05), (1.2, 2.4, 0.15))
                m.railing(hx + 1.18, y - 1.2, hx + 1.18, y + 1.2, 0.12, 1.0, balcony, 5)
        side = max(1, int(D / 2.8))
        for s in (-1, 1):
            for i in range(side):
                x = -hx + D * (i + 0.5) / side
                m.side_window(x, s * hy, 1.6, 1.1, 1.2, frame, 'glass', s)
                m.side_window(x, s * (hy - T), 1.6, 1.1, 1.2, frame, 'glass', -s)
        spot = elevator(m, lift_xy[0], lift_xy[1], FH, labels[k - 1], lift_facing)
        anchors = contents(m, k) or {}
        _raise(m, o0, b0, z)
        if dx:
            for o in m.objects[o0:]:
                o.matrix_world = Matrix.Translation((dx, 0, 0)) @ o.matrix_world
            for i in range(b0, len(m.boxes)):
                c, sz = m.boxes[i]
                m.boxes[i] = (Vector((c.x + dx, c.y, c.z)), sz)
        for name, (pos, yaw) in anchors.items():
            m.anchors[name] = ((pos[0] + dx, pos[1], z + (pos[2] if len(pos) > 2 else 0.0)), yaw)
        m.interiors.append(((dx, 0, z + FH / 2), (D - 2 * T, W - 2 * T, FH)))
        m.anchors['lift%d:%s' % (k, labels[k - 1])] = ((spot[0] + dx, spot[1], z), 0 if lift_facing > 0 else 180)
    m.level = 0
    top = floors * FH
    if roof:
        m.roof = True
        m.box(roof_mat, (dx, 0, top + 0.15), (D + 0.4, W + 0.4, 0.3))
        m.roof = False
    return top


def ground_lift(m, lift_xy, FH, label, facing=1):
    """The ground floor's elevator (level 1) and its stop."""
    m.level = 1
    spot = elevator(m, lift_xy[0], lift_xy[1], FH, label, facing)
    m.anchors['lift1:%s' % label] = ((spot[0], spot[1], 0.0), 0 if facing > 0 else 180)


def bed_anchor(cx, cy, top, head):
    """Where the sleeping clip's actor stands on a bed centred at (cx, cy) with its top at `top` and the head end
    toward `head` (a unit 2D direction, Blender local), 0.15 m off the bed centre (as the facility beds are built). Returns ((x, y, z), yaw in Blender degrees)."""
    hx, hy = head
    # UE yaw that lays the clip's body with its head at the pillow end (checked in game: the clip's head is at
    # the actor's -Y, so the actor's +Y points away from the head)
    theta = math.degrees(math.atan2(hx, hy))
    return ((cx - hx * 0.15, cy - hy * 0.15, top), -theta)


def futon(m, x, y, along_x=True):
    """A futon on the floor with a pillow at the -X (or -Y) end; returns its bed anchor."""
    sx, sy = (2.0, 1.0) if along_x else (1.0, 2.0)
    m.box('white', (x, y, 0.06), (sx, sy, 0.1))
    m.box('sky', (x + (0.15 if along_x else 0), y + (0 if along_x else 0.15), 0.12), (sx - 0.4, sy - 0.05, 0.03) if along_x else (sx - 0.05, sy - 0.4, 0.03))
    m.box('white', (x - 0.8 if along_x else x, y if along_x else y - 0.8, 0.15), (0.3, 0.5, 0.1) if along_x else (0.5, 0.3, 0.1))
    return bed_anchor(x, y, 0.13, (-1, 0) if along_x else (0, -1))


# --------------------------------------------------------------------------------------------- floor contents
def classroom_floor(m, k, D, W):
    hx, hy = D / 2, W / 2
    if k == 3:  # music room: a piano, chairs in rows, a stage
        m.box('black', (-hx + 2.0, -3.0, 0.5), (1.6, 1.0, 1.0), solid=True)
        m.box('black', (-hx + 2.0, -3.0, 1.02), (1.6, 1.4, 0.04))
        m.box('white', (-hx + 2.75, -3.0, 0.78), (0.1, 1.3, 0.04))
        m.box('wood', (-hx + 1.4, 0, 0.15), (2.0, 8.0, 0.3))
        for r in range(4):
            for c in range(8):
                x = -hx + 4.5 + r * 1.3
                y = -hy + 2.0 + c * (W - 4.0) / 7
                m.box('red', (x, y, 0.44), (0.42, 0.42, 0.06))
                m.box('red', (x - 0.2, y, 0.7), (0.05, 0.42, 0.5))
        m.box('wood_dark', (-hx + T + 0.05, 4.0, 1.8), (0.06, 3.0, 1.2))
        return {'music': ((0.5, 1.5), 180)}
    m.box('green', (-hx + T + 0.03, 0, 1.7), (0.04, 6.0, 1.4))
    m.box('wood_dark', (-hx + T + 0.05, 0, 0.95), (0.1, 6.2, 0.08))
    m.box('wood', (-hx + 1.6, 0, 0.4), (0.7, 1.4, 0.8), solid=True)
    desks(m, -hx + 3.0, hx - 2.6, -hy + 1.0, -1.6, 4, 4)
    desks(m, -hx + 3.0, hx - 2.6, 1.6, hy - 1.0, 4, 4)
    return {'class%d' % k: ((-1.0, 0.0), 90)}


def dorm_stairs(m, k, D, W, FH, mat='stone'):
    """Dorm stairs in the corridor at the -Y end (the front is +X): steps for looks and collision, and the landing anchor
    'stair<k>' the game uses to climb (the same walk-up as the campus buildings)."""
    hy = W / 2
    y = -hy + T + 1.2
    x_low = 3.4
    for i in range(10):
        m.box(mat, (x_low - i * 0.3, y, (i + 0.5) * FH / 10 / 2), (0.3, 2.0, (i + 1) * FH / 10))
    m.boxes.append((Vector((x_low - 1.35, y, FH / 2)), Vector((3.0, 2.2, FH))))
    m.box('metal', (x_low - 1.35, y + 1.1, 1.0), (3.0, 0.05, 1.0))
    return {'stair%d' % k: ((2.1, y + 1.9), 0)}


def dorm_floor(m, k, D, W, accent):
    """Corridor along the front, rooms behind with beds and desks."""
    hx, hy = D / 2, W / 2
    wall_x = -0.5
    beds = {}
    for i in range(4):
        y0 = -hy + T + i * (W - 2 * T) / 4
        y1 = y0 + (W - 2 * T) / 4
        # partition between rooms, the corridor wall with a door gap, a bed, a desk
        m.box('cream', (-hx / 2 - 0.2, y1, 1.5), (hx - 0.6, 0.12, 3.0), solid=True) if i < 3 else None
        m.box('cream', (wall_x, (y0 + y1) / 2 - 0.7, 1.5), (0.12, (y1 - y0) - 1.6, 3.0), solid=True)
        m.box(accent, (wall_x + 0.07, (y0 + y1) / 2 + 0.85, 1.05), (0.02, 0.85, 2.1))
        m.box('white', (-hx + 1.1, y0 + 0.6, 0.25), (1.9, 0.9, 0.5))
        m.box(accent, (-hx + 1.1, y0 + 0.6, 0.52), (1.8, 0.85, 0.06))
        m.box('white', (-hx + 0.4, y0 + 0.6, 0.6), (0.3, 0.5, 0.1))
        beds['bed%d_%d' % (k, i)] = bed_anchor(-hx + 1.1, y0 + 0.6, 0.55, (-1, 0))
        m.box('wood', (-hx + 0.6, y1 - 0.5, 0.75), (0.6, 1.0, 0.04))
        m.box('wood_dark', (-hx + 0.35, y1 - 0.5, 1.4), (0.04, 0.8, 0.6))
    beds.update(dorm_stairs(m, k, D, W, 3.1))
    return beds


def hotel_floor(m, k, D, W):
    hx, hy = D / 2, W / 2
    beds = {}
    for i in range(3):
        y0 = -hy + T + i * (W - 2 * T) / 3
        y1 = y0 + (W - 2 * T) / 3
        if i < 2:
            m.box('cream', (-1.0, y1, 1.5), (D - 2.6, 0.12, 3.0), solid=True)
        m.box('white', (-hx + 1.4, (y0 + y1) / 2, 0.3), (2.0, 2.2, 0.6))
        m.box('cloth_blue', (-hx + 1.4, (y0 + y1) / 2, 0.62), (1.9, 2.1, 0.06))
        m.box('wood_dark', (-hx + 0.32, (y0 + y1) / 2, 0.9), (0.06, 2.2, 1.2))
        for s in (-0.7, 0.7):
            m.box('white', (-hx + 0.8, (y0 + y1) / 2 + s, 0.7), (0.4, 0.5, 0.12))
        beds['bed%d_%d' % (k, i)] = bed_anchor(-hx + 1.4, (y0 + y1) / 2, 0.65, (-1, 0))
    sofa(m, hx - 1.2, 2.8, 2, 2.0, 'navy')  # beside the elevator (shaft at hx - 0.8, -0.8)
    if k == 6:
        beds['room6'] = ((hx - 2.6, 1.2), 0)
    return beds


def lodge_floor(m, k, D, W):
    hx, hy = D / 2, W / 2
    for (x, y) in ((-2.0, -3.0), (-2.0, 3.0), (1.5, -3.0)):
        m.box('mint', (x, y, 0.07), (2.0, 2.8, 0.04))
        m.box('wood_dark', (x, y, 0.25), (0.9, 0.6, 0.06))
        for s in (-1, 1):
            m.box('red', (x + s * 0.7, y, 0.1), (0.45, 0.45, 0.08))
    sofa(m, hx - 1.0, 3.5, 2, 2.0, 'cloth_indigo')
    plant(m, hx - 0.7, -4.5)
    return {'lounge2': ((1.0, 1.4), 0), 'bed2_0': futon(m, -2.4, -0.5), 'bed2_1': futon(m, -2.4, 0.8)}
