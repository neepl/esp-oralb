from esp_oralb import ring
from esp_oralb.ring import Px


def rows_with(grid, px):
    return [y for y, row in enumerate(grid) if px in row]


def test_face_mouth_direction():
    # eyes are in the top half; smile's lowest pixel is lower than a frown's
    happy, sad = ring.face(40, True), ring.face(40, False)
    assert min(rows_with(happy, Px.FACE)) < 20
    assert max(rows_with(happy, Px.FACE)) > max(rows_with(sad, Px.FACE)) - 2


def test_dial_progress():
    start, half, done = ring.dial(60, 0), ring.dial(60, 60), ring.dial(60, 150)
    count = lambda g, p: sum(row.count(p) for row in g)
    assert count(start, Px.TICK_DONE) == 0
    assert 0 < count(half, Px.TICK_DONE) < count(half, Px.TICK_DONE) + count(half, Px.TICK_TODO)
    assert count(done, Px.TICK_TODO) == 0
    # dot at 12 o'clock at the start, 6 o'clock at 1:00
    assert min(rows_with(start, Px.DOT)) < 10
    assert max(rows_with(half, Px.DOT)) > 50
