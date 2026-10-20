"""星運彩券行 (plan §15.1 / §15.7.1): a small walk-in lottery shop next to the convenience store.

Anchors: counter (betting, claims), screens (draw results and odds wall), scratch (scratch-card table),
seat (bench to wait for the draw) and board (the red board of big wins by the door).
"""
from interiors import Put, T, counter, pendant, shell, sign_on


def walkin_lottery(Model):
    m = Model('lottery')
    D, W, H = 7.0, 7.0, 3.4
    front = shell(m, D, W, H, 'red', 'white', 'tile', door_w=1.8, front_windows=(-2.3,), side_windows=0)
    # Red and gold shop front, a star over the door.
    m.box('yellow', (front + 0.06, 0, H - 0.3), (0.06, W, 0.24))
    sign_on(m, front, '星運彩券行', 2.85, 3.6, 'red', 'yellow')
    m.cyl('yellow', (front + 0.08, 0, 2.62), 0.18, 0.04, 5, axis='X')
    # Red board of big wins outside, left of the door.
    m.box('red', (front + 0.12, 2.3, 1.6), (0.06, 1.2, 1.4))
    m.box('yellow', (front + 0.16, 2.3, 2.2), (0.02, 1.0, 0.18))
    for i in range(4):
        m.box('white', (front + 0.16, 2.3, 1.85 - i * 0.24), (0.02, 0.9, 0.12))
    # Counter across the back with two terminals and a ticket printer.
    counter(m, -D / 2 + 1.3, 0, 0, 4.2, 'red', 'white', register=False)
    for y in (-1.0, 1.0):
        m.box('black', (-D / 2 + 1.25, y, 1.25), (0.3, 0.45, 0.35))
        m.box('glass_dark', (-D / 2 + 1.42, y, 1.28), (0.02, 0.38, 0.26))
    m.box('white', (-D / 2 + 1.2, 0, 1.12), (0.3, 0.3, 0.16))
    # Screen wall behind the counter: draw numbers, odds and the jackpot.
    for i, y in enumerate((-2.0, 0.0, 2.0)):
        m.box('black', (-D / 2 + T + 0.06, y, 2.3), (0.08, 1.7, 1.0))
        m.box('glass_dark' if i != 1 else 'navy', (-D / 2 + T + 0.11, y, 2.3), (0.02, 1.56, 0.86))
    for k in range(6):
        m.sphere(['red', 'yellow', 'red', 'yellow', 'red', 'sky'][k], (-D / 2 + T + 0.14, -0.55 + k * 0.22, 2.42), 0.08)
    m.box('yellow', (-D / 2 + T + 0.13, 0, 2.08), (0.02, 1.2, 0.14))
    # Scratch-card table on the right, with card racks on the wall.
    p = Put(m, 0.6, -2.4, 0)
    p.box('wood_light', (0, 0, 0.9), (1.4, 0.6, 0.05))
    p.box('metal', (0, 0, 0.45), (0.08, 0.08, 0.9))
    for k in range(5):
        m.box(['yellow', 'pink', 'sky', 'mint', 'orange'][k], (-0.6 + k * 0.45, -W / 2 + T + 0.04, 1.7), (0.36, 0.04, 0.5))
    # Bench under the left window to wait for the draw.
    b = Put(m, 1.2, 2.5, 0)
    b.box('wood', (0, 0, 0.45), (1.8, 0.45, 0.06), solid=True)
    b.box('wood_dark', (0, 0.22, 0.75), (1.8, 0.05, 0.55))
    for x in (-0.8, 0.8):
        b.box('metal', (x, 0, 0.22), (0.06, 0.4, 0.44))
    for x in (-1.2, 1.2):
        pendant(m, x, 0, H, 0.7)
    m.anchors['counter'] = ((-D / 2 + 2.6, -0.4), 180)
    m.anchors['screens'] = ((-0.2, 0.9), 180)
    m.anchors['scratch'] = ((1.6, -1.6), 0)
    m.anchors['seat'] = ((0.8, 1.2), 90)
    m.anchors['board'] = ((front + 1.2, 2.0), 0)
    return m
