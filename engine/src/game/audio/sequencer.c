/*
 * Channel sequencer: reads a channel's byte stream (notes, durations and
 * commands $E0-$F1) when its current note ends.
 *
 * Stream format (normal mode):
 *   $00-$7F        duration only: replay the current note for n frames
 *                  (n * the channel's duration multiplier);
 *   $80-$DF        note (n - $80, 0 = rest, else transposed index into the
 *                  noteFrequencies table), optionally followed by a duration
 *                  byte ($00-$7F); without one the previous duration is kept;
 *   $E0-$F1        command (see below).
 * With CH_SLIDE set, a note is followed by a second note (the slide target,
 * transposed, no rest check) and a mandatory duration. With CH_RAW_PERIOD
 * set, every non-command byte starts a raw period (high byte, low byte,
 * [target high, target low when sliding], duration).
 *
 * Commands receive DE = address of their first argument byte. They are run
 * the original way: readInstruction pushes a continuation ($9CE0: "inc de /
 * jp readInstruction") and jumps through the commandHandlers table, so after
 * a normal RET the stream continues one byte after DE. Commands without
 * argument therefore decrement DE, and the stop commands (E2, EF, F0) pop
 * that continuation and readChannelInstruction's return address, returning
 * straight to update() without the channel writing its PSG registers.
 */
#include "game/audio/audio.h"

/* ------------------------------------------------------------ helpers */

/* Duration byte -> noteDuration in frames. */
static void set_note_duration(SoftwareChannel *ch, uint8_t duration) {
    ch->noteDuration = audio_multiply(duration, ch->duration);
}

static uint16_t note_period(uint8_t note) {
    return rd16((uint16_t)(NOTE_PERIODS + 2 * note));
}

/* Runs the handler of command `cmd` with the original stack discipline.
 * Returns true when the command stopped the channel: it has then already
 * returned to update() on behalf of readChannelInstruction and runChannel. */
static bool run_command(uint8_t cmd, uint16_t *p) {
    uint8_t index = cmd & 0x1F;
    uint16_t handler = rd16((uint16_t)(COMMAND_HANDLERS + 2 * index));
    push16(COMMAND_CONTINUATION);
    uint16_t sp_after_ret = (uint16_t)(cpu.sp + 2);
    cpu.de = *p;
    cpu.bc = index;
    cpu.hl = handler;
    cpu.a = (uint8_t)handler;
    rt_dispatch(handler); /* jp (hl) */
    if (cpu.sp > sp_after_ret) return true;
    if (cpu.sp < sp_after_ret) rt_stack_error(COMMAND_CONTINUATION, sp_after_ret);
    *p = (uint16_t)(cpu.de + 1); /* the continuation: inc de */
    return false;
}

/* Address of byte `offset` of the channel at `channel` (IX + 8-bit offset). */
static uint16_t channel_byte(uint16_t channel, uint8_t offset) {
    return (uint16_t)(channel + offset);
}

/* ---------------------------------------------------------- sequencer */

/* $9C39: reads the next note of channel IX from its data pointer, running
 * the commands found on the way, and restarts the note: envelopes from their
 * first step, currentPlayDuration = 0. */
LIFTED(readChannelInstruction, 0x9C39) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    uint16_t p = ch->dataPointer;
    uint8_t byte;

    while ((byte = rd8(p++)) >= 0xE0) {
        if (run_command(byte, &p)) return;
    }

    if (ch->flags & CH_RAW_PERIOD) {
        ch->noteFrequency = (uint16_t)(byte << 8 | rd8(p));
        p++;
        if (ch->flags & CH_SLIDE) {
            uint8_t high = rd8(p++);
            ch->noteFrequency2 = (uint16_t)(high << 8 | rd8(p));
            p++;
        }
        set_note_duration(ch, rd8(p++));
    } else if (byte < 0x80) {
        set_note_duration(ch, byte);
    } else {
        uint8_t note = (uint8_t)(byte - 0x80);
        if (note != 0) note = (uint8_t)(note + ch->transpose);
        ch->noteFrequency = note_period(note);
        if (ch->flags & CH_SLIDE) {
            uint8_t target = (uint8_t)(rd8(p++) - 0x80 + ch->transpose);
            ch->noteFrequency2 = note_period(target);
            set_note_duration(ch, rd8(p++));
        } else if (!(rd8(p) & 0x80)) {
            set_note_duration(ch, rd8(p++));
        }
    }

    ch->volumeEnvelopeCounter = 0;
    ch->pitchEnvelopeCounter = 0;
    ch->dataPointer = p;
    ch->currentPlayDuration = 0;
    LIFTED_RETURN();
}

/* ----------------------------------------------------------- commands */
/* in: IX = channel, DE = first argument byte. out: DE as described above. */

/* F1: switch the music to the underwater song (the splash effect ends with
 * it), except on level 16. The song reset wipes the running effect channel
 * too; the stream then continues with its E2. */
LIFTED(handleF1, 0x9D08) {
    if (ram8(v_level) == LEVEL_16) LIFTED_RETURN();
    audio_play_song(SONG_UNDERWATER);
    LIFTED_RETURN();
}

/* EE nn: transpose += nn. */
LIFTED(handleEE, 0x9D17) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    ch->transpose = (uint8_t)(ch->transpose + rd8(cpu.de));
    LIFTED_RETURN();
}

/* E0 nn: duration multiplier. */
LIFTED(handleE0, 0x9D1F) {
    channel_at(cpu.ix)->duration = rd8(cpu.de);
    LIFTED_RETURN();
}

/* E1 nn: volume (0-15). */
LIFTED(handleE1, 0x9D24) {
    channel_at(cpu.ix)->volume = rd8(cpu.de);
    LIFTED_RETURN();
}

/* E3 nn: writes the noise control register ($E0 | nn). Shift rate 3 (clocked
 * by tone 2) lets the channel's periods through to tone 2; any other rate is
 * fixed, so the channel stops writing periods (CH_FIXED_NOISE). */
LIFTED(handleE3, 0x9D29) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    uint8_t control = rd8(cpu.de) | PSG_NOISE_CONTROL;
    psg_write_unless_muted(ch, control);
    if ((control & PSG_NOISE_RATE_TONE2) == PSG_NOISE_RATE_TONE2) ch->flags &= (uint8_t)~CH_FIXED_NOISE;
    else ch->flags |= CH_FIXED_NOISE;
    LIFTED_RETURN();
}

/* E4 nn: volume envelope (1-based, 0 = none). */
LIFTED(handleE4VolumeEnvelope, 0x9D40) {
    channel_at(cpu.ix)->volumeEnvelope = rd8(cpu.de);
    LIFTED_RETURN();
}

/* ED nn: pitch envelope (1-based, 0 = none). */
LIFTED(handleEDPitchEnvelope, 0x9D45) {
    channel_at(cpu.ix)->pitchEnvelope = rd8(cpu.de);
    LIFTED_RETURN();
}

/* E5 llhh: jump. out: DE = target - 1 (the continuation adds 1). */
LIFTED(handleE5, 0x9D4A) {
    cpu.hl = (uint16_t)(cpu.de + 1);
    cpu.de = (uint16_t)(rd16(cpu.de) - 1);
    LIFTED_RETURN();
}

/* E6 / E7: portamento on / off. */
LIFTED(handleE6, 0x9D50) {
    channel_at(cpu.ix)->flags |= CH_SLIDE;
    cpu.de--; /* no argument */
    LIFTED_RETURN();
}

LIFTED(handleE7, 0x9D56) {
    channel_at(cpu.ix)->flags &= (uint8_t)~CH_SLIDE;
    cpu.de--;
    LIFTED_RETURN();
}

/* E8 / E9: raw period mode on / off. */
LIFTED(handleE8, 0x9D5C) {
    channel_at(cpu.ix)->flags |= CH_RAW_PERIOD;
    cpu.de--;
    LIFTED_RETURN();
}

LIFTED(handleE9, 0x9D62) {
    channel_at(cpu.ix)->flags &= (uint8_t)~CH_RAW_PERIOD;
    cpu.de--;
    LIFTED_RETURN();
}

/* EF: stop the channel and free SFX channel 3 for any looping effect. */
LIFTED(handleEF, 0x9D68) {
    ram8(v_soundSoftwareChannelSevenState) = 0;
    cpu.a = 0;
    TAIL_CALL(f_sub_9D76);
}

/* F0 nn: v_soundBattleSoundFlags = nn (tells the janken battle code the
 * music or count is over), then stop like E2. */
LIFTED(handleF0, 0x9D6E) {
    ram8(v_soundBattleSoundFlags) = rd8(cpu.de);
    TAIL_CALL(f_handleE2);
}

/* E2: end of the stream: stop the channel and reset the effect priority. */
LIFTED(handleE2, 0x9D72) {
    ram8(v_soundEffectPriority) = 0;
    cpu.a = 0;
    TAIL_CALL(f_sub_9D76);
}

/* $9D76: stops channel IX (flags = A, always 0), gives every borrowed voice
 * back to the music (unmutes music channels 2-4 and SFX channel 3), resets
 * the noise to white noise unless SFX channel 2 plays, silences the channel's
 * voice, then returns directly to update()'s channel loop by dropping the
 * command continuation and readChannelInstruction's return address.
 * out: A = the silencing byte. */
LIFTED(sub_9D76, 0x9D76) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    ch->flags = cpu.a;
    channel_at(MUSIC_CHANNEL_2)->flags &= (uint8_t)~CH_MUTED;
    channel_at(MUSIC_CHANNEL_3)->flags &= (uint8_t)~CH_MUTED;
    channel_at(MUSIC_CHANNEL_4)->flags &= (uint8_t)~CH_MUTED;
    channel_at(SFX_CHANNEL_3)->flags &= (uint8_t)~CH_MUTED;
    if (!(channel_at(SFX_CHANNEL_2)->flags & CH_ACTIVE))
        io_out(PSG_PORT, PSG_NOISE_CONTROL | 0x04); /* white noise, fastest rate */
    cpu.a = volume_latch(ch, PSG_VOLUME_OFF);
    psg_write_unless_muted(ch, cpu.a); /* _LABEL_9DE4_ */
    cpu.sp += 4; /* pop hl / pop hl */
    LIFTED_RETURN();
}

/* EA llhh: call. Pushes the address of the argument's last byte on the
 * channel's own stack (see callStackTop), then jumps like E5. */
LIFTED(handleEA, 0x9D9E) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    uint16_t target = rd16(cpu.de);
    uint16_t return_point = (uint16_t)(cpu.de + 1);
    uint8_t high_slot = --ch->callStackTop;
    ch->callStackTop--;
    uint16_t slot = channel_byte(cpu.ix, high_slot);
    wr8(slot, (uint8_t)(return_point >> 8));
    wr8((uint16_t)(slot - 1), (uint8_t)return_point);
    cpu.de = (uint16_t)(target - 1);
    LIFTED_RETURN();
}

/* EB: return from an EA call. out: DE = saved address (+1 by the
 * continuation = the byte after the EA command). */
LIFTED(handleEB, 0x9DB9) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    uint16_t slot = channel_byte(cpu.ix, ch->callStackTop);
    cpu.de = rd16(slot);
    cpu.hl = (uint16_t)(slot + 1);
    ch->callStackTop = (uint8_t)(ch->callStackTop + 2);
    LIFTED_RETURN();
}

/* EC ii nn llhh: loop. Counter ii (channel byte $17 + ii) is loaded with nn
 * when zero, then decremented: while non-zero jump to llhh (so the section
 * plays nn times), else continue after the command. */
LIFTED(handleECLoop, 0x9DCC) {
    uint16_t p = cpu.de;
    uint16_t counter = channel_byte(cpu.ix, (uint8_t)(rd8(p++) + LOOP_COUNTERS_OFFSET));
    if (rd8(counter) == 0) wr8(counter, rd8(p));
    p++;
    uint8_t left = (uint8_t)(rd8(counter) - 1);
    wr8(counter, left);
    if (left != 0) {
        cpu.de = p;
        TAIL_CALL(f_handleE5);
    }
    cpu.de = (uint16_t)(p + 1);
    LIFTED_RETURN();
}

/* $9DE4: volume off on channel IX's voice (unless muted). */
LIFTED(_LABEL_9DE4_, 0x9DE4) {
    SoftwareChannel *ch = channel_at(cpu.ix);
    cpu.a = volume_latch(ch, PSG_VOLUME_OFF);
    psg_write_unless_muted(ch, cpu.a);
    LIFTED_RETURN();
}

/* $9DEB: writes A to the PSG unless channel IX is muted. Preserves A. */
LIFTED(writeAToPsgIfFlagBit2_LABEL_9DEB_, 0x9DEB) {
    psg_write_unless_muted(channel_at(cpu.ix), cpu.a);
    LIFTED_RETURN();
}
