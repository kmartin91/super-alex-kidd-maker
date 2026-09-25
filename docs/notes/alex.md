# Module `alex`: the player character

All 129 routines of the module (see `alex-routines.txt`) are lifted in
`engine/src/game/alex/`. Every one is exercised by the shadow test with
**0 mismatches**; relaxed lockstep runs pass (see Verification).

| file | contents |
|---|---|
| `alex.h` | states, action states, flag bits, tile classes, sprite/animation descriptor addresses, call and flag helpers, module-internal prototypes |
| `states.c` | `updateAlex`, spawning, idle, walking, crouching, jumping/in air, death |
| `special.c` | shop doors, floor hatches, ladders, screen-flip freeze, Cane of Flight, diving, janken states |
| `swimming.c` | splash, swimming (vertical and horizontal) |
| `vehicles.c` | motorcycle, boat, peticopter, losing a vehicle |
| `physics.c` | terrain probes, gravity/landing, acceleration, friction, braking, stun shake |
| `tiles.c` | tile interactions (body and floor), level 17 floor puzzle, ghost floor, event entity `$60` |
| `camera.c` | per-level camera handlers (table `$3F31`) |
| `actions.c` | button 2: punch, block breaking and contents, capsules |
| `graphics.c` | animation, sprite descriptors, Alex tile streaming to VRAM |

Module-internal C helpers are prefixed `alex_` (non-static, shared between
the files through `alex.h`); everything else is `static`.

## State machine

`updateAlex` ($2958, entity type 1, always slot 0 `$C300`) runs every frame:

1. clears `v_horizontalScrollSpeed` / `v_verticalScrollSpeed`;
2. if `flags` bit 7 (hit) is set: in the peticopter or on the boat Alex only
   loses the vehicle (`$388E`); otherwise he dies (`$2F41`: speeds and action
   cleared, state `$0F`, the game freezes 30 frames, sound `$89`);
3. runs the state handler through the original table at `$2982` (kept as a ROM
   dispatch because the handlers observe its registers and flags, see below);
4. unless dead or stunned, runs the level's camera handler (`camera.c`).

| state | name | handler | notes |
|---|---|---|---|
| $00 | spawning | $29BA | respawn at (x=$80, y=$60) |
| $01 | idle | $2A9E | B2 action (punch or item), B1 jumps, pad walks/crouches |
| $02 | walking | $2B41 | |
| $03 | in air | $2CD0 | jump boost, gravity, steering, landing |
| $04 | crouched | $2E60 | slides when crouching while moving |
| $05 | swimming | $34B6 | |
| $06 | peticopter | $36F1 | waits for B1 to take off |
| $07 | Cane of Flight | $336F | set when the item menu closes with action state 1; falls otherwise |
| $08 / $09 | motorcycle / jumping | $2FA7 / $302F | |
| $0A | climbing a ladder | $3256 | |
| $0B / $0C | boat / jumping | $3094 / $3107 | |
| $0D / $0E | reaching / crossing a shop door | $3180 / $31A8 | |
| $0F | dead | $2F8A | ghost rises; life lost at lines $A3-$A7 after wrapping |
| $10 | screen flip | $3340 | frozen while the flip-screen levels scroll one screen |
| $11 / $12 | to a floor hatch / going down | $31CC / $3223 | |
| $13 | diving | $38C5 | after a vehicle crash above water |
| $14 | walking in | $3468 | at 1.5 px/frame |
| $15-$18 | janken | $3919 $3961 $39A5 $3949 | count, walk to x=$28, wait, show hand |
| $19 | petrified | $39B4 | janken lost; `unknown6` frames later: hit (dies) |
| $1A | held still | $39D4 | event entity `$60` |
| $1B | vehicle crash | $38C2 | falls (levels 1, 5, 9) |

Main transitions: idle/walking -> jump (B1, state 3) or fall (no ground under
the feet); in air -> landing (idle or walking); body tile = water -> splash
(state 5); ladder tile + UP (or ladder top + DOWN) -> state $0A; shop door
tile + UP (idle, x >= $18) -> $0D -> $0E -> game state `$05`; hatch floor +
DOWN (just pressed) -> $11 -> $12 -> swimming.

## Physics (8.8 fixed point, per frame)

Positions: `xPos`/`yPos` high byte = screen pixel of the top-left corner of a
16x24 box. Speeds are signed: `$0200` = 2 px/frame right/down.

| constant | value | meaning |
|---|---|---|
| walk accel / max | $0040 / $0200 | pushing the pad |
| walk friction | $0020 | pad released |
| walk brake | $0040 | pushing the other way; turns around at 0 |
| air accel / max | $0010 / $0200 | steering in the air |
| air friction / brake | $0008 / $0010 | |
| jump boost | 22 frames | while B1 held: ySpeed = -(2 + abs(xSpeed)/4) px/frame |
| gravity | $0040 | per frame once the boost ends |
| terminal speed | 4 px/frame | from $04xx the fraction is cleared |
| crouch slide | $0020 | speed and friction |
| swim | $0100 ($0180 with B1) | accel $0010 ($0020); friction $0008 |
| swim vertical | up to -1 px/f (UP: -1.5), sink up to 1 px/f | at the surface: sink at 0.5 |
| motorcycle | accel $0040, max $0400, min $0100 | jump boost 16 frames: -(2 + xSpeed/2) |
| boat | accel $0040, max $0280, min $0100 | lands only on water |
| peticopter | accel $0040, max $0200, friction $0020 | B1: 7 frames of thrust, up to -2 px/f; glides otherwise |
| cane of flight | 1 px/frame in 4 directions | bobbing +-0.5 px/f, flips every 4 frames |
| stun | 60 frames | xSpeed +-$0080 flipping every 2 frames |

Terrain probes (`physics.c`) read the RAM copy of the name table (`$C800`);
the attribute byte holds the collision class in bits 5-7:

| attr & $E0 | meaning |
|---|---|
| bit 7 | solid |
| $20 | water |
| $40 | pickup: tile < $90 money bag in the background, >= $90 shop shelf |
| $60 | tile >= $70 shop door, $3F ladder, others deadly |
| $A0 | special floor: $3F ladder top, > $3F hatch, $0D-$24 level 17 effects |
| $C0 / $E0 | breakable block / block with contents (tile 1-4 skull, 5-8 question box, 9-12 money) |

Ground probe: 25 px below the corner at x+4 and x+12 (`unknown9` apart).
Walls: 3 points (y+1, y+15, y+23) at x+2 (left) or x+14 (right). Landing cuts the frame's
vertical speed so that the feet end on the tile's top edge.

Camera (horizontal): the view scrolls at Alex's speed once he passes column
`$60 + (xSpeed >> 4)` (look-ahead); where the level cannot scroll he stops at
x < 4 / x >= $F4. Vehicles are held near x = $44. Levels 11 and 16 flip
screen by screen; levels 1 and 17 have vertical sections.

## Entity fields and RAM (Alex)

| field | meaning |
|---|---|
| `flags` bit 7 | hit (enemy, deadly or solid tile at the body); bit 0 set then cleared at spawn |
| `unknown3` (+$14) | motion flags: 0 facing right, 1 moving right, 2 moving horizontally, 3 vertical direction down (swim/fly/ladder), 4 moving vertically, 5 ?, 6 landed this frame, 7 falling |
| `unknown8` (+$1C) | action flags: 0 attacking (no new action), 1 attack is a thrown capsule, 2 in air: gravity phase (boost over), 3 fist hurts enemies, 4 stunned, 6 peticopter engine on |
| `unknown6` (+$18) | stun timer; petrification timer (state $19) |
| `unknown7` (+$19) | punch pose timer ($0A frames) |
| `unknown9` (+$1D) | foot width: distance between the ground probes ($08, $0F on vehicles) |
| `unknown11` (+$1F) | height: feet offset below the corner ($18, $10 on the boat) |
| `stateTimer` (+$1B) | jump boost, door crossing, hatch descent, peticopter thrust, cane bobbing |
| `battleDecision` (+$17) | janken hand 0-2 |
| `isOffScreenFlags` high | non-zero ($FF) while Alex is above the top of the screen: the probes then use absolute coordinates biased by $40 |

| RAM | meaning |
|---|---|
| `v_alexActionState` $C054 | 0 none, 1 Cane of Flight, 2 invincible, 3/4 magic capsule A/B, 5 power bracelet, 7 motorcycle, 8 boat, 9 peticopter (1 and 2 end when `v_invincibilityTimer` $C05A reaches 0) |
| $C201 | tile set index currently in VRAM (next to `v_alexTilesIndex`) |
| $C203 | bank of `nametableChangeSourceMetatile` ($85) |
| $C211 | pointer to the ladder/door/hatch tile touched last |
| $C213 (`v_nametableEntryAttrLastThreeBits`) | $20 while the body tile is water |
| $C214 (`v_nextGhostSpawnTimer`) | ghost floor period (128 frames) |
| $C229 / $C22A | level 17 floor puzzle: correct steps / last colour |
| $C20B | checked by the diving state (non-zero: wait) |
| $C25A / $C25C | set to 1 / 0 with the janken snapshot of Alex |
| $C20C/$C20E/$C210 | speeds and state saved during a screen flip |
| `temporaryAlexCopy` $C240 | 32-byte snapshot of Alex's entity (the ldir copies 32 bytes) |

## Original bugs and quirks (kept, marked `QUIRK` in the code)

- Speed clamps compare unsigned: accelerating against the current motion
  (e.g. `accelerateAlexRight` with a negative xSpeed) snaps to the maximum.
- `updateAlexCrouched`: after the wall probe, the probe's A register (the
  rotated tile attribute) replaces Alex's motion flags when a crouch slide
  starts or is blocked.
- Ladder: jumping off left and right loads `$0711` / `$07FF` then overwrites
  E with 2: both sides probe at dx = 2.
- `$415E` (vehicles kept on screen): the bottom check compares the off-screen
  flag (0) instead of y, so vehicles are never stopped at the bottom line.
- `alexHandler_3961` loads `($000A)` (a byte of `setVdpAddress`) into
  `unknown3` instead of the constant `$0A`.
- `alexHandler_39B4` (petrified) passes the state-table offset left in DE
  ($32) to the gravity routine as the x offset of its off-screen probe.
- The skull box stun (`$45EA`) acts on the entity in IX: when a projectile
  (shock wave, bullet) breaks the box, the projectile gets "stunned", not Alex.
- The level 17 puzzle spawns its ghost at `v_horizontalScroll` low byte (the
  fraction) + $30.
- The money bag check skips the rightmost name table column pair.
- `updateAlexSpawning` sets flags bit 0 then clears the whole byte.
- The swimming ladder hop sets ySpeed = $F000 (-16 px) for one frame.
- ROM revision: this ROM lacks the `.IFDEF _REV1` screen-bottom check of
  `_LABEL_3A68_` (the reference builds revision 1), so the labels after it
  (`_LABEL_3AD5_` at $3ACE, the table `$3F3A` at $3F31...) are 7 bytes lower.

## Register fidelity notes (for other lifters)

- `_LABEL_39ED_`, `isEntityCollidingWithTerrainAtOffset` and `_LABEL_3A41_`
  save A and the caller's flags in AF' (`EX AF,AF'`); when the first tile is
  solid they return with that AF'. Signatures list A'/F' as live, so callers
  must enter them with the flags the original sequence leaves (`FLAGS_ZERO`
  after `or a` on isOffScreen, BIT results, `add a,$07`, the state dispatch).
  Hence the flag-exact helpers (`z80_test`, `z80_cp_flags`) and exact exit
  flags of `interactWithTileAtOffset`, `interactWithFloorWithOffset`, the
  physics helpers, `sub_355B`, `sub_3742` and the cane moves.
- The state handlers are dispatched through `jumpToAthPointer` with the table
  at `$2982` (registers and flags of the dispatch reach the handlers:
  `alexHandler_39B4` uses DE, `alexHandler_3256`/`38C5` hand the flags to a
  probe). The camera, action and block-content tables are C switches; the
  special-floor table (`$3D24`) is dispatched through the ROM because its
  targets' registers (HL = target, C) are outputs of `interactWithFloor`.
- `loadAlexTilesToVRAM` leaves A' = 0 and F' = the flags of its last OUTI.
- `_LABEL_4578_` leaves the block's tile index in D' (EXX).

## Routines

All lifted, 0 mismatches. Calls = compared calls over the final shadow suite
(61 runs, `--depth 12`).

| addr | original name | role | file | calls |
|---|---|---|---|---|
| $2958 | updateAlex | Alex's updater: hit handling, state dispatch ($2982), camera | states.c | 145162 |
| $29BA | updateAlexSpawningAtCenter | state $00: respawn at the screen centre | states.c | 2 |
| $29C2 | updateAlexSpawning | spawn: on foot, walking in, or on the boat/motorcycle/peticopter | states.c | 155 |
| $2A6E | _LABEL_2A6E_ | item menu closed with the Cane of Flight: state 7 | states.c | 1 |
| $2A9E | updateAlexIdle | state $01 idle | states.c | 67254 |
| $2B0B | crouch | crouch | states.c | 758 |
| $2B1F | walkLeft | walkLeft | states.c | 550 |
| $2B23 | sub_2B23 | walk, facing left | states.c | 806 |
| $2B2C | walkRight | walkRight | states.c | 1270 |
| $2B30 | sub_2B30 | walk, facing right | states.c | 1758 |
| $2B39 | walk | walk in the current direction | states.c | 744 |
| $2B41 | updateAlexWalking | state $02 walking | states.c | 58678 |
| $2BFA | setAlexIdleStateAndLoadIdleAnimationDescriptor | become idle | states.c | 1684 |
| $2C04 | leadAlexIdleSpriteDescriptor | idle sprite by facing | states.c | 2650 |
| $2CA1 | fall | fall off a ledge | states.c | 560 |
| $2CAE | jump | jump | states.c | 800 |
| $2CBC | sub_2CBC | enter the in-air state | states.c | 1360 |
| $2CD0 | updateAlexInAir | state $03 in air (jump boost, gravity, steering, landing) | states.c | 40368 |
| $2E60 | updateAlexCrouched | state $04 crouched (slides) | states.c | 24568 |
| $2F41 | _LABEL_2F41_ | Alex hit: lose the vehicle or die (30-frame freeze) | states.c | 133 |
| $2F8A | updateAlexDead | state $0F dead (ghost rising) | states.c | 43756 |
| $2FA7 | updateAlexRidingMotorcycle | state $08 motorcycle | vehicles.c | 1780 |
| $2FD5 | _LABEL_2FD5_ | motorcycle wheels (rocks/walls) and speed control | vehicles.c | 2060 |
| $302F | alexHandler_302F | state $09 motorcycle jump | vehicles.c | 296 |
| $3094 | updateAlexRidingBoat | state $0B boat | vehicles.c | 1388 |
| $30C5 | _LABEL_30C5_ | boat hull check and speed control | vehicles.c | 2170 |
| $3107 | updateAlexRidingBoatInAir | state $0C boat jump | vehicles.c | 800 |
| $3180 | updateAlexReachingDoor | state $0D walk to a shop door | special.c | 132 |
| $31A8 | updateAlexCrossingDoor | state $0E cross the door, enter/leave the shop | special.c | 264 |
| $31C0 | sub_31C0 | walk left to the door | special.c | 318 |
| $31CC | alexHandler_31CC | state $11 move to a floor hatch | special.c | 956 |
| $3223 | alexHandler_3223 | state $12 go down the hatch (vertical scroll) | special.c | 256 |
| $3230 | _LABEL_3230_ | grab a ladder (state $0A) | special.c | 10 |
| $3256 | alexHandler_3256 | state $0A climbing | special.c | 1462 |
| $3320 | _LABEL_3320_ | freeze for a screen flip (state $10) | special.c | 29 |
| $3340 | alexHandler_3340 | state $10 end of the screen flip | special.c | 3252 |
| $335F | saveTempAlexCopy | snapshot Alex's entity (saveTempAlexCopy) | special.c | 54 |
| $336F | alexHandler_336F | state $07 Cane of Flight | special.c | 3430 |
| $33DC | sub_33DC | cane: up | special.c | 1100 |
| $3400 | _LABEL_3400_ | cane: down | special.c | 918 |
| $3424 | _LABEL_3424_ | cane: left | special.c | 472 |
| $3442 | _LABEL_3442_ | cane: right | special.c | 936 |
| $3468 | updateAutoWalkingRight | state $14 walking in from the left | special.c | 1680 |
| $3478 | clearEntities2to4AndMaybeReset0xC054 | clear attack entities, drop the item in use | swimming.c | 34 |
| $3498 | splash | splash into the water | swimming.c | 34 |
| $34B6 | updateAlexSwiming | state $05 swimming | swimming.c | 30728 |
| $355B | sub_355B | swimming: vertical | swimming.c | 30714 |
| $363E | _LABEL_363E_ | swimming: horizontal | swimming.c | 30714 |
| $36F1 | updateAlexFlyingPeticopter | state $06 peticopter | vehicles.c | 8450 |
| $3742 | sub_3742 | peticopter: horizontal | vehicles.c | 7444 |
| $37D5 | _LABEL_37D5_ | peticopter: vertical | vehicles.c | 7444 |
| $388E | _LABEL_388E_ | lose the vehicle (per-level crash table $3903) | vehicles.c | 15 |
| $389C | _LABEL_389C_ | vehicle destroyed: Alex falls (state $1B) | vehicles.c | 8 |
| $38C2 | alexHandler_38C2 | state $1B falling after a crash | special.c | 426 |
| $38C5 | alexHandler_38C5 | state $13 diving | special.c | 384 |
| $3919 | alexHandler_3919 | state $15 janken count, choose a hand | special.c | 200 |
| $3928 | _LABEL_3928_ | janken: UP/DOWN select the hand | special.c | 866 |
| $3949 | alexHandler_3949 | state $18 janken: show the hand | special.c | 100 |
| $3961 | alexHandler_3961 | state $16 janken: walk to x=$28 | special.c | 132 |
| $39A5 | alexHandler_39A5 | state $17 janken: wait, choose a hand | special.c | 666 |
| $39B4 | alexHandler_39B4 | state $19 petrified, then hit | special.c | 64 |
| $39D4 | alexHandler_39D4 | state $1A held still (event entity) | special.c | 280 |
| $39ED | _LABEL_39ED_ | wall probe (3 points) | physics.c | 107072 |
| $3A03 | isEntityCollidingWithTerrainAtOffset | probe 2 points vertically (isEntityCollidingWithTerrainAtOffset) | physics.c | 220709 |
| $3A11 | _LABEL_3A11_ | wall probe above the screen | physics.c | 1860 |
| $3A41 | _LABEL_3A41_ | probe 2 points horizontally (ground/ceiling) | physics.c | 210396 |
| $3A4F | _LABEL_3A4F_ | ground probe above the screen | physics.c | 974 |
| $3A68 | _LABEL_3A68_ | gravity, landing | physics.c | 28278 |
| $3A7E | _LABEL_3A7E_ | ceiling check while rising | physics.c | 17630 |
| $3ACE | _LABEL_3AD5_ | land on the probed tile | physics.c | 1234 |
| $3AE1 | _LABEL_3AE8_ | boat gravity (lands on water, wrecks on land) | physics.c | 552 |
| $3B24 | accelerateAlexLeft | accelerateAlexLeft | physics.c | 25604 |
| $3B44 | _LABEL_3B4B_ | friction on leftward motion if moving | physics.c | 8744 |
| $3B49 | applyFrictionMovingLeft | applyFrictionMovingLeft | physics.c | 4888 |
| $3B4F | resetEntityUnknown3AndAlexSpeed | stop horizontally (resetEntityUnknown3AndAlexSpeed) | physics.c | 108061 |
| $3B56 | sub_3B56 | set xSpeed | physics.c | 112697 |
| $3B5A | leftBrake | leftBrake: brake a leftward motion, turn right | physics.c | 3992 |
| $3B77 | accelerateAlexRight | accelerateAlexRight | physics.c | 46994 |
| $3B95 | _LABEL_3B9C_ | friction on rightward motion if moving | physics.c | 15290 |
| $3B9A | applyFrictionMovingRight | applyFrictionMovingRight | physics.c | 8614 |
| $3BAA | rightBrake | rightBrake: brake a rightward motion, turn left | physics.c | 3822 |
| $3BC8 | _LABEL_3BCF_ | vehicles: slow down to a minimum speed | physics.c | 414 |
| $3BDA | _LABEL_3BE1_ | accelerate upwards | physics.c | 11812 |
| $3BF0 | _LABEL_3BF7_ | brake a rise | physics.c | 1680 |
| $3C0B | _LABEL_3C12_ | accelerate downwards | physics.c | 8070 |
| $3C21 | _LABEL_3C28_ | brake a descent | physics.c | 7388 |
| $3C3E | interactWithTile | interactWithTile (centre) | tiles.c | 194160 |
| $3C41 | interactWithTileAtOffset | interactWithTileAtOffset | tiles.c | 251472 |
| $3D00 | interactWithFloor | interactWithFloor (feet) | tiles.c | 124656 |
| $3D03 | interactWithFloorWithOffset | interactWithFloorWithOffset | tiles.c | 155370 |
| $3D59 | _LABEL_3D60_ | puzzle floor colour 3 | tiles.c | 880 |
| $3D5D | _LABEL_3D64_ | puzzle floor colour 1 | tiles.c | 640 |
| $3D61 | _LABEL_3D68_ | puzzle floor colour 4 | tiles.c | 320 |
| $3D65 | _LABEL_3D6C_ | puzzle floor colour 2 | tiles.c | 480 |
| $3D69 | _LABEL_3D70_ | puzzle floor colour 5 | tiles.c | 480 |
| $3D6B | sub_3D6B | level 17 floor puzzle step | tiles.c | 2800 |
| $3DB8 | _LABEL_3DBF_ | ghost floor | tiles.c | 1312 |
| $3E04 | tickJitter | stunned: shake (tickJitter) | physics.c | 150 |
| $3E39 | _LABEL_3E40_ | event entity $60: init | tiles.c | 2 |
| $3E49 | _LABEL_3E50_ | event entity $60: wait for idle Alex | tiles.c | 700 |
| $3E73 | sub_3E73 | event entity $60: show a static sprite | tiles.c | 2 |
| $3E89 | _LABEL_3E90_ | event entity $60: second sprite | tiles.c | 30 |
| $3E9F | _LABEL_3EA6_ | event entity $60: end | tiles.c | 60 |
| $3F55 | _LABEL_3F5C_ | camera, levels 1 and 17 | camera.c | 45411 |
| $3F66 | _LABEL_3F6D_ | camera, level 13 | camera.c | 2657 |
| $3F6E | _LABEL_3F75_ | camera, horizontal levels | camera.c | 68759 |
| $3FCA | _LABEL_3FD1_ | camera, levels 5 and 9 (crash into the water) | camera.c | 9635 |
| $401E | _LABEL_4025_ | camera, levels 11 and 16 (flip-screen) | camera.c | 6279 |
| $40E0 | _LABEL_40E7_ | camera: vertical sections | camera.c | 45411 |
| $411D | _LABEL_4124_ | camera: horizontal scroll with threshold | camera.c | 116720 |
| $4157 | _LABEL_415E_ | camera: keep vehicles on screen vertically | camera.c | 71516 |
| $4182 | loadAlexAnimationDescriptor | loadAlexAnimationDescriptor | graphics.c | 142946 |
| $41A3 | loadAlexSpriteDescriptor | loadAlexSpriteDescriptor | graphics.c | 158220 |
| $41AC | requestLevelTilesUpdateIfAlexTilesChanged | VBlank: load Alex's tiles if they changed | graphics.c | 219020 |
| $41B9 | loadAlexTilesToVRAM2000 | load a tile set to VRAM $2000 | graphics.c | 17102 |
| $41C1 | loadAlexTilesToVRAM | loadAlexTilesToVRAM | graphics.c | 17318 |
| $4501 | handleAction | handleAction (button 2) | actions.c | 1282 |
| $4538 | punch | punch | actions.c | 1232 |
| $4557 | _LABEL_455E_ | attack sprite + hit the tile at the fist | actions.c | 1756 |
| $4571 | _LABEL_4578_ | break a block, release its contents | actions.c | 279 |
| $45B7 | requestNametableChangeBackground | replace a block with background/water | actions.c | 331 |
| $45EA | _LABEL_45F1_ | skull box: stun | actions.c | 2 |
| $460E | _LABEL_4615_ | question box: next item | actions.c | 24 |
| $4620 | spawnMoneyBagAt | money bag | actions.c | 28 |
| $4627 | tickPunch | tickPunch | actions.c | 15122 |
| $463A | _LABEL_4641_ | throw magic capsule A | actions.c | 2 |
| $4647 | _LABEL_464E_ | throw magic capsule B | actions.c | 4 |
| $4652 | sub_4652 | launch the capsule | actions.c | 6 |
| $4688 | _LABEL_468F_ | no action (motorcycle) | actions.c | 10 |

Unreachable code: none of the 129 routines. Inside them, only the fallback
dispatches for values outside the game's tables (block contents tile > 12,
action state > 13, level outside 1-17: these jump through the original ROM
table like the original) are never taken. A source-coverage build of the
shadow binary (private, LLVM `-fprofile-instr-generate`) over the suite below
covers 99% of the module's lines; the rest are those fallbacks.

## Verification

```sh
cd port
make BUILD=build-alex LIFTED_EXTRA="$(ls src/game/wip/alex/*.c | tr '\n' ' ')" build-alex/shadow build-alex/lockstep
# shadow (always with --depth 12 so that nested lifted calls are compared)
./build-alex/shadow ../original.sms --depth 12 --mode play --seed S --frames 8000              # S = 1..8
./build-alex/shadow ../original.sms --depth 12 --mode play --seed N --level N --lives --frames 6000   # N = 1..17
./build-alex/shadow ../original.sms --depth 12 --script SCRIPT --level L --lives --frames F    # scripts below
# lockstep
./build-alex/lockstep ../original.sms --relaxed --mode play --seed S --frames 20000             # S = 1..8
./build-alex/lockstep ../original.sms --relaxed --mode play --seed N+20 --level N --lives --frames 10000
./build-alex/lockstep ../original.sms --relaxed --mode idle --frames 3000
./build-alex/lockstep ../original.sms --relaxed --script SCRIPT --level L --lives --frames F    # every script
```

Results: 61 shadow runs, 129/129 routines exercised, 0 mismatches;
62 relaxed lockstep runs (8 seeds x 20000 frames, 17 levels x 10000 frames,
idle, 36 scripted runs), all identical.

A mutation check (friction constant, and the entry flags of one probe call)
is caught by the shadow test (F' mismatch in the ground states), which
confirms that A'/F' are compared.

### Scenario scripts

Each line below is one script with `;` between commands (recreate with
`tr ';' '\n'`). They put Alex in rare situations with RAM pokes: `$C31A` =
state, `$C054` = action state, `$C05A` = its timer, `$C30C`/`$C30E` = x/y,
`$C31C` = action flags, `$C318` = stun timer, `$C8xx-$CDxx` = name table RAM
(tile byte, then attribute byte). Level 2 starts with Alex idle at x=$20,
y=$88 and no scroll, so his centre tile is at `$CC8A` and his floor tile at
`$CD0A`.

| script (level, frames) | purpose |
|---|---|
| `cane` (2, 1800) | Cane of Flight chosen on the pause map (exit calls $2A6E), flying, punching, losing it |
| `cane2` (2, 1800; 11, 1800) | Cane of Flight set directly, flights to the screen edges and walls |
| `moto` (2, 1800; 7, 1400) | motorcycle: riding, jumping, slowing down, wrecking |
| `capsule` (2, 1800) | magic capsule A (and B while the first flies) |
| `capsule2` (2, 1800) | magic capsule B |
| `janken` (2, 1800) | janken states $15-$19, $1A, walk to position from both sides |
| `misc` (2, 1800) | state $00, hatch state $11/$12, event entity $60 |
| `ladder` (2, 1800) | ladder tiles at the body, climbing, jumping off, ladder top |
| `event60` (2, 1800) | event entity $60 touching idle Alex (all its states) |
| `floor17` (17, 1500) | level 17 floor puzzle solved, wrong colour, ghost floor, hatch |
| `shop3` (10, 1700) | shop: enter, walk, leave on the motorcycle (spawn on the bike) |
| `stun` (2, 1100) | stun end in idle/walking/crouched, bullets through handleAction, water at the body |
| `ladder2` (2, 1100) | ladder top from the floor, reaching the floor above, jumping off left, puzzle tile outside level 17, crushed in a wall |
| `hatch2` (17, 1100) | shop door at the screen edge, hatch to the right |
| `money` (2, 800) | money bag tiles |
| `flip11` (11, 1400; 16, 1400) | flip-screen at the bottom line (down) |
| `peti` (2, 1200) | peticopter from the ground (crushed at once) |
| `boatland` (2, 900) | boat on land |
| `motofast` (7, 900; 2, 900) | motorcycle past x=$44 (camera) |
| `peti2` (2, 1400; 9, 1400) | peticopter: take off, climb to the top, walls, descend |
| `peti_money` (2, 800) | two money bags in one frame (pending name table change) |
| `tiles2` (2, 800) | puzzle floor outside level 17, water at the body while idle |
| `boathull` (2, 800) | boat hull hits a solid tile |
| `flipup` (11, 800) | flip-screen at the bottom line while rising (up) |
| `swim` (3, 1100) | stunned while swimming, surface exit tile |
| `peti_wall_left` (2, 800) | peticopter blocked on the left, RIGHT held |
| `peti_wall_right` (2, 900) | peticopter blocked on the right, LEFT held; handleAction with the motorcycle |
| `swim_ladder` (3, 1100) | ladder above the head while swimming |
| `shelf` (2, 800) | shop shelf tile, money at the name table ring edge |
| `vert1` (1, 800; 17, 800) | camera of levels 1/17 without vertical scroll and scrolling up |

```
cane: 100 keys 1;106 keys -;600 pause;640 poke C054 01;640 poke C05A FF;640 poke C05B 04;700 pause;720 keys U;800 keys R;850 keys D;900 keys L;950 keys UR;1000 keys 2;1006 keys -;1040 keys DL;1080 keys U;1200 keys U2;1206 keys D;1300 keys L;1400 keys R;1500 keys -;1520 poke C054 00;1600 keys R
cane2: 100 keys 1;106 keys -;600 poke C054 01;600 poke C05A FF;600 poke C05B 04;601 poke C31A 07;610 keys U;760 keys D;900 keys L;1000 keys R;1300 keys UL;1400 keys DR;1500 keys 2;1506 keys U;1600 keys -
moto: 100 keys 1;106 keys -;600 poke C31D 0F;600 poke C30F 40;600 poke C054 07;601 poke C31A 08;610 keys R;700 keys R1;720 keys R;760 keys R2;766 keys R;800 keys L;850 keys R1;870 keys R;900 keys 1;910 keys R;1000 keys L;1100 keys R1;1130 keys R
capsule: 100 keys 1;106 keys -;600 poke C054 03;610 keys 2;616 keys -;660 keys 1;670 keys 12;676 keys -;760 poke C054 04;770 keys 2;776 keys -;820 keys R;830 keys R2;836 keys R;900 keys 1;910 keys 2;916 keys -
capsule2: 100 keys 1;106 keys -;600 poke C054 04;610 keys 2;616 keys -;700 poke C31C 00;710 keys 2;716 keys -;760 keys 1;770 keys 12;776 keys -
janken: 100 keys 1;106 keys -;600 poke C31A 16;700 keys U;706 keys -;720 keys D;726 keys -;740 keys D;746 keys -;760 keys D;766 keys -;780 keys U;786 keys -;800 poke C31A 15;810 keys U;816 keys -;830 keys U;836 keys -;850 keys D;856 keys -;900 poke C31A 18;950 poke C31A 1A;1000 poke C318 20;1000 poke C31A 19;1200 poke C30C 60;1201 poke C31A 16;1400 poke C31A 01
misc: 100 keys 1;106 keys -;600 poke C31A 00;700 poke C31A 11;900 keys -;1100 poke C440 60;1100 poke C44C 20;1100 poke C44E 88;1300 keys R;1320 keys -
ladder: 100 keys 1;106 keys -;600 poke CC8A 3F;600 poke CC8B 60;600 poke CC4A 3F;600 poke CC4B 60;610 keys U;700 keys -;710 keys D;760 keys U;800 keys R;820 keys -;900 poke CD0A 3F;900 poke CD0B A0;910 keys D;1000 keys U;1040 keys L;1060 keys -;1100 poke CC8A 3F;1100 poke CC8B 60;1110 keys U;1150 keys D;1300 keys -
event60: 100 keys 1;106 keys -;700 poke C440 60;700 poke C44C 20;700 poke C44E 88;900 keys R;910 keys -
floor17: 100 keys 1;106 keys -;600 poke CD07 A0;600 poke CD06 21;620 poke CD06 11;640 poke CD06 19;660 poke CD06 0D;680 poke CD06 0D;700 poke CD06 21;720 poke CD06 19;740 poke CD06 11;760 poke CD06 15;780 poke CD06 0D;800 poke CD06 15;820 poke CD06 19;840 poke CD06 21;860 poke CD06 11;900 poke CD06 1D;1300 poke CD06 40;1310 keys D;1320 keys -
shop3: 100 keys 1;106 keys -;600 poke C069 10;601 poke C31A 0D;700 keys 2;706 keys -;740 keys 2;746 keys -;780 keys 2;786 keys -;820 keys 2;826 keys -;860 keys 2;866 keys -;900 peek C01F 1;1000 keys R;1100 keys L;1200 poke C054 07;1200 keys -;1210 peek C300 20;1250 keys U;1300 keys -;1400 peek C01F 1;1400 peek C300 20;1450 keys R;1500 keys R1;1520 keys R;1600 peek C300 20
stun: 100 keys 1;106 keys -;600 poke C31C 10;600 poke C318 08;650 keys R;660 poke C31C 10;660 poke C318 06;700 keys D;710 poke C31C 10;710 poke C318 06;760 keys -;800 poke C054 09;810 keys 2;816 keys -;850 poke C054 08;860 keys 2;866 keys -;900 poke CC8B 20
ladder2: 100 keys 1;106 keys -;600 poke CD0A 3F;600 poke CD0B A0;610 keys D;640 keys -;700 poke C31A 0A;710 keys U;740 keys -;800 poke C31A 0A;810 keys L;820 keys -;900 poke CD0A 0D;900 poke CD0B A0;950 poke CC8B 80
hatch2: 100 keys 1;106 keys -;600 poke CC86 70;600 poke CC87 60;610 keys U;640 keys -;700 poke C069 80;701 poke C31A 11
money: 100 keys 1;106 keys -;600 poke C202 80;600 poke CC8A 50;600 poke CC8B 40;700 poke CC8A 50;700 poke CC8B 40
flip11: 100 keys 1;106 keys -;600 poke C30E B0;800 keys R;810 keys R1;812 poke C30E B0;900 keys -;1000 poke C314 0B;1000 poke C30E B0;1200 poke C314 C1;1200 poke C312 00;1200 poke C311 00;1200 poke C30E B0
peti: 100 keys 1;106 keys -;600 poke C054 09;600 poke C31D 08;601 poke C31A 06;620 keys 1;622 keys -;630 keys 1;632 keys -;640 keys 1;642 keys -;650 keys 1;652 keys -;660 keys 1;662 keys -;670 keys 1;672 keys -;680 keys 1;682 keys -;690 keys 1;692 keys -;700 keys 1;702 keys -;710 keys 1;712 keys -;720 keys 1;722 keys -;730 keys L;790 keys LR;800 keys R;880 keys RL;900 keys L;950 keys -;1000 keys D;1100 keys -
boatland: 100 keys 1;106 keys -;600 poke C31D 0F;600 poke C31F 10;600 poke C054 08;601 poke C31A 0B;610 keys R
motofast: 100 keys 1;106 keys -;600 poke C31D 0F;600 poke C30F 40;600 poke C054 07;601 poke C31A 08;602 poke C30C 80;610 keys R
peti2: 100 keys 1;106 keys -;600 poke C054 09;600 poke C31D 08;600 poke C30E 70;601 poke C31A 06;620 keys 1;622 keys -;628 keys 1;630 keys -;636 keys 1;638 keys -;644 keys 1;646 keys -;652 keys 1;654 keys -;660 keys 1;662 keys -;668 keys 1;670 keys -;676 keys 1;678 keys -;684 keys 1;686 keys -;692 keys 1;694 keys -;700 keys 1;702 keys -;708 keys 1;710 keys -;716 keys 1;718 keys -;724 keys 1;726 keys -;732 keys 1;734 keys -;740 keys 1;742 keys -;748 keys 1;750 keys -;756 keys 1;758 keys -;770 keys L;830 keys LR;840 keys R;990 keys RL;1000 keys L;1100 keys -;1150 keys D;1300 keys -
peti_money: 100 keys 1;106 keys -;600 poke C054 09;600 poke C31D 08;601 poke C31A 06;620 poke CC4A 50;620 poke CC4B 40;620 poke CCCA 50;620 poke CCCB 40;620 keys 1;622 keys -
tiles2: 100 keys 1;106 keys -;600 poke CD0A 0D;600 poke CD0B A0;650 poke CD0B 80;700 poke CC8B 20
boathull: 100 keys 1;106 keys -;600 poke C31D 0F;600 poke C31F 10;600 poke C054 08;601 poke C31A 0B;602 poke CC4D 80
flipup: 100 keys 1;106 keys -;600 poke C31A 03;600 poke C314 01;600 poke C312 FE;600 poke C311 00;601 poke C30E B0
swim: 100 keys 1;106 keys -;700 poke C31C 10;700 poke C318 06;720 keys U;760 poke C892 59;760 poke C893 60;761 poke C8D2 59;761 poke C8D3 60;800 keys -;900 poke C852 3F;900 poke C853 60;900 keys U;1000 keys -
peti_wall_left: 100 keys 1;106 keys -;600 poke C054 09;600 poke C31D 08;600 poke C30E 70;601 poke C31A 06;620 keys 1;622 keys L;626 poke CAC5 80;626 poke CB05 80;626 poke CB45 80;632 keys LR;700 keys -
peti_wall_right: 100 keys 1;106 keys -;600 poke C054 09;600 poke C31D 08;600 poke C30E 70;601 poke C31A 06;620 keys 1;622 keys R;621 poke CB4F 80;621 poke CB8F 80;621 poke CBCF 80;624 keys RL;700 keys -;800 poke C054 07;801 keys 2;807 keys -
swim_ladder: 100 keys 1;106 keys -;900 poke C812 3F;900 poke C813 60;900 poke C852 3F;900 poke C853 60;901 keys U;1000 keys -
shelf: 100 keys 1;106 keys -;600 poke CC8A 90;600 poke CC8B 40;700 poke CCBC 50;700 poke CCBD 40;700 poke C0B0 08;700 poke C30C F0
vert1: 100 keys 1;106 keys -;600 peek C0C9 1;600 peek C300 20;650 poke C054 07;651 keys 2;657 keys -;700 poke C0C9 08;701 peek C0C9 1;750 poke C0C9 02;750 poke C30E B0;751 peek C0C9 1;751 peek C300 20
```
