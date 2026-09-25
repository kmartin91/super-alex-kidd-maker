/*
 * Life lost, respawn and game over (game state 6, $6C05-$6F29).
 *
 * The state is entered once Alex's death animation is over. It takes a life
 * (lives are BCD) and:
 *  - during a demo, simply returns to the title screen;
 *  - with no life left, shows GAME OVER and the score for $C0 frames. Holding
 *    Up and pressing button 2 eight times buys a continue for $400: 3 lives
 *    and the level restarts from its beginning. Otherwise back to the title;
 *  - in levels that scroll vertically (except level 1), puts Alex back where
 *    the last checkpoint copy (temporaryAlexCopy) was taken;
 *  - in level 13, on a vehicle, or when he died flying the peticopter,
 *    restarts the level from the beginning (state $0A uninitialized);
 *  - otherwise searches the current screen for a free spot above solid
 *    ground, scanning columns from x=$10 and rows from y=$10 downwards, and
 *    respawns Alex there, invincible for a while.
 * Entities 7-16 whose unknown1 has bit 7 set are respawnable level entities:
 * they are reset to their initial type and position (table at $6F29).
 * Gameplay then resumes without reloading the level ($8A, or $89 in a janken
 * battle).
 *
 * During the respawn search Alex's entity fields hold the search state:
 *   unknown6 ($18) = y where each column scan starts, unknown5 ($16) = x of
 *   the column, battleDecision ($17) = width to test right of the spot (in 8
 *   pixel steps), state ($1A) = number of 8-pixel cells tested below,
 *   unknown7 ($19) = x saved while testing.
 */
#include "states.h"

#define RESPAWN_ENTITY_TABLE 0x6F29 /* bank 0: (type, x, y) of respawnable entities 1-4 */
#define CONTINUE_PRICE 0x048C       /* bank 0: the "400" entry of the score table */
#define GAME_OVER_DURATION 0xC0     /* frames the GAME OVER screen stays */
#define CONTINUE_PRESSES 8          /* button 2 presses (while holding Up) for a continue */
#define LEVEL_WITHOUT_RESPAWN 0x0D  /* dying here always restarts the level */

/* ------------------------------------------------ respawn spot search */

/* _LABEL_39ED_ run in the alternate register bank: tests `count` 8-pixel
 * cells from Alex's position (DE = $0100, A = 8) for solid terrain. The
 * original swaps banks with EXX around the call so that its loop counter in
 * B survives; BC', DE' and HL' keep the callee's results. */
static bool respawn_cells_blocked(uint8_t count) {
    op_exx();
    cpu.de = 0x0100;
    cpu.a = 0x08;
    cpu.b = count;
    CALL_HELPER(f__LABEL_39ED_);
    op_exx();
    return cpu.f & FLAG_C;
}

/* _LABEL_6EAF_ / _LABEL_6EBB_: moves Alex down from his current position
 * (or from the top of the search column when `start_of_column`) until the
 * area he would occupy is free, moving to the next column (x + 8) when the
 * bottom of the screen (y = $90) is reached, and giving up from x = $60. */
static void search_free_spot(Entity *alex, bool start_of_column) {
    for (;;) {
        if (start_of_column) {
            Y_PIXEL(alex) = alex->unknown6;
            X_PIXEL(alex) = alex->unknown5;
        }
        start_of_column = false;

        alex->unknown7 = X_PIXEL(alex); /* saved x */
        bool blocked = respawn_cells_blocked(alex->state);
        if (!blocked) {
            /* Also test the columns to the right. */
            uint8_t columns = alex->state;
            X_PIXEL(alex) = (uint8_t)(alex->unknown7 + 8);
            do {
                if (respawn_cells_blocked(alex->battleDecision)) {
                    blocked = true;
                    break;
                }
                X_PIXEL(alex) += 8;
            } while (--columns);
            X_PIXEL(alex) = alex->unknown7;
            if (!blocked) return;
        }

        Y_PIXEL(alex) += 8;
        if (Y_PIXEL(alex) < 0x90) continue;
        alex->unknown5 += 8;
        if (alex->unknown5 >= 0x60) return;
        start_of_column = true;
    }
}

/* $6EA8: restart the free-spot search at the top of the search column. */
LIFTED(_LABEL_6EAF_, 0x6EA8) {
    search_free_spot(entity_at(cpu.ix), true);
    LIFTED_RETURN();
}

/* $6EB4: continue the free-spot search from Alex's current position. */
LIFTED(_LABEL_6EBB_, 0x6EB4) {
    search_free_spot(entity_at(cpu.ix), false);
    LIFTED_RETURN();
}

/* $6F1A: compensates the fine vertical scroll (v_levelData_C0BC_ < 7) in the
 * y position of entity IX. */
LIFTED(_LABEL_6F21_, 0x6F1A) {
    uint8_t fine_scroll = ram8(v_levelData_C0BC_);
    if (fine_scroll < 7) {
        cpu.b = fine_scroll;
        Y_PIXEL(entity_at(cpu.ix)) -= fine_scroll;
    }
    LIFTED_RETURN();
}

/* Is the tile at (Alex x + (DE & $FF), Alex y + (DE >> 8)) solid? */
static bool solid_tile_at(uint16_t offset) {
    cpu.de = offset;
    CALL_HELPER(f_getNearEntityTileAttrWithOffset);
    return cpu.a & 0x80;
}

/* Scans the screen until Alex stands on two solid tiles with free space
 * above them. Starts at x = $10, y = $10; `swimming` Alex needs no ground. */
static void find_respawn_position(Entity *alex, bool swimming) {
    alex->unknown6 = 0x10;
    alex->unknown5 = 0x10;
    alex->battleDecision = 1;
    alex->state = swimming ? 2 : 1;
    CALL_HELPER(f__LABEL_6EAF_);
    if (swimming) return;
    for (;;) {
        if (solid_tile_at(0x1900) && solid_tile_at(0x1908)) return;
        Y_PIXEL(alex) += 8;
        if (Y_PIXEL(alex) < 0x90) {
            CALL_HELPER(f__LABEL_6EBB_);
        } else {
            /* Nothing in this column: next one. */
            alex->unknown5 += 8;
            CALL_HELPER(f__LABEL_6EAF_);
        }
    }
}

/* Entities 7-16 with bit 7 of unknown1 set go back to their initial type and
 * position; returns with the registers of the original loop. */
static void reset_respawnable_entities(void) {
    cpu.ix = ENTITY_SLOT(7);
    for (uint8_t left = 10; left; left--) {
        Entity *e = entity_at(cpu.ix);
        if (e->unknown1 & 0x80) {
            uint16_t initial = load_ath_pointer(RESPAWN_ENTITY_TABLE - 2, e->unknown1 & 0x7F);
            cpu.hl = initial;
            cpu.e = left;
            op_exx(); /* the counter and the pointer are kept in the main bank */
            CALL_HELPER(f_destroyCurrentEntity);
            op_exx();
            e->type = rd8(initial);
            X_PIXEL(e) = rd8((uint16_t)(initial + 1));
            Y_PIXEL(e) = rd8((uint16_t)(initial + 2));
            cpu.hl = (uint16_t)(initial + 2);
        }
        cpu.de = ENTITY_SIZE;
        cpu.ix = (uint16_t)(cpu.ix + ENTITY_SIZE);
    }
    cpu.b = 0;
}

/* The level restarts from its beginning (state $0A with bit 7 clear). */
static void restart_level(void) {
    destroy_entities(ENTITY_SLOT(1), ENTITY_ARRAY_SIZE);
    ram8(v_gameState) = STATE_GAMEPLAY;
}

/* _LABEL_6D73_: resumes gameplay (or the janken battle) with the right music. */
static void resume_gameplay(void) {
    CALL_HELPER(f_updateEntities);
    uint8_t song = ram8(v_level);
    if (ram8(v_currentLevelIsBonusLevel)) song++;
    ram8(v_soundControl) = level_song(song);
    if (ram8(v_level) != 0x10 && ram8(v_alexStateBeforeHit) == ALEX_SWIMMING)
        ram8(v_soundControl) = SOUND_UNDERWATER_SONG;
    if (ram8(v_hasBattleStarted)) ram8(v_soundControl) = SOUND_CASTLE_SONG;

    map_bank(BANK(2));
    wait_frame(IRQ_SPRITES);
    uint8_t next = ram8(v_hasBattleStarted) ? (STATE_JANKEN_GAME | STATE_INITIALIZED)
                                            : (STATE_GAMEPLAY | STATE_INITIALIZED);
    cpu.b = next;
    ram8(v_gameState) = next;
}

/* gameOver ($6DC2): GAME OVER screen, with the hidden continue. */
static void game_over(void) {
    CALL_HELPER(f_clearVDPTablesAndDisableScreen);
    cpu.b = 5;
    CALL_HELPER(f_sleepTenthsOfSecond);
    CALL_HELPER(f_clearScroll);
    /* Background and sprite colour 0 black. */
    vdp_set_address(VDP_CRAM_WRITE(0x00));
    vdp_write(0x00);
    vdp_set_address(VDP_CRAM_WRITE(0x10));
    vdp_write(0x00);

    cpu.de = NAMETABLE_WRITE(10, 10);
    cpu.hl = 0x6E84; /* "GAME OVER" */
    cpu.b = 9;
    CALL_HELPER(f_copyNametableEntriesToVRAM);
    cpu.de = NAMETABLE_WRITE(8, 14);
    cpu.hl = 0x6E8D; /* "SCORE       0" */
    cpu.b = 13;
    CALL_HELPER(f_copyNametableEntriesToVRAM);
    cpu.hl = v_score + 2;
    cpu.de = NAMETABLE_WRITE(14, 14);
    CALL_HELPER(f_drawThreeBcdBytes);

    ram8(v_VDPRegister0Value) = 0x26;
    vdp_set_address(VDP_REGISTER(0, 0x26));
    cpu.de = VDP_REGISTER(0, 0x26);
    ram8(v_soundControl) = SOUND_GAME_OVER_SONG;
    enable_interrupts();
    CALL_HELPER(f_enableDisplay);

    /* _RAM_C07F_ counts the frames left; v_itemBeignBoughtIndex counts the
     * button 2 presses while Up is held. */
    ram8(_RAM_C07F_) = GAME_OVER_DURATION;
    ram8(v_itemBeignBoughtIndex) = 0;
    for (;;) {
        wait_frame(IRQ_WAIT_ONLY);
        if (!(ram8(v_inputData) & JOY_UP)) {
            ram8(v_itemBeignBoughtIndex) = 0;
        } else if ((ram8(v_inputDataChanges) & JOY_BTN2) &&
                   ++ram8(v_itemBeignBoughtIndex) >= CONTINUE_PRESSES) {
            ram8(v_itemBeignBoughtIndex) = 0;
            /* Pay $400 if the money is there. */
            cpu.hl = CONTINUE_PRICE;
            cpu.bc = v_money;
            CALL_HELPER(f_subtractBCDToA); /* carry: not enough money */
            if (!(cpu.f & FLAG_C)) {
                cpu.hl -= 2;
                cpu.bc -= 2;
                CALL_HELPER(f_subtractBCD);
                disable_interrupts();
                cpu.de = NAMETABLE_WRITE(8, 17);
                cpu.hl = 0x6E9A; /* "CONTINUE MODE" */
                cpu.b = 13;
                CALL_HELPER(f_copyNametableEntriesToVRAM);
                enable_interrupts();
                ram8(v_soundControl) = SOUND_POWERUP;
                cpu.b = 30;
                CALL_HELPER(f_sleepTenthsOfSecond);
                ram8(v_lives) = 3;
                ram8(v_gameState) = STATE_GAMEPLAY;
                return;
            }
        }
        if (--ram8(_RAM_C07F_) == 0) break;
    }
    ram8(v_gameState) = STATE_TITLE;
}

/* $6C05: main-loop handler of state 6 (no EXX in this one). */
LIFTED(updateLifeLostState, 0x6C05) {
    if (ram8(v_inputFlags) & INPUT_FLAG_DEMO) {
        /* Demo: back to the title screen. */
        ram8(v_gameState) = STATE_TITLE;
        LIFTED_RETURN();
    }

    CALL_ROUTINE(f_disableDisplay);
    cpu.b = 5;
    CALL_ROUTINE(f_sleepTenthsOfSecond);
    CALL_ROUTINE(f_reset_9DF3); /* audioEngine.reset */

    /* Lives are BCD: SUB 1 / DAA. */
    cpu.hl = v_lives;
    cpu.a = ram8(v_lives);
    alu_sub(1);
    if (cpu.a == 0) {
        game_over();
        LIFTED_RETURN();
    }
    op_daa();
    ram8(v_lives) = cpu.a;

    map_bank(BANK(2));
    destroy_entities(ENTITY_SLOT(1), 5);   /* Alex and what he carries */
    destroy_entities(ENTITY_SLOT(17), 12); /* slots 17-28 */
    reset_respawnable_entities();

    Entity *alex = entity_at(v_alex);
    if ((ram8(v_scrollFlags) & SCROLL_VERTICAL) && ram8(v_level) != 1) {
        /* Vertical levels: back to the checkpoint copy of Alex. */
        copy_bytes(v_alex, temporaryAlexCopy, ENTITY_SIZE);
        cpu.bc = 0;
        cpu.de = v_alex + ENTITY_SIZE;
        cpu.hl = (uint16_t)(alex->spriteDescriptorPointer - 1);
        ram8(v_alexTilesIndex) = rd8(cpu.hl);
        ram8(v_alexActionState) = ACTION_INVINCIBLE;
        ram8(v_invincibilityTimer) = 0xFF; /* respawn grace period */
        resume_gameplay();
        TAIL_CALL(f_enableDisplay);
    }

    cpu.ix = v_alex;
    alex->type = ENTITY_ALEX;
    uint8_t before_hit = ram8(v_alexStateBeforeHit);
    if (ram8(v_level) == LEVEL_WITHOUT_RESPAWN || ram8(v_shouldSpawnRidingBoat_RAM_C051_) ||
        ram8(v_alexActionState) >= ACTION_RIDING_MOTORCYCLE ||
        before_hit == ALEX_FLYING_PETICOPTER) {
        restart_level();
        LIFTED_RETURN();
    }

    find_respawn_position(alex, before_hit == ALEX_SWIMMING);

    /* _LABEL_6D4F_: found. */
    ram8(v_alexActionState) = ACTION_INVINCIBLE;
    ram8(v_invincibilityTimer) = 0xFF;
    alex->unknown6 = 0;
    alex->unknown5 = 0;
    alex->battleDecision = 0;
    alex->unknown7 = 0;
    alex->state = ALEX_SPAWNING;
    CALL_ROUTINE(f__LABEL_6F21_);
    CALL_ROUTINE(f_updateAlexSpawning);
    resume_gameplay();
    TAIL_CALL(f_enableDisplay);
}

/* $6EA7: nothing to do at VBlank. */
LIFTED(handleInterruptLifeLostState, 0x6EA7) {
    LIFTED_RETURN();
}
