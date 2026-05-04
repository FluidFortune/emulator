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
//  hal_kodedot_sdl2.cpp — KodeDot emulation layer
//
//  Extends the base SDL2 HAL to emulate KodeDot-specific
//  hardware based on the known block diagram.
//
//  Key differences from T-Deck SDL2 build:
//    - 480×480 window (square AMOLED) vs 320×240
//    - D-pad button labels in window title bar
//    - Dual-bus SPI Treaty: spi_mutex + c5_mutex
//    - NFC/RFID via nfc_sdl2 (mock/bridge/file)
//    - IR emulation (loopback — TX immediately triggers RX)
//    - Haptic emulation (stdout pulse + window flash)
//    - IMU/Magnetometer: returns slowly drifting mock values
//    - C5 co-processor: simulated as WiFi/BLE available
//    - Legacy app scaling: 320×240 → 480×480 letterbox
//
//  Build:
//    pio run -e kodedot_sdl2
//
//  Controls:
//    Arrow keys   = D-pad
//    Enter        = OK/Goto
//    RAlt+Enter   = same as OK
//    F1           = Top button
//    F2           = Bottom button
//    F3           = Left button
//    Mouse        = Touch (mapped to 480×480)
// ============================================================

#ifdef PISCES_KODEDOT_SDL2

#include "hal_kodedot.h"
#include "hal_sdl2.h"
#include "arduino_compat.h"
#include "nfc_sdl2.h"
#include <stdio.h>
#include <math.h>
#include <pthread.h>

// ─────────────────────────────────────────────
//  KodeDot-specific globals
// ─────────────────────────────────────────────
pthread_mutex_t c5_mutex = PTHREAD_MUTEX_INITIALIZER;  // C5 co-processor bus

// Simulated sensor state
static float imu_accel_x = 0.0f;
static float imu_accel_y = 0.0f;
static float imu_accel_z = 9.81f;  // 1G resting
static float mag_heading  = 0.0f;   // Degrees, slowly drifting

// IR loopback buffer
static uint32_t ir_last_code = 0;
static bool     ir_received  = false;

// Haptic state
static bool     haptic_active = false;
static uint32_t haptic_end_ms = 0;

// ─────────────────────────────────────────────
//  SCREEN OVERRIDE FOR KODEDOT
//  Override SCREEN_W/H from hal_sdl2.h
// ─────────────────────────────────────────────
// KodeDot uses 480×480 — redefine for this build
#undef  SCREEN_W
#undef  SCREEN_H
#undef  SDL_SCALE
#define SCREEN_W    KD_SCREEN_W   // 480
#define SCREEN_H    KD_SCREEN_H   // 480
#define SDL_SCALE   1             // 1:1 — 480px fits on modern monitors

// Reallocate pixel_buf at correct size
// (hal_sdl2.cpp allocates for 320×240 — KodeDot build overrides)
uint16_t kd_pixel_buf[KD_SCREEN_W * KD_SCREEN_H];

// ─────────────────────────────────────────────
//  LEGACY APP SCALING
//  Apps written for 320×240 still work.
//  drawPixel() checks if we're in legacy mode
//  and scales coordinates.
// ─────────────────────────────────────────────
static bool legacy_scale_active = false;

static inline void kd_pixel(int16_t x, int16_t y, uint16_t color) {
    if (legacy_scale_active) {
        // Scale 320×240 → 480×480
        // x: 0..319 → 0..479  (multiply by 1.5)
        // y: 0..239 → 120..359 (center vertically with 120px letterbox)
        int sx = (x * KD_SCREEN_W) / 320;
        int sy = ((y * KD_SCREEN_W) / 320) + ((KD_SCREEN_H - KD_SCREEN_W) / 2);
        if (sx < 0 || sx >= KD_SCREEN_W) return;
        if (sy < 0 || sy >= KD_SCREEN_H) return;
        kd_pixel_buf[sy * KD_SCREEN_W + sx] = color;
        // Scale pixels: fill 2×2 block to avoid single-pixel gaps
        if (sx+1 < KD_SCREEN_W) kd_pixel_buf[sy * KD_SCREEN_W + sx + 1] = color;
        if (sy+1 < KD_SCREEN_H) kd_pixel_buf[(sy+1) * KD_SCREEN_W + sx] = color;
        if (sx+1 < KD_SCREEN_W && sy+1 < KD_SCREEN_H)
            kd_pixel_buf[(sy+1) * KD_SCREEN_W + sx + 1] = color;
    } else {
        if (x < 0 || x >= KD_SCREEN_W || y < 0 || y >= KD_SCREEN_H) return;
        kd_pixel_buf[y * KD_SCREEN_W + x] = color;
    }
}

void kd_set_legacy_scaling(bool enabled) {
    legacy_scale_active = enabled;
    if (enabled) printf("[KD] Legacy 320×240 scaling active\n");
}

// ─────────────────────────────────────────────
//  HAPTIC EMULATION
//  Real DRV2605TR produces vibration effects.
//  SDL2: flash window border + stdout.
// ─────────────────────────────────────────────
void kd_haptic_play(uint8_t effect_id) {
    // DRV2605 effect IDs: 1=strong click, 2=buzz, etc.
    const char* effect_names[] = {
        "NONE", "Strong Click", "Soft Bump", "Double Click",
        "Heavy Click", "Buzz", "Pulsing Strong", "Transition Ramp"
    };
    int idx = (effect_id < 8) ? effect_id : 0;
    printf("[KD] Haptic: effect %d (%s)\n", effect_id, effect_names[idx]);

    haptic_active = true;
    haptic_end_ms = hal_sdl2_millis() + 200;

    // Flash window title to indicate haptic
    if (sdl_window) {
        SDL_SetWindowTitle(sdl_window,
            "Pisces Moon OS — KodeDot [HAPTIC]");
        // Will be restored in next frame present
    }
}

// ─────────────────────────────────────────────
//  IR EMULATION
//  Loopback: transmit immediately triggers receive.
//  Useful for testing IR apps without hardware.
// ─────────────────────────────────────────────
void kd_ir_send(uint32_t code, const char* protocol) {
    printf("[KD] IR TX: 0x%08X (%s)\n", code, protocol ? protocol : "NEC");
    // Loopback
    ir_last_code = code;
    ir_received  = true;
}

bool kd_ir_receive(uint32_t* code) {
    if (!ir_received) return false;
    if (code) *code = ir_last_code;
    ir_received = false;
    return true;
}

// ─────────────────────────────────────────────
//  IMU EMULATION — LSM6DSV5X
//  Returns slowly varying mock values to let
//  apps that use orientation data run correctly.
// ─────────────────────────────────────────────
void kd_imu_get_accel(float* x, float* y, float* z) {
    // Simulate gentle device movement
    float t = hal_sdl2_millis() / 1000.0f;
    *x = sinf(t * 0.3f) * 0.5f;
    *y = cosf(t * 0.2f) * 0.3f;
    *z = 9.81f + sinf(t * 0.1f) * 0.1f;
}

void kd_mag_get_heading(float* heading) {
    // Slowly rotate heading 0-360 over 2 minutes
    float t = hal_sdl2_millis() / 1000.0f;
    *heading = fmodf(t * 3.0f, 360.0f);
}

// ─────────────────────────────────────────────
//  C5 CO-PROCESSOR SIMULATION
//  The C5 handles WiFi 6 + BLE 5 + 802.15.4.
//  On SDL2: simulated as always-available.
//  c5_mutex is a real pthread mutex — apps that
//  honor it will behave correctly on real hardware.
// ─────────────────────────────────────────────
bool kd_c5_wifi_available() { return true; }
bool kd_c5_ble_available()  { return true; }
bool kd_c5_154_available()  { return true; }  // 802.15.4 (Thread/Zigbee)

// ─────────────────────────────────────────────
//  FRAME PRESENT OVERRIDE
//  Adds KodeDot status bar at bottom of 480×480:
//    Battery % | WiFi | BLE | NFC | Haptic indicator
// ─────────────────────────────────────────────
void kd_hal_present() {
    // Restore window title if haptic expired
    if (haptic_active && hal_sdl2_millis() > haptic_end_ms) {
        haptic_active = false;
        if (sdl_window)
            SDL_SetWindowTitle(sdl_window,
                "Pisces Moon OS — KodeDot Dev Environment");
    }

    // Render status bar at bottom 20px of display
    int bar_y = KD_SCREEN_H - 20;
    for (int x = 0; x < KD_SCREEN_W; x++)
        for (int y = bar_y; y < KD_SCREEN_H; y++)
            kd_pixel_buf[y * KD_SCREEN_W + x] = 0x0010;  // Dark blue

    // Use base HAL present with kd_pixel_buf
    pthread_mutex_lock(&sdl_spi_mutex);

    uint32_t rgba[KD_SCREEN_W * KD_SCREEN_H];
    for (int i = 0; i < KD_SCREEN_W * KD_SCREEN_H; i++) {
        uint16_t c = kd_pixel_buf[i];
        uint8_t r = ((c >> 11) & 0x1F) << 3;
        uint8_t g = ((c >>  5) & 0x3F) << 2;
        uint8_t b = ( c        & 0x1F) << 3;
        rgba[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    SDL_UpdateTexture(sdl_texture, nullptr, rgba, KD_SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer);

    pthread_mutex_unlock(&sdl_spi_mutex);
}

// ─────────────────────────────────────────────
//  KODEDOT SDL2 INIT
//  Called instead of hal_sdl2_init() for KodeDot builds
// ─────────────────────────────────────────────
bool hal_kodedot_sdl2_init() {
    // Override window size for 480×480
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "[KD] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    sdl_window = SDL_CreateWindow(
        "Pisces Moon OS — KodeDot Dev Environment",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        KD_SCREEN_W * SDL_SCALE, KD_SCREEN_H * SDL_SCALE,
        SDL_WINDOW_SHOWN
    );

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    sdl_texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        KD_SCREEN_W, KD_SCREEN_H);

    pthread_mutex_init(&sdl_input.lock, nullptr);
    memset(kd_pixel_buf, 0, sizeof(kd_pixel_buf));

    nfc_init();

    system("mkdir -p ./sd/apps ./sd/cyber_logs ./sd/roms/nes ./sd/roms/gb ./sd/payloads");

    printf("[KD] KodeDot SDL2 environment initialized\n");
    printf("[KD] Display: %dx%d (AMOLED square — theoretical)\n", KD_SCREEN_W, KD_SCREEN_H);
    printf("[KD] Processor: ESP32-P4 @ 400MHz (simulated)\n");
    printf("[KD] Co-proc: ESP32-C5 WiFi6/BLE5 (simulated)\n");
    printf("[KD] Peripherals: NFC ST25R3916B, RFID, IR, IMU, Haptic\n");
    printf("[KD] Controls:\n");
    printf("       Arrow keys  = D-pad\n");
    printf("       Enter       = OK/Goto\n");
    printf("       F1/F2/F3    = Top/Bottom/Left buttons\n");
    printf("       Mouse       = Touch\n");
    printf("[KD] TODOs remaining: see hal_kodedot.h\n");

    return true;
}

#endif // PISCES_KODEDOT_SDL2
