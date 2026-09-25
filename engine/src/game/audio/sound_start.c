/*
 * Sound starters: the routines readSoundRequest jumps to for each request
 * (soundHandlers table), and the three special requests $B1-$B3.
 *
 * Songs ($81-$89, $AF, $B0) reset the whole engine and load the music
 * channels. Sound effects go to one of the three SFX channels:
 *   SFX channel 1: two-voice effects; mutes music channels 2 and 3;
 *   SFX channel 2: one-voice effects; mutes music channel 3 (and 4 for
 *                  noise effects);
 *   SFX channel 3: looping effects, not subject to priorities.
 * Muting (CH_MUTED) keeps the music channel running silently so that it is
 * still in time when the effect ends (command E2 unmutes everything).
 * Effects on channels 1 and 2 have a priority: a request whose priority is
 * lower than v_soundEffectPriority (the one playing) is ignored.
 *
 * Every starter acknowledges the request (v_soundControl = $80), except $B1
 * in one case (see _LABEL_99A3_).
 */
#include "game/audio/audio.h"

/* Priorities of the starters. */
enum {
    PRIORITY_NONE = 0x00,
    PRIORITY_LOW = 0x20,
    PRIORITY_MEDIUM = 0x30,
    PRIORITY_NOISE = 0x40,
    PRIORITY_HIGH = 0x60,
    PRIORITY_TOP = 0x70,
};

/* ------------------------------------------------------------ helpers */

static void mute(uint16_t channel) { channel_at(channel)->flags |= CH_MUTED; }
static void unmute(uint16_t channel) { channel_at(channel)->flags &= (uint8_t)~CH_MUTED; }

void audio_load_channels(uint16_t header, uint16_t channel) {
    /* realHandleSong: header = count, then 9 bytes per channel. QUIRK: a
     * count of 0 would load 256 channels (djnz). */
    uint8_t count = rd8(header++);
    do {
        for (int i = 0; i < 9; i++) ram8(channel + i) = rd8((uint16_t)(header + i)); /* ldir */
        header = (uint16_t)(header + 9);
        SoftwareChannel *ch = channel_at(channel);
        ch->callStackTop = CALL_STACK_EMPTY;
        ch->noteDuration = 1; /* the first note is read on the next frame */
        ch->currentPlayDuration = 0;
        /* QUIRK: bytes $0E-$1F are not cleared: an effect interrupted in the
         * middle of an EC loop leaves its counter for the next effect. */
        channel = (uint16_t)(channel + CHANNEL_SIZE);
    } while (--count);
    audio_acknowledge_request();
}

void audio_play_song(uint16_t header) {
    audio_clear_state();
    audio_silence_psg();
    audio_load_channels(header, MUSIC_CHANNEL_1);
}

/* _LABEL_9A9D_: common tail of the effect starters. */
static void start_effect(uint16_t header, uint16_t channel, uint8_t priority) {
    ram8(v_soundEffectPriority) = priority;
    audio_silence_tone2_and_noise();
    audio_load_channels(header, channel);
}

/* sub_9A06: two-voice effect on SFX channel 1 (tone 1 + tone 2). */
static void start_two_voice_effect(uint16_t header, uint8_t priority, uint8_t current) {
    if (priority < current) {
        audio_acknowledge_request();
        return;
    }
    mute(MUSIC_CHANNEL_2);
    mute(MUSIC_CHANNEL_3);
    mute(SFX_CHANNEL_3);
    start_effect(header, SFX_CHANNEL_1, priority);
}

/* sub_9A49: noise effect on SFX channel 2. */
static void start_noise_effect(uint16_t header, uint8_t priority, uint8_t current) {
    if (priority < current) {
        audio_acknowledge_request();
        return;
    }
    mute(MUSIC_CHANNEL_4);
    mute(MUSIC_CHANNEL_3);
    mute(SFX_CHANNEL_3);
    start_effect(header, SFX_CHANNEL_2, priority);
}

/* _LABEL_9A8B_: one-voice (tone 2) effect on SFX channel 2. */
static void start_one_voice_effect(uint16_t header, uint8_t priority, uint8_t current) {
    if (priority < current) {
        audio_acknowledge_request();
        return;
    }
    mute(MUSIC_CHANNEL_3);
    mute(SFX_CHANNEL_3);
    start_effect(header, SFX_CHANNEL_2, priority);
}

/* _LABEL_9A6D_: looping effect on SFX channel 3 (no priority involved; the
 * music voice it takes is muted every frame by handleFadeOutAndSfx). */
static void start_looping_effect(uint16_t header) {
    mute(SFX_CHANNEL_2);
    audio_silence_tone2_and_noise();
    audio_load_channels(header, SFX_CHANNEL_3);
}

/* ------------------------------------------------------------- songs */

/* $99F0: starts a song. in: BC = song header. */
LIFTED(handleSong, 0x99F0) {
    audio_play_song(cpu.bc);
    LIFTED_RETURN();
}

/* $9AA3: loads the channels of header BC into consecutive software channels
 * starting at DE, then acknowledges the request. */
LIFTED(realHandleSong, 0x9AA3) {
    audio_load_channels(cpu.bc, cpu.de);
    LIFTED_RETURN();
}

/* ------------------------------------- effects on SFX channel 1 (two voices) */
/* in: BC = effect header, E = priority of the effect playing. */

LIFTED(_LABEL_99F9_, 0x99F9) { /* $8D, $A3 (star box) */
    start_two_voice_effect(cpu.bc, PRIORITY_NONE, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_99FC_, 0x99FC) { /* $8E (coins), $8F (power-up) */
    start_two_voice_effect(cpu.bc, PRIORITY_HIGH, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A00_, 0x9A00) { /* $92 (splash), $A2 */
    start_two_voice_effect(cpu.bc, PRIORITY_TOP, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A04_, 0x9A04) { /* $95 (boss defeated) */
    start_two_voice_effect(cpu.bc, PRIORITY_LOW, cpu.e);
    LIFTED_RETURN();
}

/* in: A = priority of the new effect. */
LIFTED(sub_9A06, 0x9A06) {
    start_two_voice_effect(cpu.bc, cpu.a, cpu.e);
    LIFTED_RETURN();
}

/* $9B (falling into a pit): stops everything (music included) first; the
 * effect then plays with priority 0 (reset leaves A = 0). */
LIFTED(_LABEL_9A1F_, 0x9A1F) {
    audio_clear_state();
    audio_silence_psg();
    start_effect(cpu.bc, SFX_CHANNEL_1, PRIORITY_NONE);
    LIFTED_RETURN();
}

/* $9E (lightning), $A0: tone 1 + noise, so the drums are muted too. */
LIFTED(_LABEL_9A24_, 0x9A24) {
    if (PRIORITY_TOP < cpu.e) {
        audio_acknowledge_request();
        LIFTED_RETURN();
    }
    mute(MUSIC_CHANNEL_2);
    mute(MUSIC_CHANNEL_3);
    mute(MUSIC_CHANNEL_4);
    mute(SFX_CHANNEL_3);
    start_effect(cpu.bc, SFX_CHANNEL_1, PRIORITY_TOP);
    LIFTED_RETURN();
}

/* in: A = priority. Starts on SFX channel 1 without any check. */
LIFTED(sub_9A3E, 0x9A3E) {
    start_effect(cpu.bc, SFX_CHANNEL_1, cpu.a);
    LIFTED_RETURN();
}

/* ------------------------------------------- effects on SFX channel 2 */

LIFTED(_LABEL_9A43_, 0x9A43) { /* $A4 (shock wave) */
    start_noise_effect(cpu.bc, PRIORITY_HIGH, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A47_, 0x9A47) { /* $99 */
    start_noise_effect(cpu.bc, PRIORITY_NOISE, cpu.e);
    LIFTED_RETURN();
}

/* in: A = priority of the new effect. */
LIFTED(sub_9A49, 0x9A49) {
    start_noise_effect(cpu.bc, cpu.a, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A7A_, 0x9A7A) { /* $8A (punch), $8B (smoke), $8C (block), $91 (jump), $9A, $A1 */
    start_one_voice_effect(cpu.bc, PRIORITY_NONE, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A7D_, 0x9A7D) { /* $90, $A5, $A8 (bullet), $AD (janken count), $AE (janken throw) */
    start_one_voice_effect(cpu.bc, PRIORITY_HIGH, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A81_, 0x9A81) { /* $9C */
    start_one_voice_effect(cpu.bc, PRIORITY_TOP, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A85_, 0x9A85) { /* $97 (merman bubbles), $98 (monkey leaf), $AC (boss head) */
    start_one_voice_effect(cpu.bc, PRIORITY_MEDIUM, cpu.e);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A89_, 0x9A89) { /* $A6, $A7, $A9 */
    start_one_voice_effect(cpu.bc, PRIORITY_LOW, cpu.e);
    LIFTED_RETURN();
}

/* in: A = priority. Also the direct starter of $93 (battle lost) and $94
 * (text box), entered with A = E = current priority: always accepted, the
 * priority stays the same. */
LIFTED(_LABEL_9A8B_, 0x9A8B) {
    start_one_voice_effect(cpu.bc, cpu.a, cpu.e);
    LIFTED_RETURN();
}

/* in: A = priority, BC = header, DE = first channel. */
LIFTED(_LABEL_9A9D_, 0x9A9D) {
    start_effect(cpu.bc, cpu.de, cpu.a);
    LIFTED_RETURN();
}

/* ------------------------------------------- effects on SFX channel 3 */

/* $9D, $9F: ignored while a capsule effect loops on channel 3. */
LIFTED(_LABEL_9A60_, 0x9A60) {
    if (ram8(v_soundSoftwareChannelSevenState) != 0) {
        audio_acknowledge_request();
        LIFTED_RETURN();
    }
    start_looping_effect(cpu.bc);
    LIFTED_RETURN();
}

/* $AA, $AB (magic capsules): marks channel 3 busy. */
LIFTED(_LABEL_9A68_, 0x9A68) {
    ram8(v_soundSoftwareChannelSevenState) = CHANNEL_SEVEN_BUSY;
    start_looping_effect(cpu.bc);
    LIFTED_RETURN();
}

LIFTED(_LABEL_9A6D_, 0x9A6D) { /* $96 */
    start_looping_effect(cpu.bc);
    LIFTED_RETURN();
}

/* --------------------------------------------------- special requests */

/* $B1: stops the effect of SFX channel 2 if the last sound requested was the
 * text box ($94, which loops forever) or the jump ($91), and gives tone 2
 * back to the music. QUIRK: otherwise the request is not acknowledged, so it
 * stays pending and is checked again every frame until the game requests
 * another sound. */
LIFTED(_LABEL_99A3_, 0x99A3) {
    uint8_t last = ram8(v_soundNumber);
    if (last != SOUND_INDEX_TEXTBOX && last != SOUND_INDEX_JUMP) LIFTED_RETURN();
    channel_at(SFX_CHANNEL_2)->flags = 0;
    channel_at(MUSIC_CHANNEL_3)->flags = CH_ACTIVE;
    unmute(SFX_CHANNEL_3);
    audio_acknowledge_request();
    LIFTED_RETURN();
}

/* $B3: fades the music out (see handleFadeOutAndSfx) and stops the drums. */
LIFTED(_LABEL_99BE_, 0x99BE) {
    ram8(v_soundFadeOutVolume) = FADE_START_VOLUME;
    ram8(v_soundFadeOutTimer) = FADE_STEP_FRAMES;
    channel_at(MUSIC_CHANNEL_4)->flags = 0;
    io_out(PSG_PORT, PSG_NOISE | PSG_LATCH_VOLUME | PSG_VOLUME_OFF);
    audio_acknowledge_request();
    LIFTED_RETURN();
}

/* $B2: stops the looping effect of SFX channel 3 and gives its voice back to
 * the music (channel 3 unmuted, channel 4 restarted in fixed-noise mode). */
LIFTED(handler_LABEL_99D3_, 0x99D3) {
    channel_at(SFX_CHANNEL_3)->flags = 0;
    ram8(v_soundSoftwareChannelSevenState) = 0;
    /* QUIRK: `ld a,$DF` (tone 2 volume off) is immediately overwritten by
     * `ld a,$80`, and that $80 is what goes to the PSG: it clears the low
     * bits of tone 0's period until music channel 1 rewrites it. */
    uint8_t a = CH_ACTIVE;
    channel_at(MUSIC_CHANNEL_3)->flags = a;
    io_out(PSG_PORT, a);
    channel_at(MUSIC_CHANNEL_4)->flags = CH_ACTIVE | CH_FIXED_NOISE;
    unmute(SFX_CHANNEL_2);
    audio_acknowledge_request();
    LIFTED_RETURN();
}
