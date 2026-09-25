/*
 * Janken (rock-paper-scissors) battle helpers ($71A9, $751E-$7982): getting
 * Alex into position, the opponent "thinking" of a throw, thought clouds,
 * name and round-result marks, the end of a won match, and the bobbing
 * projectile thrown by Janken the Great.
 *
 * The battle opponent lives in entity slot 6 (Entity.data >> 1 selects the
 * opponent settings, data bit 0 = a boss fight follows the match); the
 * state machine itself (updateBattle*) is in the game states module.
 */
#include "game/enemies2/enemies2.h"

/* $71A9: battle state 1: once the opponent is on screen, the screen stopped
 * scrolling and Alex stands on the ground, Alex walks to his battle position
 * and the battle advances. */
LIFTED(updateBattleMakeAlexGetIntoPosition, 0x71A9) {
    Entity *e = entity_at(cpu.ix);
    const Entity *opponent = entity_at(SLOT_OPPONENT);
    Entity *alex = entity_at(SLOT_ALEX);
    if ((opponent->isOffScreenFlags >> 8) | (uint8_t)e->isOffScreenFlags) LIFTED_RETURN();
    if (ram8(v_scrollFlags) & SCROLL_ANY) LIFTED_RETURN();
    if (alex->state >= ALEX_IN_AIR) LIFTED_RETURN();
    e->state++;
    alex->state = ALEX_BATTLE_GO_TO_POSITION;
    LIFTED_RETURN();
}

/* ------------------------------------------- Janken's projectile ($19)
 * Thrown by Janken the Great during his fight (updateEntity0x19 dispatches
 * on Entity.state). It flies left while bobbing: it rises, waits, sinks,
 * waits, with pauses (unknown5) growing by 2 frames per cycle
 * (battleDecision holds the current pause length). */
#define PROJECTILE_BOB_ACCEL 0x0040
#define PROJECTILE_MAX_RISE 0xFE00
#define PROJECTILE_MAX_SINK 0x0200
#define PROJECTILE_STATE_RISING 2

/* $751E: state 1, initial pause, then start rising at 2 px/frame. */
LIFTED(_LABEL_7525_, 0x751E) {
    Entity *e = entity_at(cpu.ix);
    if (--e->unknown5 != 0) LIFTED_RETURN();
    e->ySpeed = PROJECTILE_MAX_RISE;
    e->state++;
    LIFTED_RETURN();
}

/* $752E: state 2, decelerate the rise and accelerate downwards until the
 * sinking speed reaches 2 px/frame. */
LIFTED(_LABEL_7535_, 0x752E) {
    Entity *e = entity_at(cpu.ix);
    e->ySpeed = (uint16_t)(e->ySpeed + PROJECTILE_BOB_ACCEL);
    if (e->ySpeed & 0x8000) LIFTED_RETURN();          /* still rising */
    if (e->ySpeed < PROJECTILE_MAX_SINK) LIFTED_RETURN();
    cpu.de = PROJECTILE_MAX_SINK;
    TAIL_CALL(f_sub_7548);
}

/* $7548: clamp the vertical speed to DE, go to the next state and start a
 * pause 2 frames longer than the previous one. */
LIFTED(sub_7548, 0x7548) {
    Entity *e = entity_at(cpu.ix);
    e->ySpeed = cpu.de;
    e->state++;
    e->battleDecision = (uint8_t)(e->battleDecision + 2);
    e->unknown5 = e->battleDecision;
    LIFTED_RETURN();
}

/* $755D: state 3, pause while sinking. */
LIFTED(_LABEL_7564_, 0x755D) {
    Entity *e = entity_at(cpu.ix);
    if (--e->unknown5 != 0) LIFTED_RETURN();
    e->state++;
    LIFTED_RETURN();
}

/* $7565: state 4, decelerate the fall and accelerate upwards until the
 * rising speed reaches 2 px/frame. */
LIFTED(_LABEL_756C_, 0x7565) {
    Entity *e = entity_at(cpu.ix);
    e->ySpeed = (uint16_t)(e->ySpeed - PROJECTILE_BOB_ACCEL);
    if (!(e->ySpeed & 0x8000)) LIFTED_RETURN();       /* still sinking */
    if (e->ySpeed >= PROJECTILE_MAX_RISE) LIFTED_RETURN();
    cpu.de = PROJECTILE_MAX_RISE;
    TAIL_CALL(f_sub_7548);
}

/* $7581: state 5, pause while rising, then back to state 2. */
LIFTED(_LABEL_7588_, 0x7581) {
    Entity *e = entity_at(cpu.ix);
    if (--e->unknown5 != 0) LIFTED_RETURN();
    e->state = PROJECTILE_STATE_RISING;
    LIFTED_RETURN();
}

/* ------------------------------------------------------ opponent thinking */
#define OPPONENT_THINK_DELAYS 0x775C /* _DATA_7763_: frames between changes of mind, per opponent data */
#define DECISION_INDEX_MASK 0x1F

/* $758A: while the opponent's thinking time (slot 6 unknown11) lasts, it
 * changes its mind every few frames (countdown in unknown6), taking the next
 * throw from its decision list (v_opponentDecisionsPointer, 32 entries,
 * cycled by v_BattleOpponentDecisionIndex). With the Telepathy Ball the
 * opponent's thought cloud (slot 27) shows the current throw. */
LIFTED(simulateOpponentChoosing_LABEL_7941_, 0x758A) {
    Entity *e = entity_at(cpu.ix);
    Entity *opponent = entity_at(SLOT_OPPONENT);
    if (opponent->unknown11 == 0) LIFTED_RETURN();
    opponent->unknown11--;

    uint8_t delay = rd8((uint16_t)(OPPONENT_THINK_DELAYS + e->data));
    uint16_t decisions = ram16(v_opponentDecisionsPointer);
    /* QUIRK: "ret p" - the countdown fires when it goes negative, so the
     * opponent changes its mind every delay + 1 frames. */
    if ((int8_t)--e->unknown6 >= 0) LIFTED_RETURN();
    e->unknown6 = delay;

    uint8_t index = ++ram8(v_BattleOpponentDecisionIndex) & DECISION_INDEX_MASK;
    uint8_t decision = rd8((uint16_t)(decisions + index));
    e->battleDecision = decision;
    if (ram8(v_hasTelepathyBall) == 0) LIFTED_RETURN();
    entity_at(SLOT_THOUGHT_OPPONENT)->battleDecision = decision;
    LIFTED_RETURN();
}

/* ------------------------------------------------------- thought clouds */
#define THOUGHT_CLOUD_AREA_SIZE 0xEC

/* $75BF: draw the thought clouds above Alex and (with the Telepathy Ball)
 * above the opponent: backs up the name-table area under the clouds into
 * v_nametableCopy, patches the cloud tiles in, and spawns the throw preview
 * entities ($0B) in slots 28 (Alex) and 27 (opponent). Leaves IY at the last
 * spawned preview. */
LIFTED(drawThoughtClouds, 0x75BF) {
    cpu.hl = SLOT_THOUGHT_OPPONENT;
    CALL_ROUTINE(f_clearEntity);
    cpu.hl++;
    CALL_ROUTINE(f_clearEntity);

    copy_bytes(v_nametableCopy, v_thoughtCloudAlexArea, THOUGHT_CLOUD_AREA_SIZE);
    cpu.de = v_thoughtCloudAlexArea;
    CALL_ROUTINE(f_drawThoughtClouds_patchNametableWithThoughtCloud);

    uint8_t scroll = (uint8_t)(ram16(v_horizontalScroll) >> 8);
    Entity *alex_preview = entity_at(SLOT_THOUGHT_ALEX);
    alex_preview->type = ENTITY_THOUGHT_CLOUD;
    set_x_pixel(alex_preview, (uint8_t)(0x20 + scroll));
    set_y_pixel(alex_preview, 0x3F);
    cpu.iy = SLOT_THOUGHT_ALEX;
    if (ram8(v_hasTelepathyBall) == 0) LIFTED_RETURN();

    Entity *opponent_preview = entity_at(SLOT_THOUGHT_OPPONENT);
    opponent_preview->type = ENTITY_THOUGHT_CLOUD;
    set_x_pixel(opponent_preview, (uint8_t)(0xB0 + scroll));
    set_y_pixel(opponent_preview, 0x3F);
    cpu.iy = SLOT_THOUGHT_OPPONENT;
    cpu.de = v_thoughtCloudOpponentArea;
    TAIL_CALL(f_drawThoughtClouds_patchNametableWithThoughtCloud);
}

/* ------------------------------------------------------ battle set-up */
#define BATTLE_TILES_BANK 0x84
#define BATTLE_TILES 0x98E9
#define BATTLE_TILES_VRAM 0x7000   /* VDP write address of tile $180 */
#define CLEARED_SLOTS_FOR_BATTLE 0x16

/* $791D: clear entity slots 7-28, silence the sound engine and load the
 * battle tiles (hands, clouds, marks) with interrupts disabled. */
LIFTED(prepareForBattle, 0x791D) {
    cpu.hl = SLOT_BOSS_HEAD;
    for (int i = 0; i < CLEARED_SLOTS_FOR_BATTLE; i++) {
        CALL_ROUTINE(f_clearEntity);
        cpu.hl++;
    }
    CALL_ROUTINE(f_reset_9DF3);
    wr8(0xFFFF, BATTLE_TILES_BANK);
    cpu.hl = BATTLE_TILES;
    cpu.de = BATTLE_TILES_VRAM;
    cpu.iff1 = cpu.iff2 = 0;
    TAIL_CALL(f_decompressTilesToVram);
}

#define ALEX_NAME_TILES 0x769C        /* _DATA_76A3_: "ALEX" name-table entries */
#define ALEX_NAME_SIZE 8
#define NAME_ROW_SIZE 0x2E
#define SCORE_MARKS_TEMPLATE 0x7764   /* _DATA_776B_ */
#define SCORE_MARKS_SIZE 0x0B

/* $793A: back up the name row, write "ALEX" into it, build the round-result
 * marks sprite in RAM and spawn its entity ($0C) in slot 23. (The opponent's
 * name is only drawn in the Japanese version.) */
LIFTED(drawAlexName_LABEL_7941_, 0x793A) {
    copy_bytes(v_battleNameRowBackup, v_battleNameRow, NAME_ROW_SIZE);
    copy_bytes(v_battleNameRow, ALEX_NAME_TILES, ALEX_NAME_SIZE);
    copy_bytes(v_scoreMarksSprite, SCORE_MARKS_TEMPLATE, SCORE_MARKS_SIZE);
    cpu.hl = SLOT_BLOCK_DEBRIS;
    CALL_ROUTINE(f_clearEntity);
    ram8(SLOT_BLOCK_DEBRIS) = ENTITY_SCORE_MARKS;
    LIFTED_RETURN();
}

/* $7982: round-result marks ($0C): uses the RAM sprite built above, shown
 * at the top left of the screen. */
LIFTED(updateEntity0x0C, 0x7982) {
    Entity *e = entity_at(cpu.ix);
    if (e->flags & EF_INITIALIZED) LIFTED_RETURN();
    e->flags |= EF_INITIALIZED;
    entity_at(SLOT_BLOCK_DEBRIS)->spriteDescriptorPointer = v_scoreMarksSprite;
    set_x_pixel(e, 0x28);
    set_y_pixel(e, 0x30);
    LIFTED_RETURN();
}

/* ------------------------------------------------------ end of the match */
#define GOOSEKA_NAMETABLE_CHANGES 0x7787
#define GOOSEKA_NAMETABLE_CHANGE_COUNT 2
#define BOSS_ENTRANCE_DELAY 0x28

/* $78C7: match won (state $0B): once the text box is closed, Alex goes back
 * to idle and the battle entities are removed. Without a boss fight
 * (data bit 0 clear) the opponent is simply defeated; otherwise the "boss
 * fight" message is shown and the boss appears after $28 frames. */
LIFTED(updateBattleBattleWon, 0x78C7) {
    Entity *e = entity_at(cpu.ix);
    CALL_ROUTINE(f_isTextboxGameState);
    if (zero_set()) LIFTED_RETURN();
    CALL_ROUTINE(f_setAlexIdleStateAndLoadIdleAnimationDescriptor);
    CALL_ROUTINE(f_destroyBattleEntities);
    if (!(e->data & 1)) TAIL_CALL(f_killOpponent);
    e->state++;
    e->unknown5 = BOSS_ENTRANCE_DELAY;
    ram8(v_textBoxMessageIndex) = TXT_BOSS_FIGHT;
    ram8(v_gameState) = STATE_TEXT_BOX;
    LIFTED_RETURN();
}

/* $77B7: Gooseka's variant: also queue the name-table patches that open the
 * way (applied later by updateBattlePatchNametable). */
LIFTED(updateBattleBattleWonAndSetupNametablePatches, 0x77B7) {
    CALL_ROUTINE(f_updateBattleBattleWon);
    ram16(v_battleNametablePatches) = GOOSEKA_NAMETABLE_CHANGES;
    ram8(v_battleNametablePatchCount) = GOOSEKA_NAMETABLE_CHANGE_COUNT;
    LIFTED_RETURN();
}

/* $7804: boss body (Gooseka, Parplin) during the head fight: hurts Alex on
 * contact and is defeated when the head is gone. */
LIFTED(updateBattleDestroyWhenDefeated, 0x7804) {
    CALL_ROUTINE(f_tryToKillAlexIfColliding);
    if (entity_at(SLOT_BOSS_HEAD)->type == 0) TAIL_CALL(f_killOpponent);
    LIFTED_RETURN();
}
