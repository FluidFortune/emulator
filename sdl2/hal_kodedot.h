// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it
// and/or modify it under the terms of the GNU Affero General
// Public License as published by the Free Software Foundation,
// either version 3 of the License, or any later version.
//
// fluidfortune.com

// ============================================================
//  hal_kodedot.h — Pisces Moon OS KodeDot HAL
//
//  Target: KodeDot handheld computer
//  Sources: KodeDot block diagram (unofficial, May 2026)
//           Kickstarter campaign specs
//           Community hardware speculation
//
//  STATUS: THEORETICAL — no pinouts confirmed.
//          Every TODO comment marks an unknown.
//          When official specs arrive, search TODO and fill in.
//          The application layer does not change at all.
//
//  DUAL-CHIP ARCHITECTURE:
//    ESP32-C5-MINI-1-N4  — Connectivity co-processor
//                          (WiFi 6, BLE 5, 802.15.4)
//                          Runs esp-hosted-mcu firmware
//                          Talks to P4 via SDIO
//
//    ESP32-P4NRW32X      — Main application processor
//                          Dual-core Xtensa LX7 @ 400MHz
//                          32MB PSRAM embedded
//                          AI acceleration (vector instructions)
//                          Runs Pisces Moon OS
//
//  SPI BUS TREATY — applies here exactly as on T-Deck:
//    The P4 drives display, SD, NFC, RFID on shared buses.
//    Any code touching those buses MUST take spi_mutex first.
//    The C5 connectivity core is the analog of T-Deck's LoRa —
//    a separate chip on a shared bus that needs arbitration.
//
//  DISPLAY:
//    AMOLED touch display (resolution TODO — estimating 480×480
//    based on KodeDot promotional imagery and square form factor)
//    Touch controller TODO
//
//  INPUT:
//    D-pad (Right, Left, Top, Bottom) + Left + OK/Goto
//    IO Expander TCA6408MGTR handles button matrix
//    No physical keyboard (unlike T-Deck)
//    Haptic motor DRV2605TR for tactile feedback
//
//  Note for Pisces Moon port:
//    Trackball → D-pad is a 1:1 mapping
//    The up/down/left/right/click convention is identical
//    update_trackball() / update_trackball_game() work unchanged
// ============================================================

#ifndef HAL_KODEDOT_H
#define HAL_KODEDOT_H

#ifdef PISCES_KODEDOT

#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────────
//  DISPLAY GEOMETRY
//  TODO: Confirm with official KodeDot spec
//  Best guess from promotional imagery: 480×480
//  T-Deck was 320×240 — this is a larger canvas
// ─────────────────────────────────────────────
#define KD_SCREEN_W     480     // TODO: confirm
#define KD_SCREEN_H     480     // TODO: confirm
#define KD_SCREEN_BPP   16      // RGB565 assumed; may be RGB888

// Pisces Moon was designed for 320×240.
// Until confirmed, the SDL2 KodeDot build renders at 480×480
// with a letterbox/scale option for 320×240 apps.
// New KodeDot-native apps should target KD_SCREEN_W/H.
#define KD_SCALE_LEGACY_APPS    1   // Scale 320×240 → 480×480 (4:3 → 1:1)

// ─────────────────────────────────────────────
//  MAIN PROCESSOR — ESP32-P4NRW32X
// ─────────────────────────────────────────────
#define KD_MCU_CORES    2
#define KD_MCU_FREQ     400     // MHz (P4 max — may run lower)
#define KD_PSRAM_MB     32      // Embedded in P4 die
#define KD_FLASH_MB     16      // TODO: confirm flash size

// ─────────────────────────────────────────────
//  CONNECTIVITY CO-PROCESSOR — ESP32-C5-MINI-1-N4
//  Runs esp-hosted firmware, exposes WiFi/BLE to P4
//  via SDIO interface. P4 treats it like a network card.
// ─────────────────────────────────────────────
#define KD_C5_SDIO_CLK  -1      // TODO: confirm SDIO pins
#define KD_C5_SDIO_CMD  -1      // TODO: confirm
#define KD_C5_SDIO_D0   -1      // TODO: confirm
#define KD_C5_SDIO_D1   -1      // TODO: confirm
#define KD_C5_SDIO_D2   -1      // TODO: confirm
#define KD_C5_SDIO_D3   -1      // TODO: confirm
#define KD_C5_POWER_EN  -1      // TODO: confirm ON/OFF pin
#define KD_C5_RESET     -1      // TODO: confirm DETECT/RESET pin

// ─────────────────────────────────────────────
//  POWER — BQ25896RTWB PMIC + BQ27220YZH Fuel Gauge
// ─────────────────────────────────────────────
#define KD_PMIC_ADDR    0x6B    // BQ25896 I2C address
#define KD_FUEL_ADDR    0x55    // BQ27220 I2C address
#define KD_I2C_SDA      -1      // TODO: confirm
#define KD_I2C_SCL      -1      // TODO: confirm
#define KD_LDO_3V3      -1      // TODO: confirm LDO enable pin

// ─────────────────────────────────────────────
//  DISPLAY — AMOLED + LED Driver LP5815
// ─────────────────────────────────────────────
#define KD_TFT_CS       -1      // TODO: confirm
#define KD_TFT_DC       -1      // TODO: confirm
#define KD_TFT_RST      -1      // TODO: confirm
#define KD_TFT_MOSI     -1      // TODO: confirm
#define KD_TFT_MISO     -1      // TODO: confirm
#define KD_TFT_SCK      -1      // TODO: confirm
#define KD_TFT_BL       -1      // TODO: confirm backlight (AMOLED may not have one)
#define KD_LED_ADDR     0x14    // LP5815 I2C address (TODO: confirm)

// ─────────────────────────────────────────────
//  TOUCH — controller TODO
//  T-Deck used GT911. KodeDot AMOLED likely has
//  integrated touch, controller unknown.
// ─────────────────────────────────────────────
#define KD_TOUCH_ADDR   -1      // TODO: confirm touch controller I2C address
#define KD_TOUCH_INT    -1      // TODO: confirm
#define KD_TOUCH_RST    -1      // TODO: confirm

// ─────────────────────────────────────────────
//  STORAGE — MicroSD
// ─────────────────────────────────────────────
#define KD_SD_CS        -1      // TODO: confirm
#define KD_SD_MOSI      -1      // TODO: confirm (may share TFT SPI bus)
#define KD_SD_MISO      -1      // TODO: confirm
#define KD_SD_SCK       -1      // TODO: confirm

// ─────────────────────────────────────────────
//  INPUT — D-PAD via IO Expander TCA6408MGTR
//  TCA6408 is an 8-bit I2C GPIO expander.
//  8 buttons → 8 GPIOs on the expander.
//  Pisces Moon trackball convention maps 1:1:
//    TRK_UP    → KD_DPAD_TOP
//    TRK_DOWN  → KD_DPAD_BOT
//    TRK_LEFT  → KD_DPAD_LEFT
//    TRK_RIGHT → KD_DPAD_RIGHT
//    TRK_CLICK → KD_BTN_OK
// ─────────────────────────────────────────────
#define KD_IOEXP_ADDR   0x20    // TCA6408 default I2C address
#define KD_IOEXP_INT    -1      // TODO: confirm interrupt pin

// IO Expander bit assignments (TODO: confirm from schematic)
#define KD_DPAD_RIGHT   (1 << 0)
#define KD_DPAD_LEFT    (1 << 1)
#define KD_DPAD_TOP     (1 << 2)
#define KD_DPAD_BOT     (1 << 3)
#define KD_BTN_TOP      (1 << 4)    // Physical button top
#define KD_BTN_BOT      (1 << 5)    // Physical button bottom
#define KD_BTN_OK       (1 << 6)    // OK/Goto
#define KD_BTN_LEFT     (1 << 7)    // Left button

// ─────────────────────────────────────────────
//  HAPTIC — DRV2605TR
// ─────────────────────────────────────────────
#define KD_HAPTIC_ADDR  0x5A    // DRV2605 I2C address (fixed)
#define KD_HAPTIC_EN    -1      // TODO: confirm enable pin (PWM_DN?)

// ─────────────────────────────────────────────
//  SENSORS
// ─────────────────────────────────────────────
// IMU — LSM6DSV5X (6-axis, I2C)
#define KD_IMU_ADDR     0x6A    // LSM6DSV5X default I2C address

// Magnetometer — LIS2MDL (I2C)
#define KD_MAG_ADDR     0x1E    // LIS2MDL I2C address (fixed)

// ─────────────────────────────────────────────
//  NFC / RFID
//  Both present on KodeDot per block diagram.
//  ST25R3916B = NFC (I2C)
//  "Analog RFID" = 125kHz RFID (likely SPI or UART)
// ─────────────────────────────────────────────
#define KD_NFC_ADDR     0x08    // ST25R3916B I2C address
#define KD_NFC_IRQ      -1      // TODO: confirm
#define KD_RFID_CS      -1      // TODO: confirm (if SPI)
#define KD_RFID_RX      -1      // TODO: confirm (if UART)

// ─────────────────────────────────────────────
//  IR
// ─────────────────────────────────────────────
#define KD_IR_TX        -1      // TODO: confirm IR emitter pin
#define KD_IR_RX        -1      // TODO: confirm IR receiver pin

// ─────────────────────────────────────────────
//  AUDIO
// ─────────────────────────────────────────────
// Amplifier chain: ES8374 → ANA4050B → speaker
// Microphone: MSM581A72H48PG (PDM or I2S)
#define KD_AMP_ADDR     0x10    // ES8374 I2C address (TODO: confirm)
#define KD_I2S_BCLK     -1      // TODO: confirm
#define KD_I2S_LRCK     -1      // TODO: confirm
#define KD_I2S_DOUT     -1      // TODO: confirm
#define KD_I2S_DIN      -1      // TODO: confirm
#define KD_MIC_CLK      -1      // TODO: confirm PDM/I2S mic clock
#define KD_MIC_DATA     -1      // TODO: confirm

// ─────────────────────────────────────────────
//  EXPANSION CONNECTORS
//  20-pin connector: 14 programmable GPIOs, 3V3 2A
//  6-pin connector:  2 programmable GPIOs, 5V 2A
// ─────────────────────────────────────────────
// These are the selling point for peripheral accessories.
// GPIO assignments TODO pending official spec.
#define KD_EXP20_GPIO_COUNT  14
#define KD_EXP6_GPIO_COUNT   2

// ─────────────────────────────────────────────
//  SPI BUS TREATY — KodeDot flavor
//
//  The dual-chip architecture introduces a new
//  treaty consideration: the C5 co-processor
//  shares bus time with the P4.
//
//  Required extension to the treaty:
//    spi_mutex    — same as T-Deck, protects SD/display/NFC
//    c5_mutex     — new, protects SDIO bus to C5
//    ANY code calling WiFi/BLE must take c5_mutex first
//    Ghost Engine on Core 0 checks both before SD writes
//
//  This is architecturally identical to LoRa on T-Deck
//  (lora_voice_active flag) but formalized as a mutex
//  because the C5 is a full co-processor, not a passive radio.
// ─────────────────────────────────────────────
// extern SemaphoreHandle_t spi_mutex;   // same as T-Deck
// extern SemaphoreHandle_t c5_mutex;    // new for KodeDot

// ─────────────────────────────────────────────
//  PISCES MOON PORT STATUS
//
//  READY NOW (no changes needed):
//    ✓ All app logic
//    ✓ ELF module ABI
//    ✓ SPI Bus Treaty pattern
//    ✓ Ghost Engine dual-core split
//    ✓ File system / SD card layout
//    ✓ Gemini client
//    ✓ Bridge App protocol
//    ✓ Ghost Partition security
//
//  NEEDS HAL SWAP (fill in TODOs above):
//    ○ Display driver (ST7789 → AMOLED driver)
//    ○ Touch controller (GT911 → KodeDot touch)
//    ○ Trackball → D-pad GPIO mapping
//    ○ PMU (AXP2101 → BQ25896 + BQ27220)
//    ○ WiFi/BLE via C5 SDIO instead of native S3
//    ○ Screen resolution (320×240 → 480×480)
//
//  NEW CAPABILITIES (not on T-Deck):
//    + NFC read/write (ST25R3916B)
//    + 125kHz RFID
//    + IR transmit/receive
//    + Magnetometer
//    + Haptic feedback
//    + AI vector acceleration (P4 hardware)
//    + WiFi 6 / 802.15.4 via C5
// ─────────────────────────────────────────────

#endif // PISCES_KODEDOT
#endif // HAL_KODEDOT_H
