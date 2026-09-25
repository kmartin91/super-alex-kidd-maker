# Alex Kidd in Miracle World (SMS, USA/Europe rev 0): level data format

This document specifies everything the engine reads to build and run a level, as needed by
a level editor. Every address was checked against the ROM (`original.sms`, CRC32 17A40E29)
and the disassembly in `reference/akmw` (whose `_LABEL_xxxx_` names are rev 1 addresses:
trust `build/rev0.sym` and the addresses given here). `maker/tools/levels.py` implements all of
it (decode, encode, verify, render); section 13 describes its model.

Contents

1. Conventions
2. Overview: what the engine reads for a level
3. Level descriptor
4. Layout: rows/columns tables, how the engine walks screens, level kinds
5. Screen format (RLE)
6. Metatiles, name-table words, game flags and collision
7. Entities per screen
8. Graphics: tiles, palettes, animations
9. Other per-level tables
10. Special levels and level-number checks in code
11. Bank handling: every routine that reads level data
12. Free space and what an editor can change safely
13. `maker/tools/levels.py`
14. Open questions

---------------------------------------------------------------------------------------------

## 1. Conventions

* The ROM is 128 KB = 8 banks of 16 KB. With the Sega mapper, banks 0 and 1 stay at
  CPU $0000-$7FFF. Slot 2 (CPU $8000-$BFFF) holds the bank last written to $FFFF. The game
  always writes `$80 | bank`, e.g. `$82` for bank 2 or `$86` for bank 6.
* **ROM file offset** of a CPU address:
  * addr < $8000: the offset is the address itself.
  * slot 2: offset = bank × $4000 + (addr − $8000). For example CPU $8022 in bank 6 is ROM $18022.
* Bank 2 is the code bank normally present in slot 2 during gameplay. Tables in bank 2 have
  ROM offset = CPU address.
* `v_level` (RAM $C023) runs from 1 to 17. Per-level tables are indexed as `table[v_level-1]`.
  In code this appears as `ld hl,table-2 ; rst $10` (loadAthPointer: HL = word at HL+2·A) for
  word tables, and `ld hl,table-1 ; add hl,bc` for byte tables.
* Screen = 16 × 12 metatiles = 256 × 192 pixels. Metatile = 2 × 2 tiles = 16 × 16 pixels.
* Name table: VRAM $3800 (the code writes the VDP address $7800), 32 × 28 words. A RAM mirror of
  it lives at $C800-$CEFF, with row r and column c at $C800 + r·$40 + c·2. Collision reads that
  mirror (section 6).
* Entity slots are $20 bytes each at $C300 + n·$20, for n = 0..29 (0-based; slot 0 is Alex).
  WLA's `v_entities.N` is 1-based, so `v_entities.N` = slot N−1.

## 2. Overview: what the engine reads for a level

`initGameplayState` ($0ABD) runs once per level start (state 3 → $0A). Stages below run in
order; the bank mapped in slot 2 is given in brackets.

1. Palette: `loadLevelPalette` $10FF [7] copies `levelPalettesPointers[$1112]` (32 bytes) to CRAM.
2. Sprite tiles: `loadLevelSpriteTiles` $1134 [7] calls `spriteTilesLoadersPointers[$1142]`.
3. Fixed sprite tiles [7]: rice ball, money bags, and the bullets when `levelSpawnStates[$0E1F]` ≠ 0.
4. Text characters [5] and item boxes [3].
5. Background tiles: `loadLevelTiles` $0E6C [3] loads the gold bag/cloud and calls
   `tilesetLoadersPointers[$0E7D]`, which uses `levelMainTilesetPointers[$C480]`.
6. `loadLevel` $65AA reads the descriptor through `LevelDescriptorPointerTable[$66CF]`, then maps
   the descriptor's bank [6] and draws the first screen through the layout, the screen RLE and
   the metatiles [5].
7. `shopDoorsConfigs[$0D70]`.
8. Entity descriptors: `v_entityDescriptorsPointer = entitiesDescriptorsPointers[$B505]` [2].
9. For levels 11 and 16 only: the castle breakable-block lists ($97DD / $9800) are copied to $D900 [2].
10. Alex's position: `startingPositions[$0DA3]`.
11. `paletteUpdatersPointers[$0D2C]`, `levelTileUpdatersPointers[$156D]`,
    `scrollFlagsUpdatersPointers[$0D0A]`, `entityLoadersPointers[$0D4E]`,
    `levelQuestionMarkBoxIndexes[$0E30]`.
12. Fixed sprite tiles [7]/[5]: ghost, 1up, power bracelet.
13. Music: `levelSongs[$0DC5]` [2].

Every frame during gameplay, the engine runs:

* scroll: `updateScroll` $67BD, which fetches new screens through the layout [descriptor bank]
  and builds rows/columns from metatiles [5];
* `updateNametable` $6B42 [2 is mapped here], which copies them to the $C800 mirror;
* `draw` $6919, which writes them to VRAM in VBlank;
* `loadNewEntities` $6F3D → the per-level entity loader [2, assumed];
* `updateScrollFlags` $6457 → the per-level scroll-flag updater;
* `updateLevelTiles` $158F [5] → the per-level tile animator;
* `updatePalette` $107C → the per-level palette updater.

Table summary (all indexed by v_level−1):

| Table | Address (bank) | Entry | Read by |
|---|---|---|---|
| LevelDescriptorPointerTable | $66CF (1) | dw → 12-byte descriptor in bank 1 | loadLevel $65AA |
| entitiesDescriptorsPointers | $B505 (2) | dw → per-screen table (bank 2) | initGameplayState $0B80 |
| levelMainTilesetPointers | $8480 (3) = ROM $C480 | dw → RLE tiles (bank 3) | tileset loaders |
| tilesetLoadersPointers | $0E7D (0) | dw → code | loadLevelTiles $0E6C |
| spriteTilesLoadersPointers | $1142 (0) | dw → code | loadLevelSpriteTiles $1134 |
| levelPalettesPointers | $1112 (0) | dw → 32 bytes in bank 7 | loadLevelPalette $10FF |
| startingPositions | $0DA3 (0) | db x, db y | initGameplayStateSecondary $0C43 |
| levelSongs | $0DC5 (0) | db sound id (18 entries) | init, shop, map, life-lost, bonus |
| levelSpawnStates | $0E1F (0) | db | initGameplayState |
| levelQuestionMarkBoxIndexes | $0E30 (0) | db | initGameplayStateSecondary |
| shopDoorsConfigs | $0D70 (0) | db x, dw mirror address | initGameplayState |
| scrollFlagsUpdatersPointers | $0D0A (0) | dw → code | init; updateScrollFlags $6457 |
| paletteUpdatersPointers | $0D2C (0) | dw → code | init; updatePalette $107C |
| entityLoadersPointers | $0D4E (0) | dw → code | init; loadNewEntities $6F3D |
| levelTileUpdatersPointers | $156D (0) | dw → code | init; updateLevelTiles $158F |

## 3. Level descriptor

`LevelDescriptorPointerTable` at CPU/ROM **$66CF** (bank 1, fixed) holds 17 words. Each points to
a 12-byte descriptor, also in bank 1 ($66F1, $66FD, … $67B1, stored consecutively).
`loadLevel` ($65AA) reads it with `ld hl,$66CD ; rst $10` and copies the fields to RAM:

| Off | Size | Field | RAM | Meaning |
|---|---|---|---|---|
| 0 | 1 | bank | $C081 v_levelBankNumber | Value written to $FFFF (always $86) before reading layouts and screens. It is re-written before every screen fetch, so layout and screen data may be in any bank. |
| 1 | 2 | rowsPtr ("layout pointer") | $C0A3 | Slot-2 address of the **rows table** (indexed by vertical screen). |
| 3 | 2 | colsPtr ("second layout pointer") | $C0A8 | Slot-2 address of the **columns table** (indexed by horizontal screen). Equal to rowsPtr for levels that never scroll vertically. |
| 5 | 1 | startScreenX | $C0B6 | Horizontal screen number before the first draw. The first screen shown is **startScreenX−1** (see 4.2). |
| 6 | 1 | startScreenY | $C0C4 | Vertical screen number (row) of the first screen. It is also the initial `v_currentScreenNumber`. |
| 7 | 1 | width | $C0A0 | Last horizontal screen index reachable by scrolling right. Scrolling stops when the horizontal screen reaches it, so a row has width+1 screens. Castles: number of rooms per row. |
| 8 | 1 | height | $C0A5 | Last vertical screen index (vertical levels). Castles: initial entity index (= startY·width + startX−1). |
| 9 | 1 | scrollFlags | $C080 → $C0C9 | Bit 0 down, bit 1 up, bit 2 left, bit 3 right (the directions currently allowed). Bit 5 = drop to row 1 screen 0. Bit 6 = drop to row 1 screen x/4. Bit 7 = vertical/auto (see below). Bit 4 is unused. |
| 10 | 2 | metatileTablePtr | $C087 | Bank-5 address of the 256-entry metatile pointer table: $8000 (table A) or $8200 (table B). The bank is **not** stored: bank 5 is hard-coded. |

What bit 7 of scrollFlags does depends on the level:

* **Levels 1 and 17** (hard-coded, see below): marks the vertical part.
* **Other levels**: `loadLevel` sets `v_shouldAlexStartWalkingtoNextScreen` = 1, so Alex walks in from the first screen (the castle entrance). It also sets `v_entityIndex` = height.

`loadLevel` special cases, hard-coded on v_level:

* **v_level = 1 or $11 (17)**: set verticalScreen = 0 and v_currentScreenNumber = $81. Load
  the first row of the screen below (`_LABEL_6A73_`), then set VDP reg 0 = $06.
* **v_level = $0D (13)**: v_currentScreenNumber = 7 (the level scrolls left from screen 7).
* **All other levels**: v_scrollFlags = descriptor flags. If bit 7 is set, apply the auto-walk
  described above.

Also before the first draw: `v_levelData_C0B7_ = v_levelData_C0C5_ = $7800`, the name-table
write address. The first screen is drawn by calling updateScroll/updateNametable/draw 256
times with a horizontal speed of $0100, i.e. by "scrolling left" one full screen.

Descriptor values:

| Lv | Name (used here) | Desc | Bank | Rows | Cols | sX | sY | W | H | Flags | MT |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Mt. Eternal (vertical descent) | $66F1 | $86 | $8000 | $8004 | 1 | 1 | 2 | 9 | $81 | A |
| 2 | Mt. Eternal part 2 | $66FD | $86 | $85C0 | $85C0 | 1 | 0 | 17 | 0 | $08 | A |
| 3 | Lake Fathom (underwater) | $6709 | $86 | $871F | $8723 | 1 | 0 | 5 | 1 | $28 | A |
| 4 | Island of St. Nurari | $6715 | $86 | $8AD6 | $8AD6 | 1 | 0 | 4 | 0 | $08 | A |
| 5 | Lake Fathom part 2 (Peticopter) | $6721 | $86 | $8DCE | $8DD2 | 1 | 0 | 15 | 1 | $48 | A |
| 6 | Village of Namui | $672D | $86 | $904A | $904A | 1 | 0 | 8 | 0 | $08 | A |
| 7 | Mt. Kave | $6739 | $86 | $937B | $937B | 1 | 0 | 8 | 0 | $08 | A |
| 8 | The Blakwoods | $6745 | $86 | $97EE | $97EE | 1 | 0 | 11 | 0 | $08 | A |
| 9 | River (boat) | $6751 | $86 | $9D66 | $9D6A | 1 | 0 | 15 | 1 | $48 | A |
| 10 | Bingoo Lowland | $675D | $86 | $A06A | $A06A | 1 | 0 | 14 | 0 | $08 | A |
| 11 | Radactian Castle | $6769 | $86 | $A2C6 | $A2CE | 1 | 2 | 5 | 10 | $80 | A |
| 12 | City of Radactian | $6775 | $86 | $A9C6 | $A9C6 | 1 | 0 | 2 | 0 | $08 | A |
| 13 | Swamp (Peticopter, scrolls left) | $6781 | $86 | $AAB4 | $AAB4 | 8 | 0 | 255 | 0 | $04 | A |
| 14 | Kingdom of Nibana part 1 | $678D | $86 | $AB52 | $AB52 | 1 | 0 | 1 | 0 | $08 | B |
| 15 | Kingdom of Nibana part 2 | $6799 | $86 | $AC6C | $AC6C | 1 | 0 | 10 | 0 | $08 | A |
| 16 | Janken's Castle | $67A5 | $86 | $AF35 | $AF41 | 1 | 1 | 7 | 7 | $80 | B |
| 17 | Crag Lake (vertical) | $67B1 | $86 | $BC53 | $BC53 | 1 | 0 | 1 | 1 | $01 | B |

About the names: the reference disassembly's names are kept, except where the screenshots
disagree. The pieces of evidence were:

* Level 13's descriptor is labelled "swamp", but its palette pointer is labelled
  "MtEthernalStage2".
* The table labelled "craggLakeMetatileDeletes" is really used by level 16.
* Level 17 contains the Janken-floor crown puzzle.

Always refer to levels by number.

## 4. Layout

### 4.1 Tables

All layout data of the 17 levels is in bank 6: ROM $18000-$1BDB8. For each level it is stored as
follows (verified for all levels):

```
rowsTable:  dw row_0, row_1, ...          ; indexed by vertical screen number
colsTable:  dw col_0, col_1, ...          ; indexed by horizontal screen number (absent if == rowsTable)
row_k:      dw screen, screen, ...        ; indexed by horizontal screen number
col_k:      dw screen, screen, ...        ; indexed by vertical screen number
screens:    RLE streams (section 5), each used by one or more table entries
```

There is no length field anywhere. Tables are as long as the engine will ever index them. In
the ROM each table ends where the next structure starts, and `maker/tools/levels.py` sizes them that
way. Screens may be referenced many times: level 2 has 18 positions but only 5 distinct
screens, and level 5 repeats 4 sky screens.

The two tables must agree: `row[v][h]` and `col[h][v]` must be the same screen wherever
both can be used. The castles' tables are exact transposes of each other.

### 4.2 How the engine fetches screens

State lives in RAM:

* $C0B6 `v_horizontalScreenNumber` (h): the screen at the left of the camera.
* $C0C4 `v_verticalScreenNumber` (v): the screen at the top of the camera.
* $C08D `v_currentScreenNumber` (bit 7 = "new screen, load its entities").
* Column cursor: $C0B3 = left/right half of the metatile; $C0B4/5 = metatile column ×$80;
  $C0B7 = name-table column address.
* Row cursor: $C0C1 = top/bottom half; $C0C2/3 = metatile row ×$80.
* $D700 `v_decompressedLevelLayoutData`: the 192-byte current screen.

**Horizontal scrolling** (`updateScroll` $67BD, speed $C0AB; a negative speed moves right, in `scrollRight` $68A0):

* A new 8-pixel column is built every 8 pixels. `_LABEL_6841_` ($683A) fetches and decompresses a
  screen when the column cursor wraps:
  ```
  bank <- v_levelBankNumber
  screen = word[ word[rowsPtr + 2*v] + 2*h' ]   ; h' = h+1 when moving right, h-1 when moving left
  RLE-decode screen -> $D700
  ```
* Then `loadLinesToNametable` $685E [bank 5] builds one column of 12 metatiles × 2 words into
  $CF00 (v_columnToDraw).
* Moving right, h is incremented when a full screen has scrolled. When the new h equals `width`,
  bit 3 of v_scrollFlags is cleared and scrolling stops.
* Moving left stops when h would go below 0.
* Each time a new screen starts to enter, v_currentScreenNumber is incremented (right) or
  decremented (left), with bit 7 set.

**Vertical scrolling** (`updateVerticalScroll` $69C4, speed $C0B9):

* `_LABEL_6B21_` ($6B1A) fetches `word[ word[colsPtr + 2*h] + 2*(v+1) ]` when moving down, or `+2*v` when moving up.
* `_LABEL_6A76_` $6A6F [bank 5] builds a row of 16 metatiles × 2 words into $CF38 (v_rowToDraw).
* Moving down:
  * v is incremented each time a full screen (192 px) has scrolled.
  * If v < height, the next screen is fetched.
  * When v reaches **height**, v is reset to **0**, bit 0 of v_scrollFlags is cleared and the scroll stops.
  * So after a vertical section ends, horizontal scrolling continues on **row 0** of the rows table.
* Moving up stops below v = 0.

**First screen.**

* `loadLevel` "scrolls left" 256 pixels starting from (startScreenX, startScreenY), so the first
  screen shown is `rows[startScreenY][startScreenX−1]`.
* Its entities are never loaded: v_currentScreenNumber is set to startScreenY without bit 7.
  In practice entity index 0 is always empty.

**Screen drops** (`_LABEL_6671_` $666A). This is called by Alex's handlers when he goes down
through an ENTER-DOWN tile (section 6) or falls into a hole. It destroys the off-screen
entities and then acts on v_scrollFlags:

* **bit 7**: v_currentScreenNumber += 1 (with bit 7 set).
* **bit 6**: h = h/4 (`rrca rrca and $3F`); v_currentScreenNumber = $90 + h.
* **bit 5**: h = 0; v_currentScreenNumber = $86.

It then loads the first row of `col[h][v+1]` and restores bank 2. The caller sets v_scrollFlags
bit 0 and a vertical speed of 3 px/frame ($3223).

When the drop ends, the per-level scroll-flags updater takes over:

* **$6574** (level 3): v = 1, width = 1, flags = right.
* **$6532** (levels 5 and 9): v = 1. If h < 3, width = 3 and flags = right; otherwise no scrolling.

### 4.3 Level kinds and the physical map

These are derived by `maker/tools/levels.py` (`derive_map`). "Entity index" is the index into the
level's per-screen entity table (section 7), i.e. `v_currentScreenNumber & $7F` or, in
castles, `v_entityIndex`.

| Kind | Levels | Reachable screens | Entity index |
|---|---|---|---|
| horizontal | 2, 4, 6, 7, 8, 10, 12, 14, 15 | `rows[0][0..width]`, left to right | = h |
| reverse | 13 | `rows[0][0..startX−1]`; starts at screen 7 and scrolls **left** (flags $04) | = h |
| vertical | 1, 17 | `cols[startX−1][0..height]`, top to bottom. Level 1 (flags bit 7) then continues on `rows[0][1..width]` to the right; `scrollFlagsUpdater $645B` switches to "right" when the vertical part ends. | = v for the vertical part (the start is $81, so screen 1's entities are loaded immediately); then height + h |
| drop | 3, 5, 9 | Upper part: `rows[0][0..width]`. Lower part, after a drop: `rows[1][0..1]` (level 3, flags bit 5) or `rows[1][0..3]` (levels 5/9, bit 6; the lower screen under upper screen h is h/4) | upper = h; lower = 6+h (level 3), $10+h (levels 5, 9) |
| castle | 11, 16 | 2-D grid `rows[r][c]` (4×5 and 6×7). Doors, ladders and holes scroll exactly one screen in a direction, set by Alex's handlers; `scrollFlagsUpdater $6476` stops the scroll when aligned. | `v_entityIndex` = r·width + c. Starts at `height`; ±1 for left/right, ±width for up/down (loader $707D) |

Screens outside a level's own range:

* Level 4's row holds 8 screens, but width = 4. Screens 5-7 belong to the **level 3 sub-area**
  (4.4).
* Level 17's row holds 4 screens. Screens 2-3 are its sub-area.

Rendering all 17 maps with `maker/tools/levels.py render` shows exactly the expected level graphics
(section 13).

### 4.4 Sub-areas ("bonus level" state 8)

Entity **$4C** (sub-area trigger, placed with a $81 record) sets `_RAM_C07F_` = its data byte
and switches to STATE_BONUS_LEVEL. The loader `_LABEL_1735_` ($1735) then does the following,
without reading any descriptor:

* **v_level ≠ 17**:
  * sets v_currentLevelIsBonusLevel;
  * temporarily sets v_level+1 and loads that level's background tiles (`loadLevelTiles`),
    palette and sprite tiles;
  * uses the rows/cols pointer **$8AD6** (hard-coded; level 4's table) with h = 6 and width = 7.
    Screens 5, 6 and 7 of that row are shown, with entity indexes 8, 9, 10 (v_currentScreenNumber = $88).
  * Only level 3 places a $4C trigger, so this is Lake Fathom's land area. It uses level 4's
    graphics and level 3's entity table indexes 8-10.
* **v_level = 17**:
  * if `$D800` ≠ 0 (crown collected), jump to the ending ($189A);
  * otherwise use rows/cols **$BC53** (level 17's own) with h = 3 (or 4 when `_RAM_C07F_` ≠ 0) and
    width 0. The screen shown is 2 (door room) or 3 (symbol-floor puzzle room). The room's
    entities are spawned by code at $1822 (index 8 is empty).
* In both cases v_levelBankNumber is **not** reloaded: the layout is read from whatever bank the
  current level used (bank 6).

## 5. Screen format (RLE)

A screen is 192 bytes (16 columns × 12 rows, row-major; byte = metatile id), compressed as
a byte stream decoded by `_LABEL_6BEF_` (**$6BE8**) into $D700:

```
n = 0          end of screen
n = $01-$7F    run: the next byte repeated n times        (2 bytes)
n = $81-$FF    literal: the next (n & $7F) bytes copied   (1 + n&$7F bytes)
```

* Runs may cross rows.
* The decoder does not check the length. A screen that decodes to more than 192 bytes
  overwrites the RAM after $D7C0 (the $D800 flags), so the encoder must emit exactly 192.
* The decoder reads the stream with slot 2 = v_levelBankNumber.

Original encoder choices (166 of 174 screens reproduced exactly by `rle_encode`):

* a run of ≥ 3 is always a run token;
* a run of exactly 2 is a run token if no literal is pending, or if it is followed by
  another run ≥ 2 or by the end of the screen; otherwise it is absorbed into the literal.

The other 8 screens show hand-made quirks: runs split at a row boundary, a 2-run between two
literals, and the last byte emitted as a separate 1-byte literal. They are:

* level 3 $8A46
* level 5 $8ED3
* level 9 $9DBF, $9EA2, $A006
* level 11 $A51F
* level 16 $B8B7
* level 17 $BC5D

`levels.py` records their token list (`rle_tokens`) so that they re-encode byte-identically.
The canonical encoder is 1-5 bytes smaller on each of them.

## 6. Metatiles, name-table words, game flags and collision

### 6.1 Tables and entries

* Two tables sit in bank 5: **A** at CPU $8000 (ROM $14000) and **B** at CPU $8200 (ROM $14200).
  Each holds 256 words (little-endian), one pointer per metatile id, to an 8-byte entry in bank 5
  (ROM $14450-$14D34).
* The level descriptor chooses the table: levels 14, 16 and 17 use B, all others use A.
* An entry is 4 SMS name-table words, in the order **TL, TR, BL, BR**. The column builder takes
  words 0 and 2, or 1 and 3; the row builder takes words 0-1 or 2-3.
* The two tables share every pointer except ids $50-$69 and $7A-$95.
* Ids **$0D-$0F, $12-$13 and $1B-$1F** (plus $59-$67 in table A) point to 1-byte stubs, so their
  8 bytes overlap the next entry. They are never used by a level. An editor must not edit
  them "in place" (`overlaps_next_entry` in the model).

Word format (little-endian: the low byte is the tile index):

| Bits (of the 16-bit word) | Meaning |
|---|---|
| 0-8 | tile index (bit 8 is never set in these tables; every background tile used is 0-170) |
| 9 | horizontal flip |
| 10 | vertical flip |
| 11 | palette select (1 = sprite palette, CRAM 16-31) |
| 12 | priority (drawn in front of sprites) |
| **13-15** | **game flags** (attribute byte bits 5-7), ignored by the VDP |

Fixed entries referenced by address in code:

* `backgroundMetatile` = $8503 = id $20: tiles $35-$38, flags 0. It replaces broken blocks.
* `waterMetatile` = $850B = id $21: tiles $78-$7B, flags $20. It replaces blocks broken while swimming.
* $8400 and $8410: the octopus pot.
* $8420: the doorway drawn by entity $60.
* $8B5D and $8B75: table-B ids $50/$52, used by battle and captive code.

### 6.2 Collision is done on the name-table mirror

Screens are expanded into the RAM mirror at $C800, and the VDP gets a copy. All terrain tests
read the **attribute byte** (high byte) of one 8×8 cell of that mirror, so flags are per
quarter of a metatile. The lookup is `getNearEntityTileAttrWithOffset` **$7C44**, with variants
$7C48, $7C73, $7C82, $7C8D and $7C9C. For entity point (X, Y) = (xPos.high + e, yPos.high + d):

```
col = (X - h_fine) >> 3          ; h_fine = high byte of v_horizontalScroll ($C0B0)
Y'  = Y + v_verticalScroll.high  ; +$20 on carry, +$20 again if >= $E0 (28-row wrap)
addr = $C800 + (Y' & $F8)*8 + col*2 + 1       ; -> attribute byte (tile index at addr-1)
```

Game-flag meanings (bits 7-5 of the attribute byte). "Tile" is the tile index low byte, which
some rules use; these tile numbers are fixed by the code.

| flags | Alex (`interactWithTileAtOffset` $3C41, `interactWithFloorWithOffset` $3D03, state handlers) | Other entities |
|---|---|---|
| $00 | nothing | air |
| $20 | **water**: falling in → swimming; the boat floats; diving splashes; the Peticopter crashes | air |
| $40 | tile < $90: **money bag tile** (touch → small-money-bag amount, BCD `01 00 00`, coin sound, replaced by background); tile ≥ $90: shop item selector (index table $3C9C) | air |
| $60 | tile $3F: **ladder**. Tile ≥ $70: **shop door** (Up while idle, x ≥ $18). Any other tile: **deadly** (spikes, lava, flames). Tile $59 above the water surface kills a swimming Alex. | air |
| $80 | **solid**. Alex dies if the body probe (y+$0C, x+8) is inside one. | solid |
| $A0 | solid, and **floor trigger** under Alex's feet (y+$18, x+8): tile < $0D inert; $0D-$24 puzzle floor codes (level 17 only); **$1D-$20 ghost floor in every level** (spawns a ghost every 128 frames); $3F ladder top (Down); > $3F **ENTER-DOWN** (Down → walk to the level's door x, then drop: section 4.2). $25-$3E overrun the jump table and must not be used. | solid |
| $C0 | solid, **breakable rock** (punch, bracelet shockwave, bullets, motorcycle) | solid |
| $E0 | solid, **breakable box**. The type comes from the TL tile: 1-4 skull (Alex freezes), 5-8 question (spawns the next `questionMarkBoxItems` entry), 9-12 star (money bag). Other tiles crash. | solid |

Other entities (enemies, items) test bit 7 only: solid or not.

Breaking works as follows:

* The punch probe is at (y+$0C, x−7 or x+$17).
* `_LABEL_4578_` ($4571) aligns to the metatile, spawns debris, and runs the box handler if bit 5
  of the TL attribute is set.
* It then requests name-table change $80 with backgroundMetatile (or waterMetatile when swimming).
* Debris graphics type comes from the TL tile: < $0D → 1, < $7C → 2, else 3.

Money: a $40 metatile (id $0A, tiles $25/$26 = the gold-bag graphic) gives the small-bag amount
when touched and is replaced the same way (request $80, destination = the touched metatile).

Shop door, octopus pot and ENTER-DOWN hole:

* These do **not** come from the map position.
* `shopDoorsConfigs` (section 9) gives the x Alex walks to and the mirror address used for the
  priority/redraw effects.
* So a level can have only one door or hole of this kind.
* The octopus record ($82) overrides the pointer.

### 6.3 Name-table change requests (`v_nametableChangeRequest` $C202)

These are processed in VBlank by `handleNametableChangeRequest` $4222 through the table at $4230:

| Request | What it does |
|---|---|
| $80 | Writes a metatile (8 bytes from bank 5, **hard-coded $85**) at v_nametableChangeDestination. In castles it also marks the persistent-deletes record (6.4). |
| $81 / $82 | Shop door / pot priority redraw at v_shopDoorNametablePointer |
| $83 / $84 | Octopus pot drawn from $14400 / $14410 |
| $85 | Block copy from bank 7 (spiked pillars, entities $10-$14) |
| $86 | Deadly fill (entity $15) |
| $87 | Clear a block (collapsing floors, $16/$17) |
| $88 | Janken petrification (bank 4) |
| $89 | Doorway $14420 (entity $60) |

### 6.4 Castle breakable-block tables (levels 11 and 16)

Levels 11 and 16 revisit rooms, so the collectables and boxes that were taken must stay taken.

At init (`initGameplayState` $0B9D, hard-coded `cp $0B` / `cp $10`), the table at **$97DD** (level 11,
5 rooms per row) or **$9800** (level 16, 7 rooms per row) in bank 2 is expanded into RAM
$D900 + row·$100 + col·$20. It also sets `_RAM_C08E_` = 1.

ROM format, row by row (room by room within each row):

```
per room: n, pos_1 ... pos_n     ; n = 0 for none; pos = metatile index 0..191 in the room
after each full row of rooms: $FF ends the table, anything else starts the next row
```

The RAM record is `n, flag1, pos1, flag2, pos2, ...` with every flag cleared.

At run time:

* Request $80 sets the flag of a listed position when it is broken.
* On every room change, `scrollFlagsUpdater $6476` sets `$D700[pos] = 0` (metatile $00) for every
  flagged entry before the room is drawn.
* Positions not listed are not remembered.

An editor that moves money/boxes in these castles must regenerate this table. The rows are
the castle grid rows, and the tables contain 4 rows (level 11) and 6 rows (level 16).

## 7. Entities per screen

### 7.1 Tables

* `entitiesDescriptorsPointers` at **$B505** (bank 2): 17 words.
* Each word points to a per-level table of words, one per **entity index** (4.3), at $B527-$B6EC.
  Each of those words points to a descriptor stream at $B6ED-$BE87.
* There is no count: a table is as long as the largest reachable index. Entry counts per level:
  12, 18, 11, 5, 20, 9, 9, 12, 20, 15, 20, 3, 8, 2, 11, 42, 10.
* Streams are shared freely. For example, the castles point all empty rooms at one `00` byte,
  and level 5 cycles 4 streams.
* `initGameplayState` sets `v_entityDescriptorsPointer` ($C061) with `ld hl,$B503 ; call $0010`
  at $0B80, after `ld a,$82 / ld ($FFFF),a`.

### 7.2 Loading

`updateGameplayState` calls `loadNewEntities` ($6F3D) every frame. It jumps through
`v_entityLoaderPointer` (from `entityLoadersPointers[$0D4E]`):

* `loadEntitiesNormal` **$6F41** for all levels except 11 and 16.
* `loadEntitiesSpecial` **$707D** for castles.

The loaders do nothing unless bit 7 of v_currentScreenNumber is set. They clear it and read the
stream selected by:

* **normal**: index = `v_currentScreenNumber & $7F`;
* **castle**: index = `v_entityIndex` ($C065), updated first:
  * down: += width, with vertical offset −(fine scroll $C0BC);
  * up: −= width, with offset +$C0BC;
  * left: −1;
  * otherwise: +1.

Neither loader maps a bank: they rely on bank 2 being in slot 2, which `updateNametable` ($6B42)
guarantees every frame.

### 7.3 Stream grammar

```
stream  := special* ( $00 | N entity{N} )          N = 1..$7F; nothing follows the N entities
entity  := type y x data                            (4 bytes)
special := b [...] with bit 7 set, tested in this order:
   b & 1  ($81)  type y x data  -> slot 28 ($C680), or slot 29 ($C6A0) if slot 28 is in use
                                   (overwrites slot 29 unconditionally)
   b & 2  ($82)  set            -> octopus arms: table at $70FB (bank 1), entry set-1: dw pot address
                                   (-> v_shopDoorNametablePointer), then 8 x (delay, y, x, follower)
                                   written to slots 6-13 as type $24; x/y absolute
   b & 4  ($84)  type y x data  -> slot 5 ($C3A0), overwritten unconditionally ("always present":
                                   rice ball, bosses, items)
   else   ($88)  len bytes{len} -> copied to $D8A0 (len = 0 would copy 64 KB)
```

Loading details:

* Normal entities go into the first free slot among **6-15** (10 slots). If none is free, the
  rest of the screen's entities are dropped.
* For the $81 and $84 records, only flag bits 0 and 1 are cleared. The other fields of the slot
  are not reset.

Coordinates (code at $6F9F):

```
ix+0  = type
ix+14 = y + v_newEntityVerticalOffset           (yPos.high; 0 except in castle vertical moves)
ix+11 = low byte of v_horizontalScroll          (xPos.low)
ix+12 = x + high byte of v_horizontalScroll - v_newEntityHorizontalOffset   (xPos.high)
ix+3  = data
ix+9 / ix+10 = off-screen page: $FF/00 (entering from the right), $01/00 (left), 00/$01 (below), 00/$FF (above)
```

So **x, y are pixel coordinates inside the entering screen** (x 0-255, y 0-191). This was
cross-checked in the running game (level 1):

* Index 1 = `2D 78 68 00` → slot 6 holds type $2D, x=$68, y=$78, ix+10=1 right after the level starts.
* Index 2 (`2D 20 88 00`, `2D 98 40 00`) → slots 6/7 get x=$88/$40, y=$1C/$94 one frame after
  v_currentScreenNumber became $82 (the screen had already scrolled 4 px up).

The **$88 record** (only type of "other" special used) is a list of name-table patches:
`count, {dest_lo dest_hi metatile_id} × count`.

* dest is a mirror address $C800-$CEFF (row = (dest−$C800)/64, column = ((dest−$C800) mod 64)/2).
* metatile_id indexes the level's metatile table.
* The buffer is consumed only by entity **$4B** (name-table changer), which applies one patch per
  trigger (data 0 = on punch, 2 = on touch, 1 = punch counter only).
* Example: level 3 index 9 makes 7 star boxes appear.

### 7.4 Entity types

Updater table `entityTypeJumpTable` at **$2892** (99 words, index = type−1). "L" means the type
is placed by level descriptors (ids in use: 10 11 12 13 15 16 17 1C 1D 1E 1F 20 23 24 25 2A 2C
2D 2E 2F 30 31 33 3D 3E 40 42 44 45 46 4B 4C 50 51 52 53 54 55 57 63). Ids $59-$5F point to $0000
(reset) and must never be used.

| Type | Updater | Description | Data byte (placed types) |
|---|---|---|---|
| 01 | $2958 | Alex | |
| 02 | $4489 | Peticopter/boat bullet | |
| 03 | $443F | vehicle explosion | |
| 04 | $44CD | bullet impact | |
| 05-0A | $4689, $46C2, $4719, $4885, $4768, $4863 | magic capsule A/B effects | |
| 0B | $761F | Janken thought cloud | |
| 0C | $7982 | battle sprite helper (unused) | |
| 0D-0F | $799A, $7A89, $7B2E | Gooseka / Chokkinna / Parplin heads | |
| 10-13 L | $49EB, $4A26, $4A32, $4A3E | castle spiked pillar/crusher, 4 shapes (pattern in bank 7 $BE9E+); x,y = name-table cell of its top | initial delay (frames) |
| 14 | $497D | active pillar (from 10-13) | |
| 15 L | $4B1C | descending spiked ceiling band | width in tiles |
| 16 L | $4A4A | collapsing floor, triggered by Alex's x | hole half-width (tiles) |
| 17 L | $4AE7 | collapsing floor, triggered by a punch | hole half-width |
| 18 | $0966 | static sprite | |
| 19 | $74C7 | Janken projectile | |
| 1A | $789E | Chokkinna spell | |
| 1B | $4914 | power bracelet shockwave | |
| 1C L | $7143 | Janken the Great | opponent id (1) |
| 1D L | $778F | Gooseka | opponent id: 2 = first match (dies, rice ball), 3 = rematch (head fight follows) |
| 1E L | $780F | Chokkinna | 4 / 5 |
| 1F L | $78A1 | Parplin | 6 / 7 |
| 20 L | $4EE8 | bat (left), turns into 36 | – |
| 21 | $2439 | item-select arrow | |
| 22 | $4E96 | merman bubbles | |
| 23 L | $4E29 | merman | hits already taken (dies at 3) |
| 24 | $4C27 | octopus arm (only through $82 records) | |
| 25 L | $52E0 | Blakwoods bear boss (26-28, 4A are its states) | – |
| 29 | $55EC | monkey leaf | |
| 2A L | $5573 | monkey in a tree | – |
| 2B | $567D | smoke puff | |
| 2C L | $4FEA | plant | – |
| 2D L / 33 L | $5030 / $5081 | monster bird left / right | – |
| 2E L / 35 | $5158 / $51EC | killer fish left / right | – |
| 2F L / 37 | $56C5 / $571C | monster frog / jumping | – |
| 30 L / 34 | $50DA / $512B | small fish left / right | – |
| 31 L / 32 | $57C7 / $587C | sea horse left / right | (overwritten) |
| 36 | $4F7B | bat right | |
| 38-3B | $5901... | block debris | |
| 3C | $5A2A | money bag | |
| 3D L | $5D8D | circular flame (radius $20 around x,y) | – |
| 3E L / 3F | $5E0D / $5E74 | scorpion / walking flame, left / right | 0 = scorpion, else flame |
| 40 L / 41 | $5EB3 / $5EFE | storm cloud / lightning | – |
| 42 L | $5F45 | leaping fish (y = $BF) | – |
| 43 | $5622 | boss-defeat smoke | |
| 44 L | $5BCA | rice ball (level end, +1000) | – |
| 45 L | $60B5 | Saint Nurari | – |
| 46 L | $5C2F | Namui bull boss (47-49 are its states) | – |
| 4B L | $61C6 | name-table changer (uses the $88 patches) | 0 punch, 2 touch, 1 counter |
| 4C L | $6279 | sub-area trigger (4.4) | value stored in $C07F |
| 4D / 4E / 4F | $5A8F / $5ADF / $5B30 | 1up / power bracelet / ghost | |
| 50 L | $6077 | village elder | – |
| 51 L | $5FAA | captive | 0 = Princess Lora, else Egle |
| 52 L | $6106 | special item | item 0 crown, 1 telepathy ball, 2 letter, 3 Hirotta stone, 4 moonlight stone, 5 extra life, 6 bracelet, 7 teleport powder, 8 sun stone |
| 53 L | $616F | King of Nibana | – |
| 54 L | $62A8 | ground walker | – |
| 55 L | $6361 | hopping walker | – |
| 56 | $1B41 | map arrow | |
| 57 L | $63F4 | static deadly flame | – |
| 58 | $1B8E | Janken's castle on the map | |
| 60 / 61 | $3E28 / $3EBA | level 17 puzzle-room helpers | |
| 62 | $39DB | Alex eating the rice ball | |
| 63 L | $3EFC | invisible punch target (clears the hard-coded mirror cell $CC08) | – |

"–" means the data byte is ignored; the ROM stores 0 there, except for one $46 with 1.

Entity-side level logic:

* The octopus arm picks pot graphic $83/$84 from `v_level < 5` and screen < 3.
* The arrow reads `mapArrowPositions[v_level]`.
* Question boxes use `v_questionMarkBoxIndex` (section 9).

## 8. Graphics: tiles, palettes, animations

### 8.1 VDP layout

Initial register values (set at $027D):

* R0 = $26: mode 4, leftmost column masked. Levels 1 and 17 set R0 = $06 in `loadLevel`; the
  castles toggle it during room changes.
* R2 = $FF: name table at $3800.
* R5 = $FF: sprite attribute table (SAT) at $3F00.
* R6 = $FF: sprite tiles at $2000, i.e. sprite tile n = VRAM tile 256+n.
* R7 = $00: backdrop colour = CRAM $10.

Tile use:

* Background tiles are VRAM tiles 0-255. Levels only use tiles 0-170.
* Tiles 448-511 overlap the name table and SAT, so they cannot hold graphics.

Background tile map, identical in every level:

| Tiles | Content | Loaded by |
|---|---|---|
| 0 | blank | leftover from the level-start screen |
| 1-36 | item boxes: skull, ?, star, then pink star, waves, fish, moon, skull, sun (4 tiles each) | init, raw $480 bytes from bank 3 $8000 (ROM $C000) |
| 37-46 | gold bag + cloud | `loadLevelTiles` $0E6C, RLE bank 3 $84A2 |
| 53-170 | level tileset (per level, below) | tileset loader |
| 176-239 | 4bpp font | init, RLE bank 5 $B2B1 (ROM $172B1) |

Sprite tile map:

| Tiles | Content |
|---|---|
| 256-267 | Alex, uploaded when his frame changes (bank 4, 3bpp) |
| 269-271 | rice ball |
| 272-286 | bullets, only in levels 5, 9 and 13 |
| 288-301 | ghost |
| 302-305 | 1up |
| 306-309 | bracelet |
| 310-317 | money bags |
| 318-447 | per-level enemies (`spriteTilesLoadersPointers`) |

### 8.2 Background tileset loaders

`loadLevelTiles` jumps through `tilesetLoadersPointers` ($0E7D). Each loader is straight-line code
built from these steps:

* `fillVram` of 4 tiles (16 for levels 3 and 17);
* the level's **main set**, via `levelMainTilesetPointers` at bank 3 $8480 (ROM $C480), read with
  `ld a,(v_level)` or a **hard-coded index** (2, 3 or $0B) and `ld hl,$847E ; rst $10`;
* fixed "additional sets" given as immediates.

All sources are in bank 3, mapped by every caller:

* init $0B48
* bonus $175E
* map exit $2035
* shop exit $1C93

Result (t = tile number; later loads overwrite earlier ones):

| Lv | Loader | Loads in order |
|---|---|---|
| 1 | $0E9F | fill $00 t53-56; mainSet $8ECE → t57-80; aditionalSet4 (raw $A0 bytes) $B7F6 → t115-119; aditionalSet1 $8583 → t120-170 |
| 2 | $0FF9 | fill $00 t53-56; mainSet2 $9158 (fixed index 2) → t57-71; aditionalSet2 $89E1 → t118-170; aditionalSet3 $8E65 → t61-64 |
| 3 | $0F21 | fill $00 t53-68; mainSet3 $9317 → t69-100; aditionalSet5 $B896 → t104-111; aditionalSet4 → t115-119; aditionalSet1 → t120-170 |
| 4 | $0F54 | fill $00 t53-56; mainSet4 $956A → t57-115 |
| 5 | $0F99 | the whole level-1 loader, then aditionalSet5 → t104-111, aditionalSet3 → t61-64 |
| 6 | $0F6C | fill; mainSet2 (fixed index 2) → t57-71; mainSet5 $9B82 → t73-115; aditionalSet2 → t118-170 |
| 7 | $0FAE | fill **$0A** t53-56; level7Set $9F1D → t57-97 |
| 8 | $0EC9 | aditionalSet2 → t118-170; level8Set $A24C → t53-127 (no fill) |
| 9 | $0FC6 | fill; mainSet → t57-80; aditionalSet5 → t104-111; aditionalSet1 → t120-170; aditionalSet3 → t61-64 |
| 10 | $0F00 | fill; mainSet2 → t57-71; aditionalSet2 → t118-170 |
| 11 | $0F54 | fill; level11Set $AA05 → t57-108 |
| 12 | $0F6C | as level 6 |
| 13 | $0FF9 | as level 2 |
| 14 | $1058 | fill; level11Set (fixed index $0B) → t57-108; level14Set $B49B → t109-139 |
| 15 | $0F00 | as level 10 |
| 16 | $0EDF | fill; level16Set $AE33 → t57-89; aditionalSet1 → t120-170 |
| 17 | $1022 | fill t53-68; mainSet3 (fixed index 3) → t69-100; level17Set $B117 → t85-119; level17AditionalSet $B75C → t57-61; aditionalSet1 → t120-170 |

Sprite tiles:

* `loadLevelSpriteTiles` maps **$87** (immediate at $1135) and jumps through
  `spriteTilesLoadersPointers` ($1142).
* Each entry is a list of `call loadXTiles`. Every `loadXTiles` is `ld hl,src ; ld de,dst ; ld bc,len ;`
  followed by a raw copy, sometimes with a mirrored copy after it.
* All sources are in bank 7. The Peticopter and teleport-powder loaders switch to bank 5 and back.
* The per-level lists are in `src/spriteTileLoaders.asm`. `levels.py` reproduces them (the
  `tile_loads` field in the model).

`maker/tools/levels.py` rebuilds all of this by **interpreting the loader code of the ROM** (a
13-opcode Z80 subset plus native versions of the copy/fill/decompress helpers, in
`LoaderInterpreter`). Edited pointer tables or loaders are therefore followed automatically.
The result was compared with the VRAM of the running game at the start of every level. All
background tiles referenced by the level's metatiles and all sprite tiles match. The only
differences are animated tiles and Alex's frames.

### 8.3 Compression formats

**Tiles: "Phantasy Star RLE"** (`decompressTilesToVram` $0293, HL = source, DE = VDP address).

* The data is 4 bitplane streams, stored one after the other.
* Plane p writes bytes DE+p, DE+p+4, DE+p+8, … (one VDP address write per byte). Byte k of a
  plane is row k%8 of tile k/8.
* Each plane is `{h [data]}* 00`:
  * h = 0 ends the plane;
  * h bit 7 set: copy h&$7F literal bytes;
  * otherwise repeat the next byte h times.
* h&$7F = 0 would mean 256; that never occurs.
* The planes of a set have equal lengths, so tiles = plane length / 8.

**Other copy formats:**

* raw copy: `copyBytesToVRAM` $0145 (BC bytes) or `rst $30` (B bytes, 0 = 256);
* `fillVram` $0184 (BC bytes of L);
* `copyMirroredTilesToVramAtCurrentAddress` $02C5: BC bytes, bit-reversed (horizontal flip),
  written right after the previous write;
* Alex frames: 3bpp, 24 bytes per tile, plane 3 = 0;
* 1bpp: map and shop only.

**Screens:** section 5.

**Shop name table** (`decompressNametable` $0E41): a low-byte stream, then a high-byte stream,
each with stride 2 and the same RLE rules.

### 8.4 Palettes

* `levelPalettesPointers` ($1112, bank 0) holds 17 slot-2 addresses in **bank 7** ($87 immediate at
  $1100). Palettes are at ROM $1FC9E-$1FE9D.
* Each palette is 32 bytes copied to CRAM $00-$1F: 16 background colours, then 16 sprite colours.
  The format is `--BBGGRR`: 2 bits per component; multiply by 85 to get 8-bit values.
* Levels 2 and 13 share a palette.
* Metatile words with bit 11 set use the sprite half. Only level 3's octopus metatiles $41-$46 do this.

Palette updaters (`paletteUpdatersPointers` $0D2C, run by `updatePalette` $107C in states $89/$8A):

* **$1089** (levels 1, 3, 5, 9, 16, 17): water sparkle. Every 9 frames it writes $3F/$3D/$3B/$3D
  (table $10D6) to CRAM $0B. It is skipped in a sub-area.
* **All other levels** jump straight to **$10B0**, which only flashes CRAM $14 (table $10DA) while
  Alex is invincible. $1089 also ends there.

Timers and tables are hard-coded in bank 0.

### 8.5 Tile animations

`updateLevelTiles` $158F:

* runs every 18 frames ($12 immediate at $15A5);
* maps **$85** ($15A7);
* jumps through `levelTileUpdatersPointers` ($156D).

The frames are raw tile data in bank 5, listed in pointer tables in bank 0. Six-frame tables
play 0,1,2,3,2,1; four-frame tables play 0,1,2,3.

| Updater | Levels | Frame table | Frames × bytes | Tiles |
|---|---|---|---|---|
| updateWaterTilesA $15D2 | 1, 3, 5, 9, 17 | $1620 | 6 × $40 | 136-137 |
| updateSwampTiles $15DF | 2, 6, 10, 13, 15 | $162C | 6 × $40 | 70-71 |
| updateLavaTilesA $15EC | 4 | $1638 | 4 × $60 | 79-81 |
| updateWaterTilesB $15F9 | 16 | $1640, then $1620 | 4 × $60, then 6 × $40 | 69-71, 136-137 |
| updateLavaTilesB $1612 | 7 | $1648 | 4 × $60 | 90-92 |
| doNotUpdateTiles $161F | 8, 11, 12, 14 | – | – | – |

The frame indexes are never reset between levels. The static tileset already contains one
frame at each destination.

### 8.6 Other graphics reloads

**Pause map.** The map overwrites background tiles 0-155. `exitMapState` $1FE9 reloads the palette,
`loadLevelTiles`, the boxes, the font, the capsule/bracelet tiles and `loadLevelSpriteTiles`. In a
sub-area it uses level+1.

**Shop.** The shop replaces tiles 41-201 and the palette. Its exit ($1C33) reloads the palette,
`loadLevelTiles` and the font.

**Janken battles.** Battles load bank-4 tiles ($791D) into sprite tiles 288+ and 384+.

**Sub-areas.** Section 4.4 applies: the graphics of level+1 are used, but the tile/palette
updaters of the current level keep running.

## 9. Other per-level tables

All of these tables sit in the fixed banks 0/1 and are indexed by v_level−1. Their addresses are
immediates in code. "Code" entries are routine addresses: an editor can choose between the
existing routines but not invent new behaviour without new code.

| Lv | Alex x,y | Song | Spawn | ?-box idx | Door x, ptr | Tiles upd | Pal upd | Scroll upd | Ent loader | Camera | Crash→water | Map arrow | Shop items / sold |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | $20,$58 | $82 | 0 | 1 | $00, $0000 | $15D2 | $1089 | $645B | $6F41 | $3F55 | 1 | $4D,$74 | $BE45 / $BF51 |
| 2 | $20,$88 | $82 | 0 | 2 | $60, $CC54 | $15DF | $10DE | $645B | $6F41 | $3F6E | 0 | $44,$64 | $BE45 / $BF51 |
| 3 | $40,$20 | $83 | 0 | 3 | $72, $CC5C | $15D2 | $1089 | $6574 | $6F41 | $3F6E | 0 | $53,$5C | $BE51 / $BF60 |
| 4 | $1B,$90 | $82 | 0 | 4 | $00, $0000 | $15EC | $10E1 | $645B | $6F41 | $3F6E | 0 | $5A,$4E | $BE51 / $BF60 |
| 5 | $20,$70 | $88 | 9 | 5 | $42, $C940 | $15D2 | $1089 | $6532 | $6F41 | $3FCA | 1 | $62,$4E | $BE51 / $BF60 |
| 6 | $20,$88 | $82 | 0 | 6 | $90, $CC20 | $15DF | $10E4 | $645B | $6F41 | $3F6E | 0 | $70,$45 | $BE51 / $BF60 |
| 7 | $20,$88 | $82 | 0 | 7 | $00, $0000 | $1612 | $10E7 | $645B | $6F41 | $3F6E | 0 | $68,$30 | $BE5D / $BF6F |
| 8 | $20,$88 | $82 | 0 | 8 | $30, $CC08 | $161F | $10EA | $645B | $6F41 | $3F6E | 0 | $7D,$2A | $BE5D / $BF6F |
| 9 | $20,$88 | $83 | 1 | 2 | $00, $0000 | $15D2 | $1089 | $6532 | $6F41 | $3FCA | 1 | $8D,$25 | $BE69 / $BF7E |
| 10 | $20,$88 | $82 | 0 | 10 | $70, $CC18 | $15DF | $10ED | $645B | $6F41 | $3F6E | 0 | $9B,$32 | $BE69 / $BF7E |
| 11 | $20,$88 | $84 | 0 | 11 | $00, $0000 | $161F | $10F0 | $6476 | $707D | $401E | 0 | $AD,$2A | $BE75 / $BF8D |
| 12 | $20,$88 | $82 | 0 | 12 | $50, $C9D0 | $161F | $10F3 | $645B | $6F41 | $3F6E | 0 | $C0,$30 | $BE75 / $BF8D |
| 13 | $E8,$70 | $88 | 9 | 13 | $00, $0000 | $15DF | $10F6 | $645B | $6F41 | $3F66 | 0 | $B8,$45 | $BE75 / $BF8D |
| 14 | $20,$88 | $82 | 0 | 14 | $00, $0000 | $161F | $10F9 | $645B | $6F41 | $3F6E | 0 | $70,$80 | $BE75 / $BF8D |
| 15 | $20,$88 | $82 | 0 | 15 | $50, $CC10 | $15DF | $10FC | $645B | $6F41 | $3F6E | 0 | $90,$6E | $BE81 / $BF9C |
| 16 | $20,$88 | $84 | 0 | 16 | $00, $0000 | $15F9 | $1089 | $6476 | $707D | $401E | 0 | $A4,$44 | $BE81 / $BF9C |
| 17 | $10,$88 | $82 | 0 | 17 | $00, $0000 | $15D2 | $1089 | $645B | $6F41 | $3F55 | 0 | $B4,$70 | $BE81 / $BF9C |

* **startingPositions $0DA3** (x, y). Alex's `xPos.high`/`yPos.high` in screen pixels on the
  first screen (`initGameplayStateSecondary` $0C4B).
* **levelSongs $0DC5.** A sound command: $82 main theme, $83 underwater, $84 castle, $88
  Peticopter. There are 18 entries. In a sub-area, and after a life lost in one, entry level+1 is
  used.
* **levelSpawnStates $0E1F.**
  * 0 = on foot.
  * 1 = boat (`v_shouldSpawnRidingBoat` $C051).
  * Any other value = Peticopter (`v_alexActionState` = 9).
  * Non-zero values also load the bullet tiles.
* **levelQuestionMarkBoxIndexes $0E30.** The initial `v_questionMarkBoxIndex` ($C07C). Each
  question box broken spawns `questionMarkBoxItems[$0DD7 + index++]`; the table repeats 1up $4D,
  power bracelet $4E, ghost $4F. Level 9 uses 2, which looks like a data slip.
* **shopDoorsConfigs $0D70.** Three bytes per level: x (in name-table pixels) and the RAM
  name-table address of the door or pot. Uses:
  * x is where Alex walks before entering a shop door or an ENTER-DOWN hole.
  * The address is the redraw origin for the door-priority and pot effects.
  * 0/0 means the level has no door.
  * The octopus record ($82) overwrites the address.
* **levelTileUpdatersPointers $156D, paletteUpdatersPointers $0D2C.** Section 8.
* **scrollFlagsUpdatersPointers $0D0A.** Code run every frame (`updateScrollFlags` $6457):
  * **$645B** (default): only acts when flags bit 7 is set and neither up nor down is. It then
    sets VDP R0 = $26 and flags = right. This is level 1's switch from vertical to horizontal; for
    level 17 it never triggers.
  * **$6476** (castles): per-room persistence (6.4). When one screen has been scrolled
    (scroll = 0), it clears the direction bits, the speeds and the auto-walk.
  * **$6532** (levels 5 and 9) and **$6574** (level 3): when a vertical scroll starts, they
    replace "right" by bit 7. When the drop has finished they set v = 1 and reset the column
    cursors, then:
    * $6532: if h < 3, width = 3 and flags = right; otherwise flags = 0.
    * $6574: width = 1 and flags = right.
* **entityLoadersPointers $0D4E.** $6F41 normal, $707D castle (section 7.2).
* **Camera handlers $3F33.** Called from `updateAlex` with `rst $20`. They decide when the
  camera scrolls:
  * **$3F6E** standard: follows Alex horizontally. The camera starts at x ≥ $60 + lead and moves
    only in the directions allowed by the flags; x < 4 and x ≥ $F4 act as walls. The motorcycle
    and boat force auto-scroll.
  * **$3F66** (level 13): keeps Alex at x = $80.
  * **$3F55** (levels 1, 17): also follows the fall. While flag "down" is set, the vertical speed
    is Alex's y speed once y ≥ $50.
  * **$3FCA** (levels 5, 9): a crash dives into the lower row through `_LABEL_6671_`.
  * **$401E** (castles): leaving the screen at x ≥ $F0 or y ≥ $A8 starts a one-screen
    transition. The directions are fixed: right/left ±$0400, down +$0400 via `_LABEL_6671_`,
    up −$0400. The castle room layout must match the doors and holes.
* **Crash-to-water flags $3904.** 1 = a wrecked vehicle drops Alex into the water (state $1B);
  0 = he pops out on foot.
* **Map arrow positions $1BA7.** The arrow's (x, y) on the pause/level-start map.
* **Shop tables.**
  * $1F89 points to bank 6: 3 × (dw name-table destination, dw item graphics).
  * $1FAB points to bank 6: 3 × (db item, dw flag RAM, dw name-table).
  * Both are read with the **$86 immediate** at $1E31 and $1C04.
* **Global tables:**
  * `questionMarkBoxItems` $0DD7: 72 bytes.
  * Level-17 floor sequence $3DE9: `05 01 02 03 05 02 01 04 03 04 FF`.
  * Octopus arm sets $70FB: 2 words → $70FF / $7121. Each set is a pot address followed by
    8 × (delay, y, x, follower).
  * Demo levels $0A7C: 2, 3, 4, 5. Their inputs are in bank 5.
  * Life-lost respawn entities $6F29.

## 10. Special levels and level-number checks in code

Every read of v_level ($C023) was checked: 69 instructions. The level-dependent behaviour that
is **hard-coded** (compare instructions or fixed indexes, not tables):

| Level(s) | Where | Effect |
|---|---|---|
| 1, 17 | `loadLevel` $6617/$6641 | Vertical start: v = 0, v_currentScreenNumber = $81, preload the row below, VDP R0 = $06 |
| 1 | `updateLifeLostState` $6C86 | Excluded from the "flags bit 7 → respawn at the room snapshot" rule |
| 13 | `loadLevel` $6646 | v_currentScreenNumber = 7 (level scrolls left from screen 7) |
| 13 | `spawnFlyingPeticopter` $2A34 | Peticopter faces left |
| 13 | `updateLifeLostState` $6CB6 | Losing a life restarts the whole level |
| 11 | `initGameplayState` $0BA0 | Castle block table $97DD, 5 rooms/row, $C08E = 1 |
| 16 | `initGameplayState` $0BF3 | Castle block table $9800, 7 rooms/row, $C08E = 1 |
| ≥ 16 | $19B3, $2296 | Janken's castle icon on the map |
| 16 | $2145, $23C3, $6D85, audio $9D08 | Keeps the castle song while swimming (no underwater song) |
| 17 | `interactWithFloor` handlers $3D6B | Floor-symbol puzzle (sequence $3DE9). 10 correct steps spawn item $52 data 0 (crown); a mistake spawns a ghost |
| 17 | bonus loader $1751/$1814 | Level-17 sub-area / ending path (4.4) |
| ≠ 17 | bonus loader $175B | Generic sub-area: layout $8AD6 + graphics of level+1 |
| < 5 | octopus arm $4CBC | Pot patch $83 (can enter) on screens < 3, else $84 |
| 10 | level complete $192B | Pre-sets `$D802` bit 0 (the letter item in level 11 stays hidden until Egle is freed) |
| 2 | demo $0A08 | Demo 1 starts on the motorcycle |
| 2, 3, $0B | tileset loaders $0F77, $1004, $102D, $1063 | Fixed indexes into `levelMainTilesetPointers` (tilesets shared between levels) |

Level progression:

* The only level end is touching a rice ball (entity $44: `updateRiceBall` $5BCA → state 4).
* `updateLevelCompletedState` $18DE then does `inc (v_level)`, so the next level is always
  level+1; there is no table.
* Rice balls come from $84 records, from defeated bosses (entities $1D-$1F and $1C, placed with
  $84 records, turn into $43 and then $44), and from entities $50/$51.
* Level 17 has no rice ball: the crown (item $52 data 0) sets `$D800` and the state-8 path shows
  the ending ($189A).

Special level families:

* **Vertical levels (1, 17):** section 4.3. Level 1's vertical part is 10 screens (`cols[0]`), and
  its horizontal part is the bottom row of `rows[0]`. Level 17 is 2 screens tall (sky/lake surface,
  then underwater). Its sub-area is screens 2-3 of its row:
  * screen 2 is a brick room with a door; code places trigger $4C (data 1) and entity $60;
  * screen 3 is the room with the symbol floor (the crown puzzle); code places entity $61.
* **Reverse level (13):** starts at screen 7 (startScreenX = 8), scrolls left (flags $04), and the
  camera handler keeps Alex centred. A rice ball in the $84 record of screen 0 ends it.
* **Castles (11, 16):** a room grid. Other properties:
  * castle song;
  * room transitions by the $401E camera handler;
  * `v_entityIndex` entity loader;
  * persistent block table;
  * auto-walk in from the entrance screen (flags bit 7);
  * on death Alex returns to the snapshot taken when entering the room.
* **Underwater and lake levels:**
  * Level 3 is underwater from the start: song $83, start y = $20.
  * Levels 5 and 9 have an underwater lower row reached by crashing (levels 5, 9 camera
    handler $3FCA) or by an ENTER-DOWN tile.
  * Level 1's bottom row and level 17 contain water.
  * Water behaviour comes from $20 flags (section 6), not from the level number, except the
    song rules above.
* **Peticopter levels (5, 13):** spawn state 9 and song $88. **Boat level (9):** spawn state 1.
  In these levels, and on the motorcycle, a lost life restarts the level. The motorcycle is an
  item, not a level property; the only hard-coded motorcycle is the demo.
* **Sub-areas:** section 4.4. Level 3's land area uses level 4's layout screens 5-7 and level 4's
  graphics. Level 17 uses its own screens 2-3.
* **Octopus (level 3):** `$82` records place 8 arm segments and set the pot address. Defeating it
  redraws the pot with request $83, which you can enter through ENTER-DOWN, or $84, which you
  cannot.
* **Bosses** are placed with `$84` records:
  * Gooseka $1D: level 2 screen 17, level 11 room 14
  * Chokkinna $1E: level 7 screen 8, level 12 screen 2
  * Parplin $1F: level 10 screen 14, level 15 screen 10
  * Janken $1C: level 16 room 13
  
  Their data byte selects the opponent and whether it is the first or second encounter. They
  wait until the scroll has stopped, i.e. the last screen of the level.
* **Life lost** (`updateLifeLostState` $6C05): there are no checkpoints.
  * Levels with flag bit 7 other than level 1 respawn at the room-entry snapshot ($C240).
  * Swimming Alex: the first free spot from (x=$10, y=$10).
  * Otherwise the game searches the visible screen for a free spot with solid ground under both
    feet.

## 11. Bank handling: every routine that reads level data

"Data" means the address or bank comes from level data. "Hard-coded" means it is an immediate
in code, which must be patched to move the data.

| Data | Reader(s) | Slot 2 when read | Address source | Bank source |
|---|---|---|---|---|
| Descriptor pointer table + descriptors | `loadLevel` $65AA | not used (bank 1 is fixed). Bank 3 is still mapped from loadLevelTiles, so descriptors must stay below $8000. | table: immediate $66CD at $65AD; descriptors: table data | fixed bank |
| Rows table → row tables → screens | `_LABEL_6841_` $683A, then RLE `_LABEL_6BEF_` $6BE8; also the first draw in `loadLevel` | `v_levelBankNumber`, written at $6848 before each fetch | descriptor rowsPtr | **data** (descriptor byte 0) |
| Cols table → column tables → screens | `_LABEL_6B21_` $6B1A → $6BE8 | `v_levelBankNumber`, written at $6B2C | descriptor colsPtr | **data** |
| Sub-area layouts | bonus loader $1735 → the same fetch routines | stale `v_levelBankNumber` of the current level | **hard-coded** $8AD6 ($177D/$1780), $BC53 ($178F/$1792) | stale data |
| Metatile pointer table + entries (columns/rows) | `loadLinesToNametable` $685E, `_LABEL_6A76_` $6A6F | **$85 hard-coded** ($6870, $6A89) | descriptor metatileTablePtr → $C087 | **hard-coded** |
| Metatile entries for $D8A0 patches | nametable changer $61C6 | $85 hard-coded ($6257), then $82 restored | $C087 | hard-coded |
| Fixed metatiles (background, water, pot, doorway, captive, battle) | $45B7, $3EFC, $4244 (request $80, maps $85), $42F1, $43C3, $5FAA, $7773/$777A | $85 hard-coded | **hard-coded** $8503/$850B/$8400/$8410/$8420/$8B5D/$8B75/$8A65 | hard-coded |
| RAM name-table mirror (collision) | $7C44 family, `interactWithTile` $3C41, `interactWithFloor` $3D03, … | n/a (RAM $C800) | derived from metatiles | – |
| Entity pointer table | `initGameplayState` $0B7D | $82, written at $0B78 | **hard-coded** $B503 (at $0B81) | hard-coded |
| Per-screen entity tables + streams | `loadEntitiesNormal` $6F41, `loadEntitiesSpecial` $707D, special records $6FD8, `loadOctopusArms` $7028 | **bank 2 assumed.** No write in the loaders; `updateNametable` $6B42 writes $82 every frame | `v_entityDescriptorsPointer` (data from the table) | **implicit bank 2** |
| Octopus arm sets | `loadOctopusArms` $7028 | fixed bank 1 | hard-coded $70F9 | fixed |
| $D8A0 patch list | name-table changer $61C6 ($61EF, $6238) | RAM | – | – |
| Castle block tables | `initGameplayState` $0BA8/$0BF3 | $82 (from $0B78) | **hard-coded** $97DD/$9800 + level compares | hard-coded |
| $D900 persistence | `scrollFlagsUpdater` $6476, request $80 handler $4244 | RAM | – | – |
| Main tilesets | tileset loaders $0E9F-$107B | 3, mapped by the callers ($0B48, $175E, $2035, $1C93) | table `levelMainTilesetPointers` bank 3 $8480 (**hard-coded** $847E); set addresses are data in that table | **hard-coded** (bank 3) |
| Additional tile sets, fills, VRAM destinations | tileset loaders | 3 | **hard-coded** immediates | hard-coded |
| Sprite tiles | $1164-$156C | **$87 hard-coded** ($1135); Peticopter/powder use $85 | hard-coded immediates | hard-coded |
| Fixed tiles (font, boxes, rice ball, money, ghost, 1up, bracelet, bullets) | `initGameplayState` | $85 ($0B3B), $83 ($0B49), 7, $87 ($0CAF), $85 ($0CC9) | hard-coded | hard-coded |
| Palettes | `loadLevelPalette` $10FF | **$87 hard-coded** ($1100) | table $1112 (hard-coded $1110) → bank-7 addresses | hard-coded |
| Tile animation frames | `updateLevelTiles` $158F + updaters | **$85 hard-coded** ($15A7) | frame tables $1620-$164F (bank 0, hard-coded per updater) | hard-coded |
| Per-level tables (section 9) | init, life lost, shop, map, … | fixed banks | hard-coded table addresses | fixed |
| Shop tables | $1C04, $1E31 | **$86 hard-coded** | tables $1F89/$1FAB | hard-coded |

Conclusions for relocating data:

* **Layouts and screens can move to any bank** (including new banks of an expanded ROM) by
  changing the descriptor's bank byte and pointers. The two fetch routines always re-map
  `v_levelBankNumber` before reading.
  * **Exception: sub-areas.** Level 3's sub-area reads `$8AD6` in the bank of level 3, so level 4's
    layout (or a copy of it) must stay at $8AD6 in level 3's bank, or the immediates at
    $177D/$1780 must be patched. The same applies to $BC53 for level 17 ($178F/$1792).
* **Descriptors** must stay in banks 0/1: they are read while bank 3 is mapped.
* **Metatile tables** can be anywhere **in bank 5** (the pointer is data). Another bank needs
  patches at $6870/$6872, $6A89/$6A8B, $6257/$6259 and in the request-$80 handler $4244, which
  also reads the fixed entries.
* **Entity descriptors must stay in bank 2.** The loaders do not map a bank, and the table address
  is an immediate.
* **Graphics** (tiles, palettes, animations) are all hard-coded to banks 3/5/7. The loaders are
  code, so a new tileset needs a new loader routine or patched immediates. Only the choice of
  loader, main set, palette and updater per level is data.
* **The bank values written are $80|n.**
  * The Sega mapper uses the low bits, so on a ROM of up to 2 MB, $80|n selects bank n: an
    expanded ROM works unchanged, and new banks are reached by writing $88 and above.
  * The interrupt handler saves and restores slot 2 by reading the $FFFF RAM mirror ($DFFF), so
    extra switches are safe.
  * The C port masks banks with `& 7` (per the task description) and would need a larger mask.

## 12. Free space and what an editor can change safely

Free ($FF padding at the end of each bank, ROM offsets):

| Bank | Free | Notes |
|---|---|---|
| 2 | $BE88-$BFFF (376 bytes) | directly after the last entity stream: room for more entity data |
| 3 | $FFDC-$FFFF (36) | |
| 4 | $13FBB-$13FFF (69) | |
| 5 | $17ED1-$17FFF (303) | usable for metatile entries |
| 6 | $1BFAB-$1BFFF (85) | layout data ends at $1BDB8; $1BDB9-$1BFAA holds pause-menu and shop data |
| 7 | $1FF86-$1FFFF (122) | |
| 0, 1 | none | |

Unreferenced bytes inside level data (dead data, safe to reuse):

* $1A208 (1 byte, an extra $00 after screen $A201 of level 10);
* in bank 2, 4-byte entity records after the end of a stream (the count was reduced):
  * $B8C1 (level 5 index 16)
  * $B9E9, $BA0D, $BA3B, $BA4D (level 9 indexes 2, 6, 12, 14)

Rules for an editor:

1. **Screens.**
   * Edit the 192 metatile ids freely, then re-encode with the RLE of section 5.
   * If the stream grows, move it: rewrite every row/column table entry that points to it, and
     keep it in the level's bank.
   * Shared screens change everywhere they are used.
   * Screen x,y positions matter for the fixed-position systems:
     * shop door/hole x and pointer ($0D70);
     * $88 name-table patches (raw mirror addresses);
     * hard-coded mirror cells in entities $63 ($CC08) and $51 ($CE84);
     * castle block positions (6.4).
2. **Layouts.**
   * Row r / column c tables must stay consistent. For horizontal levels, `cols` may simply be
     the same table.
   * width = (number of screens in the row) − 1.
   * The first screen shown is `rows[startY][startX−1]`.
   * Vertical levels also need `cols[startX−1]` entries 0..height.
3. **Entities.**
   * Use indexes as in 4.3.
   * At most 10 normal entities are alive at once, plus the special slots.
   * Index 0 (the start screen) is never loaded, so put nothing there.
   * Bosses and rice balls go in $84 records.
   * Keep the streams in bank 2 (376 free bytes plus the space freed by rewriting).
4. **Metatiles.**
   * An entry's words carry the collision flags, and some behaviours depend on fixed tile numbers
     (6.2: ladder $3F, doors ≥ $70, boxes 1-12, floors $0D-$24). New entries should copy an
     existing entry of the wanted class and change only the tiles.
   * Never edit the stub ids $0D-$0F, $12-$13, $1B-$1F (and $59-$67 in table A).
   * Table A and table B share most entries: edits to a shared entry affect all 17 levels.
5. **Castles.** Regenerate the breakable-block tables of 6.4 when money bags or boxes move in
   levels 11 and 16.
6. **Header checksum.** The SEGA header checksum at $7FFA is the 16-bit sum of the whole 128 KB
   except $7FF0-$7FFF (size nibble $F at $7FFF). Real consoles with the export BIOS check it,
   so it must be recomputed after patching (`levels.apply_patches` does this).

## 13. `maker/tools/levels.py`

Python 3.7, standard library only. It reads `original.sms` at run time and stores no game data.

```
python3 maker/tools/levels.py dump N            descriptor, tables, layout tables, map grid, every screen's
                                          metatile ids, entity streams (decoded)
python3 maker/tools/levels.py verify            decode -> JSON -> encode, byte comparison with the ROM,
                                          RLE fallback check and coverage report
python3 maker/tools/levels.py render N out.ppm  whole map (plus sub-area row) with the level's own VRAM tiles;
                                          options --no-entities --no-grid --scale K --vram DUMP
python3 maker/tools/levels.py json [N]          the model (or one level) as JSON
options: --rom PATH
```

### 13.1 API

* `load(rom_bytes)` returns a plain dict:
  * `metatile_tables.A|B.entries[256]`: `ptr`, `words` (TL, TR, BL, BR; authoritative for
    encoding), `decoded` (tile/flip/palette/priority/game_flags per word), `word_kinds`, `class`,
    `overlaps_next_entry`.
  * `globals`: question-box items, floor puzzle sequence, octopus arm sets, demo levels.
  * `levels[17]`, each with:
    * `descriptor`: all fields of section 3, plus `rom_offset`.
    * `layout.rows[]` / `layout.cols[]`: each `{ptr, screens:[index into screens]}`; plus
      `cols_is_rows`.
    * `screens[]`: `{ptr, rom_offset, encoded_size, metatiles[192], rle_tokens?}`.
    * `map[]`: derived placement `{x, y, screen, entity_index, part}` (section 4.3). It is not
      encoded.
    * `kind`: horizontal, reverse, vertical, drop or castle.
    * `sub_area`: levels 3 and 17.
    * `entities`: `{table_ptr, screens:[{ptr, size, records:[…]}]}`. The record kinds are
      `entity`, `extra_slot` ($81), `octopus_arms` ($82), `fixed_slot` ($84) and `ram_block` ($88,
      decoded into `patches`), each with the original `code` byte.
    * `tables`: every value of section 9.
    * `graphics`: palette pointer, the exact list of VRAM loads (`tile_loads`,
      `background_loads`) and `tile_animations`.
    * `castle_blocks`: levels 11 and 16.
* `encode(model)` returns `[(rom_offset, bytes)]` for all of the above: descriptor tables and
  descriptors, rows/cols/row/column tables, screens, entity pointer tables and streams, per-level
  tables, castle tables, globals, and metatile pointer tables and entries.
  * Every structure is written at its `ptr`.
  * To grow data, the caller changes the `ptr` fields (and the pointers that reference them)
    to free space, respecting section 11.
  * Screens are encoded with their `rle_tokens` if still valid, otherwise with the canonical
    encoder (`rle_encode`).
* `apply_patches(rom_bytes, patches, fix_checksum=True)` returns the patched ROM with the header
  checksum fixed. `header_checksum(rom)` computes that checksum.
* `level_vram(rom_bytes, level)` returns `(vram[16384], cram[32])` as `initGameplayState` leaves
  them. `render_level(model, level, vram, cram, …)` returns a `Canvas` (PPM writer).

### 13.2 Verification performed

`verify` output (abridged):

```
encode(): 1306 blocks, 24407 bytes, 0 mismatching blocks
level  1 ... level 17: descriptor+layout ok, screens, entity streams, castle block tables ok
RLE: 166/174 screens re-encoded byte-identically by the canonical encoder; the other 8 are
     reproduced through their recorded tokenisation (rle_tokens).
  canonical-only re-encoding: decode(encode(x)) == x for all 174 screens: yes; size <= original for all: yes
coverage: descriptors fully covered; bank 6 all but 1 dead byte; bank 2 all but 5 dead records
VERIFY PASSED
```

Rendering checks. `render` for levels 1, 5 and 11 was compared with screenshots of the running
game (lockstep harness at the start of the level).

* Background pixels are identical. The only differences are sprites, the masked left column and
  the entity-driven spike pillars of level 11.
* For every level's start screen, the pixel difference is ≤ 8%, due to sprites, palette cycling
  and animated tiles.
* Castles were compared after the auto-walk into room 1.

VRAM checks. The VRAM produced by the ROM-based loader was compared with the game's VRAM at
the start of all 17 levels. Every tile used by the level's metatiles is identical, except
tiles that are animated at run time (136-137, 70-71, 79-81, 90-92, 69-71). All raw-copied
sprite tiles are identical too. CRAM is identical except in the palette-cycling levels.

Edit round trip in the game. The following was done on a scratch copy of the ROM:

1. Level 2's first screen was loaded.
2. Six question boxes and a star box were drawn into it through `metatiles`.
3. The screen's `ptr` was moved to the free space at bank 6 $BFAB.
4. `encode` rewrote the row table, and the patches were applied.

The patched ROM ran in the harness and showed the new blocks on level 2's first screen.

Entity check, level 1:

* After the first screen loaded, slot 6 ($C3C0) held type $2D at x=$68, y=$78, off-screen page
  (0, +1). This is index 1's record `2D 78 68 00`.
* After falling one screen, index 2 (`2D 20 88 00`, `2D 98 40 00`) appeared in slots 6/7 as
  x=$88/$40, y=$1C/$94. That was one frame after v_currentScreenNumber became $82, with the
  camera 4 px further down.

## 14. Open questions

* **Level names.** Level names other than the obvious ones are inherited from the reference
  (2 "Mt. Eternal part 2", 5 "Lake Fathom part 2", 13 "Swamp"). Use numbers.
* **The 8 non-canonical screens** (section 5) look hand-edited. No single rule reproduces them;
  `levels.py` keeps their tokenisation instead.
* **Entity x at load time** is relative to the camera. When a screen starts entering, the camera
  is 0-7 px into the current screen, so an entity with x smaller than that offset can land one
  page off. Keep x ≥ 8 to be safe (not verified in play).
* **$88 name-table patches** use raw mirror addresses. In rooms reached by vertical scrolling
  (castles, level 1), the name-table row that corresponds to a screen row depends on how far the
  name table has wrapped (28 rows versus 24 per screen). The existing data was authored for those
  exact positions. Computing the address for a new position needs care and was not verified.
* **Money pickup** skips a metatile whose left column is at name-table column 31. The exact
  intent is unclear (probably the scroll seam).
* **Castle block lists.** Two level-16 positions (room [0][6] pos $6E, room [3][4] pos $53) do
  not match a static collectable. They may refer to blocks created by name-table changers.
* **Question-box index.** `levelQuestionMarkBoxIndexes` for level 9 is 2 instead of 9, and
  `levelSongs` has an 18th entry. Both are harmless and look like data slips.
* **Vehicle crash flags.** `$3904` flags level 1 as "crash into water", but level 1 has no vehicle.
* **Sub-areas.** The generic path (layout $8AD6 + graphics of level+1) only makes sense for
  level 3. Other levels were not tested with a $4C trigger.
* **Expanded ROM.** Using an expanded ROM ($88+ bank values) was reasoned about from the mapper
  semantics but not tested. The port's `& 7` mapper mask would have to change.
* **Castle transitions.** The castle room-transition rules (which Alex flag selects up or down
  when leaving through the bottom) were read from code and not exercised in the harness.
