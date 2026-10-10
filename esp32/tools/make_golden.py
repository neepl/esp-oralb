#!/usr/bin/env python3
"""Export golden vectors for the C++ zone code from the Python reference (LDA path).

    cd esp32 && uv run --project ../python python tools/make_golden.py

Reads python/data/zones-*.jsonl and writes test/data/zones_golden.txt (line based):

    S t ax ay az gx gy gz     raw IMU sample (tracker fixture: first N_SAMPLES of the first recording)
    F f0..f11 zone            per-window imu12 feature + ZoneFilter output while replaying the S lines
    TRK n_windows current c0..c15   ZoneTracker state after all S lines
    SN t ax ay az             samples without gyro (plain-motion packets), first N_NOGYRO of the last recording
    FN f0..f11 zone           the same for the no-gyro replay
    G f0..f11 zone            every window of the last recording (windows() features) + filter outputs

Zones are indices in model order.
"""
import sys
from pathlib import Path

from esp_oralb import zonemodel as zm

N_SAMPLES = 1500
N_NOGYRO = 300

root = Path(__file__).resolve().parents[1]
files = sorted((root.parent / "python" / "data").glob("zones-*.jsonl"))
if not files:
    sys.exit("no python/data/zones-*.jsonl")
model = zm.load_default()
assert model.kind == "lda"
zones = model.zones


def g(v):
    return repr(float(v))


def replay(samples, use_gyro):
    """Run the Python tracker; return per-window (feature, zone index) and the tracker."""
    tr = zm.ZoneTracker(model)
    rec = []
    inner = tr.feature

    def spy(chunk):
        x = inner(chunk)
        rec.append(x)
        return x

    tr.feature = spy
    out = []
    for s in samples:
        n = len(rec)
        gyro = tuple(int(c) for c in s["g"]) if use_gyro and s.get("g") else None
        tr.add(s["t"], tuple(int(c) for c in s["a"]), gyro)
        if len(rec) > n and rec[-1] is not None:
            out.append((rec[-1], zones.index(tr.current)))
    return out, tr


def zline(tag, x, z):
    return f"{tag} " + " ".join(g(c) for c in x) + f" {z}"


out = [f"# golden vectors (LDA); sources {files[0].name}, {files[-1].name}; zones in model order"]

first = zm.load(files[0])[:N_SAMPLES]
for s in first:
    gy = s["g"] or (0, 0, 0)
    out.append(f"S {g(s['t'])} " + " ".join(str(int(c)) for c in s["a"]) + " " + " ".join(str(int(c)) for c in gy))
wins, tr = replay(first, True)
out += [zline("F", x, z) for x, z in wins]
cov = " ".join(g(tr.coverage.get(z, 0.0)) for z in zones)
out.append(f"TRK {len(wins)} {zones.index(tr.current)} {cov}")

last = zm.load(files[-1])
nog = last[:N_NOGYRO]
for s in nog:
    out.append(f"SN {g(s['t'])} " + " ".join(str(int(c)) for c in s["a"]))
out += [zline("FN", x, z) for x, z in replay(nog, False)[0]]

flt = zm.ZoneFilter(model)
for x, _ in zm.windows(last, "lda"):
    out.append(zline("G", x, zones.index(flt.update(x))))

path = root / "test" / "data" / "zones_golden.txt"
path.write_text("\n".join(out) + "\n")
print(f"wrote {path} ({sum(l.startswith('G ') for l in out)} full-file windows, {len(wins)} replay windows, {path.stat().st_size // 1024} KiB)")
