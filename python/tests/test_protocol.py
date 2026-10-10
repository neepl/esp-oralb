"""Vectors taken from oralb-ble's tests/test_parser.py."""

from esp_oralb.protocol import parse


def test_rejects_bad_length():
    assert parse(b"\x00" * 10) is None
    assert parse(b"") is None


def test_io_series_running():
    # IO Series 7, running, sector 7 ("last") with 6 sectors
    f = parse(b"\x0612\x03r\x02\x03\x00\x07\x00\x06")
    assert f.model_name == "iO Series"
    assert f.running
    assert f.brush_seconds == 123
    assert f.mode_name == "daily clean"
    assert f.sectors == 6
    assert f.sector == 6
    assert not f.high_pressure


def test_io_series_idle_sector_cleared():
    f = parse(b"\x061\x16\x08r\x00\x00\x03\x02\x00\x04")
    assert not f.running
    assert f.state_name == "selection menu"
    assert f.sector == 0
    assert f.mode_name == "whiten"


def test_high_pressure():
    # 0xc0 = high pressure, no button flags
    f = parse(b'\x03"\x0c\x03\xc0\x010\x07\x04<\x04')
    assert f.high_pressure
    assert f.brush_seconds == 60 + 48
    assert f.sector == 4
    assert f.sector_timer == 60


def test_button_overrides_pressure():
    f = parse(b"\x02\x01\x08\x03\x96\x00\x00\x01\x01\x00\x04")  # 150 = high|button
    assert f.button_pressed
    assert not f.high_pressure


def test_short_frame():
    f = parse(b"\x01\x02\x05\x03\x00\x00\x08\x04\x01")
    assert f.model_name == "Triumph D36"
    assert f.sector_timer is None
    assert f.sectors == 4
    assert f.sector == 1
    assert f.brush_seconds == 8
