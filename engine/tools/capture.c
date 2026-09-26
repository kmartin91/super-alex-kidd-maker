#include "capture.h"

#include "game/ram.h"
#include "rt/maker.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ level video */

static struct {
    int level;
    long settle, frame_no, gameplay_frames;
    bool poked, done;
    LevelVideo *out;
} lv;

static void level_frame(void) {
    lv.frame_no++;
    uint8_t state = mach.ram[0x001F]; /* v_gameState */
    /* Start a game from the title screen, then pick the level. */
    mach.joy = (lv.frame_no > 60 && lv.frame_no < 66) ? JOY_BTN1 : 0;
    if (!lv.poked && (state & 0x0F) == 3) {
        mach.ram[0x0023] = (uint8_t)lv.level; /* v_level */
        lv.poked = true;
    }
    if (lv.poked && state == 0x8A) lv.gameplay_frames++;
    if (lv.gameplay_frames >= lv.settle) {
        LevelVideo *o = lv.out;
        o->frame = lv.frame_no;
        o->slot2 = mach.slot[2];
        memcpy(o->vdp_regs, mach.vdp.reg, sizeof(o->vdp_regs));
        memcpy(o->cram, mach.vdp.cram, sizeof(o->cram));
        memcpy(o->vram, mach.vdp.vram, sizeof(o->vram));
        memcpy(o->ram, mach.ram, sizeof(o->ram));
        lv.done = true;
        rt_quit();
    }
    if (lv.frame_no > 20000) rt_quit(); /* never became playable */
}

bool capture_level_video(const uint8_t *rom, uint32_t rom_size, int level, long settle, LevelVideo *out) {
    memset(&lv, 0, sizeof(lv));
    lv.level = level;
    lv.settle = settle;
    lv.out = out;
    machine_init(&mach, (uint8_t *)rom, rom_size);
    memset(&cpu, 0, sizeof(cpu));
    rt_frame_hook = level_frame;
    rt_run();
    return lv.done;
}

/* ------------------------------------------------------------ entity icons */

#define SPAWN_SLOT 15          /* last normal entity slot (0-based) */
#define OPPONENT_SLOT 5        /* janken opponents and bosses (0-based) */
#define SPAWN_X 0x80
#define SPAWN_Y 0x70
#define OPPONENT_X 0xB0
#define OPPONENT_Y 0x70
#define SETTLE_FRAMES 40       /* gameplay frames before spawning */
#define WATCH_FRAMES 6         /* frames after spawning */
#define OPPONENT_WATCH_FRAMES 150 /* the camera stops, Alex walks into place, the opponent shows */

/* Janken opponents are placed as for a match, with their settings ($84
 * record data, docs/notes/enemies2.md); 0 = an ordinary entity. */
static int opponent_data(int type) {
    switch (type) {
    case 0x1C: return 1; /* Janken */
    case 0x1D: return 2; /* Gooseka */
    case 0x1E: return 4; /* Chokkinna */
    case 0x1F: return 6; /* Parplin */
    default: return 0;
    }
}

static struct {
    int level, type, data, slot;
    bool poked;
    long frame_no, gameplay_frames, watch;
    int ent_x, ent_y;
    bool done;
} ic;
static uint32_t sprites[VDP_WIDTH * VDP_HEIGHT]; /* 0 = transparent */

/* Draws the sprites of the RAM sprite table that belong to the traced
 * entities (rt/maker.h), lower index on top as on the console. */
static void render_traced_sprites(uint32_t *out) {
    const Vdp *v = &mach.vdp;
    const uint8_t *r = v->reg;
    uint16_t base = (r[6] & 0x04) ? 0x2000 : 0;
    int h = (r[1] & 0x02) ? 16 : 8;
    memset(out, 0, sizeof(uint32_t) * VDP_WIDTH * VDP_HEIGHT);
    int end = mach.ram[0x0009] & 0x3F; /* low byte of v_spriteTerminatorPointer */
    for (int i = end - 1; i >= 0; i--) {
        if (!maker.ram_traced[i]) continue;
        int sy = mach.ram[0x0700 + i] + 1;
        if (sy > 240) sy -= 256;
        int sx = mach.ram[0x0780 + i * 2] - ((r[0] & 0x08) ? 8 : 0);
        int n = mach.ram[0x0781 + i * 2];
        if (h == 16) n &= 0xFE;
        for (int y = 0; y < h; y++) {
            int py = sy + y;
            if (py < 0 || py >= VDP_HEIGHT) continue;
            const uint8_t *row = &v->vram[(base + n * 32 + y * 4) & 0x3FFF];
            for (int x = 0; x < 8; x++) {
                int px = sx + x;
                if (px < 0 || px >= VDP_WIDTH) continue;
                int bit = 7 - x;
                int c = ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
                        (((row[3] >> bit) & 1) << 3);
                if (c) out[py * VDP_WIDTH + px] = vdp_color(v->cram[16 + c]);
            }
        }
    }
}

static void icon_frame(void) {
    ic.frame_no++;
    uint8_t state = mach.ram[0x001F];
    mach.joy = (ic.frame_no > 60 && ic.frame_no < 66) ? JOY_BTN1 : 0;
    if (!ic.poked && (state & 0x0F) == 3) {
        mach.ram[0x0023] = (uint8_t)ic.level;
        mach.ram[0x0025] = 3;
        ic.poked = true;
    }
    if (ic.poked && (state == 0x8A || (ic.data && ic.gameplay_frames >= SETTLE_FRAMES))) ic.gameplay_frames++;
    if (ic.gameplay_frames == SETTLE_FRAMES && !ic.slot) {
        ic.slot = ic.data ? OPPONENT_SLOT : SPAWN_SLOT;
        uint8_t *e = &mach.ram[0x0300 + ic.slot * 0x20];
        memset(e, 0, 0x20);
        e[0x00] = (uint8_t)ic.type;
        /* As the entity loader does ($6F9F): x is relative to v_horizontalScroll ($C0AF). */
        e[0x0B] = mach.ram[0x00AF];
        e[0x0C] = (uint8_t)((ic.data ? OPPONENT_X : SPAWN_X) + mach.ram[0x00B0]);
        e[0x0E] = ic.data ? OPPONENT_Y : SPAWN_Y;
        ((Entity *)e)->data = (uint8_t)ic.data;
        maker.traced[ic.slot] = true;
    }
    /* An opponent's match starts once the camera stops (updateBattleMake-
     * AlexGetIntoPosition), as maker mode does when one comes into view. */
    if (ic.data && ic.slot) mach.ram[0x00C9] &= (uint8_t)~0x0F; /* v_scrollFlags */
    if (ic.gameplay_frames == SETTLE_FRAMES + ic.watch) {
        render_traced_sprites(sprites);
        const uint8_t *e = &mach.ram[0x0300 + ic.slot * 0x20];
        ic.ent_x = e[0x0C] - mach.ram[0x00B0];
        ic.ent_y = e[0x0E];
        ic.done = true;
        rt_quit();
    }
    if (ic.frame_no > 20000) rt_quit();
}

/* What an entity looks like: the game runs, headless and deterministic, to a
 * few frames into its level, the entity is placed on screen, and the sprites
 * it and the entities it creates add are drawn (rt/maker.h tracing). */
bool capture_entity_icon(const uint8_t *rom, uint32_t rom_size, int type, int level, EntityIcon *out) {
    memset(&ic, 0, sizeof(ic));
    ic.level = level;
    ic.type = type;
    ic.data = opponent_data(type);
    ic.watch = ic.data ? OPPONENT_WATCH_FRAMES : WATCH_FRAMES;
    machine_init(&mach, (uint8_t *)rom, rom_size);
    memset(&cpu, 0, sizeof(cpu));
    maker.trace = true;
    rt_frame_hook = icon_frame;
    rt_run();
    maker.trace = false;
    if (!ic.done) return false;
    int x0 = VDP_WIDTH, y0 = VDP_HEIGHT, x1 = -1, y1 = -1;
    for (int y = 0; y < VDP_HEIGHT; y++)
        for (int x = 0; x < VDP_WIDTH; x++)
            if (sprites[y * VDP_WIDTH + x]) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return false; /* nothing visible */
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    uint8_t *rgba = calloc((size_t)(w * h), 4);
    if (!rgba) return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = sprites[(y0 + y) * VDP_WIDTH + x0 + x];
            if (!c) continue;
            uint8_t *p = &rgba[(y * w + x) * 4];
            p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; p[3] = 255;
        }
    *out = (EntityIcon){ w, h, x0 - ic.ent_x, y0 - ic.ent_y, rgba };
    return true;
}
