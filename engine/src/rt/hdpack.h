/*
 * Graphics packs: replace what the game looks like without touching the game.
 *
 * Every 8x8 tile the VDP displays is identified by its appearance: the 64
 * colours it shows with the current palette (sprite colour 0 = transparent).
 * A pack maps these keys to replacement images, `scale` times larger and in
 * full colour, and the renderer draws them instead of the original pixels.
 * Layering, scrolling and priorities stay exactly those of the Master System.
 *
 * Pack directory: pack.txt plus PNG sheets.
 *   scale 4                     replacement size = 8*scale pixels
 *   sheet level01.png           following tiles refer to this sheet
 *   tile COL ROW KEY            cell (COL, ROW) replaces the tile whose appearance is KEY
 *   tilep COL ROW KEY CONTEXT   cell replaces the tile whose pattern is KEY, whatever its
 *                               colours, in CONTEXT (palette-cycling logos, fades, water...)
 * Keys are 16 hex digits. Contexts: 1-17 = levels, 100 title, 101 world map, 102 pause
 * map, 103 shop (see hdpack_context). Appearance keys take precedence.
 * engine/build/tileharvest writes a reference pack of the original graphics.
 */
#ifndef RT_HDPACK_H
#define RT_HDPACK_H

#include <stdbool.h>
#include <stdint.h>

#include "vdp.h"

typedef struct HdPack HdPack;

HdPack *hdpack_load(const char *dir);
void hdpack_free(HdPack *p);
int hdpack_scale(const HdPack *p);
int hdpack_tile_count(const HdPack *p);

/* Appearance key of VRAM tile `tile` drawn with palette half `half` (0 background, 1 sprites). */
uint64_t hdpack_tile_key(const Vdp *v, int tile, int half);
/* Pattern key: the tile's pixels as palette indexes (and the palette half), not colours. */
uint64_t hdpack_pattern_key(const Vdp *v, int tile, int half);

/* Context of the current screen from game RAM ($C000-$DFFF): see pack.txt contexts. */
int hdpack_context(const uint8_t *ram);

/* Renders the frame at pack scale (VDP_WIDTH*scale x VDP_HEIGHT*scale, XRGB8888).
 * With p == NULL, renders the original graphics at `scale`. */
void hdpack_render(const Vdp *v, const HdPack *p, int context, int scale, uint32_t *out);

#endif
