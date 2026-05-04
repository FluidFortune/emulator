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
//  arduino_compat.h — Arduino API shim for SDL2 native build
//
//  Provides the Arduino/ESP32 API surface that Pisces Moon
//  app code uses, backed by POSIX/SDL2 on the host.
//
//  Covers:
//    - millis() / delay() / yield()
//    - String class (thin std::string wrapper)
//    - Serial (printf-backed)
//    - pinMode / digitalWrite / digitalRead (no-ops)
//    - FreeRTOS stubs (vTaskDelay, xSemaphore*)
//    - PSRAM stubs (ps_malloc → malloc)
//    - pgm_read_* (pass-through)
//    - min/max/abs/constrain/map
//    - F() macro (no-op on native)
// ============================================================

#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#ifdef PISCES_SDL

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <algorithm>
#include <SDL2/SDL.h>
#include "hal_sdl2.h"

// ─────────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────────
inline uint32_t millis()         { return hal_sdl2_millis(); }
inline uint32_t micros()         { return hal_sdl2_millis() * 1000; }
inline void     delay(uint32_t ms) { hal_sdl2_delay(ms); }
inline void     yield()          { SDL_Delay(0); }

// ─────────────────────────────────────────────
//  GPIO STUBS — no-ops on SDL2
//  Apps that check GPIO directly are not portable
//  by design; the HAL wrappers handle input.
// ─────────────────────────────────────────────
#define INPUT         0
#define OUTPUT        1
#define INPUT_PULLUP  2
#define HIGH          1
#define LOW           0

inline void     pinMode(int, int)         {}
inline void     digitalWrite(int, int)    {}
inline int      digitalRead(int)          { return LOW; }
inline void     analogWrite(int, int)     {}
inline int      analogRead(int)           { return 0; }

// ─────────────────────────────────────────────
//  MATH HELPERS
// ─────────────────────────────────────────────
template<typename T> inline T _max(T a, T b) { return a > b ? a : b; }
template<typename T> inline T _min(T a, T b) { return a < b ? a : b; }
#define max(a,b) _max(a,b)
#define min(a,b) _min(a,b)
#define abs(x)   ((x) < 0 ? -(x) : (x))
#define constrain(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#define map(v,fl,fh,tl,th) ((v - fl) * (th - tl) / (fh - fl) + tl)
#define sq(x)    ((x)*(x))
#define radians(d) ((d)*M_PI/180.0)
#define degrees(r) ((r)*180.0/M_PI)
#define PI       M_PI

// ─────────────────────────────────────────────
//  PSRAM STUBS
//  CONFIG_SPIRAM_USE_MALLOC behavior: on real
//  hardware large allocs go to PSRAM. On SDL2
//  they just go to heap — same semantics for
//  app code that doesn't care about the address.
// ─────────────────────────────────────────────
inline void* ps_malloc(size_t size) { return malloc(size); }
inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
inline size_t ESP_getFreeHeap()    { return 4 * 1024 * 1024; }  // Report 4MB "PSRAM"
inline size_t ESP_getFreePsram()   { return 4 * 1024 * 1024; }

// ─────────────────────────────────────────────
//  PROGMEM / FLASH STUBS
//  On ESP32, PROGMEM is a no-op. Same here.
// ─────────────────────────────────────────────
#define PROGMEM
#define PSTR(s) (s)
#define F(s)    (s)
#define pgm_read_byte(p)   (*(const uint8_t*)(p))
#define pgm_read_word(p)   (*(const uint16_t*)(p))
#define pgm_read_dword(p)  (*(const uint32_t*)(p))
#define pgm_read_ptr(p)    (*(const void**)(p))
#define strlen_P           strlen
#define strcpy_P           strcpy
#define strcmp_P           strcmp

// ─────────────────────────────────────────────
//  STRING CLASS
//  Wraps std::string with Arduino String API
// ─────────────────────────────────────────────
class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(char c) : std::string(1, c) {}
    String(int n, int base = 10) {
        char buf[32];
        if (base == 16) snprintf(buf, sizeof(buf), "%x", n);
        else if (base == 8) snprintf(buf, sizeof(buf), "%o", n);
        else snprintf(buf, sizeof(buf), "%d", n);
        assign(buf);
    }
    String(long n, int base = 10) {
        char buf[32];
        snprintf(buf, sizeof(buf), base==16 ? "%lx" : "%ld", n);
        assign(buf);
    }
    String(float f, int decimals = 2) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.*f", decimals, f);
        assign(buf);
    }
    String(double f, int decimals = 2) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.*f", decimals, f);
        assign(buf);
    }

    // Arduino API
    int       indexOf(char c, int from=0)     const { return (int)find(c, from); }
    int       indexOf(const char* s, int from=0) const {
        size_t p = find(s, from); return p == npos ? -1 : (int)p;
    }
    int       lastIndexOf(char c)             const {
        size_t p = rfind(c); return p == npos ? -1 : (int)p;
    }
    String    substring(int from, int to=-1)  const {
        return to < 0 ? String(substr(from).c_str()) : String(substr(from, to-from).c_str());
    }
    void      remove(int idx, int cnt=1)            { erase(idx, cnt); }
    void      replace(const String& f, const String& t) { 
        size_t pos = 0;
        while ((pos = find(f, pos)) != npos) { std::string::replace(pos, f.length(), t); pos += t.length(); }
    }
    void      toUpperCase()  { for(auto& c : *this) c = toupper(c); }
    void      toLowerCase()  { for(auto& c : *this) c = tolower(c); }
    void      trim()         { 
        erase(begin(), std::find_if(begin(), end(), [](int c){return !isspace(c);}));
        erase(std::find_if(rbegin(), rend(), [](int c){return !isspace(c);}).base(), end());
    }
    int       toInt()        const { return atoi(c_str()); }
    float     toFloat()      const { return atof(c_str()); }
    double    toDouble()     const { return atof(c_str()); }
    bool      startsWith(const String& s) const { return find(s) == 0; }
    bool      endsWith(const String& s)   const {
        if (s.length() > length()) return false;
        return rfind(s) == length() - s.length();
    }
    char      charAt(int i)  const { return at(i); }
    int       compareTo(const String& s) const { return compare(s); }
    bool      equalsIgnoreCase(const String& s) const {
        if (length() != s.length()) return false;
        for (size_t i = 0; i < length(); i++)
            if (tolower(at(i)) != tolower(s.at(i))) return false;
        return true;
    }
    int       length()      const { return (int)std::string::length(); }
    const char* c_str()     const { return std::string::c_str(); }
    bool      isEmpty()     const { return empty(); }
    void      concat(const String& s)  { append(s); }
    void      concat(const char* s)    { append(s); }
    void      concat(char c)           { push_back(c); }

    String operator+(const String& s)  const { return String((std::string(*this) + std::string(s)).c_str()); }
    String operator+(const char* s)    const { return String((std::string(*this) + s).c_str()); }
    String operator+(char c)           const { return String((std::string(*this) + c).c_str()); }
    String operator+(int n)            const { return *this + String(n); }
    String& operator+=(const String& s)      { append(s); return *this; }
    String& operator+=(const char* s)        { append(s); return *this; }
    String& operator+=(char c)               { push_back(c); return *this; }
    bool operator==(const String& s)   const { return compare(s) == 0; }
    bool operator==(const char* s)     const { return compare(s) == 0; }
    bool operator!=(const String& s)   const { return compare(s) != 0; }
    bool operator!=(const char* s)     const { return compare(s) != 0; }
};

// ─────────────────────────────────────────────
//  SERIAL — printf-backed
// ─────────────────────────────────────────────
class SerialClass {
public:
    void begin(int)         {}
    void print(const char* s)   { printf("%s", s); }
    void print(const String& s) { printf("%s", s.c_str()); }
    void print(int n)           { printf("%d", n); }
    void print(float f)         { printf("%.2f", f); }
    void print(double f)        { printf("%.2f", f); }
    void print(char c)          { printf("%c", c); }
    void println(const char* s) { printf("%s\n", s); }
    void println(const String& s) { printf("%s\n", s.c_str()); }
    void println(int n)         { printf("%d\n", n); }
    void println(float f)       { printf("%.2f\n", f); }
    void println(char c)        { printf("%c\n", c); }
    void println()              { printf("\n"); }
    void flush()                {}
    int  available()            { return 0; }
    int  read()                 { return -1; }
    template<typename T>
    void printf(const char* fmt, T val) { ::printf(fmt, val); }
};
extern SerialClass Serial;

// ─────────────────────────────────────────────
//  FREERTOS STUBS
//  Pisces Moon uses xSemaphoreTake/Give for the
//  SPI Bus Treaty. SDL2 pthread mutex handles this.
// ─────────────────────────────────────────────
typedef void*    TaskHandle_t;
typedef uint32_t TickType_t;

#define portTICK_PERIOD_MS  1
#define pdTRUE              1
#define pdFALSE             0

// SemaphoreHandle_t is already typedef'd in hal_sdl2.h
// Remap FreeRTOS calls to pthread equivalents
#define xSemaphoreCreateMutex()         (&sdl_spi_mutex)
#define xSemaphoreTake(m, t)            (pthread_mutex_lock(m) == 0 ? 1 : 0)
#define xSemaphoreGive(m)               pthread_mutex_unlock(m)
#define xSemaphoreTakeTimeout(m,t)      xSemaphoreTake(m,t)

inline void vTaskDelay(uint32_t ticks) { hal_sdl2_delay(ticks); }

inline void xTaskCreatePinnedToCore(
    void(*fn)(void*), const char* name, int stack,
    void* param, int prio, TaskHandle_t* handle, int core)
{
    // Core 0 task spawned from setup() — already running as core0_thread
    // This call is a no-op; the thread was pre-spawned by hal_sdl2_init()
    (void)fn; (void)name; (void)stack; (void)param; (void)prio;
    (void)handle; (void)core;
    printf("[HAL] xTaskCreatePinnedToCore('%s') → already pinned to Core 0 thread\n", name);
}

// WDT stubs
inline void esp_task_wdt_reset() {}
inline void esp_task_wdt_init(int, bool) {}

// ─────────────────────────────────────────────
//  IRAM_ATTR / DRAM_ATTR — no-ops
// ─────────────────────────────────────────────
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR

// ─────────────────────────────────────────────
//  SPI / WIRE STUBS
//  Apps don't call SPI directly (SPI Bus Treaty)
//  but the headers need to compile.
// ─────────────────────────────────────────────
class SPIClass {
public:
    void begin(int, int, int, int = -1) {}
    void begin()                        {}
    void end()                          {}
};

class TwoWire {
public:
    void begin(int, int) {}
    void begin()         {}
    void end()           {}
    void beginTransmission(int) {}
    int  endTransmission()      { return 0; }
    int  requestFrom(int, int)  { return 0; }
    void write(uint8_t)         {}
    int  read()                 { return 0; }
    int  available()            { return 0; }
};

extern SPIClass  SPI;
extern TwoWire   Wire;

// ─────────────────────────────────────────────
//  SNPRINTF WRAPPER (matches Arduino behavior)
// ─────────────────────────────────────────────
#define snprintf_P snprintf

#endif // PISCES_SDL
#endif // ARDUINO_COMPAT_H
