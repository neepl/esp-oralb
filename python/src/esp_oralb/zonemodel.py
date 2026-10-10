"""Zone estimator: brush motion over a short window -> brushing zone.

Two model types share the same pipeline (windows of WINDOW seconds, a sticky HMM
filter over per-window zone scores):

* ZoneModel ("gauss"): tilt only — the mean accelerometer vector normalised to
  unit length (gravity in the brush frame), one diagonal Gaussian per zone.
* LinearZoneModel ("lda"): tilt + gyroscope + shake — 12 features per window
  (tilt, mean and spread of the rotation rate, spread of the acceleration),
  scored by a linear discriminant (LDA). ~10 points better across sessions.

Inference is dependency-free so it ports to the ESP32; fitting LDA uses numpy.
"""

from __future__ import annotations

import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

WINDOW = 0.5  # seconds
STAY = 0.9  # HMM probability of staying in the same zone between windows
MIN_VAR = 0.004  # variance floor per axis (unit-vector space)
EVIDENCE_CAP = 4.0  # max log-likelihood gap one window can contribute (below the switch cost)

GYRO_RAD_S = math.radians(4.5)  # rad/s per raw gyro unit (fitted from how gravity rotates)
ACCEL_1G = 33.0  # raw accelerometer units per g

Vec = tuple[float, ...]
Sample = tuple[tuple[int, int, int], tuple[int, int, int] | None]  # (accel, gyro)


def _unit(v: Vec) -> Vec | None:
    n = math.sqrt(sum(c * c for c in v))
    return None if n < 1e-6 else (v[0] / n, v[1] / n, v[2] / n)


def load(path: Path) -> list[dict]:
    return [r for r in map(json.loads, path.open()) if r.get("k") == "imu"]


def windows(samples: list[dict], kind: str = "gauss") -> list[tuple[Vec, str | None]]:
    """Group consecutive samples into WINDOW-long chunks -> (feature, label).

    The label is set only for cleanly labelled windows (every sample in the same zone).
    """
    feature = FEATURES[kind]
    out, chunk, start = [], [], None

    def flush() -> None:
        x = feature([(s["a"], s.get("g")) for s in chunk])
        label, count = Counter(s["zone"] for s in chunk).most_common(1)[0]
        if x is not None:
            out.append((x, label if count == len(chunk) else None))

    for s in samples:
        if start is None:
            start = s["t"]
        if s["t"] - start >= WINDOW and chunk:
            flush()
            chunk, start = [], s["t"]
        chunk.append(s)
    if chunk:
        flush()
    return out


def tilt_feature(chunk: list[Sample]) -> Vec | None:
    n = len(chunk)
    return _unit(tuple(sum(a[i] for a, _ in chunk) / n for i in range(3)))


def imu12_feature(chunk: list[Sample]) -> Vec | None:
    """tilt (3) + mean rotation rate in rad/s (3) + its spread (3) + acceleration spread in g (3)."""
    tilt = tilt_feature(chunk)
    if tilt is None:
        return None
    n = len(chunk)
    gyro = [tuple(c * GYRO_RAD_S for c in g) if g else (0.0, 0.0, 0.0) for _, g in chunk]
    g_mean = tuple(sum(g[i] for g in gyro) / n for i in range(3))
    g_std = tuple(math.sqrt(sum((g[i] - g_mean[i]) ** 2 for g in gyro) / n) for i in range(3))
    a_mean = tuple(sum(a[i] for a, _ in chunk) / n for i in range(3))
    a_std = tuple(math.sqrt(sum((a[i] - a_mean[i]) ** 2 for a, _ in chunk) / n) / ACCEL_1G for i in range(3))
    return tilt + g_mean + g_std + a_std


FEATURES = {"gauss": tilt_feature, "lda": imu12_feature}


@dataclass
class ZoneModel:
    means: dict[str, Vec]
    vars: dict[str, Vec]
    kind = "gauss"

    @property
    def zones(self) -> list[str]:
        return list(self.means)

    @classmethod
    def fit(cls, data: list[tuple[Vec, str | None]]) -> ZoneModel:
        by_zone: dict[str, list[Vec]] = defaultdict(list)
        for x, z in data:
            if z:
                by_zone[z].append(x)
        means, vars_ = {}, {}
        for z, xs in by_zone.items():
            m = tuple(sum(x[i] for x in xs) / len(xs) for i in range(3))
            means[z] = m
            vars_[z] = tuple(max(MIN_VAR, sum((x[i] - m[i]) ** 2 for x in xs) / len(xs)) for i in range(3))
        return cls(means, vars_)

    def loglik(self, x: Vec) -> dict[str, float]:
        return {
            z: -0.5 * sum((x[i] - m[i]) ** 2 / v[i] + math.log(v[i]) for i in range(3))
            for z, (m, v) in ((z, (self.means[z], self.vars[z])) for z in self.means)
        }

    def classify(self, x: Vec) -> str:
        ll = self.loglik(x)
        return max(ll, key=ll.get)

    def to_json(self) -> dict:
        return {"type": "gauss", "window": WINDOW, "stay": STAY, "means": self.means, "vars": self.vars}

    @classmethod
    def from_json(cls, d: dict) -> ZoneModel:
        return cls({k: tuple(v) for k, v in d["means"].items()}, {k: tuple(v) for k, v in d["vars"].items()})


@dataclass
class LinearZoneModel:
    """LDA on standardised imu12 features: score_z(x) = w_z . ((x - mean) / scale) + b_z.

    LDA scores are log-posteriors up to a shared constant, so they plug straight
    into the HMM filter as per-zone log-likelihoods.
    """

    zones: list[str]
    mean: Vec
    scale: Vec
    weights: list[Vec]  # one row per zone
    bias: list[float]
    kind = "lda"
    SHRINK = 0.1  # covariance shrinkage towards the diagonal (robust with few windows)

    @classmethod
    def fit(cls, data: list[tuple[Vec, str | None]]) -> LinearZoneModel:
        import numpy as np  # training only

        xs = [(x, z) for x, z in data if z]
        zones = sorted({z for _, z in xs}, key=lambda z: _ZONE_ORDER.get(z, 99))
        X = np.array([x for x, _ in xs])
        y = np.array([zones.index(z) for _, z in xs])
        mean, scale = X.mean(0), X.std(0) + 1e-9
        Xs = (X - mean) / scale
        mus = np.array([Xs[y == k].mean(0) for k in range(len(zones))])
        resid = Xs - mus[y]
        cov = resid.T @ resid / (len(Xs) - len(zones))
        cov = (1 - cls.SHRINK) * cov + cls.SHRINK * np.diag(np.diag(cov))
        inv = np.linalg.inv(cov)
        W = mus @ inv
        b = -0.5 * np.einsum("kd,kd->k", W, mus) + np.log(np.bincount(y) / len(y))
        return cls(zones, tuple(mean), tuple(scale), [tuple(w) for w in W], list(b))

    def loglik(self, x: Vec) -> dict[str, float]:
        xs = [(x[i] - self.mean[i]) / self.scale[i] for i in range(len(x))]
        return {z: sum(w * v for w, v in zip(self.weights[k], xs)) + self.bias[k] for k, z in enumerate(self.zones)}

    def classify(self, x: Vec) -> str:
        ll = self.loglik(x)
        return max(ll, key=ll.get)

    def to_json(self) -> dict:
        return {
            "type": "lda",
            "window": WINDOW,
            "stay": STAY,
            "features": "tilt3 gyro_mean3 gyro_std3 accel_std3",
            "gyro_rad_s_per_unit": GYRO_RAD_S,
            "accel_units_per_g": ACCEL_1G,
            "zones": self.zones,
            "mean": self.mean,
            "scale": self.scale,
            "weights": self.weights,
            "bias": self.bias,
        }

    @classmethod
    def from_json(cls, d: dict) -> LinearZoneModel:
        return cls(d["zones"], tuple(d["mean"]), tuple(d["scale"]), [tuple(w) for w in d["weights"]], list(d["bias"]))


# Keep the established zone order (it decides tie-breaks and the firmware's indices).
_ZONE_ORDER = {z: i for i, z in enumerate(
    ["UOL", "UOF", "UOR", "UIR", "UIF", "UIL", "UCL", "UCR", "LOR", "LOF", "LOL", "LIL", "LIF", "LIR", "LCR", "LCL"])}  # fmt: skip

AnyModel = ZoneModel | LinearZoneModel


def model_from_json(d: dict) -> AnyModel:
    return LinearZoneModel.from_json(d) if d.get("type") == "lda" else ZoneModel.from_json(d)


class ZoneFilter:
    """Sticky HMM forward filter over zones; feed one window feature at a time."""

    def __init__(self, model: AnyModel, stay: float = STAY) -> None:
        self.model = model
        self.zones = model.zones
        n = len(self.zones)
        self.log_stay, self.log_move = math.log(stay), math.log((1 - stay) / (n - 1))
        self.belief = {z: -math.log(n) for z in self.zones}

    def update(self, x: Vec) -> str:
        ll = self.model.loglik(x)
        # Cap per-window evidence so one odd window can't flip the zone on its own.
        top_ll = max(ll.values())
        ll = {z: max(v, top_ll - EVIDENCE_CAP) for z, v in ll.items()}
        best_prev = max(self.belief.values())
        new = {}
        for z in self.zones:
            # log-sum-exp over "stay in z" and "come from anywhere else" (approximated by best)
            stay = self.belief[z] + self.log_stay
            move = best_prev + self.log_move
            hi = max(stay, move)
            new[z] = hi + math.log(math.exp(stay - hi) + math.exp(move - hi)) + ll[z]
        top = max(new.values())
        self.belief = {z: v - top for z, v in new.items()}
        return max(self.belief, key=self.belief.get)


def evaluate(model: AnyModel, data: list[tuple[Vec, str | None]]) -> dict:
    """Accuracy of raw and smoothed predictions, overall and per factor (jaw/surface/side)."""
    flt = ZoneFilter(model)
    raw_ok = smooth_ok = n = 0
    factor = Counter()
    confusion: dict[str, Counter] = defaultdict(Counter)
    for x, z in data:
        smooth = flt.update(x)  # the filter also sees unlabelled transition windows
        if not z:
            continue
        raw = model.classify(x)
        n += 1
        raw_ok += raw == z
        smooth_ok += smooth == z
        confusion[z][smooth] += 1
        for i, name in enumerate(("jaw", "surface", "side")):
            factor[name] += smooth[i] == z[i]
    return {
        "windows": n,
        "raw": raw_ok / n if n else 0.0,
        "smoothed": smooth_ok / n if n else 0.0,
        **{k: v / n for k, v in factor.items()},
        "confusion": confusion,
    }


# -- calibration-free use: default model + automatic grip alignment ---------------

DEFAULT_MODEL = Path(__file__).with_name("zone_model.json")
ALIGN_SPAN = 40  # degrees searched around the default grip
ALIGN_MIN_WINDOWS = 16  # don't trust an alignment from less than ~8 s of brushing
ALIGN_PRIOR_DEG = 25.0  # weak preference for small corrections


def load_default() -> AnyModel | None:
    return model_from_json(json.loads(DEFAULT_MODEL.read_text())) if DEFAULT_MODEL.exists() else None


Mat = tuple[Vec, Vec, Vec]
IDENTITY: Mat = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def grip_rotation(roll: float, tilt: float) -> Mat:
    """Rotation about x (roll around the handle) then about y (tilt)."""
    a, b = math.radians(roll), math.radians(tilt)
    ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(b), math.sin(b)
    return ((cb, sb * sa, sb * ca), (0.0, ca, -sa), (-sb, cb * sa, cb * ca))


def rotate(m: Mat, v: Vec) -> Vec:
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


def solve_alignment(model: ZoneModel, feats: list[Vec]) -> tuple[float, float]:
    """Grip correction (roll, tilt) that makes this person's windows fit the default model best.

    Score = sum over windows of the best zone log-likelihood (capped, so stray
    windows don't dominate) minus a weak prior on large corrections.
    """

    def score(roll: float, tilt: float) -> float:
        m = grip_rotation(roll, tilt)
        fit = sum(max(-30.0, max(model.loglik(rotate(m, x)).values())) for x in feats)
        return fit - 0.5 * (roll**2 + tilt**2) / ALIGN_PRIOR_DEG**2

    best = max(
        ((score(r, t), r, t) for r in range(-ALIGN_SPAN, ALIGN_SPAN + 1, 10) for t in range(-ALIGN_SPAN, ALIGN_SPAN + 1, 10)),
    )
    _, r0, t0 = best
    best = max(best, *((score(r, t), r, t) for r in range(r0 - 8, r0 + 9, 2) for t in range(t0 - 8, t0 + 9, 2)))
    return float(best[1]), float(best[2])


class ZoneTracker:
    """Live zone estimate + per-zone brushing time for one session."""

    def __init__(self, model: AnyModel) -> None:
        self.model = model
        self.feature = FEATURES[model.kind]
        self.roll = self.tilt = 0.0
        self.align = IDENTITY
        self.feats: list[Vec] = []  # all window features this session (for alignment)
        self.reset()

    def reset(self) -> None:
        self.filter = ZoneFilter(self.model)
        self.coverage: Counter[str] = Counter()
        self.current: str | None = None
        self._chunk: list[Sample] = []
        self._start: float | None = None
        self.feats = []

    def add(self, t: float, accel: tuple[int, int, int], gyro: tuple[int, int, int] | None = None) -> None:
        if self._start is None:
            self._start = t
        if t - self._start >= WINDOW and self._chunk:
            x = self.feature(self._chunk)
            self._chunk, self._start = [], t
            if x is not None:
                if self.model.kind == "gauss":  # grip alignment applies to the tilt-only model
                    self.feats.append(x)
                    x = rotate(self.align, x)
                self.current = self.filter.update(x)
                self.coverage[self.current] += WINDOW
        self._chunk.append((accel, gyro))

    def set_alignment(self, roll: float, tilt: float) -> None:
        self.roll, self.tilt = roll, tilt
        self.align = grip_rotation(roll, tilt)
