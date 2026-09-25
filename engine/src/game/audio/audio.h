/*
 * Sound engine (ROM bank 2, $984F-$9ECC): shared definitions.
 *
 * Overview
 * --------
 * The engine runs once per frame from the VBlank interrupt (update, $984F):
 *   1. readSoundRequest: the game asks for a song or an effect by writing
 *      $81 + index to v_soundControl; the engine starts it and writes back $80
 *      ("idle").
 *   2. handleFadeOutAndSfx: steps the music fade-out and keeps the music off
 *      the PSG channel borrowed by SFX channel 3.
 *   3. runChannel for each of the 7 active software channels (4 music
 *      channels at $C118, 3 sound-effect channels at $C198). Each channel is a
 *      tiny sequencer (readChannelInstruction) reading a byte stream of notes,
 *      durations and commands $E0-$F1, then writes its PSG tone and volume.
 *
 * A sound "header" in ROM is: channel count, then 9 bytes per channel copied
 * to the start of a SoftwareChannel (flags, hardwareChannel, duration
 * multiplier, data pointer, transpose, pitch envelope, volume envelope,
 * volume). See docs/notes/audio.md for the full stream format.
 */
#ifndef GAME_WIP_AUDIO_AUDIO_H
#define GAME_WIP_AUDIO_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "game/lift.h"
#include "game/ram.h"

/* ------------------------------------------------------------------ PSG */

#define PSG_PORT 0x7F

/* Latch bytes (SN76489): 1 cc t dddd. cc = channel, t = 1 for volume. */
enum {
    PSG_LATCH = 0x80,
    PSG_TONE0 = PSG_LATCH | 0x00,
    PSG_TONE1 = PSG_LATCH | 0x20,
    PSG_TONE2 = PSG_LATCH | 0x40,
    PSG_NOISE = PSG_LATCH | 0x60,
    PSG_LATCH_VOLUME = 0x10,
    PSG_VOLUME_OFF = 0x0F,          /* attenuation 15 = silent */
    PSG_NOISE_CONTROL = PSG_NOISE,  /* $E0 | feedback << 2 | shift rate */
    PSG_NOISE_RATE_TONE2 = 0x03,    /* noise shift rate taken from tone 2 */
};

/* ------------------------------------------------------ software channels */

enum {
    MUSIC_CHANNEL_1 = 0xC118,  /* v_soundMusicChannels.1 (tone 0 in every song) */
    MUSIC_CHANNEL_2 = 0xC138,  /* tone 1 */
    MUSIC_CHANNEL_3 = 0xC158,  /* tone 2 */
    MUSIC_CHANNEL_4 = 0xC178,  /* noise (drums) */
    SFX_CHANNEL_1 = 0xC198,    /* v_soundEffectsChannels.1: two-voice effects */
    SFX_CHANNEL_2 = 0xC1B8,    /* one-voice effects */
    SFX_CHANNEL_3 = 0xC1D8,    /* looping background effects */
    CHANNEL_SIZE = 0x20,
    CHANNEL_COUNT = 7,
};

/* SoftwareChannel.flags */
enum {
    CH_ACTIVE = 0x80,         /* bit 7: the channel runs every frame */
    CH_FIXED_NOISE = 0x40,    /* bit 6: noise at a fixed shift rate, no period writes (E3) */
    CH_SLIDE = 0x20,          /* bit 5: portamento: notes come in (from, to) pairs (E6/E7) */
    CH_RAW_PERIOD = 0x08,     /* bit 3: notes are raw big-endian PSG periods (E8/E9) */
    CH_MUTED = 0x04,          /* bit 2: PSG writes suppressed: an effect borrows the voice */
};

/* SoftwareChannel.unknown1 is the channel's call stack pointer: an offset
 * inside the channel's own 32 bytes where command EA (call) stores return
 * addresses, growing down from $20 (the loop counters of EC live at $17+). */
#define callStackTop unknown1
enum { CALL_STACK_EMPTY = 0x20, LOOP_COUNTERS_OFFSET = 0x17 };

/* --------------------------------------------------------- engine state */

enum {
    SOUND_CONTROL_IDLE = 0x80,  /* v_soundControl: no pending request */
    SOUND_REQUEST_FIRST = 0x81, /* $81 + index requests sound `index` */
    SOUND_REQUEST_END = 0xB4,   /* requests >= $B4 (or < $80) shut the engine down */
    SOUND_NUMBER_LIMIT = 0x30,  /* indexes below this are remembered in v_soundNumber */

    FADE_STEP_FRAMES = 0x1E,    /* v_soundFadeOutTimer period */
    FADE_START_VOLUME = 0x0B,
    FADE_END_VOLUME = 0x03,     /* reaching it ends the fade (volume 0) */

    CHANNEL_SEVEN_BUSY = 0x80,  /* v_soundSoftwareChannelSevenState: capsule effect looping */
};

/* Indexes (request - $81) used by the engine itself. */
enum {
    SOUND_INDEX_JUMP = 0x10,    /* $91 */
    SOUND_INDEX_TEXTBOX = 0x13, /* $94, loops until request $B1 */
};

/* v_level value whose splash does not switch to the underwater song. */
#define LEVEL_16 0x10

/* ------------------------------------------------------------ ROM tables */

enum {
    SOUND_HEADERS = 0x98DD,        /* sounds: word per request index */
    SOUND_STARTERS = 0x993D,       /* soundHandlers: word per request index */
    COMMAND_HANDLERS = 0x9CE4,     /* commandHandlers: word per command $E0-$F1 */
    COMMAND_CONTINUATION = 0x9CE0, /* "inc de; jp readInstruction" */
    PSG_SILENCE_BYTES = 0x9E18,    /* psgResetVolumeBytes (4 bytes) */
    NOTE_PERIODS = 0x9E1C,         /* noteFrequencies: word per note, 0 = rest */
    SONG_UNDERWATER = 0xA3BD,      /* songUnderwater header */
    VOLUME_ENVELOPES = 0xB1F9,     /* envelopes: word per envelope (1-based) */
    PITCH_ENVELOPES = 0xB28A,      /* pitchEnvelopes: word per envelope (1-based) */
};

/* ---------------------------------------------------------- PSG helpers */

/* writeAToPsgIfFlagBit2 ($9DEB): a channel whose voice is borrowed by a
 * sound effect keeps running but stays off the PSG. */
static inline void psg_write_unless_muted(const SoftwareChannel *ch, uint8_t value) {
    if (!(ch->flags & CH_MUTED)) io_out(PSG_PORT, value);
}

/* Volume latch byte of the channel's PSG voice. */
static inline uint8_t volume_latch(const SoftwareChannel *ch, uint8_t attenuation) {
    return (uint8_t)((uint8_t)(ch->hardwareChannel + PSG_LATCH_VOLUME) | attenuation);
}

/* ------------------------------------------- helpers shared by the files */

/* engine.c */
void audio_clear_state(void);            /* zero $C111-$C1F5 (reset's RAM part) */
void audio_silence_psg(void);            /* volume off on the 4 voices */
void audio_silence_tone2_and_noise(void);/* _LABEL_9E0F_ */
void audio_acknowledge_request(void);    /* resetSoundControl */

/* sound_start.c */
void audio_load_channels(uint16_t header, uint16_t first_channel); /* realHandleSong */
void audio_play_song(uint16_t header);   /* handleSong */

/* channel.c */
uint16_t audio_multiply(uint8_t a, uint8_t b);                        /* multiplyWord */
uint8_t audio_divide(uint16_t dividend, uint8_t divisor, bool carry); /* _LABEL_9EBA_ */

#endif
