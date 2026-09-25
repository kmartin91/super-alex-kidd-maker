#include "mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t *mod_apply(const uint8_t *rom, uint32_t rom_size, const char *patch_path, uint32_t *out_size) {
    FILE *f = fopen(patch_path, "rb");
    if (!f) {
        fprintf(stderr, "mod: cannot read %s\n", patch_path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    uint8_t *out = mod_apply_bytes(rom, rom_size, data, n, out_size);
    if (out) fprintf(stderr, "mod: applied %s\n", patch_path);
    free(data);
    return out;
}

uint8_t *mod_apply_bytes(const uint8_t *rom, uint32_t rom_size, const uint8_t *data, long n, uint32_t *out_size) {
    if (n < 12 || memcmp(data, "AKMOD1\0\0", 8) != 0) {
        fprintf(stderr, "mod: not a mod patch\n");
        return NULL;
    }
    uint32_t size = read_u32(data + 8);
    if (size < rom_size || (size & (size - 1)) || size > 2u << 20) {
        fprintf(stderr, "mod: invalid ROM size %u\n", size);
        return NULL;
    }
    uint8_t *out = malloc(size);
    memset(out, 0xFF, size);
    memcpy(out, rom, rom_size);
    long pos = 12;
    int records = 0;
    while (pos + 8 <= n) {
        uint32_t off = read_u32(data + pos), len = read_u32(data + pos + 4);
        pos += 8;
        if (pos + (long)len > n || (uint64_t)off + len > size) {
            fprintf(stderr, "mod: record %d out of range\n", records);
            free(out);
            return NULL;
        }
        memcpy(out + off, data + pos, len);
        pos += len;
        records++;
    }
    fprintf(stderr, "mod: %d patch record(s), ROM %u KB\n", records, size / 1024);
    *out_size = size;
    return out;
}
