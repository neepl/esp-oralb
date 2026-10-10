"""Replay a real iO 8 capture (changes only) through the decoder and session."""

from pathlib import Path

from esp_oralb.protocol import parse
from esp_oralb.session import Screen, Session

DATA = Path(__file__).parent / "data" / "io8_session.txt"


def frames():
    for line in DATA.read_text().splitlines():
        ts, *hexbytes = line.split()
        h, m, s = map(int, ts.split(":"))
        yield h * 3600 + m * 60 + s, parse(bytes.fromhex("".join(hexbytes)))


def test_every_frame_decodes_as_io():
    for _, f in frames():
        assert f is not None
        assert f.model_name == "iO Series"
        assert f.mode_name == "daily clean" or f.mode == 8  # 8 = settings menu


def test_session_replay():
    s = Session()
    screens = {}
    for t, f in frames():
        s.on_frame(f, t, t)
        screens[t] = s.screen
    at = lambda hms: screens[sum(int(x) * k for x, k in zip(hms.split(":"), (3600, 60, 1)))]

    assert at("16:30:45") is Screen.CLOCK  # selection menu
    assert at("16:30:50") is Screen.BRUSHING
    assert at("16:31:07") is Screen.RESULT  # paused at 0:17, brush keeps its timer
    assert at("16:31:08") is Screen.BRUSHING  # resumed at 0:18
    # final stop after the timer restarted at 0:00 with four pressure episodes
    assert s.screen is Screen.RESULT
    assert s.last.seconds == 15
    assert s.last.high_pressure_hits == 4
