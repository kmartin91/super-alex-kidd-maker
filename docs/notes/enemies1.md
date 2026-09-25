# Module enemies1: entity updaters below $5901

Lifted code: `engine/src/game/enemies1/`

| file | contents |
|------|----------|
| `enemies1.h` | module constants (entity types, flags, Alex weapon bits, sounds, name table requests), byte views of Entity fields, call helpers |
| `orbit.c` | circular motion (`unknownAnimate` + 8 octant routines, `multiply`) and the octopus arm |
| `weapons.c` | vehicle missiles, Magic Capsules A/B and their helpers, Power Bracelet shockwave |
| `tile_objects.c` | background (name table) objects: sinking blocks, collapsing floors, waterfall curtain, and the name table change handlers $85/$86/$87 |
| `sea_and_sky.c` | merman + bubbles, bats, plant, monster birds, small/killer fishes, sea horses |
| `ground.c` | scimitar swordsman (8 hits), monkey + leaves, monster frog, death puffs, `killEnemy`/`killOpponent`, `getVelocitiesToPursuitAlex` |
| `misc.c` | map arrow, Janken's castle (map), level start icon, event trigger $60, toll $61, secret wall $63 |

All 89 routines of `enemies1-routines.txt` are lifted. Each keeps its original
name in `LIFTED(...)`; a comment above it gives the address and the behaviour
in game terms. Jumps to other routines stay `TAIL_CALL(f_...)` so every
original entry point remains a verified unit.

## Routine status

Calls / mismatches: sum over the final verification suite (127 shadow runs,
`--depth 12`, see "Verification"; nested calls from other lifted modules are
counted too). Every routine: calls > 0, 0 mismatches.

| addr | routine | file | what it is | calls | mism. |
|------|---------|------|------------|------:|------:|
| 04CE | unknownAnimate | orbit.c | place entity on its circle (9-bit angle) | 278492 | 0 |
| 04FE | unknownAnimateState1Updater | orbit.c | octant 0 | 3654 | 0 |
| 0535 | unknownAnimateState2Updater | orbit.c | octant 1 | 3648 | 0 |
| 0571 | unknownAnimateState3Updater | orbit.c | octant 2 | 3634 | 0 |
| 05A8 | unknownAnimateState4Updater | orbit.c | octant 3 | 85884 | 0 |
| 05E4 | unknownAnimateState5Updater | orbit.c | octant 4 | 86404 | 0 |
| 061B | unknownAnimateState6Updater | orbit.c | octant 5 | 86994 | 0 |
| 0657 | unknownAnimateState7Updater | orbit.c | octant 6 | 4818 | 0 |
| 068E | unknownAnimateState8Updater | orbit.c | octant 7 | 3456 | 0 |
| 074C | multiply | orbit.c | HL = L*E | 556984 | 0 |
| 1B41 | updateArrow | misc.c | map arrow ($56) | 230348 | 0 |
| 1B8E | updateJankensCastle | misc.c | castle on the map ($58) | 23081 | 0 |
| 39DB | updateEntity0x62 | misc.c | level start icon | 20727 | 0 |
| 3E28 | updateEntity0x60 | misc.c | event trigger (state table $3E31) | 3260 | 0 |
| 3EBA | updateEntity0x61 | misc.c | toll | 11650 | 0 |
| 3EFC | updateEntity0x63 | misc.c | secret wall (Mt. Kave) | 5495 | 0 |
| 443F | updateEntity0x03 | weapons.c | vehicle wreck puff | 1060 | 0 |
| 4489 | updateEntity0x02 | weapons.c | boat/peticopter missile | 1727 | 0 |
| 44CD | updateEntity0x04 | weapons.c | missile cooldown | 445 | 0 |
| 4689 | updateEntity0x05 | weapons.c | capsule A thrown | 300 | 0 |
| 46C2 | updateEntity0x06 | weapons.c | capsule A open (spawns helpers) | 4182 | 0 |
| 4719 | updateEntity0x07 | weapons.c | capsule B thrown | 279 | 0 |
| 4768 | updateEntity0x09 | weapons.c | capsule A helper walking | 6218 | 0 |
| 4830 | sub_4830 | weapons.c | first occupied enemy slot | 105 | 0 |
| 483F | _LABEL_4846_ | weapons.c | helper still in play? (carry) | 16434 | 0 |
| 484D | _LABEL_4854_ | weapons.c | remove helper (end item if capsule gone) | 406 | 0 |
| 4853 | _LABEL_485A_ | weapons.c | end item in use + remove | 216 | 0 |
| 485E | sub_485E | weapons.c | remove, carry clear | 1052 | 0 |
| 4863 | updateEntity0x0A | weapons.c | capsule A helper in the air | 2111 | 0 |
| 4885 | updateEntity0x08 | weapons.c | capsule B barrier | 12564 | 0 |
| 48BE | _LABEL_48C5_ | weapons.c | Power Bracelet punch (action 5) | 245 | 0 |
| 48E1 | sub_48E1 | weapons.c | spawn shockwave left | 85 | 0 |
| 48E9 | sub_48E9 | weapons.c | spawn shockwave right | 149 | 0 |
| 48EF | sub_48EF | weapons.c | spawn shockwave (A, HL, DE) | 234 | 0 |
| 4914 | updateShockwave | weapons.c | shockwave ($1B) | 5679 | 0 |
| 493D | _LABEL_4944_ | weapons.c | item gravity + landing | 5324 | 0 |
| 497D | updateEntity0x14 | tile_objects.c | sinking block | 18283 | 0 |
| 49EB | updateEntity0x10 | tile_objects.c | sinking block setup A | 9353 | 0 |
| 49F5 | _LABEL_49FC_ | tile_objects.c | sinking block common setup | 10374 | 0 |
| 4A26 | updateEntity0x11 | tile_objects.c | sinking block setup B | 613 | 0 |
| 4A32 | updateEntity0x12 | tile_objects.c | sinking block setup C | 340 | 0 |
| 4A3E | updateEntity0x13 | tile_objects.c | sinking block setup D | 68 | 0 |
| 4A4A | updateEntity0x16 | tile_objects.c | collapsing floor (proximity) | 30544 | 0 |
| 4A72 | _LABEL_4A79_ | tile_objects.c | start collapse | 24 | 0 |
| 4A7F | _LABEL_4A86_ | tile_objects.c | collapse step | 314 | 0 |
| 4AD0 | _LABEL_4AD7_ | tile_objects.c | collapsing floor setup | 192 | 0 |
| 4AE7 | updateEntity0x17 | tile_objects.c | collapsing floor (punched) | 764 | 0 |
| 4B1C | updateEntity0x15 | tile_objects.c | waterfall curtain | 2923 | 0 |
| 4B97 | _LABEL_4B9E_ | tile_objects.c | request $85: copy block ROM->mirror+VRAM | 767 | 0 |
| 4BC6 | _LABEL_4BCD_ | tile_objects.c | request $87: erase block | 56 | 0 |
| 4BEC | _LABEL_4BF3_ | tile_objects.c | request $86: draw curtain rows | 117 | 0 |
| 4BFE | sub_4BFE | tile_objects.c | draw one curtain row | 234 | 0 |
| 4C1C | _LABEL_4C23_ | tile_objects.c | next name table row (wrap) | 3672 | 0 |
| 4C27 | updateOctopusArm | orbit.c | octopus arm segment ($24) | 125104 | 0 |
| 4DA6 | getVelocitiesToPursuitAlex | ground.c | aim at Alex | 2686 | 0 |
| 4E0D | sub_4E0D | ground.c | 16/8 division -> fraction | 2686 | 0 |
| 4E29 | updateMerman | sea_and_sky.c | merman ($23) | 47935 | 0 |
| 4E96 | updateMermanBubbles | sea_and_sky.c | merman bubble ($22) | 42512 | 0 |
| 4EE8 | updateBatLeft | sea_and_sky.c | bat ($20) | 53581 | 0 |
| 4F3C | bat_LABEL_4F43_ | sea_and_sky.c | bat wave | 19428 | 0 |
| 4F75 | _LABEL_4F7C_ | sea_and_sky.c | bat animation | 132946 | 0 |
| 4F7B | updateBatRight | sea_and_sky.c | bat ($36) | 3195 | 0 |
| 4FA6 | spawnMermanBubbles | sea_and_sky.c | bubbles into slots 17-21 | 268 | 0 |
| 4FEA | updatePlant | sea_and_sky.c | carnivorous plant ($2C) | 26478 | 0 |
| 5030 | updateMonsterbirdLeft | sea_and_sky.c | monster bird ($2D) | 151113 | 0 |
| 5081 | updateMonsterbirdRight | sea_and_sky.c | monster bird ($33) | 67223 | 0 |
| 50DA | updateSmallFishLeft | sea_and_sky.c | small fish ($30) | 86550 | 0 |
| 512B | updateSmallFishRight | sea_and_sky.c | small fish ($34) | 60008 | 0 |
| 5158 | updateKillerFishLeft | sea_and_sky.c | killer fish ($2E) | 2714 | 0 |
| 51EC | updateKillerFishRight | sea_and_sky.c | killer fish ($35) | 1724 | 0 |
| 52E0 | updateEntity0x25 | ground.c | swordsman walking left | 5061 | 0 |
| 5350 | _LABEL_5357_ | ground.c | swordsman: turn right | 2874 | 0 |
| 5359 | updateEntity0x26 | ground.c | swordsman walking right | 2429 | 0 |
| 53BF | _LABEL_53C6_ | ground.c | swordsman: turn left | 2886 | 0 |
| 53C8 | updateEntity0x27 | ground.c | swordsman attacking left | 3073 | 0 |
| 544A | updateEntity0x28 | ground.c | swordsman attacking right | 3092 | 0 |
| 54D8 | _LABEL_54DF_ | ground.c | swordsman hit: knock-back | 138 | 0 |
| 550E | updateEntity0x4A | ground.c | knocked-back swordsman | 2069 | 0 |
| 5540 | killOpponent | ground.c | 8-hit enemy defeated | 88 | 0 |
| 556A | _LABEL_5571_ | ground.c | stop (Alex dead) | 4920 | 0 |
| 5573 | updateEntity0x2A | ground.c | monkey | 4700 | 0 |
| 559E | killEnemy | ground.c | enemy killed -> smoke puff | 164 | 0 |
| 55EC | updateMonkeyLeaf | ground.c | monkey leaf ($29) | 2782 | 0 |
| 5622 | updateEntity0x43 | ground.c | big death puff -> rice ball | 677 | 0 |
| 567D | updateSmokePuff | ground.c | smoke puff ($2B) | 3188 | 0 |
| 56C5 | updateEntity0x2F | ground.c | monster frog sitting | 24260 | 0 |
| 571C | updateMonsterFrogJumping | ground.c | monster frog in the air ($37) | 39628 | 0 |
| 57C7 | updateSeaHorseLeft | sea_and_sky.c | sea horse ($31) | 11205 | 0 |
| 587C | updateSeaHorseRight | sea_and_sky.c | sea horse ($32) | 3338 | 0 |

No routine is unreachable. Branch coverage (clang source coverage of the
lifted code over the scenarios): 100 % of the lines; every branch taken both
ways except the dead/degenerate ones listed under "Dead code".

## Entity types (game behaviour)

Levels are `v_level` numbers (1 Mt. Eternal ... 16 Janken's castle, 17 Cragg Lake).

| type | name | behaviour |
|------|------|-----------|
| $02 | vehicle missile | fired by the boat / peticopter (levels 5, 9, 13), slot 2; flies 20 frames, explodes on walls (breaking breakable blocks) or enemies, becomes $04 |
| $03 | vehicle wreck puff | puff animated 20 frames where Alex loses his vehicle (slot 4) |
| $04 | missile cooldown | invisible; after 5 frames clears Alex's attack bits so he can fire again |
| $05 | Magic Capsule A (thrown) | arcs under gravity (slot 4), stops at walls, opens on landing |
| $06 | Magic Capsule A (open) | releases 8 helpers ($09) into slots 2/3, 10 then 30 frames apart |
| $07 | Magic Capsule B (thrown) | arcs under gravity, becomes the barrier on landing |
| $08 | Magic Capsule B barrier | follows Alex (x-4, y-3) for 1200 frames; Alex is invincible and the barrier kills what it touches; then the item is used up |
| $09 | capsule A helper (walking) | walks toward the first enemy present when it appeared (±2 px/f), jumps at it when close, falls off ledges |
| $0A | capsule A helper (air) | jumping/falling helper, lands back into $09; vanishes after hitting something |
| $10-$13 | sinking block setups | remember graphics/size/speed, become $14 (Radactian castle, Janken's castle) |
| $14 | sinking block | background block that moves down one tile row every N frames until the row under it is not empty |
| $15 | waterfall curtain | checkered tiles poured downward, one row per 15 frames for 18 rows, only into empty cells (Janken's castle) |
| $16 | collapsing floor | when Alex is 0-15 px right of it, a 2-row hole opens under it and widens by one tile each side every 3 frames (`data` steps) |
| $17 | punchable collapsing floor | same, triggered by Alex's attack, hole at fixed screen position ($74,$A0) |
| $1B | shockwave | Power Bracelet shot, 4 px/f, breaks every breakable block on its way, vanishes on walls / screen edges |
| $20/$36 | bat (left/right) | flies ±0.5 px/f along a small sine wave around a base height, turns at walls |
| $22 | merman bubble | flies in one of 8 directions, pops near the top of the screen |
| $23 | merman | swims up/down 0.25 px/f, reverses every 192 frames and then blows up to 5 bubbles; 3 hits |
| $24 | octopus arm segment | chain of segments swinging around the previous segment (radius 8); 3 hits on the root segment defeat the octopus (all segments -> puffs, name table change $83/$84) |
| $25/$26 | swordsman walking (left/right) | scimitar warrior of The Blakwoods, ±0.375 px/f toward Alex |
| $27/$28 | swordsman attacking | ±0.25 px/f, swings (sound $99) while Alex is within 32 px in front |
| $29 | monkey leaf | thrown straight at Alex (aim computed once), harmless to attacks |
| $2A | monkey | sits and throws a leaf each animation loop (64-frame timer) |
| $2B | smoke puff | death animation of ordinary enemies |
| $2C | carnivorous plant | bobs 0.5 px/f, reverses every 64 frames; invulnerable |
| $2D/$33 | monster bird | flies ±0.5 px/f, turns at walls |
| $2E/$35 | killer fish | swims ±0.375 px/f on a large sine wave, turns at walls |
| $2F | monster frog (sitting) | waits 16 frames then jumps (-1.5 px/f) whenever on ground |
| $30/$34 | small fish | swims ±0.375 px/f, turns at walls |
| $31/$32 | sea horse | sinks (left) / rises (right) 48 frames then hops sideways along a sine; turns every 2 hops |
| $37 | monster frog (air) | gravity 8/256 px/f², lands back into $2F |
| $43 | big death puff | puff of an 8-hit enemy; leaves a rice ball ($44) if `unknown1` != 0 |
| $4A | knocked-back swordsman | 8 frames at 1 px/f away from its direction; 8th hit -> `killOpponent` |
| $56 | map arrow | blinking arrow at the level's map position (24 px higher on the pause map; fixed place in bonus levels) |
| $58 | Janken's castle | static map sprite |
| $60 | event trigger | invisible; Alex standing on it is frozen, two sprites appear (slots 27/28), then change $89 draws a block and Alex is released |
| $61 | toll | invisible; touching it with slot 27 occupied pays 50 (BCD) and clears slots 27/28 (not found in the level data) |
| $62 | level start icon | animated sprite at (216,128) on the level start screen |
| $63 | secret wall | invisible (Mt. Kave); punching it replaces name table entry $CC08 with a background metatile |

## Entity fields used by these types

General (all types): `flags` bit 0 = first-frame setup done, bit 1 = removed
by the movement code when leaving the screen, bit 4 = collapse started ($16/$17),
bit 7 = touched by `checkEntityCollision` (weapon hit / hit Alex).
`isOffScreenFlags` low byte = horizontal screen offset (**+1 = one screen to
the left**, $FF = one to the right), high byte = vertical offset (+1 = below).

| type(s) | field meanings |
|---------|----------------|
| orbiting ($24) | `stateTimer` + bit 0 of `unknown8` = 9-bit angle (512/turn, 0 = right, 128 = down); `unknown9` radius; `unknown11`/`state` centre X / its screen offset; `unknown10`/`unknown7` centre Y / its screen offset |
| octopus arm | `data` hits; `unknown4` 0 = root segment, else chained; `unknown5` delay before swinging; `unknown6` swing direction (0/$FF); `battleDecision` frames in current swing (reverses at $60); `animationTimerResetValue`/`unknown1` fraction bytes of the root centre X/Y |
| $02/$03/$04 | `unknown7` frames left |
| $05-$0A | `unknown3` bit 1 moving right, bit 6 landed, bit 7 falling; $06: `unknown10` helpers left, `unknown11` delay; $08 (slot 4): `stateTimer`+`unknown8` 16-bit frames left; $09: `unknown9` bit 0 = tracking a target, `stateTimer`/`unknown8` = target slot |
| $10-$14 | `state`/`stateTimer` ROM source (bank 7); `unknown10` rows; `unknown11` bytes per row; `unknown8`/`unknown9` mirror address; `unknown7` frames per step; `data` countdown |
| $15 | `data` width in tiles; `unknown8`/`unknown9` current row; `unknown6` rows left; `animationTimer` countdown |
| $16/$17 | `data` steps (level data); `unknown8`/`unknown9` hole left end; `unknown11` hole width in bytes; `unknown6` steps left; `unknown7` frames per step; `animationTimer` countdown |
| $20/$36 | `unknown5`/`battleDecision` base height (pixel/fraction); `unknown6` wave phase |
| $2E/$35 | same as bats (large wave) |
| $22 | `battleDecision` direction index (0-7) |
| $23 | `data` hits; `battleDecision` frames in current direction; `unknown6` bubbles blown |
| $25-$28/$4A | `unknown5` hits; `unknown2` hitbox index (forced to $A8 during the attack test, saved in `unknown6`); `unknown9`/`unknown10`/`unknown11` type and xSpeed to restore; `battleDecision` knock-back frames; `unknown1` = $81 (unused) |
| $2A | animation timer = throw timer |
| $2C/$2F/$37 | `battleDecision` frame counter |
| $31/$32 | `data` hop base Y; `unknown6` hop phase (0/$FF = sinking/rising); `battleDecision` sink/rise frames; `unknown5` hops done |
| $43 | `unknown1` != 0: leave a rice ball |
| $60 | `state` handler index, `animationTimer` delay |

## RAM variables and bits

- Alex `unknown8`: bit 0 attack in progress, bit 1 a thrown item/shockwave is out,
  bit 3 the current attack has not damaged anything yet (cleared by the octopus
  and merman so that one attack = one hit). `$F6`/`$F4` masks clear them.
- Alex `unknown3`: bit 0 facing right, bit 1 moving right.
- `v_alexActionState` ($C054): 2 invincible (needs `v_invincibilityTimer` $C05A
  non-zero, see `updateInvincibility`), 3 capsule A, 4 capsule B, 5 Power
  Bracelet, 7 motorcycle, 8 boat, 9 peticopter.
- `_RAM_C0F4_` (word: low = Y, high = X), `_RAM_C0FB_` (screen X), `_RAM_C0FF_`
  (screen Y): position of the last updated octopus arm segment = centre of the
  next one.
- `_RAM_C07F_`: shared "event in progress" flag; the big death puff clears it.
- `v_nametableChangeRequest` ($C202) = $80 | handler index (table $4237):
  $80 metatile, $83/$84 octopus defeated, $85 copy block, $86 draw curtain,
  $87 erase block, $89 event trigger block. Parameters:
  `v_nametableChangeDestination` ($C204, mirror address), 
  `nametableChangeSourceMetatile` ($C206, ROM source in bank 7),
  `_RAM_C208_` rows / `_RAM_C209_` bytes per row (2 bytes per tile).
- Name table mirror $C800-$CEFF (28 rows x 64 bytes); mirror - $5000 = VDP
  write control word. Attribute bits 7/6 of an entry are the game's
  solid/breakable collision flags.
- Entity slots are numbered from 1 like the reference (`v_entities.2` = $C320):
  2-3 weapons/helpers, 4 thrown item, 7-16 enemies, 17-21 enemy projectiles,
  27-28 event sprites.

## Original bugs and quirks (kept, marked `QUIRK` in the code)

- Capsules A/B: when thrown to the left the wall probe is at x+$42 (66 px to the
  right) instead of a negative offset.
- Capsule A helper: on its first frame with a target, the walking animations
  are swapped (right-walking helper shows the left animation).
- Octopus defeat leaves IX on slot 7, so the entity loop resumes after slot 7
  (some entities may be updated twice that frame).
- Octopus root centre: horizontal scroll compensation assumes a negative scroll
  speed (screen scrolling right only).
- `_LABEL_4B9E_` (copy block): the row wrap test runs on the VDP address, where
  it never triggers; blocks crossing the bottom of the name table write past
  $CEFF.
- `sub_4BFE` (curtain row): only the low byte of the row address is restored,
  wrong page if a row ends on a 256-byte boundary.
- Sinking block: the "row below" test does not wrap at the bottom of the mirror.
- Collapsing floor: the hole's left end is decremented as 8 bits (cannot cross
  its 256-byte page).
- Bats and killer fish negate the vertical scroll speed byte by byte (wrong for
  speeds with both bytes non-zero); killer fish apply the vertical scroll twice.
- Merman / plant reverse their speed with CPL high / NEG low (wrong if the low
  byte is 0; never the case with their constants).
- Killer fish right turning left keeps its speed for one frame.
- Sea horse right uses the hop base saved by the left phase.
- Swordsman walking right measures Alex from x+$38 (8-bit, wraps near the right
  edge); a punch can never reach it in that state (only shots can).
  `updateEntity0x28` plays the attack sound on every loop start, $27 only when
  the animation frame is not 0. `unknown1` = $81 is set but never read.
- Monster frog jumping: the ground test is skipped only while the speed high
  byte is $FF, so it also runs during the fast first part of the jump.
- `getVelocitiesToPursuitAlex`: for any entity other than the monkey leaf the
  minor-axis speed is divided by 4 but the major one is not (aim is off).
  `sub_4E0D`'s rounding test drops bit 8 of twice the remainder.
- Toll ($61) tests slot 27 twice (slot 28 probably intended).

## Dead code / unreachable branches

- `updateEntity0x02`: after breaking a block, `_LABEL_4578_` always leaves a
  name table request pending, so the "no request -> keep flying" outcome never
  happens (the missile always explodes).
- `_LABEL_4B9E_` / `_LABEL_4BCD_` with 0 bytes per row (LDIR of 64 KB / 256-byte
  row): row sizes are constants (4, 8, 32 and >= 4), never 0.
- `updateShockwave`: its screen-offset test cannot trigger in normal play (the
  x < $0C / x >= $F4 tests remove it first); exercised with scripted spawns.
- `$61` (toll) and `$17` are rare: $17 is in Janken's castle data, no level
  data reference to $61 was found; both exercised with scripted spawns.

## Verification

Build: `cd port && make BUILD=build-enemies1 LIFTED_EXTRA="$(ls src/game/wip/enemies1/*.c | tr '\n' ' ')" build-enemies1/shadow build-enemies1/lockstep`
(use `make -B` after editing `enemies1.h`: the Makefile does not track wip headers).

Shadow (all with `--depth 12`), 127 runs, all `OK`, 0 mismatches in any routine:
- `--mode idle --frames 3000`
- `--mode play --seed 1..8 --frames 10000`
- `--mode play --seed 1..3 --level N --lives --frames 12000` for N = 1..17
- 67 scripted scenarios (`--script ... --level N --lives`): items (capsules A/B,
  bracelet in both directions, missiles against planted breakable tiles), the
  swordsman (punches, shockwaves, Alex dead), monkey, tile objects (including
  two objects triggering on the same frame), event trigger/toll, secret wall,
  map arrow on a bonus level, octopus kills (root/chained, early/late level,
  vertical scroll), kills of every enemy type, every type spawned off screen,
  full projectile slots. Scripts spawn entities by poking a free slot (22-26)
  and use `v_alexActionState` = 2 + `v_invincibilityTimer` for invincibility.
  They were generated by a scratch script (not part of the repository).

The final runs were made with the promoted modules `src/game/alex`, `audio`
and `enemies2` in the build: no mismatch in any lifted routine of any module.

Lockstep (`--relaxed`), all identical: `--mode play --seed 1..6 --frames 20000`,
`--mode play --level N --lives --frames 20000` for N = 1..17, `--mode idle
--frames 5000`, and the 67 scripted scenarios.

## Open issues

- Several names are descriptive guesses from behaviour and screenshots
  (swordsman, waterfall curtain, event trigger, toll, sinking block); the
  exact in-game objects of $10-$17/$60/$61 were not identified.
- `earnEntityPoints` ($5761) is below $5901 but not in this module's list; it
  is only called here.
