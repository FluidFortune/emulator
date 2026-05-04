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
//  sdfat_sdl2.h — SdFat API backed by host filesystem
//
//  Maps the SdFat 2.x API that Pisces Moon uses to POSIX
//  file I/O on the host. The SD card root becomes ./sd/
//
//  Supported API surface (what Pisces Moon actually calls):
//    SdFat sd;
//    sd.begin(cs, SPI_SPEED)
//    sd.exists(path)
//    sd.mkdir(path)
//    sd.remove(path)
//    SdFile f; f.open(path, flags); f.read/write/close/seek
//    SdFile dir; dir.openRoot(); dir.openNextFile()
//
//  The SPI Bus Treaty is honored: all sd.* operations in the
//  real codebase are wrapped in xSemaphoreTake(spi_mutex).
//  On SDL2, that mutex is sdl_spi_mutex — same guarantee.
// ============================================================

#ifndef SDFAT_SDL2_H
#define SDFAT_SDL2_H

#ifdef PISCES_SDL

#include "arduino_compat.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

#define SDL_SD_ROOT     "./sd"
#define O_READ          0x01
#define O_WRITE         0x02
#define O_CREAT         0x04
#define O_APPEND        0x08
#define O_TRUNC         0x10
#define O_RDWR          (O_READ | O_WRITE)
#define FILE_READ       O_READ
#define FILE_WRITE      (O_WRITE | O_CREAT | O_APPEND)
#define FILE_OVERWRITE  (O_WRITE | O_CREAT | O_TRUNC)

// ─────────────────────────────────────────────
//  PATH HELPER — prepend ./sd/ to all SD paths
// ─────────────────────────────────────────────
static inline std::string sd_host_path(const char* path) {
    std::string p = SDL_SD_ROOT;
    if (path && path[0] != '/') p += '/';
    if (path) p += path;
    return p;
}

// ─────────────────────────────────────────────
//  SdFile — wraps FILE* with SdFat-compatible API
// ─────────────────────────────────────────────
class SdFile {
public:
    FILE*       _fp        = nullptr;
    DIR*        _dir       = nullptr;
    bool        _is_dir    = false;
    char        _path[256] = {0};
    std::string _dir_path;
    struct dirent* _dir_entry = nullptr;

    bool open(const char* path, uint8_t flags = O_READ) {
        close();
        std::string hp = sd_host_path(path);
        strncpy(_path, path, sizeof(_path)-1);

        // Check if it's a directory
        struct stat st;
        if (stat(hp.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            _is_dir = true;
            _dir = opendir(hp.c_str());
            _dir_path = hp;
            return _dir != nullptr;
        }

        const char* mode = "rb";
        if (flags & O_TRUNC)  mode = "wb";
        else if (flags & O_APPEND) mode = "ab";
        else if (flags & O_WRITE)  mode = "r+b";

        _fp = fopen(hp.c_str(), mode);
        if (!_fp && (flags & O_CREAT)) {
            // Create parent directories
            std::string dir_part = hp.substr(0, hp.rfind('/'));
            std::string cmd = "mkdir -p \"" + dir_part + "\"";
            system(cmd.c_str());
            _fp = fopen(hp.c_str(), (flags & O_APPEND) ? "ab" : "wb");
        }
        _is_dir = false;
        return _fp != nullptr;
    }

    bool openRoot() {
        close();
        _is_dir = true;
        _dir_path = SDL_SD_ROOT;
        _dir = opendir(SDL_SD_ROOT);
        return _dir != nullptr;
    }

    // Directory iteration
    SdFile openNextFile() {
        SdFile f;
        if (!_is_dir || !_dir) return f;
        while ((_dir_entry = readdir(_dir)) != nullptr) {
            if (_dir_entry->d_name[0] == '.') continue;
            std::string child = _dir_path + "/" + _dir_entry->d_name;
            f.open(child.c_str(), O_READ);
            return f;
        }
        return f;
    }

    void rewindDirectory() {
        if (_dir) rewinddir(_dir);
    }

    bool isDirectory() { return _is_dir; }

    bool isOpen() { return (_fp != nullptr) || (_dir != nullptr); }

    operator bool() { return isOpen(); }

    void close() {
        if (_fp)  { fclose(_fp);  _fp  = nullptr; }
        if (_dir) { closedir(_dir); _dir = nullptr; }
        _is_dir = false;
    }

    int read(void* buf, size_t len) {
        if (!_fp) return -1;
        return (int)fread(buf, 1, len, _fp);
    }

    int read() {
        if (!_fp) return -1;
        int c = fgetc(_fp);
        return (c == EOF) ? -1 : c;
    }

    size_t write(const void* buf, size_t len) {
        if (!_fp) return 0;
        return fwrite(buf, 1, len, _fp);
    }

    size_t write(uint8_t c) {
        if (!_fp) return 0;
        return fwrite(&c, 1, 1, _fp);
    }

    size_t write(const char* s) {
        if (!_fp || !s) return 0;
        return fwrite(s, 1, strlen(s), _fp);
    }

    bool seek(uint32_t pos) {
        if (!_fp) return false;
        return fseek(_fp, pos, SEEK_SET) == 0;
    }

    uint32_t position() {
        if (!_fp) return 0;
        return (uint32_t)ftell(_fp);
    }

    uint32_t size() {
        if (!_fp) return 0;
        long cur = ftell(_fp);
        fseek(_fp, 0, SEEK_END);
        long sz = ftell(_fp);
        fseek(_fp, cur, SEEK_SET);
        return (uint32_t)sz;
    }

    bool available() {
        if (!_fp) return false;
        return ftell(_fp) < (long)size();
    }

    void flush() { if (_fp) fflush(_fp); }

    // Arduino print-style
    size_t print(const char* s) { return s ? write(s) : 0; }
    size_t print(const String& s) { return write(s.c_str()); }
    size_t println(const char* s) { size_t n = write(s); n += write("\n"); return n; }
    size_t println(const String& s) { return println(s.c_str()); }
    size_t println(int n) { char buf[24]; snprintf(buf,sizeof(buf),"%d\n",n); return write(buf); }

    // Name
    const char* name() {
        const char* p = strrchr(_path, '/');
        return p ? p+1 : _path;
    }
};

// ─────────────────────────────────────────────
//  SdFat — main class, wraps host filesystem
// ─────────────────────────────────────────────
class SdFat {
public:
    bool begin(uint8_t cs = 0, uint32_t speed = 0) {
        (void)cs; (void)speed;
        struct stat st;
        if (stat(SDL_SD_ROOT, &st) != 0) {
            system("mkdir -p " SDL_SD_ROOT "/apps "
                              SDL_SD_ROOT "/cyber_logs "
                              SDL_SD_ROOT "/roms/nes "
                              SDL_SD_ROOT "/roms/gb "
                              SDL_SD_ROOT "/payloads");
        }
        printf("[SD] SdFat → host filesystem at %s\n", SDL_SD_ROOT);
        return true;
    }

    bool exists(const char* path) {
        struct stat st;
        return stat(sd_host_path(path).c_str(), &st) == 0;
    }

    bool mkdir(const char* path) {
        std::string cmd = "mkdir -p \"" + sd_host_path(path) + "\"";
        return system(cmd.c_str()) == 0;
    }

    bool remove(const char* path) {
        return ::remove(sd_host_path(path).c_str()) == 0;
    }

    bool rename(const char* from, const char* to) {
        return ::rename(sd_host_path(from).c_str(),
                        sd_host_path(to).c_str()) == 0;
    }

    SdFile open(const char* path, uint8_t flags = O_READ) {
        SdFile f;
        f.open(path, flags);
        return f;
    }

    uint64_t totalBytes() { return (uint64_t)16 * 1024 * 1024 * 1024; }  // 16GB
    uint64_t usedBytes()  { return 0; }
};

// ─────────────────────────────────────────────
//  SdSpiConfig stub — used in ghost_partition.cpp
// ─────────────────────────────────────────────
struct SdSpiConfig {
    SdSpiConfig(uint8_t, uint8_t, uint32_t, void* = nullptr) {}
};
#define DEDICATED_SPI 0
#define SD_SCK_MHZ(x) 0

#endif // PISCES_SDL
#endif // SDFAT_SDL2_H
