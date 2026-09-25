/*
 * Story characters and items ($5FAA-$62A7): prisoners Egle / Princess Lora,
 * the village elder, Saint Nurari, King High Stone, the story item pick-ups
 * and the bonus-level trigger.
 *
 * Characters talk through _LABEL_60D4_: it switches to the text box game
 * state and records the entity and its animation so the text box state can
 * animate them while the message is shown.
 */
#include "game/enemies2/enemies2.h"

#define NULL_SPRITE 0x80E1
#define CHARACTER_SPRITE 0x8134      /* characterSpriteDescriptor */

/* Story items ($52), indexed by Entity.data:
 *  0 bonus level entrance, 1 Telepathy Ball, 2 Letter to Nibana,
 *  3 Hirotta Stone, 4 Moonstone Medallion, 5 extra life, 6 Power Bracelet,
 *  7 Teleport Powder, 8-9 Sunstone Medallion. */
#define ITEM_SPRITES 0x641B          /* _DATA_6422_: sprite descriptor per item */
#define ITEM_OWNED_FLAGS 0x642F      /* _DATA_6436_: v_has... variable per item */
#define ITEM_COLLECTED_FLAGS 0x6443  /* _DATA_644A_: D80x "already picked up" flag per item */
#define ITEM_BONUS_LEVEL 0
#define ITEM_EXTRA_LIFE 5
#define ITEM_SUNSTONE_MEDALLION 8
#define ITEM_HIROTTA_STONE 3

/* ------------------------------------------ prisoners ($51)
 * data 1: Egle in the Radactian Castle. His cell opens once Alex has broken
 * the two cell blocks (v_storyEventCounter == 2, counted by the name-table
 * changers), after a $40-frame delay (unknown6).
 * data 0: Princess Lora in Janken's castle; her cell opens as soon as Alex
 * walks on her screen (a metatile of the name table is replaced).
 * Opening: 1000 points, v_collectedItemFlags bit 0 set (so the prisoner is
 * not respawned), then the character talks. Lora also spawns the rice ball
 * that ends the level; Egle takes the letter back (its collected flag is
 * cleared so that it can be found again). */
#define EGLE_CELL_SPRITE 0x830B
#define EGLE_FREE_SPRITE 0x8331
#define LORA_CELL_SPRITE 0x815C
#define EGLE_ANIMATION 0x8306
#define LORA_ANIMATION 0x812F
#define CELL_OPEN_DELAY 0x40
#define CELL_BLOCKS_TO_BREAK 2
#define LORA_CELL_METATILE_NAMETABLE 0xCE84
#define LORA_CELL_METATILE 0x8B5D      /* in bank 5 */

static void show_freed_prisoner(Entity *e) {
    e->spriteDescriptorPointer = e->data ? EGLE_FREE_SPRITE : CHARACTER_SPRITE;
}

/* $5FAA */
LIFTED(updateEntity0x51, 0x5FAA) {
    Entity *e = entity_at(cpu.ix);
    if (e->flags & EF_INITIALIZED) {
        show_freed_prisoner(e);
        LIFTED_RETURN();
    }
    if (ram8(v_collectedItemFlags) != 0) TAIL_CALL(f_destroyCurrentEntity);

    if (!(e->flags & EF_DESTROY_OFFSCREEN)) { /* here: "cell visible" */
        e->spriteDescriptorPointer = e->data ? EGLE_CELL_SPRITE : LORA_CELL_SPRITE;
        if (is_offscreen(e)) LIFTED_RETURN();
        e->flags |= EF_DESTROY_OFFSCREEN;
        e->unknown6 = CELL_OPEN_DELAY;
    }

    if (e->data != 0) {
        /* Egle: wait for the blocks, then for the delay. (QUIRK: the original
         * re-tests the initialized flag here, which is always clear on this
         * path; the dead test is dropped.) */
        if (ram8(v_storyEventCounter) != CELL_BLOCKS_TO_BREAK) LIFTED_RETURN();
        if (--e->unknown6 != 0) {
            show_freed_prisoner(e);
            LIFTED_RETURN();
        }
    } else {
        /* Lora: open the cell when Alex walks on a still screen. */
        if (ram8(v_scrollFlags) & (uint8_t)~SCROLL_VERTICAL) LIFTED_RETURN();
        if (entity_at(SLOT_ALEX)->state != ALEX_WALKING) LIFTED_RETURN();
        ram8(v_nametableChangeRequest) = 0x80;
        ram16(v_nametableChangeDestination) = LORA_CELL_METATILE_NAMETABLE;
        wr8(0xFFFF, 0x85); /* the metatile address below refers to bank 5 */
        ram16(nametableChangeSourceMetatile) = LORA_CELL_METATILE;
        wr8(0xFFFF, 0x82);
    }

    /* The cell opens. */
    e->flags |= EF_INITIALIZED;
    cpu.l = SCORE_1000;
    CALL_ROUTINE(f_addScore);
    ram8(v_storyEventCounter) = 0;
    ram8(v_collectedItemFlags) |= 1;
    if (e->data == 0) {
        Entity *rice_ball = entity_at(SLOT_BLOCK_DEBRIS);
        rice_ball->type = ENTITY_RICE_BALL;
        set_x_pixel(rice_ball, 0x80);
        set_y_pixel(rice_ball, 0x80);
        cpu.iy = SLOT_BLOCK_DEBRIS;
        cpu.a = TXT_PRINCESS_LORA;
        cpu.hl = LORA_ANIMATION;
    } else {
        ram8(_RAM_D802_) &= (uint8_t)~1; /* Letter to Nibana can be collected again */
        cpu.a = TXT_EGLE;
        cpu.hl = EGLE_ANIMATION;
    }
    TAIL_CALL(f__LABEL_60D4_);
}

/* $6077: the elder of the Village of Namui ($50). Silent while the bull
 * guards the village (v_storyEventCounter != 0). Then he talks once, spawns
 * the rice ball in slot 6, and on the next frame plays the star-box jingle. */
#define ELDER_SPRITE 0x802F
#define ELDER_ANIMATION 0x802A

LIFTED(updateEntity0x50, 0x6077) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = ELDER_SPRITE;
    if (ram8(v_storyEventCounter) != 0) LIFTED_RETURN();
    if (!(e->flags & EF_INITIALIZED)) {
        set_y_pixel(e, 0x88);
        Entity *rice_ball = entity_at(SLOT_OPPONENT);
        rice_ball->type = ENTITY_RICE_BALL;
        set_x_pixel(rice_ball, 0x98);
        set_y_pixel(rice_ball, 0x60);
        cpu.iy = SLOT_OPPONENT;
        cpu.a = TXT_VILLAGE_ELDER;
        cpu.hl = ELDER_ANIMATION;
        TAIL_CALL(f__LABEL_60D4_);
    }
    if (e->flags & EF_DESTROY_OFFSCREEN) LIFTED_RETURN();
    e->flags |= EF_DESTROY_OFFSCREEN; /* here: "jingle played" */
    ram8(v_soundControl) = SOUND_STAR_BOX;
    LIFTED_RETURN();
}

/* $60B5: Saint Nurari ($45) on his island: talks once the screen stopped
 * scrolling, then gives the Sunstone Medallion (story item 8 in slot 27). */
#define NURARI_SPRITE 0x82DE
#define NURARI_ANIMATION 0x82D9

LIFTED(updateSaintNurari, 0x60B5) {
    Entity *e = entity_at(cpu.ix);
    e->spriteDescriptorPointer = NURARI_SPRITE;
    if (!(e->flags & EF_INITIALIZED)) {
        if (ram8(v_scrollFlags) != 0) LIFTED_RETURN();
        cpu.a = TXT_SAINT_NURARI;
        cpu.hl = NURARI_ANIMATION;
        TAIL_CALL(f__LABEL_60D4_);
    }
    if (e->flags & EF_DESTROY_OFFSCREEN) LIFTED_RETURN();
    e->flags |= EF_DESTROY_OFFSCREEN; /* here: "gift given" */
    Entity *gift = entity_at(SLOT_THOUGHT_OPPONENT);
    gift->type = ENTITY_STORY_ITEM;
    set_x_pixel(gift, 0x72);
    set_y_pixel(gift, 0x70);
    gift->data = ITEM_SUNSTONE_MEDALLION;
    cpu.iy = SLOT_THOUGHT_OPPONENT;
    LIFTED_RETURN();
}

/* $60CD: a character (IX) starts talking: A = message, HL = animation
 * descriptor shown while the text box is open. */
LIFTED(_LABEL_60D4_, 0x60CD) {
    entity_at(cpu.ix)->flags |= EF_INITIALIZED;
    ram8(v_textBoxMessageIndex) = cpu.a;
    ram8(v_shouldShowNuraiOrOldMan) = 1;
    ram8(v_gameState) = STATE_TEXT_BOX;
    ram16(v_nuraiOrOldManEntityAnimationDescriptorTemporaryPointer) = cpu.hl;
    ram16(v_nuraiOrOldManEntityTemporaryPointer) = cpu.ix;
    LIFTED_RETURN();
}

/* $6106: story item pick-up ($52, item index in data). Not spawned again
 * once collected. Collecting sets its "collected" flag, then: item 0 starts
 * the bonus level, item 5 gives a life, the others set their v_has... flag. */
LIFTED(updateEntity0x52, 0x6106) {
    Entity *e = entity_at(cpu.ix);
    uint16_t collected_flag = rom_word_table(ITEM_COLLECTED_FLAGS, e->data);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        if (rd8(collected_flag) != 0) TAIL_CALL(f_destroyCurrentEntity);
        e->spriteDescriptorPointer = rom_word_table(ITEM_SPRITES, e->data);
        LIFTED_RETURN();
    }
    if (is_offscreen(e)) LIFTED_RETURN();
    e->flags |= EF_DESTROY_OFFSCREEN;
    cpu.iy = SLOT_ALEX;
    CALL_ROUTINE(f_checkEntityCollision);
    if (carry_set()) LIFTED_RETURN();

    ram8(v_soundControl) = SOUND_POWERUP;
    wr8(collected_flag, (uint8_t)(rd8(collected_flag) | 1));
    if (e->data == ITEM_BONUS_LEVEL) {
        ram8(v_gameState) = STATE_BONUS_LEVEL;
        LIFTED_RETURN();
    }
    if (e->data == ITEM_EXTRA_LIFE) {
        ram8(v_lives) = bcd_increment(ram8(v_lives));
    } else {
        uint16_t owned_flag = rom_word_table(ITEM_OWNED_FLAGS, e->data);
        wr8(owned_flag, (uint8_t)(rd8(owned_flag) | 1));
    }
    TAIL_CALL(f_destroyCurrentEntity);
}

/* $616F: King High Stone of Nibana ($53, invisible, drawn in the
 * background). Talks once the screen stopped scrolling; if Alex brings the
 * Letter to Nibana he then gives the Hirotta Stone, otherwise a ghost
 * appears. */
LIFTED(updateEntity0x53, 0x616F) {
    Entity *e = entity_at(cpu.ix);
    bool has_letter = ram8(v_hasLetterToNibana) != 0;
    if (!(e->flags & EF_INITIALIZED)) {
        e->spriteDescriptorPointer = NULL_SPRITE;
        if (ram8(v_scrollFlags) != 0) LIFTED_RETURN();
        e->flags |= EF_INITIALIZED;
        ram8(v_gameState) = STATE_TEXT_BOX;
        ram8(v_textBoxMessageIndex) = has_letter ? TXT_KING_HIGH_STONE : TXT_KING_HIGH_STONE_NO_LETTER;
        LIFTED_RETURN();
    }
    if (e->flags & EF_DESTROY_OFFSCREEN) LIFTED_RETURN();
    e->flags |= EF_DESTROY_OFFSCREEN; /* here: "reward given" */
    Entity *reward = entity_at(SLOT_THOUGHT_OPPONENT);
    if (has_letter) {
        reward->type = ENTITY_STORY_ITEM;
        reward->data = ITEM_HIROTTA_STONE;
        set_x_pixel(reward, 0x58);
        set_y_pixel(reward, 0x88);
    } else {
        reward->type = ENTITY_GHOST;
        set_x_pixel(reward, 0xD8);
        set_y_pixel(reward, 0x30);
    }
    cpu.iy = SLOT_THOUGHT_OPPONENT;
    LIFTED_RETURN();
}

/* $6279: invisible bonus-level entrance ($4C): copies its data into
 * v_storyEventCounter when set up, and starts the bonus level when Alex
 * touches it. */
LIFTED(updateEntity0x4C, 0x6279) {
    Entity *e = entity_at(cpu.ix);
    if (!(e->flags & EF_INITIALIZED)) {
        e->flags |= EF_INITIALIZED;
        e->spriteDescriptorPointer = NULL_SPRITE;
        ram8(v_storyEventCounter) = e->data;
    }
    if (is_offscreen(e)) LIFTED_RETURN();
    cpu.iy = SLOT_ALEX;
    CALL_ROUTINE(f_checkEntityCollision);
    if (carry_set()) LIFTED_RETURN();
    ram8(v_gameState) = STATE_BONUS_LEVEL;
    TAIL_CALL(f_destroyCurrentEntity);
}
