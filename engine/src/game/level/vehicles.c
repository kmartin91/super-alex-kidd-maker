/*
 * Alex helpers that sit between the name-table request handlers in the ROM
 * ($43EB-$44FF): leaving a wrecked vehicle, firing from the boat or the
 * Peticopter, punching while swimming. IX = Alex's entity (v_alex).
 */
#include "level.h"
#include "rt/maker.h"

#define SOUND_MAIN_SONG 0x82
#define SOUND_BIKE_SONG_ 0x85
#define SOUND_PETICOPTER_SONG_ 0x88
#define SOUND_BULLET 0xA8
#define ALEX_IDLE_RIGHT_SPRITE 0x90BC

/* Alex's entity fields used here (see game/ram.h Entity). */
#define ALEX_STATE_JUMPING_FROM_WRECK 0x03      /* ix+$1A */
#define ALEX_ACTION_BUSY 0x09                   /* v_alex.unknown8: punch in progress */
#define FACING_RIGHT 0x01                       /* unknown3 bit 0 */

/* The explosion of the vehicle (entity type 3) goes to slot 3 (v_entities.4). */
#define EXPLOSION_SLOT 0xC360
#define ENTITY_VEHICLE_EXPLOSION 0x03

/* The vehicle's bullet (entity type 2) is slot 1 (v_entities.2). */
#define BULLET_SLOT 0xC320
#define ENTITY_BULLET 0x02
#define BULLET_SPRITE 0x84E8
#define BULLET_SPEED 0x0400
#define BULLET_LIFETIME 0x14

/* Swimming punch: sprite descriptors and punch offsets for _LABEL_455E_. */
#define SWIM_PUNCH_LEFT_SPRITE 0x8E49
#define SWIM_PUNCH_LEFT_OFFSETS 0x0AF9
#define SWIM_PUNCH_RIGHT_SPRITE 0x8E5E
#define SWIM_PUNCH_RIGHT_OFFSETS 0x0A1F
#define SWIM_PUNCH_TIME 0x0A

/* $440E (_LABEL_4415_): Alex is back on foot (action state 0) and the vehicle
 * explodes at his position + (8, 16). out: IY = the explosion slot. */
LIFTED(_LABEL_4415_, 0x440E) {
    Entity *alex = entity_at(cpu.ix);
    alex->unknown11 = 0x18;
    alex->unknown9 = 0x08;
    ram8(v_alexActionState) = 0;
    cpu.iy = EXPLOSION_SLOT;
    Entity *explosion = entity_at(EXPLOSION_SLOT);
    entity_byte(EXPLOSION_SLOT, ENTITY_X) = (uint8_t)(entity_byte(v_alex, ENTITY_X) + 0x08);
    entity_byte(EXPLOSION_SLOT, ENTITY_Y) = (uint8_t)(entity_byte(v_alex, ENTITY_Y) + 0x10);
    explosion->type = ENTITY_VEHICLE_EXPLOSION;
    explosion->unknown7 = 0x14;
    explosion->animationTimer = 0x04;
    explosion->animationTimerResetValue = 0x04;
    LIFTED_RETURN();
}

/* $43EB (_LABEL_43F2_): the vehicle is wrecked: the main song resumes and
 * Alex jumps out (y speed -2) facing right, then as $440E. out: IY. */
LIFTED(_LABEL_43F2_, 0x43EB) {
    ram8(v_soundControl) = SOUND_MAIN_SONG;
    /* Maker levels (rt/maker.h): the level's own song ($0DC5, levelSongs),
     * unless it is a vehicle's (states.h maker_foot_song). */
    if (maker.active) {
        uint8_t song = rd8((uint16_t)(0x0DC5 - 1 + ram8(v_level)));
        if (song != SOUND_BIKE_SONG_ && song != SOUND_PETICOPTER_SONG_) ram8(v_soundControl) = song;
    }
    Entity *alex = entity_at(cpu.ix);
    alex->unknown8 = 0x04;
    alex->state = ALEX_STATE_JUMPING_FROM_WRECK;
    alex->unknown3 = 0x03;
    entity_at(v_alex)->ySpeed = 0xFE00;
    entity_at(v_alex)->xSpeed = 0x0000;
    cpu.hl = ALEX_IDLE_RIGHT_SPRITE;
    CALL_ROUTINE(f_loadAlexSpriteDescriptor);
    TAIL_CALL(f__LABEL_4415_);
}

/* $444C (_LABEL_4453_): boat / Peticopter fire (action states 8, 9): a
 * bullet at Alex's height + 16, moving 4 px/frame in his direction. Facing
 * right, it starts 16 px ahead; none if that is off the right edge. */
LIFTED(_LABEL_4453_, 0x444C) {
    ram8(v_soundControl) = SOUND_BULLET;
    entity_at(v_alex)->unknown8 |= ALEX_ACTION_BUSY;
    entity_byte(BULLET_SLOT, ENTITY_Y) = (uint8_t)(entity_byte(v_alex, ENTITY_Y) + 0x10);
    uint8_t x = entity_byte(v_alex, ENTITY_X);
    uint16_t speed = (uint16_t)-BULLET_SPEED;
    if (entity_at(cpu.ix)->unknown3 & FACING_RIGHT) {
        if (x + 0x10 > 0xFF) LIFTED_RETURN();
        x = (uint8_t)(x + 0x10);
        speed = BULLET_SPEED;
    }
    entity_byte(BULLET_SLOT, ENTITY_X) = x;
    Entity *bullet = entity_at(BULLET_SLOT);
    bullet->xSpeed = speed;
    bullet->type = ENTITY_BULLET;
    bullet->spriteDescriptorPointer = BULLET_SPRITE;
    bullet->unknown7 = BULLET_LIFETIME;
    LIFTED_RETURN();
}

/* $44DB (_LABEL_44E2_): punch while swimming (unless one is in progress):
 * continues in the common punch code _LABEL_455E_ with the swimming sprites.
 * out: HL, IY as _LABEL_455E_ leaves them. */
LIFTED(_LABEL_44E2_, 0x44DB) {
    Entity *alex = entity_at(cpu.ix);
    if (alex->unknown8 & 0x01) LIFTED_RETURN();
    entity_at(v_alex)->unknown8 |= ALEX_ACTION_BUSY;
    alex->unknown7 = SWIM_PUNCH_TIME;
    if (!(alex->unknown3 & FACING_RIGHT)) {
        cpu.hl = SWIM_PUNCH_LEFT_SPRITE;
        cpu.de = SWIM_PUNCH_LEFT_OFFSETS;
    } else {
        cpu.hl = SWIM_PUNCH_RIGHT_SPRITE;
        cpu.de = SWIM_PUNCH_RIGHT_OFFSETS;
    }
    TAIL_CALL(f__LABEL_455E_);
}
