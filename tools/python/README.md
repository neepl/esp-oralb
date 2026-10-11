# esp-oralb (Python PoC)

A live brushing display for Oral-B iO toothbrushes, replacing the SmartGuide that came
with the Triumph 7000. It only listens to the BLE advertisements the brush broadcasts
(manufacturer data, company ID `0x00DC`). It never pairs or connects, so the Oral-B app
keeps working as usual.

Screens:
- **Clock**: time of day, date, and the last brushing result.
- **Brushing**: large timer, sector/quadrant indicator, progress to 2:00, pressure warning.
- **Result**: final time for 60 s, then back to the clock.

## Run

```sh
cd python
uv run esp-oralb --demo          # simulated session, no brush needed
uv run esp-oralb --raw           # print every Oral-B advert (check the brush is seen)
uv run esp-oralb --log brush.log # live display, and record adverts to a file
uv run esp-oralb --address <id>  # lock to one brush if there are several
uv run esp-oralb --zones         # also connect to the brush and track 16 brushing zones
uv run esp-oralb --demo --zones  # demo with zones replayed from a recording
```

## Zones

The brush streams raw accelerometer/gyro data only over a GATT connection (no pairing
needed) and only while the motor runs; zones are not computed by the brush. `--zones`
connects, classifies the brush orientation every 0.5 s with a default model
(`src/esp_oralb/zone_model.json`) and automatically corrects for the user's grip
(roll/tilt) during the session, so no per-person calibration is needed. Brushes that
don't expose motion data fall back to the plain display.

```sh
uv run esp-oralb-zones record                 # guided, spoken recording of all 16 zones
uv run esp-oralb-zones eval A.jsonl B.jsonl   # train on one recording, test on another
uv run esp-oralb-zones train A.jsonl ...      # rebuild the default model
```

**No brush seen?** Check that Bluetooth is enabled in the brush's own settings menu (iO 7–10):
with it off, the brush broadcasts nothing. Then keep the brush within a few metres of the receiver.

macOS: the first BLE scan asks for Bluetooth permission for your terminal app
(System Settings → Privacy & Security → Bluetooth).

## Layout

- `src/esp_oralb/protocol.py`: decodes the advertisement bytes. No dependencies; ported 1:1 to C++.
- `src/esp_oralb/session.py`: CLOCK → BRUSHING → RESULT state machine. Pure logic, also ported.
- `src/esp_oralb/ui.py`: terminal renderer (rich).
- `src/esp_oralb/app.py`: bleak scanner, CLI, demo source.

```sh
uv run pytest
```
