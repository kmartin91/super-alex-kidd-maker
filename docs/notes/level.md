# Module `level`: level engine (137 routines)

All 137 routines of `docs/notes/level-routines.txt` are lifted. Each one was called and
compared by the shadow test (`--depth 12`) with 0 mismatches. The relaxed lockstep runs
against the reference emulator pass, including a level mod built with `maker/tools/leveledit.py`.
Instruction coverage was measured with the lockstep `--cov` map: every one of the 2081
original instructions of these routines ran in the scenarios below. No routine is dead
code.

## Files

| file | contents |
|------|----------|
| `engine/src/game/level/level.h` | module overview (data flow), constants (mapper, name table, descriptor layout, scroll flags, buffer flags), **named aliases for the level RAM** (`v_columnCursor`, `v_rowHalf`, `v_topRowVdpAddress`...), entity byte offsets, `metatile_entry`, the `z80_af_after_*` helpers, cross-file prototypes (`level_*`) |
| `layout.c` | level descriptor (`read_level_descriptor`), `loadLevel`, screen fetch through the rows/columns tables (`level_fetch_screen_from_rows/columns`, `_LABEL_6841_`, `_LABEL_6B21_`, `sub_6B1E`), screen RLE (`level_decode_screen` = `_LABEL_6BEF_`), screen drops (`_LABEL_6671_`) |
| `scroll.c` | **the scrolling state machine** (`updateScroll`: horizontal left/right, vertical down/up), column builder (`loadLinesToNametable`), row builders (`_LABEL_6A76_`, `_LABEL_6A73_`) |
| `nametable.c` | RAM name-table mirror update (`updateNametable`), VDP upload (`draw`, `updateVdpAddressAfterDraw`), shop name-table RLE (`decompressNametable`, `@bitplane`) |
| `scroll_flags.c` | `updateScrollFlags` and the four per-level updaters (level 1, castles, levels 5/9, level 3) |
| `nametable_changes.c` | `handleNametableChangeRequest` and its handlers $80-$84, $88, $89 (`$4244`-`$43C3`), the name-table changer entity `$4B` (`$61C6`, `$6220`, `$6224`), `requestBlockSound` |
| `entities.c` | spawning a screen's normal entity records (`_LABEL_6F8F_`, `_LABEL_6FA6_`), castle entity loader (`loadEntitiesSpecial`) |
| `graphics.c` | background tileset loaders, `loadLevelTiles`, `loadLevelSpriteTiles`, palettes (`loadLevelPalette`, `updatePalette`, the 12 palette updaters), tile animation (`updateLevelTiles` and its updaters) |
| `sprite_tiles.c` | the 17 per-level sprite tile lists and the 37 single loaders |
| `vehicles.c` | four Alex helpers that sit in the $43EB-$44FF range: wreck exit, vehicle explosion, boat/Peticopter fire, swimming punch |
| `scenarios/*.txt` | the shadow/lockstep scripts used below (each starts with `#` lines giving the command-line options) |

Cross-file helpers are prefixed `level_`. Tile destinations are written as tile numbers
(`TILE(57)`, `SPRITE_TILE(68)`, tile counts), matching the tables of
`docs/level-format.md` section 8.

## Verification

Build (private directory):

```
cd port && make BUILD=build-level LIFTED_EXTRA="$(ls src/game/wip/level/*.c | tr '\n' ' ')" build-level/shadow build-level/lockstep
```

The same 82 scenarios were run in both harnesses: the shadow test with `--depth 12`
(the routines nest: `loadLevel -> updateScroll -> _LABEL_6841_ -> _LABEL_6BEF_`, etc.) and
the relaxed lockstep with `--cov`. Results: shadow 82/82 `OK ... 0 lifted routine(s) with
mismatches`; lockstep 81/82 `OK ... identical [relaxed]`, and one `STOP` (see open issues).
The scenarios were:

```
S=src/game/wip/level/scenarios
--mode idle --frames 3000
--mode play --seed S --frames 20000                      # S = 1, 2, 3, 5, 7
--mode play --seed S --level N --lives --frames 20000    # N = 1..17, S = 3, 11, 21 (51 runs)
--script $S/sub_area_level3.txt --level 3 --lives --frames 4000       (plus a shorter variant)
--script $S/sub_area_level3_pause.txt --level 3 --lives --frames 1200
--script $S/sub_area_level17_door.txt --level 17 --lives --frames 1400
--script $S/sub_area_level17_puzzle.txt --level 17 --lives --frames 1400
--script $S/changer_touch.txt --level 2 --lives --frames 450          (twice)
--script $S/changer_punch.txt --level 2 --lives --frames 560
--script $S/changer_count.txt --level 2 --lives --frames 560
--script $S/requests.txt --level 3 --lives --frames 800               (and --level 16)
--script $S/castle_up.txt --level 11 --lives --frames 750
--script $S/castle_top.txt --level 11 --lives --frames 900
--script $S/level13_left_edge.txt --level 13 --lives --frames 1400
--script $S/level9_end.txt --level 9 --lives --frames 2800
--script $S/level9_end_crash.txt --level 9 --lives --frames 1700
--script $S/crash_far.txt --level 9 --lives --frames 1400             (and --level 5)
--script $S/level3_drop.txt --level 3 --lives --frames 1400
--script $S/pause_map.txt --level N --lives --frames 1300             # N = 2, 3, 16
--mod PATCH --level 2 --mode play --seed S --lives --frames 20000     # S = 1, 3
--mod PATCH --script $S/mod_level2.txt --level 2 --lives --frames 800
```

What the situations cover:

* **Vertical levels** 1 and 17 (level runs), level 1's switch to horizontal ($645B).
* **Drops**: level 3 through the ENTER-DOWN hole (`level3_drop`: Alex state $11 is poked,
  he swims to the hole x and drops), levels 5/9 by crashing (random play, and `crash_far`
  from screen 12: lower screen 3, where scrolling stops).
* **Castles** 11 and 16: random play (auto-walk, right/down moves), upward moves and the
  refused move above the top row (`castle_up`, `castle_top`: Alex is poked above the top of
  the screen, which the castle camera handler turns into an upward move). `castle_up` also
  flags a position in the entered room's record (erased on entry) and sends two request
  $80 whose positions are not listed.
* **Level 13**'s left edge (`level13_left_edge`) and the end of a right-scrolling level
  (`level9_end`).
* **Pause map**: random play pauses now and then; `pause_map` and `sub_area_level3_pause`
  open and close it explicitly (exitMapState: palette, tiles, sprite tiles, scroll registers).
* **Block breaking**: the demo and random play punch rocks and star/skull/question boxes
  (request $80 from `_LABEL_4578_`, `requestBlockSound`), and castle blocks get recorded.
* **Shop doors**: random play enters shops (request $81, `decompressNametable`), `requests` too.
* **Sub-areas** (bonus state 8): entered by poking `v_gameState = $08` (what entity $4C
  does), for level 3 (level 4's layout and graphics, walked through to the end) and level
  17 (both rooms).
* **Name-table changer** ($4B): poked into slot 28 with a 2-patch list at $D8A0, for the
  three kinds (touch, punch, punch counter).
* **Octopus pot and special requests** ($82, $83, $84, $89, Janken's petrification $88 with
  the source pointer $AAFE that the battle code sets): poked in `requests`.

Pokes only shortcut long play to reach these situations; the game code then runs normally.

**Level mod.** `python3 maker/tools/leveledit.py export original.sms 2` (to a scratch file), then
rocks and money bags were added to screens 3 and 0 of the model, saved as `level_02.json` in
a scratch mod directory and built with `python3 maker/tools/leveledit.py build original.sms DIR
DIR/patch.bin`. The mod moves level 2 to bank 9 (`v_levelBankNumber` = $89). Lockstep
`--mod DIR/patch.bin --level 2 --relaxed` (seeds 1, 3, 5, 20000 frames) and the shadow test
pass, and a screenshot shows the new blocks after scrolling.

**Sanity check of the test**: see the end of this file.

## Routine status

Calls are summed over the 82 shadow scenarios (compared calls at any nesting depth up to 12).

| addr | routine | file | calls | mismatches |
|---|---|---|---:|---:|
| 0E41 | `decompressNametable` | nametable.c | 4 | 0 |
| 0E4B | `decompressNametable@bitplane` | nametable.c | 8 | 0 |
| 0E6C | `loadLevelTiles` | graphics.c | 407 | 0 |
| 0E9F | `loadMtEthernalTileset` | graphics.c | 146 | 0 |
| 0EC9 | `loadTheBlakwoodsTileset` | graphics.c | 24 | 0 |
| 0EDF | `loadJankensCastleTileset` | graphics.c | 42 | 0 |
| 0F00 | `loadBingooLowlandTileset` | graphics.c | 70 | 0 |
| 0F21 | `loadLakeFathomTileset` | graphics.c | 52 | 0 |
| 0F54 | `loadTheIslandOfStNurariTileset` | graphics.c | 70 | 0 |
| 0F6C | `loadTheVillageOfNamuiTileset` | graphics.c | 52 | 0 |
| 0F99 | `loadLakeFathomPart2Tileset` | graphics.c | 16 | 0 |
| 0FAE | `loadMtKaveTileset` | graphics.c | 28 | 0 |
| 0FC6 | `loadRiverTileset` | graphics.c | 38 | 0 |
| 0FF9 | `loadMtEthernalStage2Tileset` | graphics.c | 250 | 0 |
| 1022 | `loadCraggLakeTileset` | graphics.c | 40 | 0 |
| 1058 | `loadTheKingdomOfNibanaPart1Tileset` | graphics.c | 18 | 0 |
| 107C | `updatePalette` | graphics.c | 5351556 | 0 |
| 1089 | `paletteUpdater_LABEL_1089_` | graphics.c | 2341608 | 0 |
| 10B0 | `_LABEL_10B0_` | graphics.c | 12070232 | 0 |
| 10DE | `paletteUpdater_LABEL_10DE_` | graphics.c | 523200 | 0 |
| 10E1 | `paletteUpdater_LABEL_10E1_` | graphics.c | 294144 | 0 |
| 10E4 | `paletteUpdater_LABEL_10E4_` | graphics.c | 769088 | 0 |
| 10E7 | `paletteUpdater_LABEL_10E7_` | graphics.c | 383480 | 0 |
| 10EA | `paletteUpdater_LABEL_10EA_` | graphics.c | 463272 | 0 |
| 10ED | `paletteUpdater_LABEL_10ED_` | graphics.c | 574392 | 0 |
| 10F0 | `paletteUpdater_LABEL_10F0_` | graphics.c | 516624 | 0 |
| 10F3 | `paletteUpdater_LABEL_10F3_` | graphics.c | 151944 | 0 |
| 10F6 | `paletteUpdater_LABEL_10F6_` | graphics.c | 442888 | 0 |
| 10F9 | `paletteUpdater_LABEL_10F9_` | graphics.c | 131032 | 0 |
| 10FC | `paletteUpdater_LABEL_10FC_` | graphics.c | 614248 | 0 |
| 10FF | `loadLevelPalette` | graphics.c | 407 | 0 |
| 1134 | `loadLevelSpriteTiles` | graphics.c | 406 | 0 |
| 1164 | `loadMtEthernalSpriteTiles` | sprite_tiles.c | 114 | 0 |
| 117F | `loadMtEthernalStage2SpriteTiles` | sprite_tiles.c | 60 | 0 |
| 119D | `loadLakeFathomSpriteTiles` | sprite_tiles.c | 52 | 0 |
| 11B5 | `loadTheIslandOfStNurariSpriteTiles` | sprite_tiles.c | 42 | 0 |
| 11D3 | `loadLakeFathomPart2SpriteTiles` | sprite_tiles.c | 16 | 0 |
| 11EB | `loadTheVillageOfNamuiSpriteTiles` | sprite_tiles.c | 32 | 0 |
| 1206 | `loadMtKaveSpriteTiles` | sprite_tiles.c | 28 | 0 |
| 1221 | `loadTheBlakwoodsSpriteTiles` | sprite_tiles.c | 24 | 0 |
| 1239 | `loadRiverSpriteTiles` | sprite_tiles.c | 38 | 0 |
| 1254 | `loadBingooLowlandSpriteTiles` | sprite_tiles.c | 24 | 0 |
| 126F | `loadTheRadactianCastleSpriteTiles` | sprite_tiles.c | 28 | 0 |
| 1299 | `loadTheCityOfRadactianSpriteTiles` | sprite_tiles.c | 18 | 0 |
| 12AE | `loadSwampSpriteTiles` | sprite_tiles.c | 190 | 0 |
| 12C0 | `loadTheKingdomOfNibanaPart1SpriteTiles` | sprite_tiles.c | 18 | 0 |
| 12CF | `loadTheKingdomOfNibanaPart2SpriteTiles` | sprite_tiles.c | 46 | 0 |
| 12ED | `loadJankensCastleSpriteTiles` | sprite_tiles.c | 42 | 0 |
| 1311 | `loadCraggLakeSpriteTiles` | sprite_tiles.c | 40 | 0 |
| 132F | `loadSmokePuffTiles` | sprite_tiles.c | 1624 | 0 |
| 133B | `loadMonsterBirdTiles` | sprite_tiles.c | 800 | 0 |
| 1350 | `loadMermanBubblesTiles` | sprite_tiles.c | 384 | 0 |
| 135C | `loadMermanTiles` | sprite_tiles.c | 384 | 0 |
| 1368 | `loadSmallFishTiles` | sprite_tiles.c | 520 | 0 |
| 137D | `loadKillerFishTiles` | sprite_tiles.c | 364 | 0 |
| 1392 | `loadMonkeyLeafTiles` | sprite_tiles.c | 316 | 0 |
| 139E | `loadMonkeyTiles` | sprite_tiles.c | 316 | 0 |
| 13AA | `loadMonsterFrogTiles` | sprite_tiles.c | 388 | 0 |
| 13B6 | `loadPlantTiles` | sprite_tiles.c | 808 | 0 |
| 13C2 | `loadSeaHorseTiles` | sprite_tiles.c | 104 | 0 |
| 13DA | `loadStNurariTiles` | sprite_tiles.c | 84 | 0 |
| 13E6 | `loadFlyingFishTiles` | sprite_tiles.c | 108 | 0 |
| 13FB | `loadOxTiles` | sprite_tiles.c | 64 | 0 |
| 1425 | `loadRollingRockTiles` | sprite_tiles.c | 196 | 0 |
| 1431 | `loadGrizzlyBearTiles` | sprite_tiles.c | 48 | 0 |
| 143D | `loadLightningTiles` | sprite_tiles.c | 48 | 0 |
| 1449 | `loadDarkCloudTiles` | sprite_tiles.c | 48 | 0 |
| 1455 | `loadFlameTiles` | sprite_tiles.c | 640 | 0 |
| 1461 | `loadScorpionTiles` | sprite_tiles.c | 640 | 0 |
| 1476 | `loadEgleTiles` | sprite_tiles.c | 56 | 0 |
| 1482 | `loadMoonlightStoneMedallionTiles` | sprite_tiles.c | 164 | 0 |
| 148E | `loadPrincessLoraTiles` | sprite_tiles.c | 84 | 0 |
| 149A | `loadBatTiles` | sprite_tiles.c | 160 | 0 |
| 14A6 | `loadGreenDebrisTiles` | sprite_tiles.c | 1624 | 0 |
| 14B2 | `loadDebrisATiles` | sprite_tiles.c | 1624 | 0 |
| 14BE | `loadDebrisBTiles` | sprite_tiles.c | 1276 | 0 |
| 14CA | `loadBlueDebrisTiles` | sprite_tiles.c | 300 | 0 |
| 14D6 | `loadWoodsDebrisTiles` | sprite_tiles.c | 48 | 0 |
| 14E2 | `loadTelapathyBallTiles` | sprite_tiles.c | 112 | 0 |
| 14EE | `loadLetterTiles` | sprite_tiles.c | 56 | 0 |
| 14FA | `loadHirottaStoneTiles` | sprite_tiles.c | 36 | 0 |
| 1506 | `loadGoldCrownTiles` | sprite_tiles.c | 80 | 0 |
| 1512 | `loadVillageElderTiles` | sprite_tiles.c | 64 | 0 |
| 151E | `loadTeleportPowderTiles` | sprite_tiles.c | 84 | 0 |
| 154A | `loadPeticopterTiles` | sprite_tiles.c | 84 | 0 |
| 1561 | `loadSunStoneMedallionTiles` | sprite_tiles.c | 80 | 0 |
| 158F | `updateLevelTiles` | graphics.c | 5837136 | 0 |
| 15AF | `getFourFrameTileAddress` | graphics.c | 99344 | 0 |
| 15BC | `getSixFrameTileAddress` | graphics.c | 480240 | 0 |
| 15C6 | `resetAnimatedTileTimer` | graphics.c | 104336 | 0 |
| 15C8 | `getAnimatedTileAddress` | graphics.c | 579584 | 0 |
| 15D2 | `updateWaterTilesA` | graphics.c | 206752 | 0 |
| 15DF | `updateSwampTiles` | graphics.c | 239712 | 0 |
| 15EC | `updateLavaTilesA` | graphics.c | 31104 | 0 |
| 15F9 | `updateWaterTilesB` | graphics.c | 33776 | 0 |
| 1612 | `updateLavaTilesB` | graphics.c | 34464 | 0 |
| 161F | `doNotUpdateTiles` | graphics.c | 104128 | 0 |
| 4222 | `handleNametableChangeRequest` | nametable_changes.c | 6271976 | 0 |
| 4244 | `_LABEL_424B_` | nametable_changes.c | 6112 | 0 |
| 42AF | `_LABEL_42B6_` | nametable_changes.c | 32 | 0 |
| 42BC | `_LABEL_42C3_` | nametable_changes.c | 32 | 0 |
| 42C6 | `sub_42C6` | nametable_changes.c | 128 | 0 |
| 42F1 | `sub_42F1` | nametable_changes.c | 128 | 0 |
| 4339 | `handleShopDoorNametableChange` | nametable_changes.c | 112 | 0 |
| 4348 | `_LABEL_434F_` | nametable_changes.c | 48 | 0 |
| 4358 | `_LABEL_435F_` | nametable_changes.c | 320 | 0 |
| 4376 | `_LABEL_437D_` | nametable_changes.c | 1536 | 0 |
| 43C3 | `_LABEL_43CA_` | nametable_changes.c | 48 | 0 |
| 43EB | `_LABEL_43F2_` | vehicles.c | 688 | 0 |
| 440E | `_LABEL_4415_` | vehicles.c | 1478 | 0 |
| 444C | `_LABEL_4453_` | vehicles.c | 1104 | 0 |
| 44DB | `_LABEL_44E2_` | vehicles.c | 10652 | 0 |
| 5BFA | `requestBlockSound` | nametable_changes.c | 7712 | 0 |
| 61C6 | `updateNametableChanger` | nametable_changes.c | 1250 | 0 |
| 6220 | `updateNametableChanger@setUnknown6ToOne` | nametable_changes.c | 36 | 0 |
| 6224 | `updateNametableChanger@unkown6IsNotZero` | nametable_changes.c | 72 | 0 |
| 6457 | `updateScrollFlags` | scroll_flags.c | 730462 | 0 |
| 645B | `scrollFlagsUpdater_LABEL_6462_` | scroll_flags.c | 1104970 | 0 |
| 6476 | `scrollFlagsUpdater_LABEL_647D_` | scroll_flags.c | 159968 | 0 |
| 6532 | `scrollFlagsUpdater_LABEL_6539_` | scroll_flags.c | 99844 | 0 |
| 6574 | `scrollFlagsUpdater_LABEL_657B_` | scroll_flags.c | 96142 | 0 |
| 65AA | `loadLevel` | layout.c | 186 | 0 |
| 666A | `_LABEL_6671_` | layout.c | 92 | 0 |
| 67BD | `updateScroll_LABEL_67C4_` | scroll.c | 779831 | 0 |
| 683A | `_LABEL_6841_` | layout.c | 546 | 0 |
| 685E | `loadLinesToNametable_LABEL_6865_` | scroll.c | 16609 | 0 |
| 6919 | `draw` | nametable.c | 5886315 | 0 |
| 69AE | `updateVdpAddressAfterDraw` | nametable.c | 5886766 | 0 |
| 6A6C | `_LABEL_6A73_` | scroll.c | 194 | 0 |
| 6A6F | `_LABEL_6A76_` | scroll.c | 2530 | 0 |
| 6B1A | `_LABEL_6B21_` | layout.c | 194 | 0 |
| 6B1E | `sub_6B1E` | layout.c | 197 | 0 |
| 6B42 | `updateNametable_LABEL_6B49_` | nametable.c | 779858 | 0 |
| 6BE8 | `_LABEL_6BEF_` | layout.c | 743 | 0 |
| 6F88 | `_LABEL_6F8F_` | entities.c | 1105 | 0 |
| 6F9F | `_LABEL_6FA6_` | entities.c | 1121 | 0 |
| 707D | `loadEntitiesSpecial_LABEL_6F48_` | entities.c | 79984 | 0 |

## The scrolling engine

State (all in the $C0A0-$C0C9 block, cleared at level start, copied to $C0CA for the
room/life-lost snapshot):

* **Speeds** `v_horizontalScrollSpeed` / `v_verticalScrollSpeed` (8.8). `updateAlex` clears
  both every frame and the level's camera handler sets them. A horizontal speed wins: the
  vertical scroll is not run in that frame. Positive horizontal = camera moves left;
  positive vertical = camera moves down.
* **Scroll registers** `v_horizontalScroll` / `v_verticalScroll` (8.8). Their high bytes
  are VDP R8/R9, written in VBlank by `updateVdpAddressAfterDraw`. The vertical one is kept
  in 0-$DF: the name table has 28 rows, the screen 24.
* **Accumulators** `$C0AD` / `$C0BB`: pixels scrolled since the last 8-px cell. Crossing 8
  (or going below 0) triggers a new column / row.
* **Column cursor** (0-31: the next column of the screen grid), stored three ways:
  `v_columnHalf` $C0B3 = col & 1, `v_columnCursor` $C0B4 = col × $80 (high byte $C0B5 =
  metatile column), `v_nametableColumn` $C0B7 = col × 2.
* **Row cursor** (0-23): `v_rowHalf` $C0C1, `v_rowCursor` $C0C2 = row × $80 (high byte
  $C0C3 = metatile row).
* **Screen numbers** `v_horizontalScreenNumber` h, `v_verticalScreenNumber` v, and
  `v_currentScreenNumber`. The last one is the entity index; bit 7 (NEW_SCREEN) means
  "load this screen's entities".
* **Name-table anchors**:
  * `v_topRowVdpAddress` $C0C5: the VDP address of the name-table row at the top of the
    screen grid. It moves by one row per 8 px of vertical scroll and wraps $7EC0 <-> $7800.
  * `v_rowVdpAddress` $C0B7-8: $78 << 8 | column offset, the VDP address in row 0 where a
    row buffer starts.

Horizontal, camera moving **right** (negative speed), at each 8-px boundary:

1. If `SCROLL_RIGHT` is clear, the speed is zeroed and nothing is built.
2. If the cursor is at column 0, the next screen `rows[v][h+1]` is decoded, and
   v_currentScreenNumber + 1 | NEW_SCREEN.
3. The column at the cursor is built, then the cursor advances.
4. When the cursor wraps 31 -> 0, h + 1. When h reaches `v_levelWidth`, `SCROLL_RIGHT` is
   cleared and the screen number is taken back (−1, no NEW_SCREEN).

Camera moving **left** (positive speed), at each boundary:

1. The cursor steps back first.
2. When it wraps 0 -> 31, h − 1 and `rows[v][h]` is decoded (v_currentScreenNumber − 1 |
   NEW_SCREEN).
3. At h = 0 the move is refused: `SCROLL_LEFT` is cleared, the speed zeroed and R8 set to 8.
4. The column is built at the new cursor. Because the name table is exactly one screen
   wide, the column entering on the left and the next column on the right are the same
   name-table column.

Vertical, camera moving **down**, every 8 px:

1. `v_topRowVdpAddress` + 1 row, then the row cursor advances.
2. The row built is the one entering below the screen.
3. When the cursor wraps 23 -> 0, v + 1 (v_currentScreenNumber + 1 | NEW_SCREEN) and
   `columns[h][v+1]` is decoded.
4. When v reaches `v_levelHeight`: v = 0, `SCROLL_DOWN` cleared, R9 snapped to a multiple
   of 16, and the screen number taken back. Horizontal scrolling then continues on row 0 of
   the rows table (level 1).

Camera moving **up**: the mirror image, with `columns[h][v]` fetched after v − 1. At v = 0
`SCROLL_UP` is cleared and the speed zeroed.

**One decoded screen.** $D700 holds the screen the columns/rows are taken from:
* moving right: the screen entering on the right;
* moving left: the screen on the left;
* moving down: the screen below.

Reversing direction in the middle of a screen would therefore build columns/rows from the
wrong screen. The original levels never do it: scrolling is one-way per level part, and
castle moves are exactly one screen long. An editor that wants free two-way scrolling
needs a second decoded screen or a re-fetch on reversal (in `scroll_left/scroll_right`).

**First screen** (`loadLevel`, and the sub-area loader $1735): starting at h = startX with
the camera at 0, the engine "scrolls left" 256 × 1 px. This builds the 32 columns of
screen startX−1 from right to left, and each column is copied to the mirror and the VDP at
once.

**Per-level updaters** (`updateScrollFlags`, every frame before the scroll):
* **$645B**, level 1: when the descent ends (flags = SCROLL_SPECIAL only), R0 = $26 and
  flags = right.
* **$6574** (level 3) and **$6532** (levels 5, 9):
  * while horizontal, a vertical flag means a drop has started: `SCROLL_RIGHT` is replaced
    by `SCROLL_SPECIAL`;
  * when the drop ends: v = 1, the column cursor is reset, and scrolling continues right on
    the lower row (width 1, or 3 if the drop was from screen h < 12).
* **$6476** (castles): see the next paragraph.

**Castles.** The camera handler ($401E) starts a one-screen move and keeps its speed at
±4 px. $6476:
* masks column 0 during horizontal moves;
* when the new room starts to enter (NEW_SCREEN), erases from $D700 the positions flagged
  in the room's record ($D900 + row × $100 + column × $20);
* when the scroll and the accumulator are both back at 0, ends the move.

In castles `v_currentScreenNumber` = h + v, because every move changes it by ±1. The
castle entity loader keeps the real room index in `v_entityIndex` (row × width + column).

## Name-table pipeline

```
decoded screen ($D700) --(metatile table, bank 5)--> column buffer $CF00 (24 words)
                                                 \--> row buffer    $CF38 (32 words)
v_nametableBuffersReady ($C0AA): bit 0 column ready, bit 1 row ready
main loop:  updateScroll -> updateNametable: buffers -> RAM mirror $C800 (collision)
VBlank:     draw: buffers -> VDP $3800, then R8/R9, flags cleared
```

* **Metatile words.** A metatile is 4 name-table words: TL, TR, BL, BR.
  * The column builder takes words 0/2 (left half) or 1/3 (right half).
  * The row builder takes words 0-1 (top half) or 2-3 (bottom half).
  * `metatile_entry(id)` = word at `v_metatileTable + 2 × id`, with bank 5 mapped.
* **Column destination.** The name-table column comes from the fine horizontal scroll
  (`incoming_column_offset`): the right edge when moving right; moving left, the (masked)
  left edge, 8 px back.
* **Row destination.** The row comes from the fine vertical scroll (`incoming_row_line`):
  just under the screen when moving down, the top row when moving up. The row buffer
  starts at column `v_nametableColumn` and wraps within the 32 columns.
* **Change requests** (`v_nametableChangeRequest`, processed in VBlank) patch both copies
  of the name table:
  * $80: one metatile (breaking blocks, taking money, name-table changer patches). In
    castles it also records the broken position (see Castles above).
  * $81 and $82 only rewrite attribute bytes on the VDP (priority bit, so that Alex passes
    behind the door or pot). The mirror is unchanged.
  * $83 and $84: water above the pot, the pot graphics on the VDP, and in the mirror
    either the enterable ($8400) or the closed ($8410) collision words.
  * $89: the level-17 doorway.
  * $88: Janken's petrification, 48 steps, one per frame; the handler requests itself
    again until done.

## RAM variables (meanings)

| address | generated name | alias in `level.h` / meaning |
|---|---|---|
| $C0A1 | `v_linesToLoadToNametable` | `v_columnRowsLeft`: metatiles left in the column being built (12) |
| $C0A0 | `v_levelWidth` | last horizontal screen; read as a word by `ld bc,(v_levelWidth)`, so B = $C0A1 (0 after a column) |
| $C0A2 | `v_levelData_C0A2` | not used by the level code |
| $C0A3 | `v_levelLayoutPointer` | `v_rowsTable` |
| $C0A6 | `v_columnsToLoadToNametable` | `v_rowColumnsLeft`: metatiles left in the row being built (16) |
| $C0A7 | `v_levelData_C0A7` | not used by the level code |
| $C0A8 | `v_SecondLevelLayoutPointer` | `v_columnsTable` |
| $C0AA | `v_UpdateNameTableFlags` | `v_nametableBuffersReady` (bit 0 column, bit 1 row) |
| $C0AC | (high byte of the speed) | `v_horizontalScrollSpeedHigh`: bit 7 = moving right |
| $C0AE | (high byte of the accumulator) | `v_horizontalColumnPixels`: 0-7 |
| $C0B0 | `v_levelData_C0B0` | `v_horizontalScrollPixel`: VDP R8 value |
| $C0B2 | `v_levelData_C0B2` | not used by the level code |
| $C0B3 | `v_levelData_C0B3_` | `v_columnHalf` |
| $C0B4 | `v_levelData_C0B4_` | `v_columnCursor` (word, column × $80) |
| $C0B5 | `v_levelData_C0B5_` | `v_metatileColumn` |
| $C0B7 | `v_levelData_C0B7_` | `v_nametableColumn` (column × 2); word with $C0B8 = `v_rowVdpAddress` |
| $C0BA | `v_levelData_C0BA_` | `v_verticalScrollSpeedHigh`: bit 7 = moving up |
| $C0BB | `v_levelData_C0BB_` | `v_verticalScrollAccumulator` |
| $C0BC | `v_levelData_C0BC_` | `v_verticalRowPixels` (0-7); also the vertical offset of castle entities |
| $C0BE | (high byte of `v_verticalScroll`) | `v_verticalScrollLine`: VDP R9 value, 0-$DF |
| $C0BF, $C0C0, $C0C7, $C0C8 | `v_levelData_*` | not used by the level code |
| $C0C1 | `v_levelData_C0C1_` | `v_rowHalf` |
| $C0C2 | `v_levelData_C0C2_` | `v_rowCursor` (word, row × $80) |
| $C0C3 | `v_levelData_C0C3_` | `v_metatileRow` |
| $C0C5 | `v_levelData_C0C5_` | `v_topRowVdpAddress` |
| $C087 | `v_metatileNametablePointer` | `v_metatileTable` (bank 5) |
| $C08E | `_RAM_C08E_` | `v_castleBlocksEnabled` (levels 11, 16) |
| $C077 | `v_specialLevelScrollFlags` | `v_roomRecordApplied`: persistent deletes done for this castle move |
| $C078 | `targetBase_RAM_C07A_` | `v_roomRecord`: this room's record in $D900 |
| $C05C | `v_shouldBlankLeftmostColumn` | `v_roomMoveColumnMasked`: R0 set to $26 for this horizontal castle move |
| $C06A | `v_shopEntranceDoorNametablePointer` | reused by the name-table changer as `v_patchListCursor` (next patch at $D8A1+) |
| $C07F | `_RAM_C07F_` | `v_changerCounter`: punches counted by changers of kind 1; also the sub-area selector for level 17 (door room / puzzle room), set by entity $4C |
| $C091 | `v_newEntityHorizontalOffset` | fine horizontal scroll at the time of a drop, subtracted from new entities' x |
| $C0FD | `v_shopDoorNametablePointer` | mirror address of the shop door / hole / octopus pot |
| $C206 | `nametableChangeSourceMetatile` | `v_nametableChangeSource`: 8 bytes in bank 5 |
| $C218 | `_RAM_C218_` | petrification step (0-$2F) |
| $C219 | `_RAM_C219_` | word: (step & 7) × 4, added to the step's VDP destinations |
| $C21B | `_RAM_C21B_` | word: petrification source pointer (bank 4, starts at $AAFE) |
| $C101 | (after `v_waterColorTimer`) | water sparkle colour index |
| $C05E | (after `v_invincibilityColorTimer`) | invincibility flash colour index |
| $C225 | `v_shouldUpdateLevelTiles` | set elsewhere, cleared here, no effect (quirk) |
| $D8A0 | `v_unknownEntityByteCount_RAM_D8A0_` | `v_patchList`: count, then (mirror address word, metatile id) × count, from a $88 record |

Entity fields used here:
* name-table changer: +$16 `unknown5` = patches done; +$18 `unknown6` = change queued.
* debris: +$18 = kind (1 = star box).
* Alex: `unknown8` bit 3 = the punch connected, bits 0|3 = action in progress;
  `unknown3` bit 0 = facing right.

## Register effects reproduced

The shadow test compares every register the callers read afterwards. Besides the obvious
outputs:

* **`updateScroll`** leaves BC/DE as the path that ran leaves them: the old accumulator and
  the new scroll, or $0080, or the builders' values (BC = 2 × half − 4 or 4 × half − 4,
  DE = end of the buffer), or `(v_levelWidth)` / `(v_levelHeight)` read as words.
* **AF'** is live after `updateScroll`, `updateNametable` and `draw`, because the original
  uses `ex af,af'` as a scratch register:
  * updateScroll: A' = the new metatile half and F' = the flags of its `AND 1`;
  * updateNametable's column copy and draw's column copy: A' = the last row address high
    byte and F' = the flags of its `CP`;
  * updateNametable's row copy: A' = the length of the first part and F' = the flags of
    its LDIR (done with `op_ldir` on the Z80 registers);
  * draw's row copy: AF' = what `copyBytesToVRAM` returns.
* **`scrollFlagsUpdater $6476`** runs its record loop on the alternate registers (`exx`), so
  BC', DE' and HL' keep the loop's final values.
* **`requestBlockSound`** leaves A' = $8C.
* **`_LABEL_6F8F_`**: C = the number of records left at the last slot search.

## Quirks and original bugs (kept)

1. **Left edge** (`scroll_left`, level 13's end): when the move is refused, h is left at
   $FF and the column cursor is not restored; only the scroll pixel is set to 8.
2. **Drop updaters clear $C0B8** ($6532/$6574 write the word at $C0B7). This zeroes the high
   byte of `v_rowVdpAddress` ($78): a later vertical row draw would write to VRAM $0000+
   (tile patterns). This is latent: the lower rows only scroll horizontally, and the
   sub-area loader writes $7800 again.
3. **`v_shouldUpdateLevelTiles`** is cleared but has no effect: both paths of
   `updateLevelTiles` decrement the timer once.
4. **Screen RLE**:
   * no length check: a stream that decodes to more than 192 bytes overwrites $D7C0+;
   * a literal token $80 copies 65536 bytes (LDIR with BC = 0).
   The shop name-table RLE copies 256 bytes for $80.
5. **Request $80** writes the second row of the metatile at destination + $40 with no wrap
   at the bottom of the mirror. A metatile on the last row would write into
   `v_columnToDraw` and VRAM $3F00 (sprite table).
6. **Castle persistence is one frame late.** The entering room's first column (right/left
   moves) or first row (up/down) is built in the frame the room is fetched. The flagged
   positions are erased the next frame. A remembered block in that first column/row would
   show half of it again. Latent: no castle record lists such a position.
7. **Right scroll without `SCROLL_RIGHT`**: the speed is zeroed only at the next 8-px
   boundary, and nothing is built. If a handler keeps setting a negative speed, the camera
   keeps moving over unbuilt columns. Only reached with pokes (`level9_end_crash`: a crash
   at the level end with an odd column).
8. **Castle move above the top row**: the first row step has already moved R9 and
   `v_topRowVdpAddress` when the move is refused at v = 0. Only reached with pokes
   (`castle_top`).
9. **Request $84** puts the enterable pot graphics on the VDP and the closed collision words
   in the mirror. This is intentional: the two differ only in collision flags.

## Notes for the level editor

* **Data read by these routines and where** (see also `docs/level-format.md` section 11):
  * descriptor: `read_level_descriptor`;
  * layouts/screens: `level_fetch_screen_from_rows/columns` + `level_decode_screen`, with
    the bank taken from the descriptor;
  * metatiles: `metatile_entry` (bank 5 hard-coded);
  * entity streams: `entities.c` (bank 2 assumed; `updateNametable` maps it every frame).
* **Limits.**
  * Screen indexes are doubled in 8 bits: at most 128 screens per row and per column.
  * `v_levelWidth`/`v_levelHeight` are bytes.
  * The column/row cursors assume 16 × 12 metatile screens (32 × 24 cells) and a 32-column
    name table.
* **Bigger levels** only need the two fetch functions to address larger tables (and the
  descriptor to carry them). Custom metatiles need `metatile_entry` (and request $80, which
  reads its 8 bytes with bank 5 mapped) to know another bank.
* **Two-way scrolling** needs more than one decoded screen (see "One decoded screen").

## Unreachable code

None. All 137 routines are reachable, and all 2081 of their instructions ran in the
scenarios above. Two paths could only be reached with pokes that create situations the
original levels do not produce: quirks 7 and 8 above. The other poked scenarios (sub-areas,
the octopus pot, petrification, the doorway, name-table changers, crashes on far screens)
only shortcut normal play.

## Open issues

* Lockstep `--mode play --seed 11 --level 5 --lives --frames 20000` stops at frame 10075:
  "the original deadlocks here (interrupts disabled by DI at 033D)", in the map state. Both
  machines are identical up to that frame, and the same stop happens with the unmodified
  port (`build/lockstep`). This is the original game's behaviour with that input, not this
  module's.

## Sanity check of the test

Two deliberate bugs were injected and later reverted:
1. `step_half` no longer updated AF';
2. the column builder took the bottom words 2 bytes too early.

The idle scenario then reported:
* `updateScroll` 614 mismatches ("register A' generated 01 lifted 00");
* `loadLinesToNametable` 614/614, `_LABEL_6841_` 20/20 and `loadLevel` 2/2 mismatches
  ("RAM[CF2A] ...").

After the revert: 0 mismatches.
