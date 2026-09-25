/*
 * Mods: patches applied to an in-memory copy of the user's ROM at startup.
 *
 * File format (little endian), written by maker/tools/levels.py:
 *   "AKMOD1\0\0"                 8-byte magic
 *   u32 rom_size                 size of the patched ROM (power of two, original..2 MB;
 *                                bank numbers are written as $80|bank by the game)
 *   repeated until end of file:
 *     u32 offset, u32 length, length bytes   bytes to write at ROM offset
 * The ROM is extended with $FF up to rom_size before patches are applied, so a
 * mod can add banks for new content. Mods never contain the original data.
 */
#ifndef RT_MOD_H
#define RT_MOD_H

#include <stdint.h>

/* Returns a newly allocated patched ROM (and its size), or NULL with a message. */
uint8_t *mod_apply(const uint8_t *rom, uint32_t rom_size, const char *patch_path, uint32_t *out_size);
/* Same, from a patch already in memory. */
uint8_t *mod_apply_bytes(const uint8_t *rom, uint32_t rom_size, const uint8_t *patch, long patch_size,
                         uint32_t *out_size);

#endif
