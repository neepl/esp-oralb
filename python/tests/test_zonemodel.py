import json
import math
import random

from esp_oralb import zonemodel
from esp_oralb.zonemodel import ZoneFilter, ZoneModel
from esp_oralb.zones import ZONES


def synthetic(seed: int, noise: float = 0.15):
    """Each zone = a distinct gravity direction; IMU samples with noise, 6 s per zone."""
    rnd = random.Random(seed)
    dirs = {z: (math.cos(i * 0.39) * 30, math.sin(i * 0.39) * 30, (i % 4 - 1.5) * 10) for i, z in enumerate(ZONES)}
    t, out = 0.0, []
    for z in ZONES:
        for _ in range(300):  # 6 s at 50 Hz
            a = [c + rnd.gauss(0, 30 * noise) for c in dirs[z]]
            out.append({"t": t, "k": "imu", "a": a, "g": [0, 0, 0], "zone": z})
            t += 0.02
    return out


def test_fit_and_evaluate(tmp_path):
    a, b = zonemodel.windows(synthetic(1)), zonemodel.windows(synthetic(2))
    model = ZoneModel.fit(a)
    r = zonemodel.evaluate(model, b)
    assert r["windows"] > 150
    assert r["smoothed"] > 0.9
    # JSON round-trip (this file is what the ESP32 will embed)
    m2 = ZoneModel.from_json(json.loads(json.dumps(model.to_json())))
    x = b[40][0]
    assert m2.classify(x) == model.classify(x)


def test_filter_is_sticky():
    model = ZoneModel.fit(zonemodel.windows(synthetic(3)))
    f = ZoneFilter(model)
    target = model.means["UOL"]
    for _ in range(5):
        f.update(target)
    # a single outlier window does not flip the decision
    assert f.update(model.means["LCR"]) == "UOL"


def test_linear_model_fit_roundtrip_and_filter():
    from esp_oralb.zonemodel import LinearZoneModel, model_from_json

    rnd = random.Random(5)
    data = []
    for i, z in enumerate(ZONES):
        centre = [math.cos(i), math.sin(i), (i % 4) / 4] + [0.1 * ((i * k) % 3) for k in range(9)]
        for _ in range(40):
            data.append((tuple(c + rnd.gauss(0, 0.05) for c in centre), z))
    m = LinearZoneModel.fit(data)
    assert m.zones == ZONES  # established order is kept
    hits = sum(m.classify(x) == z for x, z in data)
    assert hits / len(data) > 0.95
    m2 = model_from_json(json.loads(json.dumps(m.to_json())))
    assert m2.classify(data[100][0]) == m.classify(data[100][0])
    f = ZoneFilter(m)
    assert all(isinstance(f.update(x), str) for x, _ in data[:10])


def test_imu12_feature_shape():
    from esp_oralb.zonemodel import imu12_feature

    chunk = [((0, 0, -33), (1, -2, 3)), ((2, 0, -31), (3, 0, 1))]
    x = imu12_feature(chunk)
    assert len(x) == 12
    assert abs(sum(c * c for c in x[:3]) - 1) < 1e-9  # tilt is a unit vector
