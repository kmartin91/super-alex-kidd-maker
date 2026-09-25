/*
 * Sound engine: per-frame entry point, sound requests, fade-out and reset.
 */
#include "game/audio/audio.h"

/* ------------------------------------------------------------ helpers */

void audio_clear_state(void) {
    /* ld (hl),0 / ldir from v_soundFadeOutVolume, $E5 bytes. QUIRK: stops at
     * $C1F5, two bytes short of the end of SFX channel 3; v_soundControl
     * itself is not cleared. */
    for (uint16_t a = v_soundFadeOutVolume; a <= v_soundFadeOutVolume + 0xE4; a++) ram8(a) = 0;
}

void audio_silence_psg(void) {
    for (int i = 0; i < 4; i++) io_out(PSG_PORT, rd8((uint16_t)(PSG_SILENCE_BYTES + i)));
}

void audio_silence_tone2_and_noise(void) {
    io_out(PSG_PORT, PSG_TONE2 | PSG_LATCH_VOLUME | PSG_VOLUME_OFF);
    io_out(PSG_PORT, PSG_NOISE | PSG_LATCH_VOLUME | PSG_VOLUME_OFF);
}

void audio_acknowledge_request(void) {
    ram8(v_soundControl) = SOUND_CONTROL_IDLE;
}

/* ------------------------------------------------------------ routines */

/* $984F: sound engine entry, called once per frame by the VBlank handler
 * (with bank 2 mapped). Starts the requested sound, steps the fade-out, then
 * runs every active software channel: the 4 music channels first, then the
 * 3 sound-effect channels, so an effect's PSG writes win over the music. */
LIFTED(update, 0x984F) {
    CALL_ROUTINE(f_readSoundRequest);
    CALL_ROUTINE(f_handleFadeOutAndSfx);
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        cpu.ix = (uint16_t)(MUSIC_CHANNEL_1 + i * CHANNEL_SIZE);
        if (channel_at(cpu.ix)->flags & CH_ACTIVE) CALL_ROUTINE(f_runChannel);
    }
    LIFTED_RETURN();
}

/* $986C: music fade-out (request $B3) and SFX channel 3 arbitration.
 * While v_soundFadeOutVolume is non-zero, every 30 frames it is decreased and
 * copied to the volume of music channels 1-3; when it reaches 3 it becomes 0,
 * which silences them and ends the fade.
 * Then, while SFX channel 3 plays, the music channel using the same PSG voice
 * (channel 4 for noise, channel 3 for tone 2) is muted every frame.
 * out: A = new fade timer, new fade volume, or 0 when no fade is running. */
LIFTED(handleFadeOutAndSfx, 0x986C) {
    uint8_t fade_volume = ram8(v_soundFadeOutVolume);
    uint8_t result = fade_volume;
    if (fade_volume != 0) {
        uint8_t timer = (uint8_t)(ram8(v_soundFadeOutTimer) - 1);
        if (timer != 0) {
            ram8(v_soundFadeOutTimer) = timer;
            result = timer;
        } else {
            ram8(v_soundFadeOutTimer) = FADE_STEP_FRAMES;
            fade_volume--;
            if (fade_volume == FADE_END_VOLUME) fade_volume = 0;
            ram8(v_soundFadeOutVolume) = fade_volume;
            channel_at(MUSIC_CHANNEL_1)->volume = fade_volume;
            channel_at(MUSIC_CHANNEL_2)->volume = fade_volume;
            channel_at(MUSIC_CHANNEL_3)->volume = fade_volume;
            result = fade_volume;
        }
    }
    cpu.a = result;

    const SoftwareChannel *sfx3 = channel_at(SFX_CHANNEL_3);
    if (sfx3->flags & CH_ACTIVE) {
        /* Bit 5 of the latch tells noise ($E0) from tone 2 ($C0). */
        if (sfx3->hardwareChannel & 0x20) channel_at(MUSIC_CHANNEL_4)->flags |= CH_MUTED;
        else channel_at(MUSIC_CHANNEL_3)->flags |= CH_MUTED;
    }
    LIFTED_RETURN();
}

/* $98AE: handles the request in v_soundControl.
 *   $80        nothing to do;
 *   $81-$B3    start sound (request - $81): jumps to its starter from the
 *              soundHandlers table with BC = its header (sounds table),
 *              A = E = current effect priority; the starter acknowledges
 *              the request (writes $80) unless it decides to keep it;
 *   otherwise  shut the whole engine down (reset).
 * Requests below $B1 are remembered in v_soundNumber. */
LIFTED(readSoundRequest, 0x98AE) {
    uint8_t request = ram8(v_soundControl);
    if (!(request & 0x80) || request >= SOUND_REQUEST_END) TAIL_CALL(f_reset_9DF3);
    if (request == SOUND_CONTROL_IDLE) LIFTED_RETURN();

    uint8_t index = (uint8_t)(request - SOUND_REQUEST_FIRST);
    if (index < SOUND_NUMBER_LIMIT) ram8(v_soundNumber) = index;

    uint8_t priority = ram8(v_soundEffectPriority);
    cpu.bc = rd16((uint16_t)(SOUND_HEADERS + 2 * index));
    cpu.hl = rd16((uint16_t)(SOUND_STARTERS + 2 * index));
    cpu.d = 0;
    cpu.e = priority;
    cpu.a = priority;
    rt_dispatch(cpu.hl); /* jp (hl): the starter returns to our caller */
}

/* $9DF3 reset: stops every sound. Clears the engine state ($C111-$C1F5: fade,
 * priority, all 7 channels) then silences the PSG (falls into resetVolume).
 * Also called by the game when changing screens.
 * out: A = 0; the alternate registers are left as the original's exx'd ldir
 * and otir leave them. */
LIFTED(reset_9DF3, 0x9DF3) {
    audio_clear_state();
    cpu.hl_ = v_soundFadeOutVolume + 0xE4; /* ldir leftovers in HL' DE' BC' */
    cpu.de_ = v_soundFadeOutVolume + 0xE5;
    cpu.bc_ = 0x0000;
    TAIL_CALL(f_resetVolume);
}

/* $9E02: volume off on the four PSG voices ($9F $BF $DF $FF from ROM).
 * out: A = 0 (HL' = end of the byte list, BC' = $007F as otir leaves them). */
LIFTED(resetVolume, 0x9E02) {
    audio_silence_psg();
    cpu.hl_ = PSG_SILENCE_BYTES + 4;
    cpu.bc_ = PSG_PORT;
    cpu.a = 0;
    LIFTED_RETURN();
}

/* $9E0F: volume off on tone 2 and noise, the voices sound effects take. */
LIFTED(_LABEL_9E0F_, 0x9E0F) {
    audio_silence_tone2_and_noise();
    LIFTED_RETURN();
}

/* $9AC6: acknowledges the request (v_soundControl = $80). */
LIFTED(resetSoundControl, 0x9AC6) {
    audio_acknowledge_request();
    cpu.a = SOUND_CONTROL_IDLE;
    LIFTED_RETURN();
}
