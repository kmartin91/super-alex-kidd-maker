/*
 * Enemies that fly or swim ($4E29-$5247, $57C7-$58FF): merman and its
 * bubbles, bats, carnivorous plant, monster birds, small fishes, killer
 * fishes, sea horses.
 *
 * Common pattern: bit 0 of Entity.flags marks the first-frame setup done;
 * EF_DESTROY_OFFSCREEN lets the movement code remove the enemy once it leaves
 * the screen; each frame the enemy hurts Alex on contact
 * (tryToKillAlexIfColliding) and dies when Alex's attack touches it
 * (killEnemy: points, smoke puff). Speeds are 8.8 fixed point, in pixels per
 * frame ($FF80 = -0.5).
 */
#include "game/enemies1/enemies1.h"

/* Animation / sprite descriptors. */
#define MERMAN_ANIMATION 0x8211
#define MERMAN_BUBBLE_ANIMATION 0x825C
#define BAT_ANIMATION 0x8BBD
#define PLANT_SPRITE 0x856E
#define MONSTERBIRD_LEFT_ANIMATION 0x81B7
#define MONSTERBIRD_RIGHT_ANIMATION 0x81E4
#define SMALL_FISH_LEFT_ANIMATION 0x8BD2
#define SMALL_FISH_RIGHT_ANIMATION 0x8C4B
#define KILLER_FISH_LEFT_ANIMATION 0x83AB
#define KILLER_FISH_RIGHT_ANIMATION 0x83D8
#define SEA_HORSE_ANIMATION 0x8BF3

/* ROM tables (bank 1). */
#define MERMAN_BUBBLE_VELOCITIES 0x52C8 /* 8 x (ySpeed.low, xSpeed.high, xSpeed.low) */
#define LOW_SINE 0x5248                 /* 64 bytes: small vertical wave */
#define HIGH_SINE 0x5288                /* 64 bytes: large vertical wave */
#define WAVE_LENGTH 0x40

/* Negates both bytes of a speed separately (CPL/INC on each byte).
 * QUIRK: not a 16-bit negation when both bytes are non-zero
 * ($0080 gives $0080 back, $FFC0 gives $0040 as expected). */
static uint16_t negate_bytes(uint16_t speed) {
    uint8_t hi = (uint8_t)(-(speed >> 8)), lo = (uint8_t)(-(speed & 0xFF));
    return (uint16_t)(hi << 8 | lo);
}

/* Inverts a vertical speed: high byte complemented, low byte negated.
 * Correct for speeds whose low byte is not 0. */
static uint16_t reverse_speed(uint16_t speed) {
    uint8_t hi = (uint8_t)~(speed >> 8), lo = (uint8_t)(-(speed & 0xFF));
    return (uint16_t)(hi << 8 | lo);
}

/* ------------------------------------------------------------------ merman */

#define MERMAN_HIT_POINTS 3
#define MERMAN_SWIM_FRAMES 0xC0
#define MERMAN_RISE_SPEED 0xFFC0

/* $4E29: merman. Swims up and down (0.25 px/frame, direction reversed every
 * 192 frames) and blows bubbles when it starts rising again. Takes 3 hits
 * (one per attack). data: hits taken, battleDecision: frames in the current
 * direction. */
LIFTED(updateMerman, 0x4E29) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = 0x0A;
        e->animationTimerResetValue = 0x0A;
        if (!entity_on_screen(e)) ANIMATE_AND_RETURN(MERMAN_ANIMATION);
        e->flags |= EF_INITIALIZED;
        e->ySpeed = MERMAN_RISE_SPEED;
        e->flags |= EF_DESTROY_OFFSCREEN;
    }
    hurt_alex_on_contact();
    if (alex_attack_hits() && (alex()->unknown8 & ALEX_ATTACK_UNSPENT)) {
        alex()->unknown8 &= (uint8_t)~ALEX_ATTACK_UNSPENT;
        e->data++;
        if (e->data >= MERMAN_HIT_POINTS) TAIL_CALL(f_killEnemy);
    }
    e->battleDecision++;
    if (e->battleDecision == MERMAN_SWIM_FRAMES) {
        e->battleDecision = 0;
        e->ySpeed = reverse_speed(e->ySpeed);
        call_routine(f_spawnMermanBubbles);
    }
    ANIMATE_AND_RETURN(MERMAN_ANIMATION);
}

/* $4FA6: when the merman starts rising, fills every free projectile slot
 * (17-21) with a bubble; each bubble gets the next of 8 directions
 * (unknown6 counts the bubbles blown). Returns IY past the last slot. */
LIFTED(spawnMermanBubbles, 0x4FA6) {
    Entity *e = entity_at(cpu.ix);
    if (ENT_HI(e, ySpeed) != 0xFF) LIFTED_RETURN();
    uint16_t slot = PROJECTILE_SLOT;
    for (int i = 0; i < PROJECTILE_SLOT_COUNT; i++, slot += ENTITY_SIZE) {
        Entity *bubble = entity_at(slot);
        if (bubble->type != 0) continue;
        ram8(v_soundControl) = SOUND_MERMAN_BUBBLES;
        bubble->type = ENTITY_MERMAN_BUBBLE;
        ENT_Y(bubble) = ENT_Y(e);
        ENT_X(bubble) = ENT_X(e);
        ENT_SCREEN_X(bubble) = ENT_SCREEN_X(e);
        bubble->flags |= EF_DESTROY_OFFSCREEN;
        e->unknown6++;
        bubble->battleDecision = e->unknown6 & 7;
    }
    cpu.iy = slot;
    LIFTED_RETURN();
}

/* $4E96: merman bubble. Flies in one of 8 directions (battleDecision indexes
 * MERMAN_BUBBLE_VELOCITIES), hurts Alex, pops near the top of the screen. */
LIFTED(updateMermanBubbles, 0x4E96) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->unknown3 = 0x12;
        e->animationTimer = 8;
        e->animationTimerResetValue = 8;
        ENT_HI(e, ySpeed) = 0xFF;
        uint16_t velocity = (uint16_t)(MERMAN_BUBBLE_VELOCITIES + (uint8_t)(e->battleDecision * 3));
        ENT_LO(e, ySpeed) = rd8(velocity);
        ENT_HI(e, xSpeed) = rd8((uint16_t)(velocity + 1));
        ENT_LO(e, xSpeed) = rd8((uint16_t)(velocity + 2));
        e->flags |= EF_DESTROY_OFFSCREEN;
    }
    if (entity_on_screen(e)) {
        hurt_alex_on_contact();
        if (ENT_Y(e) < 0x18) DESTROY_AND_RETURN();
    }
    ANIMATE_AND_RETURN(MERMAN_BUBBLE_ANIMATION);
}

/* -------------------------------------------------------------------- bats */

#define BAT_SPEED 0x0080

/* Bat fields: unknown5 / battleDecision = base height (pixel / fraction, kept
 * in step with vertical scrolling), unknown6 = phase in the wave. */

/* $4EE8: bat flying left along a wave; turns right at a wall. */
LIFTED(updateBatLeft, 0x4EE8) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        if (!entity_on_screen(e)) TAIL_CALL(f__LABEL_4F7C_);
        e->flags |= EF_INITIALIZED;
        e->unknown5 = ENT_Y(e);
        e->animationTimer = 8;
        e->animationTimerResetValue = 8;
    }
    if (!entity_on_screen(e)) DESTROY_AND_RETURN();
    e->flags |= EF_DESTROY_OFFSCREEN;
    e->xSpeed = (uint16_t)-BAT_SPEED;
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
    if (tile_attr_at(0x01, 0x00) & TILE_SOLID) {
        e->type = ENTITY_BAT_RIGHT;
        e->xSpeed = BAT_SPEED;
    }
    TAIL_CALL(f_bat_LABEL_4F43_);
}

/* $4F7B: bat flying right; turns left at a wall. */
LIFTED(updateBatRight, 0x4F7B) {
    Entity *e = entity_at(cpu.ix);
    if (!entity_on_screen(e)) DESTROY_AND_RETURN();
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
    if (tile_attr_at(0x01, 0x10) & TILE_SOLID) {
        e->type = ENTITY_BAT_LEFT;
        e->xSpeed = (uint16_t)-BAT_SPEED;
    }
    TAIL_CALL(f_bat_LABEL_4F43_);
}

/* $4F3C: bat wave: Y = base height + LOW_SINE[phase]; removed when it goes
 * below the playfield. */
LIFTED(bat_LABEL_4F43_, 0x4F3C) {
    Entity *e = entity_at(cpu.ix);
    e->unknown6++;
    if (e->unknown6 == WAVE_LENGTH) e->unknown6 = 0;
    uint8_t wave = rd8((uint16_t)(LOW_SINE + e->unknown6));
    uint16_t base = (uint16_t)(ENT_WORD(e->battleDecision, e->unknown5) +
                               negate_bytes(ram16(v_verticalScrollSpeed)));
    e->unknown5 = (uint8_t)(base >> 8);
    e->battleDecision = (uint8_t)base;
    uint8_t y = (uint8_t)(wave + e->unknown5);
    if (y >= 0xC0) DESTROY_AND_RETURN();
    ENT_Y(e) = y;
    TAIL_CALL(f__LABEL_4F7C_);
}

/* $4F75: bat animation. */
LIFTED(_LABEL_4F7C_, 0x4F75) { ANIMATE_AND_RETURN(BAT_ANIMATION); }

/* ------------------------------------------------------------------- plant */

#define PLANT_SPEED 0xFF80
#define PLANT_BOB_FRAMES 0x40

/* $4FEA: carnivorous plant. Moves up and down (0.5 px/frame, direction
 * reversed every 64 frames) and hurts Alex; it cannot be killed. */
LIFTED(updatePlant, 0x4FEA) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->spriteDescriptorPointer = PLANT_SPRITE;
        if (!entity_on_screen(e)) LIFTED_RETURN();
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
        e->ySpeed = PLANT_SPEED;
        LIFTED_RETURN();
    }
    hurt_alex_on_contact();
    e->battleDecision++;
    if (e->battleDecision < PLANT_BOB_FRAMES) LIFTED_RETURN();
    e->ySpeed = reverse_speed(e->ySpeed);
    e->battleDecision = 0;
    LIFTED_RETURN();
}

/* --------------------------------------------------- monster birds, small fish */

#define MONSTERBIRD_SPEED 0x0080
#define SMALL_FISH_SPEED_LEFT 0xFFA0
#define SMALL_FISH_SPEED_RIGHT 0x0060
#define WALL_PROBE_HEIGHT 8

/* First-frame setup shared by birds and small fish. */
static void init_flyer(Entity *e) {
    e->flags |= EF_INITIALIZED;
    e->unknown3 = 0x04;
    e->animationTimer = 0x10;
    e->animationTimerResetValue = 0x10;
}

/* $5030: monster bird flying left; turns at walls. */
LIFTED(updateMonsterbirdLeft, 0x5030) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        init_flyer(e);
    } else if (entity_on_screen(e)) {
        e->xSpeed = (uint16_t)-MONSTERBIRD_SPEED;
        e->flags |= EF_DESTROY_OFFSCREEN;
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x00, WALL_PROBE_HEIGHT)) {
            e->type = ENTITY_MONSTERBIRD_RIGHT;
            e->xSpeed = MONSTERBIRD_SPEED;
        }
    }
    ANIMATE_AND_RETURN(MONSTERBIRD_LEFT_ANIMATION);
}

/* $5081: monster bird flying right; turns at walls. */
LIFTED(updateMonsterbirdRight, 0x5081) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        init_flyer(e);
    } else if (entity_on_screen(e)) {
        e->xSpeed = MONSTERBIRD_SPEED;
        e->flags |= EF_DESTROY_OFFSCREEN;
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x18, WALL_PROBE_HEIGHT)) {
            e->type = ENTITY_MONSTERBIRD_LEFT;
            e->xSpeed = (uint16_t)-MONSTERBIRD_SPEED;
        }
    }
    ANIMATE_AND_RETURN(MONSTERBIRD_RIGHT_ANIMATION);
}

/* $50DA: small fish swimming left; turns at walls. */
LIFTED(updateSmallFishLeft, 0x50DA) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        init_flyer(e);
    } else if (entity_on_screen(e)) {
        e->xSpeed = SMALL_FISH_SPEED_LEFT;
        e->flags |= EF_DESTROY_OFFSCREEN;
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x00, WALL_PROBE_HEIGHT)) {
            e->type = ENTITY_SMALL_FISH_RIGHT;
            e->xSpeed = SMALL_FISH_SPEED_RIGHT;
        }
    }
    ANIMATE_AND_RETURN(SMALL_FISH_LEFT_ANIMATION);
}

/* $512B: small fish swimming right (only ever converted from the left one,
 * which keeps its speed set); turns at walls. */
LIFTED(updateSmallFishRight, 0x512B) {
    Entity *e = entity_at(cpu.ix);
    if (entity_on_screen(e)) {
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x10, WALL_PROBE_HEIGHT)) {
            e->type = ENTITY_SMALL_FISH_LEFT;
            e->xSpeed = SMALL_FISH_SPEED_LEFT;
        }
    }
    ANIMATE_AND_RETURN(SMALL_FISH_RIGHT_ANIMATION);
}

/* ------------------------------------------------------------- killer fish */

#define KILLER_FISH_SPEED_LEFT 0xFFA0
#define KILLER_FISH_SPEED_RIGHT 0x0060

/* Killer fish wave: Y = base + HIGH_SINE[phase]. Fields as the bat's.
 * QUIRK: the vertical scroll is applied twice (to the base and again to Y). */
static void killer_fish_wave(Entity *e) {
    e->unknown6++;
    if (e->unknown6 == WAVE_LENGTH) e->unknown6 = 0;
    uint8_t wave = rd8((uint16_t)(HIGH_SINE + e->unknown6));
    uint16_t scroll = negate_bytes(ram16(v_verticalScrollSpeed));
    uint16_t base = (uint16_t)(ENT_WORD(e->battleDecision, e->unknown5) + scroll);
    e->unknown5 = (uint8_t)(base >> 8);
    e->battleDecision = (uint8_t)base;
    ENT_Y(e) = (uint8_t)(wave + (scroll >> 8) + e->unknown5);
}

/* $5158: killer fish swimming left along a large wave; turns at walls. */
LIFTED(updateKillerFishLeft, 0x5158) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = 0x10;
        e->animationTimerResetValue = 0x10;
        if (entity_on_screen(e)) {
            e->flags |= EF_INITIALIZED;
            e->unknown5 = ENT_Y(e);
        }
        ANIMATE_AND_RETURN(KILLER_FISH_LEFT_ANIMATION);
    }
    if (entity_on_screen(e)) {
        e->xSpeed = KILLER_FISH_SPEED_LEFT;
        e->flags |= EF_DESTROY_OFFSCREEN;
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x00, WALL_PROBE_HEIGHT)) {
            e->xSpeed = KILLER_FISH_SPEED_RIGHT;
            e->type = ENTITY_KILLER_FISH_RIGHT;
        } else {
            killer_fish_wave(e);
        }
    }
    ANIMATE_AND_RETURN(KILLER_FISH_LEFT_ANIMATION);
}

/* $51EC: killer fish swimming right; turns at walls (QUIRK: without changing
 * its speed, the left updater resets it next frame). */
LIFTED(updateKillerFishRight, 0x51EC) {
    Entity *e = entity_at(cpu.ix);
    if (entity_on_screen(e)) {
        hurt_alex_on_contact();
        if (alex_attack_hits()) TAIL_CALL(f_killEnemy);
        if (terrain_at(0x01, 0x18, WALL_PROBE_HEIGHT))
            e->type = ENTITY_KILLER_FISH_LEFT;
        else
            killer_fish_wave(e);
    }
    ANIMATE_AND_RETURN(KILLER_FISH_RIGHT_ANIMATION);
}

/* --------------------------------------------------------------- sea horses */

#define SEA_HORSE_SINK_FRAMES 0x30
#define SEA_HORSE_LAPS 2   /* hops before turning around */
#define SEA_HORSE_SPEED 0x0080

/* Sea horse fields: data = base Y of the hop, unknown6 = phase in the hop
 * (0 / $FF while sinking or rising), battleDecision = frames sinking or
 * rising, unknown5 = hops made in the current direction. */

/* $57C7: sea horse facing left: sinks for 48 frames, then hops to the left
 * along LOW_SINE; after two hops it turns around. */
LIFTED(updateSeaHorseLeft, 0x57C7) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->animationTimer = 0x10;
        e->animationTimerResetValue = 0x10;
        if (!entity_on_screen(e)) ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
        e->flags |= EF_INITIALIZED | EF_DESTROY_OFFSCREEN;
    }
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);

    if (e->unknown6 == 0) {
        e->ySpeed = SEA_HORSE_SPEED; /* sinking */
        e->battleDecision++;
        if (e->battleDecision < SEA_HORSE_SINK_FRAMES) ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
        e->unknown5++;
        if (e->unknown5 == SEA_HORSE_LAPS) {
            e->type = ENTITY_SEA_HORSE_RIGHT;
            e->ySpeed = (uint16_t)-SEA_HORSE_SPEED;
            e->unknown5 = 0;
            e->battleDecision = 0;
            e->unknown6 = 0xFF;
            ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
        }
        e->data = ENT_Y(e); /* start a hop */
        e->ySpeed = 0;
        e->xSpeed = (uint16_t)-SEA_HORSE_SPEED;
    }

    e->unknown6++;
    if (e->unknown6 == WAVE_LENGTH) { /* hop finished: sink again */
        e->xSpeed = 0;
        e->ySpeed = SEA_HORSE_SPEED;
        e->battleDecision = 0;
        e->unknown6 = 0;
        ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
    }
    ENT_Y(e) = (uint8_t)(rd8((uint16_t)(LOW_SINE + e->unknown6)) + e->data);
    ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
}

/* $587C: sea horse facing right: rises for 48 frames, then hops to the right
 * (LOW_SINE read backwards); after two hops it turns around.
 * QUIRK: the hop base (data) is the one saved by the left-facing phase. */
LIFTED(updateSeaHorseRight, 0x587C) {
    Entity *e = entity_at(cpu.ix);
    hurt_alex_on_contact();
    if (alex_attack_hits()) TAIL_CALL(f_killEnemy);

    if (e->unknown6 == 0xFF) {
        e->battleDecision++;
        if (e->battleDecision < SEA_HORSE_SINK_FRAMES) ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
        e->unknown5++;
        if (e->unknown5 == SEA_HORSE_LAPS) {
            e->type = ENTITY_SEA_HORSE_LEFT;
            e->unknown5 = 0;
            e->battleDecision = 0;
            e->unknown6 = 0;
            ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
        }
        e->unknown6 = WAVE_LENGTH;
        e->ySpeed = 0;
        e->xSpeed = SEA_HORSE_SPEED;
    }

    e->unknown6--;
    if (e->unknown6 == 0) { /* hop finished: rise again */
        e->xSpeed = 0;
        e->ySpeed = (uint16_t)-SEA_HORSE_SPEED;
        e->battleDecision = 0;
        e->unknown6 = 0xFF;
        ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
    }
    ENT_Y(e) = (uint8_t)(rd8((uint16_t)(LOW_SINE + e->unknown6)) + e->data);
    ANIMATE_AND_RETURN(SEA_HORSE_ANIMATION);
}
