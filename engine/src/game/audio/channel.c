/*
 * Software channel playback: timing, portamento, envelopes and PSG output.
 *
 * Every frame an active channel counts one more frame of its note; when the
 * note has lasted noteDuration frames, the sequencer (readChannelInstruction,
 * sequencer.c) reads the next one. The channel then computes the PSG period
 * (note period, portamento, pitch envelope) and attenuation (volume and
 * volume envelope) and writes them to its PSG voice, unless it is muted.
 *
 * Envelopes are strings of 4-bit steps, high nibble first, one step per
 * frame, restarted with every note. A byte $00-$03 in a high-nibble position
 * is a command:
 *   $00     restart from the first step;
 *   $01     hold the previous step forever;
 *   $02     end: volume envelopes silence the channel, pitch envelopes stop
 *           updating the period (the last one stays);
 *   $03 nn  jump to step nn.
 * Volume step s: loudness = volume + s - 15 (0 when negative).
 * Pitch step s: period = note period + (15 - s) (higher period = lower tone).
 */
#include "game/audio/audio.h"

/* ------------------------------------------------------------ helpers */

enum { ENV_RESTART = 0x00, ENV_HOLD = 0x10, ENV_END = 0x20, ENV_JUMP = 0x30 };

static uint8_t swap_nibbles(uint8_t v) { return (uint8_t)((v << 4) | (v >> 4)); }

/* loadAthPointer ($9B0B): entry `index` (1-based) of a word table. */
static uint16_t envelope_address(uint16_t table, uint8_t index) {
    uint8_t i = (uint8_t)(index - 1);
    return rd16((uint16_t)(table + 2 * i));
}

/* multiplyWord ($9EAE): 8x8 -> 16 bit shift-and-add product. The carry it
 * leaves is always 0 (the product never overflows). */
uint16_t audio_multiply(uint8_t a, uint8_t b) {
    return (uint16_t)(a * b);
}

/* _LABEL_9EBA_ ($9EBA): 16/8 bit restoring division giving 8 quotient bits.
 * When dividend / 256 < divisor, the result is dividend / divisor (a zero
 * divisor gives $FF). The original shifts inverted quotient bits into L
 * (carry set = "did not subtract") and complements them at the end; the
 * incoming carry lands in bit 7 of L and is dropped by the final RLA. */
uint8_t audio_divide(uint16_t dividend, uint8_t divisor, bool carry) {
    uint16_t hl = dividend;
    for (int i = 0; i < 8; i++) {
        bool overflow = hl & 0x8000;
        hl = (uint16_t)((hl << 1) | carry); /* adc hl,hl */
        uint8_t remainder = (uint8_t)(hl >> 8);
        if (overflow || remainder >= divisor) {
            hl = (uint16_t)(((uint8_t)(remainder - divisor) << 8) | (hl & 0xFF));
            carry = false;
        } else {
            carry = true;
        }
    }
    return (uint8_t)~(((hl & 0xFF) << 1) | carry); /* rla; cpl */
}

/* applyVolumeEnvelope ($9BB2): sets volumeToWrite from envelope `env`. */
static void volume_envelope_step(SoftwareChannel *ch, uint16_t env) {
    for (;;) {
        uint8_t step = ch->volumeEnvelopeCounter;
        uint16_t at = (uint16_t)(env + (step >> 1));
        uint8_t value = rd8(at);
        if (!(step & 1)) {
            value = swap_nibbles(value); /* high nibble first */
            if (value == ENV_RESTART) {
                ch->volumeEnvelopeCounter = 0;
                continue;
            }
            if (value == ENV_HOLD) {
                ch->volumeEnvelopeCounter--;
                continue;
            }
            if (value == ENV_END) {
                ch->volumeToWrite = PSG_VOLUME_OFF;
                return;
            }
            if (value == ENV_JUMP) {
                ch->volumeEnvelopeCounter = rd8((uint16_t)(at + 1));
                continue;
            }
        }
        ch->volumeEnvelopeCounter++;
        /* or $F0 / add volume / inc a: loudness = volume + step - 15 when the
         * addition carries (volume + step >= 16), else 0. */
        unsigned sum = (unsigned)((value & 0x0F) | 0xF0) + ch->volume;
        uint8_t loudness = sum > 0xFF ? (uint8_t)(sum + 1) : 0;
        ch->volumeToWrite = (uint8_t)~loudness & 0x0F;
        return;
    }
}

/* applyPitchEnvelope ($9BF8): sets frequencyToWrite = period + offset. */
static void pitch_envelope_step(SoftwareChannel *ch, uint16_t env, uint16_t period) {
    for (;;) {
        uint8_t step = ch->pitchEnvelopeCounter;
        uint16_t at = (uint16_t)(env + (step >> 1));
        uint8_t value = rd8(at);
        if (!(step & 1)) {
            value = swap_nibbles(value);
            if (value == ENV_RESTART) {
                ch->pitchEnvelopeCounter = 0;
                continue;
            }
            if (value == ENV_HOLD) {
                ch->pitchEnvelopeCounter--;
                continue;
            }
            if (value == ENV_END) return; /* frequencyToWrite keeps its last value */
            if (value == ENV_JUMP) {
                /* QUIRK: unlike the volume envelope, the jump does not
                 * re-read: this frame uses the target index itself as the
                 * step value, and the target step is skipped. */
                value = rd8((uint16_t)(at + 1));
                ch->pitchEnvelopeCounter = value;
            }
        }
        ch->pitchEnvelopeCounter++;
        ch->frequencyToWrite = (uint16_t)(period + ((uint8_t)~value & 0x0F));
        return;
    }
}

/* _LABEL_9B21: portamento. The period moves linearly from noteFrequency to
 * noteFrequency2 over the note: from + delta * elapsed / (duration - 1).
 * Only low bytes are used (delta, elapsed frames and duration are assumed
 * below 256). */
static uint16_t slide_period(const SoftwareChannel *ch, uint16_t from) {
    uint16_t diff = (uint16_t)(ch->noteFrequency2 - from);
    bool downwards = diff & 0x8000; /* towards a smaller period */
    uint8_t delta = (uint8_t)diff;
    if (downwards) delta = (uint8_t)-delta;
    uint16_t scaled = audio_multiply(delta, (uint8_t)ch->currentPlayDuration);
    uint8_t offset = audio_divide(scaled, (uint8_t)(ch->noteDuration - 1), false);
    return downwards ? (uint16_t)(from - offset) : (uint16_t)(from + offset);
}

/* Writes the period to the channel's tone register (two bytes, low 4 bits
 * first). A noise channel's period goes to tone 2: in shift-rate mode 3 the
 * noise generator is clocked by tone 2. */
static void write_period(const SoftwareChannel *ch) {
    uint8_t latch = ch->hardwareChannel;
    if (latch == PSG_NOISE) latch = PSG_TONE2;
    uint16_t period = ch->frequencyToWrite;
    psg_write_unless_muted(ch, (uint8_t)(latch | (period & 0x0F)));
    /* (low & $F0 | high) with nibbles swapped = bits 4-9 of the period. */
    psg_write_unless_muted(ch, swap_nibbles((uint8_t)((period & 0xF0) | (period >> 8))));
}

/* writeChannelVolume: returns the byte written (runChannel's A). */
static uint8_t write_volume(const SoftwareChannel *ch) {
    uint8_t latch = volume_latch(ch, ch->volumeToWrite);
    psg_write_unless_muted(ch, latch);
    return latch;
}

/* ------------------------------------------------------------ routines */

/* $9ACC: runs software channel IX for one frame.
 * out: A = the volume byte written to the PSG (or, when a command stopped the
 * channel, the silencing byte written by the stop command, which then returns
 * to update() directly). */
LIFTED(runChannel, 0x9ACC) {
    SoftwareChannel *ch = channel_at(cpu.ix);

    ch->currentPlayDuration++;
    if (ch->currentPlayDuration == ch->noteDuration) {
        /* May stop the channel: CALL_ROUTINE then returns for us. */
        CALL_ROUTINE(f_readChannelInstruction);
    }

    uint16_t period = ch->noteFrequency;
    if (period == 0) { /* rest */
        ch->volumeToWrite = PSG_VOLUME_OFF;
        cpu.a = write_volume(ch);
        LIFTED_RETURN();
    }

    if (ch->flags & CH_SLIDE) {
        period = slide_period(ch, period);
        ch->frequencyToWrite = period;
        if (ch->pitchEnvelope)
            pitch_envelope_step(ch, envelope_address(PITCH_ENVELOPES, ch->pitchEnvelope), period);
    } else if (ch->pitchEnvelope) {
        pitch_envelope_step(ch, envelope_address(PITCH_ENVELOPES, ch->pitchEnvelope), period);
    } else {
        ch->frequencyToWrite = period;
    }

    if (ch->volumeEnvelope)
        volume_envelope_step(ch, envelope_address(VOLUME_ENVELOPES, ch->volumeEnvelope));
    else
        ch->volumeToWrite = (uint8_t)~ch->volume & 0x0F;

    if (!(ch->flags & CH_FIXED_NOISE)) write_period(ch);
    cpu.a = write_volume(ch);
    LIFTED_RETURN();
}

/* $9B0B: HL = word table, A = 1-based index -> HL = table[A - 1]. */
LIFTED(loadAthPointer_9B0B, 0x9B0B) {
    cpu.hl = envelope_address(cpu.hl, cpu.a);
    LIFTED_RETURN();
}

/* $9BB2: one frame of the volume envelope at HL for channel IX. */
LIFTED(applyVolumeEnvelope, 0x9BB2) {
    volume_envelope_step(channel_at(cpu.ix), cpu.hl);
    LIFTED_RETURN();
}

/* $9BF8: one frame of the pitch envelope at HL for channel IX, applied to the
 * period in DE. */
LIFTED(applyPitchEnvelope, 0x9BF8) {
    pitch_envelope_step(channel_at(cpu.ix), cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* $9EAE: HL = H * E. out: HL, carry (always clear). */
LIFTED(multiplyWord, 0x9EAE) {
    cpu.hl = audio_multiply(cpu.h, cpu.e);
    cpu.f &= (uint8_t)~FLAG_C;
    LIFTED_RETURN();
}

/* $9EBA: A = HL / E (8-bit quotient, see audio_divide). in: carry flag. */
LIFTED(_LABEL_9EBA_, 0x9EBA) {
    cpu.a = audio_divide(cpu.hl, cpu.e, cpu.f & FLAG_C);
    LIFTED_RETURN();
}
