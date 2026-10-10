"""Decoder for Oral-B BLE advertisements (manufacturer data, company ID 0x00DC).

Byte layout follows https://github.com/Bluetooth-Devices/oralb-ble (parser.py):

    0  protocol version
    1  model id
    2  firmware/protocol sub-version (unused)
    3  state            (3 = running, 2 = idle, ...)
    4  pressure/status  bit field: 0x80 high pressure, 0x04 mode button, 0x08 power button
    5  brushing time, minutes
    6  brushing time, seconds
    7  mode
    8  sector           low 3 bits: 0 none, 1..6 quadrant, 7 = "last" (use sector count)
    9  sector timer     (11-byte frames only)
    10 number of sectors (11-byte frames only)

Kept deliberately free of dependencies so it ports 1:1 to the ESP32 firmware.
"""

from __future__ import annotations

from dataclasses import dataclass

ORALB_COMPANY_ID = 0x00DC

STATE_RUNNING = 3

STATES = {
    0: "unknown",
    1: "initializing",
    2: "idle",
    3: "running",
    4: "charging",
    5: "setup",
    6: "flight menu",
    8: "selection menu",
    9: "off",
    10: "post brushing statistics",
    113: "final test",
    114: "pcb test",
    115: "sleeping",
    116: "transport",
}

SMART_SERIES_MODES = {
    0: "off",
    1: "daily clean",
    2: "sensitive",
    3: "massage",
    4: "whitening",
    5: "deep clean",
    6: "tongue cleaning",
    7: "turbo",
}

IO_SERIES_MODES = {
    0: "daily clean",
    1: "sensitive",
    2: "gum care",
    3: "whiten",
    4: "intense",
    5: "super sensitive",
    6: "tongue cleaning",
    8: "settings",
    9: "off",
    11: "smart adapt",
}

# Model id (byte 1) ranges -> family name. iO brushes are 48..54.
_MODEL_FAMILIES = [
    (range(0, 3), "Triumph D36"),
    (range(32, 35), "Genius D701"),
    (range(39, 42), "Smart D700"),
    (range(48, 52), "iO Series"),
    (range(52, 53), "iO Series 4"),
    (range(53, 54), "iO Series 5"),
    (range(54, 55), "iO Series"),
    (range(64, 71), "Smart D21"),
    (range(80, 88), "Pro D601"),
    (range(112, 120), "Genius X D706"),
]


def model_name(model_id: int) -> str:
    for ids, name in _MODEL_FAMILIES:
        if model_id in ids:
            return name
    return f"Oral-B #{model_id}"


def is_io(model_id: int) -> bool:
    return 48 <= model_id <= 54


@dataclass(frozen=True, slots=True)
class BrushFrame:
    protocol: int
    model_id: int
    state: int
    status: int
    minutes: int
    seconds: int
    mode: int
    sector_raw: int
    sector_timer: int | None
    sector_count: int | None

    @property
    def brush_seconds(self) -> int:
        return self.minutes * 60 + self.seconds

    @property
    def running(self) -> bool:
        return self.state == STATE_RUNNING

    @property
    def high_pressure(self) -> bool:
        # Button flags take precedence, matching oralb-ble's _decode_pressure.
        return not (self.status & 0x0C) and bool(self.status & 0x80)

    @property
    def button_pressed(self) -> bool:
        return bool(self.status & 0x0C)

    @property
    def sectors(self) -> int:
        """Number of sectors/quadrants in the current program (defaults to 4)."""
        return (self.sector_count or 0) & 0x07 or 4

    @property
    def sector(self) -> int:
        """Current sector, 1-based; 0 when not brushing."""
        if not self.running:
            return 0
        q = self.sector_raw & 0x07
        return self.sectors if q == 7 else q

    @property
    def state_name(self) -> str:
        return STATES.get(self.state, f"state {self.state}")

    @property
    def mode_name(self) -> str:
        modes = IO_SERIES_MODES if is_io(self.model_id) else SMART_SERIES_MODES
        return modes.get(self.mode, f"mode {self.mode}")

    @property
    def model_name(self) -> str:
        return model_name(self.model_id)


def parse(data: bytes) -> BrushFrame | None:
    """Decode the manufacturer-data payload (company ID already stripped)."""
    if len(data) not in (9, 11):
        return None
    long = len(data) == 11
    return BrushFrame(
        protocol=data[0],
        model_id=data[1],
        state=data[3],
        status=data[4],
        minutes=data[5],
        seconds=data[6],
        mode=data[7],
        sector_raw=data[8],
        sector_timer=data[9] if long else None,
        sector_count=data[10] if long else None,
    )
