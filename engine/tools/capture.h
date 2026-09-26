/*
 * Captures for the level editor, shared by the command-line tools
 * (leveldump, entityicons) and the WebAssembly tools module (tools_web.c).
 * Each capture runs the game headless and deterministic from power-on.
 */
#ifndef TOOLS_CAPTURE_H
#define TOOLS_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "rt/runtime.h"

/* Video state once LEVEL is playable (after `settle` gameplay frames). */
typedef struct {
    long frame;
    int slot2;
    uint8_t vdp_regs[11];
    uint8_t cram[32];
    uint8_t vram[0x4000];
    uint8_t ram[0x2000];
} LevelVideo;

bool capture_level_video(const uint8_t *rom, uint32_t rom_size, int level, long settle, LevelVideo *out);

/* What an entity of TYPE looks like a few frames after appearing in LEVEL:
 * a w x h RGBA image (malloc'ed, caller frees) whose top-left corner is at
 * (dx, dy) from the entity's position. */
typedef struct {
    int w, h, dx, dy;
    uint8_t *rgba;
} EntityIcon;

bool capture_entity_icon(const uint8_t *rom, uint32_t rom_size, int type, int level, EntityIcon *out);

#endif
