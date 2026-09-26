/* SDL2 front end: window, input, audio and 60 Hz pacing. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/hdpack.h"
#include "rt/mod.h"
#include "rt/runtime.h"

#define SCALE 3
#define AUDIO_RATE 44100
#define FRAME_RATE 60

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio_dev;
static SDL_GameController *pad;
static uint32_t *framebuffer;
static HdPack *pack;         /* --pack DIR: replacement graphics */
static int render_scale = 1;
static uint64_t next_frame_time;
static bool fast_forward;
static bool reset_pressed;
static int start_frames;        /* Enter on the title screen: hold button 1 briefly */
static long frame_count;
static const char *snapshot_path; /* ALEXKIDD_SNAPSHOT: save the displayed image once */
static int start_level = -1;     /* --level N: start the game directly at level N */
static bool autostart;

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint8_t *load_rom(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

static uint8_t pad_state(void) {
    uint8_t j = 0;
    const uint8_t *k = SDL_GetKeyboardState(NULL);
    /* Letters follow the active layout (AZERTY, QWERTY...), not the physical position. */
#define KEY(sym) k[SDL_GetScancodeFromKey(sym)]
    if (k[SDL_SCANCODE_UP]) j |= JOY_UP;
    if (k[SDL_SCANCODE_DOWN]) j |= JOY_DOWN;
    if (k[SDL_SCANCODE_LEFT]) j |= JOY_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) j |= JOY_RIGHT;
    /* In Alex Kidd, button 1 jumps and button 2 punches / uses the power-up. */
    if (KEY(SDLK_x) || KEY(SDLK_k) || k[SDL_SCANCODE_SPACE]) j |= JOY_BTN1;
    if (KEY(SDLK_w) || KEY(SDLK_z) || KEY(SDLK_j) || k[SDL_SCANCODE_LCTRL]) j |= JOY_BTN2;
#undef KEY
    if (start_frames > 0) {
        start_frames--;
        j |= JOY_BTN1;
    }
    if (pad) {
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) j |= JOY_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) j |= JOY_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) j |= JOY_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) j |= JOY_RIGHT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) j |= JOY_BTN1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) j |= JOY_BTN2;
        int16_t ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) j |= JOY_LEFT;
        if (ax > 16000) j |= JOY_RIGHT;
        if (ay < -16000) j |= JOY_UP;
        if (ay > 16000) j |= JOY_DOWN;
    }
    return j;
}

/* Enter / Start: starts the game on the title screen and demo, pauses in game. */
static void press_start(void) {
    uint8_t state = mach.ram[0x001F] & 0x0F; /* v_gameState */
    if (state <= 2) start_frames = 4;
    else rt_nmi_pending = 1;
}

/* The console masks the leftmost 8-pixel column with a plain colour (VDP
 * register 0 bit 5, set by the game): it is left out of the picture. */
#define HIDDEN_LEFT 8
#define SHOWN_WIDTH (VDP_WIDTH - HIDDEN_LEFT)

static void present(void) {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    double scale = (double)w / SHOWN_WIDTH < (double)h / VDP_HEIGHT ? (double)w / SHOWN_WIDTH : (double)h / VDP_HEIGHT;
    if (scale >= 2.0 && render_scale == 1) scale = (int)scale; /* integer scaling keeps pixels sharp */
    SDL_Rect src = { HIDDEN_LEFT * render_scale, 0, SHOWN_WIDTH * render_scale, VDP_HEIGHT * render_scale };
    SDL_Rect dst;
    dst.w = (int)(SHOWN_WIDTH * scale);
    dst.h = (int)(VDP_HEIGHT * scale);
    dst.x = (w - dst.w) / 2;
    dst.y = (h - dst.h) / 2;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src, &dst);
    if (snapshot_path && frame_count == 240) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) {
            SDL_SaveBMP(surf, snapshot_path);
            fprintf(stderr, "snapshot %dx%d (image %dx%d at %d,%d) saved to %s\n", w, h, dst.w, dst.h, dst.x, dst.y, snapshot_path);
        }
        SDL_FreeSurface(surf);
    }
    SDL_RenderPresent(renderer);
}

static void toggle_fullscreen(void) {
    bool full = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
}

/* --single-level: plays one level only (as the Maker does): lives never run
 * out, and the game closes a moment after the level is completed. */
static bool single_level;
static long cleared_at;

static void single_level_frame(void) {
    uint8_t state = mach.ram[0x001F] & 0x0F; /* v_gameState */
    if (start_level > 0 || !autostart) return; /* not in the level yet */
    if (!cleared_at) mach.ram[0x0025] = 3;     /* v_lives: endless tries */
    if (state == 4 && !cleared_at) cleared_at = frame_count;
    if (cleared_at && (frame_count - cleared_at > 150 || state != 4)) rt_quit();
}

static void frame(void) {
    SDL_Event ev;
    frame_count++;
    if (single_level) single_level_frame();
    if (start_level > 0) {
        uint8_t state = mach.ram[0x001F] & 0x0F; /* v_gameState */
        if (state <= 2 && !autostart && frame_count > 30) {
            start_frames = 4; /* press a button on the title screen */
            autostart = true;
        }
        if (state == 3 && autostart) {
            mach.ram[0x0023] = (uint8_t)start_level; /* v_level */
            start_level = -1;
        }
    }
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            rt_quit();
        case SDL_KEYDOWN:
            if (ev.key.repeat) break;
            if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) rt_quit();
            if (ev.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                ev.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) press_start();
            if (ev.key.keysym.sym == SDLK_p) rt_nmi_pending = 1;
            if (ev.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) reset_pressed = true;
            if (ev.key.keysym.scancode == SDL_SCANCODE_TAB) fast_forward = true;
            if (ev.key.keysym.scancode == SDL_SCANCODE_F11 ||
                (ev.key.keysym.scancode == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT)))
                toggle_fullscreen();
            break;
        case SDL_KEYUP:
            if (ev.key.keysym.scancode == SDL_SCANCODE_TAB) fast_forward = false;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) press_start();
            /* Select / Back quits: with only a gamepad in hand, there is no Escape key. */
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) rt_quit();
            break;
        default:
            break;
        }
    }
    mach.joy = pad_state();
    mach.reset_button = reset_pressed;
    reset_pressed = false;

    hdpack_render(&mach.vdp, pack, hdpack_context(mach.ram), render_scale, framebuffer);
    SDL_UpdateTexture(texture, NULL, framebuffer, VDP_WIDTH * render_scale * 4);
    present();

    int16_t samples[AUDIO_RATE / FRAME_RATE];
    psg_render(&mach.psg, samples, AUDIO_RATE / FRAME_RATE, AUDIO_RATE);
    if (audio_dev && !fast_forward && SDL_GetQueuedAudioSize(audio_dev) < sizeof(samples) * 6)
        SDL_QueueAudio(audio_dev, samples, sizeof(samples));

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t now = SDL_GetPerformanceCounter();
    if (fast_forward) {
        next_frame_time = now;
        return;
    }
    next_frame_time += freq / FRAME_RATE;
    if (next_frame_time > now) {
        uint64_t ms = (next_frame_time - now) * 1000 / freq;
        if (ms > 1) SDL_Delay((uint32_t)(ms - 1));
        while (SDL_GetPerformanceCounter() < next_frame_time) {
        }
    } else if (now - next_frame_time > freq / 10) {
        next_frame_time = now; /* fell far behind: resynchronise */
    }
}

int main(int argc, char **argv) {
    const char *rom_path = "original.sms";
    const char *mod_path = NULL, *pack_path = NULL;
    bool fullscreen = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen")) fullscreen = true;
        else if (!strcmp(argv[i], "--single-level")) single_level = true;
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) start_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mod") && i + 1 < argc) mod_path = argv[++i];
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_path = argv[++i];
        else rom_path = argv[i];
    }
    if (pack_path) {
        pack = hdpack_load(pack_path);
        if (!pack) return 1;
        render_scale = hdpack_scale(pack);
    }
    framebuffer = calloc((size_t)(VDP_WIDTH * render_scale * VDP_HEIGHT * render_scale), sizeof(uint32_t));
    uint32_t rom_size = 0;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) {
        fprintf(stderr, "cannot read ROM '%s'. Usage: %s path/to/original.sms\n", rom_path, argv[0]);
        return 1;
    }
    if (rom_size != 0x20000 || crc32(rom, rom_size) != 0x17A40E29u) {
        fprintf(stderr, "this port needs Alex Kidd in Miracle World (USA, Europe) rev 0 (CRC32 17A40E29)\n");
        return 1;
    }
    if (mod_path) {
        uint32_t patched_size;
        uint8_t *patched = mod_apply(rom, rom_size, mod_path, &patched_size);
        if (!patched) return 1;
        free(rom);
        rom = patched;
        rom_size = patched_size;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow("Alex Kidd", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              VDP_WIDTH * SCALE, VDP_HEIGHT * SCALE,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (fullscreen) toggle_fullscreen();
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RaiseWindow(window);
    snapshot_path = getenv("ALEXKIDD_SNAPSHOT");
    printf("Controles : fleches = deplacement, Entree = demarrer / pause (carte et objets),\n"
           "           Espace, X ou K = sauter, Z, W ou J = coup de poing / utiliser l'objet,\n"
           "           Tab = accelere, F11 = plein ecran, Echap (ou Select a la manette) = quitter\n"
           "Objets : Entree, fleche sur l'objet, Z pour l'equiper, puis Entree pour reprendre.\n");
    fflush(stdout);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                VDP_WIDTH * render_scale, VDP_HEIGHT * render_scale);

    SDL_AudioSpec want = {0}, have;
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);

    machine_init(&mach, rom, rom_size);
    memset(&cpu, 0, sizeof(cpu));
    rt_frame_hook = frame;
    next_frame_time = SDL_GetPerformanceCounter();
    rt_run();

    if (pad) SDL_GameControllerClose(pad);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    hdpack_free(pack);
    free(framebuffer);
    free(rom);
    return 0;
}
