"""Offline comparison of zone features (accelerometer + gyroscope) and classifiers.

Leave-one-session-out over the guided recordings in data/, with the same sticky
HMM smoothing for every classifier (applied to its per-window class probabilities).

    uv run --with numpy --with scikit-learn python experiments/zone_features.py
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from sklearn.discriminant_analysis import LinearDiscriminantAnalysis, QuadraticDiscriminantAnalysis
from sklearn.ensemble import RandomForestClassifier
from sklearn.neighbors import KNeighborsClassifier
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

DATA = Path(__file__).resolve().parents[1] / "data"
GYRO_DPS = 4.5  # degrees/s per raw gyro unit (fitted from gravity rotation)
WIN = 25  # samples per window (~0.5 s at 50 Hz)
STAY = 0.9


def load(path: Path):
    S = [r for r in map(json.loads, path.open()) if r["k"] == "imu" and r["g"] is not None]
    a = np.array([s["a"] for s in S], float)
    g = np.array([s["g"] for s in S], float) * np.radians(GYRO_DPS)  # rad/s
    z = [s["zone"] for s in S]
    return a, g, z


def orientation_heading(a: np.ndarray, g: np.ndarray, dt: float = 0.02, leak: float = 0.995) -> np.ndarray:
    """Complementary filter: gravity direction from accel, heading = leaky integral of the
    gyro component about the gravity axis (leaky so integration bias can't run away)."""
    grav = a[0] / np.linalg.norm(a[0])
    h = 0.0
    out = np.zeros(len(a))
    for i in range(len(a)):
        # gyro-propagate gravity, then pull towards the accelerometer
        grav = grav - np.cross(g[i], grav) * dt
        acc = a[i] / (np.linalg.norm(a[i]) + 1e-9)
        grav = 0.98 * grav + 0.02 * acc
        grav /= np.linalg.norm(grav)
        h = leak * h + float(g[i] @ grav) * dt
        out[i] = h
    return out


def windows(a, g, z, feats: set[str]):
    head = orientation_heading(a, g) if "heading" in feats else None
    X, Y = [], []
    for i in range(0, len(a) - WIN + 1, WIN):
        sl = slice(i, i + WIN)
        labels = Counter(z[sl])
        lab, cnt = labels.most_common(1)[0]
        am = a[sl].mean(0)
        f = list(am / np.linalg.norm(am))  # tilt (gravity direction)
        if "gyro_mean" in feats:
            f += list(g[sl].mean(0))  # systematic rotation during the stroke
        if "gyro_std" in feats:
            f += list(g[sl].std(0))  # stroke rotation pattern
        if "acc_std" in feats:
            f += list(a[sl].std(0) / 33.0)  # stroke shaking pattern (in g)
        if "heading" in feats:
            f += [np.sin(head[sl].mean()), np.cos(head[sl].mean())]
        X.append(f)
        Y.append(lab if cnt == WIN else None)
    return np.array(X), Y


def hmm_smooth(P: np.ndarray, stay: float = STAY, cap: float = 4.0) -> np.ndarray:
    """Forward-filter per-window class probabilities with a sticky transition model."""
    n = P.shape[1]
    lp = np.log(np.clip(P, 1e-12, 1))
    lp = np.maximum(lp, lp.max(1, keepdims=True) - cap)
    ls, lm = np.log(stay), np.log((1 - stay) / (n - 1))
    b = np.full(n, -np.log(n))
    out = []
    for row in lp:
        stay_ = b + ls
        move = b.max() + lm
        b = np.logaddexp(stay_, move) + row
        b -= b.max()
        out.append(int(b.argmax()))
    return np.array(out)


CLASSIFIERS = {
    "diag-gauss": lambda: make_pipeline(StandardScaler(), QuadraticDiscriminantAnalysis(reg_param=0.5)),
    "QDA": lambda: make_pipeline(StandardScaler(), QuadraticDiscriminantAnalysis(reg_param=0.1)),
    "LDA": lambda: make_pipeline(StandardScaler(), LinearDiscriminantAnalysis()),
    "kNN": lambda: make_pipeline(StandardScaler(), KNeighborsClassifier(9)),
    "forest": lambda: RandomForestClassifier(300, min_samples_leaf=3, random_state=0),
    "MLP": lambda: make_pipeline(StandardScaler(), MLPClassifier((32,), alpha=1e-2, max_iter=3000, random_state=0)),
}
FEATURE_SETS = {
    "tilt": set(),
    "tilt+gyro": {"gyro_mean", "gyro_std"},
    "tilt+gyro+shake": {"gyro_mean", "gyro_std", "acc_std"},
    "tilt+gyro+shake+heading": {"gyro_mean", "gyro_std", "acc_std", "heading"},
}


def main() -> None:
    files = sorted(DATA.glob("zones-*.jsonl"))
    raw = [load(f) for f in files]
    print(f"{len(files)} sessions: " + ", ".join(f.stem[-6:] for f in files))
    names = list(CLASSIFIERS)
    print(f"{'features':26s}" + "".join(f"{n:>12s}" for n in names))
    best = (0, None)
    for fname, feats in FEATURE_SETS.items():
        sets = [windows(*r, feats) for r in raw]
        row = []
        for cname in names:
            accs = []
            for k in range(len(sets)):
                Xtr = np.vstack([sets[j][0] for j in range(len(sets)) if j != k])
                Ytr = sum((sets[j][1] for j in range(len(sets)) if j != k), [])
                m = [y is not None for y in Ytr]
                clf = CLASSIFIERS[cname]().fit(Xtr[m], np.array(Ytr, object)[m])
                Xte, Yte = sets[k]
                pred = hmm_smooth(clf.predict_proba(Xte))
                classes = clf.classes_ if hasattr(clf, "classes_") else clf[-1].classes_
                ok = [classes[p] == y for p, y in zip(pred, Yte) if y is not None]
                accs.append(np.mean(ok))
            acc = float(np.mean(accs))
            row.append(acc)
            if acc > best[0]:
                best = (acc, (fname, cname, accs))
        print(f"{fname:26s}" + "".join(f"{v:12.0%}" for v in row))
    acc, (fname, cname, accs) = best
    print(f"\nbest: {fname} + {cname}: {acc:.0%} (per held-out session: {', '.join(f'{a:.0%}' for a in accs)})")


if __name__ == "__main__":
    sys.exit(main())
