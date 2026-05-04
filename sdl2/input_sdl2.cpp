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
//  input_sdl2.cpp — Touch, Trackball, Keyboard for SDL2 build
//
//  Implements the exact same function signatures as:
//    touch.cpp     → init_touch(), get_touch()
//    trackball.cpp → init_trackball(), update_trackball(),
//                    update_trackball_game()
//    keyboard.cpp  → init_keyboard(), get_keypress()
//
//  Timing is preserved exactly:
//    update_trackball()      — 250ms lockout  (UI navigation)
//    update_trackball_game() — 80ms lockout   (game loops)
//
//  Input is sourced from sdl_input (hal_sdl2.cpp event pump):
//    Mouse click/drag → touch_x, touch_y, touch_pressed
//    Arrow keys       → tb_x, tb_y
//    RAlt+Enter       → tb_click
//    All other keys   → key_queue
// ============================================================

#ifdef PISCES_SDL

#include "hal_sdl2.h"
#include "arduino_compat.h"
#include <stdint.h>

// ─────────────────────────────────────────────
//  TOUCH
// ─────────────────────────────────────────────
bool init_touch() {
    printf("[INPUT] GT911 touch → SDL2 mouse\n");
    return true;
}

bool get_touch(int16_t* x, int16_t* y) {
    pthread_mutex_lock(&sdl_input.lock);
    bool pressed = sdl_input.touch_pressed;
    if (pressed) {
        *x = sdl_input.touch_x;
        *y = sdl_input.touch_y;
    }
    pthread_mutex_unlock(&sdl_input.lock);
    return pressed;
}

// ─────────────────────────────────────────────
//  TRACKBALL
//  Timing lockouts match real hardware exactly.
//  update_trackball()      = 250ms UI lockout
//  update_trackball_game() = 80ms game lockout
// ─────────────────────────────────────────────
static uint32_t tb_last_move_ui   = 0;
static uint32_t tb_last_move_game = 0;
static bool     tb_click_consumed = false;

void init_trackball() {
    printf("[INPUT] Trackball → Arrow keys (↑↓←→) + RAlt+Enter=click\n");
}

// Internal — reads state, applies lockout
static TrackballState _trackball_read(uint32_t lockout_ms, uint32_t* last_time) {
    TrackballState out = {0, 0, false};

    pthread_mutex_lock(&sdl_input.lock);
    int tx = sdl_input.tb_x;
    int ty = sdl_input.tb_y;
    bool click = sdl_input.tb_click;

    // Click is edge-triggered — consume it once
    if (click && !tb_click_consumed) {
        out.clicked = true;
        tb_click_consumed = true;
    } else if (!click) {
        tb_click_consumed = false;
    }
    pthread_mutex_unlock(&sdl_input.lock);

    // Apply lockout to movement
    uint32_t now = hal_sdl2_millis();
    if (tx != 0 || ty != 0) {
        if (now - *last_time >= lockout_ms) {
            out.x = tx;
            out.y = ty;
            *last_time = now;
        }
    }

    return out;
}

TrackballState update_trackball() {
    return _trackball_read(250, &tb_last_move_ui);
}

TrackballState update_trackball_game() {
    return _trackball_read(80, &tb_last_move_game);
}

// ─────────────────────────────────────────────
//  KEYBOARD
// ─────────────────────────────────────────────
void init_keyboard() {
    printf("[INPUT] T-Deck keyboard → SDL2 keyboard\n");
}

char get_keypress() {
    return hal_sdl2_dequeue_key();
}

// Export hal_sdl2_dequeue_key for keyboard.cpp
char hal_sdl2_dequeue_key();

#endif // PISCES_SDL
