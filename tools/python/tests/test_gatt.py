from esp_oralb.gatt import decode_sensor
from esp_oralb.zones import ZONES, zone_name


def test_decode_comino_packet():
    # captured from the user's iO 8 (ff0d read)
    raw = bytes.fromhex("b47e010006 1ef4f7 dd7e 09fe02 1df9fa 0000 1080".replace(" ", ""))
    a, b = decode_sensor(raw)
    assert (a.ts, a.gyro, a.accel) == (0x7EB4, (1, 0, 6), (30, -12, -9))
    assert (b.ts, b.gyro, b.accel) == (0x7EDD, (9, -2, 2), (29, -7, -6))


def test_decode_rejects_other_lengths():
    assert decode_sensor(b"\x00" * 19) == []


def test_zones():
    assert len(ZONES) == len(set(ZONES)) == 16
    assert zone_name("UOL") == "upper left outside"
    assert zone_name("LCR") == "lower right chewing"
