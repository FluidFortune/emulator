// Pisces Moon OS — Peripheral Bridge Sketch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fluidfortune.com

// ============================================================
//  PiscesMoon_PeripheralBridge.ino
//
//  Flash this to any ESP32-S3 dev board to use it as a
//  hardware peripheral source for the Pisces Moon SDL2
//  dev environment.
//
//  Your dev board becomes the sensor layer. The SDL2 emulator
//  running on your laptop gets live NFC, RFID, GPS data.
//  No T-Deck Plus required.
//
//  WIRING (adapt to your board's pin numbers):
//
//  NFC — ST25R3916B breakout (or MFRC522 for cheaper option)
//    SDA  → GPIO 8
//    SCL  → GPIO 9
//    IRQ  → GPIO 10  (optional)
//    GND  → GND
//    3V3  → 3.3V
//
//  RFID — Generic 125kHz RFID reader (UART output)
//    TX   → GPIO 16  (reader TX → ESP32 RX)
//    GND  → GND
//    5V   → 5V (most readers)
//
//  GPS — Any UART GPS module (optional)
//    TX   → GPIO 17
//    RX   → GPIO 18  (not used, GPS is receive-only here)
//    GND  → GND
//    3V3  → 3.3V
//
//  CONNECT TO SDL2 EMULATOR:
//    1. Flash this sketch
//    2. Open Pisces Moon SDL2 build
//    3. In a terminal: ./pisces_moon --nfc-bridge /dev/ttyUSB0
//       (or pass --nfc-bridge /dev/ttyACM0 on Linux,
//              --nfc-bridge COM3 on Windows)
//    4. Wave your NFC/RFID cards near the reader
//    5. SDL2 emulator receives live tag data
//
//  PROTOCOL (same JSON bridge as bridge_app.cpp):
//    Device → Host:
//      {"event":"ready","device":"PiscesPeriphBridge","version":"1.0.0"}
//      {"event":"nfc_tag","type":"A","uid":"04A3F211C25E80","ndef_text":"Hello"}
//      {"event":"nfc_removed"}
//      {"event":"rfid_tag","type":"EM4100","id":"00DEADC0"}
//      {"event":"gps","lat":34.067,"lng":-118.204,"sats":8,"valid":true}
//      {"event":"heartbeat","uptime_s":42}
//
//    Host → Device (optional commands):
//      {"cmd":"ping"}
//      {"cmd":"nfc_write","data":"NDEF hex string"}
//
//  SUPPORTED READER HARDWARE:
//    NFC:  ST25R3916B (KodeDot native), MFRC522, PN532
//    RFID: Any 125kHz Wiegand or UART reader (RDM6300, ID-12LA)
//    GPS:  Any TinyGPSPlus-compatible NMEA module
//
//  PLATFORMIO DEPENDENCIES (add to your sketch's platformio.ini):
//    lib_deps =
//      miguelbalboa/MFRC522 @ ^1.4.10     ; if using MFRC522
//      mikalhart/TinyGPSPlus @ 1.0.3      ; if using GPS
//      bblanchon/ArduinoJson @ 7.4.3
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>

// ─────────────────────────────────────────────
//  CONFIG — adapt to your wiring
// ─────────────────────────────────────────────
#define BRIDGE_BAUD     115200

// NFC (I2C) — comment out if not using NFC
#define USE_NFC         1
#define NFC_SDA         8
#define NFC_SCL         9
// For MFRC522 (SPI) uncomment instead:
// #define USE_MFRC522  1
// #define MFRC522_SS   5
// #define MFRC522_RST  4

// RFID 125kHz (UART) — comment out if not using RFID
#define USE_RFID        1
#define RFID_RX         16
#define RFID_TX         -1   // Not needed for most readers

// GPS (UART) — comment out if not using GPS
// #define USE_GPS      1
// #define GPS_RX       17
// #define GPS_TX       18
// #define GPS_BAUD     9600

// Heartbeat interval
#define HEARTBEAT_MS    5000

// ─────────────────────────────────────────────
//  NFC LIBRARY SELECTION
// ─────────────────────────────────────────────
#ifdef USE_MFRC522
  #include <MFRC522.h>
  MFRC522 mfrc522(MFRC522_SS, MFRC522_RST);
#endif

#ifdef USE_GPS
  #include <TinyGPSPlus.h>
  TinyGPSPlus gps;
  HardwareSerial gpsSerial(1);
#endif

#ifdef USE_RFID
  HardwareSerial rfidSerial(2);
#endif

// ─────────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────────
static bool     nfc_present      = false;
static uint8_t  last_nfc_uid[10] = {0};
static uint8_t  last_nfc_uid_len = 0;
static uint32_t last_rfid_id     = 0;
static bool     rfid_present     = false;
static unsigned long last_heartbeat = 0;

// ─────────────────────────────────────────────
//  JSON HELPERS
// ─────────────────────────────────────────────
static void sendEvent(JsonDocument& doc) {
    serializeJson(doc, Serial);
    Serial.println();
}

// ─────────────────────────────────────────────
//  UID → HEX STRING
// ─────────────────────────────────────────────
static String uidToHex(uint8_t* uid, uint8_t len) {
    String s = "";
    for (int i = 0; i < len; i++) {
        if (uid[i] < 0x10) s += "0";
        s += String(uid[i], HEX);
    }
    s.toUpperCase();
    return s;
}

// ─────────────────────────────────────────────
//  NFC POLL — MFRC522
// ─────────────────────────────────────────────
#ifdef USE_MFRC522
static void pollNFC() {
    if (!mfrc522.PICC_IsNewCardPresent()) {
        if (nfc_present) {
            nfc_present = false;
            JsonDocument doc;
            doc["event"] = "nfc_removed";
            sendEvent(doc);
        }
        return;
    }

    if (!mfrc522.PICC_ReadCardSerial()) return;

    uint8_t* uid    = mfrc522.uid.uidByte;
    uint8_t  uid_len = mfrc522.uid.size;

    // Only report if new tag or same tag returning
    bool same = (uid_len == last_nfc_uid_len &&
                 memcmp(uid, last_nfc_uid, uid_len) == 0);

    if (!same || !nfc_present) {
        memcpy(last_nfc_uid, uid, uid_len);
        last_nfc_uid_len = uid_len;
        nfc_present = true;

        JsonDocument doc;
        doc["event"]    = "nfc_tag";
        doc["type"]     = "A";
        doc["uid"]      = uidToHex(uid, uid_len);

        // Try to read NDEF (NTAG213/215/216)
        // Page 4 = start of user data on NTAG
        byte buf[18]; byte sz = sizeof(buf);
        if (mfrc522.MIFARE_Read(4, buf, &sz) == MFRC522::STATUS_OK) {
            // Check for NDEF TLV (0x03)
            if (buf[0] == 0x03) {
                uint8_t ndef_len = buf[1];
                // Check for text record (0xD1 0x01 ... 0x54)
                if (ndef_len > 4 && buf[2] == 0xD1 && buf[4] == 0x54) {
                    uint8_t lang_len = buf[6] & 0x3F;
                    char text[64] = {0};
                    int text_start = 7 + lang_len;
                    int text_len   = ndef_len - 3 - lang_len;
                    if (text_len > 63) text_len = 63;
                    memcpy(text, buf + text_start, text_len);
                    doc["ndef_text"] = text;
                }
            }
        }

        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        sendEvent(doc);
    }
}
#else
static void pollNFC() {}  // Stub if no NFC reader
#endif

// ─────────────────────────────────────────────
//  RFID POLL — RDM6300 / ID-12LA style
//  These readers send 14-byte frames at 9600 baud:
//  STX(1) + DATA(10) + CHECKSUM(2) + ETX(1)
//  The 10 data bytes are ASCII hex of the 5-byte ID
// ─────────────────────────────────────────────
#ifdef USE_RFID
static void pollRFID() {
    if (rfidSerial.available() < 14) return;

    uint8_t frame[14];
    rfidSerial.readBytes(frame, 14);

    if (frame[0] != 0x02 || frame[13] != 0x03) {
        // Flush bad frame
        while (rfidSerial.available()) rfidSerial.read();
        return;
    }

    // Parse 10 ASCII hex chars → 5 bytes
    char ascii[11] = {0};
    memcpy(ascii, frame + 1, 10);
    uint32_t id = (uint32_t)strtoul(ascii + 2, nullptr, 16);  // Skip version byte

    if (id != last_rfid_id || !rfid_present) {
        last_rfid_id  = id;
        rfid_present  = true;

        JsonDocument doc;
        doc["event"] = "rfid_tag";
        doc["type"]  = "EM4100";
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%08X", id);
        doc["id"] = id_str;
        sendEvent(doc);
    }
}
#else
static void pollRFID() {}
#endif

// ─────────────────────────────────────────────
//  GPS POLL
// ─────────────────────────────────────────────
#ifdef USE_GPS
static unsigned long last_gps_send = 0;
static void pollGPS() {
    while (gpsSerial.available()) {
        gps.encode(gpsSerial.read());
    }
    // Send GPS update every 2 seconds if we have a fix
    if (millis() - last_gps_send > 2000) {
        last_gps_send = millis();
        JsonDocument doc;
        doc["event"] = "gps";
        doc["valid"] = gps.location.isValid();
        if (gps.location.isValid()) {
            doc["lat"]  = gps.location.lat();
            doc["lng"]  = gps.location.lng();
            doc["alt_ft"] = gps.altitude.feet();
            doc["sats"] = gps.satellites.value();
            doc["speed_mph"] = gps.speed.mph();
        }
        sendEvent(doc);
    }
}
#else
static void pollGPS() {}
#endif

// ─────────────────────────────────────────────
//  COMMAND HANDLER (host → device)
// ─────────────────────────────────────────────
static char cmdBuf[256];
static int  cmdLen = 0;

static void handleCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = 0; cmdLen = 0;
                JsonDocument doc;
                if (deserializeJson(doc, cmdBuf) == DeserializationError::Ok) {
                    const char* cmd = doc["cmd"] | "";
                    if (strcmp(cmd, "ping") == 0) {
                        JsonDocument resp;
                        resp["ok"]              = true;
                        resp["data"]["pong"]    = true;
                        resp["data"]["device"]  = "PiscesPeriphBridge";
                        resp["data"]["version"] = "1.0.0";
                        resp["data"]["uptime_s"]= millis()/1000;
                        resp["data"]["nfc"]     = (bool)USE_NFC;
                        resp["data"]["rfid"]    = (bool)USE_RFID;
                        serializeJson(resp, Serial);
                        Serial.println();
                    }
                }
            }
        } else {
            if (cmdLen < 255) cmdBuf[cmdLen++] = c;
        }
    }
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(BRIDGE_BAUD);
    delay(500);

#ifdef USE_MFRC522
    SPI.begin();
    mfrc522.PCD_Init();
    Serial.println("[BRIDGE] MFRC522 NFC initialized");
#endif

#ifdef USE_RFID
    rfidSerial.begin(9600, SERIAL_8N1, RFID_RX, RFID_TX);
    Serial.println("[BRIDGE] RFID UART initialized");
#endif

#ifdef USE_GPS
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    Serial.println("[BRIDGE] GPS UART initialized");
#endif

    // Announce readiness
    JsonDocument doc;
    doc["event"]           = "ready";
    doc["device"]          = "PiscesPeriphBridge";
    doc["version"]         = "1.0.0";
    doc["capabilities"]["nfc"]  = (bool)USE_NFC;
    doc["capabilities"]["rfid"] = (bool)USE_RFID;
    doc["capabilities"]["gps"]  = false;
    serializeJson(doc, Serial);
    Serial.println();
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
    handleCommands();
    pollNFC();
    pollRFID();
    pollGPS();

    // Heartbeat
    if (millis() - last_heartbeat > HEARTBEAT_MS) {
        last_heartbeat = millis();
        JsonDocument doc;
        doc["event"]     = "heartbeat";
        doc["uptime_s"]  = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        serializeJson(doc, Serial);
        Serial.println();
    }

    delay(50);
}
