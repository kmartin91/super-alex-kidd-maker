/*
 * Entities of a screen: spawning the records of a screen's entity stream
 * (docs/level-format.md section 7) and the castle entity loader.
 *
 * The stream itself is walked by _LABEL_6F7E_ ($6F77, module "states"): it
 * handles the special records and hands the normal records to $6F88.
 * A normal record is 4 bytes: type, y, x, data. x and y are pixel positions
 * inside the screen that is entering.
 */
#include "level.h"
#include "rt/maker.h"

#define FIRST_NORMAL_SLOT 0xC3C0      /* slot 6 (0-based): v_entities.7 */
#define NORMAL_SLOT_COUNT 10          /* slots 6-15 */

/* Writes the record that follows `stream` (type, y, x, data) into `slot`.
 * Returns the address of the record's last byte. The position is converted
 * to camera coordinates:
 *   xPos = x + fine horizontal scroll - v_newEntityHorizontalOffset (low byte:
 *          the scroll's sub-pixel byte), yPos = y + v_newEntityVerticalOffset;
 *   isOffScreenFlags = the page the entity is on ($C063/$C064, set by the
 *   stream walker from the scroll direction). */
static uint16_t spawn_entity_from_record(uint16_t slot, uint16_t stream) {
    Entity *e = entity_at(slot);
    e->type = rd8(++stream);
    entity_byte(slot, ENTITY_Y) = rd8(++stream);
    uint8_t x = rd8(++stream);
    uint16_t scroll = ram16(v_horizontalScroll);
    entity_byte(slot, ENTITY_X_SUBPIXEL) = (uint8_t)scroll;
    entity_byte(slot, ENTITY_X) = (uint8_t)(x + (scroll >> 8) - ram8(v_newEntityHorizontalOffset));
    e->data = rd8(++stream);
    uint16_t page = ram16(v_addedEntitiesShouldBeOffscreenHorizontally);
    entity_byte(slot, ENTITY_PAGE_Y) = (uint8_t)(page >> 8);
    entity_byte(slot, ENTITY_PAGE_X) = (uint8_t)page;
    entity_byte(slot, ENTITY_Y) = (uint8_t)(ram8(v_newEntityVerticalOffset) + entity_byte(slot, ENTITY_Y));
    return stream;
}

static uint16_t find_free_normal_slot(void) {
    uint16_t slot = FIRST_NORMAL_SLOT;
    for (int n = 0; n < NORMAL_SLOT_COUNT; n++, slot += ENTITY_SIZE)
        if (entity_at(slot)->type == 0) return slot;
    /* Maker mode (rt/maker.h): as many as the level wants, in the extra slots. */
    if (maker.active)
        for (slot = MAKER_EXTRA_RAM; slot < MAKER_EXTRA_RAM + MAKER_EXTRA_SLOTS * ENTITY_SIZE; slot += ENTITY_SIZE)
            if (entity_at(slot)->type == 0) return slot;
    return 0;
}

/* Spawns `count` records after `stream` (count 0 means 256), each into the
 * first free slot among 6-15; when none is free, the rest of the screen's
 * entities are dropped. C is the number of records left when the last slot
 * search started, as the original leaves it. Returns the stream position. */
/* Maker mode (rt/maker.h): the camera goes back and forth, so a screen can
 * enter again while entities it spawned are still alive: those are not
 * spawned twice. */
static bool maker_record_alive(uint16_t record) {
    for (uint16_t slot = FIRST_NORMAL_SLOT, n = 0; n < NORMAL_SLOT_COUNT; n++, slot += ENTITY_SIZE)
        if (entity_at(slot)->type != 0 && maker.slot_record[maker_slot_index(slot)] == record) return true;
    for (uint16_t slot = MAKER_EXTRA_RAM; slot < MAKER_EXTRA_RAM + MAKER_EXTRA_SLOTS * ENTITY_SIZE; slot += ENTITY_SIZE)
        if (entity_at(slot)->type != 0 && maker.slot_record[maker_slot_index(slot)] == record) return true;
    return false;
}

static uint16_t spawn_entities(uint16_t stream, uint8_t count) {
    do {
        cpu.c = count;
        if (maker.active && maker_record_alive((uint16_t)(stream + 1))) {
            stream = (uint16_t)(stream + 4);
            continue;
        }
        uint16_t slot = find_free_normal_slot();
        if (slot == 0) break;
        if (maker.active) maker.slot_record[maker_slot_index(slot)] = (uint16_t)(stream + 1);
        stream = spawn_entity_from_record(slot, stream);
    } while (--count != 0);
    return stream;
}

/* $6F88 (_LABEL_6F8F_): HL = stream (at the count byte), B = number of normal
 * records. out: HL = last byte read, C (see spawn_entities). */
LIFTED(_LABEL_6F8F_, 0x6F88) {
    cpu.hl = spawn_entities(cpu.hl, cpu.b);
    LIFTED_RETURN();
}

/* $6F9F (_LABEL_6FA6_): IX = slot to fill, HL = stream (before the record),
 * B = records left including this one: the rest go to free slots as above.
 * Used with B = 1 by the $81/$84 special records (fixed slots). */
LIFTED(_LABEL_6FA6_, 0x6F9F) {
    uint16_t stream = spawn_entity_from_record(cpu.ix, cpu.hl);
    uint8_t left = (uint8_t)(cpu.b - 1);
    if (left != 0) stream = spawn_entities(stream, left);
    cpu.hl = stream;
    LIFTED_RETURN();
}

/* $707D loadEntitiesSpecial: castle entity loader (levels 11 and 16, through
 * v_entityLoaderPointer). When a new room enters (NEW_SCREEN), the room's
 * entity index v_entityIndex = row * width + column is updated from the move:
 *   down (hole or SCROLL_DOWN): + width, entities placed relative to the fine
 *     vertical scroll (-$C0BC), coming from below (page $0100);
 *   up: - width, +$C0BC, from above ($FF00);
 *   left: - 1, from the left ($0001);  otherwise (right): + 1, from the right ($00FF);
 * and its stream (entitiesDescriptorsPointers table, bank 2 already mapped) is
 * handed to the stream walker _LABEL_6F7E_ with DE = page. */
LIFTED(loadEntitiesSpecial_LABEL_6F48_, 0x707D) {
    if (!(ram8(v_currentScreenNumber) & NEW_SCREEN)) LIFTED_RETURN();
    ram8(v_currentScreenNumber) &= (uint8_t)~NEW_SCREEN;
    ram8(v_newEntityVerticalOffset) = 0;

    uint8_t flags = ram8(v_scrollFlags);
    uint8_t index = ram8(v_entityIndex);
    uint16_t page;
    if (ram8(v_isScrollingDownToNextScreen) || (flags & SCROLL_DOWN)) {
        ram8(v_newEntityVerticalOffset) = (uint8_t)-ram8(v_verticalRowPixels);
        ram8(v_isScrollingDownToNextScreen) = 0;
        index = (uint8_t)(index + ram8(v_levelWidth));
        page = 0x0100;
    } else if (flags & SCROLL_UP) {
        ram8(v_newEntityVerticalOffset) = ram8(v_verticalRowPixels);
        index = (uint8_t)(index - ram8(v_levelWidth));
        page = 0xFF00;
    } else if (flags & SCROLL_LEFT) {
        index--;
        page = 0x0001;
    } else {
        index++;
        page = 0x00FF;
    }
    ram8(v_entityIndex) = index;

    /* rst $10: HL = stream of this room (also leaves A, BC as it does). */
    cpu.a = index;
    cpu.hl = ram16(v_entityDescriptorsPointer);
    CALL_ROUTINE(f_loadAthPointer);
    cpu.de = page;
    TAIL_CALL(f__LABEL_6F7E_);
}
