/*
 * WebAssembly front end (Emscripten + Asyncify), used by the level editor to
 * play a level inside the page.
 *
 * JavaScript (maker/public/js/player.js) owns the canvas, keyboard and audio:
 *   web_set_rom(ptr, size)          the user's ROM (kept for later runs)
 *   web_play(patch_ptr, size, level) async: runs the game with an optional mod
 *                                   patch, fast-forwarding the title screen and
 *                                   level intro until LEVEL is playable
 *   web_stop()                      ends the current run at the next frame
 *   web_set_joy(bits), web_press_pause()
 *   web_set_refresh(hz)             60 (Japan, USA) or 50 (Europe) frames a second
 *   web_ram()                       the game's RAM (for tests)
 *   web_music_start(level)          music only (the Maker's menu): the song
 *   web_music_frame()               of LEVEL, one frame of samples per call
 * A run plays one level only, like a level of the Maker: lives never run out,
 * and the run ends a moment after the level is completed (status "cleared").
 * Every displayed frame calls Module.present(pixels, samples, count) and then
 * awaits Module.waitFrame(), which resolves at the next 60 Hz tick.
 */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/hdpack.h"
#include "rt/mod.h"
#include "rt/runtime.h"

#define AUDIO_RATE 44100
#define MAX_SAMPLES_PER_FRAME (AUDIO_RATE / 50)
#define BOOT_FRAME_LIMIT 3000

static uint8_t *base_rom;
static uint32_t base_rom_size;
static uint8_t *run_rom;
static uint32_t framebuffer[VDP_WIDTH * VDP_HEIGHT];
static int16_t samples[MAX_SAMPLES_PER_FRAME];
static int samples_per_frame = AUDIO_RATE / 60;
static double psg_clock = PSG_CLOCK_NTSC;

static uint8_t joy;
static int pause_requested;
static int stop_requested;
static int target_level;
static long frame_no;
static int level_set;
static int booting; /* fast-forward until the level is playable */
static long cleared_at; /* frame the level was completed, 0 before */
static int game_running; /* web_play is on (the machine is the game's) */

#define CLEARED_FRAMES 150 /* the end of the level stays on screen this long */

EM_ASYNC_JS(void, web_wait_frame, (void), { await Module.waitFrame(); });
EM_JS(void, web_present, (const uint32_t *pixels, const int16_t *audio, int count),
      { Module.present(pixels, audio, count); });
EM_JS(void, web_status, (const char *text), { if (Module.onStatus) Module.onStatus(UTF8ToString(text)); });

static void frame(void) {
    frame_no++;
    if (stop_requested) rt_quit();
    uint8_t state = mach.ram[0x001F]; /* v_gameState */
    if (booting) {
        /* Press a button on the title screen, then pick the level. */
        mach.joy = (frame_no > 30 && frame_no < 36) ? JOY_BTN1 : 0;
        if ((state & 0x0F) == 3 && !level_set) {
            mach.ram[0x0023] = (uint8_t)target_level; /* v_level */
            level_set = 1;
        }
        if (level_set && state == 0x8A) {
            booting = 0;
            web_status("playing");
        } else if (frame_no > BOOT_FRAME_LIMIT) {
            web_status("error: the level never started");
            rt_quit();
        } else {
            psg_render(&mach.psg, samples, samples_per_frame, AUDIO_RATE); /* keep the PSG phase moving */
            return;                                                      /* no display, no wait */
        }
    }
    if (!cleared_at) mach.ram[0x0025] = 3; /* v_lives: endless tries */
    if ((state & 0x0F) == 4 && !cleared_at) cleared_at = frame_no; /* level completed */
    if (cleared_at && (frame_no - cleared_at > CLEARED_FRAMES || (state & 0x0F) != 4)) rt_quit();
    mach.joy = joy;
    if (pause_requested) {
        pause_requested = 0;
        rt_nmi_pending = 1;
    }
    hdpack_render(&mach.vdp, NULL, 0, 1, framebuffer);
    psg_render(&mach.psg, samples, samples_per_frame, AUDIO_RATE);
    web_present(framebuffer, samples, samples_per_frame);
    web_wait_frame();
}

EMSCRIPTEN_KEEPALIVE void web_set_joy(int bits) { joy = (uint8_t)bits; }
EMSCRIPTEN_KEEPALIVE void web_press_pause(void) { pause_requested = 1; }
EMSCRIPTEN_KEEPALIVE void web_stop(void) { stop_requested = 1; }

/* The machine's frames per second: 60 as in Japan and the USA, or 50 as on a
 * European (PAL) Master System, where the game and its music, which step
 * once a frame, run a sixth slower, and the sound chip's clock is a little
 * lower. JavaScript paces the frames; this sets the samples of each frame
 * (returned) and the sound chip's clock, for the next runs. */
EMSCRIPTEN_KEEPALIVE int web_set_refresh(int hz) {
    int pal = hz == 50;
    samples_per_frame = AUDIO_RATE / (pal ? 50 : 60);
    psg_clock = pal ? PSG_CLOCK_PAL : PSG_CLOCK_NTSC;
    return samples_per_frame;
}
/* The game's 8 KB of RAM ($C000-$DFFF), read by the automated tests. */
EMSCRIPTEN_KEEPALIVE uint8_t *web_ram(void) { return mach.ram; }

EMSCRIPTEN_KEEPALIVE int web_set_rom(const uint8_t *data, int size) {
    free(base_rom);
    base_rom = malloc((size_t)size);
    memcpy(base_rom, data, (size_t)size);
    base_rom_size = (uint32_t)size;
    return 1;
}

/* Runs until web_stop(). Returns 0 on a normal stop, -1 on error. */
EMSCRIPTEN_KEEPALIVE int web_play(const uint8_t *patch, int patch_size, int level) {
    if (!base_rom) return -1;
    free(run_rom);
    run_rom = NULL;
    uint32_t size = base_rom_size;
    const uint8_t *rom = base_rom;
    if (patch && patch_size > 0) {
        run_rom = mod_apply_bytes(base_rom, base_rom_size, patch, patch_size, &size);
        if (!run_rom) {
            web_status("error: invalid mod patch");
            return -1;
        }
        rom = run_rom;
    }
    machine_init(&mach, rom, size);
    mach.psg.clock = psg_clock;
    memset(&cpu, 0, sizeof(cpu));
    rt_nmi_pending = 0;
    stop_requested = 0;
    pause_requested = 0;
    joy = 0;
    frame_no = 0;
    level_set = 0;
    cleared_at = 0;
    target_level = level;
    booting = 1;
    web_status("starting");
    rt_frame_hook = frame;
    game_running = 1;
    rt_run();
    game_running = 0;
    web_status(cleared_at ? "cleared" : "stopped");
    return 0;
}

/* Music only, for the Maker's menu: the game's sound engine alone plays the
 * song of LEVEL (levelSongs, $0DC5), the game itself does not run. */
#define LEVEL_SONGS 0x0DC5
#define V_SOUND_CONTROL 0x0110 /* $C110 */
#define SOUND_UPDATE 0x984F    /* called once per frame by the VBlank handler */
#define MUSIC_STACK 0xDFF0

EMSCRIPTEN_KEEPALIVE int web_music_start(int level) {
    if (!base_rom || game_running || level < 1 || LEVEL_SONGS - 1 + level >= (int)base_rom_size) return 0;
    machine_init(&mach, base_rom, base_rom_size); /* bank 2 (the sound engine) in slot 2 */
    mach.psg.clock = psg_clock;
    memset(&cpu, 0, sizeof(cpu));
    mach.ram[V_SOUND_CONTROL] = base_rom[LEVEL_SONGS - 1 + level];
    return 1;
}

/* One frame of the song: its samples, or NULL while a game runs. */
EMSCRIPTEN_KEEPALIVE int16_t *web_music_frame(void) {
    if (game_running) return NULL;
    cpu.sp = MUSIC_STACK - 2; /* as if CALLed: the engine's RET pops it */
    rt_dispatch(SOUND_UPDATE);
    psg_render(&mach.psg, samples, samples_per_frame, AUDIO_RATE);
    return samples;
}

int main(void) { return 0; }
