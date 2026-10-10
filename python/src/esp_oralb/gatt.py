"""GATT connection to an Oral-B iO brush: live timer/state/pressure and IMU stream.

Characteristic map and sensor packet layout from
https://github.com/MatrixEditor/oralb-io (oralb/blesdk/model.py). Reads and
notification subscriptions only; nothing is written to the brush.
"""

from __future__ import annotations

import asyncio
import struct
from collections.abc import Callable
from dataclasses import dataclass

from bleak import BleakClient, BleakScanner

from .protocol import ORALB_COMPANY_ID


def _uuid(short: str) -> str:
    return f"a0f0{short.lower()}-5047-4d53-8208-4f72616c2d42"


CH_STATE = _uuid("ff04")  # [state, ?]
CH_BATTERY = _uuid("ff05")  # [percent, ...]
CH_BUTTON = _uuid("ff06")
CH_MODE = _uuid("ff07")
CH_TIME = _uuid("ff08")  # [minutes, seconds]
CH_QUADRANT = _uuid("ff09")
CH_PRESSURE = _uuid("ff0b")  # [level 0/1/2 (2 = too much), ts u16, raw u16, prev ts u16, prev raw u16, 0x37]
CH_SENSOR = _uuid("ff0d")  # 20-byte IMU packets


@dataclass(frozen=True, slots=True)
class ImuSample:
    ts: int  # brush clock, wraps at 16 bit
    gyro: tuple[int, int, int] | None
    accel: tuple[int, int, int]


def decode_sensor(data: bytes) -> list[ImuSample]:
    """Decode one ff0d notification into IMU samples (empty if not motion data)."""
    if len(data) != 20:
        return []
    if data[-1] != 0x80:  # plain motion data: 4 x (ts u16, accel xyz i8)
        out = []
        for i in range(0, 20, 5):
            ts, ax, ay, az = struct.unpack_from("<Hbbb", data, i)
            out.append(ImuSample(ts, None, (ax, ay, az)))
        return out
    if data[-2] == 0x10:  # "comino": 2 x 8 bytes (ts u16, gyro xyz i8, accel xyz i8)
        out = []
        for i in (0, 8):
            ts, gx, gy, gz, ax, ay, az = struct.unpack_from("<Hbbbbbb", data, i)
            out.append(ImuSample(ts, (gx, gy, gz), (ax, ay, az)))
        return out
    return []  # high-resolution / calibration / dashboard packets: not used yet


class BrushLink:
    """Connect to the first Oral-B brush seen and stream its data to callbacks."""

    def __init__(
        self,
        on_imu: Callable[[float, ImuSample], None] | None = None,
        on_time: Callable[[int], None] | None = None,
        on_state: Callable[[int], None] | None = None,
        on_pressure: Callable[[int], None] | None = None,
        on_raw: Callable[[float, str, bytes], None] | None = None,
        address: str | None = None,
    ) -> None:
        self.on_imu, self.on_time, self.on_state = on_imu, on_time, on_state
        self.on_pressure, self.on_raw, self.address = on_pressure, on_raw, address
        self.client: BleakClient | None = None
        self.supports_motion = False

    async def find(self, timeout: float | None = None):
        def match(d, a) -> bool:
            if ORALB_COMPANY_ID not in a.manufacturer_data:
                return False
            return not self.address or d.address.lower() == self.address.lower()

        return await BleakScanner.find_device_by_filter(match, timeout=timeout or 1e9)

    async def run(self, device, until: asyncio.Event) -> None:
        loop = asyncio.get_running_loop()
        async with BleakClient(device, timeout=20) as c:
            self.client = c
            chars = {ch.uuid for s in c.services for ch in s.characteristics}
            self.supports_motion = CH_SENSOR in chars

            def sub(uuid: str, handler: Callable[[bytes], None]) -> asyncio.Future:
                def cb(_, data: bytearray) -> None:
                    t = loop.time()
                    if self.on_raw:
                        self.on_raw(t, uuid[4:8], bytes(data))
                    handler(bytes(data), t)

                return c.start_notify(uuid, cb)

            if self.supports_motion and self.on_imu:
                await sub(CH_SENSOR, lambda d, t: [self.on_imu(t, s) for s in decode_sensor(d)])
            if CH_TIME in chars and self.on_time:
                await sub(CH_TIME, lambda d, t: len(d) >= 2 and self.on_time(d[0] * 60 + d[1]))
            if CH_STATE in chars and self.on_state:
                await sub(CH_STATE, lambda d, t: d and self.on_state(d[0]))
            if CH_PRESSURE in chars and self.on_pressure:
                await sub(CH_PRESSURE, lambda d, t: len(d) >= 5 and self.on_pressure(d[0]))  # brush's own level
            # Everything else that notifies is only logged raw (e.g. ff09 quadrant, ff21 control).
            if self.on_raw:
                handled = {CH_SENSOR, CH_TIME, CH_STATE, CH_PRESSURE}
                for s in c.services:
                    for ch in s.characteristics:
                        if ch.uuid not in handled and "notify" in ch.properties:
                            try:
                                await sub(ch.uuid, lambda d, t: None)
                            except Exception:  # e.g. ff0c needs an encrypted link
                                pass
            while c.is_connected and not until.is_set():
                await asyncio.sleep(0.2)
        self.client = None
