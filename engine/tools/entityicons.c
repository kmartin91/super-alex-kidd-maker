/*
 * entityicons: captures what each entity type looks like, for the level editor.
 *
 * For every TYPE:LEVEL argument the game runs twice, headless and deterministic,
 * to a few frames into LEVEL: once with an entity of TYPE placed on screen and
 * once without. The sprite pixels that differ are the entity's appearance.
 *
 * usage: entityicons ROM OUT.json TYPE:LEVEL [TYPE:LEVEL...]
 * Output: {"<type>": {"w", "h", "dx", "dy", "rgba": base64}}; (dx, dy) is the
 * top-left corner of the image relative to the entity's position.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"

static void b64(FILE *f, const uint8_t *p, size_t n) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        fputc(tab[(v >> 18) & 63], f);
        fputc(tab[(v >> 12) & 63], f);
        fputc(i + 1 < n ? tab[(v >> 6) & 63] : '=', f);
        fputc(i + 2 < n ? tab[v & 63] : '=', f);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s ROM OUT.json TYPE:LEVEL...\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t rom[0x20000];
    uint32_t rom_size = (uint32_t)fread(rom, 1, sizeof(rom), f);
    fclose(f);
    FILE *out = fopen(argv[2], "w");
    if (!out) return 2;
    fprintf(out, "{");
    int written = 0;
    for (int a = 3; a < argc; a++) {
        int type, level;
        if (sscanf(argv[a], "%i:%i", &type, &level) != 2) continue;
        EntityIcon icon;
        if (!capture_entity_icon(rom, rom_size, type, level, &icon)) {
            fprintf(stderr, "type $%02X: nothing visible in level %d\n", type, level);
            continue;
        }
        fprintf(out, "%s\n \"%d\": {\"w\": %d, \"h\": %d, \"dx\": %d, \"dy\": %d, \"rgba\": \"", written ? "," : "",
                type, icon.w, icon.h, icon.dx, icon.dy);
        b64(out, icon.rgba, (size_t)(icon.w * icon.h * 4));
        fprintf(out, "\"}");
        free(icon.rgba);
        written++;
    }
    fprintf(out, "\n}\n");
    fclose(out);
    fprintf(stderr, "%d icon(s) written to %s\n", written, argv[2]);
    return 0;
}
