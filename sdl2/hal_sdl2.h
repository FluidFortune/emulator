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
//  hal_sdl2.h — Pisces Moon OS SDL2 Emulation HAL
//
//  Provides the same interface as the ESP32-S3 hardware layer
//  but backed by SDL2 on Linux/macOS/Windows.
//
//  Architecture mirrors real hardware exactly:
//    - Two pthreads = Core 0 (Ghost Engine) + Core 1 (UI/Apps)
//    - SDL2 mutex   = spi_mutex (SPI Bus Treaty)
//    - SDL2 window  = 320x240 IPS display (2x scaled to 640x480)
//    - Mouse        = GT911 touch input
//    - Arrow keys   = Trackball (250ms / 80ms lockout honored)
//    - Keyboard     = T-Deck QWERTY pass-through
//    - ./sd/        = MicroSD card filesystem root
//    - malloc()     = PSRAM (CONFIG_SPIRAM_USE_MALLOC behavior)
//
//  Developers write apps against the standard Pisces Moon API.
//  No SDL2 calls appear in app code. The HAL is invisible.
// ============================================================

#ifndef HAL_SDL2_H
#define HAL_SDL2_H

#ifdef PISCES_SDL

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <SDL2/SDL.h>

// ─────────────────────────────────────────────
//  DISPLAY GEOMETRY — matches T-Deck Plus exactly
// ─────────────────────────────────────────────
#define SCREEN_W        320
#define SCREEN_H        240
#define SDL_SCALE       2       // Window rendered at 640x480

// ─────────────────────────────────────────────
//  SPI MUTEX — mirrors spi_mutex in main.cpp
//  All SDL2 render calls are serialized through
//  this mutex, exactly as SPI Bus Treaty requires.
// ─────────────────────────────────────────────
extern pthread_mutex_t sdl_spi_mutex;

// FreeRTOS-compatible macro for app code that uses xSemaphoreTake
#define pdMS_TO_TICKS(ms)   (ms)
#define pdTRUE              1
typedef pthread_mutex_t*    SemaphoreHandle_t_SDL;

// ─────────────────────────────────────────────
//  DUAL CORE THREAD HANDLES
//  core1_thread = UI + apps  (mirrors ESP32 Core 1)
//  core0_thread = Ghost Engine (mirrors ESP32 Core 0)
// ─────────────────────────────────────────────
extern pthread_t core0_thread;
extern pthread_t core1_thread;

// ─────────────────────────────────────────────
//  SHARED INPUT STATE
//  Written by the SDL event pump (core1 main loop)
//  Read by get_touch(), get_keypress(), update_trackball()
// ─────────────────────────────────────────────
typedef struct {
    // Touch / mouse
    int16_t  touch_x;
    int16_t  touch_y;
    bool     touch_pressed;

    // Keyboard queue — single char ring buffer (64 deep)
    char     key_queue[64];
    int      key_head;
    int      key_tail;

    // Trackball state — set by arrow key events
    int      tb_x;      // -1=LEFT, 0=NONE, 1=RIGHT
    int      tb_y;      // -1=UP,   0=NONE, 1=DOWN
    bool     tb_click;

    // Quit signal from window close
    bool     quit_requested;

    pthread_mutex_t lock;
} SDLInputState;

extern SDLInputState sdl_input;

// ─────────────────────────────────────────────
//  SDL2 DISPLAY SURFACE
//  Apps draw to pixel_buf via the GFX wrapper.
//  The SDL event loop blits pixel_buf to the
//  window texture every frame.
// ─────────────────────────────────────────────
extern uint16_t  pixel_buf[SCREEN_W * SCREEN_H];  // RGB565 framebuffer
extern SDL_Window*   sdl_window;
extern SDL_Renderer* sdl_renderer;
extern SDL_Texture*  sdl_texture;

// ─────────────────────────────────────────────
//  SD CARD EMULATION
//  Maps /sd/ on the host filesystem.
//  Create ./sd/apps/ and ./sd/cyber_logs/ etc
//  to match the real MicroSD layout.
// ─────────────────────────────────────────────
#define SDL_SD_ROOT     "./sd"

// ─────────────────────────────────────────────
//  HAL INIT / TEARDOWN
// ─────────────────────────────────────────────
bool  hal_sdl2_init();      // Called once at startup — creates window, threads
void  hal_sdl2_teardown();  // Cleanup on exit

// ─────────────────────────────────────────────
//  FRAME PRESENT
//  Call after all GFX drawing for a frame is done.
//  Blits pixel_buf → SDL texture → screen.
//  The GFX wrapper calls this automatically after
//  any full-screen operation. Apps don't need to
//  call it directly.
// ─────────────────────────────────────────────
void  hal_sdl2_present();

// ─────────────────────────────────────────────
//  EVENT PUMP
//  Called from Core 1 main loop every iteration.
//  Drains SDL event queue → updates sdl_input.
// ─────────────────────────────────────────────
void  hal_sdl2_pump_events();

// ─────────────────────────────────────────────
//  TIMING
//  Wraps SDL_GetTicks() to provide millis() / delay()
// ─────────────────────────────────────────────
uint32_t hal_sdl2_millis();
void     hal_sdl2_delay(uint32_t ms);

#endif // PISCES_SDL
#endif // HAL_SDL2_H
