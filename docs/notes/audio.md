# Module `audio`: sound engine (bank 2, $984F-$9ECC)

All 61 routines of `docs/notes/audio-routines.txt` are lifted, exercised and
match the generated code (0 mismatches). Relaxed lockstep runs against the
reference emulator pass (PSG registers are compared every frame there).

## Files

| file | contents |
|------|----------|
| `engine/src/game/audio/audio.h` | constants (PSG latches, channel addresses, channel flags, ROM tables, engine state values), `psg_write_unless_muted`, shared helper prototypes |
| `engine/src/game/audio/engine.c` | `update`, `handleFadeOutAndSfx`, `readSoundRequest`, `reset`, `resetVolume`, `_LABEL_9E0F_`, `resetSoundControl` |
| `engine/src/game/audio/sound_start.c` | song/effect starters (`handleSong`, `realHandleSong`, `$99F9-$9A9D`), special requests `$B1-$B3` |
| `engine/src/game/audio/channel.c` | `runChannel`, envelopes, `loadAthPointer`, `multiplyWord`, `_LABEL_9EBA_` (division) |
| `engine/src/game/audio/sequencer.c` | `readChannelInstruction` and command handlers E0-F1, `sub_9D76`, `_LABEL_9DE4_`, `writeAToPsgIfFlagBit2` |
| `engine/src/game/audio/scenarios/sounds.txt` | shadow/lockstep script that requests every sound and runs the commands no ROM data uses |

Cross-file helpers are prefixed `audio_` (see `audio.h`).

## Verification

Build: `cd port && make BUILD=build-audio LIFTED_EXTRA="$(ls src/game/wip/audio/*.c | tr '\n' ' ')" build-audio/shadow build-audio/lockstep`

The routines are deeply nested (update -> readSoundRequest -> starter ->
`sub_9A06` -> `_LABEL_9A9D_` -> `realHandleSong`, or update -> runChannel ->
readChannelInstruction -> command -> `sub_9D76` -> `_LABEL_9DE4_`), so the
shadow runs use `--depth 12` (with the default depth 2 only update,
readSoundRequest, handleFadeOutAndSfx and runChannel are compared). The
scripted run also passes at depths 1, 2, 3, 5 and 30.

Shadow runs (all `OK ... 0 lifted routine(s) with mismatches`):

```
./build-audio/shadow ../original.sms --mode idle --frames 3000 --depth 12
./build-audio/shadow ../original.sms --mode play --seed S --frames 10000 --depth 12        # S = 1..6
./build-audio/shadow ../original.sms --mode play --seed N+10 --level N --lives --frames 6000 --depth 12   # N = 1..17
./build-audio/shadow ../original.sms --script tests/scenarios/audio_sounds.txt --frames 13700 --depth 12
```

Lockstep runs (all `OK: N frames identical ... [relaxed]`):

```
./build-audio/lockstep ../original.sms --relaxed --mode play --seed 1 --frames 20000
./build-audio/lockstep ../original.sms --relaxed --mode play --seed 3 --frames 20000
./build-audio/lockstep ../original.sms --relaxed --mode play --seed 7 --frames 20000
./build-audio/lockstep ../original.sms --relaxed --mode idle --frames 3000
./build-audio/lockstep ../original.sms --relaxed --mode play --seed 5 --level 2 --lives --frames 8000
./build-audio/lockstep ../original.sms --relaxed --script tests/scenarios/audio_sounds.txt --frames 13700
```

Sanity check of the test: deliberately breaking the pitch-envelope jump quirk
produced mismatches in `applyPitchEnvelope`/`runChannel` (then reverted).

`sounds.txt` pokes `v_soundControl` with every request `$81-$B3`, forces
priority rejections, runs `$B1/$B2/$B3`, invalid requests (`$50`, `$B4`), the
splash on levels 2 and 16, and plays small hand-written streams from the
unused RAM bytes `$C1F8-$C1FF` on SFX channel 3 to reach the commands that no
ROM data uses.

## Routine status

Calls: summed over the natural runs (idle, 6 play seeds, 17 levels) and over
`sounds.txt`, at depth 12 (nested compares count a call several times).

| addr | routine | better name | natural | script | mismatches |
|------|---------|-------------|--------:|-------:|-----------:|
| 984F | update | | 165000 | 13700 | 0 |
| 986C | handleFadeOutAndSfx | | 330000 | 27400 | 0 |
| 98AE | readSoundRequest | | 330000 | 27400 | 0 |
| 99A3 | _LABEL_99A3_ | stopTextboxOrJumpEffect ($B1) | 47372 | 512 | 0 |
| 99BE | _LABEL_99BE_ | startMusicFadeOut ($B3) | 0 | 4 | 0 |
| 99D3 | handler_LABEL_99D3_ | stopLoopingEffect ($B2) | 220 | 24 | 0 |
| 99F0 | handleSong | playSong | 1364 | 96 | 0 |
| 99F9 | _LABEL_99F9_ | twoVoiceEffectPrio00 | 108 | 12 | 0 |
| 99FC | _LABEL_99FC_ | twoVoiceEffectPrio60 | 144 | 12 | 0 |
| 9A00 | _LABEL_9A00_ | twoVoiceEffectPrio70 | 792 | 24 | 0 |
| 9A04 | _LABEL_9A04_ | twoVoiceEffectPrio20 | 0 | 8 | 0 |
| 9A06 | sub_9A06 | startTwoVoiceEffect | 1044 | 56 | 0 |
| 9A1F | _LABEL_9A1F_ | resetThenEffect (falling) | 8 | 4 | 0 |
| 9A24 | _LABEL_9A24_ | toneAndNoiseEffectPrio70 | 4 | 16 | 0 |
| 9A3E | sub_9A3E | startEffectOnSfx1 | 12 | 16 | 0 |
| 9A43 | _LABEL_9A43_ | noiseEffectPrio60 | 24 | 8 | 0 |
| 9A47 | _LABEL_9A47_ | noiseEffectPrio40 | 0 | 8 | 0 |
| 9A49 | sub_9A49 | startNoiseEffect | 24 | 16 | 0 |
| 9A60 | _LABEL_9A60_ | loopingEffectUnlessCapsule | 0 | 24 | 0 |
| 9A68 | _LABEL_9A68_ | capsuleLoopingEffect | 0 | 16 | 0 |
| 9A6D | _LABEL_9A6D_ | startLoopingEffect | 0 | 40 | 0 |
| 9A7A | _LABEL_9A7A_ | oneVoiceEffectPrio00 | 4436 | 32 | 0 |
| 9A7D | _LABEL_9A7D_ | oneVoiceEffectPrio60 | 64 | 20 | 0 |
| 9A81 | _LABEL_9A81_ | oneVoiceEffectPrio70 | 0 | 8 | 0 |
| 9A85 | _LABEL_9A85_ | oneVoiceEffectPrio30 | 24 | 12 | 0 |
| 9A89 | _LABEL_9A89_ | oneVoiceEffectPrio20 | 44 | 16 | 0 |
| 9A8B | _LABEL_9A8B_ | startOneVoiceEffect | 4572 | 100 | 0 |
| 9A9D | _LABEL_9A9D_ | startEffect | 5408 | 160 | 0 |
| 9AA3 | realHandleSong | loadSoundChannels | 6772 | 296 | 0 |
| 9AC6 | resetSoundControl | acknowledgeSoundRequest | 8228 | 372 | 0 |
| 9ACC | runChannel | | 1252398 | 80688 | 0 |
| 9B0B | loadAthPointer (9B0B) | envelopeAddress | 1628178 | 97006 | 0 |
| 9BB2 | applyVolumeEnvelope | | 959644 | 59520 | 0 |
| 9BF8 | applyPitchEnvelope | | 668534 | 37486 | 0 |
| 9C39 | readChannelInstruction | | 208648 | 12084 | 0 |
| 9D08 | handleF1 | cmdUnderwaterSong | 24 | 32 | 0 |
| 9D17 | handleEE | cmdTransposeBy | 13312 | 576 | 0 |
| 9D1F | handleE0 | cmdDurationMultiplier | 0 | 8 | 0 |
| 9D24 | handleE1 | cmdVolume | 53960 | 2568 | 0 |
| 9D29 | handleE3 | cmdNoiseControl | 3160 | 296 | 0 |
| 9D40 | handleE4VolumeEnvelope | | 81368 | 3648 | 0 |
| 9D45 | handleEDPitchEnvelope | | 0 | 8 | 0 |
| 9D4A | handleE5 | cmdJump | 48952 | 3464 | 0 |
| 9D50 | handleE6 | cmdSlideOn | 0 | 16 | 0 |
| 9D56 | handleE7 | cmdSlideOff | 0 | 16 | 0 |
| 9D5C | handleE8 | cmdRawPeriodsOn | 0 | 8 | 0 |
| 9D62 | handleE9 | cmdRawPeriodsOff | 0 | 8 | 0 |
| 9D68 | handleEF | cmdStopAndFreeChannel7 | 0 | 40 | 0 |
| 9D6E | handleF0 | cmdSetBattleFlagsAndStop | 864 | 32 | 0 |
| 9D72 | handleE2 | cmdStop | 10448 | 536 | 0 |
| 9D76 | sub_9D76 | stopChannel | 20896 | 1152 | 0 |
| 9D9E | handleEA | cmdCall | 15256 | 640 | 0 |
| 9DB9 | handleEB | cmdReturn | 13048 | 440 | 0 |
| 9DCC | handleECLoop | | 29328 | 1784 | 0 |
| 9DE4 | _LABEL_9DE4_ | silenceChannelVoice | 20896 | 1152 | 0 |
| 9DEB | writeAToPsgIfFlagBit2_LABEL_9DEB_ | psgWriteUnlessMuted | 2775622 | 177900 | 0 |
| 9DF3 | reset | | 1726 | 507 | 0 |
| 9E02 | resetVolume | | 3500 | 1016 | 0 |
| 9E0F | _LABEL_9E0F_ | silenceTone2AndNoise | 5408 | 200 | 0 |
| 9EAE | multiplyWord | | 241042 | 14846 | 0 |
| 9EBA | _LABEL_9EBA_ | divideWordByByte | 84706 | 6010 | 0 |

Only reached through `sounds.txt`:
- **Never used by the shipped data (dead in the original game):** commands
  E0 (`handleE0`), E6/E7, E8/E9, ED and EF. No reachable stream of any of the
  48 sounds contains them (the slide and raw-period modes are always set by
  the channel header flags instead). They were exercised with streams poked
  into RAM.
- **Used by the game, but not reached by random play:** `$B3` fade-out
  (`_LABEL_99BE_`; no direct `ld a,$B3` in the disassembly, it could only come
  from a table: possibly unused), `$95` boss defeated (`9A04`), `$99`
  (`9A47`), `$9C` (`9A81`), and the SFX channel 3 loops `$96`, `$9D`, `$9F`,
  `$AA`, `$AB` (`9A6D`, `9A60`, `9A68`: map capsules, some bosses).

## Control flow kept exactly

- `readSoundRequest` ends with `jp (hl)` into the starter: lifted as
  `rt_dispatch(handler)` with the original registers (BC = header,
  A = E = priority, D = 0); the starter returns to update. Invalid requests
  `TAIL_CALL(f_reset_9DF3)`.
- `readChannelInstruction` runs commands the original way (`run_command` in
  `sequencer.c`): push the continuation `$9CE0`, `rt_dispatch` the handler
  read from `commandHandlers` ($9CE4), then either SP is back above the
  continuation (normal RET: DE + 1, read on) or it is higher: the handler
  stopped the channel and the routine returns without popping.
- `handleE2`/`handleEF`/`handleF0` `TAIL_CALL` into `sub_9D76`, which pops two
  return addresses (`cpu.sp += 4`) and returns: control goes straight back to
  update's channel loop, skipping the rest of runChannel (no PSG period or
  volume write that frame). `runChannel` calls `readChannelInstruction` with
  `CALL_ROUTINE`, which returns for it in that case.
- `handleECLoop` `TAIL_CALL`s `handleE5` for the jump back.
- `reset`/`resetVolume` use the alternate register set as scratch (`exx`);
  the lifted versions leave HL' = $9E1C, BC' = $007F, DE' = $C1F6 (reset) as
  the original does, since callers in banks 0/1 have them live.

## Meanings discovered

RAM:
- `v_soundControl` ($C110): `$80` idle, `$81+i` request sound i, anything
  with bit 7 clear or >= `$B4` makes the engine reset every frame.
- `v_soundEffectPriority` ($C113): priority of the effect on SFX channels 1/2
  (`$00/$20/$30/$40/$60/$70`); requests with a lower priority are dropped;
  E2 (any channel, music included) resets it to 0.
- `v_soundSoftwareChannelSevenState` ($C114): `$80` while a magic capsule
  effect (`$AA/$AB`) loops on SFX channel 3; blocks `$9D/$9F`; cleared by
  `$B2` and command EF.
- `v_soundBattleSoundFlags` ($C115): written by command F0 (janken music,
  janken count, dead song) so the game knows the music/count has finished.
- `v_soundNumber` ($C116): index of the last sound requested (< `$30`); used
  by `$B1`.
- `$C117`: unused padding (cleared by reset).
- `$C1F8-$C1FF`: not used by anything (the test streams live there).

`SoftwareChannel`:
- `flags`: bit 7 active, bit 6 fixed-rate noise (no period writes; E3),
  bit 5 slide/portamento (E6/E7), bit 3 raw periods (E8/E9), bit 2 muted
  (PSG writes suppressed while an effect borrows the voice). Bits 0, 1, 4
  unused.
- `hardwareChannel`: PSG tone latch of the voice ($80/$A0/$C0/$E0); volume
  latch = +$10. A noise channel writes its period to tone 2 ($C0).
- `duration`: duration multiplier (frames per duration unit).
- `unknown1` ($09) = **call stack top** (`callStackTop` alias in audio.h):
  offset in the channel's own 32 bytes where EA pushes return pointers,
  starting at $20 and growing down.
- `pitchEnvelope` / `volumeEnvelope`: 1-based indexes into `pitchEnvelopes`
  ($B28A, 15 entries) / `envelopes` ($B1F9, 18 entries), 0 = none.
- `noteFrequency`, `noteFrequency2`: PSG periods of the note and of the slide
  target; `frequencyToWrite`, `volumeToWrite`: the values written this frame.
- `repetitionCounters` ($17-$1F): EC loop counters (shared with the call
  stack area).

Channels: music 1-4 at $C118/$C138/$C158/$C178 (songs always use tone 0,
tone 1, tone 2, noise in that order), effects 1-3 at $C198/$C1B8/$C1D8.

## Music data format

`sounds` ($98DD): 48 words, header address per request index; `soundHandlers`
($993D): 51 words, starter per index (the last 3 are `$B1-$B3`).

Header: `count`, then per channel 9 bytes copied to the channel: flags,
hardwareChannel, duration multiplier, data pointer (word), transpose, pitch
envelope, volume envelope, volume. The loader also sets callStackTop = $20,
noteDuration = 1 (first note read on the next frame), currentPlayDuration = 0.

Stream (read by `readChannelInstruction` when currentPlayDuration reaches
noteDuration):
- `$00-$7F`: duration only (replay the same period for n x multiplier
  frames).
- `$80+n`: note n (0 = rest, otherwise n + transpose indexes
  `noteFrequencies` at $9E1C: 73 PSG periods, A2..G#8). Followed by an
  optional duration byte (< $80); without it the previous duration is kept.
- slide mode (flag bit 5): note, target note (transposed, no rest check),
  mandatory duration. The period moves linearly:
  `from + (to - from) * elapsed / (duration - 1)` using low bytes only.
- raw mode (flag bit 3): `hi lo [hi2 lo2] duration` raw periods.
- Commands (argument bytes after the command):

| cmd | args | meaning |
|-----|------|---------|
| E0 | nn | duration multiplier (unused) |
| E1 | nn | volume 0-15 |
| E2 | | end: stop channel, priority 0, unmute borrowed voices |
| E3 | nn | noise control `$E0\|nn`; rate 3 = periods go to tone 2, else fixed noise |
| E4 | nn | volume envelope |
| E5 | llhh | jump |
| E6/E7 | | slide on/off (unused) |
| E8/E9 | | raw periods on/off (unused) |
| EA | llhh | call (channel-local stack) |
| EB | | return |
| EC | ii nn llhh | loop: counter ii ($17+ii) loaded with nn when 0; jump while non-zero after decrement (body plays nn times) |
| ED | nn | pitch envelope (unused) |
| EE | nn | transpose += nn |
| EF | | stop and clear v_soundSoftwareChannelSevenState (unused) |
| F0 | nn | v_soundBattleSoundFlags = nn, then E2 |
| F1 | | switch music to the underwater song unless v_level = 16 (splash effect) |

Envelopes: 4-bit steps, high nibble first, one per frame, restarted by each
note. In a high-nibble position a byte `$00` restarts, `$01` holds the
previous step, `$02` ends (volume: silence; pitch: keep last period),
`$03 nn` jumps to step nn. Volume step s: loudness = volume + s - 15 (min 0),
attenuation = 15 - loudness. Pitch step s: period + (15 - s).

Sounds ($81 + index; T0/T1/T2 = tone voices, N = noise; /xx = header flags):

| req | name | header | starter | channels |
|-----|------|--------|---------|----------|
| $81 | intro | $9ECD | handleSong | T0 T1 T2 N |
| $82 | base song | $9F81 | handleSong | T0 T1 T2 N |
| $83 | underwater | $A3BD | handleSong | T0 T1 T2 N |
| $84 | castle | $A57D | handleSong | T0 T1 T2 N |
| $85 | bike | $A757 | handleSong | T0 T1 T2 N |
| $86 | level starting | $A8E0 | handleSong | T0 T1 T2 |
| $87 | janken music | $A937 | handleSong | T0 T1 T2 N |
| $88 | peticopter | $AAD1 | handleSong | T0 T1 T2 N |
| $89 | dead | $AC56 | handleSong | T0 T1 T2 (/A0: slide) |
| $8A | punch | $AC81 | 9A7A: SFX2, prio 0 | T2/A8 |
| $8B | smoke puff | $AC9B | 9A7A: SFX2, prio 0 | N/A8 |
| $8C | block | $ACB2 | 9A7A: SFX2, prio 0 | N |
| $8D | ? | $ACC9 | 99F9: SFX1, prio 0 | T1 T2 |
| $8E | coins | $ACE1 | 99FC: SFX1, prio $60 | T1/88 T2/88 |
| $8F | power-up | $AD02 | 99FC: SFX1, prio $60 | T1 T2 |
| $90 | ? | $AD1E | 9A7D: SFX2, prio $60 | T2/A0 |
| $91 | jump | $AD2C | 9A7A: SFX2, prio 0 | T2/A8 |
| $92 | splash | $AD46 | 9A00: SFX1, prio $70 | T1/A8 N/A8 (ends with F1 E2) |
| $93 | battle lost | $AD8D | 9A8B: SFX2, current prio | T2/A8 |
| $94 | text box | $ADA7 | 9A8B: SFX2, current prio | T2 (loops until $B1) |
| $95 | boss defeated | $ADCC | 9A04: SFX1, prio $20 | T1/A8 T2/A8 |
| $96 | ? | $AE59 | 9A6D: SFX3 loop | T2/A8 |
| $97 | merman bubbles | $AE7F | 9A85: SFX2, prio $30 | T2/A8 |
| $98 | monkey leaf | $AEA3 | 9A85: SFX2, prio $30 | T2/A8 |
| $99 | ? | $AEB3 | 9A47: SFX2 noise, prio $40 | N/A8 |
| $9A | ? | $AECC | 9A7A: SFX2, prio 0 | T2/88 |
| $9B | falling | $AEE3 | 9A1F: reset, SFX1, prio 0 | T1/A0 T2/A0 |
| $9C | ? | $AEFA | 9A81: SFX2, prio $70 | T2/A8 |
| $9D | ? | $AF14 | 9A60: SFX3 loop | T2/A8 |
| $9E | lightning | $AF36 | 9A24: SFX1, prio $70 | T1/A8 N/A8 |
| $9F | ? | $AF6D | 9A60: SFX3 loop | N |
| $A0 | ? | $AF84 | 9A24: SFX1, prio $70 | T1/A8 N/A8 |
| $A1 | ? | $AFBD | 9A7A: SFX2, prio 0 | T2 |
| $A2 | ? | $AFCA | 9A00: SFX1, prio $70 | T1/A8 T2/A8 |
| $A3 | star box | $AFE9 | 99F9: SFX1, prio 0 | T1/A8 T2/A8 |
| $A4 | shock wave | $B034 | 9A43: SFX2 noise, prio $60 | N/A8 |
| $A5 | ? | $B04D | 9A7D: SFX2, prio $60 | T2/A8 |
| $A6 | ? | $B062 | 9A89: SFX2, prio $20 | T2/88 |
| $A7 | ? | $B076 | 9A89: SFX2, prio $20 | T2/A8 |
| $A8 | bullet | $B090 | 9A7D: SFX2, prio $60 | T2/A8 |
| $A9 | ? | $B0AA | 9A89: SFX2, prio $20 | T2/88 |
| $AA | magic capsule A | $B0BE | 9A68: SFX3 loop | T2/A8 |
| $AB | magic capsule B | $B0DD | 9A68: SFX3 loop | T2/88 |
| $AC | boss head | $B0F4 | 9A85: SFX2, prio $30 | T2/A8 |
| $AD | janken count | $B11D | 9A7D: SFX2, prio $60 | T2/A8 (ends with F0) |
| $AE | janken throw | $B16F | 9A7D: SFX2, prio $60 | T2/A8 |
| $AF | game over | $B189 | handleSong | T0 T1 T2 |
| $B0 | ending | $B1D4 | handleSong | T0 T1 T2 N (base song data, slower) |
| $B1 | stop text box/jump | - | 99A3 | |
| $B2 | stop SFX3 loop | - | 99D3 | |
| $B3 | music fade-out | - | 99BE | |

## Quirks and original bugs (kept, marked `QUIRK` in the code)

1. `$B2` (`handler_LABEL_99D3_`): `ld a,$DF` is immediately overwritten by
   `ld a,$80`, so `$80` is sent to the PSG instead of "tone 2 volume off":
   it zeroes the low 4 bits of tone 0's period until music channel 1
   rewrites it. It also forces music channel 3 flags to exactly `$80` and
   channel 4 to `$C0` (fixed noise), even if those channels had ended; a
   channel restarted this way keeps counting from a finished note (its
   currentPlayDuration is already past noteDuration), so it holds its last
   note until the 16-bit counter wraps. `$B1` does the same to channel 3.
2. `$B1` (`_LABEL_99A3_`): when the last sound was neither the text box nor
   the jump, the request is not acknowledged: `v_soundControl` stays `$B1`
   and the check repeats every frame until the game requests another sound
   (common in play: 47372 compared calls).
3. Pitch envelope `$03 nn` (jump) does not re-read like the volume envelope:
   the jump frame uses `nn` itself as the step value and step `nn` is
   skipped. Only pitch envelope 4 ($B2D0) has a jump, and no sound uses it.
4. `reset` clears `$C111-$C1F5`, two bytes short of the end of SFX
   channel 3 ($C1F6-$C1F7, the first call-stack slot; harmless).
5. `realHandleSong` only initialises bytes $00-$0D of a channel: loop
   counters of an effect interrupted inside an EC loop survive into the
   next effect loaded on that channel (its loop then runs short).
6. The splash (`$92`) ends with `F1 E2`: F1 resets the whole engine (the
   splash's own channel is zeroed), then E2 silences the now-zeroed channel
   with `hardwareChannel + $10 | $0F = $1F`, a PSG *data* byte that lands in
   the register latched last (the noise control, just written with `$E4`).
7. E2 in a music channel (intro, level-starting jingle...) also resets
   `v_soundEffectPriority`, dropping the protection of an effect playing at
   that moment.
8. `realHandleSong` with a channel count of 0 would load 256 channels
   (djnz); not present in the data.
9. Slide maths use only low bytes (period difference, elapsed frames,
   duration), so slides longer than 255 frames or wider than 255 period
   units go wrong. A slide note lasting 1 frame divides 0 by 0, which
   `_LABEL_9EBA_` answers with `$FF`: the note is played at `from +/- $FF`.
   Neither case occurs in the data.
10. `multiplyWord`'s carry output is always 0; `_LABEL_9EBA_`'s incoming carry
    never affects its result (it is shifted into bit 7 of L and dropped by
    the final RLA).

## Open issues

- Names marked `?` in the sound table are unknown effects (not identified in
  the constants file).
- Suggested better names (table above) are only used in comments: the
  `LIFTED(...)` names must stay the generated ones.
