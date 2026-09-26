/*
 * WebAssembly tools module for the level editor (no Asyncify: every call runs
 * to completion). maker/public/js/capture-worker.js calls it from a Web
 * Worker so that the page never freezes:
 *   web_tools_set_rom(ptr, size)     the user's ROM (copied)
 *   web_level_video(level)           -> pointer to 32 bytes of CRAM followed by
 *                                       16 KB of VRAM once LEVEL is playable,
 *                                       or 0 if it never became playable
 *   web_entity_icon(type, level)     -> pointer to int32 {w, h, dx, dy} followed
 *                                       by w*h RGBA pixels, or 0 if nothing
 *                                       showed up; valid until the next call
 */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "../../tools/capture.h"

static uint8_t *rom;
static uint32_t rom_size;

EMSCRIPTEN_KEEPALIVE void web_tools_set_rom(const uint8_t *data, uint32_t size) {
    free(rom);
    rom = malloc(size);
    memcpy(rom, data, size);
    rom_size = size;
}

EMSCRIPTEN_KEEPALIVE uint8_t *web_level_video(int level) {
    static LevelVideo v;
    static uint8_t out[32 + 0x4000];
    if (!rom || !capture_level_video(rom, rom_size, level, 30, &v)) return 0;
    memcpy(out, v.cram, 32);
    memcpy(out + 32, v.vram, 0x4000);
    return out;
}

EMSCRIPTEN_KEEPALIVE int32_t *web_entity_icon(int type, int level) {
    static int32_t *out;
    free(out);
    out = 0;
    EntityIcon icon;
    if (!rom || !capture_entity_icon(rom, rom_size, type, level, &icon)) return 0;
    size_t pixels = (size_t)icon.w * (size_t)icon.h * 4;
    out = malloc(16 + pixels);
    out[0] = icon.w;
    out[1] = icon.h;
    out[2] = icon.dx;
    out[3] = icon.dy;
    memcpy(out + 4, icon.rgba, pixels);
    free(icon.rgba);
    return out;
}

int main(void) { return 0; }
