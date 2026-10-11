"""Terminal renderer (rich) that mimics the old SmartGuide LCD."""

from __future__ import annotations

import time

from rich.align import Align
from rich.console import Group
from rich.panel import Panel
from rich.style import Style
from rich.table import Table
from rich.text import Text

from . import ring
from .ring import Px
from .session import RESULT_HOLD, TARGET_SECONDS, Screen, Session
from .zonemodel import ZoneTracker
from .zones import ZONES, zone_map, zone_name

ZONE_TARGET = TARGET_SECONDS / len(ZONES)  # seconds per zone for full coverage

GFX_SIZE = 24  # pixels; rendered with half blocks -> 24 cols x 12 rows
CYAN = "#2ad4e0"

# 3x5 pixel font; each pixel is drawn as two terminal cells to keep it square-ish.
_FONT = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "001", "001", "001"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
    ":": ("0", "1", "0", "1", "0"),
    " ": ("0", "0", "0", "0", "0"),
}


def big(text: str, style: str) -> Text:
    rows = []
    for r in range(5):
        line = "  ".join(
            "".join("██" if px == "1" else "  " for px in _FONT[ch][r]) for ch in text
        )
        rows.append(line)
    return Text("\n".join(rows), style=style)


def mmss(seconds: int) -> str:
    return f"{seconds // 60}:{seconds % 60:02d}"


def gfx_text(grid: ring.Grid, colors: dict) -> Text:
    """Draw a pixel grid with half-block characters (two pixels per terminal cell)."""
    t = Text()
    size = len(grid)
    for y in range(0, size, 2):
        for x in range(size):
            top, bot = colors.get(grid[y][x]), colors.get(grid[y + 1][x])
            if top and bot:
                t.append("▀", Style(color=top, bgcolor=bot))
            elif top:
                t.append("▀", Style(color=top))
            elif bot:
                t.append("▄", Style(color=bot))
            else:
                t.append(" ")
        t.append("\n")
    t.rstrip()
    return t




def _status_line(s: Session, now: float) -> Text:
    if not s.seen_recently(now, 3600):
        return Text("waiting for toothbrush…", style="grey50")
    f = s.frame
    ago = now - s.last_rx
    rssi = f"  RSSI {s.rssi}" if s.rssi is not None else ""
    return Text(f"{f.model_name}  ·  {f.state_name}  ·  seen {ago:.0f}s ago{rssi}", style="grey50")


def _clock(s: Session, now: float, zt: ZoneTracker | None = None) -> Group:
    wall = time.localtime()
    colon = ":" if wall.tm_sec % 2 == 0 else " "
    parts = [
        Align.center(big(time.strftime("%H", wall) + colon + time.strftime("%M", wall), "bold cyan")),
        Text(""),
        Align.center(Text(time.strftime("%A, %d %B", wall), style="cyan")),
        Text(""),
    ]
    if s.last:
        ok = s.last.seconds >= TARGET_SECONDS
        when = time.strftime("%H:%M", time.localtime(s.last.ended_at))
        parts.append(
            Align.center(
                Text.assemble(
                    ("last brushing  ", "grey70"),
                    (mmss(s.last.seconds), "bold green" if ok else "bold yellow"),
                    (f"  at {when}", "grey70"),
                )
            )
        )
    parts.append(Align.center(_status_line(s, now)))
    return Group(*parts)


def _with_zones(main, zt: ZoneTracker | None, highlight: bool):
    """Put the 16-zone coverage map next to the main graphic when zone tracking is on."""
    if zt is None:
        return main
    cov = {z: zt.coverage.get(z, 0.0) / ZONE_TARGET for z in ZONES}
    g = Table.grid(padding=(0, 3))
    g.add_column(vertical="middle")
    g.add_column(vertical="middle")
    g.add_row(main, zone_map(zt.current if highlight else None, cov))
    return g


def _zone_status(zt: ZoneTracker | None) -> list:
    if zt is None:
        return []
    zone = zone_name(zt.current) if zt.current else "…"
    grip = f"grip auto-align roll {zt.roll:+.0f}° tilt {zt.tilt:+.0f}°" if zt.feats else "grip: default"
    return [Align.center(Text(f"zone: {zone}   ·   {grip}", style="grey50"))]


def _brushing(s: Session, now: float, zt: ZoneTracker | None = None) -> Group:
    f = s.frame
    secs = s.brush_seconds
    high = f.high_pressure and f.running
    style = "bold red" if high else ("bold green" if secs >= TARGET_SECONDS else "bold white")
    pressure = (
        Text(" ! TOO MUCH PRESSURE ! ", style="bold white on red")
        if high
        else Text(f"pressure ok  ·  warnings: {s.pressure_hits}", style="grey50")
    )
    parts = [
        Align.center(Text(f"{f.model_name}  ·  {f.mode_name}", style="grey70")),
        Text(""),
        Align.center(
            _with_zones(
                gfx_text(
                    ring.dial(GFX_SIZE, secs),
                    {
                        Px.TICK_DONE: CYAN if secs >= TARGET_SECONDS else "white",
                        Px.TICK_TODO: "grey35",
                        Px.DOT: "red" if high else CYAN,
                        Px.HUB: "grey35",
                        Px.PIP: "white",
                    },
                ),
                zt,
                highlight=True,
            )
        ),
        Text(""),
        Align.center(big(mmss(secs), style)),
        Text(""),
        Align.center(pressure),
        *_zone_status(zt),
    ]
    if s.stale(now):
        parts.append(Align.center(Text(f"signal lost {now - s.last_rx:.0f}s…", style="yellow")))
    return Group(*parts)


def _result(s: Session, now: float, zt: ZoneTracker | None = None) -> Group:
    last = s.last
    ok = last.seconds >= TARGET_SECONDS
    msg = "great job! 2 minutes done" if ok else f"{mmss(TARGET_SECONDS - last.seconds)} short of 2:00"
    left = max(0, RESULT_HOLD - (now - s.result_since))
    face = gfx_text(ring.face(GFX_SIZE, ok), {Px.FACE: CYAN})
    return Group(
        Align.center(_with_zones(face, zt, highlight=False)),
        Text(""),
        Align.center(big(mmss(last.seconds), "bold white")),
        Text(""),
        Align.center(Text(msg, style="green" if ok else "yellow")),
        Align.center(Text(f"pressure warnings: {last.high_pressure_hits}", style="grey50")),
        Align.center(Text(f"clock in {left:.0f}s", style="grey37")),
    )


def render(s: Session, now: float, zt: ZoneTracker | None = None) -> Panel:
    body = {Screen.CLOCK: _clock, Screen.BRUSHING: _brushing, Screen.RESULT: _result}[s.screen](s, now, zt)
    return Panel(body, title="esp-oralb", subtitle=s.address or "", width=72 if zt else 64, padding=(1, 2))
