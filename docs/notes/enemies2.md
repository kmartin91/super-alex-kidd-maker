# Module enemies2: items, late-level enemies, story characters, janken opponents and bosses

Lifted code: `engine/src/game/enemies2/`

| file | contents |
|------|----------|
| `enemies2.h` | shared constants (entity slots, flags, types, sounds, messages), named RAM, small helpers |
| `items.c` | block debris, money bag, extra life, power bracelet, ghost, rice ball ($5901-$5C2E) |
| `enemies.c` | Namui bull, circular / patrolling flames and scorpions, lightning cloud, water leaper, walking and hopping monsters, static flames ($5C2F-$63F3 minus story) |
| `story.c` | prisoners Egle / Princess Lora, village elder, Saint Nurari, King High Stone, story items, bonus-level trigger ($5FAA-$62A7) |
| `battle.c` | janken helpers: Alex's positioning, opponent thinking, thought clouds, name/score marks, end of match, Janken's projectile ($71A9, $751E-$7982) |
| `bosses.c` | Gooseka, Chokkinna, Parplin dispatchers, head spawns, boss heads, Parplin's head path ($778F-$7C43) |
| `tilemap.c` | name-table attribute probes used for terrain checks ($7C44-$7C7F) |

Entity slots are numbered like the reference (`v_entities.1` = Alex = $C300,
`ENTITY_SLOT(n)` = $C300 + (n-1)*$20). Conventions: `Entity.flags` bit 0 =
initialized, bit 1 = destroy when leaving the screen (the engine does it in
`_LABEL_27D0_`/`_LABEL_273A_`; several updaters also use it as a private
"phase 2" flag), bit 7 = hit by Alex's punch (set by the collision check).

## Routine status

All 89 routines are lifted. "calls" is the number of shadow comparisons over
the final suite (`--depth 12`, see *Verification*), all with 0 mismatches.

| addr | routine | file | calls | mism. | exercised by (some scenarios) |
|------|---------|------|------:|------:|-------------------------------|
| $5901 | updateDebrisTopLeft | items.c | 4436 | 0 | L12, L14 random play |
| $5964 | updateDebris | items.c | 33808 | 0 | L12, L14 |
| $5985 | sub_5985 | items.c | 67872 | 0 | L12, L14 |
| $598F | updateDebrisBottomLeft | items.c | 4135 | 0 | L12, L14 |
| $59C1 | updateDebrisTopRight | items.c | 4339 | 0 | L12, L14 |
| $59F4 | updateDebrisBottomRight | items.c | 4170 | 0 | L12, L14 |
| $5A2A | updateMoneyBag | items.c | 3138 | 0 | L1, L12, L15; items_expire, items_offscr |
| $5A8F | updateLife | items.c | 488 | 0 | life, items_expire, items_offscr |
| $5ADF | updatePowerBracelet | items.c | 627 | 0 | L1, play seeds; items_expire, items_offscr |
| $5B30 | updateGhost | items.c | 1123 | 0 | L14, play_s1, king_noletter, items_offscr |
| $5BCA | updateRiceBall | items.c | 28623 | 0 | L5, L14, elder, lora |
| $5C20 | unused_LABEL_5C27_ | items.c | 0 | 0 | **unreachable** (no caller in the ROM) |
| $5C2F | updateEntity0x46 | enemies.c | 1201 | 0 | bull_hits, bull_kill, bull_turn |
| $5C99 | _LABEL_5CA0_ | enemies.c | 60 | 0 | bull_* |
| $5CA9 | updateEntity0x47 | enemies.c | 341 | 0 | bull_hits |
| $5CF0 | updateEntity0x48 | enemies.c | 509 | 0 | bull_left, bull_turn |
| $5D2F | updateEntity0x49 | enemies.c | 254 | 0 | bull_left |
| $5D74 | _LABEL_5D7B_ | enemies.c | 6 | 0 | bull_hits, bull_kill, bull_left |
| $5D8D | updateCircularFlame | enemies.c | 9958 | 0 | L12, flame_edge, flame_scroll |
| $5E0D | updateflameOrScorpionLeft | enemies.c | 64024 | 0 | L2, L10, L12 |
| $5E74 | updateflameOrScorpionRight | enemies.c | 27814 | 0 | L2, L10, L12 |
| $5EB3 | updateEntity0x40 | enemies.c | 729 | 0 | cloud |
| $5EFE | updateEntity0x41 | enemies.c | 357 | 0 | cloud |
| $5F45 | updateEntity0x42 | enemies.c | 2034 | 0 | L5, L9, leaper_punch |
| $5FAA | updateEntity0x51 | story.c | 3052 | 0 | egle, egle_done, lora, lora_scroll, items_offscr |
| $6077 | updateEntity0x50 | story.c | 1348 | 0 | elder |
| $60B5 | updateSaintNurari | story.c | 1685 | 0 | nurari |
| $60CD | _LABEL_60D4_ | story.c | 8 | 0 | egle, elder, lora, nurari |
| $6106 | updateEntity0x52 | story.c | 3502 | 0 | items, item_bonus, king_letter, nurari, items_offscr |
| $616F | updateEntity0x53 | story.c | 7830 | 0 | L14 random play, king_letter, king_noletter |
| $6279 | updateEntity0x4C | story.c | 10482 | 0 | L17 random play, bonus_touch |
| $62A8 | updateEntity0x54 | enemies.c | 8962 | 0 | L7, walker_wall, walker_punch |
| $6361 | updateEntity0x55 | enemies.c | 5805 | 0 | L5, L6, hopper_wall |
| $63F4 | updateEntity0x57 | enemies.c | 1204 | 0 | flame57 |
| $71A9 | updateBattleMakeAlexGetIntoPosition | battle.c | 4100 | 0 | L12, all battles, chok_scroll, goo_jump |
| $751E | _LABEL_7525_ | battle.c | 48 | 0 | jank_boss, jank_16 |
| $752E | _LABEL_7535_ | battle.c | 212 | 0 | jank_boss |
| $7548 | sub_7548 | battle.c | 44 | 0 | jank_boss |
| $755D | _LABEL_7564_ | battle.c | 96 | 0 | jank_boss |
| $7565 | _LABEL_756C_ | battle.c | 182 | 0 | jank_boss |
| $7581 | _LABEL_7588_ | battle.c | 70 | 0 | jank_boss |
| $758A | simulateOpponentChoosing_LABEL_7941_ | battle.c | 42990 | 0 | all battles |
| $75BF | drawThoughtClouds | battle.c | 36 | 0 | all battles (with and without Telepathy Ball) |
| $778F | updateGooseka | bosses.c | 13465 | 0 | goo_* |
| $77B7 | updateBattleBattleWonAndSetupNametablePatches | battle.c | 16 | 0 | goo_boss, goo_plain, goo_kill |
| $77C6 | updateGoosekaSpawnHead | bosses.c | 240 | 0 | goo_boss, goo_heads, goo_kill |
| $7804 | updateBattleDestroyWhenDefeated | battle.c | 24102 | 0 | goo_*, parp_* |
| $780F | updateChokkinna | bosses.c | 16981 | 0 | L12, chok_* |
| $7835 | updateChokkinnaSpawnHead | bosses.c | 320 | 0 | chok_boss, chok_heads, chok_kill |
| $7868 | updateChokkinnaCastSpells | bosses.c | 12164 | 0 | chok_* |
| $789E | updateChokkinnaSpell | bosses.c | 6386 | 0 | chok_* |
| $78A1 | updateParplin | bosses.c | 12499 | 0 | parp_* |
| $78C7 | updateBattleBattleWon | battle.c | 72 | 0 | chok_*, parp_*, goo_* (boss and no-boss) |
| $78EA | updateParplinSpawnHead | bosses.c | 240 | 0 | parp_boss, parp_heads, parp_kill |
| $791D | prepareForBattle | battle.c | 36 | 0 | all battles |
| $793A | drawAlexName_LABEL_7941_ | battle.c | 36 | 0 | all battles |
| $7982 | updateEntity0x0C | battle.c | 23872 | 0 | all battles |
| $799A | updateGoosekaHead | bosses.c | 4787 | 0 | goo_boss, goo_heads, goo_kill |
| $79AA | updateGoosekaHeadState0 | bosses.c | 204 | 0 | goo_* |
| $79C9 | updateGoosekaHeadState1 | bosses.c | 4426 | 0 | goo_* |
| $79E9 | updateGoosekaHeadState2 | bosses.c | 4344 | 0 | goo_* |
| $7A09 | _LABEL_7A10_ | bosses.c | 17540 | 0 | goo_* |
| $7A39 | _LABEL_7A40_ | bosses.c | 80 | 0 | goo_heads, goo_kill, chok_hit1 |
| $7A3A | _LABEL_7A41_ | bosses.c | 184 | 0 | goo_heads, goo_kill, chok_heads, chok_kill, chok_hit1 |
| $7A72 | updateBattleHeadState3 | bosses.c | 1116 | 0 | goo_heads, chok_heads |
| $7A89 | updateChokkinnaHead | bosses.c | 6049 | 0 | chok_* |
| $7A99 | updateChokkinnaHeadState0 | bosses.c | 848 | 0 | chok_* |
| $7ABB | updateChokkinnaHeadState1 | bosses.c | 2404 | 0 | chok_* |
| $7AEC | updateChokkinnaHeadState2 | bosses.c | 8330 | 0 | chok_* |
| $7B11 | _LABEL_7B18_ | bosses.c | 4808 | 0 | chok_* |
| $7B2E | updateParplinHead | bosses.c | 7171 | 0 | parp_boss, parp_heads, parp_kill |
| $7B71 | _LABEL_7B78_ | bosses.c | 1206 | 0 | parp_* |
| $7B81 | _LABEL_7B88_ | bosses.c | 2368 | 0 | parp_* |
| $7B92 | _LABEL_7B99_ | bosses.c | 1184 | 0 | parp_* |
| $7BA3 | _LABEL_7BAA_ | bosses.c | 2166 | 0 | parp_* |
| $7BAD | _LABEL_7BB4_ | bosses.c | 2304 | 0 | parp_* |
| $7BBE | _LABEL_7BC5_ | bosses.c | 1152 | 0 | parp_* |
| $7BCF | _LABEL_7BD6_ | bosses.c | 72 | 0 | parp_* |
| $7BE5 | _LABEL_7BEC_ | bosses.c | 3888 | 0 | parp_* |
| $7BFC | _LABEL_7C03_ | bosses.c | 6976 | 0 | parp_* |
| $7C09 | _LABEL_7C10_ | bosses.c | 7040 | 0 | parp_* |
| $7C14 | _LABEL_7C1B_ | bosses.c | 4736 | 0 | parp_* |
| $7C21 | _LABEL_7C28_ | bosses.c | 2368 | 0 | parp_* |
| $7C2C | _LABEL_7C33_ | bosses.c | 2304 | 0 | parp_* |
| $7C39 | _LABEL_7C40_ | bosses.c | 4608 | 0 | parp_* |
| $7C44 | getNearEntityTileAttrWithOffset | tilemap.c | 1340930 | 0 | every level |
| $7C48 | _LABEL_7C4F_ | tilemap.c | 2684754 | 0 | every level |
| $7C56 | sub_7C56 | tilemap.c | 5438975 | 0 | every level |
| $7C73 | _LABEL_7C7A_ | tilemap.c | 34650 | 0 | L12, L17 |

Line coverage of the lifted code (clang source coverage of a private
instrumented shadow build over the whole suite): 100% of the lines except
the dead `unused_LABEL_5C27_`. The only untaken branch sides left are the
"callee returned to our caller's caller" exits of `CALL_ROUTINE` /
`call_jump_table` (never happen for these callees), the debris
`is_offscreen` test (the engine destroys debris before it can be off screen),
and two conditions whose other side the game geometry never produces
(Parplin's head is always below y=$88 when it enters segment 8; the lightning
cloud's last frame always coincides with animation timer 1).

### Unreachable

* `unused_LABEL_5C27_` ($5C20): puts a rice ball in slot 27 at (E, D). No
  call, jump or pointer table in the ROM references $5C20 (the recompiler
  lists it only because the reference labels it). Lifted for completeness,
  cannot be exercised.

## Behaviours

### Items ($5901-$5C2E)

* **Block debris** ($38-$3B). Breaking a block spawns $38 (top-left piece) in
  slot 23 with the block kind in `unknown6`; on its first update it spawns
  $39/$3A/$3B in slots 24-26, which place themselves at +0/+8 px from it.
  Pieces fly at 0.5 px/frame sideways and 0.5 px/frame up, fall with gravity
  $30/256 px/frame², animate with the animation of their block kind
  (`_DATA_5D8C_` at $5D85), and vanish at x >= $F8.
* **Money bag** ($3C): block kind (`unknown6`) < 4: big bag (sprite $8359,
  `takeMoney` index 3 in `unknown5`), else small bag ($8367, index 0).
* **Extra life** ($4D), **power bracelet** ($4E): picked up on contact
  (+1 life in BCD / `v_hasPowerBracelet` + counter). All three items vanish
  when off screen or after $F0 frames (countdown in `battleDecision`).
* **Ghost** ($4F): waits $80 frames (`unknown6`), then chases Alex with
  `getVelocitiesToPursuitAlex`, facing him; flags bit 1 = chasing.
* **Rice ball** ($44): touching it scores 1000 and sets
  `STATE_LEVEL_COMPLETED`.

### Enemies ($5C2F-$63F3)

* **Namui bull** ($46 walking left, $48 walking right, $47/$49 knocked back;
  identified from a screenshot in the Village of Namui). Turns at x = $18 and
  $D8. Each punch: smoke-puff sound, knock-back (type $47/$49, direction
  reversed) for a number of frames taken from the table at $5D75 indexed by
  the hit count, then it walks on in its original direction, faster by the
  table's second byte. Eighth punch: `_LABEL_5D7B_` (points, fanfare,
  generic defeated updater $43 with `unknown1` = 0). While it exists it keeps
  `v_storyEventCounter` ($C07F) at 1, which keeps the village elder silent.
* **Circular flame** ($3D): orbits its spawn point (radius $20, angle
  +2/frame, position computed by `unknownAnimate`); the orbit centre follows
  the scroll; once a screen to the left, it lets the engine destroy it.
* **Flame / scorpion** ($3E left, $3F right; data != 0: flame, immune to
  punches; data 0: scorpion): patrols at 0.5 px/frame, turning at walls
  (tile at +9 px down on its front edge is solid) and at platform edges (the
  tile 8 px lower is not solid).
* **Lightning cloud** ($40/$41, Bingoo Lowland): drifts left 16 frames at
  1 px/frame, then brakes by 8/256 px per frame; when stopped it plays the
  lightning sound and the strike animation ($85E9) until frame $13, then
  starts over.
* **Water leaper** ($42, Lake Fathom part 2): jumps from the bottom of the
  screen (up 1 px/frame, left 0.5 px/frame) with a random gravity of 2 or 4
  (`ld a,r`), falling sprite once it moves down.
* **Walking monster** ($54, Mt Kave): walks towards where Alex was when it
  appeared (0.375 px/frame), turns at walls (`isEntityCollidingWithTerrainAtOffset`
  on its front side, `unknown6` = 2 or $0E), falls with a growing speed
  (`battleDecision:unknown5`, +$10/frame) once its trailing edge leaves the
  ground.
* **Hopping monster** ($55, Village of Namui): hops (jump speed -1 px/frame,
  gravity $10) left/right, bouncing on the floor, turning at walls.
* **Static flame** ($57): harmful animated flame.

### Story ($5FAA-$62A7)

* **Prisoner** ($51). data 1 = Egle (Radactian Castle): once the two cell
  blocks are broken (`v_storyEventCounter` == 2, counted by the "increment"
  kind of name-table changer $4B) and $40 frames passed, 1000 points, text
  $0F; clearing `_RAM_D802_` bit 0 lets the Letter to Nibana (story item 2
  placed in the same level, pre-flagged as collected by the level-completion
  code when `v_level` is $0A) appear: Egle "gives" the letter.
  data 0 = Princess Lora (Janken's castle): as soon as Alex walks on a still
  screen, her cell metatile is replaced (name-table change request, metatile
  $8B5D of bank 5 at $CE84), 1000 points, text $14, and the rice ball that
  ends the game level is spawned in slot 23 at (x $80, y $80).
* **Village elder** ($50): when `v_storyEventCounter` is 0: moves to y $88,
  spawns the rice ball in slot 6 at (x $98, y $60) and talks (text $0E);
  next frame: star-box jingle.
* **Saint Nurari** ($45): talks (text $0D) once the screen stopped scrolling
  (`v_scrollFlags` == 0), then gives the Sunstone Medallion (story item 8 in
  slot 27 at (x $72, y $70)).
* **King High Stone** ($53, invisible): once the screen stopped scrolling,
  text $10 (with the Letter to Nibana) or $11 (without), then spawns in slot 27
  the Hirotta Stone (story item 3 at (x $58, y $88)) or a ghost at (x $D8, y $30).
* **Story item** ($52, index in `data`): sprite from `_DATA_6422_` ($641B),
  per-level "collected" flag from `_DATA_644A_` ($6443, pointers to
  $D800-$D807), owned flag from `_DATA_6436_` ($642F). Items: 0 bonus-level
  entrance (sets `STATE_BONUS_LEVEL`), 1 Telepathy Ball, 2 Letter to Nibana,
  3 Hirotta Stone, 4 Moonstone Medallion, 5 extra life, 6 Power Bracelet,
  7 Teleport Powder, 8-9 Sunstone Medallion.
* **Bonus-level entrance** ($4C, invisible): copies its data into
  `v_storyEventCounter` at set-up, starts the bonus level on contact.
* `_LABEL_60D4_` ($60CD): common "character talks" helper: sets the
  entity's initialized flag, message A, `v_shouldShowNuraiOrOldMan` = 1,
  `STATE_TEXT_BOX`, and records the entity (IX) and its talking animation
  (HL) for the text-box state.

### Janken battles ($71A9, $751E-$7982)

The opponent (Gooseka $1D, Chokkinna $1E, Parplin $1F, Janken $1C) sits in
slot 6; `data >> 1` selects the opponent settings (0 Janken, 1 Gooseka,
2 Chokkinna, 3 Parplin), `data` bit 0 = a boss fight follows a won match.
Level placement: Gooseka L2 (data 2) and L11 (3), Chokkinna L7 (4) and L12
(5), Parplin L10 (6) and L15 (7), Janken L16 (1).

* `updateBattleMakeAlexGetIntoPosition` (state 1): waits for the opponent on
  screen, no scrolling (`v_scrollFlags & $0F`), Alex on the ground (state <
  3), then sets Alex to "go to battle position" (Alex walks to x = $28).
* `simulateOpponentChoosing` ($758A): while the thinking time (slot 6
  `unknown11`, $FF during the dance, $46 while counting) lasts, the opponent
  takes the next throw of its 32-entry decision list every "delay + 1"
  frames (delay per opponent data in `_DATA_7763_` at $775C, countdown in
  `unknown6`). With the Telepathy Ball, the opponent's thought cloud (slot 27)
  shows it.
* `drawThoughtClouds`: saves the name-table copy under the clouds ($CA08,
  $EC bytes) into `v_nametableCopy`, patches the cloud tiles, spawns the
  throw previews ($0B) in slots 28 (Alex) and 27 (opponent, only with the
  Telepathy Ball).
* `drawAlexName`: saves the name row ($C908, 46 bytes, into $C260), writes
  "ALEX" (the opponent's name is only drawn in the Japanese build), copies the
  11-byte score-marks sprite template ($7764) to $C2A0 and spawns its entity
  ($0C) in slot 23 (`updateEntity0x0C` shows it at (x $28, y $30)).
* `prepareForBattle`: clears slots 7-28, resets the sound engine, loads the
  battle tiles (bank 4, $98E9) to VRAM $3000 (control word $7000) with
  interrupts disabled.
* `updateBattleBattleWon` (state $0B): after the text box: Alex idle, battle
  entities removed; no boss: `killOpponent`; boss: state $0C, `unknown5` =
  $28, text $0B ("boss fight"). Gooseka's variant also queues 2 name-table
  patches (`goosekaNametableChanges` $7787 -> `_RAM_C219_`, count
  `_RAM_C218_`).
* **Janken's projectile** ($19 states 1-5): pause, then bob (vertical speed
  +-$40/frame between -2 and +2 px/frame) with pauses growing by 2 frames per
  half-cycle (`battleDecision`), countdown in `unknown5`.

### Bosses ($778F-$7C43)

* `updateGooseka/Chokkinna/Parplin`: dispatch on slot 6 state through the
  tables at $7797 / $7817 / $78A9 (states 0-$0C are the janken states, then
  the boss states lifted here).
* Spawn head: the body shows a "transforming" sprite for `unknown5` ($28)
  frames, then the headless sprite, and the head ($0D/$0E/$0F) appears in
  slot 7 at the body position, with the boss-head sound.
* Body during the fight (`updateBattleDestroyWhenDefeated`,
  `updateChokkinnaCastSpells`): harmful; defeated (`killOpponent`) when the
  head is gone. Chokkinna keeps one spell ($1A, slot 8, 1 px/frame left,
  `updateChokkinnaSpell` = contact damage only) in flight.
* Heads need 3 punches (`unknown1`), then `killEnemy`.
  * Gooseka: rises to 32 px above the body, then bobs around that height
    (+-$10/frame² acceleration) while sweeping between x $11 and $E0
    (`unknown3` bit 1 = moving right).
  * Chokkinna: bounces in arcs over y = $28 (jump speed $FB34, $5E/$1E
    deceleration), drifting sideways, reversing each bounce.
  * Gooseka/Chokkinna, punched (`_LABEL_7A41_`): stunned 60 frames in state
    3 (`updateBattleHeadState3`), state and speeds saved in `unknown6`,
    `unknown10-11`, `unknown8-9`.
  * Parplin: follows a 10-segment loop (`_DATA_7B64_` at $7B5D, curves built
    from the +-$20 speed helpers $7BFC-$7C39, which report "speed reached
    0" in Z); a punch makes it invulnerable for 30 frames (`stateTimer`).

### Terrain probes ($7C44-$7C7F)

`getNearEntityTileAttrWithOffset` (entity x + E, entity y + D),
`_LABEL_7C4F_` (x in A), `_LABEL_7C7A_` (level position E, D) and `sub_7C56`
return the attribute byte of the name-table entry displayed there, read from
the RAM name table $C800 (32x28 entries of 2 bytes; y wrapped to 224 lines).
Outputs: A = attribute (bit 7 solid, bit 5 shop door), HL = its address,
B = wrapped y, C = screen x, S/Z/P from `y & $F8`.

## Unknown fields and RAM (meanings found)

| RAM | name in code | meaning |
|-----|--------------|---------|
| $C07F `_RAM_C07F_` | `v_storyEventCounter` | story event counter: blocks broken in Egle's cell ($4B increment kind), 1 while the Namui bull lives, reset by name-table changers, $4C sets it to its data |
| $D800-$D807 | `v_collectedItemFlags` | per-level "already collected / done" flags of story items and prisoners (cleared at level completion; $D802 pre-set before level 11) |
| $C218 / $C219 | `v_battleNametablePatchCount` / `v_battleNametablePatches` | name-table patches applied after a battle (updateBattlePatchNametable) |
| $C260 | `v_battleNameRowBackup` | 46-byte backup of the name row overwritten by "ALEX" |
| $C2A0 | `v_scoreMarksSprite` | RAM sprite descriptor of the round-result marks |
| $C908 | `v_battleNameRow` | name-table copy row of the names |
| $CA08 / $CA2C | `v_thoughtCloudAlexArea` / `...OpponentArea` | name-table copy areas of the thought clouds |

Entity fields used by these updaters: see the comment above each entity in
the sources (e.g. `battleDecision` is a lifespan for items, a hit counter for
the bull, a fall speed high byte for the walker, a pause length for Janken's
projectile, the bob height for Gooseka's head). Sound $8D (unnamed in the
reference) is the boss-head hit sound (`SOUND_BOSS_HIT`).

## Original quirks kept (marked `QUIRK` in the code)

* `reverse_x_speed` (`_LABEL_5CA0_`, and inline copies in $54/$55): negates
  the low byte and complements the high byte separately, so a speed with a
  zero low byte ends up $100 too small ($FF00 -> $0000). The speeds used by
  these enemies all have a non-zero low byte, so it never shows.
* Chokkinna's head (`updateChokkinnaHeadState1`) picks its new direction by
  testing the whole `unknown3` byte after `xor $02`, not bit 1.
* `updateEntity0x51` re-tests its initialized flag ($5FEF) on a path where it
  is known to be clear (dead test, dropped in C).
* `_LABEL_5D7B_` ($5D74) is a lone `jp _LABEL_555C_` ($5555): a variant of
  `killOpponent`'s body stored right after it and used by nothing else (the
  generated code inlines it; the lifted routine implements it);
  `_LABEL_7A40_` pops its caller's return
  address so that a punched head abandons the current state handler
  (return-to-grandparent), reproduced with `cpu.sp += 2`.
* `simulateOpponentChoosing` uses `ret p` on the countdown, so changes of mind
  happen every table value + 1 frames.

## Verification

Build: `make BUILD=build-enemies2 LIFTED_EXTRA="$(ls src/game/wip/enemies2/*.c | tr '\n' ' ')" build-enemies2/shadow build-enemies2/lockstep`
(the Makefile does not track `enemies2.h`: delete the binaries after editing it).

Shadow suite, all with `--depth 12`, 0 mismatches in every routine of the build:
* `--mode idle --frames 3000`; `--mode play --seed 1..5 --frames 10000`;
* `--mode play --level N --lives --seed 11/12/13 --frames 8000` for N = 1..17;
* scripted scenarios (`--script`, generated), all starting a game
  (`100 keys 1`, `106 keys -`) with `--level` and `--lives`, then poking an
  entity into a free slot (fields data/x/y/state/flags/offscreen/speeds, type
  last) around frame 700:
  * battles: opponent poked into slot 6 ($C3A0: type, data $C3A3, x $C3AC,
    y $C3AE, state $C3BA) in level 11 (whose start screen does not scroll);
    text boxes closed by tapping button 2 every 50 frames; results forced by
    poking Alex's throw ($C317) and the opponent's ($C3B7) every 10-25 frames
    (win: 2/0, lose: 1/0, tie: 0/0); `v_hasTelepathyBall` ($C048) = 1 in some
    runs; boss heads punched by poking the head position ($C3CC/$C3CE) in
    front of Alex while he punches (and its state $C3DA = 1 for
    `chok_hit1`). Scenarios: goo/chok/parp/jank `_boss`, `_plain` (no boss),
    `_heads`, `_kill`, goo_lose, chok_tie, chok_scroll (level 12: scrolling
    screen), goo_jump (Alex in the air), jank_16;
  * entities (level 12 unless noted, slot 20 $C560 / 21 / 22): life,
    items_expire, items_offscr (off-screen flag poked), bull_hits/kill/turn/
    left, cloud, flame57, flame_edge, flame_scroll, walker_wall,
    walker_punch, hopper_wall, leaper_punch, items (story items 1-9 at Alex's
    position, then an already collected one), item_bonus, bonus_touch, egle
    ($C07F = 1 then 2), egle_done, lora (level 11), lora_scroll, elder,
    nurari (level 4, `v_scrollFlags` poked to 0), king_letter, king_noletter.

Relaxed lockstep (`--relaxed`) against the reference emulator: play seeds
1-5 x 20000 frames, idle 3000, levels 1-17 x seeds 11/12 x 8000 frames, and
every scripted scenario above: all identical.

## Open issues

* `unused_LABEL_5C27_` is dead code and stays unexercised.
* Names of some creatures are descriptive guesses from screenshots
  (walking/hopping monsters, water leaper, lightning cloud).
