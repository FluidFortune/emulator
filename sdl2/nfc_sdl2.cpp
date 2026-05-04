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

#ifdef PISCES_SDL

#include "nfc_sdl2.h"
#include "hal_sdl2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>

// ─────────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────────
NfcEmulationMode nfc_mode        = NFC_MODE_MOCK;
NfcTag           nfc_bridge_tag  = {NFC_TYPE_NONE, {0}, 0, {0}, 0, "", false};
RfidTag          rfid_bridge_tag = {RFID_TYPE_NONE, 0, "", false};
pthread_mutex_t  nfc_bridge_lock = PTHREAD_MUTEX_INITIALIZER;

static NfcTag    nfc_mock_tag    = {NFC_TYPE_NONE, {0}, 0, {0}, 0, "", false};
static RfidTag   rfid_mock_tag   = {RFID_TYPE_NONE, 0, "", false};
static int       bridge_fd       = -1;
static pthread_t bridge_thread;
static bool      bridge_running  = false;

// ─────────────────────────────────────────────
//  DEFAULT MOCK TAGS
//  Realistic test data for app development
// ─────────────────────────────────────────────
static void load_default_mocks() {
    // NFC: NTAG213 with NDEF text "Pisces Moon Dev Tag"
    nfc_mock_tag.type    = NFC_TYPE_A;
    nfc_mock_tag.uid[0]  = 0x04;
    nfc_mock_tag.uid[1]  = 0xA3;
    nfc_mock_tag.uid[2]  = 0xF2;
    nfc_mock_tag.uid[3]  = 0x11;
    nfc_mock_tag.uid[4]  = 0xC2;
    nfc_mock_tag.uid[5]  = 0x5E;
    nfc_mock_tag.uid[6]  = 0x80;
    nfc_mock_tag.uid_len = 7;
    strncpy(nfc_mock_tag.ndef_text, "Pisces Moon Dev Tag", sizeof(nfc_mock_tag.ndef_text)-1);
    // NDEF record bytes (EN text "Pisces Moon Dev Tag")
    uint8_t ndef[] = {0xD1,0x01,0x17,0x54,0x02,0x65,0x6E,
                      0x50,0x69,0x73,0x63,0x65,0x73,0x20,
                      0x4D,0x6F,0x6F,0x6E,0x20,0x44,0x65,
                      0x76,0x20,0x54,0x61,0x67};
    memcpy(nfc_mock_tag.data, ndef, sizeof(ndef));
    nfc_mock_tag.data_len = sizeof(ndef);
    nfc_mock_tag.valid    = false;  // Not "present" until polled

    // RFID: EM4100 card, typical HID facility code
    rfid_mock_tag.type   = RFID_TYPE_EM4100;
    rfid_mock_tag.id     = 0x00DEADC0;
    snprintf(rfid_mock_tag.id_str, sizeof(rfid_mock_tag.id_str), "%08X", rfid_mock_tag.id);
    rfid_mock_tag.valid  = false;
}

// ─────────────────────────────────────────────
//  FILE MODE — load tags from ./sd/nfc_tags.json
//
//  Format:
//  {
//    "nfc": {
//      "type": "A",
//      "uid": "04A3F211C25E80",
//      "ndef_text": "Hello World"
//    },
//    "rfid": {
//      "type": "EM4100",
//      "id": "00DEADC0"
//    }
//  }
// ─────────────────────────────────────────────
static void load_file_tags() {
    FILE* f = fopen("./sd/nfc_tags.json", "r");
    if (!f) {
        printf("[NFC] ./sd/nfc_tags.json not found — using defaults\n");
        load_default_mocks();
        return;
    }

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);

    // Minimal JSON parse (no ArduinoJson on native — use strstr)
    // NFC ndef_text
    char* p = strstr(buf, "\"ndef_text\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p = strchr(p, '"');
            if (p) {
                p++;
                char* end = strchr(p, '"');
                if (end) {
                    int len = (int)(end - p);
                    if (len > (int)sizeof(nfc_mock_tag.ndef_text)-1) len = sizeof(nfc_mock_tag.ndef_text)-1;
                    strncpy(nfc_mock_tag.ndef_text, p, len);
                    nfc_mock_tag.ndef_text[len] = 0;
                    nfc_mock_tag.type  = NFC_TYPE_A;
                    nfc_mock_tag.valid = false;
                    printf("[NFC] File tag loaded: \"%s\"\n", nfc_mock_tag.ndef_text);
                }
            }
        }
    }

    // RFID id
    p = strstr(buf, "\"id\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p = strchr(p, '"');
            if (p) {
                p++;
                rfid_mock_tag.id = (uint32_t)strtoul(p, nullptr, 16);
                snprintf(rfid_mock_tag.id_str, sizeof(rfid_mock_tag.id_str), "%08X", rfid_mock_tag.id);
                rfid_mock_tag.type  = RFID_TYPE_EM4100;
                rfid_mock_tag.valid = false;
                printf("[NFC] File RFID loaded: %s\n", rfid_mock_tag.id_str);
            }
        }
    }
}

// ─────────────────────────────────────────────
//  BRIDGE MODE — serial reader thread
//
//  Reads JSON lines from the peripheral bridge:
//  {"event":"nfc_tag","type":"A","uid":"04A3F211C25E80","ndef_text":"Hello"}
//  {"event":"rfid_tag","type":"EM4100","id":"00DEADC0"}
//  {"event":"nfc_removed"}
// ─────────────────────────────────────────────
static void* bridge_reader(void* arg) {
    char line[512];
    int  pos = 0;

    printf("[NFC] Bridge reader thread started\n");

    while (bridge_running && bridge_fd >= 0) {
        char c;
        int n = read(bridge_fd, &c, 1);
        if (n <= 0) { hal_sdl2_delay(10); continue; }

        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                line[pos] = 0;
                pos = 0;

                // Parse event type
                if (strstr(line, "\"nfc_tag\"")) {
                    NfcTag tag = {NFC_TYPE_NONE, {0}, 0, {0}, 0, "", false};
                    tag.type  = NFC_TYPE_A;
                    tag.valid = true;

                    // Extract uid
                    char* p = strstr(line, "\"uid\"");
                    if (p) {
                        p = strchr(p, '"'); if (p) p++;
                        p = strchr(p, '"'); if (p) p++;
                        char* end = strchr(p, '"');
                        if (end) {
                            int bytes = (int)(end-p)/2;
                            if (bytes > NFC_UID_MAX_LEN) bytes = NFC_UID_MAX_LEN;
                            for (int i = 0; i < bytes; i++) {
                                char hex[3] = {p[i*2], p[i*2+1], 0};
                                tag.uid[i] = (uint8_t)strtoul(hex, nullptr, 16);
                            }
                            tag.uid_len = bytes;
                        }
                    }

                    // Extract ndef_text
                    p = strstr(line, "\"ndef_text\"");
                    if (p) {
                        p = strchr(p, ':'); if (p) p++;
                        p = strchr(p, '"'); if (p) p++;
                        char* end = strchr(p, '"');
                        if (end) {
                            int len = (int)(end-p);
                            if (len > 127) len = 127;
                            strncpy(tag.ndef_text, p, len);
                            tag.ndef_text[len] = 0;
                        }
                    }

                    pthread_mutex_lock(&nfc_bridge_lock);
                    nfc_bridge_tag = tag;
                    pthread_mutex_unlock(&nfc_bridge_lock);
                    printf("[NFC] Bridge: NFC tag received UID=%02X... text=\"%s\"\n",
                           tag.uid[0], tag.ndef_text);

                } else if (strstr(line, "\"rfid_tag\"")) {
                    RfidTag tag = {RFID_TYPE_NONE, 0, "", false};
                    tag.type  = RFID_TYPE_EM4100;
                    tag.valid = true;

                    char* p = strstr(line, "\"id\"");
                    if (p) {
                        p = strchr(p, '"'); if (p) p++;
                        p = strchr(p, '"'); if (p) p++;
                        char* end = strchr(p, '"');
                        if (end) {
                            char idstr[16] = {0};
                            int len = (int)(end-p);
                            if (len > 15) len = 15;
                            strncpy(idstr, p, len);
                            tag.id = (uint32_t)strtoul(idstr, nullptr, 16);
                            snprintf(tag.id_str, sizeof(tag.id_str), "%08X", tag.id);
                        }
                    }

                    pthread_mutex_lock(&nfc_bridge_lock);
                    rfid_bridge_tag = tag;
                    pthread_mutex_unlock(&nfc_bridge_lock);
                    printf("[NFC] Bridge: RFID tag received ID=%s\n", tag.id_str);

                } else if (strstr(line, "\"nfc_removed\"")) {
                    pthread_mutex_lock(&nfc_bridge_lock);
                    nfc_bridge_tag.valid = false;
                    pthread_mutex_unlock(&nfc_bridge_lock);
                    printf("[NFC] Bridge: NFC tag removed\n");
                }
            }
        } else {
            if (pos < 511) line[pos++] = c;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────
//  PUBLIC API
// ─────────────────────────────────────────────
bool nfc_init() {
    load_default_mocks();
    if (nfc_mode == NFC_MODE_FILE) load_file_tags();
    printf("[NFC] ST25R3916B → SDL2 emulation (mode: %s)\n",
           nfc_mode == NFC_MODE_MOCK   ? "MOCK" :
           nfc_mode == NFC_MODE_BRIDGE ? "BRIDGE" : "FILE");
    return true;
}

void nfc_set_mode(NfcEmulationMode mode) {
    nfc_mode = mode;
    if (mode == NFC_MODE_FILE) load_file_tags();
    else if (mode == NFC_MODE_MOCK) load_default_mocks();
}

bool nfc_bridge_connect(const char* port) {
    bridge_fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (bridge_fd < 0) {
        printf("[NFC] Bridge: cannot open %s\n", port);
        return false;
    }
    struct termios tty;
    tcgetattr(bridge_fd, &tty);
    cfsetspeed(&tty, B115200);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tcsetattr(bridge_fd, TCSANOW, &tty);

    bridge_running = true;
    pthread_create(&bridge_thread, nullptr, bridge_reader, nullptr);
    printf("[NFC] Bridge connected to %s at 115200 baud\n", port);
    nfc_mode = NFC_MODE_BRIDGE;
    return true;
}

void nfc_bridge_disconnect() {
    bridge_running = false;
    if (bridge_fd >= 0) { close(bridge_fd); bridge_fd = -1; }
    pthread_join(bridge_thread, nullptr);
    printf("[NFC] Bridge disconnected\n");
}

void nfc_inject_mock(NfcTag tag)    { nfc_mock_tag  = tag; }
void rfid_inject_mock(RfidTag tag)  { rfid_mock_tag = tag; }

// ─────────────────────────────────────────────
//  POLL FUNCTIONS
//  Called from app code — returns tag if present
//  Mock mode: tag "appears" for 2 seconds every
//  8 seconds to simulate real detection cycles.
// ─────────────────────────────────────────────
NfcTag nfc_poll() {
    if (nfc_mode == NFC_MODE_BRIDGE) {
        pthread_mutex_lock(&nfc_bridge_lock);
        NfcTag t = nfc_bridge_tag;
        pthread_mutex_unlock(&nfc_bridge_lock);
        return t;
    }

    // Mock / File: simulate presence cycle
    uint32_t t = hal_sdl2_millis() % 8000;
    NfcTag result = nfc_mock_tag;
    result.valid  = (t < 2000);  // Present for 2s, absent for 6s
    return result;
}

RfidTag rfid_poll() {
    if (nfc_mode == NFC_MODE_BRIDGE) {
        pthread_mutex_lock(&nfc_bridge_lock);
        RfidTag t = rfid_bridge_tag;
        pthread_mutex_unlock(&nfc_bridge_lock);
        return t;
    }

    uint32_t t = hal_sdl2_millis() % 10000;
    RfidTag result = rfid_mock_tag;
    result.valid   = (t < 1500);  // Present for 1.5s, absent for 8.5s
    return result;
}

bool nfc_write(NfcTag* tag, const uint8_t* data, uint16_t len) {
    if (!tag || !tag->valid) return false;
    if (len > NFC_DATA_MAX_LEN) len = NFC_DATA_MAX_LEN;
    memcpy(tag->data, data, len);
    tag->data_len = len;
    printf("[NFC] Write %d bytes to tag UID=%02X...\n", len, tag->uid[0]);
    return true;
}

#endif // PISCES_SDL
