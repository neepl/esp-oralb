"""Screen state machine: CLOCK -> BRUSHING -> RESULT -> CLOCK.

Pure logic driven by `on_frame()` (every advertisement) and `tick()` (UI loop),
with explicit timestamps so it's testable and ports directly to the ESP32.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from .protocol import BrushFrame

TARGET_SECONDS = 120  # dentist-recommended 2 minutes
STALE_AFTER = 3.0  # no adverts for this long while brushing -> show as stale
LOST_AFTER = 10.0  # no adverts for this long while brushing -> end session
RESULT_HOLD = 60.0  # how long the final time stays on screen
MIN_SESSION = 5  # sessions shorter than this (s) are not remembered as "last"


class Screen(Enum):
    CLOCK = "clock"
    BRUSHING = "brushing"
    RESULT = "result"


@dataclass
class LastSession:
    seconds: int
    ended_at: float  # wall clock (time.time())
    high_pressure_hits: int


class Session:
    def __init__(self) -> None:
        self.screen = Screen.CLOCK
        self.frame: BrushFrame | None = None
        self.address: str | None = None
        self.rssi: int | None = None
        self.last_rx = 0.0  # monotonic time of last advert
        self.result_since = 0.0
        self.pressure_hits = 0
        self.session_id = 0  # bumps whenever a new brushing session starts
        self.brush_seconds = 0  # timer from the last *running* frame
        self._was_high = False
        self.last: LastSession | None = None

    # -- inputs -----------------------------------------------------------

    def on_frame(
        self, frame: BrushFrame, now: float, wall: float, address: str = "", rssi: int | None = None
    ) -> None:
        self.frame = frame
        self.address = address
        self.rssi = rssi
        self.last_rx = now

        if frame.running:
            if self.screen is not Screen.BRUSHING:
                # Resumed within the brush's own pause window keeps its timer,
                # so only reset counters when the brush restarted from zero.
                if self.screen is Screen.CLOCK or frame.brush_seconds <= 2:
                    self._new_session()
                self.screen = Screen.BRUSHING
            elif frame.brush_seconds + 2 < self.brush_seconds:
                # Timer restarted without an idle frame (seen on iO 8): new session.
                self._new_session()
            self.brush_seconds = frame.brush_seconds
            high = frame.high_pressure
            if high and not self._was_high:
                self.pressure_hits += 1
            self._was_high = high
        elif self.screen is Screen.BRUSHING:
            self._end(now, wall)

    def tick(self, now: float, wall: float) -> None:
        if self.screen is Screen.BRUSHING and now - self.last_rx > LOST_AFTER:
            self._end(now, wall)
        elif self.screen is Screen.RESULT and now - self.result_since > RESULT_HOLD:
            self.screen = Screen.CLOCK

    # -- derived ----------------------------------------------------------

    def stale(self, now: float) -> bool:
        return now - self.last_rx > STALE_AFTER

    def seen_recently(self, now: float, window: float = 30.0) -> bool:
        return self.frame is not None and now - self.last_rx < window

    # -- internals --------------------------------------------------------

    def _new_session(self) -> None:
        self.pressure_hits = 0
        self.session_id += 1

    def _end(self, now: float, wall: float) -> None:
        self._was_high = False
        # Use the last running timer value: idle frames may already be reset.
        secs = self.brush_seconds
        if secs >= MIN_SESSION:
            self.last = LastSession(secs, wall, self.pressure_hits)
            self.screen = Screen.RESULT
            self.result_since = now
        else:
            self.screen = Screen.CLOCK
