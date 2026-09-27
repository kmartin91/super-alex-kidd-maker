/*
 * Bonus zones of the Maker's levels (rt/maker.h MakerZone): an extra row of
 * the level's layout with its own entity lists, reached through doors
 * (entity $4C, invisible: data 0 goes into the zone, data 1 back).
 *
 * A door only reacts once Alex has been away from it (so that one he stands
 * on when he appears doesn't send him straight back). Taking one asks for the
 * game's bonus level state (8); its handler (states/bonus.c) then calls
 * maker_zone_transition, which:
 *   - into the zone: keeps what makes the main level (its variables, entity
 *     slots, name-table copy, maker-mode bookkeeping), then draws the zone
 *     like a plain horizontal level whose entity lists start at entity_base;
 *   - back: puts all that back, Alex where he took the door, and the door
 *     waits for him to step away.
 * What Alex gains in the zone (money, score, items, lives) is kept.
 */
#include <string.h>

#include "states.h"
#include "rt/maker.h"

void maker_zone_draw(void); /* level/layout.c */

#define ENTITY_SLOTS_BYTES 0x3C0 /* 30 slots of 32 bytes from v_entities */
#define EXTRA_SLOTS_BYTES (MAKER_EXTRA_SLOTS * ENTITY_SIZE)
#define NAMETABLE_COPY_BYTES 0x700
#define LEVEL_VARIABLES 0x2A     /* v_levelWidth.. */

/* RAM ranges of the main level kept while Alex is in the zone. */
static const struct { uint16_t address, size; } KEPT[] = {
    {v_VDPRegister0Value, 1},
    {v_entityDescriptorsPointer, 7},  /* $C061-$C067: entity lists, index, offsets */
    {v_levelScrollFlags, 0x0E},       /* $C080-$C08D: flags, bank, updaters, current screen */
    {v_newEntityHorizontalOffset, 1},
    {v_levelWidth, LEVEL_VARIABLES},  /* $C0A0-$C0C9: layout, scroll, camera */
    {v_entities, ENTITY_SLOTS_BYTES},
    {MAKER_EXTRA_RAM, EXTRA_SLOTS_BYTES},
    {v_nametable, NAMETABLE_COPY_BYTES},
};

static struct {
    uint8_t ram[1 + 7 + 0x0E + 1 + LEVEL_VARIABLES + ENTITY_SLOTS_BYTES + EXTRA_SLOTS_BYTES + NAMETABLE_COPY_BYTES];
    bool camera_both_ways, entered_from_left, start_screen_pending;
    uint8_t scroll_flags_set, after_start_screen;
    uint8_t slot_home[MAKER_SLOTS], slot_type[MAKER_SLOTS];
    uint16_t slot_record[MAKER_SLOTS];
} kept;

static void keep_main_level(void) {
    uint8_t *p = kept.ram;
    for (size_t i = 0; i < sizeof KEPT / sizeof KEPT[0]; i++) {
        memcpy(p, ram_ptr(KEPT[i].address), KEPT[i].size);
        p += KEPT[i].size;
    }
    kept.camera_both_ways = maker.camera_both_ways;
    kept.entered_from_left = maker.entered_from_left;
    kept.start_screen_pending = maker.start_screen_pending;
    kept.scroll_flags_set = maker.scroll_flags_set;
    kept.after_start_screen = maker.after_start_screen;
    memcpy(kept.slot_home, maker.slot_home, sizeof kept.slot_home);
    memcpy(kept.slot_type, maker.slot_type, sizeof kept.slot_type);
    memcpy(kept.slot_record, maker.slot_record, sizeof kept.slot_record);
}

static void restore_main_level(void) {
    const uint8_t *p = kept.ram;
    for (size_t i = 0; i < sizeof KEPT / sizeof KEPT[0]; i++) {
        memcpy(ram_ptr(KEPT[i].address), p, KEPT[i].size);
        p += KEPT[i].size;
    }
    maker.camera_both_ways = kept.camera_both_ways;
    maker.entered_from_left = kept.entered_from_left;
    maker.start_screen_pending = kept.start_screen_pending;
    maker.scroll_flags_set = kept.scroll_flags_set;
    maker.after_start_screen = kept.after_start_screen;
    memcpy(maker.slot_home, kept.slot_home, sizeof kept.slot_home);
    memcpy(maker.slot_type, kept.slot_type, sizeof kept.slot_type);
    memcpy(maker.slot_record, kept.slot_record, sizeof kept.slot_record);
}

static void copy_to_vram(uint16_t src, uint16_t vram_dst, uint16_t count) {
    cpu.hl = src;
    cpu.de = vram_dst;
    cpu.bc = count;
    CALL_ROUTINE(f_copyBytesToVRAM);
}

/* Doors waiting for Alex to step away before they work, by entity slot
 * (outside the entity's bytes, which the game uses up). */
static bool door_waits[MAKER_SLOTS];

void maker_zone_door_wait(uint16_t slot) {
    door_waits[maker_slot_index(slot)] = true;
}

/* Called by a door (enemies2/story.c) every frame it is on screen. */
void maker_zone_door(uint16_t slot, bool touching) {
    Entity *door = entity_at(slot);
    bool *waits = &door_waits[maker_slot_index(slot)];
    if (!touching) {
        *waits = false; /* Alex is away: the door works */
        return;
    }
    if (*waits) return;
    bool back = door->data != 0;
    if (back != maker.zone.inside) return; /* a door of the other area */
    maker.zone.request = back ? MAKER_ZONE_LEAVE : MAKER_ZONE_ENTER;
    if (!back) maker.zone.door = slot;
    ram8(v_gameState) = STATE_BONUS_LEVEL;
}

/* Common end of both transitions: Alex's sprites, the screen back on, play. */
static void resume(void) {
    cpu.bc = 0; /* C is an input of updateEntities */
    cpu.ix = v_alex;
    CALL_ROUTINE(f_updateEntities);
    vdp_set_address(VDP_REGISTER(0, ram8(v_VDPRegister0Value)));
    ram8(v_gameState) = STATE_GAMEPLAY | STATE_INITIALIZED;
    enable_interrupts();
    CALL_ROUTINE(f_enableDisplay);
}

static void enter_zone(void) {
    keep_main_level();
    uint16_t lists = ram16(v_entityDescriptorsPointer);
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
    destroy_entities(v_entities, ram8(v_entitydataArrayLength));
    fill_bytes(v_levelWidth, 0x00, LEVEL_VARIABLES);
    maker_zone_draw();
    ram16(v_entityDescriptorsPointer) = (uint16_t)(lists + 2 * maker.zone.entity_base);
    ram8(v_entityIndex) = 0;

    map_bank(BANK(2));
    Entity *alex = entity_at(v_alex);
    alex->type = ENTITY_ALEX;
    X_PIXEL(alex) = maker.zone.alex_x;
    Y_PIXEL(alex) = maker.zone.alex_y;
    cpu.ix = v_alex;
    CALL_ROUTINE(f_updateAlexSpawning);
    maker.zone.inside = true;
    ram8(v_VDPRegister0Value) = 0x26; /* the left column masked, as in plain levels */
    resume();
}

static void leave_zone(void) {
    CALL_ROUTINE(f_clearVDPTablesAndDisableScreen);
    destroy_entities(v_entities, ram8(v_entitydataArrayLength));
    restore_main_level();
    copy_to_vram(v_nametable, VDP_VRAM_WRITE(0x3800), NAMETABLE_COPY_BYTES);
    CALL_ROUTINE(f_updateVdpAddressAfterDraw);
    map_bank(BANK(2));
    /* The door waits until Alex steps away from it. */
    if (entity_at(maker.zone.door)->type == 0x4C) maker_zone_door_wait(maker.zone.door);
    maker.zone.inside = false;
    resume();
}

void maker_zone_transition(void) {
    uint8_t request = maker.zone.request;
    maker.zone.request = 0;
    if (request == MAKER_ZONE_ENTER) enter_zone();
    else leave_zone();
}
