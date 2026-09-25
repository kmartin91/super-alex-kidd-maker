#include "scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/mod.h"
#include "rt/runtime.h"

long sc_max_frames = 3000;
uint32_t sc_seed = 1;
const char *sc_mode = "play";
int sc_start_level = -1;
const char *sc_shot_dir;
int sc_shot_every = 60;

static bool infinite_lives;
static const char *mod_path;
static bool level_poked;
static uint32_t input_rng;
static uint8_t held_input;
static int held_frames;

typedef struct ScriptCmd {
    long frame;
    char cmd[8];
    char arg[16];
    unsigned a, v;
} ScriptCmd;
static ScriptCmd script[1024];
static int script_len, script_pos;
static uint8_t script_keys;

static void load_script(const char *path) {
    FILE *sf = fopen(path, "r");
    if (!sf) {
        fprintf(stderr, "cannot read script %s\n", path);
        exit(2);
    }
    char line[128];
    while (fgets(line, sizeof(line), sf) && script_len < 1024) {
        ScriptCmd *c = &script[script_len];
        memset(c, 0, sizeof(*c));
        if (line[0] == '#') continue;
        int n = sscanf(line, "%ld %7s %15s", &c->frame, c->cmd, c->arg);
        if (n < 2) continue;
        if (!strcmp(c->cmd, "poke") || !strcmp(c->cmd, "peek"))
            sscanf(line, "%ld %7s %x %x", &c->frame, c->cmd, &c->a, &c->v);
        script_len++;
    }
    fclose(sf);
    sc_mode = "script";
}

bool scenario_parse_arg(int argc, char **argv, int *i) {
    const char *a = argv[*i];
    if (!strcmp(a, "--lives")) {
        infinite_lives = true;
        return true;
    }
    if (*i + 1 >= argc) return false;
    const char *v = argv[*i + 1];
    if (!strcmp(a, "--level")) sc_start_level = atoi(v);
    else if (!strcmp(a, "--frames")) sc_max_frames = atol(v);
    else if (!strcmp(a, "--seed")) sc_seed = (uint32_t)atol(v);
    else if (!strcmp(a, "--mode")) sc_mode = v;
    else if (!strcmp(a, "--shots")) sc_shot_dir = v;
    else if (!strcmp(a, "--every")) sc_shot_every = atoi(v);
    else if (!strcmp(a, "--script")) load_script(v);
    else if (!strcmp(a, "--mod")) mod_path = v;
    else return false;
    (*i)++;
    return true;
}

void scenario_init(void) {
    input_rng = sc_seed * 2654435761u + 1;
}

static uint32_t next_rand(void) {
    input_rng ^= input_rng << 13;
    input_rng ^= input_rng >> 17;
    input_rng ^= input_rng << 5;
    return input_rng;
}

void scenario_write_shot(long frame) {
    static uint32_t px[VDP_WIDTH * VDP_HEIGHT];
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%06ld.ppm", sc_shot_dir ? sc_shot_dir : ".", frame);
    vdp_render(&mach.vdp, px);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", VDP_WIDTH, VDP_HEIGHT);
    for (int i = 0; i < VDP_WIDTH * VDP_HEIGHT; i++) {
        uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void run_script(long frame, void (*poke)(uint16_t, uint8_t), uint8_t *joy, bool *nmi) {
    while (script_pos < script_len && script[script_pos].frame <= frame) {
        ScriptCmd *c = &script[script_pos++];
        if (!strcmp(c->cmd, "keys")) {
            script_keys = 0;
            for (const char *k = c->arg; *k; k++) {
                switch (*k) {
                case 'U': script_keys |= JOY_UP; break;
                case 'D': script_keys |= JOY_DOWN; break;
                case 'L': script_keys |= JOY_LEFT; break;
                case 'R': script_keys |= JOY_RIGHT; break;
                case '1': script_keys |= JOY_BTN1; break;
                case '2': script_keys |= JOY_BTN2; break;
                default: break;
                }
            }
        } else if (!strcmp(c->cmd, "pause")) {
            *nmi = true;
        } else if (!strcmp(c->cmd, "poke")) {
            poke((uint16_t)c->a, (uint8_t)c->v);
        } else if (!strcmp(c->cmd, "shot")) {
            scenario_write_shot(frame);
        } else if (!strcmp(c->cmd, "peek")) {
            printf("frame %ld peek %04X:", frame, c->a);
            for (unsigned i = 0; i < c->v; i++) printf(" %02X", rd8((uint16_t)(c->a + i)));
            printf("\n");
        }
    }
    *joy = script_keys;
}

void scenario_frame(long frame, void (*poke)(uint16_t, uint8_t), uint8_t *joy, bool *nmi) {
    *nmi = false;
    uint8_t state = mach.ram[0x001F] & 0x0F; /* v_gameState */
    if (sc_start_level >= 0 && !level_poked && state == 3) {
        poke(0xC023, (uint8_t)sc_start_level);
        level_poked = true;
    }
    if (state < 3) level_poked = false;
    if (infinite_lives && state >= 3) poke(0xC025, 3);
    if (sc_shot_dir && sc_shot_every > 0 && frame % sc_shot_every == 0) scenario_write_shot(frame);

    if (!strcmp(sc_mode, "script")) {
        run_script(frame, poke, joy, nmi);
        return;
    }
    if (!strcmp(sc_mode, "idle")) {
        *joy = 0;
        return;
    }
    if (frame < 200) {
        *joy = (frame % 40) < 5 ? JOY_BTN1 : 0;
        return;
    }
    if (held_frames-- <= 0) {
        uint32_t r = next_rand();
        static const uint8_t moves[] = {0, JOY_RIGHT, JOY_RIGHT, JOY_RIGHT, JOY_LEFT, JOY_UP, JOY_DOWN,
                                        JOY_RIGHT | JOY_DOWN, JOY_LEFT | JOY_DOWN};
        held_input = moves[r % sizeof(moves)];
        if (r & 0x100) held_input |= JOY_BTN2;
        if (r & 0x200) held_input |= JOY_BTN1;
        held_frames = 2 + (int)((r >> 12) % 40);
        if ((r >> 24) % 97 == 0) *nmi = true;
    }
    *joy = held_input;
}

uint8_t *scenario_load_rom(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(b);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    if (mod_path) {
        uint32_t patched_size;
        uint8_t *patched = mod_apply(b, *size, mod_path, &patched_size);
        if (!patched) exit(2);
        free(b);
        b = patched;
        *size = patched_size;
    }
    return b;
}
