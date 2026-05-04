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
//  hal_sdl2.cpp — SDL2 HAL Implementation
//
//  Two-thread architecture mirrors ESP32-S3 dual core exactly:
//
//  core1_thread (main thread):
//    - Runs setup() then loop() — identical to Arduino Core 1
//    - Handles SDL event pump
//    - Drives all UI, touch, keyboard, apps
//
//  core0_thread (spawned pthread):
//    - Runs core0GhostTask() — identical to ESP32 Core 0
//    - Ghost Engine: WiFi scan simulation, BLE scan, GPS
//    - Checks wifi_in_use / sd_in_use traffic flags
//    - Honors spi_mutex before any "SD write" operations
//
//  sdl_spi_mutex mirrors the hardware SPI Bus Treaty:
//    - Any "SPI" operation (framebuffer flush, SD access)
//      must take this mutex first
//    - Same timeout values as real hardware:
//      3000ms init, 2000ms LoRa TX, 500ms channel, 20ms poll
// ============================================================

#ifdef PISCES_SDL

#include "hal_sdl2.h"
#include "arduino_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ─────────────────────────────────────────────
//  GLOBAL STATE DEFINITIONS
// ─────────────────────────────────────────────
pthread_mutex_t  sdl_spi_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_t        core0_thread;
pthread_t        core1_thread;
SDLInputState    sdl_input      = {0};

uint16_t         pixel_buf[SCREEN_W * SCREEN_H];
SDL_Window*      sdl_window   = nullptr;
SDL_Renderer*    sdl_renderer = nullptr;
SDL_Texture*     sdl_texture  = nullptr;

// Ghost Engine simulation state (mirrors wardrive.cpp globals)
int  networks_found  = 0;
int  bt_found        = 0;
int  esp_found       = 0;
bool wardrive_active = true;

// Traffic flags (mirrors main.cpp)
volatile bool wifi_in_use = false;
volatile bool sd_in_use   = false;

// ─────────────────────────────────────────────
//  FORWARD DECLARATIONS (defined in main_sdl2.cpp)
// ─────────────────────────────────────────────
extern void setup();
extern void loop();
extern void core0GhostTask(void* param);

// ─────────────────────────────────────────────
//  SDL KEY → ASCII
//  Maps SDL keysym to the same char values the
//  T-Deck keyboard firmware produces.
// ─────────────────────────────────────────────
static char sdl_key_to_ascii(SDL_Keycode key, SDL_Keymod mod) {
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (key >= SDLK_a && key <= SDLK_z)
        return shift ? (char)(key - 32) : (char)key;
    if (key >= SDLK_0 && key <= SDLK_9) {
        if (!shift) return (char)key;
        const char shifted[] = ")!@#$%^&*(";
        return shifted[key - SDLK_0];
    }
    switch (key) {
        case SDLK_RETURN:    return 13;
        case SDLK_BACKSPACE: return 8;
        case SDLK_SPACE:     return ' ';
        case SDLK_PERIOD:    return shift ? '>' : '.';
        case SDLK_COMMA:     return shift ? '<' : ',';
        case SDLK_SLASH:     return shift ? '?' : '/';
        case SDLK_MINUS:     return shift ? '_' : '-';
        case SDLK_EQUALS:    return shift ? '+' : '=';
        case SDLK_SEMICOLON: return shift ? ':' : ';';
        case SDLK_QUOTE:     return shift ? '"' : '\'';
        case SDLK_LEFTBRACKET:  return shift ? '{' : '[';
        case SDLK_RIGHTBRACKET: return shift ? '}' : ']';
        case SDLK_BACKSLASH:    return shift ? '|' : '\\';
        case SDLK_BACKQUOTE:    return shift ? '~' : '`';
        default: return 0;
    }
}

// ─────────────────────────────────────────────
//  KEY QUEUE HELPERS
// ─────────────────────────────────────────────
static void key_enqueue(char c) {
    pthread_mutex_lock(&sdl_input.lock);
    int next = (sdl_input.key_tail + 1) % 64;
    if (next != sdl_input.key_head) {
        sdl_input.key_queue[sdl_input.key_tail] = c;
        sdl_input.key_tail = next;
    }
    pthread_mutex_unlock(&sdl_input.lock);
}

char hal_sdl2_dequeue_key() {
    pthread_mutex_lock(&sdl_input.lock);
    char c = 0;
    if (sdl_input.key_head != sdl_input.key_tail) {
        c = sdl_input.key_queue[sdl_input.key_head];
        sdl_input.key_head = (sdl_input.key_head + 1) % 64;
    }
    pthread_mutex_unlock(&sdl_input.lock);
    return c;
}

// ─────────────────────────────────────────────
//  EVENT PUMP — drains SDL queue each frame
//  Called from Core 1 main loop
// ─────────────────────────────────────────────
void hal_sdl2_pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {

        case SDL_QUIT:
            sdl_input.quit_requested = true;
            break;

        // ── TOUCH via mouse ──
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.touch_x       = (int16_t)(e.button.x / SDL_SCALE);
                sdl_input.touch_y       = (int16_t)(e.button.y / SDL_SCALE);
                sdl_input.touch_pressed = true;
                pthread_mutex_unlock(&sdl_input.lock);
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.touch_pressed = false;
                pthread_mutex_unlock(&sdl_input.lock);
            }
            break;

        case SDL_MOUSEMOTION:
            if (e.motion.state & SDL_BUTTON_LMASK) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.touch_x = (int16_t)(e.motion.x / SDL_SCALE);
                sdl_input.touch_y = (int16_t)(e.motion.y / SDL_SCALE);
                pthread_mutex_unlock(&sdl_input.lock);
            }
            break;

        // ── TRACKBALL via arrow keys ──
        // ── KEYBOARD via all other keys ──
        case SDL_KEYDOWN: {
            SDL_Keycode k   = e.key.keysym.sym;
            SDL_Keymod  mod = (SDL_Keymod)e.key.keysym.mod;

            if (k == SDLK_UP) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_y = -1;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_DOWN) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_y = 1;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_LEFT) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_x = -1;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_RIGHT) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_x = 1;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_RETURN && (mod & KMOD_RALT)) {
                // Right-Alt+Enter = trackball click
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_click = true;
                pthread_mutex_unlock(&sdl_input.lock);
            } else {
                char c = sdl_key_to_ascii(k, mod);
                if (c) key_enqueue(c);
            }
            break;
        }

        case SDL_KEYUP: {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_UP || k == SDLK_DOWN) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_y = 0;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_LEFT || k == SDLK_RIGHT) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_x = 0;
                pthread_mutex_unlock(&sdl_input.lock);
            } else if (k == SDLK_RETURN) {
                pthread_mutex_lock(&sdl_input.lock);
                sdl_input.tb_click = false;
                pthread_mutex_unlock(&sdl_input.lock);
            }
            break;
        }

        default: break;
        }
    }
}

// ─────────────────────────────────────────────
//  FRAME PRESENT
//  Converts RGB565 pixel_buf → RGBA SDL texture
//  Takes spi_mutex to mirror SPI Bus Treaty
// ─────────────────────────────────────────────
void hal_sdl2_present() {
    pthread_mutex_lock(&sdl_spi_mutex);

    // Convert RGB565 → RGBA8888 for SDL
    uint32_t rgba[SCREEN_W * SCREEN_H];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t c = pixel_buf[i];
        uint8_t r = ((c >> 11) & 0x1F) << 3;
        uint8_t g = ((c >>  5) & 0x3F) << 2;
        uint8_t b = ( c        & 0x1F) << 3;
        rgba[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    SDL_UpdateTexture(sdl_texture, nullptr, rgba, SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer);

    pthread_mutex_unlock(&sdl_spi_mutex);
}

// ─────────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────────
uint32_t hal_sdl2_millis() {
    return SDL_GetTicks();
}

void hal_sdl2_delay(uint32_t ms) {
    SDL_Delay(ms);
}

// ─────────────────────────────────────────────
//  GHOST ENGINE SIMULATION — Core 0 thread
//
//  Mirrors the real wardrive.cpp Ghost Engine:
//  - Fires every ~4 seconds (matching real scan window)
//  - Increments networks_found, bt_found, esp_found
//  - Checks wifi_in_use and sd_in_use before "SD writes"
//  - Takes spi_mutex before any SD-equivalent operation
//  - Runs on its own thread = Core 0 behavior preserved
// ─────────────────────────────────────────────
static void* core0_ghost_runner(void* param) {
    printf("[CORE0] Ghost Engine thread started\n");
    hal_sdl2_delay(2000);  // Boot delay matches real hardware

    srand(42);  // Deterministic for dev reproducibility

    while (true) {
        hal_sdl2_delay(4000);  // Scan window: 4 seconds

        // Honor traffic flags — same logic as wardrive.cpp
        if (wifi_in_use || sd_in_use) {
            printf("[CORE0] Ghost Engine deferred — traffic flag set\n");
            continue;
        }

        // Take spi_mutex before SD write — SPI Bus Treaty
        if (pthread_mutex_trylock(&sdl_spi_mutex) == 0) {
            // Simulate WiFi scan
            int new_nets = rand() % 4;
            networks_found += new_nets;

            // Simulate BLE scan
            int new_bt = rand() % 3;
            bt_found += new_bt;

            // Simulate Espressif device detection
            if (rand() % 5 == 0) esp_found++;

            // Simulate writing to /sd/cyber_logs/ (CSV append)
            // In real hardware this is SdFat write — here just printf
            printf("[CORE0] Ghost scan: +%d WiFi +%d BLE | Total: %d/%d\n",
                   new_nets, new_bt, networks_found, bt_found);

            pthread_mutex_unlock(&sdl_spi_mutex);
        } else {
            printf("[CORE0] Ghost Engine: SPI mutex busy, skipping write\n");
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────
//  CORE 1 RUNNER — setup() then loop()
//  This IS the main thread on real hardware.
//  On SDL2 it's also the main thread since SDL
//  requires window/event handling on main thread.
// ─────────────────────────────────────────────
static void core1_run() {
    setup();
    while (!sdl_input.quit_requested) {
        hal_sdl2_pump_events();
        loop();
    }
}

// ─────────────────────────────────────────────
//  HAL INIT
// ─────────────────────────────────────────────
bool hal_sdl2_init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "[HAL] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    sdl_window = SDL_CreateWindow(
        "Pisces Moon OS — SDL2 Dev Environment",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * SDL_SCALE, SCREEN_H * SDL_SCALE,
        SDL_WINDOW_SHOWN
    );
    if (!sdl_window) {
        fprintf(stderr, "[HAL] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        fprintf(stderr, "[HAL] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    sdl_texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H);
    if (!sdl_texture) {
        fprintf(stderr, "[HAL] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    pthread_mutex_init(&sdl_input.lock, nullptr);
    memset(pixel_buf, 0, sizeof(pixel_buf));

    // Ensure SD root exists
    system("mkdir -p ./sd/apps ./sd/cyber_logs ./sd/roms/nes ./sd/roms/gb ./sd/payloads");

    printf("[HAL] SDL2 init complete — 320x240 @ 2x scale (640x480)\n");
    printf("[HAL] Controls:\n");
    printf("        Mouse click/drag = Touch\n");
    printf("        Arrow keys       = Trackball\n");
    printf("        RAlt+Enter       = Trackball click\n");
    printf("        All other keys   = T-Deck keyboard\n");
    printf("        ./sd/            = MicroSD root\n");
    printf("[HAL] Spawning Core 0 Ghost Engine thread...\n");

    // Spawn Core 0 thread
    if (pthread_create(&core0_thread, nullptr, core0_ghost_runner, nullptr) != 0) {
        fprintf(stderr, "[HAL] Failed to spawn Core 0 thread\n");
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────
//  HAL TEARDOWN
// ─────────────────────────────────────────────
void hal_sdl2_teardown() {
    pthread_cancel(core0_thread);
    pthread_join(core0_thread, nullptr);

    if (sdl_texture)  SDL_DestroyTexture(sdl_texture);
    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    if (sdl_window)   SDL_DestroyWindow(sdl_window);
    SDL_Quit();

    pthread_mutex_destroy(&sdl_input.lock);
    pthread_mutex_destroy(&sdl_spi_mutex);
}

// ─────────────────────────────────────────────
//  ENTRY POINT
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (!hal_sdl2_init()) return 1;
    core1_run();
    hal_sdl2_teardown();
    return 0;
}

#endif // PISCES_SDL
