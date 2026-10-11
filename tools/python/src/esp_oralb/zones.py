"""16 brushing zones and a guided recording session for training the zone estimator.

    uv run esp-oralb-zones record                 # spoken prompts walk you through all zones
    uv run esp-oralb-zones record --shuffle       # same, zones in random order
    uv run esp-oralb-zones eval A.jsonl B.jsonl   # train on A, test on B (and B on A)
    uv run esp-oralb-zones train A.jsonl B.jsonl  # fit on all files -> default model shipped with the app

Zones are named from the brusher's point of view: jaw (Upper/Lower),
side (Left/Front/Right), surface (Outer/Inner/Chewing; no front chewing).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import random
import shutil
import time
from pathlib import Path

from rich.align import Align
from rich.console import Console, Group
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from . import zonemodel
from .gatt import BrushLink, ImuSample

# Recording order follows one continuous path around the mouth.
ZONES = [
    "UOL", "UOF", "UOR", "UIR", "UIF", "UIL", "UCL", "UCR",
    "LOR", "LOF", "LOL", "LIL", "LIF", "LIR", "LCR", "LCL",
]  # fmt: skip
_JAW = {"U": "upper", "L": "lower"}
_SURFACE = {"O": "outside", "I": "inside", "C": "chewing"}
_SIDE = {"L": "left", "F": "front", "R": "right"}

SETTLE = 2.5  # seconds after a prompt that are not labelled (moving to the zone)
HOLD = 6.0  # labelled seconds per zone
SHUFFLE_SETTLE = 3.5  # random order means longer moves across the mouth
DATA_DIR = Path(__file__).resolve().parents[2] / "data"


def zone_name(z: str) -> str:
    return f"{_JAW[z[0]]} {_SIDE[z[2]]} {_SURFACE[z[1]]}"


def zone_map(highlight: str | None = None, coverage: dict[str, float] | None = None) -> Table:
    """3x3 grid per jaw: rows outer/inner/chewing, columns left/front/right."""
    t = Table.grid(padding=(0, 1))
    for _ in range(3):
        t.add_column(justify="center", width=9)
    for jaw in "UL":
        t.add_row(Text(""), Text(_JAW[jaw].upper(), style="grey50"), Text(""))
        for surf in "OIC":
            cells = []
            for side in "LFR":
                z = jaw + surf + side
                if z not in ZONES:
                    cells.append(Text(""))
                    continue
                label = f"{_SIDE[side][:1].upper()}-{_SURFACE[surf][:3]}"
                if z == highlight:
                    style = "bold black on #2ad4e0"
                elif coverage is not None:
                    frac = min(coverage.get(z, 0.0), 1.0)
                    style = "black on white" if frac >= 1 else ("white on grey35" if frac > 0 else "grey35")
                else:
                    style = "grey50"
                cells.append(Text(f" {label} ", style=style))
            t.add_row(*cells)
    return t


def _say(text: str) -> None:
    if shutil.which("say"):
        asyncio.get_running_loop().create_task(asyncio.create_subprocess_exec("say", "-r", "190", text))


async def record(out: Path, address: str | None, console: Console, shuffle: bool = False) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    f = out.open("w", buffering=1)
    label: list[str | None] = [None]
    counts: dict[str, int] = {}
    n_imu = [0]
    last_imu = [0.0]  # the brush streams motion only while the motor runs

    def write(rec: dict) -> None:
        f.write(json.dumps(rec, separators=(",", ":")) + "\n")

    def on_imu(t: float, s: ImuSample) -> None:
        n_imu[0] += 1
        last_imu[0] = t
        if label[0]:
            counts[label[0]] = counts.get(label[0], 0) + 1
        write({"t": round(t, 4), "k": "imu", "ts": s.ts, "g": s.gyro, "a": s.accel, "zone": label[0]})

    link = BrushLink(
        on_imu=on_imu,
        on_time=lambda v: write({"t": round(time.monotonic(), 4), "k": "time", "v": v}),
        on_state=lambda v: write({"t": round(time.monotonic(), 4), "k": "state", "v": v}),
        on_pressure=lambda v: write({"t": round(time.monotonic(), 4), "k": "pressure", "v": v}),
        on_raw=lambda t, c, d: c not in ("ff0d", "ff0b") and write({"t": round(t, 4), "k": "raw", "c": c, "d": d.hex()}),
        address=address,
    )
    console.print("Turn the brush on (it must be awake to connect)…")
    _say("Turn the brush on")
    dev = await link.find()
    console.print(f"found {dev.address}, connecting…")
    stop = asyncio.Event()
    task = asyncio.create_task(link.run(dev, stop))
    while link.client is None or not link.client.is_connected:
        if task.done():
            task.result()
            return
        await asyncio.sleep(0.1)
    await asyncio.sleep(1.0)
    if not link.supports_motion:
        console.print("[red]This brush does not provide motion data (ff0d): zone tracking is not supported.[/red]")
        stop.set()
        await task
        return

    status = {"zone": None, "phase": "get ready", "left": 3.0, "i": 0}

    def view() -> Panel:
        z = status["zone"]
        title = zone_name(z).upper() if z else status["phase"]
        bar = "█" * int(status["left"] * 4)
        return Panel(
            Group(
                Align.center(Text(title, style="bold #2ad4e0")),
                Align.center(Text(f"{status['phase']}  {status['left']:.1f}s  {bar}", style="grey70")),
                Text(""),
                Align.center(zone_map(z)),
                Text(""),
                Align.center(Text(f"zone {status['i']}/{len(ZONES)} · IMU samples {n_imu[0]}", style="grey50")),
            ),
            title="zone recording",
            width=60,
        )

    async def countdown(seconds: float, live: Live) -> None:
        """Count down only while motion data flows; pause (and say so) when the motor stops."""
        left, phase, paused = seconds, status["phase"], False
        prev = time.monotonic()
        while left > 0:
            if task.done():
                raise RuntimeError("brush disconnected (turned off?)")
            now = time.monotonic()
            flowing = now - last_imu[0] < 0.3
            if flowing:
                left -= now - prev
                if paused:
                    status["phase"], paused = phase, False
            elif not paused:
                status["phase"], paused = "paused: switch the motor on", True
                _say("paused")
            prev = now
            status["left"] = max(left, 0.0)
            live.update(view(), refresh=True)
            await asyncio.sleep(0.1)

    try:
        with Live(view(), console=console, auto_refresh=False) as live:
            if time.monotonic() - last_imu[0] > 0.3:
                status["phase"] = "start brushing (motor on)"
                _say("Start the motor and put the brush in your mouth")
                while time.monotonic() - last_imu[0] > 0.3:
                    if task.done():
                        raise RuntimeError("brush disconnected (turned off?)")
                    live.update(view(), refresh=True)
                    await asyncio.sleep(0.1)
            status["phase"] = "get ready"
            _say("Starting")
            await countdown(3, live)
            order = random.sample(ZONES, len(ZONES)) if shuffle else ZONES
            settle = SHUFFLE_SETTLE if shuffle else SETTLE
            write({"t": round(time.monotonic(), 4), "k": "meta", "order": order, "shuffle": shuffle})
            for i, z in enumerate(order, 1):
                status.update(zone=z, phase="move", i=i)
                _say(zone_name(z))
                await countdown(settle, live)
                label[0] = z
                status["phase"] = "brush"
                await countdown(HOLD, live)
                label[0] = None
            status.update(zone=None, phase="done", left=0.0)
            live.update(view(), refresh=True)
            _say("Done. You can turn the brush off.")
    finally:
        stop.set()
        await task
        f.close()
        if out.stat().st_size == 0:
            out.unlink()
    console.print(f"saved {out}")
    for z in ZONES:
        console.print(f"  {z} {zone_name(z):24s} {counts.get(z, 0):5d} samples")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="guided labelled recording")
    r.add_argument("--out", type=Path, help="output .jsonl (default data/zones-<time>.jsonl)")
    r.add_argument("--address", help="brush address/UUID")
    r.add_argument("--shuffle", action="store_true", help="prompt the zones in random order")
    e = sub.add_parser("eval", help="cross-evaluate recordings")
    e.add_argument("files", type=Path, nargs=2)
    e.add_argument("--model", choices=("lda", "gauss"), default="lda")
    t = sub.add_parser("train", help="fit model on recordings")
    t.add_argument("files", type=Path, nargs="+")
    t.add_argument("--model", choices=("lda", "gauss"), default="lda")
    t.add_argument("--out", type=Path, default=zonemodel.DEFAULT_MODEL)
    args = ap.parse_args()
    console = Console()
    if args.cmd == "record":
        out = args.out or DATA_DIR / f"zones-{time.strftime('%Y%m%d-%H%M%S')}.jsonl"
        try:
            asyncio.run(record(out, args.address, console, args.shuffle))
        except KeyboardInterrupt:
            console.print("aborted")
    elif args.cmd == "eval":
        _eval(args.files, args.model, console)
    elif args.cmd == "train":
        data = [w for f in args.files for w in zonemodel.windows(zonemodel.load(f), args.model)]
        model = MODELS[args.model].fit(data)
        args.out.write_text(json.dumps(model.to_json(), indent=1))
        console.print(f"{args.model} model with {len(model.zones)} zones -> {args.out}")


MODELS = {"lda": zonemodel.LinearZoneModel, "gauss": zonemodel.ZoneModel}


def _eval(files: list[Path], kind: str, console: Console) -> None:
    sets = [zonemodel.windows(zonemodel.load(f), kind) for f in files]
    for train, test, name in ((sets[0], sets[1], "A->B"), (sets[1], sets[0], "B->A")):
        r = zonemodel.evaluate(MODELS[kind].fit(train), test)
        console.print(
            f"{name}: {r['windows']} windows  raw {r['raw']:.0%}  smoothed {r['smoothed']:.0%}  "
            f"| jaw {r['jaw']:.0%}  surface {r['surface']:.0%}  side {r['side']:.0%}"
        )
        t = Table(title=f"confusion {name} (rows = true zone)", show_lines=False)
        t.add_column("true")
        for z in ZONES:
            t.add_column(z, justify="right")
        for z in ZONES:
            row = r["confusion"].get(z, {})
            tot = sum(row.values()) or 1
            t.add_row(z, *[(f"{row.get(p, 0) * 100 // tot}" if row.get(p) else "") for p in ZONES])
        console.print(t)


if __name__ == "__main__":
    main()
