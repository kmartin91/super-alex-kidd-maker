/*
 * The shop (game state 5, $1BC9-$1F2F).
 *
 * Entering a shop door (gameplay sets state 5): the level's variables and the
 * RAM name table are saved, Alex is saved in map entity slot 2 and put at the
 * shop entrance, and the shop screen is drawn: the shopkeeper, the level's
 * three items (unless already bought: records at $D7D0) and the prices. The
 * welcome (or "sold out") text box opens right away.
 *
 * Buying: Alex selects an item by punching it (the item entity sets
 * v_shopSelectedItemIndex). The price is taken from the wallet if possible
 * and the item given; a text box says so (or that the money is missing).
 * The next purchase is possible once Alex is back on the ground.
 *   1 cane of flight, 2 teleport powder, 3 magic capsule A, 4 magic capsule B,
 *   5 telepathy ball, 6 power bracelet (v_has* flags),
 *   7 motorcycle, 9 peticopter (v_alexActionState), 8 extra life.
 * The interrupt handler redraws the money and removes the picture of the item
 * just bought from the RAM name table.
 *
 * Walking out of the door sets bit 6 of v_gameState: everything is restored
 * and gameplay resumes ($8A) where Alex entered.
 */
#include "states.h"

/* Bank 0 tables. */
#define SHOP_PRICE_ROW 0x1F30          /* 18 bytes copied to the RAM name table ($CD44) */
#define SHOP_PALETTE 0x1F42
#define SHOP_ITEM_FLAG_POINTERS 0x1F62 /* item 1-6: pointer to its v_has* flag */
#define SHOP_PRICES 0x1F6E             /* item 1-9: 3-byte BCD price */
#define SHOP_STOCK_POINTERS 0x1F89     /* level 1-17: 3 x (name-table destination, picture) */
#define SHOP_SOLD_ITEM_POINTERS 0x1FAB /* level 1-17: 3 x (item, v_has* flag, picture position) */
/* Bank 5. */
#define SHOP_NAMETABLE 0x908E
#define SHOP_TILES_1 0x93F3
#define SHOP_TILES_2 0x9840
#define SHOPKEEPER_NAMETABLE 0x9800
#define MAGIC_CAPSULE_TILES 0xAF11
#define ALEX_STATE_TILES 0xB0B1
#define TEXT_CHARACTER_TILES 0xB2B1
/* Bank 2. */
#define NUMBER_TILES 0xB385

#define SHOP_STOCK _RAM_D7D0_ /* 3 x (sold flag, name-table destination, picture), then $FF */
#define SHOP_STOCK_RECORD_SIZE 5
#define SAVED_ALEX MAP_ENTITY_SLOT(2) /* Alex while he is in the shop */
#define SHOP_DOOR_OFFSET 0x28
#define SHOP_DOOR_NAMETABLE_POINTER _RAM_CC06_

/* v_shopFlags */
#define SHOP_FLAG_IN_SHOP 0x01
#define SHOP_FLAG_WAIT_LANDING 0x40 /* a purchase message was shown */

/* Text box messages (textPointers). */
#define TXT_SHOP_WELCOME 0x01
#define TXT_SHOP_INSUFFICIENT_FUNDS 0x02
#define TXT_SHOP_ITEM_PURCHASED 0x03
#define TXT_SHOP_SOLD_OUT 0x16

enum ShopItem {
    ITEM_CANE_OF_FLIGHT = 1,
    ITEM_TELEPORT_POWDER,
    ITEM_MAGIC_CAPSULE_A,
    ITEM_MAGIC_CAPSULE_B,
    ITEM_TELEPATHY_BALL,
    ITEM_POWER_BRACELET,
    ITEM_MOTORCYCLE,
    ITEM_EXTRA_LIFE,
    ITEM_PETICOPTER,
};

static void copy_to_vram(uint16_t src, uint16_t vram_dst, uint16_t count) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.bc = count;
    CALL_HELPER(f_copyBytesToVRAM);
}

/* Sets bit 0 of the v_has* flag of an item (1-6); leaves BC = 2 * item. */
static void give_item(uint8_t item) {
    cpu.hl = load_ath_pointer(SHOP_ITEM_FLAG_POINTERS - 2, item);
    wr8(cpu.hl, (uint8_t)(rd8(cpu.hl) | 0x01));
}

/* _LABEL_1D04_: enter the shop. */
static void enter_shop(void) {
    ram8(v_gameState) |= STATE_INITIALIZED;
    CALL_HELPER(f_reset_9DF3); /* audioEngine.reset */
    /* Save the level variables, then clear them. */
    copy_bytes(v_temporaryLevelDataCopy, v_levelWidth, 0x2A);
    fill_bytes(v_levelWidth, 0x00, 0x2A);
    CALL_HELPER(f_clearVDPTablesAndDisableScreen);
    cpu.b = 5;
    CALL_HELPER(f_sleepTenthsOfSecond);
    CALL_HELPER(f_clearScroll);
    copy_bytes(v_nametableCopy, v_nametable, 0x700);

    map_bank(BANK(5));
    cpu.hl = SHOP_NAMETABLE;
    cpu.de = v_nametable;
    CALL_HELPER(f_decompressNametable);
    ram8(v_entitydataArrayLength) = 1;
    fill_bytes(v_mapEntities, 0x00, 0x60);
    map_bank(BANK(2));
    copy_bytes(SAVED_ALEX, v_alex, ENTITY_SIZE);
    cpu.bc = 0; /* as left by the LDIR: C is an input of updateEntities */
    cpu.ix = v_alex;
    Entity *alex = entity_at(v_alex);
    alex->type = ENTITY_ALEX;
    X_PIXEL(alex) = 0x20;
    Y_PIXEL(alex) = 0x88;
    CALL_HELPER(f_updateAlexSpawning);
    CALL_HELPER(f_updateEntities);

    /* An item paid for but not given yet (left the shop too early). */
    if (ram8(v_shopSelectedItemIndex)) {
        give_item(ram8(v_shopSelectedItemIndex));
        ram8(v_shopSelectedItemIndex) = 0;
    }

    map_bank(BANK(2));
    cpu.de = VDP_VRAM_WRITE(0x1800);
    cpu.hl = NUMBER_TILES;
    cpu.bc = 0x0050;
    cpu.a = 0x01;
    CALL_HELPER(f_load1bppTiles);
    map_bank(BANK(5));
    cpu.hl = SHOP_TILES_1;
    cpu.de = VDP_VRAM_WRITE(0x0520);
    CALL_HELPER(f_decompressTilesToVram);
    cpu.hl = SHOP_TILES_2;
    cpu.de = VDP_VRAM_WRITE(0x0E00);
    CALL_HELPER(f_decompressTilesToVram);
    cpu.de = _RAM_CB08_;
    cpu.hl = SHOPKEEPER_NAMETABLE;
    cpu.bc = 0x0808;
    CALL_HELPER(f_copyTileBlock);
    copy_to_vram(MAGIC_CAPSULE_TILES, VDP_VRAM_WRITE(0x1200), 0x01C0);
    copy_to_vram(0xB291, VDP_VRAM_WRITE(0x1FE0), 0x0020);
    copy_to_vram(ALEX_STATE_TILES, VDP_VRAM_WRITE(0x1420), 0x01E0);
    copy_to_vram(SHOP_PALETTE, VDP_CRAM_WRITE(0), 0x0020);

    ram8(v_shopFlags) = SHOP_FLAG_IN_SHOP;
    /* Remember the door Alex came in by; the shop's own door is at x = $28. */
    ram8(v_horizontalPositionShopHasBeenEnteredFrom) = ram8(v_shopDoorOffset);
    ram8(v_shopDoorOffset) = SHOP_DOOR_OFFSET;
    ram16(v_shopEntranceEnteredFromDoorNametablePointer) = ram16(v_shopDoorNametablePointer);
    ram16(v_shopDoorNametablePointer) = SHOP_DOOR_NAMETABLE_POINTER;

    ram8(v_textBoxMessageIndex) = TXT_SHOP_SOLD_OUT;
    bool in_stock = false;
    for (int i = 0; i < 3; i++)
        if (ram8(SHOP_STOCK + i * SHOP_STOCK_RECORD_SIZE) == 0) in_stock = true;
    if (in_stock) {
        ram8(v_textBoxMessageIndex) = TXT_SHOP_WELCOME;
        ram8(v_itemBeignBoughtIndex) = 0;
        vdp_set_address(VDP_REGISTER(0, 0x06)); /* line interrupts off */
        map_bank(BANK(6));
        /* Stock records: (sold flag, destination, picture) x 3, then $FF. */
        uint16_t src = load_ath_pointer(SHOP_STOCK_POINTERS - 2, ram8(v_level));
        for (int i = 0; i < 3; i++)
            copy_bytes((uint16_t)(SHOP_STOCK + 1 + i * SHOP_STOCK_RECORD_SIZE),
                       (uint16_t)(src + i * 4), 4);
        ram8(SHOP_STOCK + 3 * SHOP_STOCK_RECORD_SIZE) = 0xFF;
        /* Draw the pictures of the items not sold yet (3 rows of 6 bytes). */
        for (uint16_t record = SHOP_STOCK; rd8(record) != 0xFF; record += SHOP_STOCK_RECORD_SIZE) {
            if (rd8(record) != 0) continue;
            cpu.de = rd16((uint16_t)(record + 1));
            cpu.hl = rd16((uint16_t)(record + 3));
            cpu.bc = 0x0306;
            CALL_HELPER(f_copyTileBlock);
        }
    }

    /* _LABEL_1E77_ */
    copy_bytes(_RAM_CD44_, SHOP_PRICE_ROW, 0x12);
    copy_to_vram(v_nametable, VDP_VRAM_WRITE(0x3800), 0x0600);
    cpu.hl = v_money + 2;
    cpu.de = NAMETABLE_WRITE(4, 21);
    CALL_HELPER(f_drawThreeBcdBytes);
    map_bank(BANK(2));
    enable_interrupts();
    CALL_HELPER(f_enableDisplay);
    ram8(v_soundControl) = level_song(ram8(v_level));
}

/* _LABEL_1C33_: Alex walked out: back to the level. */
static void leave_shop(void) {
    CALL_HELPER(f_reset_9DF3); /* audioEngine.reset */
    CALL_HELPER(f_clearVDPTablesAndDisableScreen);
    map_bank(BANK(2));
    ram8(v_gameState) = STATE_GAMEPLAY | STATE_INITIALIZED;
    copy_bytes(v_nametable, v_nametableCopy, 0x700);
    copy_to_vram(v_nametable, VDP_VRAM_WRITE(0x3800), 0x0700);
    CALL_HELPER(f_updateVdpAddressAfterDraw);
    copy_bytes(v_levelWidth, v_temporaryLevelDataCopy, 0x2A);
    ram8(v_soundControl) = level_song(ram8(v_level));
    ram8(v_entitydataArrayLength) = ENTITY_ARRAY_SIZE;
    copy_bytes(v_alex, SAVED_ALEX, ENTITY_SIZE);
    cpu.bc = 0; /* as left by the LDIR: C is an input of updateEntities */
    cpu.ix = v_alex;
    CALL_HELPER(f_updateAlexSpawning);
    CALL_HELPER(f_updateEntities);

    CALL_HELPER(f_loadLevelPalette);
    map_bank(BANK(3));
    CALL_HELPER(f_loadLevelTiles);
    map_bank(BANK(5));
    cpu.hl = TEXT_CHARACTER_TILES;
    cpu.de = VDP_VRAM_WRITE(0x1600);
    CALL_HELPER(f_decompressTilesToVram);
    map_bank(BANK(2));
    uint8_t action = ram8(v_alexActionState);
    if (action >= ACTION_RIDING_MOTORCYCLE && action != ACTION_RIDING_BOAT) {
        /* Motorcycle / peticopter bullet tiles. */
        map_bank(BANK(7));
        copy_to_vram(0x9B29, VDP_VRAM_WRITE(0x2200), 0x0020);
        copy_to_vram(0x9429, VDP_VRAM_WRITE(0x2220), 0x01C0);
    }

    ram8(v_shopDoorOffset) = ram8(v_horizontalPositionShopHasBeenEnteredFrom);
    ram16(v_shopDoorNametablePointer) = ram16(v_shopEntranceEnteredFromDoorNametablePointer);
    ram8(v_textBoxMessageIndex) = 0;
    ram8(v_shopFlags) = 0;
    ram8(v_shopSelectedItemIndex) = 0;
    map_bank(BANK(2));
    cpu.de = VDP_REGISTER(0, 0x26);
    vdp_set_address(VDP_REGISTER(0, 0x26));
    enable_interrupts();
    wait_frame(IRQ_SPRITES_AND_STATE);
    CALL_HELPER(f_enableDisplay);
    cpu.b = 10;
}

/* _LABEL_1EAF_: pays for the item Alex selected and gives it. */
static void buy_selected_item(void) {
    cpu.hl = v_shopFlags;
    if (ram8(v_shopFlags) & SHOP_FLAG_WAIT_LANDING) {
        if (entity_at(v_alex)->state == ALEX_IN_AIR) return;
        ram8(v_shopFlags) &= (uint8_t)~SHOP_FLAG_WAIT_LANDING;
        ram8(v_shopSelectedItemIndex) = 0;
    }
    uint8_t item = ram8(v_shopSelectedItemIndex);
    if (item == 0) return;

    /* Can Alex pay? (money - price without storing it: carry = too poor) */
    cpu.hl = (uint16_t)(SHOP_PRICES - 3 + (uint8_t)(item * 3));
    cpu.bc = v_money;
    CALL_HELPER(f_subtractBCDToA);
    if (cpu.f & FLAG_C) {
        ram8(v_shopSelectedItemIndex) = 0;
        ram8(v_itemBeignBoughtIndex) = 0;
        ram8(v_shopFlags) |= SHOP_FLAG_WAIT_LANDING;
        ram8(v_textBoxMessageIndex) = TXT_SHOP_INSUFFICIENT_FUNDS;
        return;
    }
    cpu.hl -= 2;
    cpu.bc -= 2;
    CALL_HELPER(f_subtractBCD);
    ram8(v_shopFlags) |= SHOP_FLAG_WAIT_LANDING;
    ram8(v_itemBeignBoughtIndex) = item; /* the interrupt handler removes its picture */

    if (item < ITEM_MOTORCYCLE) {
        give_item(item);
    } else if (item == ITEM_EXTRA_LIFE) {
        /* Lives are BCD: ADD 1 / DAA. */
        cpu.a = ram8(v_lives);
        alu_add(1);
        op_daa();
        ram8(v_lives) = cpu.a;
    } else {
        /* Motorcycle or peticopter. */
        ram8(v_alexActionState) = item;
        ram8(v_invincibilityTimer) = 0;
    }
    ram8(v_shopSelectedItemIndex) = 0;
    ram8(v_textBoxMessageIndex) = TXT_SHOP_ITEM_PURCHASED;
}

/* $1BC9: main-loop handler of state 5. */
LIFTED(updateShopState, 0x1BC9) {
    enter_state_handler();
    if (!(ram8(v_gameState) & STATE_INITIALIZED)) {
        enter_shop();
        LIFTED_RETURN();
    }
    if (ram8(v_gameState) & STATE_SHOP_EXIT) {
        leave_shop();
        TAIL_CALL(f_sleepTenthsOfSecond);
    }

    wait_frame(IRQ_SPRITES_AND_STATE);
    if (ram8(v_textBoxMessageIndex)) {
        /* A message is pending: open the text box (it returns to state $85). */
        ram8(v_gameState) = STATE_TEXT_BOX;
        LIFTED_RETURN();
    }
    CALL_ROUTINE(f_updateEntities);
    buy_selected_item();
    LIFTED_RETURN();
}

/* $1BEE: VBlank handler of the shop. */
LIFTED(handleInterruptShopState, 0x1BEE) {
    cpu.hl = v_money + 2;
    cpu.de = NAMETABLE_WRITE(4, 21);
    CALL_ROUTINE(f_drawThreeBcdBytes);
    CALL_ROUTINE(f_handleNametableChangeRequest);

    uint8_t bought = ram8(v_itemBeignBoughtIndex);
    if (bought == 0) LIFTED_RETURN();
    ram8(v_itemBeignBoughtIndex) = 0;

    /* Records (item, v_has* flag pointer, picture position) of the level. */
    map_bank(BANK(6));
    uint16_t record = load_ath_pointer(SHOP_SOLD_ITEM_POINTERS - 2, ram8(v_level));
    uint8_t wanted = bought;
    for (int i = 0; i < 3; i++) {
        if (rd8(record) == wanted) {
            uint16_t flag = rd16((uint16_t)(record + 1));
            wr8(flag, (uint8_t)(rd8(flag) | 0x01));
            cpu.de = rd16((uint16_t)(record + 3));
            cpu.bc = 0x0306;
            CALL_ROUTINE(f_clearRamNametableArea);
            /* QUIRK: the original keeps its registers from clearRamNametableArea:
             * the scan goes on from the last cleared row + 5 (a RAM name-table
             * address, not the next record) and compares with D as left by it
             * (high byte of the address after the cleared area) instead of the
             * item number. Harmless unless such a byte happens to match. */
            record = cpu.hl;
            wanted = cpu.d;
        }
        record = (uint16_t)(record + 5);
    }
    LIFTED_RETURN();
}
