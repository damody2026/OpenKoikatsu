"""Walk-in elevator shafts for the multi-storey places.

A shaft is 1.6 m (along X) x 2.2 m with 0.1 m walls; the landing opening (1.1 m x 2.3 m) is on the +X side when
facing=1, -X when facing=-1, and may differ per floor. The car (a floor, ceiling and a button panel), and the landing
doors that close the opening, are the game's (OKStreetSocialElevator): they move, so they are not in the model.
Floor slabs above the ground leave the shaft open (slab_with_hole) so the car can pass through them.
"""
SX, SY, WALL = 1.6, 2.2, 0.1
OPEN_W, OPEN_H = 1.1, 2.3


def shaft(m, x, y, H, facing=1, mat='concrete', trim='metal'):
    """One floor of a shaft centred at (x, y), floor at 0, height H. Returns the landing spot in front of it."""
    f = facing
    hx, hy = SX / 2, SY / 2
    m.box(mat, (x - f * (hx - WALL / 2), y, H / 2), (WALL, SY, H), solid=True)                     # back
    for s in (-1, 1):
        m.box(mat, (x, y + s * (hy - WALL / 2), H / 2), (SX, WALL, H), solid=True)                 # sides
    side = (SY - OPEN_W) / 2
    for s in (-1, 1):                                                                            # front, beside the opening
        m.box(mat, (x + f * (hx - WALL / 2), y + s * (OPEN_W / 2 + side / 2), H / 2), (WALL, side, H), solid=True)
    m.box(mat, (x + f * (hx - WALL / 2), y, (OPEN_H + H) / 2), (WALL, OPEN_W, H - OPEN_H))      # above the opening
    # landing face: a steel frame, the floor indicator above and the call button beside the opening
    for s in (-1, 1):
        m.box(trim, (x + f * (hx + 0.01), y + s * (OPEN_W / 2 + 0.04), OPEN_H / 2), (0.03, 0.08, OPEN_H))
    m.box(trim, (x + f * (hx + 0.01), y, OPEN_H + 0.04), (0.03, OPEN_W + 0.16, 0.08))
    m.box('black', (x + f * (hx + 0.02), y, OPEN_H + 0.3), (0.03, 0.5, 0.2))
    m.box('lamp', (x + f * (hx + 0.025), y, OPEN_H + 0.3), (0.02, 0.16, 0.08))
    m.box(trim, (x + f * (hx + 0.02), y - (OPEN_W / 2 + 0.3), 1.1), (0.03, 0.12, 0.22))
    m.box('lamp', (x + f * (hx + 0.035), y - (OPEN_W / 2 + 0.3), 1.12), (0.02, 0.05, 0.05))
    return (x + f * 1.6, y)


def slab_with_hole(m, mat, cx, cy, sx, sy, z, thick, hole):
    """A floor slab centred at (cx, cy) of size sx x sy with the shaft (hole = (x, y)) left open."""
    hx0, hx1 = hole[0] - SX / 2, hole[0] + SX / 2
    hy0, hy1 = hole[1] - SY / 2, hole[1] + SY / 2
    x0, x1, y0, y1 = cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2
    pieces = [(x0, hx0, y0, y1), (hx1, x1, y0, y1), (hx0, hx1, y0, hy0), (hx0, hx1, hy1, y1)]
    for (a, b, c, d) in pieces:
        if b - a > 0.01 and d - c > 0.01:
            m.box(mat, ((a + b) / 2, (c + d) / 2, z), (b - a, d - c, thick))
