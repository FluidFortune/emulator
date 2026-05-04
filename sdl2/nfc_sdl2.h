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
//  nfc_sdl2.h — NFC / RFID emulation for SDL2 dev environment
//
//  Hardware targets:
//    ST25R3916B  — NFC (I2C) — present on KodeDot block diagram
//    Analog RFID — RFID (SPI) — present on KodeDot block diagram
//
//  Three modes, selected at runtime:
//
//  1. MOCK MODE (default)
//     Returns scripted tag data. Useful for basic app development.
//     No hardware required.
//
//  2. BRIDGE MODE
//     Receives real NFC/RFID tag data from a developer's own
//     ESP32-S3 running the Pisces Moon Peripheral Bridge sketch.
//     Connected via USB serial at 115200 baud, same JSON protocol
//     as bridge_app.cpp.
//
//  3. FILE MODE
//     Reads tag data from ./sd/nfc_tags.json — developer can
//     pre-populate test scenarios without any hardware.
//
//  App code calls the same API regardless of mode:
//    nfc_init()
//    nfc_poll()          → returns NfcTag (or empty if none present)
//    nfc_write(tag, data)
//    rfid_poll()         → returns RfidTag
// ============================================================

#ifndef NFC_SDL2_H
#define NFC_SDL2_H

#ifdef PISCES_SDL

#include "arduino_compat.h"
#include "hal_sdl2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

// ─────────────────────────────────────────────
//  TAG STRUCTURES
//  Mirror what the real ST25R3916B driver returns
// ─────────────────────────────────────────────
#define NFC_UID_MAX_LEN   10
#define NFC_DATA_MAX_LEN  256

typedef enum {
    NFC_TYPE_NONE    = 0,
    NFC_TYPE_A       = 1,   // ISO 14443-A (Mifare, NTAG)
    NFC_TYPE_B       = 2,   // ISO 14443-B
    NFC_TYPE_F       = 3,   // FeliCa
    NFC_TYPE_V       = 4,   // ISO 15693
} NfcTagType;

typedef enum {
    RFID_TYPE_NONE   = 0,
    RFID_TYPE_EM4100 = 1,   // 125kHz EM4100
    RFID_TYPE_HID    = 2,   // HID Prox
    RFID_TYPE_AWID   = 3,   // AWID
} RfidTagType;

struct NfcTag {
    NfcTagType  type;
    uint8_t     uid[NFC_UID_MAX_LEN];
    uint8_t     uid_len;
    uint8_t     data[NFC_DATA_MAX_LEN];
    uint16_t    data_len;
    char        ndef_text[128];  // Decoded NDEF text record if present
    bool        valid;
};

struct RfidTag {
    RfidTagType type;
    uint32_t    id;
    char        id_str[16];
    bool        valid;
};

// ─────────────────────────────────────────────
//  NFC MODE
// ─────────────────────────────────────────────
typedef enum {
    NFC_MODE_MOCK   = 0,
    NFC_MODE_BRIDGE = 1,
    NFC_MODE_FILE   = 2,
} NfcEmulationMode;

extern NfcEmulationMode nfc_mode;

// ─────────────────────────────────────────────
//  BRIDGE STATE
//  When mode = NFC_MODE_BRIDGE, a background
//  thread reads from the peripheral bridge
//  serial port and updates nfc_bridge_tag /
//  rfid_bridge_tag.
// ─────────────────────────────────────────────
extern NfcTag   nfc_bridge_tag;
extern RfidTag  rfid_bridge_tag;
extern pthread_mutex_t nfc_bridge_lock;

// ─────────────────────────────────────────────
//  PUBLIC API — same signatures as real driver
// ─────────────────────────────────────────────
bool      nfc_init();
NfcTag    nfc_poll();
bool      nfc_write(NfcTag* tag, const uint8_t* data, uint16_t len);
RfidTag   rfid_poll();

// Mode control — call before nfc_init() or at runtime
void      nfc_set_mode(NfcEmulationMode mode);
bool      nfc_bridge_connect(const char* serial_port);  // e.g. "/dev/ttyUSB0"
void      nfc_bridge_disconnect();

// Inject a mock tag (for testing without hardware or file)
void      nfc_inject_mock(NfcTag tag);
void      rfid_inject_mock(RfidTag tag);

#endif // PISCES_SDL
#endif // NFC_SDL2_H
