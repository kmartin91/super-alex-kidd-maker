/*
 * Name-table change requests: game code asks for a change of the background
 * by writing v_nametableChangeRequest ($80 | handler index); the VBlank
 * handler (handleNametableChangeRequest) performs it, on the VDP name table
 * and, for most requests, on the RAM mirror (collision) as well.
 *
 *   $80  write metatile v_nametableChangeSource at mirror address
 *        v_nametableChangeDestination (blocks broken, money taken, $88 patches)
 *   $81  shop door: give its tiles priority (Alex walks behind it)
 *   $82  the same for the 4x4 octopus pot
 *   $83  octopus defeated: pot that can be entered (ENTER-DOWN floor)
 *   $84  octopus defeated: pot that cannot be entered
 *   $85-$87  spiked pillars / ceiling / collapsing floors (module enemies1)
 *   $88  Janken petrification, one step per frame
 *   $89  doorway of entity $60 (level 17 sub-area)
 *
 * Also here: the name-table changer entity ($4B), which applies the patch
 * list of a screen's $88 record one change at a time, and requestBlockSound.
 */
#include "level.h"

#define NAMETABLE_CHANGE_HANDLERS 0x4230  /* nametableChangeHandlersPointers */
#define REQUEST_PENDING 0x80
#define REQUEST_WRITE_METATILE 0x80
#define REQUEST_JANKEN_PETRIFICATION 0x88

/* Bank 5 name-table words. */
#define WATER_METATILE 0x850B             /* metatile $21: 4 water words */
#define OCTOPUS_POT_GRAPHICS 0x8400       /* 2 rows x 4 words */
#define OCTOPUS_POT_ENTERABLE 0x8400      /* mirror version with the ENTER-DOWN floor */
#define OCTOPUS_POT_CLOSED 0x8410
#define DOORWAY_WORDS 0x8420              /* 6 rows x 4 words */
#define DOORWAY_VDP 0x7BB4                /* name table row 14, column 26 */
#define DOORWAY_MIRROR 0xCBB4
#define DOORWAY_ROWS 6
#define DOORWAY_ROW_BYTES 8

/* Bank 4: per petrification step pair, list of VDP destinations. */
#define JANKEN_PETRIFICATION_TABLE 0xAAC2
#define PETRIFICATION_STEPS 0x30
#define v_petrificationStep _RAM_C218_    /* 0 to $2F */
#define v_petrificationColumn _RAM_C219_  /* word: (step & 7) * 4 */
#define v_petrificationSource _RAM_C21B_  /* word: next 4 name-table bytes (bank 4) */

#define PRIORITY_ATTRIBUTE 0x10           /* attribute bit 4 = word bit 12 */

/* $4222 handleNametableChangeRequest (VBlank): takes the pending request and
 * jumps to its handler (jumpToPointerAtA with A = 2 * request). */
LIFTED(handleNametableChangeRequest, 0x4222) {
    uint8_t request = ram8(v_nametableChangeRequest);
    ram8(v_nametableChangeRequest) = 0;
    if (!(request & REQUEST_PENDING)) LIFTED_RETURN();
    cpu.a = request;
    alu_add(cpu.a);
    cpu.hl = NAMETABLE_CHANGE_HANDLERS;
    TAIL_CALL(f_jumpToPointerAtA);
}

/* ------------------------------------------------ $80: write a metatile */

/* In castles, the position of the changed metatile in the room (row * 16 +
 * column, from the mirror address and the vertical scroll) is looked up in
 * the room's record of breakable blocks (v_roomRecord) and flagged there, so
 * that the block stays broken when the room is entered again. */
static void remember_broken_block(void) {
    uint8_t scrolled_rows = ram8(v_verticalScrollLine) & 0xF0; /* metatile rows * 16 */
    uint16_t dest = ram16(v_nametableChangeDestination);
    uint8_t column = (uint8_t)((dest & 0xFF) >> 2) & 0x0F;
    uint8_t row16 = (uint8_t)((dest >> 7) << 4);               /* (mirror row / 2) * 16 */
    uint8_t position = (uint8_t)(row16 + column);
    bool borrow = position < scrolled_rows;
    position = (uint8_t)(position - scrolled_rows);
    if (borrow) position = (uint8_t)(position - 0x20);         /* the name table has 14 metatile rows */

    uint16_t p = ram16(v_roomRecord);
    uint8_t count = rd8(p);
    for (uint8_t n = count; n != 0; n--) {
        p = (uint16_t)((p & 0xFF00) | (uint8_t)(p + 2));       /* INC L twice */
        if (rd8(p) == position) {
            wr8((uint16_t)((p & 0xFF00) | (uint8_t)(p - 1)), 1);
            return;
        }
    }
}

/* $4244 (request $80): the 8 bytes at v_nametableChangeSource (bank 5) go to
 * the mirror at v_nametableChangeDestination (2 words, then 2 words one row
 * lower) and to the VDP. QUIRK: no wrap at the bottom of the name table. */
LIFTED(_LABEL_424B_, 0x4244) {
    map_bank(BANK(5));
    if (ram8(v_castleBlocksEnabled)) remember_broken_block();

    uint16_t dest = ram16(v_nametableChangeDestination);
    uint16_t src = ram16(v_nametableChangeSource);
    for (int i = 0; i < 4; i++) wr8((uint16_t)(dest + i), rd8((uint16_t)(src + i)));
    for (int i = 0; i < 4; i++)
        wr8((uint16_t)(dest + NAMETABLE_ROW_BYTES + i), rd8((uint16_t)(src + 4 + i)));

    cpu.hl = src;
    cpu.de = (uint16_t)(dest - MIRROR_TO_VDP);
    cpu.bc = 0x0204;               /* 2 rows of 4 bytes */
    CALL_ROUTINE(f_copyNameTableBlockToVram);
    LIFTED_RETURN();
}

/* --------------------------------------------------- $83/$84: octopus pot */

/* $42F1: replaces 3 rows of the pot area with water (at v_shopDoorNametablePointer,
 * mirror and VDP), then draws the pot below them on the VDP. Leaves
 * v_nametableChangeDestination on the mirror row of the pot. out: BC. */
LIFTED(sub_42F1, 0x42F1) {
    map_bank(BANK(5));
    uint16_t dest = cpu.de;
    for (int row = 0; row < 3; row++) {
        for (int i = 0; i < 8; i++) wr8((uint16_t)(dest + i), rd8((uint16_t)(WATER_METATILE + i)));
        dest = (uint16_t)(dest + NAMETABLE_ROW_BYTES);
    }
    ram16(v_nametableChangeDestination) = dest;

    uint16_t vdp = (uint16_t)(ram16(v_shopDoorNametablePointer) - MIRROR_TO_VDP);
    for (int row = 0; row < 3; row++) {
        cpu.hl = WATER_METATILE;
        cpu.de = vdp;
        cpu.b = 8;
        CALL_ROUTINE(f_memcpyToVRAM);
        vdp = (uint16_t)(vdp + NAMETABLE_ROW_BYTES);
    }
    cpu.hl = OCTOPUS_POT_GRAPHICS;
    cpu.de = vdp;
    cpu.bc = 0x0208;               /* 2 rows of 8 bytes */
    TAIL_CALL(f_copyNameTableBlockToVram);
}

/* $42C6: HL = 16 bytes: the pot's two rows into the mirror at
 * v_nametableChangeDestination (collision version of the pot). */
LIFTED(sub_42C6, 0x42C6) {
    uint16_t dest = ram16(v_nametableChangeDestination);
    uint16_t src = cpu.hl;
    for (int row = 0; row < 2; row++, dest = (uint16_t)(dest + NAMETABLE_ROW_BYTES))
        for (int i = 0; i < 8; i++) wr8((uint16_t)(dest + i), rd8(src++));
    LIFTED_RETURN();
}

/* $42BC (request $83): the octopus pot that can be entered. */
LIFTED(_LABEL_42C3_, 0x42BC) {
    cpu.de = ram16(v_shopDoorNametablePointer);
    CALL_ROUTINE(f_sub_42F1);
    cpu.hl = OCTOPUS_POT_ENTERABLE;
    TAIL_CALL(f_sub_42C6);
}

/* $42AF (request $84): the same pot graphics, with the closed collision words. */
LIFTED(_LABEL_42B6_, 0x42AF) {
    cpu.de = ram16(v_shopDoorNametablePointer);
    CALL_ROUTINE(f_sub_42F1);
    cpu.hl = OCTOPUS_POT_CLOSED;
    TAIL_CALL(f_sub_42C6);
}

/* ------------------------------------------------ $81/$82: tile priority */

/* $4358 (_LABEL_435F_): HL = mirror address of an attribute byte, DE = its VDP
 * address, B = entries per row, C = rows. Rewrites the attribute bytes on the
 * VDP with the priority bit set (the mirror is unchanged); the tile bytes in
 * between are skipped by reading the data port. */
LIFTED(_LABEL_435F_, 0x4358) {
    uint16_t mirror = cpu.hl, vdp = cpu.de;
    uint8_t entries = cpu.b;
    uint8_t rows = cpu.c;
    do {
        vdp_set_address(vdp);
        uint16_t p = mirror;
        uint8_t n = entries;
        do {
            vdp_write(rd8(p) | PRIORITY_ATTRIBUTE);
            vdp_read();
            p = (uint16_t)(p + 2);
        } while (--n != 0);
        vdp = (uint16_t)(vdp + NAMETABLE_ROW_BYTES);
        mirror = (uint16_t)(mirror + NAMETABLE_ROW_BYTES);
    } while (--rows != 0);
    LIFTED_RETURN();
}

/* $4339 handleShopDoorNametableChange (request $81): the 2x4 door at
 * v_shopDoorNametablePointer. */
LIFTED(handleShopDoorNametableChange, 0x4339) {
    uint16_t attributes = (uint16_t)(ram16(v_shopDoorNametablePointer) + 1);
    cpu.hl = attributes;
    cpu.de = (uint16_t)(attributes - MIRROR_TO_VDP);
    cpu.bc = 0x0204;               /* 2 entries x 4 rows */
    TAIL_CALL(f__LABEL_435F_);
}

/* $4348 (request $82): the 4x4 pot, 3 rows below v_shopDoorNametablePointer. */
LIFTED(_LABEL_434F_, 0x4348) {
    uint16_t attributes = (uint16_t)(ram16(v_shopDoorNametablePointer) + 1 + 3 * NAMETABLE_ROW_BYTES);
    cpu.hl = attributes;
    cpu.de = (uint16_t)(attributes - MIRROR_TO_VDP);
    cpu.bc = 0x0404;               /* 4 entries x 4 rows */
    TAIL_CALL(f__LABEL_435F_);
}

/* ---------------------------------------------- $88, $89: special scenes */

/* $4376 (request $88): one step of Janken's petrification (48 steps, it
 * requests itself again until done). The step picks a list of VDP
 * destinations in bank 4 (by step / 8, offset by (step & 7) * 4); 4 bytes
 * from v_petrificationSource go to each. */
LIFTED(_LABEL_437D_, 0x4376) {
    map_bank(BANK(4));
    uint8_t step = ram8(v_petrificationStep);
    uint16_t column = (uint16_t)((step & 7) * 4);
    ram16(v_petrificationColumn) = column;
    uint16_t list = rd16((uint16_t)(JANKEN_PETRIFICATION_TABLE + ((step & 0xF8) >> 2)));
    uint8_t count = rd8(list);
    uint16_t src = ram16(v_petrificationSource);
    do {
        list = (uint16_t)(list + 2);
        uint16_t destination = (uint16_t)(rd8((uint16_t)(list - 1)) | (rd8(list) << 8));
        cpu.hl = src;
        cpu.de = (uint16_t)(ram16(v_petrificationColumn) + destination);
        cpu.b = 4;
        CALL_ROUTINE(f_memcpyToVRAM);
        src = cpu.hl;
    } while (--count != 0);
    ram16(v_petrificationSource) = src;

    uint8_t next = (uint8_t)(ram8(v_petrificationStep) + 1);
    ram8(v_petrificationStep) = next;
    if (next != PETRIFICATION_STEPS) ram8(v_nametableChangeRequest) = REQUEST_JANKEN_PETRIFICATION;
    LIFTED_RETURN();
}

/* $43C3 (request $89): draws the doorway (6 rows x 4 words) at row 14,
 * columns 26-29, on the VDP and in the mirror. */
LIFTED(_LABEL_43CA_, 0x43C3) {
    map_bank(BANK(5));
    cpu.hl = DOORWAY_WORDS;
    cpu.de = DOORWAY_VDP;
    cpu.bc = (DOORWAY_ROWS << 8) | DOORWAY_ROW_BYTES;
    CALL_ROUTINE(f_copyNameTableBlockToVram);
    uint16_t src = DOORWAY_WORDS, dest = DOORWAY_MIRROR;
    for (int row = 0; row < DOORWAY_ROWS; row++, dest = (uint16_t)(dest + NAMETABLE_ROW_BYTES))
        for (int i = 0; i < DOORWAY_ROW_BYTES; i++) wr8((uint16_t)(dest + i), rd8(src++));
    LIFTED_RETURN();
}

/* -------------------------------------------- name-table changer (entity $4B) */

/* Entity fields. */
#define changeQueued unknown6     /* +$18: a change waits for the request slot */
#define changesDone unknown5      /* +$16: patches applied so far */
#define NULL_SPRITE_DESCRIPTOR 0x80E1
enum { CHANGER_ON_PUNCH = 0, CHANGER_COUNT_PUNCHES = 1, CHANGER_ON_TOUCH = 2 };
#define ALEX_PUNCH_HIT 0x08       /* v_alex.unknown8 bit 3: the punch connected */
#define ENTITY_INITIALIZED 0x01
#define ENTITY_FLAG_1 0x02

/* $6224 (@unkown6IsNotZero): IX = changer. When the request slot is free,
 * requests the next patch of the list at $D8A0 (request $80 with the
 * destination and the metatile's words), or destroys the changer when the
 * list is done. */
LIFTED(updateNametableChanger_unkown6IsNotZero, 0x6224) {
    if (ram8(v_nametableChangeRequest) != 0) LIFTED_RETURN();
    entity_at(v_alex)->unknown8 &= (uint8_t)~ALEX_PUNCH_HIT;
    Entity *e = entity_at(cpu.ix);
    e->changeQueued = 0;
    e->changesDone++;
    if (ram8(v_patchList) < e->changesDone) TAIL_CALL(f_destroyCurrentEntity);

    ram8(v_nametableChangeRequest) = REQUEST_WRITE_METATILE;
    uint16_t patch = ram16(v_patchListCursor);
    ram16(v_nametableChangeDestination) = (uint16_t)(rd8(patch) | (rd8((uint16_t)(patch + 1)) << 8));
    uint8_t metatile = rd8((uint16_t)(patch + 2));
    ram16(v_patchListCursor) = (uint16_t)(patch + 3);
    map_bank(BANK(5));
    ram16(v_nametableChangeSource) = metatile_entry(metatile);
    map_bank(BANK(2));
    LIFTED_RETURN();
}

/* $6220 (@setUnknown6ToOne): queue a change, then as above. */
LIFTED(updateNametableChanger_setUnknown6ToOne, 0x6220) {
    entity_at(cpu.ix)->changeQueued = 1;
    TAIL_CALL(f_updateNametableChanger_unkown6IsNotZero);
}

/* $61C6 updateNametableChanger (entity $4B, invisible). Once on screen it
 * starts reading the patch list of its screen ($D8A0, from the $88 record).
 * Its data byte selects the trigger:
 *   0: each punch that hits it applies one patch;
 *   2: touching it applies one patch per contact;
 *   1: it only counts a punch in v_changerCounter, then disappears.
 * out: IY as the collision helpers leave it. */
LIFTED(updateNametableChanger, 0x61C6) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & ENTITY_INITIALIZED)) {
        e->spriteDescriptorPointer = NULL_SPRITE_DESCRIPTOR;
        if (e->isOffScreenFlags != 0) LIFTED_RETURN();
        e->flags |= ENTITY_INITIALIZED;
        e->flags |= ENTITY_FLAG_1;
        e->changeQueued = 0;
        e->changesDone = 0;
        ram8(v_changerCounter) = 0;
        ram16(v_patchListCursor) = v_patchList + 1;
    }

    if (e->data == CHANGER_COUNT_PUNCHES) {
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (cpu.f & FLAG_C) LIFTED_RETURN();
        ram8(v_changerCounter)++;
        TAIL_CALL(f_destroyCurrentEntity);
    }
    if (e->changeQueued) TAIL_CALL(f_updateNametableChanger_unkown6IsNotZero);

    if (e->data == CHANGER_ON_TOUCH) {
        cpu.iy = v_alex;
        CALL_ROUTINE(f_checkEntityCollision);
        if (cpu.f & FLAG_C) LIFTED_RETURN();
    } else {
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (cpu.f & FLAG_C) LIFTED_RETURN();
        if (!(entity_at(v_alex)->unknown8 & ALEX_PUNCH_HIT)) LIFTED_RETURN();
    }
    TAIL_CALL(f_updateNametableChanger_setUnknown6ToOne);
}

/* ------------------------------------------------------ block debris sound */

#define DEBRIS_SLOT 0xC5C0        /* v_entities.23 */
#define ENTITY_DEBRIS_TOP_LEFT 0x38
#define SOUND_BLOCK 0x8C
#define SOUND_STAR_BOX 0xA3
#define DEBRIS_KIND_STAR_BOX 1

/* $5BFA requestBlockSound: A = debris kind (1 = star box), D = y, E = x.
 * Plays the block sound (or the star-box sound) and starts the debris entity
 * in slot 22 at (x, y). The original saves A in AF' around the sound request,
 * which leaves A' = $8C. out: IY = the debris slot. */
LIFTED(requestBlockSound, 0x5BFA) {
    uint8_t kind = cpu.a;
    ram8(v_soundControl) = SOUND_BLOCK;
    cpu.af_ = (uint16_t)((SOUND_BLOCK << 8) | (cpu.af_ & 0xFF));
    cpu.iy = DEBRIS_SLOT;
    Entity *debris = entity_at(DEBRIS_SLOT);
    debris->type = ENTITY_DEBRIS_TOP_LEFT;
    debris->unknown6 = kind;
    if (kind == DEBRIS_KIND_STAR_BOX) ram8(v_soundControl) = SOUND_STAR_BOX;
    entity_byte(DEBRIS_SLOT, ENTITY_Y) = cpu.d;
    entity_byte(DEBRIS_SLOT, ENTITY_X) = cpu.e;
    debris->flags &= (uint8_t)~ENTITY_INITIALIZED;
    LIFTED_RETURN();
}
