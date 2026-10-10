from esp_oralb.protocol import parse
from esp_oralb.session import LOST_AFTER, RESULT_HOLD, Screen, Session


def fr(state: int, secs: int, status: int = 0x32, sector: int = 1):
    return parse(bytes([6, 49, 50, state, status, secs // 60, secs % 60, 0, sector, 0, 4]))


def test_full_cycle():
    s = Session()
    s.on_frame(fr(2, 0), 0, 1000)
    assert s.screen is Screen.CLOCK
    for t in range(0, 125):
        s.on_frame(fr(3, t), t, 1000 + t)
    assert s.screen is Screen.BRUSHING
    # idle frame with timer already reset still records the real duration
    s.on_frame(fr(2, 0), 125, 1125)
    assert s.screen is Screen.RESULT
    assert s.last.seconds == 124
    s.tick(125 + RESULT_HOLD + 1, 0)
    assert s.screen is Screen.CLOCK


def test_pressure_hits_counted_per_episode():
    s = Session()
    seq = [0x32, 0xB2, 0xB2, 0x32, 0xB2, 0x32]
    for t, st in enumerate(seq):
        s.on_frame(fr(3, t + 10, st), t, t)
    assert s.pressure_hits == 2


def test_signal_lost_ends_session():
    s = Session()
    s.on_frame(fr(3, 30), 0, 0)
    s.tick(LOST_AFTER - 1, 0)
    assert s.screen is Screen.BRUSHING
    s.tick(LOST_AFTER + 1, 0)
    assert s.screen is Screen.RESULT
    assert s.last.seconds == 30


def test_short_blip_not_remembered():
    s = Session()
    s.on_frame(fr(3, 2), 0, 0)
    s.on_frame(fr(2, 2), 1, 1)
    assert s.screen is Screen.CLOCK
    assert s.last is None


def test_resume_after_pause_keeps_counters():
    s = Session()
    s.on_frame(fr(3, 40, 0xB2), 0, 0)
    s.on_frame(fr(2, 40), 1, 1)  # paused
    assert s.screen is Screen.RESULT
    s.on_frame(fr(3, 41), 5, 5)  # resumed, brush kept its timer
    assert s.screen is Screen.BRUSHING
    assert s.pressure_hits == 1
