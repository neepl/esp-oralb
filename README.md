# esp-oralb

A bathroom display for the Oral-B iO 8, replacing the SmartGuide that came with the Triumph 7000,
on a 2.4" ESP32 "Cheap Yellow Display" (ESP32-2432S024C). It listens to the brush's BLE adverts,
connects over GATT once brushing starts, and shows:

* **Clock**: time, day of the week, date, Kyiv weather icon (day/night set) and the air-raid alert status.
* **Brushing**: large timer, pressure warning and, if the brush streams IMU data, the 16 mouth zones.
* **Result**: a good/bad smiley for a few seconds after the session.

Pixel design with Ukrainian (default) or English text (`lang` setting), light-sensor backlight, screen off at night (touch wakes it),
RGB status LED, telnet log and OTA updates over Wi-Fi.

Repository layout: the firmware is at the root (PlatformIO project). `tools/python/` is the original
Python proof of concept, still used to record zone sessions and train the zone model; `tools/design/`
generates the pixel UI. The C++ in `lib/core/` is a port of the Python modules (`protocol.py`,
`session.py`, `gatt.py`, `zonemodel.py`).

## Board and pinout

Sunton **ESP32-2432S024C** ("Cheap Yellow Display" family): ESP32-WROOM-32 (no PSRAM), 2.4"
240x320 ILI9341 SPI TFT, CST816S capacitive touch (I2C), CH340 USB serial.

| Function | GPIO |
|---|---|
| TFT MOSI / MISO / SCLK | 13 / 12 / 14 |
| TFT CS / DC / RST | 15 / 2 / not connected |
| TFT backlight (PWM) | 27 |
| Touch SDA / SCL | 33 / 32 |
| Touch INT / RST | 21 / 25 |
| Touch I2C address | 0x15 (CST816S) |

Source (pin defines, ILI9341 + CST816S, BGR colour space, mirror-X):
[rzeldent/platformio-espressif32-sunton `esp32-2432S024C.json`](https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-2432S024C.json).
The same file lists the unused TF card (CS 5, MOSI 23, SCLK 18, MISO 19), RGB LED (4/16/17),
light sensor (34); the speaker amplifier on GPIO26 is unused (no speaker fitted). Pins live in `include/board.h`; the LovyanGFX config is in `src/ui.cpp`.

If the picture is mirrored/rotated or colours are swapped/inverted, override
`BOARD_TFT_OFFSET_ROTATION`, `BOARD_TFT_RGB_ORDER_BGR`, `BOARD_TFT_INVERT` via `build_flags`.

## Build, test, flash

```
pio run                  # build (PlatformIO Core 6.2+, pioarduino platform 55.03.312 = Arduino-ESP32 3.3.12 on ESP-IDF 5.5.5, mbedTLS 3.6.6)
pio test -e native       # host unit tests (needs a C++ compiler: clang++/g++)
pio run -t upload        # flash over USB (CH340)
pio device monitor       # 115200 baud
```

The board enumerates through a CH340 USB-serial chip. macOS 12+ and Linux have a driver built in;
on older macOS or on Windows install the WCH CH34x driver. If the upload cannot sync, hold BOOT
while it connects. Partition table: `min_spiffs` (two 1.9 MB app slots, needed for OTA; the firmware is ~1.3 MB).

Pinned libraries: LovyanGFX 1.2.28, NimBLE-Arduino 2.5.1, WiFiManager 2.0.17 (its one format warning is in the library). ESP-IDF 5 uses the TLSF heap allocator, which does not fragment over time like IDF 4.4. `lib_ignore = Wire` keeps LovyanGFX from linking the IDF I2C driver (IRAM overflow); touch is read by the bit-banged `src/touch.cpp`. `custom_sdkconfig` in `platformio.ini` shrinks the mbedTLS send buffer to 4 KB with dynamic buffers (the alert TLS fetch ran out of heap otherwise); it makes PlatformIO rebuild the framework libraries (~2 min on a clean build) and needs the partition table as a local file (`min_spiffs.csv`).

## First boot / Wi-Fi

1. With no saved network the setup portal opens (LED blue) and the clock screen shows
   `Wi-Fi setup: join esp-oralb-setup`. It stays open until a network is saved.
2. Join the open Wi-Fi network **esp-oralb-setup** with a phone; the captive portal opens
   (or browse to 192.168.4.1), pick your network (2.4 GHz only), enter the password.
3. The clock syncs over NTP (POSIX TZ `EET-2EEST,M3.5.0/3,M10.5.0/4`, Europe/Kyiv, `TZ_POSIX` in
   `include/config.h`). See "Network" below for always-on vs. short Wi-Fi sessions.
4. With a saved network the portal **never opens on its own**: the boot connection gets
   `WIFI_CONNECT_TIMEOUT_S` (30 s), then it keeps retrying in the background (logged as
   `wifi: saved network '...'`, `wifi: disconnected (reason N)`). Without Wi-Fi the clock shows `--:--`;
   brushing works regardless.
5. **Hold the BOOT button for 5 s** to open the setup portal on demand (3 min, LED blue); the saved network
   is kept unless you enter a new one. Touching the screen only ever wakes the backlight.

## Screens (landscape 320x240, pixel design)

Black background, pixel fonts, Ukrainian text by default (English selectable, see "Language" below). The single source of truth is `tools/design/design.py`
(`uv run --no-project --with pillow python design.py mock OUT` renders mockups; `gen` writes
`include/gfx_assets.h`; `golden` writes `test/data/golden_*.rle`). The firmware draws the same
coordinates through `lib/core/screens.cpp`, and a native test compares the result pixel for pixel.
Fonts: big 3x5 block digits and **Press Start 2P** (8x8, Cyrillic) by The Press Start 2P Project
Authors, SIL Open Font License 1.1 (`tools/design/OFL.txt`).

* **Brushing**: big `M:SS` timer (white; red under high pressure with "Тиснеш!" bottom left; cyan after
  2:00), 8 progress squares (15 s each, the current one blinks at 1 Hz), teeth chart (current zone cyan,
  coverage grey -> white at 7.5 s per zone, Л/П side labels). Without zone data (no ff0d) the chart is hidden and
  timer + progress are centred vertically.
* **Result** (60 s after the session ends, sessions < 5 s ignored): pixel face (happy from 2:00, else sad) with
  "Чудово!" / "Замало", final time, teeth chart with the coverage of the session that just ended
  (centred face and time when there is no zone data).
* **Clock**: big `HH:MM`, "Субота, 10 жовтня", weather icon + temperature (hidden until the first fetch),
  "Остання: M:SS о HH:MM", small status line ("Чекаю щітку...", or the Wi-Fi setup hint).

Rendering uses four 320x60 sprite bands (38 KB; no full-screen framebuffer, no PSRAM).
The panel is RGB565, the mockups are RGB888, so colours differ by at most 7 per channel on the device;
the native test compares RGB888 exactly.

## Language

The `lang` setting (web page select, `uk` default or `en`) switches every on-screen text live, no reboot: weekday and date line,
result captions, pressure warning, status lines, alert block, side labels and the OTA notices. `tools/design/design.py` holds one
string table per language (`LANG`, with `days`, `months` and `str`); `gen` emits both into `include/gfx_assets.h` as
`gfx::LANGS[]` and the font subset is the union of the base set and every character used by any language (so a new string
cannot silently render as `?`). The firmware picks the table through `Scene::lang`. English: "Sat, 11 October" (3-letter weekday,
the longest line "Wed, 30 September" is 272 of 320 px), "Great!" / "Too short" / "Too hard!" / "Waiting for brush...",
alert block "ALERT" / "THREAT" / "SAFE", "Kyiv", "at HH:MM" (the Ukrainian "з" is "since", but "since 20:30" does not fit beside the
icon), side labels L / R. `design.py mock OUT` renders Ukrainian into `OUT` and English into `OUT/en`; English goldens are
`test/data/golden_en_*.rle` (the Ukrainian ones are unchanged). Settings blob version 2 adds `lang`; a stored version 1 blob loads
with `uk`. The web page itself is English only.

## Weather

Open-Meteo current conditions over **plain HTTP** (`api.open-meteo.com` serves HTTP; no TLS heap cost),
location `WEATHER_LAT/LON` in `include/config.h` (Kyiv), refreshed every `WEATHER_REFRESH_MIN` (30) minutes
only while the clock screen is showing, in the same short Wi-Fi session as NTP when they coincide.
WMO codes map to icons: 0-1 clear, 2 partly, 3 cloudy, 45/48 fog, 51-67 and 80-82 rain, 71-77 and 85-86 snow, 95-99 storm.

## Backlight, night mode and LED

* **Light sensor** (CdS on GPIO34, ADC 0 dB): the backlight follows ambient light. The reading is smoothed
  (EMA over a few seconds), mapped linearly between `LDR_BRIGHT_RAW` (-> `BACKLIGHT_MAX`) and `LDR_DARK_RAW`
  (-> `BACKLIGHT_MIN`) and slew-limited, so it does not flicker. It replaces the old fixed level and idle dimming.
  The log prints `ldr raw <avg> -> bl <level>` every 10 s: use it to calibrate the two raw values in `include/config.h`
  (CYD boards usually read high in the dark and near 0 in bright light).
* **Night mode**: between `NIGHT_START_H` (23) and `NIGHT_END_H` (7), on the clock screen without brush activity the
  backlight turns fully off after `NIGHT_WAKE_S` (8) seconds. A touch wakes it for the same 8 s. Brushing turns it on,
  and after a night session the result screen stays for its normal 60 s hold, then the clock goes dark 8 s later. When on at
  night the sensor level is used. BLE keeps running (no deep sleep).
* **RGB LED** (GPIO 4/16/17, active low via `LED_ACTIVE_LOW`, brightness capped by `LED_MAX` = 40/255):
  dim green while brushing; red flashing at about 4 Hz under high pressure (takes precedence); three green blinks (1.5 s)
  when brush time reaches 2:00, then dim green again; blue while the Wi-Fi setup portal is open; otherwise off.
  Always off during night hours. The pins are driven to "off" at the very start of `setup()`; `LED_SELF_TEST`
  flashes red, green, blue at boot.

## Network

* **Always on (default, `WIFI_ALWAYS_ON 1` in `include/config.h`)**: after the Wi-Fi portal / auto-connect at boot the
  device stays connected in STA mode (auto-reconnect, default modem sleep, which BLE coexistence needs). SNTP keeps syncing
  continuously; the weather is fetched on the usual schedule (30 min, quick retries after a failure, only on the clock
  screen, never while brushing) without switching Wi-Fi off. Connect/disconnect/IP are logged
  (`wifi: connected 192.168.x.x rssi -NN`).
* **Short sessions (`WIFI_ALWAYS_ON 0`)**: Wi-Fi is switched on briefly for NTP + weather and then off again. The
  services below only exist while Wi-Fi is up, so they are effectively unavailable in this mode.
* **mDNS**: `esp-oralb.local`, advertising `_arduino._tcp` (OTA) and `_telnet._tcp`.
* **Log over telnet** (port 23, up to 2 clients, plain text): `nc esp-oralb.local 23`. Every `DIAG()` line and the ESP-IDF
  log output (`E (14644) wifi: ...`, still also sent to the UART) go to a ring of the last 200 lines (100 chars each,
  longer lines end in `~`). A new client first gets the ring, then live lines. Sending is non-blocking: a client that cannot
  keep up loses the oldest lines (`[N lines dropped]`), BLE and drawing are never stalled. The ring is static memory (~20 KB).
* **OTA update**: the first flash must be over USB. After that:
  1. `cp include/secrets.example.h include/secrets.h` and set `OTA_PASSWORD` (`secrets.h` is git-ignored; the build falls back
     to the example file with `change-me` if it is missing), then flash that firmware once over USB.
  2. `ESP_ORALB_OTA_PASSWORD=<the password> pio run -e cyd_ota -t upload` (the password is read from the environment, it is not
     stored in `platformio.ini`; the env uses `upload_port = esp-oralb.local`).
  During an update the BLE scan stops and the GATT link is dropped, the screen shows "Оновлення..." with a percentage,
  progress is logged every 10 %; on success the device reboots; on an error "Помилка оновлення" is shown for 3 s and normal
  operation resumes.

## Air-raid alert block

The bottom-right corner of the clock screen shows the alert state of one region, replacing the old "last session"
block (the session's last result is still kept in memory for a future web UI): an icon and the state (missile +
"ТРИВОГА" red; Shahed-style drone + "ЗАГРОЗА" yellow; two-tone green shield + "БЕЗПЕЧНО" when there is no alert, shown
for as long as that lasts), "з HH:MM" since when (local time) and the city. The icon art, colours and
texts are generated from `tools/design/design.py` (`ALERT_ICON`, `ALERT_STYLE`, `STR`): change them there, then run
`design.py gen` and `design.py golden`.

* **API**: `GET https://siren.pp.ua/api/v3/alerts/{ALERT_REGION_ID}` with `accept: application/json` (HTTPS only; HTTP
  answers 301; limit 50 requests/min per IP). `ALERT_REGION_ID` in `include/config.h`: `"31"` = м. Київ (default), `"14"` = Київська область.
  This reimplements the PaulAnnekov/uasiren client.
* **Mapping**: no active alerts -> clear. An alert with a "Red" level, or without a level list (a full alert) -> red. Otherwise "Yellow" -> yellow.
  Alert types are not distinguished (AIR, ARTILLERY, ...). `since` is the `createdAt` of the chosen level (the alert's `lastUpdate` as fallback),
  converted from UTC to local time. The parser is in `lib/core/alert.cpp` with native tests.
* **Polling**: every `ALERT_POLL_S` (15) s, only while the clock screen is showing (never while brushing; right away when the clock returns
  after a session), only with `WIFI_ALWAYS_ON 1` (no clock needed: certificate dates are not checked in this mbedTLS config). With `WIFI_ALWAYS_ON 0` the block stays hidden.
  If no poll has succeeded for `ALERT_MAX_AGE_S` (120) s the state becomes unknown and the block is hidden.
* **TLS**: `WiFiClientSecure` with the pinned root CA ISRG Root X1 (`include/isrg_root_x1.h`, from letsencrypt.org/certs/isrgrootx1.pem),
  one handshake per poll, connection closed afterwards. To leave heap for TLS (~40 KB) the screen is drawn in 8 bands of 320x30 (19 KB sprite).
  Serial/telnet log: `alert: <none|yellow|red> since HH:MM` on changes, `alert: fetch failed (<code>), heap free X largest Y` on errors.

## Web UI and time keeping

Needs `WIFI_ALWAYS_ON 1`. **http://esp-oralb.local/** (or the device IP, port 80), one small dark single-page app
(source `tools/web/index.html`, served gzipped from flash). **Basic auth on every URL**: user `esp-oralb`, password =
`OTA_PASSWORD` from `include/secrets.h`. POSTs also need an `X-Requested-With` header (the page sends it; it stops a
foreign web page from using your cached login). The server runs in the network task between the weather/alert fetches
(an HTTP request never overlaps a TLS handshake; a page load may wait a few seconds while a fetch runs). It stops while the
Wi-Fi setup portal is open (port 80) and during OTA, and comes back by itself.

| Endpoint | |
|---|---|
| `GET /api/status` | JSON: firmware build + SDK, uptime, heap free/largest, Wi-Fi SSID/RSSI/IP, device time + source + TZ, light sensor (smoothed raw, level, applied backlight), screen, brush last seen / last session, weather and alert state |
| `GET /api/settings`, `POST /api/settings` | form-urlencoded; any subset of the keys below; 400 `{"error":...}` on a bad value (nothing is applied) |
| `POST /api/time` | `epoch=<UTC seconds>` or `local=YYYY-MM-DDTHH:MM[:SS]` (read in the device's time zone, DST-aware); sets the clock, source `manual` |
| `POST /api/reboot` | saves the time, restarts |
| `POST /api/portal` | opens the Wi-Fi setup portal (same as holding BOOT) |

Settings (NVS namespace `cfg`, defaults = `config.h`, applied live): `lat`, `lon`, `loc` (display name only), `region` (alert region
id), `tz` (POSIX TZ string), `night_start`, `night_end`, `night_wake` (s), `ldr_bright`, `ldr_dark`, `bl_min`, `bl_max`,
`led_max`, `show_weather`, `show_alert`, `show_zones` (0/1), `lang` (`uk` default | `en`, applied on the next frame). Night with `night_start == night_end` never happens. A new
location or region invalidates the old data and refetches at once; a disabled alert stops polling; a disabled widget draws
as its "no data" state (weather/alert hidden, plain brushing screen without the teeth chart; the zone tracking itself keeps
running). The `config.h` values are only the defaults.

**Time without internet.** Source levels: `none < restored < rtc < manual < ntp`, shown in `/api/status`.
* At boot, before Wi-Fi: if the system time is valid (the IDF keeps it across software resets: RTC timer plus the boot-time
  offset in RTC store registers) it is kept, source `rtc`; otherwise the epoch last saved in NVS is restored (so the clock is
  behind by the power-off time), source `restored`. A system time more than a day behind the saved one is distrusted.
* The clock, night mode and the weather day/night icon work on any source. Weather and alerts still need Wi-Fi.
* NTP keeps syncing while Wi-Fi is up; every sync (also the hourly ones) sets source `ntp`. A manual set (`POST /api/time`) is
  source `manual` until the next NTP sync.
* The epoch is saved to NVS (one 8-byte key `cfg/epoch`) after every NTP sync, after a manual set, every 10 minutes, and before
  each intentional restart (portal save, heap safety restart, reboot endpoint, OTA end).
* The RGB LED never signalled a missing NTP sync (it has no time input), so it is unchanged.

## Zones

* Scanning is continuous (company ID `0x00DC`; the raw NimBLE manufacturer field is stripped of its
  two company-ID bytes before decoding; ~8 adverts/s drive the session state machine).
* When the brush advertises, the firmware connects as a GATT client (scanning continues) and, if
  characteristic `a0f0ff0d-5047-4d53-8208-4f72616c2d42` exists, subscribes to ff0d (IMU), ff08 (time),
  ff04 (state), ff0b (pressure, raw u16 at offset 3). If ff0d is missing, zones are unsupported: it
  never retries and the display stays plain.
* If adverts stop while connected, frames are synthesized from GATT time/state; "too much pressure" is the
  brush's own level in byte 0 of ff0b (0 light, 1 normal, 2 too much; ~2500 raw on the iO 8), applied immediately.
* IMU samples feed the tracker only while BRUSHING; the tracker resets when `session_id` changes.
  Each 0.5 s window yields 12 features (tilt, gyro mean, gyro spread, accel spread) scored by an LDA
  model and smoothed by the sticky HMM filter. There is no grip alignment with the LDA model.
  Reconnect happens 3 s after a disconnect, once the brush advertises again.
* The default model is `tools/python/src/esp_oralb/zone_model.json`, embedded as `lib/core/zone_model_data.h`
  (regenerate with `python3 tools/gen_model_header.py`).

## Layout and tests

```
include/board.h, config.h     pins, TZ, timings
src/                          Arduino code: main, ui (LovyanGFX), ble_scan/ble_link (NimBLE), net (WiFiManager+NTP)
lib/core/                     plain C++17, no Arduino: protocol, session, imu (ff0d), zonemodel, screens (drawing), weather, generated headers
test/test_core/               Unity tests (native)
test/data/                    io8_session.txt (real capture), zones_golden.txt (Python reference vectors)
tools/gen_model_header.py     zone_model.json -> C++ header
tools/web/index.html          web UI page; `uv run python tools/gen_web.py` gzips it into include/web_page.h
tools/make_golden.py          Python -> golden fixture:  uv run --project tools/python python tools/make_golden.py
tools/design/                 design.py (pixel design source of truth; gen/golden/mock), OFL.txt, Press Start 2P font
tools/python/                 Python PoC: live console display, zone recorder/trainer (uv project)
```

Native tests replay the real iO 8 capture (same assertions as `test_real_session.py`), check the ff0d
and advert decoders, and compare the C++ imu12 features (1e-9), ZoneFilter and ZoneTracker against Python
output on real zone recordings (including plain-motion packets without gyro).

