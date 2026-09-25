# Module `states`: game-state machine and screens

Lifted code: `engine/src/game/states/` (83 routines, all lifted).

| file | contents |
|------|----------|
| `states.h` | shared constants (game states, sounds, Alex states/actions, entity slots, IRQ flags, banks) and small helpers (`wait_frame`, `destroy_entities`, `load_ath_pointer`, `level_song`, `copy_bytes`/`fill_bytes`, `CALL_HELPER`) |
| `title.c` | title screen, its VBlank animation, `startGame`, title sprite descriptors |
| `gameplay.c` | demo, gameplay init/update/VBlank, per-screen entity loader |
| `level.c` | level intro (map parchment) and level completed |
| `life_lost.c` | life lost, respawn spot search, game over and the hidden continue |
| `shop.c` | shop |
| `textbox.c` | text boxes / dialogues |
| `map.c` | pause map, inventory, item use, map exit |
| `bonus.c` | bonus levels, rooms of level 17, ending |
| `battle.c` | janken battles and the fight with Janken |

## The state machine

`v_gameState` ($C01F): low nibble = state, bit 7 = "initialized". The main
loop ($0045) runs `gameStateMainLoopPointers[state]` forever (after an `EXX`;
most handlers `EXX` back to get HL = v_gameState). The VBlank handler runs
`gameStateInterruptHandlersPointers[state]` only when bit 3 of the flags given
to `waitForInterrupt` is set **and** bit 7 of v_gameState is set.

A handler seeing bit 7 clear builds its screen and sets bit 7. So writing a
bare state number means "enter from scratch" and `state | $80` "resume"
(e.g. `$8A` = back to the running level without reloading it).

| state | handler(s) | enter / leave |
|---|---|---|
| 0, 1 title | `initOrUpdateTitleScreenState`, `handleInterruptTitleScreenState` | power-on, game over, demo end. Button 1/2 -> `startGame` (copies 25 initial bytes from $0824 to v_gameState.., i.e. state 3, level 1, 3 lives). $01D0 frames idle -> 2 |
| 2 demo | `updateDemoState` -> `initGameplayState`/`updateGameplayState`, `handleInterruptDemoState` | plays demo 1-4 (levels at $0A7C, input at $0A80, bank 5); any button -> 1, end of data -> 0 |
| 3 level starting | `updateLevelStartingState`, `handleInterruptLevelStartingState` | map parchment unrolls (one 2-tile column every 3 frames up to v_mapLoadingState = $15), then $50 frames with the arrow; -> $0A |
| 4 level completed | `updateLevelCompletedState` (VBlank: `ret`) | set by the level's end; first call cleans up, second call `v_level++` -> 3 |
| 5 shop | `updateShopState`, `handleInterruptShopState` | door -> 5; pending message -> 7 (returns $85); bit 6 set (walked out) -> $8A |
| 6 life lost | `updateLifeLostState` (VBlank: `ret`) | death -> 6; -> $8A/$89 (respawn), $0A (level restart), 0 (demo / game over) |
| 7 text box | `updateTextBoxState`, `handleInterruptTextBoxState` | v_textBoxMessageIndex set + state 7; button closes -> $89 (battle), $85 (shop) or $8A |
| 8 bonus | `updateBonusLevelState`, `handleInterruptBonusLevelState` | entity $4C; builds bonus level / level-17 room -> $8A; ending -> 6 (lives 1 -> game over) |
| 9 janken | same handlers as $A | battle running (v_hasBattleStarted) |
| $A gameplay | `initOrUpdateGameplayState` -> `initGameplayState` / `updateGameplayState`, `handleInterruptGameplayState` | pause (NMI) -> $B |
| $B map | `updateMapState`, `handleInterruptMapState` | pause again -> `exitMapState` -> $8A |

Timers and counts: title vignettes every $20 frames (first after $3C), logo
colour every 3 frames, demo after $01D0 frames; level intro $50 frames;
game over $C0 frames; continue = hold Up + 8 presses of button 2, costs $400
(the "400" entry of the score table), gives 3 lives and restarts the level;
ending: scroll speed $0039, $BD frames after the text.

### Register contract of the main-loop handlers

Because of the `EXX` pair in the main loop, whatever a handler leaves in BC/DE
(and BC') reaches the next handler, and the liveness analysis lists them as
outputs. The lifted handlers therefore keep the original's register values at
every exit: calls are made in the same order with the same inputs, and when
the original leaves a value from an `LDIR` or a table lookup the lifted code
sets it explicitly (e.g. `cpu.bc = 0` after a copy that precedes
`updateEntities`, which reads C; `level_song()` leaves BC = level).
Some routines use the alternate bank as scratch around calls
(`_LABEL_6EBB_`, `updateItemSelectArrow`, `updateBattleRespawOpponent`, the
octopus arms in `_LABEL_6F7E_`); the lifted code keeps the same `EXX` pairs
because BC'/DE'/HL' are compared.

## Variables (meanings found while lifting)

| variable | meaning |
|---|---|
| `v_inputFlags` bit 5 | a demo is playing (readInput leaves the pad alone) |
| `v_nextMapNametableUpdateTimer` ($C03E) | level intro: frames to the next column; text box: 1 = print a character at VBlank |
| `v_currentMapOrTextNametablePointer` ($C038) | map column source / text box character pointer |
| `v_textBoxCounter` ($C07D) | characters left in the current text segment; bit 7 = repeat the same character |
| `v_textBoxFlags` ($C07E) | cursor move after each character: $20 right, $60 left, $80 down, $C0 up, 0 = end |
| `v_textboxCursor` ($CFE0) | pseudo-entity whose x/y (pixels) is the text cursor |
| `_RAM_C074_` | set to $0100 when a text box opens (not read in this module) |
| `_RAM_C07F_` | shared: game-over countdown ($C0 frames); room variant in level 17 (from entity $4C's data) |
| `_RAM_C014_` | ending: frames to wait after the last line |
| `_RAM_C095_` | high byte of `v_endingSequencePointer`; cleared at the end marker = "ending text finished" |
| `_RAM_C08E_` | 1 in the levels whose broken blocks are remembered (11, 16): enables `v_metatileDeletesTable` |
| `targetBase_RAM_C07A_` / `targetBlock_RAM_C07A_` | block pointers while copying the persistent metatile deletes |
| `v_itemBeignBoughtIndex` ($C057) | shop: item just bought (the VBlank handler removes its picture); game over: button-2 press counter |
| `_RAM_D7D0_` | shop stock: 3 records (sold flag, name-table destination, picture), then $FF |
| `_RAM_D800_`..`_RAM_D807_` | per-level event flags, cleared at level completion; D800 set by entity $51 (starts the ending in level 17); D802 bit 0 set when level 10 is completed |
| `v_inventoryItemSelectionState` ($C053) | bit 0: an item was used this pause; bit 7: clear its picture in VRAM at VBlank |
| `v_selectedItemNametablePointer` | VRAM position of the used item's picture |
| `v_shopFlags` | bit 0 in the shop; bit 6 a purchase message was shown (next purchase once Alex lands) |
| `_RAM_C218_`, `_RAM_C219_`, `_RAM_C21B_` | queued name-table patches: count (+1) and list pointer (4-byte records copied to `v_nametableChangeDestination`); request $88 animation: step and frame data |
| `_RAM_C2A0_` | sprite descriptor of the battle score (entity $0C); round marks at $C2A6 + 2*round: $A4 Alex won, $A5 opponent won |
| `_RAM_C260_` | saved 46 bytes of the name table under the battle names ($C908) |
| `_RAM_CA08_` | thought-cloud area of the RAM name table ($EC bytes saved in `v_nametableCopy`) |
| `v_temporaryLevelDataCopy` ($C0CA) | level variables saved while in the shop / map |
| `v_nametableCopy` ($D000) | RAM name table saved while in the shop / map; thought-cloud backup in battles |
| `v_mapEntities` ($CF80) | entity slots used by the map (1 item arrow, 2 map arrow, 3 castle) and the shop (1 = Alex, 2 = saved Alex) |
| Alex during the respawn search | unknown6 = start y, unknown5 = column x, battleDecision = width to test, state = cells tested, unknown7 = saved x |
| janken opponent (slot 6) | unknown7 = round number, unknown6 = timer, unknown11 = thinking time, unknown1 = hits taken (fight) |

Level numbers used by the code: 1-17; level 2 starts on the motorcycle in the
demo; 11 and 16 have persistent block deletions (named Radactian castle and
Cragg lake in the reference); 13 always restarts from the beginning on death;
>= 16 shows Janken's castle on the map; 17 is the last level (rooms, ending).

## Original bugs / quirks kept

- **exitMapState with interrupts disabled** (`map.c`, QUIRK comment):
  `clearVDPTablesAndDisableScreen` disables interrupts and they stay off until
  the `EI` near the end, while `updateEntities` runs. If Alex is hit then, the
  death code waits for a VBlank with interrupts off and the original freezes
  (the runtime delivers the interrupt and prints a warning). Kept as is.
- **Shop VBlank handler** (`handleInterruptShopState`, QUIRK comment): after
  clearing the picture of the bought item, the scan of the level's item
  records continues with the registers left by `clearRamNametableArea` (HL =
  last cleared row, D = high byte of the address after it) instead of the next
  record. Harmless unless a byte of the RAM name table matches.
- The pause-screen tiles of the sun stone medallion's right half carry item
  code 7 (power bracelet); unreachable because the arrow is limited to
  x = $70-$E8.
- The ending's "clear line" command ($FE) splits its 64-byte clear into 32
  bytes at the row and 32 bytes at the top of the name table when D + E >= $FE
  (VDP address high + low byte), which only happens for row 26; a clear at the
  last row (27) is not split. Kept as is.
- Using the telepathy ball from the map sets action $06 and removes its
  picture but not the `v_hasTelepathyBall` flag.

## Status of the routines

All 83 routines are lifted. "calls" = compared calls summed over the 44
shadow scenarios of the Verification section (run with `--depth 12`, so
nested calls count too). The lifted name of `drawThoughtClouds@patchNametableWithThoughtCloud`
is `drawThoughtClouds_patchNametableWithThoughtCloud` (as in gen_funcs.h).

| addr | routine | file | calls | mismatches | exercised by |
|---|---|---|---:|---:|---|
| $076D | `initOrUpdateTitleScreenState` | title.c | 9095 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $080C | `startGame` | title.c | 96 | 0 | play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, bonus, throne, ending, battles |
| $0842 | `handleInterruptTitleScreenState` | title.c | 145504 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0872 | `showTitleUnderwaterFrame` | title.c | 1152 | 0 | idle, shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0881 | `showTitleBoatFrame` | title.c | 1120 | 0 | idle, shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0890 | `showTitleTreeFrame` | title.c | 512 | 0 | idle, lifelost, continue, demo_death, ending |
| $089C | `showTitlePeticopterFrame` | title.c | 512 | 0 | idle, lifelost, continue, demo_death, ending |
| $08AB | `showTitleJankenFrame` | title.c | 512 | 0 | idle, lifelost, continue, demo_death, ending |
| $08BA | `showTitlePushStartFrame` | title.c | 480 | 0 | idle, lifelost, continue, demo_death, ending |
| $08F6 | `loadTitleSprites` | title.c | 130 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0951 | `sub_0951` | title.c | 1040 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0966 | `return` | title.c | 104340 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $09E5 | `updateDemoState` | gameplay.c | 11303 | 0 | idle, lifelost, continue, demo_death, ending |
| $0A35 | `handleInterruptDemoState` | gameplay.c | 361120 | 0 | idle, lifelost, continue, demo_death, ending |
| $0A88 | `initOrUpdateGameplayState` | gameplay.c | 144867 | 0 | play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, bonus, throne, ending, battles |
| $0A8E | `updateGameplayState` | gameplay.c | 312182 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0AB1 | `handleInterruptGameplayState` | gameplay.c | 5351776 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $0ABD | `initGameplayState` | gameplay.c | 158 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $1650 | `updateBonusLevelState` | bonus.c | 2850 | 0 | bonus, throne, ending |
| $16A6 | `handleInterruptBonusLevelState` | bonus.c | 45504 | 0 | bonus, throne, ending |
| $18CD | `handleInterruptLevelCompletedState` | level.c | 32 | 0 | levelcomplete |
| $18CE | `updateLevelCompletedState` | level.c | 9 | 0 | levels (3/17), levelcomplete |
| $194F | `updateLevelStartingState` | level.c | 3286 | 0 | play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, bonus, throne, ending, battles |
| $1A01 | `handleInterruptLevelStartingState` | level.c | 16112 | 0 | play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, bonus, throne, ending, battles |
| $1BC9 | `updateShopState` | shop.c | 752 | 0 | shop |
| $1BEE | `handleInterruptShopState` | shop.c | 12000 | 0 | shop |
| $1FCD | `updateMapState` | map.c | 44653 | 0 | play seeds (4), levels (14/17), map2, bonus |
| $1FE6 | `handleInterruptMapState` | map.c | 713744 | 0 | play seeds (4), levels (14/17), map2, bonus |
| $1FE9 | `exitMapState` | map.c | 80 | 0 | play seeds (3), levels (10/17), map2, bonus |
| $2439 | `updateItemSelectArrow` | map.c | 178632 | 0 | play seeds (4), levels (14/17), map2, bonus |
| $24B6 | `pauseStateOnUpOrDownPressed` | map.c | 154832 | 0 | play seeds (4), levels (14/17), map2 |
| $24B7 | `_LABEL_24B7_` | map.c | 0 | 0 | not exercised (dead code) |
| $24CF | `_LABEL_24CF_` | map.c | 357264 | 0 | play seeds (4), levels (14/17), map2, bonus |
| $24EC | `copyPauseItemsToNametable` | map.c | 86 | 0 | play seeds (4), levels (14/17), map2, bonus |
| $2522 | `copyTileBlock` | map.c | 2460 | 0 | play seeds (4), levels (14/17), shop, map2, bonus |
| $2532 | `clearRamNametableArea` | map.c | 320 | 0 | play seeds (2), levels (1/17), shop, map2 |
| $255A | `useMagicCapsuleA` | map.c | 8 | 0 | map2 |
| $2568 | `useMagicCapsuleB` | map.c | 8 | 0 | map2 |
| $2576 | `_LABEL_2576_` | map.c | 8 | 0 | map2 |
| $2580 | `useCaneOfFlight` | map.c | 8 | 0 | map2 |
| $2594 | `useTeleportPowder` | map.c | 8 | 0 | map2 |
| $25A8 | `usePowerBracelet` | map.c | 32 | 0 | play seeds (2), levels (1/17), map2 |
| $25B4 | `_LABEL_25B4_` | map.c | 144 | 0 | play seeds (2), levels (1/17), map2 |
| $25D3 | `_LABEL_25D3_` | map.c | 8 | 0 | map2 |
| $6C05 | `updateLifeLostState` | life_lost.c | 108 | 0 | play seeds (5), levels (13/17), levelcomplete, lifelost, continue, demo_death, bonus, ending, battles |
| $6EA7 | `handleInterruptLifeLostState` | life_lost.c | 32 | 0 | levelcomplete |
| $6EA8 | `_LABEL_6EAF_` | life_lost.c | 246 | 0 | play seeds (5), levels (11/17), levelcomplete, lifelost, bonus |
| $6EB4 | `_LABEL_6EBB_` | life_lost.c | 1612 | 0 | play seeds (5), levels (11/17), levelcomplete, lifelost, bonus |
| $6F1A | `_LABEL_6F21_` | life_lost.c | 120 | 0 | play seeds (5), levels (11/17), levelcomplete, lifelost, bonus |
| $6F3D | `loadNewEntities` | gameplay.c | 624395 | 0 | idle, play seeds (5), levels (17/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $6F41 | `loadEntitiesNormal_LABEL_6F48_` | gameplay.c | 908980 | 0 | idle, play seeds (5), levels (15/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending |
| $6F77 | `_LABEL_6F7E_` | gameplay.c | 2904 | 0 | idle, play seeds (5), levels (16/17), shop, map2, levelcomplete, lifelost, continue, demo_death, bonus, throne, ending, battles |
| $7143 | `updateJanken` | battle.c | 208712 | 0 | battles |
| $7175 | `updateBattleInit` | battle.c | 120 | 0 | battles |
| $71C5 | `updateBattleLoadOpponentTilesAndShowTextbox1` | battle.c | 3200 | 0 | battles |
| $7209 | `updateBattleShowTextbox2` | battle.c | 152 | 0 | battles |
| $7228 | `isTextboxGameState` | battle.c | 1064 | 0 | battles |
| $7230 | `updateBattleStartRound` | battle.c | 544 | 0 | battles |
| $724A | `updateBattleDance` | battle.c | 274180 | 0 | battles |
| $7278 | `updateBattleThrow` | battle.c | 41712 | 0 | battles |
| $72AC | `updateBattleHandleThrows` | battle.c | 15840 | 0 | battles |
| $72D5 | `updateBattleRoundTie` | battle.c | 320 | 0 | battles |
| $72DF | `updateBattleRoundLost` | battle.c | 384 | 0 | battles |
| $730D | `updateBattleRoundWon` | battle.c | 352 | 0 | battles |
| $733B | `updateBattleShowBattleLostTextbox` | battle.c | 80 | 0 | battles |
| $7350 | `updateBattleTurnAlexIntoStatue` | battle.c | 80 | 0 | battles |
| $736B | `updateBattleRespawOpponent` | battle.c | 22960 | 0 | battles |
| $73A7 | `_LABEL_73AE_` | battle.c | 48 | 0 | battles |
| $73C4 | `updateBattleStartFight` | battle.c | 16 | 0 | battles |
| $73D1 | `_LABEL_73D8_` | battle.c | 2448 | 0 | battles |
| $7440 | `_LABEL_7447_` | battle.c | 16 | 0 | battles |
| $744C | `_LABEL_7453_` | battle.c | 16 | 0 | battles |
| $7468 | `_LABEL_746F_` | battle.c | 768 | 0 | battles |
| $749D | `updateBattlePatchNametable` | battle.c | 144 | 0 | battles |
| $74C6 | `updateBattleNop` | battle.c | 54620 | 0 | battles |
| $74C7 | `updateEntity0x19` | battle.c | 1152 | 0 | battles |
| $7502 | `_LABEL_7509_` | battle.c | 16 | 0 | battles |
| $7609 | `drawThoughtClouds@patchNametableWithThoughtCloud` | battle.c | 288 | 0 | battles |
| $761F | `updateEntity0x0B` | battle.c | 166206 | 0 | battles |
| $763A | `destroyBattleEntities` | battle.c | 176 | 0 | battles |
| $7966 | `restoreSomeNametableStuff_LABEL_796D_` | battle.c | 512 | 0 | battles |
| $7DC2 | `updateTextBoxState` | textbox.c | 9034 | 0 | levels (1/17), shop, battles |
| $7F22 | `handleInterruptTextBoxState` | textbox.c | 143504 | 0 | levels (1/17), shop, battles |

Unreachable: `_LABEL_24B7_` ($24B7) is never referenced (no call, jump or
pointer in the ROM): dead code, lifted but not exercisable.
`handleInterruptLevelCompletedState` and `handleInterruptLifeLostState` are
only called if v_gameState is $84/$86 during a VBlank with bit 3 set, which
the game never does (both states wait with flags 1/$80); exercised by poking
the state. `pauseStateOnUpOrDownPressed` is an empty routine.

## Verification

Build: `cd port && make BUILD=build-states LIFTED_EXTRA="$(ls src/game/wip/states/*.c | tr '\n' ' ')" build-states/shadow build-states/lockstep`

Shadow runs use `--depth 12` so that nested lifted calls (handlers called
from VBlank inside a main-loop handler, item routines called from
`updateEntities`...) are compared too. Scenario scripts are in the session
scratchpad (`states/scripts/`); they poke RAM to set up situations:

All 83 routines are lifted; 82 were compared with **0 mismatches** over 44
shadow scenarios (all `OK`), summed in the table above:

- `--mode idle --frames 3000` and `--mode idle --seed 2 --frames 6000` (title, demos)
- `--mode play --seed {1,2,3,4,7} --frames 8000` (deaths, game over, shops/maps by random pause)
- `--mode play --seed 10+N --level N --lives --frames 6000` for N = 1..17
- scripts: `shop` (enter by poke, refused and accepted purchases of several
  items, extra life, leave), `map2` (pause, every usable item incl. the
  Hirotta stone, up/down), `levelcomplete` (state 4, then $84 and $86 to reach
  the two trivial VBlank handlers), `lifelost` / `lifelost_inf` (ground search,
  swimming, peticopter, vehicle, boat, level 13, game over; also with
  `--level 3` and `--level 11` for the checkpoint path), `continue`,
  `continue_poor`, `demo_death`, `bonus` (`--level 2/5/9`: bonus level, pause
  and death inside it), `throne` (`--level 17`, both room variants), `ending`
  (`--level 17`, whole text scroll then game over), battles `b0 b2 b4 b6`
  (opponent poked into slot 6 in level 11; won, lost, tied rounds, statue,
  respawn) and `b1win` (Janken: fight, projectiles, defeat by poking state 15,
  floor animation, ladder, medallion).

Relaxed lockstep (`build-states/lockstep ../original.sms --relaxed ...`), all
`OK ... identical`: seeds 1-8 x 20000 frames, idle 20000, levels 1-17 x 20000
(`--lives`), and the scenario scripts above (42 runs).


## Open issues

- **Shadow harness end of run**: `tests/shadow.c` stops only on a frame waited
  at depth 0. With every main-loop handler lifted, every frame is waited inside
  a compared call, so a run never ended. Fixed in the harness itself: `tests/shadow.c` now also stops at the frame limit inside a session (the workaround `shadow_support.c` was removed on integration).
- Names kept from the reference where the meaning is unknown: `_LABEL_*`,
  `sub_0951`, entity `unknown*` fields; the text and comments give what is
  known.
