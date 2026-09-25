/*
 * Button 2: punching, breaking blocks, capsules ($44DB, $4501-$4688).
 *
 * The action depends on v_alexActionState (the item or vehicle in use):
 *   0, 1, 2, 6 (and 10-13) -> punch
 *   3 / 4                  -> throw a magic capsule (entity $05 / $07, slot 4)
 *   5                      -> power bracelet shock wave ($48BE, other module)
 *   7                      -> nothing (motorcycle)
 *   8 / 9                  -> shoot (boat, peticopter: $444C, other module)
 *
 * A punch shows the punch sprite for ALEX_PUNCH_TIME frames (timer in
 * Entity.unknown7) and, if the fist (12 px down, 7 px left of Alex's corner
 * or 23 px right of it) is on a breakable block (attribute & $C0 == $C0), the
 * block breaks: debris is spawned, the block is replaced by background (or
 * water) and a block with contents (attribute bit 5) releases them:
 *   tiles 1-4  skull box: Alex is stunned for 60 frames (unless protected)
 *   tiles 5-8  question box: next item of the level's question box list
 *   tiles 9-12 money bag
 */
#include "alex.h"

#define ALEX_PUNCH_TIME 0x0A
#define STUN_TIME 0x3C
#define FIST_OFFSET_LEFT 0x0CF9   /* dy = 12, dx = -7 */
#define FIST_OFFSET_RIGHT 0x0C17  /* dy = 12, dx = 23 */
#define BLOCK_METATILE_BANK 0x85
#define METATILE_BACKGROUND 0x8503
#define METATILE_WATER 0x850B
#define NAMETABLE_CHANGE_BLOCK 0x80
#define STUN_IMMUNITY 0x4600      /* bank 1: per action state, 1 = not stunned */
#define QUESTION_BOX_ITEMS 0x0DD7 /* bank 0: entity types, by v_questionMarkBoxIndex */
#define CAPSULE_SLOT 0xC360       /* v_entities.4 */
#define CAPSULE_A_SPRITE 0x80E6
#define CAPSULE_B_SPRITE 0x80F4

/* Debris kinds of requestBlockSound, by block tile index. */
#define DEBRIS_ROCK 1
#define DEBRIS_STAR 2
#define DEBRIS_OTHER 3

/* $4627 tickPunch: count the punch pose down. Returns true (Z flag set) when
 * no punch pose is showing any more: at the end of the timer (the punch flags
 * are cleared), or at once for a thrown capsule. Leaves HL = &unknown8. */
bool alex_tick_punch(void) {
    Entity *alex = ALEX;
    bool done;
    cpu.hl = v_alex + 0x1C;
    if (alex->unknown8 & ACTFLAG_THROWING) {
        done = true;
    } else {
        done = --alex->unknown7 == 0;
        if (done) alex->unknown8 &= (uint8_t)~(ACTFLAG_PUNCHING | ACTFLAG_ATTACK_BOX);
    }
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (done ? FLAG_Z : 0));
    return done;
}

LIFTED(tickPunch, 0x4627) {
    alex_tick_punch();
    LIFTED_RETURN();
}

/* $45B7 requestNametableChangeBackground: replace the block at
 * v_nametableChangeDestination with background (or water while swimming). */
void alex_request_background_tile(void) {
    uint16_t metatile = ALEX->state == ALEX_STATE_SWIMMING ? METATILE_WATER : METATILE_BACKGROUND;
    ram8(RAM_METATILE_BANK) = BLOCK_METATILE_BANK;
    ram16(nametableChangeSourceMetatile) = metatile;
    ram8(v_nametableChangeRequest) = NAMETABLE_CHANGE_BLOCK;
    cpu.hl = metatile;
    cpu.f = z80_cp_flags(ALEX->state, ALEX_STATE_SWIMMING);
}

LIFTED(requestNametableChangeBackground, 0x45B7) {
    alex_request_background_tile();
    LIFTED_RETURN();
}

/* $45EA (_LABEL_45F1_): skull box: stun the entity in IX (Alex, or the
 * projectile that broke the box: QUIRK, then the projectile is "stunned"),
 * unless the current action state protects Alex. */
static void open_skull_box(void) {
    if (rd8((uint16_t)(STUN_IMMUNITY + ram8(v_alexActionState)))) return;
    Entity *self = entity_at(cpu.ix);
    self->unknown8 |= ACTFLAG_STUNNED;
    self->unknown6 = STUN_TIME;
}

LIFTED(_LABEL_45F1_, 0x45EA) {
    open_skull_box();
    LIFTED_RETURN();
}

/* $460E (_LABEL_4615_): question box at (B = y, C = x): spawn the next item. */
static void open_question_box(uint16_t yx) {
    cpu.de = yx;
    uint8_t index = ram8(v_questionMarkBoxIndex);
    ram8(v_questionMarkBoxIndex) = alu_inc(index);
    cpu.hl = (uint16_t)(QUESTION_BOX_ITEMS + index);
    cpu.bc = rd8(cpu.hl);
    alex_call(f_spawnEntityAt);
}

LIFTED(_LABEL_4615_, 0x460E) {
    open_question_box(cpu.bc);
    LIFTED_RETURN();
}

/* $4620 spawnMoneyBagAt (B = y, C = x) */
static void spawn_money_bag(uint16_t yx) {
    cpu.de = yx;
    cpu.c = ENTITY_MONEY_BAG;
    alex_call(f_spawnEntityAt);
}

LIFTED(spawnMoneyBagAt, 0x4620) {
    spawn_money_bag(cpu.bc);
    LIFTED_RETURN();
}

/* $4571 (_LABEL_4578_): break the block whose name table attribute is at
 * attr_pointer; screen_yx = the probe's screen position (B = y, C = x).
 * Leaves the block's tile index in D' (EXX), as the original. */
void alex_break_block(uint16_t attr_pointer, uint16_t screen_yx) {
    uint16_t block = (uint16_t)(attr_pointer & ~0x0043); /* top-left entry of the 16x16 block */
    ram16(v_nametableChangeDestination) = block;
    uint8_t tile = rd8(block);
    cpu.de_ = (uint16_t)(tile << 8 | (uint8_t)cpu.de_);
    uint8_t attr = rd8((uint16_t)(block + 1));
    uint8_t debris = tile < 0x0D ? DEBRIS_ROCK : tile < 0x7C ? DEBRIS_STAR : DEBRIS_OTHER;
    /* Debris position: the block's corner in level x / screen y. */
    uint8_t x = (uint8_t)(((uint8_t)screen_yx & 0xF0) + ram8(v_horizontalScroll + 1));
    uint8_t row = (uint8_t)((screen_yx >> 8) & 0xF0);
    uint8_t y = (uint8_t)(row - ram8(v_verticalScroll + 1));
    if (row < ram8(v_verticalScroll + 1)) y = (uint8_t)(y - 0x20); /* 224-line name table */
    if (y >= 0xE0) y = (uint8_t)(y - 0x20);
    cpu.de = (uint16_t)(y << 8 | x);
    cpu.b = attr;
    cpu.c = debris;
    cpu.a = debris;
    alex_call(f_requestBlockSound);
    if (attr & TILE_VARIANT) {
        /* The block has contents: tile 1-12 (table at $45D0). */
        cpu.bc = cpu.de;
        switch (tile) {
        case 1: case 2: case 3: case 4: open_skull_box(); break;
        case 5: case 6: case 7: case 8: open_question_box(cpu.bc); break;
        case 9: case 10: case 11: case 12: spawn_money_bag(cpu.bc); break;
        default: /* not in the game data: dispatch as the original */
            cpu.a = tile;
            cpu.hl = 0x45D0;
            alex_call(f_jumpToAthPointer);
            break;
        }
    }
    alex_request_background_tile();
}

LIFTED(_LABEL_4578_, 0x4571) {
    alex_break_block(cpu.hl, cpu.bc);
    LIFTED_RETURN();
}

/* $4557 (_LABEL_455E_): show the attack sprite and hit the tile at the fist
 * (dy = fist >> 8, dx = fist & $FF from Alex's corner). */
static void strike(uint16_t sprite, uint16_t fist) {
    ram8(v_soundControl) = SOUND_PUNCH;
    alex_set_sprite(sprite);
    uint8_t x = (uint8_t)(HI(ALEX->xPos) + (uint8_t)fist);
    if (x >= 0xF8 || x < 0x0A) return; /* fist outside the screen */
    cpu.a = x;
    cpu.d = (uint8_t)(fist >> 8);
    alex_call(f__LABEL_7C4F_);
    if ((cpu.a & TILE_CLASS_BREAKABLE) != TILE_CLASS_BREAKABLE) return;
    alex_break_block(cpu.hl, cpu.bc);
}

LIFTED(_LABEL_455E_, 0x4557) {
    strike(cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* $4538 punch */
static void punch(void) {
    Entity *alex = ALEX;
    alex->unknown8 |= ACTFLAG_PUNCHING | ACTFLAG_ATTACK_BOX;
    alex->unknown7 = ALEX_PUNCH_TIME;
    if (alex->unknown3 & MOTION_FACING_RIGHT) strike(SPR_PUNCH_RIGHT, FIST_OFFSET_RIGHT);
    else strike(SPR_PUNCH_LEFT, FIST_OFFSET_LEFT);
}

LIFTED(punch, 0x4538) {
    punch();
    LIFTED_RETURN();
}

/* $4652: launch the capsule entity already placed in slot 4, with sprite
 * `sprite`: it flies 1 px/frame forward and starts with an upward speed of
 * 2 px/frame. Alex's attack lasts as long as the capsule. */
static void launch_capsule(uint16_t sprite) {
    Entity *alex = ALEX;
    Entity *capsule = entity_at(CAPSULE_SLOT);
    ram8(v_soundControl) = SOUND_THROW;
    capsule->spriteDescriptorPointer = sprite;
    alex->unknown8 |= ACTFLAG_PUNCHING | ACTFLAG_THROWING | ACTFLAG_ATTACK_BOX;
    HI(capsule->xPos) = HI(alex->xPos);
    HI(capsule->yPos) = (uint8_t)(HI(alex->yPos) + 4);
    capsule->unknown3 = alex->unknown3 & MOTION_RIGHT;
    capsule->xSpeed = capsule->unknown3 ? 0x0100 : 0xFF00;
    capsule->ySpeed = 0xFE00;
}

LIFTED(sub_4652, 0x4652) {
    launch_capsule(cpu.hl);
    LIFTED_RETURN();
}

/* $463A / $4647: throw magic capsule A or B (entity `type` in slot 4). */
static void throw_capsule(uint8_t type, uint16_t sprite) {
    cpu.iy = CAPSULE_SLOT;
    entity_at(CAPSULE_SLOT)->type = type;
    launch_capsule(sprite);
}

LIFTED(_LABEL_4641_, 0x463A) {
    throw_capsule(ENTITY_CAPSULE_A, CAPSULE_A_SPRITE);
    LIFTED_RETURN();
}

LIFTED(_LABEL_464E_, 0x4647) {
    throw_capsule(ENTITY_CAPSULE_B, CAPSULE_B_SPRITE);
    LIFTED_RETURN();
}

/* $4688 (_LABEL_468F_): no action (motorcycle). */
LIFTED(_LABEL_468F_, 0x4688) {
    LIFTED_RETURN();
}

/* $4501 handleAction: button 2 was pressed. */
void alex_handle_action(void) {
    Entity *alex = ALEX;
    if (alex->unknown8 & ACTFLAG_PUNCHING) return;
    if (HI(alex->isOffScreenFlags)) return;
    if ((ram8(v_gameState) & 0x0F) == 0x05) return; /* not in shops */
    uint8_t action = ram8(v_alexActionState);
    switch (action) {
    case ACTION_NONE:
    case ACTION_CANE_OF_FLIGHT:
    case ACTION_INVINCIBLE:
    case 6:
    case 10: case 11: case 12: case 13:
        punch();
        break;
    case ACTION_CAPSULE_A: throw_capsule(ENTITY_CAPSULE_A, CAPSULE_A_SPRITE); break;
    case ACTION_CAPSULE_B: throw_capsule(ENTITY_CAPSULE_B, CAPSULE_B_SPRITE); break;
    case ACTION_POWER_BRACELET: alex_call(f__LABEL_48C5_); break;
    case ACTION_MOTORCYCLE: break;
    case ACTION_BOAT:
    case ACTION_PETICOPTER: alex_call(f__LABEL_4453_); break;
    default: /* outside the table: jump as the original */
        cpu.a = action;
        cpu.hl = 0x451C;
        alex_call(f_jumpToAthPointer);
        break;
    }
}

LIFTED(handleAction, 0x4501) {
    alex_handle_action();
    LIFTED_RETURN();
}
