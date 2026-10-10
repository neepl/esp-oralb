"""Display graphics modelled on the iO handle screen.

* Brushing: a dial of radial ticks with a cyan progress dot travelling clockwise
  from 12 o'clock (one revolution = 2:00), a hub in the middle.
* Result: a large face (eyes + mouth, no outline), smiling or frowning.

Pure geometry on a square pixel grid so the ESP32 port can draw the same shapes.
"""

from __future__ import annotations

import math
from enum import Enum

from .session import TARGET_SECONDS

TICKS = 48


class Px(Enum):
    TICK_DONE = "tick_done"
    TICK_TODO = "tick_todo"
    DOT = "dot"
    HUB = "hub"
    PIP = "pip"
    FACE = "face"


Grid = list[list[Px | None]]


def progress_angle(seconds: int) -> float:
    """Clockwise degrees from 12 o'clock; one revolution per TARGET_SECONDS."""
    return min(seconds, TARGET_SECONDS) / TARGET_SECONDS * 360.0


def dial(size: int, seconds: int) -> Grid:
    c, R = (size - 1) / 2, size / 2
    prog = progress_angle(seconds)
    dot_a = math.radians(prog)
    dot_x, dot_y = c + 0.84 * R * math.sin(dot_a), c - 0.84 * R * math.cos(dot_a)
    grid: Grid = [[None] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            r = math.hypot(dx, dy)
            if math.hypot(x - dot_x, y - dot_y) <= 0.11 * R:
                grid[y][x] = Px.DOT
            elif 0.68 * R <= r <= R:
                ang = math.degrees(math.atan2(dx, -dy)) % 360
                step = 360 / TICKS
                nearest = round(ang / step) * step
                # keep ticks thin: arc distance to the tick centre under ~0.5 px
                if abs(math.radians(ang - nearest)) * r <= 0.55:
                    grid[y][x] = Px.TICK_DONE if nearest % 360 < prog or prog >= 360 else Px.TICK_TODO
            elif r <= 0.10 * R:
                grid[y][x] = Px.PIP
            elif r <= 0.30 * R:
                grid[y][x] = Px.HUB
    return grid


def face(size: int, happy: bool) -> Grid:
    c, R = (size - 1) / 2, size / 2
    grid: Grid = [[None] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            u, v = (x - c) / R, (y - c) / R  # -1..1, y down
            eye = any(math.hypot(u - ex, v + 0.35) < 0.16 for ex in (-0.45, 0.45))
            if happy:  # lower arc of a circle centred above the middle
                cy, lo, hi = -0.15, 25, 155
            else:  # upper arc of a circle centred below the middle
                cy, lo, hi = 1.05, 205, 335
            d = math.hypot(u, v - cy)
            ang = math.degrees(math.atan2(v - cy, u)) % 360
            mouth = abs(d - 0.75) < 0.1 and lo <= ang <= hi
            if eye or mouth:
                grid[y][x] = Px.FACE
    return grid
