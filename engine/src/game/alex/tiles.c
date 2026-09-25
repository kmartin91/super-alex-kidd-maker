/*
 * What happens when Alex touches special tiles ($3C3E-$3DF3), and the $60
 * event entity ($3E39-$3EB9).
 *
 * interactWithTile looks at the tile under a point of Alex's body (default:
 * his centre, 12 px down and 8 px right of his corner):
 *   solid                 -> Alex is crushed (hit flag, handled by updateAlex)
 *   water ($20)           -> RAM_IN_WATER = $20 (the state handler splashes)
 *   $40 pickup, tile<$90  -> a money bag drawn in the background: collected
 *   $40 pickup, tile>=$90 -> shop shelf: selects the item under Alex
 *   $60 tile >= $70       -> shop door: UP enters (idle, not at the left edge)
 *   $60 tile == $3F       -> ladder: UP grabs it
 *   $60 other tiles       -> deadly (hit flag)
 * interactWithFloor looks at the tile under his feet (default 24 px down):
 *   $A0 tile == $3F       -> top of a ladder: DOWN climbs down
 *   $A0 tile >  $3F       -> hatch: pressing DOWN enters it (state $11)
 *   $A0 tiles $0D-$24     -> level 17 floor effects (table at $3D24): the
 *                            coloured floor puzzle and the ghost floor.
 *
 * Exit flags: the swimming and motorcycle handlers call a terrain probe right
 * after these routines, and the probe keeps the flags in F' (see physics.c),
 * so the flags each path leaves are reproduced.
 */
#include "alex.h"

#define TILE_SHOP_DOOR_MIN 0x70     /* door tiles are $70 and up */
#define TILE_SHOP_SHELF_MIN 0x90    /* shelf tiles are $90 and up */
#define TILE_FLOOR_EFFECT_MIN 0x0D  /* first tile of the $3D24 table */
#define SHOP_SHELF_ITEMS 0x3C9C     /* bank 0: item index per shelf tile - $90 */
#define FLOOR_EFFECT_HANDLERS 0x3D24 /* bank 0: jump table, 24 entries */
#define PUZZLE_SOLUTION 0x3DE9      /* bank 0: expected colour sequence */
#define DOOR_MIN_X 0x18             /* doors are ignored at the left edge */
#define PUZZLE_LEVEL 0x11
#define PUZZLE_STEPS 10
#define GHOST_SPAWN_PERIOD 0x7F

/* ------------------------------------------------------------ body tile */

/* Money bag drawn in the background (HL points at its tile byte). */
static void collect_background_money(uint16_t tile_ptr) {
    uint16_t block = (uint16_t)(tile_ptr & ~0x0043); /* top-left entry of the 16x16 block */
    /* QUIRK: skips the rightmost column of the 32-column name table ring,
     * where the block may be half scrolled out. */
    uint8_t column = (uint8_t)(((ram8(v_levelData_C0B0) >> 2) + (uint8_t)block) & 0x3E);
    if (column == 0x3E) {
        cpu.f = z80_cp_flags(column, 0x3E);
        cpu.hl = block;
        return;
    }
    cpu.hl = block & 0xFF00; /* ld l,$00 (the argument of takeMoney) */
    alex_call(f_takeMoney);
    ram8(v_soundControl) = SOUND_COINS;
    cpu.hl = block;
    uint8_t pending = ram8(v_nametableChangeRequest);
    if (pending) {
        cpu.f = flag_szxyp(pending);
        return;
    }
    ram16(v_nametableChangeDestination) = block;
    alex_request_background_tile(); /* erase the bag */
}

/* Doors, ladders and deadly tiles (class $60). */
static void touch_door_class_tile(uint16_t attr_ptr) {
    Entity *alex = ALEX;
    uint16_t tile_ptr = (uint16_t)(attr_ptr - 1);
    uint8_t tile = rd8(tile_ptr);
    ram16(RAM_SPECIAL_TILE) = tile_ptr;
    cpu.hl = tile_ptr;
    if (tile >= TILE_SHOP_DOOR_MIN) {
        uint8_t input = ram8(v_inputData);
        if (alex->state != ALEX_STATE_IDLE) {
            cpu.f = z80_cp_flags(alex->state, ALEX_STATE_IDLE);
        } else if (HI(alex->xPos) < DOOR_MIN_X) {
            cpu.f = z80_cp_flags(HI(alex->xPos), DOOR_MIN_X);
        } else {
            cpu.f &= (uint8_t)~FLAG_C;
            alu_bit(0, input, input);
            if (input & PAD_UP) alex->state = ALEX_STATE_REACHING_DOOR;
        }
        return;
    }
    cpu.f = z80_cp_flags(tile, TILE_LADDER);
    if (tile != TILE_LADDER) {
        alex->flags |= ENTITY_FLAG_HIT; /* spikes, fire... */
        return;
    }
    uint8_t input = ram8(v_inputData);
    alu_bit(0, input, input);
    if (!(input & PAD_UP)) return;
    alex->ySpeed = 0xFF00; /* climb up at 1 px/frame */
    alex_start_climbing();
}

/* $3C41 interactWithTileAtOffset. Leaves BC/HL as the original (tile lookup
 * position and pointer) and its flags. */
void alex_interact_with_tile(uint16_t offset) {
    Entity *alex = ALEX;
    ram8(RAM_IN_WATER) = 0;
    uint8_t offscreen = HI(alex->isOffScreenFlags);
    if (offscreen) {
        cpu.f = flag_szxyp(offscreen);
        return;
    }
    cpu.de = offset;
    alex_call(f_getNearEntityTileAttrWithOffset);
    uint8_t attr = cpu.a;
    uint16_t attr_ptr = cpu.hl;
    if (attr & TILE_SOLID) {
        alu_bit(7, attr, attr);
        alex->flags |= ENTITY_FLAG_HIT; /* stuck inside a wall */
        return;
    }
    if (!(attr & TILE_SPECIAL)) {
        uint8_t tile_class = attr & TILE_CLASS_MASK;
        cpu.f = z80_cp_flags(tile_class, TILE_CLASS_WATER);
        if (tile_class == TILE_CLASS_WATER) ram8(RAM_IN_WATER) = tile_class;
        return;
    }
    if (attr & TILE_VARIANT) {
        touch_door_class_tile(attr_ptr);
        return;
    }
    uint16_t tile_ptr = (uint16_t)(attr_ptr - 1);
    uint8_t shelf = sub8_flags(rd8(tile_ptr), TILE_SHOP_SHELF_MIN, 0);
    if (!(cpu.f & FLAG_C)) {
        /* Shop: standing in front of an item selects it. */
        cpu.hl = alu_add16(SHOP_SHELF_ITEMS, shelf);
        ram8(v_shopSelectedItemIndex) = rd8(cpu.hl);
        return;
    }
    collect_background_money(tile_ptr);
}

LIFTED(interactWithTileAtOffset, 0x3C41) {
    alex_interact_with_tile(cpu.de);
    LIFTED_RETURN();
}

/* $3C3E interactWithTile: at Alex's centre. */
LIFTED(interactWithTile, 0x3C3E) {
    alex_interact_with_tile(0x0C08);
    LIFTED_RETURN();
}

/* ------------------------------------------------------------ floor tile */

/* $3D03 interactWithFloorWithOffset */
void alex_interact_with_floor(uint16_t offset) {
    Entity *alex = ALEX;
    uint8_t offscreen = HI(alex->isOffScreenFlags);
    if (offscreen) {
        cpu.f = flag_szxyp(offscreen);
        return;
    }
    cpu.de = offset;
    alex_call(f_getNearEntityTileAttrWithOffset);
    if ((cpu.a & TILE_CLASS_MASK) != TILE_CLASS_FLOOR) {
        ram8(v_nextGhostSpawnTimer) = 0; /* left the ghost floor */
        cpu.f = FLAGS_ZERO;
        return;
    }
    uint16_t tile_ptr = (uint16_t)(cpu.hl - 1);
    cpu.hl = tile_ptr;
    ram16(RAM_SPECIAL_TILE) = tile_ptr;
    uint8_t tile = rd8(tile_ptr);
    if (tile > TILE_LADDER) {
        /* Hatch in the floor: DOWN (just pressed) goes through it. */
        uint8_t pressed = ram8(v_inputDataChanges);
        cpu.f &= (uint8_t)~FLAG_C;
        alu_bit(1, pressed, pressed);
        if (pressed & PAD_DOWN) alex->state = ALEX_STATE_TO_HATCH;
        return;
    }
    if (tile == TILE_LADDER) {
        /* Top of a ladder: DOWN climbs down. */
        uint8_t input = ram8(v_inputData);
        cpu.f &= (uint8_t)~FLAG_C;
        alu_bit(1, input, input);
        if (!(input & PAD_DOWN)) return;
        alex->ySpeed = 0x0100;
        alex_start_climbing();
        return;
    }
    cpu.a = sub8_flags(tile, TILE_FLOOR_EFFECT_MIN, 0);
    if (cpu.f & FLAG_C) return;
    /* Floor effects dispatch through the ROM table (tail jump). */
    cpu.hl = FLOOR_EFFECT_HANDLERS;
    alex_call(f_jumpToAthPointer);
}

LIFTED(interactWithFloorWithOffset, 0x3D03) {
    alex_interact_with_floor(cpu.de);
    LIFTED_RETURN();
}

/* $3D00 interactWithFloor: under Alex's feet. */
LIFTED(interactWithFloor, 0x3D00) {
    alex_interact_with_floor(0x1808);
    LIFTED_RETURN();
}

/* $3D6B: level 17 floor puzzle. Stepping from one coloured floor to another
 * of `color` (1-5) must follow the sequence at PUZZLE_SOLUTION; a wrong
 * colour restarts it and spawns a ghost. After 10 correct steps the reward
 * entity ($52) appears in slot 6. */
static void floor_puzzle_step(uint8_t color) {
    cpu.c = color;
    uint8_t level = ram8(v_level);
    if (level != PUZZLE_LEVEL) {
        cpu.f = z80_cp_flags(level, PUZZLE_LEVEL);
        return;
    }
    uint8_t previous = ram8(RAM_PUZZLE_LAST_COLOR);
    ram8(RAM_PUZZLE_LAST_COLOR) = color;
    cpu.hl = RAM_PUZZLE_LAST_COLOR;
    if (previous == color) {
        cpu.f = z80_cp_flags(previous, color); /* still on the same colour */
        return;
    }
    uint8_t progress = ram8(RAM_PUZZLE_PROGRESS);
    cpu.de = progress;
    cpu.hl = (uint16_t)(PUZZLE_SOLUTION + progress);
    uint8_t expected = rd8(cpu.hl);
    if (expected == color) {
        cpu.f = z80_cp_flags(expected, color);
        cpu.hl = RAM_PUZZLE_PROGRESS;
        progress = alu_inc(ram8(RAM_PUZZLE_PROGRESS));
        ram8(RAM_PUZZLE_PROGRESS) = progress;
        ram8(v_soundControl) = SOUND_COINS;
        cpu.f = z80_cp_flags(progress, PUZZLE_STEPS);
        if (progress < PUZZLE_STEPS) return;
        cpu.iy = 0xC3A0; /* v_entities.6 */
        Entity *reward = entity_at(cpu.iy);
        reward->type = ENTITY_PUZZLE_REWARD;
        reward->data = 0x00;
        HI(reward->xPos) = 0x20;
        HI(reward->yPos) = 0x1F;
        return;
    }
    ram8(RAM_PUZZLE_PROGRESS) = 0;
    /* QUIRK: reads the fractional byte of v_horizontalScroll. */
    cpu.de = (uint16_t)(0x30 << 8 | (uint8_t)(ram8(v_horizontalScroll) + 0x30));
    cpu.c = ENTITY_GHOST;
    alex_call(f_spawnEntityAt);
}

LIFTED(sub_3D6B, 0x3D6B) {
    floor_puzzle_step(cpu.c);
    LIFTED_RETURN();
}

/* The five colours of the puzzle floor (tiles $0D-$10, $11-$14, $15-$18,
 * $19-$1C and $21-$24). */
LIFTED(_LABEL_3D60_, 0x3D59) {
    floor_puzzle_step(3);
    LIFTED_RETURN();
}
LIFTED(_LABEL_3D64_, 0x3D5D) {
    floor_puzzle_step(1);
    LIFTED_RETURN();
}
LIFTED(_LABEL_3D68_, 0x3D61) {
    floor_puzzle_step(4);
    LIFTED_RETURN();
}
LIFTED(_LABEL_3D6C_, 0x3D65) {
    floor_puzzle_step(2);
    LIFTED_RETURN();
}
LIFTED(_LABEL_3D70_, 0x3D69) {
    floor_puzzle_step(5);
    LIFTED_RETURN();
}

/* $3DB8 (_LABEL_3DBF_): ghost floor (tiles $1D-$20): while Alex stands on
 * it, a ghost appears under him every 128 frames (the first one at once). */
LIFTED(_LABEL_3DBF_, 0x3DB8) {
    cpu.hl = v_nextGhostSpawnTimer;
    uint8_t timer = alu_dec(ram8(v_nextGhostSpawnTimer));
    ram8(v_nextGhostSpawnTimer) = timer;
    if (!(timer & 0x80)) LIFTED_RETURN();
    ram8(v_nextGhostSpawnTimer) = GHOST_SPAWN_PERIOD;
    cpu.e = HI(entity_at(cpu.ix)->xPos);
    cpu.d = (uint8_t)(HI(ALEX->yPos) + 0x18);
    cpu.c = ENTITY_GHOST;
    alex_call(f_spawnEntityAt);
    LIFTED_RETURN();
}

/* -------------------------------------------- $60 event entity ($3E28) */
/* An invisible entity: when idle Alex touches it, Alex is held still
 * (ALEX_STATE_FROZEN) while two static sprites appear in slots 27 and 28
 * (30 then 60 frames); then Alex is released, name table change $89 is
 * requested and the entity removes itself and the sprites. */

#define EVENT_SPRITE_1 0x8000
#define EVENT_SPRITE_2 0x8A27
#define NULL_SPRITE 0x80E1

/* $3E39 (_LABEL_3E40_): state 0, init. */
LIFTED(_LABEL_3E40_, 0x3E39) {
    Entity *self = entity_at(cpu.ix);
    LO(self->isOffScreenFlags) = 0;
    self->spriteDescriptorPointer = NULL_SPRITE;
    self->state = alu_inc(self->state);
    LIFTED_RETURN();
}

/* $3E73: static sprite `descriptor` in slot IY, 24 px right of the entity,
 * at line y. */
static void show_event_sprite(uint16_t descriptor, uint8_t y) {
    Entity *sprite = entity_at(cpu.iy);
    cpu.hl = descriptor;
    cpu.c = y;
    sprite->type = ENTITY_STATIC_SPRITE;
    HI(sprite->xPos) = (uint8_t)(HI(entity_at(cpu.ix)->xPos) + 0x18);
    HI(sprite->yPos) = y;
    sprite->spriteDescriptorPointer = descriptor;
}

LIFTED(sub_3E73, 0x3E73) {
    show_event_sprite(cpu.hl, cpu.c);
    LIFTED_RETURN();
}

/* $3E49 (_LABEL_3E50_): state 1, wait for idle Alex to touch it. */
LIFTED(_LABEL_3E50_, 0x3E49) {
    Entity *self = entity_at(cpu.ix);
    if (ALEX->state != ALEX_STATE_IDLE) LIFTED_RETURN();
    if (HI(self->isOffScreenFlags) | LO(self->isOffScreenFlags)) LIFTED_RETURN();
    cpu.iy = v_alex;
    alex_call(f_checkEntityCollision);
    if (cpu.f & FLAG_C) LIFTED_RETURN();
    ALEX->state = ALEX_STATE_FROZEN;
    self->state = alu_inc(self->state);
    self->animationTimer = 0x1E;
    cpu.iy = 0xC640; /* v_entities.27 */
    show_event_sprite(EVENT_SPRITE_1, 0x77);
    LIFTED_RETURN();
}

/* $3E89 (_LABEL_3E90_): state 2, second sprite after 30 frames. */
LIFTED(_LABEL_3E90_, 0x3E89) {
    Entity *self = entity_at(cpu.ix);
    if (--self->animationTimer) LIFTED_RETURN();
    self->animationTimer = 0x3C;
    self->state = alu_inc(self->state);
    cpu.iy = 0xC660; /* v_entities.28 */
    show_event_sprite(EVENT_SPRITE_2, 0x87);
    LIFTED_RETURN();
}

/* $3E9F (_LABEL_3EA6_): state 3, end after 60 frames. */
LIFTED(_LABEL_3EA6_, 0x3E9F) {
    Entity *self = entity_at(cpu.ix);
    if (--self->animationTimer) LIFTED_RETURN();
    ALEX->state = ALEX_STATE_IDLE;
    ram8(v_nametableChangeRequest) = 0x89;
    cpu.hl = 0xC640;
    alex_call(f_clearEntity);
    cpu.hl++;
    alex_call(f_clearEntity);
    alex_call(f_destroyCurrentEntity);
    LIFTED_RETURN();
}
