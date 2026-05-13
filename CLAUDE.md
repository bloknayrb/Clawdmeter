# Project context

ESP32-C6 firmware for a desk-side Claude Code usage monitor on a **Waveshare
ESP32-C6-Touch-AMOLED-1.47** board (368×448 portrait AMOLED). Connects to a
Windows host daemon over BLE; daemon polls Anthropic API for usage data and
pushes a JSON payload to the device.

This file is for future Claude Code sessions to bootstrap quickly. Read this
first.

## Status: mid-port — Arduino → ESP-IDF, S3 → C6

The project was originally built for the Waveshare ESP32-S3-Touch-AMOLED-2.16
on Arduino/pioarduino. That code lives in git history at the parent of the
port commit. NimBLE-Arduino doesn't support ESP32-C6 (issue #685, wontfix),
so the current direction is a full rewrite to **ESP-IDF + esp-nimble-cpp**
targeting the C6 board.

The Windows daemon (`daemon/claude-usage-daemon.py`) and the BLE protocol
(service UUIDs, JSON payload, REQ-notify refresh) are unchanged — only the
firmware side is being rewritten.

The implementation plan is at
`C:\Users\blokn\.claude\plans\i-want-to-port-radiant-quokka.md`. Read it
before starting any firmware work.

## Hardware (current target)

- Board: Waveshare ESP32-C6-Touch-AMOLED-1.47
- Display: **SH8601** AMOLED via QSPI (CS=5, SCLK=0, D0–3=1..4, RST=11)
- Touch: **FT3168** via I2C (SDA=8, SCL=7, INT=15, RST=10) — driven by the
  Espressif `esp_lcd_touch_ft5x06` component (Focaltech protocol family)
- PMU: **AXP2101** on the same I2C bus, addr 0x34
- Button: **GPIO 9** (BOOT) — short press = HID Space, long press = Shift+Tab
- IMU: **none** on this board; no auto-rotation
- RAM: 512 KB SRAM, **no PSRAM**
- Flash: 16 MB

## Reuse map

The vox-memo project at `C:\Users\blokn\GitHub\vox-memo` runs on the same
physical board and provides the following components verbatim or near-verbatim:

- `firmware/components/esp_lcd_sh8601/` — SH8601 panel driver, copy directly
- `firmware/main/display.c` + `touch.c` — display/touch init reference
- `firmware/main/axp2101.c/h` — AXP2101 driver (rename API surface to `power_*`)

The following come forward from the old Arduino firmware (preserved in git
history at the pre-port commit):

- `font_*.c` — LVGL 9 bitmap fonts (Tiempos, Styrene, Mono variants)
- `splash_animations.h` — 13 × 20×20 pixel-art creature animation frames
- `icons.h` — battery icons (RGB565A8 alpha) + raw RGB565 icons
- `theme.h` — color constants
- `data.h` — `UsageData` struct
- `ui.cpp` logic — translate to C/ESP-IDF LVGL, resize to 368×448
- `ble.cpp` logic — translate from NimBLE-Arduino to esp-nimble-cpp

## Architecture (target layout)

```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults    (target=esp32c6, NimBLE + LVGL flags)
├── partitions.csv
├── components/
│   └── esp_lcd_sh8601/   (from vox-memo)
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml (lvgl, esp_lvgl_port, esp_lcd_touch_ft5x06,
    │                      esp-nimble-cpp)
    ├── main.c            (app_main → display + ble + button tasks)
    ├── display.c/h       (SH8601 panel + LVGL strip buffers + FT3168 touch)
    ├── ble.cpp/h         (esp-nimble-cpp peripheral: data svc + HID kb)
    ├── ui.c/h            (3 screens: splash, usage, bluetooth)
    ├── splash.c/h        (lv_image with nearest-neighbor scaling, no canvas)
    ├── splash_animations.h, icons.h, theme.h, data.h, fonts/font_*.c
    ├── power.c/h         (AXP2101)
    └── button.c/h        (GPIO 9 short/long press)
```

## Build / flash

ESP-IDF v5.3+ via `idf.py` (CMake). Drop PlatformIO. On Windows, use the
ESP-IDF PowerShell launched from the IDF Tools Installer.

```powershell
idf.py -C firmware set-target esp32c6
idf.py -C firmware build
idf.py -C firmware -p COM3 flash monitor
```

The C6 device shows up as a USB Serial/JTAG port (typically `COM3` if no
other USB serial is present). To list COM ports from PowerShell:

```powershell
[System.IO.Ports.SerialPort]::getportnames()
```

No boot-mode button gymnastics — the C6's USB Serial/JTAG interface stays
active across resets, so `idf.py flash` works directly.

## BLE protocol (host ↔ device)

Custom GATT service `4c41555a-4465-7669-6365-000000000001`:

- `...0002` **RX** — host writes JSON usage payload here. Format:
  `{"s":<5h_pct>,"sr":<5h_reset_min>,"w":<7d_pct>,"wr":<7d_reset_min>,
  "st":"<status>","ok":true}`. Max ~80 bytes.
- `...0003` **TX** — device notifies `{"ack":true}` on parse success or
  `{"err":true}` on failure. Daemon doesn't subscribe.
- `...0004` **REQ** — device fires a `0x01` notify on subscribe if it has
  no usage data yet (post-boot). Daemon subscribes and re-polls immediately
  when this fires.

Plus a standard HID-over-GATT keyboard service (UUID `0x1812`) for the
GPIO 9 button to send Space / Shift+Tab to the connected host.

## Daemon (Windows)

`daemon/claude-usage-daemon.py` — Python 3 with `bleak` and `httpx`, runs via:

```powershell
uv run daemon/claude-usage-daemon.py
```

Reads OAuth token from `%USERPROFILE%\.claude\.credentials.json`, polls
`api.anthropic.com/v1/messages` every 60s for the rate-limit headers, sends
JSON over BLE. Caches the device's BLE address at
`%APPDATA%\claude-usage-monitor\ble-address`. Bonding is handled by the
Windows BT stack on first connect — no manual pairing step.

Detail: WinRT requires the device to be advertising for `BleakClient` to
connect (unlike BlueZ which can queue connections). The daemon re-scans by
address before each connect to get a fresh `BLEDevice` object.

The legacy bash daemon (`daemon/claude-usage-daemon.sh`) is Linux-only and
no longer the recommended path.

## Critical gotchas

1. **NimBLE-Arduino does not work on ESP32-C6** (maintainer wontfix). All
   BLE code must be esp-nimble-cpp on ESP-IDF.
2. **No PSRAM on the C6.** All buffers live in internal SRAM. LVGL strip
   buffers are sized at `368×20×2 = 14.7 KB × 2`. Watch
   `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` at startup; if
   it drops below 40 KB, shrink the strips.
3. **Splash uses LVGL nearest-neighbor scaling, not a back-canvas.** The
   old Arduino code pre-rendered a 480×480×2 = 460 KB canvas in PSRAM. On
   the C6 there's no room for that. The new approach: keep a 20×20 RGB565
   source buffer (800 bytes), display via `lv_image` with
   `lv_image_set_antialias(false)` and `lv_image_set_scale(img, 256*18)`
   for 18× upscale → 360×360 centered.
4. **LVGL access from non-LVGL tasks must be locked.** Use the
   `lvgl_port_lock()` / `lvgl_port_unlock()` pattern around all LVGL calls
   from the BLE task or button task.
5. **Touch swap/mirror calibration is empirical.** The S3 board used
   `setSwapXY(true)` + `setMirrorXY(true,false)`. The C6 may differ —
   calibrate against vox-memo's settings if drifting.
6. **HID descriptor parity**: esp-nimble-cpp's `NimBLEHIDDevice` is
   modeled on NimBLE-Arduino's, but verify the descriptor builds and
   advertises before assuming the Arduino-era HID code translates 1:1.

## User profile / preferences

See `~/.claude/projects/C--Users-blokn-GitHub-Clawdmeter/memory/` files for
persistent context (embedded-beginner senior dev, brand-conscious, prefers
iterative UI refinement, dislikes me authoring my own art when third-party
assets are intended, wants agents dispatched at appropriate model power for
task complexity). Always read those memory files at session start.

## Splash animations

13 × 20×20 pixel-art creature animations sourced from
[claudepix.vercel.app](https://claudepix.vercel.app). Pipeline (Node.js,
runs once at content-update time):

```bash
node tools/scrape_claudepix.js  # → tools/claudepix_data/*.json
node tools/convert_to_c.js      # → splash_animations.h
```

Each animation has a per-animation 10-color RGB565 palette. Cell values 0..9
index it. `splash_animations.h` is generated — do not hand-edit.
