# Pisces Moon OS — Desktop Dev Environment

Develop for Pisces Moon on your laptop. No T-Deck Plus required.  
Write real C++ against the real HAL. Test on desktop in seconds. Flash when ready.

**GitHub:** github.com/FluidFortune/emulator  
**Live Demo:** piscesdemo.fluidfortune.com  
**License:** AGPL-3.0-or-later

---

## ⚠️ KodeDot Emulation — Important Notice

The `kodedot_sdl2` build target is **hypothetical**. It is based entirely
on KodeDot's publicly available block diagram and Kickstarter campaign
specifications as of May 2026. No production hardware has been tested.
No official pinouts, schematics, or SDK have been released by KodeDot.

**What this means for developers:**

We are reasonably confident that Pisces Moon apps written against the
standard HAL will run on KodeDot hardware when it ships, because:
- The main processor (ESP32-P4) runs the same Xtensa LX7 architecture
- The ELF module ABI is hardware-independent by design
- The SPI Bus Treaty pattern applies identically to their dual-chip architecture
- The D-pad maps 1:1 to the trackball convention already used throughout

However, **we cannot guarantee compatibility** until a production unit
can be physically tested and the HAL confirmed against real hardware.
Every unknown pin assignment is marked `// TODO: confirm` in
`hal_kodedot.h`. When official specs land, those TODOs resolve and
the HAL fills in — app code does not change.

**Develop with confidence, deploy after confirmation.**

---

## What You Get

Three build targets, one codebase:

| Command | Target | Display | Status |
|---|---|---|---|
| `pio run -e esp32s3` | T-Deck Plus | 320×240 IPS | ✅ Production — flash to real hardware |
| `pio run -e sdl2` | T-Deck SDL2 | 320×240 window | ✅ Validated — mirrors real hardware exactly |
| `pio run -e kodedot_sdl2` | KodeDot SDL2 | 480×480 window | ⚠️ Hypothetical — based on block diagram only |

All three build the same OS, the same apps, and load the same ELF modules.  
App code never changes between targets. The HAL handles everything.

---

## Prerequisites

### 1. Install PlatformIO

```bash
pip install platformio
```

Or install the PlatformIO extension in VS Code.

### 2. Install SDL2

**Ubuntu / Debian:**
```bash
sudo apt install libsdl2-dev build-essential
```

**macOS:**
```bash
brew install sdl2
```

**Windows:**  
WSL2 with Ubuntu is the recommended path. Native Windows builds require
manually installing SDL2 dev libs and adding them to PATH.

### 3. Clone the repo

```bash
git clone https://github.com/FluidFortune/emulator.git
cd PiscesMoon
```

### 4. Set up your SD card directory

The SDL2 builds use `./sd/` as the MicroSD card root.
This directory is created automatically on first run, but you can
pre-populate it:

```
./sd/
├── apps/              ← Drop .elf modules here
├── cyber_logs/        ← Auto-created by Ghost Engine
├── roms/
│   ├── nes/           ← NES ROMs (.nes) for Retro app
│   └── gb/            ← Game Boy ROMs (.gb, .gbc)
├── payloads/          ← DuckyScript payloads for Ducky apps
└── nfc_tags.json      ← NFC/RFID test tags (see NFC section)
```

---

## Build and Run

### T-Deck Plus emulator

```bash
pio run -e sdl2
.pio/build/sdl2/program
```

A 640×480 window opens (320×240 at 2× scale). Pisces Moon boots — BIOS
screen, Ghost Engine, splash, launcher. All 47 apps available.

### KodeDot emulator

```bash
pio run -e kodedot_sdl2
.pio/build/kodedot_sdl2/program
```

A 480×480 window opens. Same OS, same apps, square AMOLED format.
KodeDot-specific peripherals (NFC, RFID, IR, IMU, Haptic) are emulated.

### Flash to real T-Deck Plus

```bash
pio run -e esp32s3 -t upload
```

---

## Controls

### T-Deck SDL2 (`-e sdl2`)

```
Mouse left-click / drag  →  Touch screen (mapped to 320×240)
Arrow keys ↑↓←→          →  Trackball navigation
Right-Alt + Enter        →  Trackball click (open / select)
All printable keys       →  T-Deck QWERTY keyboard
Enter                    →  Return / confirm
Backspace                →  Delete character
Window close             →  Clean shutdown
```

### KodeDot SDL2 (`-e kodedot_sdl2`)

```
Mouse left-click / drag  →  Touch screen (mapped to 480×480)
Arrow keys ↑↓←→          →  D-pad
Enter / Right-Alt+Enter  →  OK / Goto button
F1                       →  Top physical button
F2                       →  Bottom physical button
F3                       →  Left physical button
All printable keys       →  Keyboard
Window close             →  Clean shutdown
```

---

## Writing a Built-In App

Built-in apps live in the main repo as `.cpp` files and wire into the launcher.
They compile for all targets without changes.

**1. Create your app file (`src/my_app.cpp`):**

```cpp
// Pisces Moon OS
// Copyright (C) 2026 Your Name
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "touch.h"
#include "trackball.h"
#include "keyboard.h"
#include "theme.h"

extern Arduino_GFX* gfx;

void run_my_app() {
    gfx->fillScreen(C_BLACK);

    // Standard header
    gfx->fillRect(0, 0, 320, 24, C_DARK);
    gfx->drawFastHLine(0, 24, 320, C_GREEN);
    gfx->setCursor(10, 7);
    gfx->setTextColor(C_GREEN);
    gfx->setTextSize(1);
    gfx->print("MY APP | TAP HEADER TO EXIT");

    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(10, 50);
    gfx->print("Hello Pisces Moon");

    while (true) {
        int16_t tx, ty;

        // CRITICAL: ty < 40 is the global header tap convention
        if (get_touch(&tx, &ty) && ty < 40) {
            while (get_touch(&tx, &ty)) { delay(10); yield(); }
            return;
        }

        TrackballState tb = update_trackball();
        if (tb.clicked) { /* select */ }
        if (tb.x == -1) { /* left  */ }
        if (tb.x ==  1) { /* right */ }
        if (tb.y == -1) { /* up    */ }
        if (tb.y ==  1) { /* down  */ }

        char k = get_keypress();
        if (k == 'q') return;

        delay(50);
        yield();
    }
}
```

**2. Add declaration to `include/apps.h`:**

```cpp
void run_my_app();
```

**3. Add to `src/launcher.cpp`:**

In the APP IDs section:
```cpp
#define APP_MY_APP  49   // next available ID
```

In your chosen category:
```cpp
{ "TOOLS", "T", 0x8C00,
  { ...existing apps...,
    {"MY APP", APP_MY_APP} },
  6 },  // increment appCount
```

In `launchApp()` switch:
```cpp
case APP_MY_APP: run_my_app(); break;
```

**4. Test on desktop, then flash:**
```bash
pio run -e sdl2 && .pio/build/sdl2/program   # test
pio run -e esp32s3 -t upload                  # flash
```

---

## Writing an ELF Module

ELF modules are MicroSD-resident apps. No reflash to install them.
They load into PSRAM at runtime via the ELF Engine.

**1. Write your module source:**

```cpp
// my_module.cpp
#include "elf_loader.h"

extern "C" int elf_main(void* ctx_ptr) {
    ElfContext* ctx = (ElfContext*)ctx_ptr;
    Arduino_GFX* gfx = ctx->gfx;

    gfx->fillScreen(0x0000);
    gfx->setTextColor(0x07E0);
    gfx->setTextSize(2);
    gfx->setCursor(10, 50);
    gfx->print("ELF Module Running");

    // Full ElfContext fields available:
    // ctx->gfx          Display driver
    // ctx->sd           SdFat instance
    // ctx->screen_w     320 (T-Deck) or 480 (KodeDot)
    // ctx->screen_h     240 (T-Deck) or 480 (KodeDot)
    // ctx->wifi_in_use  SPI Bus Treaty traffic flag
    // ctx->lora_active  SPI Bus Treaty traffic flag
    // ctx->rom_path     ROM path if launched from Retro app
    // ctx->api_major    ABI version (currently 1)

    return 0;   // 0 = clean exit, -1 = error
}
```

**2. Create a JSON sidecar (`my_module.json`):**

```json
{
  "name":     "My Module",
  "version":  "1.0.0",
  "author":   "Your Name",
  "category": "TOOLS",
  "elf":      "my_module.elf",
  "psram_kb": 512,
  "api":      [1, 0]
}
```

**3. Compile:**

```bash
pio run -e esp32s3
# Output: .pio/build/esp32s3/firmware.elf
# Rename/extract your module ELF from the build
```

Required flags (already in platformio.ini):
```
-fPIC -mfix-esp32-psram-cache-issue --entry elf_main
```

**4. Deploy:**

Copy `my_module.elf` and `my_module.json` to:
- SDL2: `./sd/apps/`
- Real hardware: `apps/` on MicroSD

**5. Launch:** SYSTEM → ELF APPS → select your module.

---

## SPI Bus Treaty

All code touching SPI (SD, display, LoRa, NFC) must take the mutex first.
On real hardware this prevents bus corruption. In SDL2 it's a real
pthread mutex — violations show as deadlocks in dev, not random crashes
on hardware. The behavior is identical by design.

```cpp
extern SemaphoreHandle_t spi_mutex;

if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    // SD / SPI operation
    xSemaphoreGive(spi_mutex);
} else {
    // handle busy
}
```

Timeout reference: 3000ms init, 2000ms LoRa TX, 500ms SD access, 20ms polling.

**KodeDot adds a second mutex** for the ESP32-C5 co-processor SDIO bus:

```cpp
extern SemaphoreHandle_t c5_mutex;  // KodeDot only

if (c5_mutex && xSemaphoreTake(c5_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    // WiFi / BLE call via C5
    xSemaphoreGive(c5_mutex);
}
```

---

## NFC / RFID Development

SDL2 builds include full NFC and RFID emulation in three modes.

### Mode 1: Mock (default, no hardware needed)

Tags appear and disappear on automatic cycles:
- NFC: present 2 seconds, absent 6 seconds
- RFID: present 1.5 seconds, absent 8.5 seconds

### Mode 2: File

Edit `./sd/nfc_tags.json`:

```json
{
  "nfc": {
    "type": "A",
    "uid": "04A3F211C25E80",
    "ndef_text": "Your tag text here"
  },
  "rfid": {
    "type": "EM4100",
    "id": "00DEADC0"
  }
}
```

Enable before init:
```cpp
nfc_set_mode(NFC_MODE_FILE);
nfc_init();
```

### Mode 3: Bridge (live hardware)

Flash `PiscesMoon_PeripheralBridge.ino` to any ESP32-S3 dev board,
wire up your reader, then:

```bash
.pio/build/sdl2/program --nfc-bridge /dev/ttyUSB0
```

### App API (same in all modes)

```cpp
#include "nfc_sdl2.h"

nfc_init();

NfcTag tag = nfc_poll();
if (tag.valid) {
    // tag.uid[], tag.uid_len, tag.ndef_text[], tag.data[]
}

RfidTag rfid = rfid_poll();
if (rfid.valid) {
    // rfid.id (uint32_t), rfid.id_str (hex string)
}

nfc_write(&tag, data, len);
```

---

## Peripheral Bridge Sketch

`sdl2/PiscesMoon_PeripheralBridge/PiscesMoon_PeripheralBridge.ino`

Flash this to any ESP32-S3 dev board to feed live hardware data into
the SDL2 emulator. No T-Deck Plus required.

**Supported hardware:**
- NFC: MFRC522 breakout (SPI) or ST25R3916B (KodeDot-native)
- RFID 125kHz: RDM6300 or ID-12LA (UART)
- GPS: Any NMEA module (TinyGPSPlus-compatible)

**Default wiring (edit CONFIG block in sketch):**

```
MFRC522 NFC:   SDA→GPIO8  SCL→GPIO9  GND→GND  3V3→3.3V
RFID Reader:   TX→GPIO16  GND→GND    5V→5V
GPS Module:    TX→GPIO17  GND→GND    3V3→3.3V
```

**PlatformIO deps for the sketch:**
```ini
lib_deps =
    miguelbalboa/MFRC522 @ ^1.4.10
    mikalhart/TinyGPSPlus @ 1.0.3
    bblanchon/ArduinoJson @ 7.4.3
```

Uses the same JSON protocol as `bridge_app.cpp` and the web demo.

---

## KodeDot Development

> **⚠️ Hypothetical Target**  
> This build is based on KodeDot's block diagram and proposed specs as of May 2026.  
> No production hardware has been tested. Pin assignments are estimated or unknown.  
> Apps developed here are expected to run on KodeDot hardware when it ships,  
> but full compatibility cannot be confirmed until a production unit is in hand.

`pio run -e kodedot_sdl2` runs Pisces Moon emulating the KodeDot
hardware stack based on the known block diagram (May 2026).

**Emulated hardware:**

| Peripheral | Status | Notes |
|---|---|---|
| 480×480 AMOLED | ✓ Emulated | Resolution theoretical — confirm with spec |
| D-pad TCA6408 | ✓ Emulated | Arrow keys + F1/F2/F3 |
| NFC ST25R3916B | ✓ Emulated | Mock / Bridge / File modes |
| 125kHz RFID | ✓ Emulated | Same modes as NFC |
| IR TX/RX | ✓ Emulated | Loopback |
| IMU LSM6DSV5X | ✓ Emulated | Slowly varying mock values |
| Magnetometer LIS2MDL | ✓ Emulated | Rotating heading 0-360° |
| Haptic DRV2605TR | ✓ Emulated | stdout + window title flash |
| ESP32-C5 WiFi6/BLE5 | ✓ Simulated | Always-available + c5_mutex |
| 20-pin expansion | Stubbed | GPIO assignments TODO |

**TODOs** — everything in `hal_kodedot.h` marked `// TODO: confirm`
fills in when official pinouts land. App code does not change.

**KodeDot-specific API:**

```cpp
kd_haptic_play(1);              // Strong click
kd_haptic_play(5);              // Buzz

kd_ir_send(0xA1B2C3D4, "NEC");
uint32_t code;
if (kd_ir_receive(&code)) { }

float ax, ay, az;
kd_imu_get_accel(&ax, &ay, &az);

float heading;
kd_mag_get_heading(&heading);

if (kd_c5_wifi_available()) { }
if (kd_c5_ble_available())  { }
if (kd_c5_154_available())  { }  // 802.15.4
```

**Legacy app scaling:**

Existing 320×240 apps scale to 480×480 automatically.
New KodeDot-native apps should target `KD_SCREEN_W` / `KD_SCREEN_H`.

---

## Hardware Abstraction Summary

What the HAL hides from your app:

| | T-Deck | KodeDot | SDL2 |
|---|---|---|---|
| Display | ST7789 SPI | AMOLED (TBD) | SDL2 texture |
| Touch | GT911 I2C | Unknown (TBD) | Mouse |
| Navigation | Trackball GPIO | D-pad TCA6408 | Arrow keys |
| Storage | SdFat SPI | SdFat SPI | `./sd/` POSIX |
| PSRAM | 8MB via SPI | 32MB embedded P4 | `malloc()` |
| Core 0 | FreeRTOS task | FreeRTOS task | pthread |
| SPI Treaty | FreeRTOS mutex | FreeRTOS + c5 mutex | pthread mutex |
| NFC | — | ST25R3916B | nfc_sdl2 |
| RFID | — | Analog module | nfc_sdl2 |

Your app calls `gfx->`, `get_touch()`, `update_trackball()`, `get_keypress()`.
That is the entire surface. Everything else is invisible.

---

## What's Stubbed (SDL2 Only)

| Feature | SDL2 Behavior |
|---|---|
| WiFi scan | Ghost Engine simulates counts |
| BLE scan | Ghost Engine simulates counts |
| GPS | No fix (use Peripheral Bridge for live GPS) |
| LoRa SX1262 | No-op send/receive |
| PMU (AXP2101 / BQ25896) | Reports 100% battery |
| ES7210 / MSM581A mic | No audio capture |
| I2S DAC | No audio playback |
| USB HID | No injection |

---

## Quick Reference

```bash
# Develop for T-Deck
pio run -e sdl2 && .pio/build/sdl2/program

# Develop for KodeDot
pio run -e kodedot_sdl2 && .pio/build/kodedot_sdl2/program

# Flash to T-Deck Plus
pio run -e esp32s3 -t upload

# Flash HID build (USB Ducky wired mode)
pio run -e esp32s3_hid -t upload

# SDL2 with live NFC/RFID hardware
.pio/build/sdl2/program --nfc-bridge /dev/ttyUSB0

# Clean build
pio run -e sdl2 --target clean
```

---

*Pisces Moon OS v1.0.1 "The Arsenal"*  
*Eric Becker / Fluid Fortune — fluidfortune.com*  
*AGPL-3.0-or-later*
