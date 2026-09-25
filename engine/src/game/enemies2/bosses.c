/*
 * Janken opponents and their boss forms ($778F-$7C43).
 *
 * Gooseka the Slippery ($1D), Chokkinna the Scissors ($1E) and Parplin the
 * Paper ($1F) sit in entity slot 6 and run the janken state machine
 * (updateBattle*, game states module) through a jump table indexed by their
 * state. When Alex wins a match against the second encounter of each
 * (data bit 0 set), the opponent's head flies off (slot 7) and must be
 * punched three times (Entity.unknown1 counts the hits):
 *  - Gooseka's head ($0D) bobs up and down while sweeping left and right;
 *  - Chokkinna's head ($0E) bounces in arcs across the top of the screen
 *    while the body casts spells ($1A, slot 8) at Alex;
 *  - Parplin's head ($0F) follows a fixed loop around the screen.
 * A punched Gooseka/Chokkinna head is stunned for 60 frames (state 3);
 * a punched Parplin head is invulnerable for 30 frames (stateTimer).
 */
#include "game/enemies2/enemies2.h"

#define GOOSEKA_UPDATERS 0x7797
#define CHOKKINNA_UPDATERS 0x7817
#define PARPLIN_UPDATERS 0x78A9
#define GOOSEKA_HEAD_STATES 0x79A2
#define CHOKKINNA_HEAD_STATES 0x7A91
#define PARPLIN_HEAD_PATH_STATES 0x7B5D

/* Sprite descriptors (bank 2) of the boss bodies and heads. */
#define BOSS_BODY_TRANSFORMING_SPRITE 0x936D  /* Gooseka, Parplin */
#define BOSS_BODY_HEADLESS_SPRITE 0x9395
#define BOSS_HEAD_SPRITE 0x9387
#define CHOKKINNA_TRANSFORMING_SPRITE 0x9458
#define CHOKKINNA_HEADLESS_SPRITE 0x9480
#define CHOKKINNA_HEAD_SPRITE 0x9472
#define CHOKKINNA_SPELL_SPRITE 0x974B

#define HEAD_HITS_TO_DEFEAT 3
#define HEAD_STUN_STATE 3
#define HEAD_STUN_TIME 0x3C
#define PARPLIN_HEAD_INVULNERABLE_TIME 0x1E
#define GOOSEKA_HEAD_RISE 0x20          /* bob apex: head height above the body */

/* Head movement flag in Entity.unknown3. */
#define HEAD_MOVING_RIGHT 0x02

/* Dispatch on the opponent's battle state: jump table entry `state`. */
#define DISPATCH_STATE(table, state)                         \
    do {                                                     \
        if (call_jump_table((table), (state))) return;       \
        LIFTED_RETURN();                                     \
    } while (0)

/* $778F: Gooseka (states $0-$0F, see the updaters table at $7797). */
LIFTED(updateGooseka, 0x778F) {
    DISPATCH_STATE(GOOSEKA_UPDATERS, entity_at(SLOT_OPPONENT)->state);
}

/* $780F: Chokkinna (states $0-$0E, table at $7817). */
LIFTED(updateChokkinna, 0x780F) {
    DISPATCH_STATE(CHOKKINNA_UPDATERS, entity_at(SLOT_OPPONENT)->state);
}

/* $78A1: Parplin (states $0-$0E, table at $78A9). */
LIFTED(updateParplin, 0x78A1) {
    DISPATCH_STATE(PARPLIN_UPDATERS, entity_at(SLOT_OPPONENT)->state);
}

/* Spawn the boss head in slot 7 at the body's position. */
static Entity *spawn_head(uint8_t type, uint16_t sprite) {
    const Entity *body = entity_at(SLOT_OPPONENT);
    Entity *head = entity_at(SLOT_BOSS_HEAD);
    head->type = type;
    set_x_pixel(head, x_pixel(body));
    set_y_pixel(head, y_pixel(body));
    head->spriteDescriptorPointer = sprite;
    cpu.iy = SLOT_BOSS_HEAD;
    return head;
}

/* $77C6: Gooseka transforms for unknown5 frames, then his head comes off;
 * it bobs up to GOOSEKA_HEAD_RISE px above the body (kept in battleDecision). */
LIFTED(updateGoosekaSpawnHead, 0x77C6) {
    Entity *e = entity_at(cpu.ix);
    Entity *body = entity_at(SLOT_OPPONENT);
    body->spriteDescriptorPointer = BOSS_BODY_TRANSFORMING_SPRITE;
    if (--e->unknown5 != 0) LIFTED_RETURN();
    body->spriteDescriptorPointer = BOSS_BODY_HEADLESS_SPRITE;
    cpu.hl = SLOT_BOSS_HEAD;
    CALL_ROUTINE(f_clearEntity);
    Entity *head = spawn_head(ENTITY_GOOSEKA_HEAD, BOSS_HEAD_SPRITE);
    head->battleDecision = (uint8_t)(y_pixel(body) - GOOSEKA_HEAD_RISE);
    e->state++;
    ram8(v_soundControl) = SOUND_BOSS_HEAD;
    LIFTED_RETURN();
}

/* $7835: Chokkinna transforms, then her head comes off. */
LIFTED(updateChokkinnaSpawnHead, 0x7835) {
    Entity *e = entity_at(cpu.ix);
    Entity *body = entity_at(SLOT_OPPONENT);
    body->spriteDescriptorPointer = CHOKKINNA_TRANSFORMING_SPRITE;
    if (--e->unknown5 != 0) LIFTED_RETURN();
    body->spriteDescriptorPointer = CHOKKINNA_HEADLESS_SPRITE;
    spawn_head(ENTITY_CHOKKINNA_HEAD, CHOKKINNA_HEAD_SPRITE);
    e->state++;
    ram8(v_soundControl) = SOUND_BOSS_HEAD;
    LIFTED_RETURN();
}

/* $78EA: Parplin transforms, then his head comes off. */
LIFTED(updateParplinSpawnHead, 0x78EA) {
    Entity *e = entity_at(cpu.ix);
    Entity *body = entity_at(SLOT_OPPONENT);
    body->spriteDescriptorPointer = BOSS_BODY_TRANSFORMING_SPRITE;
    if (--e->unknown5 != 0) LIFTED_RETURN();
    body->spriteDescriptorPointer = BOSS_BODY_HEADLESS_SPRITE;
    spawn_head(ENTITY_PARPLIN_HEAD, BOSS_HEAD_SPRITE);
    e->state++;
    ram8(v_soundControl) = SOUND_BOSS_HEAD;
    LIFTED_RETURN();
}

/* $7868: Chokkinna's body during the head fight: hurts on contact, is
 * defeated with the head, and keeps one spell flying at a time (slot 8,
 * thrown left at 1 px/frame from 16 px below her head). */
LIFTED(updateChokkinnaCastSpells, 0x7868) {
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (entity_at(SLOT_BOSS_HEAD)->type == 0) TAIL_CALL(f_killOpponent);
    Entity *spell = entity_at(SLOT_BOSS_SPELL);
    if (spell->type != 0) LIFTED_RETURN();
    const Entity *body = entity_at(SLOT_OPPONENT);
    spell->type = ENTITY_CHOKKINNA_SPELL;
    set_x_pixel(spell, x_pixel(body));
    set_y_pixel(spell, (uint8_t)(y_pixel(body) + 0x10));
    spell->xSpeed = 0xFF00;
    spell->spriteDescriptorPointer = CHOKKINNA_SPELL_SPRITE;
    spell->flags |= EF_DESTROY_OFFSCREEN;
    cpu.iy = SLOT_BOSS_SPELL;
    LIFTED_RETURN();
}

/* $789E: Chokkinna's spell ($1A): just a harmful projectile. */
LIFTED(updateChokkinnaSpell, 0x789E) {
    TAIL_CALL(f_tryToKillAlexIfColliding);
}

/* ------------------------------------------------------------ boss heads
 * The heads run from slot 7; the code addresses slot 7 directly for
 * positions and speeds and through IX for the state fields. */

static Entity *boss_head(void) { return entity_at(SLOT_BOSS_HEAD); }

/* $799A: Gooseka's head (states 0-2 below, 3 = stunned). */
LIFTED(updateGoosekaHead, 0x799A) {
    DISPATCH_STATE(GOOSEKA_HEAD_STATES, boss_head()->state);
}

/* $79AA: state 0, rise at 1 px/frame to the bob apex, then start bobbing
 * (up at 2 px/frame) and sweeping left. */
LIFTED(updateGoosekaHeadState0, 0x79AA) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    if (y_pixel(head) < e->battleDecision) {
        e->state++;
        head->xSpeed = 0xFF00;
        head->ySpeed = 0xFE00;
    } else {
        head->ySpeed = 0xFF00;
    }
    LIFTED_RETURN();
}

/* $79C9: state 1, falling back (gravity $10) until the apex height is
 * passed, then state 2 at 2 px/frame downwards. */
LIFTED(updateGoosekaHeadState1, 0x79C9) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f__LABEL_7A10_); /* returns to our caller when punched */
    if (y_pixel(head) >= e->battleDecision) {
        e->state++;
        head->ySpeed = 0x0200;
    } else {
        head->ySpeed = (uint16_t)(head->ySpeed + 0x10);
    }
    LIFTED_RETURN();
}

/* $79E9: state 2, pulled back up (-$10) until above the apex height, then
 * state 1 at 2 px/frame upwards. */
LIFTED(updateGoosekaHeadState2, 0x79E9) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f__LABEL_7A10_);
    if (y_pixel(head) < e->battleDecision) {
        e->state--;
        head->ySpeed = 0xFE00;
    } else {
        head->ySpeed = (uint16_t)(head->ySpeed - 0x10);
    }
    LIFTED_RETURN();
}

/* $7A09: Gooseka's head contact/punch check and horizontal sweep between
 * x = $11 and x = $E0 at 1 px/frame. When punched it does not return: the
 * caller's state handler is abandoned (see _LABEL_7A40_). */
LIFTED(_LABEL_7A10_, 0x7A09) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f__LABEL_7A40_);
    if (e->unknown3 & HEAD_MOVING_RIGHT) {
        head->xSpeed = 0x0100;
        if (x_pixel(head) >= 0xE0) e->unknown3 &= (uint8_t)~HEAD_MOVING_RIGHT;
    } else {
        head->xSpeed = 0xFF00;
        if (x_pixel(head) < 0x11) e->unknown3 |= HEAD_MOVING_RIGHT;
    }
    LIFTED_RETURN();
}

/* $7A39: head punched from inside a helper.
 * QUIRK (return-to-grandparent): drops the helper's return address so that
 * _LABEL_7A41_ returns straight to the jump-table dispatcher, abandoning the
 * state handler that called the helper. */
LIFTED(_LABEL_7A40_, 0x7A39) {
    cpu.sp += 2; /* pop af: discard the return address into the state handler */
    TAIL_CALL(f__LABEL_7A41_);
}

/* $7A3A: a boss head was punched: third hit defeats it; otherwise play the
 * hit sound and stun it (state 3) for HEAD_STUN_TIME frames, saving its
 * state and speeds in unknown6 / unknown10-11 / unknown8-9. */
LIFTED(_LABEL_7A41_, 0x7A3A) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    e->flags &= (uint8_t)~EF_HIT;
    e->unknown1++;
    if (head->unknown1 >= HEAD_HITS_TO_DEFEAT) TAIL_CALL(f_killEnemy);
    ram8(v_soundControl) = SOUND_BOSS_HIT;
    head->unknown6 = head->state;
    e->state = HEAD_STUN_STATE;
    e->unknown5 = HEAD_STUN_TIME;
    ram16(SLOT_BOSS_HEAD + 0x1E) = head->ySpeed;  /* unknown10/11 */
    ram16(SLOT_BOSS_HEAD + 0x1C) = head->xSpeed;  /* unknown8/9 */
    head->ySpeed = 0;
    head->xSpeed = 0;
    LIFTED_RETURN();
}

/* $7A72: state 3 of Gooseka's and Chokkinna's heads: stunned; afterwards
 * resume the saved state and speeds. */
LIFTED(updateBattleHeadState3, 0x7A72) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    if (--e->unknown5 != 0) LIFTED_RETURN();
    head->state = head->unknown6;
    head->ySpeed = ram16(SLOT_BOSS_HEAD + 0x1E);
    head->xSpeed = ram16(SLOT_BOSS_HEAD + 0x1C);
    LIFTED_RETURN();
}

/* Chokkinna's head bounces between the top of its arc (y = $28) and the
 * ground: thrown up at $FB34 (-4.8 px/frame), gravity $5E rising, $1E
 * falling, and drifts horizontally (+-$28 per frame, reversing each bounce). */
#define CHOKKINNA_ARC_TOP 0x28
#define CHOKKINNA_JUMP_SPEED 0xFB34
#define CHOKKINNA_LANDING_SPEED 0x04CC

/* $7A89: Chokkinna's head (states 0-2 below, 3 = stunned). */
LIFTED(updateChokkinnaHead, 0x7A89) {
    DISPATCH_STATE(CHOKKINNA_HEAD_STATES, boss_head()->state);
}

/* $7A99: state 0, rise at 1 px/frame to the top, then first jump right. */
LIFTED(updateChokkinnaHeadState0, 0x7A99) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    if (y_pixel(head) < CHOKKINNA_ARC_TOP) {
        e->state++;
        head->xSpeed = 0x0200;
        head->ySpeed = CHOKKINNA_JUMP_SPEED;
        e->unknown3 |= HEAD_MOVING_RIGHT;
    } else {
        head->ySpeed = 0xFF00;
    }
    LIFTED_RETURN();
}

/* $7ABB: state 1, above the arc top: slow down (+$5E) until below it, then
 * come down fast and reverse the horizontal direction. */
LIFTED(updateChokkinnaHeadState1, 0x7ABB) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f__LABEL_7B18_); /* returns to our caller when punched */
    if (y_pixel(head) < CHOKKINNA_ARC_TOP) {
        head->ySpeed = (uint16_t)(head->ySpeed + 0x5E);
        LIFTED_RETURN();
    }
    e->state++;
    head->ySpeed = CHOKKINNA_LANDING_SPEED;
    head->unknown3 ^= HEAD_MOVING_RIGHT;
    /* QUIRK: tests the whole byte, not just the direction bit (the other
     * bits are always 0 in practice). */
    head->xSpeed = head->unknown3 != 0 ? 0x0200 : 0xFE00;
    LIFTED_RETURN();
}

/* $7AEC: state 2, below the arc top: decelerate (-$1E) until back above
 * it, then jump again. */
LIFTED(updateChokkinnaHeadState2, 0x7AEC) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f__LABEL_7A41_);
    if (y_pixel(head) < CHOKKINNA_ARC_TOP) {
        e->state--;
        head->ySpeed = CHOKKINNA_JUMP_SPEED;
    } else {
        head->ySpeed = (uint16_t)(head->ySpeed - 0x1E);
    }
    LIFTED_RETURN();
}

/* $7B11: Chokkinna's head contact/punch check (abandons the caller when
 * punched) and horizontal drift: speed +$28 per frame, or -$28 when moving
 * right. */
LIFTED(_LABEL_7B18_, 0x7B11) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    CALL_ROUTINE(f_isAlexAttackingEntity);
    if (!carry_set()) TAIL_CALL(f__LABEL_7A40_);
    uint16_t drift = (e->unknown3 & HEAD_MOVING_RIGHT) ? 0xFFD8 : 0x0028;
    head->xSpeed = (uint16_t)(head->xSpeed + drift);
    LIFTED_RETURN();
}

/* $7B2E: Parplin's head: contact damage, punches (ignored while the
 * invulnerability timer stateTimer runs), then one step of its path. */
LIFTED(updateParplinHead, 0x7B2E) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (head->stateTimer != 0) {
        e->stateTimer--;
    } else {
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (!carry_set()) {
            e->unknown1++;
            if (head->unknown1 >= HEAD_HITS_TO_DEFEAT) TAIL_CALL(f_killEnemy);
            ram8(v_soundControl) = SOUND_BOSS_HIT;
            e->stateTimer = PARPLIN_HEAD_INVULNERABLE_TIME;
        }
    }
    DISPATCH_STATE(PARPLIN_HEAD_PATH_STATES, head->state);
}

/* Parplin's head path (state = path segment). The speed helpers below
 * report through the Z flag whether the adjusted speed reached zero. */

/* $7B71: segment 0, rise at 2 px/frame until y < $70. */
LIFTED(_LABEL_7B78_, 0x7B71) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    head->ySpeed = 0xFE00;
    if (y_pixel(head) < 0x70) e->state++;
    LIFTED_RETURN();
}

/* $7B81: segments 1 and 3: curve (x speed -$20, y speed +$20) until the
 * vertical speed is zero, then fly left at 2 px/frame. */
LIFTED(_LABEL_7B88_, 0x7B81) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f__LABEL_7C10_);
    CALL_ROUTINE(f__LABEL_7C1B_);
    if (!zero_set()) LIFTED_RETURN();
    boss_head()->xSpeed = 0xFE00;
    e->state++;
    LIFTED_RETURN();
}

/* $7B92: segment 2: curve (y speed -$20, x speed +$20) until the horizontal
 * speed is zero, then rise at 2 px/frame. */
LIFTED(_LABEL_7B99_, 0x7B92) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f__LABEL_7C28_);
    CALL_ROUTINE(f__LABEL_7C03_);
    if (!zero_set()) LIFTED_RETURN();
    boss_head()->ySpeed = 0xFE00;
    e->state++;
    LIFTED_RETURN();
}

/* $7BA3: segment 4: continue until x < $48. */
LIFTED(_LABEL_7BAA_, 0x7BA3) {
    Entity *e = entity_at(cpu.ix);
    if (x_pixel(boss_head()) < 0x48) e->state++;
    LIFTED_RETURN();
}

/* $7BAD: segments 5 and 7: curve (y speed +$20, x speed +$20) until the
 * horizontal speed is zero, then descend at 2 px/frame. */
LIFTED(_LABEL_7BB4_, 0x7BAD) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f__LABEL_7C40_);
    CALL_ROUTINE(f__LABEL_7C03_);
    if (!zero_set()) LIFTED_RETURN();
    boss_head()->ySpeed = 0x0200;
    e->state++;
    LIFTED_RETURN();
}

/* $7BBE: segment 6: curve (x speed -$20, y speed -$20) until the vertical
 * speed is zero, then descend at 2 px/frame. */
LIFTED(_LABEL_7BC5_, 0x7BBE) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f__LABEL_7C10_);
    CALL_ROUTINE(f__LABEL_7C33_);
    if (!zero_set()) LIFTED_RETURN();
    boss_head()->ySpeed = 0x0200;
    e->state++;
    LIFTED_RETURN();
}

/* $7BCF: segment 8: descend to y >= $88, then dash right at 4 px/frame. */
LIFTED(_LABEL_7BD6_, 0x7BCF) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    if (y_pixel(head) < 0x88) LIFTED_RETURN();
    head->ySpeed = 0;
    head->xSpeed = 0x0400;
    e->state++;
    LIFTED_RETURN();
}

/* $7BE5: segment 9: dash right to x >= $B0, then rise again (segment 0). */
LIFTED(_LABEL_7BEC_, 0x7BE5) {
    Entity *e = entity_at(cpu.ix);
    Entity *head = boss_head();
    if (x_pixel(head) < 0xB0) LIFTED_RETURN();
    head->xSpeed = 0;
    head->ySpeed = 0xFE00;
    e->state = 0;
    LIFTED_RETURN();
}

/* Speed adjustments of the head in slot 7. The "adc" variants (or a;
 * adc hl,de) leave Z set when the new speed is zero. */
static void set_zero_flag(uint16_t v) {
    cpu.f = (uint8_t)((cpu.f & ~FLAG_Z) | (v == 0 ? FLAG_Z : 0));
}

/* $7BFC: x speed += $20; Z = speed became zero. */
LIFTED(_LABEL_7C03_, 0x7BFC) {
    Entity *head = boss_head();
    head->xSpeed = (uint16_t)(head->xSpeed + 0x20);
    set_zero_flag(head->xSpeed);
    LIFTED_RETURN();
}

/* $7C09: x speed -= $20. */
LIFTED(_LABEL_7C10_, 0x7C09) {
    Entity *head = boss_head();
    head->xSpeed = (uint16_t)(head->xSpeed - 0x20);
    LIFTED_RETURN();
}

/* $7C14: y speed += $20; Z = speed became zero. */
LIFTED(_LABEL_7C1B_, 0x7C14) {
    Entity *head = boss_head();
    head->ySpeed = (uint16_t)(head->ySpeed + 0x20);
    set_zero_flag(head->ySpeed);
    LIFTED_RETURN();
}

/* $7C21: y speed -= $20. */
LIFTED(_LABEL_7C28_, 0x7C21) {
    Entity *head = boss_head();
    head->ySpeed = (uint16_t)(head->ySpeed - 0x20);
    LIFTED_RETURN();
}

/* $7C2C: y speed -= $20; Z = speed became zero. */
LIFTED(_LABEL_7C33_, 0x7C2C) {
    Entity *head = boss_head();
    head->ySpeed = (uint16_t)(head->ySpeed - 0x20);
    set_zero_flag(head->ySpeed);
    LIFTED_RETURN();
}

/* $7C39: y speed += $20. */
LIFTED(_LABEL_7C40_, 0x7C39) {
    Entity *head = boss_head();
    head->ySpeed = (uint16_t)(head->ySpeed + 0x20);
    LIFTED_RETURN();
}
