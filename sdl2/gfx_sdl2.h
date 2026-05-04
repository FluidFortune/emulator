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
//  gfx_sdl2.h — Arduino_GFX API backed by SDL2 pixel buffer
//
//  App code calls gfx->fillScreen(), gfx->setCursor() etc
//  exactly as on real hardware. This class routes those calls
//  to pixel_buf[], then hal_sdl2_present() blits to screen.
//
//  RGB565 color values are preserved exactly — same constants
//  from theme.h work without modification.
//
//  Font: 6x8 bitmap font matching Adafruit GFX default.
//  Text rendering matches the real display character-for-character.
// ============================================================

#ifndef GFX_SDL2_H
#define GFX_SDL2_H

#ifdef PISCES_SDL

#include "hal_sdl2.h"
#include "arduino_compat.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────
//  6x8 BITMAP FONT
//  Adafruit GFX default font — 95 printable ASCII
//  characters starting at 0x20 (space).
//  Each character is 5 columns × 7 rows, padded to 6×8.
// ─────────────────────────────────────────────
static const uint8_t FONT_5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 space
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 !
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 $
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 %
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 &
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 (
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 )
    {0x08,0x2A,0x1C,0x2A,0x08}, // 0x2A *
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B +
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ,
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D -
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E .
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 0
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 1
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 2
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 3
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 4
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 6
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 7
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 8
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 9
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A :
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ;
    {0x00,0x08,0x14,0x22,0x41}, // 0x3C <
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D =
    {0x41,0x22,0x14,0x08,0x00}, // 0x3E >
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F ?
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 A
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 B
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 D
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 E
    {0x7F,0x09,0x09,0x09,0x01}, // 0x46 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 0x47 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 H
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 I
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A J
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B K
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C L
    {0x7F,0x02,0x04,0x02,0x7F}, // 0x4D M
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E N
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F O
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 R
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 S
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 0x57 W
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 X
    {0x07,0x08,0x70,0x08,0x07}, // 0x59 Y
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A Z
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B [
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ]
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E ^
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F _
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 `
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 a
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 b
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 c
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 d
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 e
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 f
    {0x08,0x14,0x54,0x54,0x3C}, // 0x67 g
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 h
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 i
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A j
    {0x7F,0x10,0x28,0x44,0x00}, // 0x6B k
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C l
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D m
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E n
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F o
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 p
    {0x08,0x14,0x14,0x18,0x7C}, // 0x71 q
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 r
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 s
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 t
    {0x3C,0x40,0x40,0x40,0x3C}, // 0x75 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 w
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 y
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A z
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B {
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C |
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D }
    {0x08,0x08,0x2A,0x1C,0x08}, // 0x7E ~
};

// ─────────────────────────────────────────────
//  SDL2 GFX — Arduino_GFX-compatible class
// ─────────────────────────────────────────────
class Arduino_GFX_SDL2 {
public:
    int16_t  _cursor_x  = 0;
    int16_t  _cursor_y  = 0;
    uint16_t _text_color = 0xFFFF;
    uint8_t  _text_size  = 1;
    bool     _dirty      = false;  // Set when pixel_buf changes

    void begin() {
        memset(pixel_buf, 0, sizeof(pixel_buf));
        hal_sdl2_present();
    }

    // ── PIXEL ──
    inline void drawPixel(int16_t x, int16_t y, uint16_t color) {
        if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
        pixel_buf[y * SCREEN_W + x] = color;
        _dirty = true;
    }

    inline uint16_t getPixel(int16_t x, int16_t y) {
        if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return 0;
        return pixel_buf[y * SCREEN_W + x];
    }

    // ── FILL ──
    void fillScreen(uint16_t color) {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) pixel_buf[i] = color;
        hal_sdl2_present();
        _dirty = false;
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        for (int16_t row = y; row < y + h; row++)
            for (int16_t col = x; col < x + w; col++)
                drawPixel(col, row, color);
        hal_sdl2_present();
        _dirty = false;
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        drawFastHLine(x,     y,     w, color);
        drawFastHLine(x,     y+h-1, w, color);
        drawFastVLine(x,     y,     h, color);
        drawFastVLine(x+w-1, y,     h, color);
    }

    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color) {
        fillRect(x + r, y, w - 2*r, h, color);
        fillRect(x, y + r, r, h - 2*r, color);
        fillRect(x + w - r, y + r, r, h - 2*r, color);
        // Corners (approx circle quadrants)
        for (int dy = 0; dy <= r; dy++)
            for (int dx = 0; dx <= r; dx++)
                if (dx*dx + dy*dy <= r*r) {
                    drawPixel(x + r - dx,     y + r - dy,     color);
                    drawPixel(x + w - r + dx - 1, y + r - dy, color);
                    drawPixel(x + r - dx,     y + h - r + dy - 1, color);
                    drawPixel(x + w - r + dx - 1, y + h - r + dy - 1, color);
                }
    }

    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color) {
        drawFastHLine(x + r, y,     w - 2*r, color);
        drawFastHLine(x + r, y+h-1, w - 2*r, color);
        drawFastVLine(x,     y + r, h - 2*r, color);
        drawFastVLine(x+w-1, y + r, h - 2*r, color);
    }

    void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
        for (int16_t y = -r; y <= r; y++)
            for (int16_t x = -r; x <= r; x++)
                if (x*x + y*y <= r*r)
                    drawPixel(cx + x, cy + y, color);
    }

    void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
        int16_t x = 0, y = r, d = 1 - r;
        while (x <= y) {
            drawPixel(cx+x,cy+y,color); drawPixel(cx-x,cy+y,color);
            drawPixel(cx+x,cy-y,color); drawPixel(cx-x,cy-y,color);
            drawPixel(cx+y,cy+x,color); drawPixel(cx-y,cy+x,color);
            drawPixel(cx+y,cy-x,color); drawPixel(cx-y,cy-x,color);
            if (d < 0) d += 2*x+3; else { d += 2*(x-y)+5; y--; }
            x++;
        }
    }

    // ── LINES ──
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        for (int16_t i = x; i < x + w; i++) drawPixel(i, y, color);
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        for (int16_t i = y; i < y + h; i++) drawPixel(x, i, color);
    }

    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        int16_t dx = abs(x1-x0), dy = abs(y1-y0);
        int16_t sx = x0<x1 ? 1 : -1, sy = y0<y1 ? 1 : -1;
        int16_t err = dx - dy;
        while (true) {
            drawPixel(x0, y0, color);
            if (x0==x1 && y0==y1) break;
            int16_t e2 = 2*err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }

    void drawTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,
                      int16_t x2,int16_t y2,uint16_t color) {
        drawLine(x0,y0,x1,y1,color);
        drawLine(x1,y1,x2,y2,color);
        drawLine(x2,y2,x0,y0,color);
    }

    void fillTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,
                      int16_t x2,int16_t y2,uint16_t color) {
        // Simple scanline fill
        if (y0 > y1) { std::swap(x0,x1); std::swap(y0,y1); }
        if (y1 > y2) { std::swap(x1,x2); std::swap(y1,y2); }
        if (y0 > y1) { std::swap(x0,x1); std::swap(y0,y1); }
        for (int16_t y = y0; y <= y2; y++) {
            float t1 = (y <= y1) ? (float)(y-y0)/(y1-y0+1) : 1.0f;
            float t2 = (float)(y-y0)/(y2-y0+1);
            int16_t xa = x0 + (x1-x0)*t1;
            int16_t xb = x0 + (x2-x0)*t2;
            if (xa > xb) std::swap(xa, xb);
            drawFastHLine(xa, y, xb-xa+1, color);
        }
    }

    // ── TEXT ──
    void setCursor(int16_t x, int16_t y) { _cursor_x = x; _cursor_y = y; }
    void setTextColor(uint16_t c)        { _text_color = c; }
    void setTextColor(uint16_t fg, uint16_t bg) { _text_color = fg; (void)bg; }
    void setTextSize(uint8_t s)          { _text_size = s; }
    int16_t getCursorX()                 { return _cursor_x; }
    int16_t getCursorY()                 { return _cursor_y; }

    void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint8_t size = 1) {
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t* bmp = FONT_5X7[c - 0x20];
        for (int col = 0; col < 5; col++) {
            uint8_t line = bmp[col];
            for (int row = 0; row < 7; row++) {
                if (line & (1 << row)) {
                    if (size == 1) {
                        drawPixel(x + col, y + row, color);
                    } else {
                        fillRect(x + col*size, y + row*size, size, size, color);
                    }
                }
            }
        }
    }

    void print(char c) {
        if (c == '\n') {
            _cursor_y += 8 * _text_size;
            _cursor_x  = 0;
            return;
        }
        if (c == '\r') return;
        drawChar(_cursor_x, _cursor_y, c, _text_color, _text_size);
        _cursor_x += 6 * _text_size;
        hal_sdl2_present();
    }

    void print(const char* s) {
        while (s && *s) print(*s++);
    }

    void print(const String& s)  { print(s.c_str()); }
    void print(int n)            { char buf[24]; snprintf(buf,sizeof(buf),"%d",n); print(buf); }
    void print(long n)           { char buf[24]; snprintf(buf,sizeof(buf),"%ld",n); print(buf); }
    void print(float f, int d=2) { char buf[24]; snprintf(buf,sizeof(buf),"%.*f",d,f); print(buf); }
    void print(double f,int d=2) { char buf[24]; snprintf(buf,sizeof(buf),"%.*f",d,f); print(buf); }
    void print(uint8_t n, int base=10) {
        char buf[24];
        if (base==16) snprintf(buf,sizeof(buf),"%02X",n);
        else snprintf(buf,sizeof(buf),"%u",n);
        print(buf);
    }
    void print(uint32_t n, int base=10) {
        char buf[24];
        if (base==16) snprintf(buf,sizeof(buf),"%08X",n);
        else snprintf(buf,sizeof(buf),"%u",n);
        print(buf);
    }

    void println(const char* s)  { print(s); print('\n'); }
    void println(const String& s){ print(s); print('\n'); }
    void println(int n)          { print(n); print('\n'); }
    void println()               { print('\n'); }

    void printf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        print(buf);
    }

    // ── BITMAP ──
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bmp,
                    int16_t w, int16_t h, uint16_t color) {
        for (int16_t row = 0; row < h; row++) {
            for (int16_t col = 0; col < w; col++) {
                int byte_idx = (row * w + col) / 8;
                int bit_idx  = 7 - ((row * w + col) % 8);
                if (bmp[byte_idx] & (1 << bit_idx))
                    drawPixel(x + col, y + row, color);
            }
        }
    }

    // ── SCREEN INFO ──
    int16_t width()  { return SCREEN_W; }
    int16_t height() { return SCREEN_H; }
};

// The global gfx pointer — same name as in main.cpp
// Apps reference via extern Arduino_GFX* gfx;
// On SDL2, Arduino_GFX is typedef'd to Arduino_GFX_SDL2
typedef Arduino_GFX_SDL2 Arduino_GFX;

#endif // PISCES_SDL
#endif // GFX_SDL2_H
