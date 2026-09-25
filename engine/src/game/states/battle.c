/*
 * Janken (rock-paper-scissors) battles and the fight with Janken
 * ($7143-$7981): the state machine of the opponent entity (slot 6, IX), run
 * by its updater updateJanken during gameplay (game state 9 once the battle
 * has started).
 *
 * v_entities.6.state (jankenUpdaters, $714B):
 *   0  updateBattleInit: load the opponent settings (16 bytes at $764A +
 *      (data & $FE) * 8) into v_battleOpponent*.
 *   1  updateBattleMakeAlexGetIntoPosition (other module): wait until the
 *      opponent is on screen and Alex on the ground, walk Alex into place.
 *   2  updateBattleLoadOpponentTilesAndShowTextbox1: once Alex dances, load
 *      the battle tiles and show the opponent's introduction.
 *   3  updateBattleShowTextbox2: draw the thought clouds and the names, show
 *      "choose rock, scissors or paper".
 *   4  updateBattleStartRound: dance music.
 *   5  updateBattleDance: the opponent "thinks" (simulateOpponentChoosing)
 *      until the music ends, then the count "jan-ken-po".
 *   6  updateBattleThrow: at the end of the count both throw.
 *   7  updateBattleHandleThrows: $1E frames later, decide the round
 *      (battleRoundResultHandlers, $72C3, indexed by Alex + 3 * opponent;
 *      0 rock, 1 scissors, 2 paper) and show the result text box:
 *      tie -> state 4; a side that wins two rounds in a row, or any round
 *      from the third one, wins the match: lost -> 8, won -> 11.
 *   8  updateBattleShowBattleLostTextbox, 9 updateBattleTurnAlexIntoStatue,
 *   10 updateBattleRespawOpponent (Alex loses a life; the opponent waits).
 *   11 _LABEL_73AE_: battle won (updateBattleBattleWon, other module), the
 *      opponent's block opens (3 name-table patches).
 *   12 updateBattleStartFight (after a text box: castle music), 13 and 19
 *      updateBattlePatchNametable: apply queued name-table patches.
 *   14 _LABEL_73D8_: the final fight with Janken: he throws projectiles
 *      (entity $19) every $60 frames; 3 punches defeat him.
 *   15 _LABEL_7447_: Janken defeated. 16: Alex walks into position again.
 *   17 _LABEL_7453_: name-table animation (request $88), 18 _LABEL_746F_:
 *      ladder patches and the moonlight stone medallion (+10000 points).
 *   20 updateBattleNop.
 * The round results are shown by entity $0C using the sprite descriptor at
 * $C2A0: tile $A4 (Alex won) or $A5 (opponent won) at $C2A6 + 2 * round.
 */
#include "states.h"

#define JANKEN_UPDATERS 0x714B
#define BATTLE_ROUND_RESULT_HANDLERS 0x72C3
#define ENTITY_0x19_UPDATERS 0x74D8
#define OPPONENT_SETTINGS 0x764A
#define THOUGHT_CLOUD_HANDS 0x7634   /* sprite descriptor of rock / scissors / paper */
#define NULL_SPRITE_DESCRIPTOR 0x80E1
#define CHARACTER_SPRITE_DESCRIPTOR 0x8134
#define JANKEN_FLYING_DESCRIPTOR 0x961A
#define THOUGHT_CLOUD_NAMETABLE 0x92A8
#define BLOCK_ENTRANCE_NAMETABLE_CHANGES 0x776B
#define LADDER_NAMETABLE_CHANGES 0x7773
#define DEFEAT_ANIMATION_DATA 0xAAFE /* bank 4, "jankenPetrificationTable" frames */

#define OPPONENT ENTITY_SLOT(6)
#define ROUND_RESULTS 0xC2A6 /* tile of each round in the battle score descriptor */
#define TILE_ALEX_WON_ROUND 0xA4
#define TILE_OPPONENT_WON_ROUND 0xA5
#define SAVED_NAMES_ROW _RAM_C260_   /* 46 bytes of the name table under the names */
#define NAMES_ROW _RAM_C908_
#define THOUGHT_CLOUDS_AREA _RAM_CA08_ /* $EC bytes saved in v_nametableCopy */

/* The janken state numbers set directly (the others just increment). */
enum BattleState {
    BATTLE_START_ROUND = 0x04,
    BATTLE_SHOW_LOST_TEXTBOX = 0x08,
    BATTLE_WON = 0x0B,
};

/* Text box messages. */
#define TXT_BATTLE_GUIDE 0x07
#define TXT_BATTLE_ROUND_LOST 0x08
#define TXT_BATTLE_ROUND_WON 0x09
#define TXT_BATTLE_ROUND_TIE 0x0A
#define TXT_BATTLE_LOST 0x15

#define SCORE_10000 0x15 /* index in the score table */

static inline bool text_box_open(void) { return (ram8(v_gameState) & STATE_NUMBER_MASK) == STATE_TEXT_BOX; }

static void next_battle_state(void) { entity_at(cpu.ix)->state++; }

/* $7143: updater of the janken opponent (slot 6). */
LIFTED(updateJanken, 0x7143) {
    if (call_jump_table(JANKEN_UPDATERS, entity_at(OPPONENT)->state)) return;
    LIFTED_RETURN();
}

/* $7175: state 0. */
LIFTED(updateBattleInit, 0x7175) {
    next_battle_state();
    ram8(v_hasBattleEnded) = 0;
    ram8(v_hasBattleStarted) = 0;
    Entity *opponent = entity_at(OPPONENT);
    opponent->animationTimer = 1;
    opponent->unknown11 = 1; /* simulateOpponentChoosing runs while non-zero */
    uint8_t settings = (uint8_t)((opponent->data & 0xFE) * 8);
    /* Janken himself (data 0/1) is visible from the start. */
    opponent->spriteDescriptorPointer = settings ? NULL_SPRITE_DESCRIPTOR : CHARACTER_SPRITE_DESCRIPTOR;
    copy_bytes(v_battleOpponentSpriteDescriptorPointer, (uint16_t)(OPPONENT_SETTINGS + settings), 0x10);
    LIFTED_RETURN();
}

/* $71C5: state 2. */
LIFTED(updateBattleLoadOpponentTilesAndShowTextbox1, 0x71C5) {
    if (entity_at(v_alex)->state != ALEX_BATTLE_DANCING) LIFTED_RETURN();
    next_battle_state();
    CALL_ROUTINE(f_clearEntities2to4AndMaybeReset0xC054);
    Entity *opponent = entity_at(OPPONENT);
    opponent->spriteDescriptorPointer = ram16(v_battleOpponentSpriteDescriptorPointer);
    if (opponent->data == 1) {
        Entity *self = entity_at(cpu.ix);
        X_PIXEL(self) = 0xB8;
        Y_PIXEL(self) = 0x80;
    }
    ram8(v_hasBattleStarted) = 1;
    CALL_ROUTINE(f_prepareForBattle); /* leaves interrupts disabled */
    cpu.hl = ram16(v_battleOpponentTilesPointer);
    cpu.de = VDP_VRAM_WRITE(0x2400);
    CALL_ROUTINE(f_decompressTilesToVram);
    enable_interrupts();
    map_bank(BANK(2));
    ram8(v_gameState) = STATE_TEXT_BOX;
    ram8(v_textBoxMessageIndex) = ram8(v_battleOpponentMessagePointer);
    LIFTED_RETURN();
}

/* $7209: state 3. Returns IY from drawThoughtClouds. */
LIFTED(updateBattleShowTextbox2, 0x7209) {
    if (text_box_open()) LIFTED_RETURN();
    cpu.hl = ram16(v_opponentNamePointer);
    CALL_ROUTINE(f_drawThoughtClouds);
    CALL_ROUTINE(f_drawAlexName_LABEL_7941_);
    ram8(v_gameState) = STATE_TEXT_BOX;
    ram8(v_textBoxMessageIndex) = TXT_BATTLE_GUIDE;
    next_battle_state();
    entity_at(cpu.ix)->animationTimerResetValue = 0x10;
    LIFTED_RETURN();
}

/* $7228: Z set when a text box is open. */
LIFTED(isTextboxGameState, 0x7228) {
    cpu.a = ram8(v_gameState) & STATE_NUMBER_MASK;
    alu_cp(STATE_TEXT_BOX);
    LIFTED_RETURN();
}

/* $7230: state 4. */
LIFTED(updateBattleStartRound, 0x7230) {
    if (text_box_open()) LIFTED_RETURN();
    entity_at(v_alex)->state = ALEX_BATTLE_DANCING;
    ram8(v_soundControl) = SOUND_JANKEN_MUSIC;
    next_battle_state();
    Entity *self = entity_at(cpu.ix);
    self->unknown11 = 0xFF; /* thinking time */
    self->unknown6 = 0x14;
    LIFTED_RETURN();
}

/* $724A: state 5, dance until the music ends. */
LIFTED(updateBattleDance, 0x724A) {
    CALL_ROUTINE(f_simulateOpponentChoosing_LABEL_7941_);
    if (ram8(v_soundBattleSoundFlags) != 0x80) {
        cpu.hl = ram16(v_opponentAnimationDescriptorPointer);
        TAIL_CALL(f_handleEntityAnimation);
    }
    ram8(v_soundControl) = SOUND_JANKEN_COUNT;
    next_battle_state();
    Entity *self = entity_at(cpu.ix);
    self->animationTimerResetValue = 0x14;
    entity_at(OPPONENT)->animationTimer = 1;
    entity_at(v_alex)->animationTimer = 1;
    entity_at(v_alex)->state = ALEX_BATTLE_COUNTING;
    self->unknown11 = 0x46;
    LIFTED_RETURN();
}

/* $7278: state 6, count until the throw. */
LIFTED(updateBattleThrow, 0x7278) {
    CALL_ROUTINE(f_simulateOpponentChoosing_LABEL_7941_);
    if (ram8(v_soundBattleSoundFlags) != 0x10) {
        cpu.hl = ram16(v_opponentCountdownAnimationDescriptorPointer);
        TAIL_CALL(f_handleEntityAnimation);
    }
    ram8(v_soundControl) = SOUND_JANKEN_THROW;
    next_battle_state();
    entity_at(v_alex)->state = ALEX_BATTLE_THROW;
    /* Sprite of the opponent's hand: rock, scissors or paper. */
    Entity *opponent = entity_at(OPPONENT);
    uint16_t throws = ram16(v_opponentThrowSpriteDescriptorPointer);
    opponent->spriteDescriptorPointer = rd16((uint16_t)(throws + (uint8_t)(opponent->battleDecision * 2)));
    entity_at(cpu.ix)->unknown6 = 0x1E;
    LIFTED_RETURN();
}

/* $72AC: state 7, compare the throws. */
LIFTED(updateBattleHandleThrows, 0x72AC) {
    Entity *self = entity_at(cpu.ix);
    if (--self->unknown6 != 0) LIFTED_RETURN();
    uint8_t result = (uint8_t)(entity_at(v_alex)->battleDecision + 3 * self->battleDecision);
    if (call_jump_table(BATTLE_ROUND_RESULT_HANDLERS, result)) return;
    ram8(v_gameState) = STATE_TEXT_BOX;
    LIFTED_RETURN();
}

/* $72D5: round result: tie. */
LIFTED(updateBattleRoundTie, 0x72D5) {
    ram8(v_textBoxMessageIndex) = TXT_BATTLE_ROUND_TIE;
    entity_at(cpu.ix)->state = BATTLE_START_ROUND;
    LIFTED_RETURN();
}

/* Marks the round in the score display and tells whether the match is
 * over: the same side won the previous round, or this is round 3 or later. */
static bool record_round(uint8_t tile) {
    Entity *self = entity_at(cpu.ix);
    uint16_t mark = (uint16_t)(ROUND_RESULTS + 2 * self->unknown7); /* unknown7 = round */
    wr8(mark, tile);
    if (rd8((uint16_t)(mark - 2)) == tile) return true;
    self->unknown7++;
    return entity_at(OPPONENT)->unknown7 >= 3;
}

/* $72DF: round result: Alex lost the round. */
LIFTED(updateBattleRoundLost, 0x72DF) {
    ram8(v_textBoxMessageIndex) = TXT_BATTLE_ROUND_LOST;
    if (!record_round(TILE_OPPONENT_WON_ROUND)) {
        entity_at(cpu.ix)->state = BATTLE_START_ROUND;
        LIFTED_RETURN();
    }
    entity_at(cpu.ix)->state = BATTLE_SHOW_LOST_TEXTBOX;
    TAIL_CALL(f_restoreSomeNametableStuff_LABEL_796D_);
}

/* $730D: round result: Alex won the round. */
LIFTED(updateBattleRoundWon, 0x730D) {
    ram8(v_textBoxMessageIndex) = TXT_BATTLE_ROUND_WON;
    if (!record_round(TILE_ALEX_WON_ROUND)) {
        entity_at(cpu.ix)->state = BATTLE_START_ROUND;
        LIFTED_RETURN();
    }
    entity_at(cpu.ix)->state = BATTLE_WON;
    TAIL_CALL(f_restoreSomeNametableStuff_LABEL_796D_);
}

/* $733B: state 8. */
LIFTED(updateBattleShowBattleLostTextbox, 0x733B) {
    if (text_box_open()) LIFTED_RETURN();
    CALL_ROUTINE(f_destroyBattleEntities);
    ram8(v_textBoxMessageIndex) = TXT_BATTLE_LOST;
    ram8(v_gameState) = STATE_TEXT_BOX;
    next_battle_state();
    LIFTED_RETURN();
}

/* $7350: state 9, Alex turns into a statue (and dies). */
LIFTED(updateBattleTurnAlexIntoStatue, 0x7350) {
    if (text_box_open()) LIFTED_RETURN();
    entity_at(cpu.ix)->animationTimerResetValue = 0x14;
    ram8(v_soundControl) = SOUND_BATTLE_LOST;
    Entity *alex = entity_at(v_alex);
    alex->state = ALEX_BATTLE_STATUE;
    alex->unknown6 = 0x3C;
    next_battle_state();
    LIFTED_RETURN();
}

/* $736B: state 10: the opponent dances until the life is lost, then goes
 * back to its initial state (type, data and position kept; Janken, data 0,
 * is moved to x=$C0, y=$98). */
LIFTED(updateBattleRespawOpponent, 0x736B) {
    if (ram8(v_gameState) != STATE_LIFE_LOST) {
        cpu.hl = ram16(v_opponentAnimationDescriptorPointer);
        TAIL_CALL(f_handleEntityAnimation);
    }
    Entity *self = entity_at(cpu.ix);
    cpu.b = self->type;
    cpu.c = self->data;
    cpu.e = X_PIXEL(self);
    cpu.d = Y_PIXEL(self);
    op_exx(); /* the saved values stay in the main bank */
    CALL_ROUTINE(f_destroyCurrentEntity);
    op_exx();
    entity_at(OPPONENT)->spriteDescriptorPointer = NULL_SPRITE_DESCRIPTOR;
    self->type = cpu.b;
    self->data = cpu.c;
    X_PIXEL(self) = cpu.e;
    Y_PIXEL(self) = cpu.d;
    if (cpu.c == 0) {
        X_PIXEL(self) = 0xC0;
        Y_PIXEL(self) = 0x98;
    }
    LIFTED_RETURN();
}

/* $73A7: state 11: battle won; queue the 3 patches that open the block. */
LIFTED(_LABEL_73AE_, 0x73A7) {
    CALL_ROUTINE(f_updateBattleBattleWon);
    entity_at(cpu.ix)->unknown6 = 0x60;
    ram16(_RAM_C219_) = BLOCK_ENTRANCE_NAMETABLE_CHANGES; /* patch list */
    ram8(_RAM_C218_) = 3;                                 /* patches left + 1 */
    Entity *opponent = entity_at(OPPONENT);
    opponent->unknown1 = 0;
    opponent->spriteDescriptorPointer = JANKEN_FLYING_DESCRIPTOR;
    LIFTED_RETURN();
}

/* $73C4: state 12: after the text box, the fight music. */
LIFTED(updateBattleStartFight, 0x73C4) {
    if (text_box_open()) LIFTED_RETURN();
    ram8(v_soundControl) = SOUND_CASTLE_SONG;
    next_battle_state();
    LIFTED_RETURN();
}

/* $73D1: state 14: fight with Janken. unknown1 counts Alex's hits, unknown6
 * the frames until the next projectile (entity $19 in slot 7 or 8). */
LIFTED(_LABEL_73D8_, 0x73D1) {
    Entity *self = entity_at(cpu.ix);
    if (self->isOffScreenFlags >> 8) LIFTED_RETURN();
    self->unknown2 = 0xCC;
    CALL_ROUTINE(f_tryToKillAlexIfColliding);

    Entity *alex = entity_at(v_alex);
    bool hit_sound = false;
    if (alex->unknown8 & 0x08) {
        /* Alex punched: is it Janken? */
        alex->unknown8 &= (uint8_t)~0x08;
        self->unknown2 = 0xD0;
        CALL_ROUTINE(f_isAlexAttackingEntity);
        if (!(cpu.f & FLAG_C)) {
            self->unknown1++;
            if (entity_at(OPPONENT)->unknown1 >= 3) {
                next_battle_state(); /* defeated */
                LIFTED_RETURN();
            }
            hit_sound = true;
        }
    }
    if (hit_sound) ram8(v_soundControl) = SOUND_BOSS_HIT;

    if (alex->state >= ALEX_DEAD) LIFTED_RETURN();
    if (--self->unknown6 != 0) LIFTED_RETURN();
    self->unknown6++;
    /* Throw a projectile from the first free slot of 7 and 8. */
    cpu.iy = ENTITY_SLOT(7);
    if (ram8(ENTITY_SLOT(7)) != 0) {
        cpu.iy = ENTITY_SLOT(8);
        if (ram8(ENTITY_SLOT(8)) != 0) LIFTED_RETURN();
    }
    self->unknown6 = 0x60;
    Entity *projectile = entity_at(cpu.iy);
    projectile->type = 0x19;
    X_PIXEL(projectile) = (uint8_t)(X_PIXEL(entity_at(OPPONENT)) + 0x08);
    Y_PIXEL(projectile) = (uint8_t)(Y_PIXEL(entity_at(OPPONENT)) + 0x10);
    ram8(v_soundControl) = SOUND_BOSS_SHOT;
    LIFTED_RETURN();
}

/* $7440: state 15: Janken defeated. */
LIFTED(_LABEL_7447_, 0x7440) {
    CALL_ROUTINE(f_handler_LABEL_99D3_);
    ram8(v_soundControl) = SOUND_BOSS_DEFEATED;
    next_battle_state();
    LIFTED_RETURN();
}

/* $744C: state 17: when Alex dances, start the 48-step name-table animation
 * of request $88 (_RAM_C218_ = step, _RAM_C21B_ = frame data). */
LIFTED(_LABEL_7453_, 0x744C) {
    Entity *alex = entity_at(v_alex);
    if (alex->state != ALEX_BATTLE_DANCING) LIFTED_RETURN();
    alex->state = ALEX_STATE_0x1A;
    ram16(_RAM_C21B_) = DEFEAT_ANIMATION_DATA;
    ram8(v_nametableChangeRequest) = 0x88;
    ram8(_RAM_C218_) = 0;
    next_battle_state();
    LIFTED_RETURN();
}

/* $7468: state 18: queue the ladder patches, drop the moonlight stone
 * medallion (entity $52 in slot 9) and give 10000 points. Returns IY = slot 9. */
LIFTED(_LABEL_746F_, 0x7468) {
    if (ram8(v_nametableChangeRequest)) LIFTED_RETURN();
    ram16(_RAM_C219_) = LADDER_NAMETABLE_CHANGES;
    ram8(_RAM_C218_) = 6;
    next_battle_state();
    cpu.iy = ENTITY_SLOT(9);
    Entity *medallion = entity_at(ENTITY_SLOT(9));
    medallion->type = 0x52;
    medallion->data = 0x04;
    X_PIXEL(medallion) = 0xA8;
    Y_PIXEL(medallion) = 0xA0;
    medallion->flags = 0x00;
    ram8(v_hasMoonstoneMedallion) = 1;
    cpu.l = SCORE_10000;
    TAIL_CALL(f_addScore);
}

/* $749D: states 13 and 19: apply the queued name-table patches (4 bytes
 * each: destination, source metatile) one per request, last one first. */
LIFTED(updateBattlePatchNametable, 0x749D) {
    if (ram8(v_nametableChangeRequest)) LIFTED_RETURN();
    uint8_t left = --ram8(_RAM_C218_);
    if (left != 0) {
        copy_bytes(v_nametableChangeDestination, (uint16_t)(ram16(_RAM_C219_) + (uint8_t)(left * 4)), 4);
        ram8(v_nametableChangeRequest) = 0x80;
        LIFTED_RETURN();
    }
    CALL_ROUTINE(f_setAlexIdleStateAndLoadIdleAnimationDescriptor);
    next_battle_state();
    LIFTED_RETURN(); /* falls into updateBattleNop */
}

/* $74C6: state 20: nothing left to do. */
LIFTED(updateBattleNop, 0x74C6) {
    LIFTED_RETURN();
}

/* $74C7: updater of Janken's projectile (entity $19): if it turned Alex to
 * stone (the collision sets bit 7 of Alex's flags, IY = Alex), Alex becomes a
 * statue and both projectiles vanish; otherwise it moves (table $74D8). */
LIFTED(updateEntity0x19, 0x74C7) {
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    Entity *alex = entity_at(cpu.iy);
    if (!(alex->flags & 0x80)) {
        if (call_jump_table(ENTITY_0x19_UPDATERS, entity_at(cpu.ix)->state)) return;
        LIFTED_RETURN();
    }
    alex->flags &= (uint8_t)~0x80;
    alex->state = ALEX_BATTLE_STATUE;
    alex->unknown6 = 0x3C;
    CALL_ROUTINE(f_handler_LABEL_99D3_);
    ram8(v_soundControl) = SOUND_BATTLE_LOST;
    cpu.hl = ENTITY_SLOT(7);
    CALL_ROUTINE(f_clearEntity);
    cpu.hl++;
    TAIL_CALL(f_clearEntity);
}

/* $7502: projectile state 0: starts moving left. */
LIFTED(_LABEL_7509_, 0x7502) {
    Entity *self = entity_at(cpu.ix);
    self->flags |= 0x02;
    self->spriteDescriptorPointer = 0x974B;
    self->xSpeed = (uint16_t)((self->xSpeed & 0x00FF) | 0xFF00);
    self->unknown5 = 0x08;
    self->battleDecision = 0x01;
    self->state++;
    LIFTED_RETURN();
}

/* $7609: copies the 4x8-byte thought cloud (bank 2, $92A8) into the RAM
 * name table at DE. */
LIFTED(drawThoughtClouds_patchNametableWithThoughtCloud, 0x7609) {
    uint16_t src = THOUGHT_CLOUD_NAMETABLE, dst = cpu.de;
    for (int row = 0; row < 4; row++) {
        copy_bytes(dst, src, 8);
        src = (uint16_t)(src + 8);
        dst = (uint16_t)(dst + NAMETABLE_ROW_BYTES);
    }
    LIFTED_RETURN();
}

/* $761F: updater of the hand shown in a thought cloud (entity $0B): its
 * sprite follows battleDecision (0 rock, 1 scissors, 2 paper). */
LIFTED(updateEntity0x0B, 0x761F) {
    Entity *self = entity_at(cpu.ix);
    self->spriteDescriptorPointer = rd16((uint16_t)(THOUGHT_CLOUD_HANDS + (uint8_t)(self->battleDecision * 2)));
    LIFTED_RETURN();
}

/* $763A: removes the thought-cloud hands (slots 27, 28) and the score
 * display (slot 23). */
LIFTED(destroyBattleEntities, 0x763A) {
    cpu.hl = ENTITY_SLOT(27);
    CALL_ROUTINE(f_clearEntity);
    cpu.hl++;
    CALL_ROUTINE(f_clearEntity);
    cpu.hl = ENTITY_SLOT(23);
    TAIL_CALL(f_clearEntity);
}

/* $7966: end of the match: restores the name table under the names and
 * the thought clouds, and flags the battle as ended. */
LIFTED(restoreSomeNametableStuff_LABEL_796D_, 0x7966) {
    copy_bytes(NAMES_ROW, SAVED_NAMES_ROW, 0x2E);
    copy_bytes(THOUGHT_CLOUDS_AREA, v_nametableCopy, 0xEC);
    ram8(v_hasBattleEnded) = 1;
    LIFTED_RETURN();
}
