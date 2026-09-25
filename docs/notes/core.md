# Core module: bank-0 engine services

Lifted code: `engine/src/game/core/`

| file | contents |
|---|---|
| `core.h` | constants shared by the module (ports, VDP layout, game states, `v_interruptFlags` / `v_inputFlags` bits, ROM table addresses, entity slots and flags, Alex action states), two tiny helpers, the `CORE_LIFT_FRAME_ROUTINES` switch |
| `interrupts.c` | rst $18/$20 dispatchers, VBlank handler, pause NMI, frame wait, invincibility timer |
| `video.c` | VDP init, display on/off, scroll reset, sprite upload, 1bpp/RLE/mirrored tile loaders, dead helpers |
| `input.c` | start-up delay, keyboard detection, controller reading |
| `score.c` | BCD score/money arithmetic and drawing, high score, entity points, the unused division |
| `entities.c` | entity update loop, movement/screen wrapping, RAM sprite table, animation, clear/spawn |
| `collision.c` | terrain lookups ($7C82-$7CB4), hitbox collision tests, Alex attack/damage dispatch |

## Routine status

"shadow" = compared calls in the standard shadow harness (`build-core/shadow`,
`--depth 12`, final scenario set, see Verification), all with 0 mismatches.
"fuzz" = extra compared calls with random inputs taken from live machine
states (private harness, see Verification), all with 0 mismatches.

| addr | routine | file | status | shadow | frames harness | fuzz | notes |
|---|---|---|---|---:|---:|---:|---|
| 0000 | start | - | generated | - | - | - | generated: sets SP, endless main loop |
| 001B | jumpToAthPointerIfBit7 | interrupts.c | lifted | 1180824 | 1181936 | 211296 |  |
| 0020 | jumpToAthPointer | interrupts.c | lifted | - | 7318789 | - | frame routine (see below) |
| 0021 | jumpToPointerAtA | interrupts.c | lifted | - | 7332181 | - | frame routine (see below) |
| 0038 | handleInterruptEntrypoint | interrupts.c | lifted | 315595 | 338642 | 99588 |  |
| 0066 | handlePauseInterrupt | interrupts.c | lifted | 137 | 252 | 10358 |  |
| 009F | reset | - | generated | - | - | - | generated: sets SP, endless main loop, soft-reset target |
| 00C0 | handleInterrupt | interrupts.c | lifted | 631190 | 677284 | 199176 |  |
| 01D6 | load1bppTiles | video.c | lifted | 64 | 60 | 31018 |  |
| 01F7 | updateSprites | video.c | lifted | 1257772 | 1336136 | 404848 |  |
| 0208 | updateSprites@oddUpdate | video.c | lifted | 1096052 | 1151120 | 356415 |  |
| 0212 | updateSprites@oddYLoop | video.c | lifted | 1096052 | 1151120 | 366751 |  |
| 026B | initVDPRegisters | video.c | lifted | 46 | 9 | 10347 |  |
| 0293 | decompressTilesToVram | video.c | lifted | 915 | 1191 | 10534 |  |
| 02A0 | decompressTilesToVram@bitplane | video.c | lifted | 3660 | 4764 | 52472 |  |
| 02C5 | copyMirroredTilesToVramAtCurrentAddress | video.c | lifted | 400 | 762 | 10431 |  |
| 02D7 | clearSprites | video.c | lifted | 0 | 0 | 10336 | dead code |
| 02E6 | waitForInterrupt | interrupts.c | lifted | - | 169321 | - | frame routine (see below) |
| 02EF | disableDisplay | video.c | lifted | 515 | 327 | 20784 |  |
| 02F6 | enableDisplay | video.c | lifted | 533 | 324 | 10454 |  |
| 02FB | sub_02FB | video.c | lifted | 1048 | 651 | 41574 |  |
| 0303 | clearScroll | video.c | lifted | 63 | 51 | 10357 |  |
| 0311 | clearVDPTablesAndDisableScreen | video.c | lifted | 315 | 243 | 10410 |  |
| 0341 | sleepOneSecond | input.c | lifted | 46 | 9 | 10347 |  |
| 0343 | sleepTenthsOfSecond | input.c | lifted | 452 | 249 | 31106 |  |
| 0350 | configurePPI | input.c | lifted | 46 | 9 | 10347 |  |
| 0367 | readInput | input.c | lifted | 1262380 | 1354568 | 408688 |  |
| 03CF | takeMoney | score.c | lifted | 92 | 414 | 10362 |  |
| 03ED | addScore | score.c | lifted | 92 | 216 | 20700 |  |
| 040B | sumBCD | score.c | lifted | 112 | 126 | 30239 |  |
| 041C | subtractBCD | score.c | lifted | 5 | 12 | 10336 |  |
| 042D | subtractBCDToA | score.c | lifted | 13 | 12 | 10336 |  |
| 043B | updateHighScore | score.c | lifted | 57 | 42 | 10353 |  |
| 0454 | drawThreeBcdBytes | score.c | lifted | 32786 | 43761 | 10361 |  |
| 0456 | drawBCDDigits | score.c | lifted | 65632 | 87579 | 31068 |  |
| 04A1 | fillRegisters | video.c | lifted | 0 | 0 | 51548 | dead code (only from unusedCodeB) |
| 04AA | unusedCodeB | video.c | lifted | 0 | 0 | 10336 | dead code |
| 04BB | unusedCodeC | video.c | lifted | 0 | 0 | 10336 | dead code |
| 0759 | divideHLByE | score.c | lifted | 0 | 0 | 10336 | dead code |
| 09D9 | clearEntities | entities.c | lifted | 191 | 120 | 10387 |  |
| 264F | updateInvincibility | interrupts.c | lifted | 3057636 | 5902520 | 706120 |  |
| 2694 | updateEntities | entities.c | lifted | 299704 | 151502 | 54739 |  |
| 26D7 | updateEntitySprites | entities.c | lifted | 957019 | 502455 | 177522 |  |
| 273A | _LABEL_273A_ | entities.c | lifted | 957019 | 502455 | 177522 |  |
| 278A | destroyCurrentEntity | entities.c | lifted | 6882 | 3885 | 13967 |  |
| 278D | clearEntity | entities.c | lifted | 12926 | 8727 | 335961 |  |
| 27D0 | _LABEL_27D0_ | entities.c | lifted | 957019 | 502455 | 177522 |  |
| 280E | handleEntityAnimation | entities.c | lifted | 760440 | 1337472 | 186044 |  |
| 5761 | earnEntityPoints | score.c | lifted | 84 | 216 | 10362 |  |
| 5B89 | spawnEntityAt | entities.c | lifted | 80 | 6318 | 10368 |  |
| 7C82 | _LABEL_7C89_ | collision.c | lifted | 170 | 90 | 10366 |  |
| 7C8D | _LABEL_7C94_ | collision.c | lifted | 214814 | 845946 | 42002 |  |
| 7C9C | _LABEL_7CA3_ | collision.c | lifted | 614063 | 1906344 | 150073 |  |
| 7CB5 | _LABEL_7CBC_ | collision.c | lifted | 15700 | 0 | 24608 |  |
| 7CBB | checkEntityCollision | collision.c | lifted | 1210348 | 5056980 | 282045 |  |
| 7CDF | checkEntityCollisionSub_LABEL_7CE6_ | collision.c | lifted | 1298862 | 5311842 | 270204 |  |
| 7D04 | isAlexAttackingEntity | collision.c | lifted | 553458 | 994878 | 100438 |  |
| 7D31 | checkAlexPunchHit | collision.c | lifted | 973508 | 3575100 | 184644 |  |
| 7D5A | _LABEL_7D61_ | collision.c | lifted | 15700 | 0 | 14272 |  |
| 7D67 | _LABEL_7D6E_ | collision.c | lifted | 31868 | 0 | 12380 |  |
| 7D7D | _LABEL_7D84_ | collision.c | lifted | 168 | 0 | 12268 |  |
| 7D84 | _LABEL_7D8B_ | collision.c | lifted | 48820 | 204876 | 19946 |  |
| 7D8B | _LABEL_7D92_ | collision.c | lifted | 36852 | 210708 | 19382 |  |
| 7D92 | tryToKillAlexIfColliding | collision.c | lifted | 607766 | 1024116 | 100540 |  |
| 7DB5 | killAlexIfColliding | collision.c | lifted | 898880 | 4602744 | 161890 |  |
| 7DC1 | doNotKillAlex | collision.c | lifted | 177884 | 721332 | 38267 |  |

Totals: 64 of 66 routines lifted (all but `start` and `reset`), 0 mismatches
in every harness. The "shadow" column comes from the standard harness, where
the three frame routines are not compiled; they are compared in the "frames
harness" column. The dead routines are compared only by the fuzzer.

## Special cases

**`start` ($0000) and `reset` ($009F): left generated.** They set SP and
contain the endless main loop (`reset` is also the `longjmp` target of
`rt_soft_reset`). A shadow session around them never ends, so no comparison
could ever complete, and nothing would be gained in readability over the
14 + 21 instructions.

**`jumpToAthPointer` ($0020), `jumpToPointerAtA` ($0021), `waitForInterrupt`
($02E6): lifted but compiled only in the game and lockstep builds by default.**
The shadow harness prints its report and stops only at a frame boundary seen
at shadow depth 0. `rt_wait_vblank` is only reached through
`waitForInterrupt`, and the main loop runs every game state through
`rst $20` (`jumpToAthPointer` -> `jumpToPointerAtA`). With any of these three
registered, every frame boundary is inside a session, so the standard harness
never ends. `core.h` therefore defines `CORE_LIFT_FRAME_ROUTINES` as 0 in
shadow builds unless `CORE_SHADOW_FRAME_ROUTINES` is defined. They were
verified with a private copy of the harness that stops at the frame limit
regardless of depth (`--stop-anywhere`), and by relaxed lockstep (they run in
every lockstep build). If the harness learns to stop inside sessions, remove
the `#if` in `core.h`.

**Dead code (no reference anywhere in the ROM):** `clearSprites` ($02D7),
`unusedCodeB` ($04AA), `unusedCodeC` ($04BB), `divideHLByE` ($0759), and
`fillRegisters` ($04A1), which only `unusedCodeB` calls. They are lifted and
verified only by the fuzzer, since the game never calls them.

**`handleInterrupt` ($00C0)** saves the interrupted registers in a C local,
swaps the register sets like the original (`EXX` / `EX AF,AF'`) so the
routines it calls see the same register values, and restores everything
(including `EI`) on exit. The mapper slot 2 is saved and restored as in the
original. It does not push the registers on the emulated stack (the area
below SP is dead; the relaxed lockstep ignores it).

**Tail calls kept:** `jumpToAthPointerIfBit7`, `isAlexAttackingEntity` and
`tryToKillAlexIfColliding` jump through the original rst $20 dispatcher
(`f_jumpToAthPointer`) so the targets get the exact registers and flags;
`updateEntities` calls entity updaters the same way (their `in:` lists include
B, C and the carry). `_LABEL_7C89_` tail-calls `sub_7C56` (another module).

## Meanings discovered

Entity fields (`Entity` in `game/ram.h`):

- `flags` (+$01): bit 0 = initialised (cleared by `spawnEntityAt`, set by the
  updater's init); bit 1 = destroyed instead of wrapping when it leaves the
  screen (`_LABEL_27D0_`/`_LABEL_273A_`); bit 7 = hit: set by the collision
  tests on the entity IX (and by some callers on IY), read by the updaters
  (Alex dies on his next update). Bit 4 is used by entities $16/$17 only.
- `isOffScreenFlags` (+$09/$0A): the entity's screen offset from the visible
  one. Low byte horizontal: +1 when it wraps past the left edge, -1 past the
  right edge. High byte vertical: -1 ($FF) above, +1 below. Y wraps modulo
  $C0 (192 lines). An entity is drawn only at 0/0, or at $FF00 (screen above)
  with Y >= $A8.
- `unknown2` (+$13): hitbox offset. Copied from byte 1 of the sprite
  descriptor by `updateEntitySprites`; indexes the 4-byte hitboxes
  [X offset, width, Y offset, height] at $91D0 (bank 2). Alex's fist box
  follows his body box (+4).
- `battleDecision` (+$17): for the item drops in slots 27/28, their
  remaining lifetime. `spawnEntityAt` replaces the one with less time left
  when both slots are busy.
- `unknown6` (+$18): random 0-7 set by `spawnEntityAt` (`LD A,R`).
- `animationTimer` (+$05) is set to 1 by `clearEntity`, so the first
  animation step happens on the next update.
- Alex `unknown8` (+$1C) bit 0: punching (`ALEX_UKNW8_PUNCH`); only then does
  `checkAlexPunchHit` test.

Data formats:

- Sprite descriptor: [count, hitbox offset, count Y offsets, count (signed X
  offset, tile) pairs]. The Y of a sprite that would be $D0 (end of the
  sprite list) becomes $CF. A sprite pushed off the left/right edge by its
  offset is hidden with Y = $E0.
- Animation descriptor (`handleEntityAnimation`): [frame count, one sprite
  descriptor pointer per frame].
- RAM sprite table `v_tempSprites` ($C700): 64 Y bytes, $C740-$C77F unused,
  64 (X, tile) pairs at $C780. Entities fill it from $C706; the first 6 slots
  belong to the states. `v_spriteTerminatorPointer` = address of the Y byte
  after the last sprite, clamped to $C73F at the end of `updateEntities`.
- RLE tiles (`decompressTilesToVram`): 4 bitplanes one after the other, each
  a list of blocks [n | $80, n raw bytes] or [n, 1 byte repeated n times],
  ended by 0; header $80 means 256 raw bytes.

RAM variables:

- `v_interruptFlags` ($C008): bit 0 = upload the sprite table, bit 3 = run
  the game state's interrupt handler (rst $18 on `v_gameState`, table $0127).
  Bits 1-2 are rotated out but not used. The handler clears the byte, which
  is what `waitForInterrupt` polls. Callers pass 1 or 9.
- `v_inputFlags` ($C005): bit 0 = SC-3000 keyboard detected (read instead of
  the pad by `readInput`); bit 5 = demo playing (set by `updateDemoState`,
  cleared when it ends; `addScore`/`takeMoney` do nothing then). Bit 1 is
  kept by `configurePPI` and the demo, meaning unknown.
- `v_levelData_C0B0` = high (pixel) byte of `v_horizontalScroll`
  ($C0AF-$C0B0, 8.8); likewise $C0BE for `v_verticalScroll`.
- `v_resetButtonState` ($C096): last reset-button bit; reset fires on the
  1 -> 0 edge.
- `v_alexActionState` ($C054), the item or vehicle in use: 0 normal,
  1 cane of flight (`ALEX_C054_STATE_1`, timed), 2 teleport powder
  (`ALEX_C054_INVINCIBLE`, timed, cannot be hurt), 3/4 magic capsule A/B,
  5 power bracelet, 6 telepathy ball (`ALEX_C054_UKN_0x06`), 7 motorcycle,
  8 boat, 9 peticopter. `updateInvincibility` times out states 1 and 2 (the
  name is a misnomer for state 1).
- `_RAM_C014_` in `updateInvincibility` is not RAM: `ld de,$C014` is the
  VDP command "CRAM write, entry $14" (Alex's clothes colour, restored to
  red $03).
- Score and money are stored divided by 10 (a "0" tile follows the six
  digits): score values 20..1000 = 200..10000 points, money bags 1/2 = 10/20
  baums, shop prices e.g. 20 = 200 baums (motorcycle), 50 = 500 (life).
- `takeMoney` actually adds money (collecting a bag).

Labels:

- `_LABEL_7C89_` ($7C82): tile attribute byte at name-table pixel (E, D),
  without horizontal scroll (vertical scroll added by `sub_7C56`).
- `_LABEL_7C94_` ($7C8D): move the attribute pointer HL along its row to
  pixel column C + E.
- `_LABEL_7CA3_` ($7C9C): attribute pointer at pixel row B + D (wrapping at
  224) in the column of L.
- `_LABEL_7CBC_` ($7CB5): `checkEntityCollision` that ignores an empty IY.
- `_LABEL_7D61_` ($7D5A) boat/peticopter shot (slot 2), `_LABEL_7D6E_` ($7D67)
  magic capsule A (slots 3 then 2), `_LABEL_7D84_` ($7D7D) magic capsule B
  (slot 4), `_LABEL_7D8B_` ($7D84) motorcycle (Alex himself), `_LABEL_7D92_`
  ($7D8B) power bracelet shock wave (slot 2): the per-action-state attack
  checkers of `isAlexAttackingEntity`.
- `_LABEL_27D0_` / `_LABEL_273A_`: horizontal / vertical move of entity IX
  with the frame's scrolling (x adds `v_horizontalScrollSpeed`, y subtracts
  `v_verticalScrollSpeed`).
- `sub_02FB`: write VDP register 1 (and its RAM copy).
- `updateSprites@oddUpdate` / `@oddYLoop`: the plain in-order upload; the
  "even" path is the flicker upload.

## Original bugs and quirks (kept, marked `QUIRK` where not obvious)

- `addScore`: when the score overflows past 999999 it is the **high score**
  that is set to 999999; the score keeps its wrapped value.
- `subtractBCD` / `subtractBCDToA` advance only C between bytes (the number
  must not cross a 256-byte page; `v_money` does not).
- Collision tests compute each bound with 8-bit adds/subtracts and test only
  the last carry, so boxes near the screen edges can wrap. The fist box of
  `checkAlexPunchHit` is not clamped at 255 like `checkEntityCollision`'s.
- `killAlexIfColliding`: `checkEntityCollision` also marks the enemy (IX) as
  hit, not only Alex.
- `updateEntities` still moves a slot that `_LABEL_27D0_` has just destroyed
  (`_LABEL_273A_` runs on the cleared slot). This is harmless because the slot
  is free.
- `updateEntitySprites` with more than 64 sprites writes Y bytes into $C740+
  and the X table, and (X, tile) pairs past $C7FF into the name-table copy at
  $C800, before `updateEntities` clamps the terminator.
- `updateSprites` derives the reversed pair count from the terminator
  address (same count as the Y bytes while it is <= $3F).
- `drawBCDDigits` takes the digits out by rotating the source byte in place
  with RLD (restored). A number that includes `v_nametableCopyFlags` itself
  would draw rotated attributes; ROM sources would repeat the high digit.
  All callers pass RAM. A value of 0 is drawn all blank.
- `clearEntity` advances only L (slots must not cross a page).
- `startGame` ($080C, not this module) zeroes `v_inputFlags`: keyboard input
  works only on the title screen and in the demo.
- `configurePPI` always clears the keyboard flag on a Master System: port
  $DE reads back the pad, not the value written. So the keyboard path of
  `readInput` is unreachable on this machine (exercised by script pokes and
  the fuzzer).
- The entity updater table ($2890) points to $0000 (`start`) for the unused
  types $59-$5F.

## Verification

All commands are run from `engine/`. The scenario scripts are in
`engine/tests/scenarios/` (replay them all with `tests/run_scenarios.sh`).

Build:
`make BUILD=build-core LIFTED_EXTRA="$(ls src/game/wip/core/*.c | tr '\n' ' ')" build-core/shadow build-core/lockstep`
(no warnings).

1. **Standard shadow harness** (`build-core/shadow ... --depth 12`), 47 runs,
   all `OK`, 0 mismatches:
   - `--mode idle --frames 3000`
   - `--mode play --seed 1..4 --frames 10000`
   - `--mode play --level N --lives --frames 8000` for N = 1..17
   - scripts (random play plus pokes, 6000 frames, `--lives`):
     `v_alexActionState` forced to 3/4/5/7/8/9 every 50 frames (all the
     attack checkers and damage handlers); the pause button every 250 frames
     (pause map, lives/score/money drawing); score and money forced to
     999999 (the overflow caps, reached on level 1); 26 item/effect entities
     spawned at once, with types $2B/$3C/$44/$4D/$38 (more than 64 sprites:
     the terminator clamp and flicker uploads)
   - `shop1.txt`/`shop2.txt`: shop state forced by poke, purchases with and
     without enough money (`subtractBCDToA`, `subtractBCD`, extra life,
     peticopter, motorcycle); `keyboard.txt`: `INPUT_FROM_KEYBOARD` forced on
     the title screen and in the demo (keyboard path of `readInput`)
2. **Frame routines** (private harness `harness_frames`, built with
   `-DCORE_SHADOW_FRAME_ROUTINES`, `--stop-anywhere --depth 12`), 9 runs (idle,
   3 play seeds, levels 5/12/16, shop script, pause script): all `OK`:
   jumpToAthPointer 7318789, jumpToPointerAtA 7332181, waitForInterrupt 169321
   calls, 0 mismatches.
3. **In-game fuzzer** (private harness `harness_fuzz --fuzz K --depth 12`, 11
   runs, idle/play/levels): every K frames it calls each core routine 4 times
   with random registers and randomised inputs (entity slots, hitboxes,
   positions, BCD numbers, sprite terminator, input flags...) from the live
   machine state, compares, and restores the state. About 500k compared calls,
   0 mismatches. It covers the dead code, and the paths that no scenario
   reaches: `updateEntitySprites` for an entity on the screen above whose
   sprite leaves by the side, `spawnEntityAt` with both item slots busy,
   `divideHLByE` with E = 0, and the keyboard rows with keys down. Data
   pointers exclude the stack page: there the original reads back the return
   addresses and pushed values it just wrote below SP, which C code does not
   push (no game caller does this).
4. **Relaxed lockstep** against the reference emulator
   (`build-core/lockstep ../original.sms --relaxed ...`), all identical:
   - `--mode play --seed 1 / 3 / 5 --frames 20000`, `--mode idle --frames 3000`
   - levels 1-17 (`--level N --lives --frames 8000`) and every script above:
     42 runs, plus the overflow script on levels 1, 2, 3 and 6.
   - `--cov` over these runs: every instruction of every lifted routine is
     executed, except the dead code, the three fuzz-only paths above, and
     `_LABEL_7D6E_` (reached by the shadow scripts on other levels). One
     exception is expected: `ld a,r` in `spawnEntityAt` is never marked,
     because lockstep substitutes that instruction.

Findings along the way: the fuzzer found that `drawBCDDigits` re-reads
`v_nametableCopyFlags` after each digit while the source byte is rotated in
place. The lifted version now mirrors the rotation. A 5th poke in a frame is
not recorded by the harness's input log (4 per frame), which produced one
spurious mismatch in a frame-spanning session. The replay of that call was
identical; the scripts now poke at most 3 bytes per frame (plus `--lives`).

## Open issues

- The three frame routines are not in the standard shadow report (see
  "Special cases"). They are verified by the private harness and by
  lockstep. If `tests/shadow.c` is changed to stop at the frame limit inside
  sessions, delete the `CORE_LIFT_FRAME_ROUTINES` switch in `core.h`.
- The reset-button path of `handleInterrupt` (`rt_soft_reset`) is not
  exercised: no scenario presses reset. It is a direct call, like the
  generated code.
- `updateEntities` relies on the entity updaters (other modules) through the
  original rst $20 dispatcher. It follows IX if an updater changes it, as the
  original does.
- `sub_7C56` ($7C56), which `_LABEL_7C89_` tail-calls, belongs to another
  module and is not lifted here.
