"""esp-oralb: passive BLE listener + live display for Oral-B toothbrushes.

    uv run esp-oralb              # live display
    uv run esp-oralb --raw        # print every Oral-B advert (debug / verify)
    uv run esp-oralb --log s.log  # also record raw adverts to a file
    uv run esp-oralb --demo       # simulated brushing session, no hardware
    uv run esp-oralb --diag       # list every BLE device in range (troubleshooting)
    uv run esp-oralb --zones      # also connect to the brush and track the 16 brushing zones
    uv run esp-oralb --demo --zones  # demo with zones replayed from a recording
"""

from __future__ import annotations

import argparse
import asyncio
import json
import time
from collections.abc import Callable
from pathlib import Path

from rich.console import Console
from rich.live import Live

from .protocol import ORALB_COMPANY_ID, BrushFrame, parse
from .session import Screen, Session
from .ui import render
from .zonemodel import ZoneTracker, load_default, solve_alignment, ALIGN_MIN_WINDOWS
from .zones import DATA_DIR

FrameSink = Callable[[BrushFrame, str, int | None, bytes], None]


async def scan(
    sink: FrameSink,
    address: str | None,
    on_unparsed: Callable[[str, bytes], None] | None = None,
    on_device: Callable[[object], None] | None = None,
) -> None:
    from bleak import BleakScanner  # imported lazily so --demo works without BLE

    def on_adv(device, adv) -> None:
        data = adv.manufacturer_data.get(ORALB_COMPANY_ID)
        if data is None:
            return
        if address and device.address.lower() != address.lower():
            return
        if on_device:
            on_device(device)
        frame = parse(data)
        if frame:
            sink(frame, device.address, adv.rssi, data)
        elif on_unparsed:
            on_unparsed(device.address, data)

    async with BleakScanner(detection_callback=on_adv):
        await asyncio.Event().wait()


async def demo(sink: FrameSink, speed: float = 4.0) -> None:
    """Fake iO brush: idle, ~2:10 of brushing with a pressure spike, then stop."""

    def frame(state: int, secs: int, status: int = 0x32, sector: int = 0) -> bytes:
        return bytes([6, 49, 50, state, status, secs // 60, secs % 60, 0, sector, 0, 4])

    def send(raw: bytes) -> None:
        sink(parse(raw), "DEMO", -55, raw)

    for _ in range(10):
        send(frame(2, 0))
        await asyncio.sleep(0.3)
    for secs in range(0, 131):
        status = 0xB2 if 40 <= secs <= 45 or 95 <= secs <= 97 else 0x32
        sector = min(secs // 30 + 1, 4)
        for _ in range(3):
            send(frame(3, secs, status, sector))
            await asyncio.sleep(1 / speed / 3)
    while True:
        send(frame(2, 130))
        await asyncio.sleep(0.5)


PRESSURE_LEVEL_HIGH = 2  # ff0b byte 0: the brush's own pressure level (0 light, 1 normal, 2 too much)


async def zone_link(session: Session, tracker: ZoneTracker, devices: asyncio.Queue, info: dict, adv_seen) -> None:
    """Keep a GATT connection to the brush whenever it is awake; stream IMU into the tracker.

    Timer/state also come over GATT and stand in for adverts if the brush stops
    advertising while connected.
    """
    from .gatt import BrushLink

    g = {"state": 2, "secs": 0, "pressure": 0}

    def synth() -> None:
        if time.monotonic() - adv_seen() < 1.5:
            return  # adverts are flowing; they are the primary source
        status = 0x80 if g["pressure"] >= PRESSURE_LEVEL_HIGH else 0
        raw = bytes([6, 50, 0, g["state"], status, g["secs"] // 60, g["secs"] % 60, 0, 0, 0, 0])
        session.on_frame(parse(raw), time.monotonic(), time.time(), info.get("address", ""), None)

    def on_imu(t, s) -> None:
        if session.screen is Screen.BRUSHING:
            tracker.add(t, s.accel, s.gyro)

    def on_time(v: int) -> None:
        g["secs"] = v
        synth()

    def on_state(v: int) -> None:
        g["state"] = v
        synth()

    def on_pressure(level: int) -> None:
        if level != g["pressure"]:
            g["pressure"] = level
            synth()  # react at once, not on the next 1 Hz timer notification

    while True:
        dev = await devices.get()
        while not devices.empty():  # use the freshest sighting
            dev = devices.get_nowait()
        info["zones"] = "connecting…"
        link = BrushLink(on_imu=on_imu, on_time=on_time, on_state=on_state, on_pressure=on_pressure)
        try:
            await link.run(dev, asyncio.Event())
        except Exception as e:  # brush went to sleep mid-connect, etc.
            info["zones"] = f"connect failed: {type(e).__name__}"
        else:
            if not link.supports_motion:
                info["zones"] = "brush has no motion data: zones unavailable"
                return
            info["zones"] = "disconnected"
        await asyncio.sleep(3)


async def align_loop(tracker: ZoneTracker) -> None:
    """Re-estimate the grip correction every few seconds from this session's windows."""
    while True:
        await asyncio.sleep(3)
        feats = tracker.feats[-240:]  # empty for the LDA model (no grip alignment)
        if len(feats) >= ALIGN_MIN_WINDOWS:
            roll, tilt = await asyncio.to_thread(solve_alignment, tracker.model, list(feats))
            tracker.set_alignment(roll, tilt)


async def demo_zones(session: Session, tracker: ZoneTracker, speed: float = 4.0) -> None:
    """Replay IMU from the newest zone recording while the demo session is brushing."""
    files = sorted(DATA_DIR.glob("zones-*.jsonl"))
    if not files:
        return
    imu = [r for r in map(json.loads, files[-1].open()) if r.get("k") == "imu"]
    while session.screen is not Screen.BRUSHING:
        await asyncio.sleep(0.05)
    t0, start = imu[0]["t"], time.monotonic()
    for r in imu:
        delay = (r["t"] - t0) / speed - (time.monotonic() - start)
        if delay > 0:
            await asyncio.sleep(delay)
        tracker.add(r["t"], tuple(r["a"]), tuple(r["g"]) if r.get("g") else None)


async def diag(console: Console) -> None:
    """Show every advertising BLE device; Oral-B candidates highlighted."""
    from bleak import BleakScanner
    from rich.table import Table

    devices: dict[str, dict] = {}
    started = time.monotonic()

    def on_adv(device, adv) -> None:
        d = devices.setdefault(device.address, {"n": 0})
        d.update(
            name=adv.local_name or device.name or "",
            rssi=adv.rssi,
            mfr={k: v.hex(" ") for k, v in adv.manufacturer_data.items()},
            uuids=adv.service_uuids,
            seen=time.monotonic(),
        )
        d["n"] += 1

    def table() -> Table:
        elapsed = time.monotonic() - started
        t = Table(title=f"BLE scan {elapsed:.0f}s: {len(devices)} devices (Ctrl+C to stop)")
        for col in ("address", "name", "rssi", "adverts", "manufacturer data", "age"):
            t.add_column(col, overflow="fold")
        rows = sorted(devices.items(), key=lambda kv: -(kv[1]["rssi"] or -999))
        for addr, d in rows[:40]:
            oralb = ORALB_COMPANY_ID in d["mfr"] or any(
                k in d["name"].lower() for k in ("oral", "io ", "braun")
            )
            mfr = "  ".join(f"0x{k:04x}: {v}" for k, v in d["mfr"].items())
            t.add_row(
                addr, d["name"], str(d["rssi"]), str(d["n"]), mfr,
                f"{time.monotonic() - d['seen']:.0f}s",
                style="bold green" if oralb else None,
            )
        if elapsed > 5 and not devices:
            t.caption = (
                "[red]no BLE devices at all: grant Bluetooth permission to your terminal app "
                "(System Settings > Privacy & Security > Bluetooth), then restart the terminal[/red]"
            )
        return t

    async with BleakScanner(detection_callback=on_adv):
        with Live(table(), console=console, auto_refresh=False) as live:
            while True:
                await asyncio.sleep(1)
                live.update(table(), refresh=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address", help="only listen to this brush (address/UUID shown in the UI)")
    ap.add_argument("--raw", action="store_true", help="print adverts instead of the display")
    ap.add_argument("--log", metavar="FILE", help="append raw adverts to FILE")
    ap.add_argument("--demo", action="store_true", help="simulate a brushing session")
    ap.add_argument("--diag", action="store_true", help="list all BLE devices in range")
    ap.add_argument("--zones", action="store_true", help="connect to the brush and track brushing zones")
    ap.add_argument("--auto-align", action="store_true", help="correct the grip automatically (generic model)")
    args = ap.parse_args()

    console = Console()
    session = Session()
    log = open(args.log, "a", buffering=1) if args.log else None
    last_raw: dict[str, bytes] = {}
    model = load_default() if args.zones else None
    if args.zones and model is None:
        console.print("no zone model found (train one with esp-oralb-zones train); running without zones")
    tracker = ZoneTracker(model) if model else None
    last_adv = [0.0]

    def sink(frame: BrushFrame, addr: str, rssi: int | None, raw: bytes) -> None:
        now, wall = time.monotonic(), time.time()
        last_adv[0] = now
        session.on_frame(frame, now, wall, addr, rssi)
        line = (
            f"{time.strftime('%H:%M:%S')} {addr} rssi={rssi} {raw.hex(' ')}  "
            f"{frame.model_name} state={frame.state_name} t={frame.brush_seconds}s "
            f"mode={frame.mode_name} sector={frame.sector}/{frame.sectors} "
            f"timer={frame.sector_timer} status=0x{frame.status:02x}"
            f"{' HIGH' if frame.high_pressure else ''}"
        )
        if log:
            log.write(line + "\n")
        if args.raw and last_raw.get(addr) != raw:  # only print changes
            last_raw[addr] = raw
            console.print(line, highlight=False)

    def unparsed(addr: str, raw: bytes) -> None:
        line = f"{time.strftime('%H:%M:%S')} {addr} UNPARSED len={len(raw)} {raw.hex(' ')}"
        if log:
            log.write(line + "\n")
        if args.raw:
            console.print(line, style="yellow", highlight=False)

    async def run() -> None:
        if args.diag:
            await diag(console)
            return
        info: dict = {}
        devices: asyncio.Queue = asyncio.Queue()
        on_device = (lambda d: devices.put_nowait(d)) if tracker and not args.demo else None
        source = demo(sink) if args.demo else scan(sink, args.address, unparsed, on_device)
        task = asyncio.create_task(source)
        helpers = []
        if tracker:
            if args.auto_align:
                helpers.append(asyncio.create_task(align_loop(tracker)))
            if args.demo:
                helpers.append(asyncio.create_task(demo_zones(session, tracker)))
            else:
                helpers.append(asyncio.create_task(zone_link(session, tracker, devices, info, lambda: last_adv[0])))
        if args.raw:
            console.print("listening for Oral-B adverts (Ctrl+C to stop)…")
            await task
            return
        seen_session = session.session_id
        with Live(render(session, time.monotonic(), tracker), console=console, screen=True, auto_refresh=False) as live:
            while not task.done():
                now = time.monotonic()
                session.tick(now, time.time())
                if tracker and session.session_id != seen_session:
                    seen_session = session.session_id
                    tracker.reset()
                live.update(render(session, now, tracker), refresh=True)
                await asyncio.sleep(0.1)
        for h in helpers:
            h.cancel()
        task.result()

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    main()
