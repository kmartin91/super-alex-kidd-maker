/*
 * VDP set-up, display on/off, scroll reset, sprite table upload and tile
 * decompression ($01D6-$0340, $04A1-$04CD).
 */
#include "game/core/core.h"

/* RAM copy of the sprite attribute table (v_tempSprites): 64 Y bytes at $C700,
 * then 64 (X, tile) pairs at $C780. Entity sprites start at $C706: the first
 * six entries are left to the game states (HUD, cursors...). */
#define SPRITE_Y_TABLE 0xC700
#define SPRITE_XT_TABLE 0xC780
/* Bytes hidden by clearSprites: the Y table, the unused $C740-$C77F and the
 * first 32 (X, tile) pairs. */
#define SPRITE_TABLE_CLEAR_SIZE 0xC0
#define SPRITES_ALWAYS_FIRST 0x11 /* sprites uploaded in order when flickering */

/* Writes `count` bytes (0 = 256) from `src` to `port` (Z80 OUTI loop). */
static void out_bytes(uint8_t port, uint16_t src, unsigned count) {
    if (count == 0) count = 256;
    while (count--) io_out(port, rd8(src++));
}

static void hide_all_sprites(void) {
    for (int i = 0; i < SPRITE_TABLE_CLEAR_SIZE; i++) ram8(SPRITE_Y_TABLE + i) = SPRITE_HIDDEN_Y;
}

/* Stores register 1 in its RAM copy and writes it to the VDP. Leaves the
 * registers as setVdpAddress does (DE = the command word, A = its high byte). */
static void write_vdp_register1(uint8_t value) {
    ram8(v_VDPRegister1Value) = value;
    cpu.de = VDP_REGISTER(1, value);
    vdp_set_address(cpu.de);
    cpu.a = cpu.d;
}

/* $01D6: expand 1-bit-per-pixel characters to 4-bitplane tiles. A = colour
 * (bitplanes to fill), BC = source bytes (0 = 65536), HL = source, DE = VDP
 * command word. Each source byte is written once per bitplane, or 0 for the
 * planes whose colour bit is clear. */
LIFTED(load1bppTiles, 0x01D6) {
    uint8_t colour = cpu.a;
    uint32_t count = bc_count(cpu.bc);
    uint16_t src = cpu.hl;
    uint8_t pixels = 0, plane_value = 0;
    ram8(v_1bppTileColor) = colour;
    vdp_set_address(cpu.de);
    while (count--) {
        pixels = rd8(src++);
        for (int plane = 0; plane < 4; plane++) {
            plane_value = (colour >> plane) & 1 ? pixels : 0;
            vdp_write(plane_value);
        }
    }
    cpu.hl = src;
    cpu.bc = 0;
    cpu.a = 0;
    /* The inner loop runs on the alternate registers (EXX): B'C' end as the
     * loop counter and port, D' and H' as the last byte written and read. */
    cpu.bc_ = VDP_DATA;
    cpu.de_ = (uint16_t)((plane_value << 8) | (cpu.de_ & 0xFF));
    cpu.hl_ = (uint16_t)((pixels << 8) | (cpu.hl_ & 0xFF));
    LIFTED_RETURN();
}

/* Upload of the whole sprite table in RAM order, from the point where the
 * VDP address is set to the Y table: `y_count` Y bytes from `src`, then the
 * 64 (X, tile) pairs. */
static void upload_sprites_in_order(uint8_t port, uint16_t src, uint8_t y_count) {
    out_bytes(port, src, y_count);
    cpu.de = VDP_VRAM_WRITE(VRAM_SPRITE_TABLE + 0x80);
    vdp_set_address(cpu.de);
    out_bytes(port, SPRITE_XT_TABLE, 0x80);
    cpu.a = cpu.d;
    cpu.b = 0;
    cpu.c = port;
    cpu.hl = SPRITE_XT_TABLE + 0x80;
}

/* $0212 (updateSprites@oddYLoop): rest of the in-order upload; the VDP address
 * is already set, B = Y bytes left (0 = 256) at HL, C = data port. */
LIFTED(updateSprites_oddYLoop, 0x0212) {
    upload_sprites_in_order(cpu.c, cpu.hl, cpu.b);
    LIFTED_RETURN();
}

static void upload_all_sprites(void) {
    cpu.de = VDP_VRAM_WRITE(VRAM_SPRITE_TABLE);
    vdp_set_address(cpu.de);
    upload_sprites_in_order(VDP_DATA, SPRITE_Y_TABLE, 64);
}

/* $0208 (updateSprites@oddUpdate): upload the RAM sprite table as it is. */
LIFTED(updateSprites_oddUpdate, 0x0208) {
    upload_all_sprites();
    LIFTED_RETURN();
}

/* $01F7: copy the RAM sprite table to VRAM (from the VBlank interrupt).
 * During play, every other frame the entity sprites after the 17th are sent
 * in reverse order, so that when more than 8 share a line (the VDP drops the
 * later ones) a different subset flickers instead of always the same. */
LIFTED(updateSprites, 0x01F7) {
    bool flicker_frame = false;
    if ((ram8(v_gameState) & GAME_STATE_MASK) >= STATE_DEMO) {
        ram8(v_spriteFlickeringCounter)++;
        flicker_frame = !(ram8(v_spriteFlickeringCounter) & 1);
    }
    /* v_spriteTerminatorPointer = address of the Y slot after the last sprite. */
    uint8_t end = ram8(v_spriteTerminatorPointer);
    if (!flicker_frame || end < SPRITES_ALWAYS_FIRST + 2) {
        upload_all_sprites();
        LIFTED_RETURN();
    }

    /* Y bytes: the first 17 in order, the others from the last one back. */
    uint16_t page = ram16(v_spriteTerminatorPointer) & 0xFF00;
    vdp_set_address(VDP_VRAM_WRITE(VRAM_SPRITE_TABLE));
    out_bytes(VDP_DATA, SPRITE_Y_TABLE, SPRITES_ALWAYS_FIRST);
    uint8_t reversed = (uint8_t)(end - SPRITES_ALWAYS_FIRST);
    uint16_t y = (uint16_t)(page | (uint8_t)(end - 1));
    for (unsigned i = 0; i < reversed; i++) vdp_write(rd8(y--));
    vdp_write(SPRITE_LIST_END);

    /* (X, tile) pairs in the same order. The original derives the count from
     * the pair address: the same number as above while end <= $3F (always, as
     * updateEntities clamps it). */
    vdp_set_address(VDP_VRAM_WRITE(VRAM_SPRITE_TABLE + 0x80));
    out_bytes(VDP_DATA, SPRITE_XT_TABLE, SPRITES_ALWAYS_FIRST * 2);
    uint8_t xt_end = (uint8_t)((end << 1) | 0x80);
    uint8_t pair_bytes = (uint8_t)(xt_end - (SPRITE_XT_TABLE + SPRITES_ALWAYS_FIRST * 2));
    unsigned pairs = pair_bytes ? pair_bytes / 2 : 128;
    uint16_t xt = (uint16_t)(page | xt_end);
    for (unsigned i = 0; i < pairs; i++) {
        xt = core_inc_low(xt, (uint8_t)-2);
        vdp_write(rd8(xt));
        vdp_write(rd8((uint16_t)(xt + 1)));
    }
    cpu.b = 0;
    LIFTED_RETURN();
}

/* $026B: initial VDP registers (table at $027D: 10 register writes and a CRAM
 * address), then black as sprite colour 0. */
LIFTED(initVDPRegisters, 0x026B) {
    for (int i = 0; i < 22; i++) io_out(VDP_CONTROL, rd8(INITIAL_VDP_REGISTER_WRITES + i));
    vdp_write(0x00);
    /* Register 1 is the second write of the table. */
    ram8(v_VDPRegister1Value) = rd8(INITIAL_VDP_REGISTER_WRITES + 2);
    LIFTED_RETURN();
}

/* One bitplane of the "Phantasy Star RLE" tile format: blocks of
 * [count | $80, count raw bytes] or [count, 1 byte repeated count times],
 * until a 0 byte. Every 4th VRAM byte is written (the other planes are
 * interleaved). Returns the source pointer after the terminator. */
static uint16_t decompress_bitplane(uint16_t src, uint16_t vram) {
    uint8_t header;
    while ((header = rd8(src++)) != 0) {
        bool raw = header & 0x80;
        unsigned count = header & 0x7F;
        if (count == 0) count = 256; /* header $80: DJNZ from 0 */
        while (count--) {
            vdp_set_address(vram);
            vdp_write(rd8(src));
            if (raw) src++;
            vram += 4;
        }
        if (!raw) src++;
    }
    cpu.a = 0;
    return src;
}

/* $02A0 (decompressTilesToVram@bitplane): one bitplane from HL to VDP DE.
 * Returns HL after the terminator. */
LIFTED(decompressTilesToVram_bitplane, 0x02A0) {
    cpu.hl = decompress_bitplane(cpu.hl, cpu.de);
    LIFTED_RETURN();
}

/* $0293: decompress RLE tiles from HL to the VDP (DE = command word): the
 * four bitplanes are stored one after the other. */
LIFTED(decompressTilesToVram, 0x0293) {
    uint16_t src = cpu.hl;
    for (int plane = 0; plane < 4; plane++) src = decompress_bitplane(src, (uint16_t)(cpu.de + plane));
    cpu.hl = src;
    cpu.de += 4;
    cpu.b = 0;
    LIFTED_RETURN();
}

/* $02C5: write BC bytes (0 = 65536) from HL to the VDP data port with their
 * bits reversed (horizontally mirrored tile rows). */
LIFTED(copyMirroredTilesToVramAtCurrentAddress, 0x02C5) {
    uint32_t count = bc_count(cpu.bc);
    uint16_t src = cpu.hl;
    while (count--) {
        uint8_t row = rd8(src++), mirrored = 0;
        for (int bit = 0; bit < 8; bit++) mirrored |= ((row >> bit) & 1) << (7 - bit);
        vdp_write(mirrored);
    }
    cpu.hl = src;
    cpu.bc = 0;
    cpu.de = 0;
    cpu.a = 0;
    LIFTED_RETURN();
}

/* $02D7 (never called): hide every sprite and wait for the interrupt to
 * upload the table. */
LIFTED(clearSprites, 0x02D7) {
    hide_all_sprites();
    cpu.a = IRQ_UPLOAD_SPRITES;
    TAIL_CALL(f_waitForInterrupt);
}

/* $02FB: A = new value of VDP register 1. Returns DE = the command written. */
LIFTED(sub_02FB, 0x02FB) {
    write_vdp_register1(cpu.a);
    LIFTED_RETURN();
}

/* $02EF: blank the screen. */
LIFTED(disableDisplay, 0x02EF) {
    write_vdp_register1(ram8(v_VDPRegister1Value) & ~VDP_R1_DISPLAY_VISIBLE);
    LIFTED_RETURN();
}

/* $02F6: show the screen. Returns DE = the command written. */
LIFTED(enableDisplay, 0x02F6) {
    write_vdp_register1(ram8(v_VDPRegister1Value) | VDP_R1_DISPLAY_VISIBLE);
    LIFTED_RETURN();
}

/* $0303: zero the pixel part of both scroll positions and the VDP scroll
 * registers. (v_levelData_C0B0 is the high byte of v_horizontalScroll.) */
LIFTED(clearScroll, 0x0303) {
    ram8(v_verticalScroll + 1) = 0;
    ram8(v_horizontalScroll + 1) = 0;
    vdp_set_address(VDP_REGISTER(9, 0));
    vdp_set_address(VDP_REGISTER(8, 0));
    LIFTED_RETURN();
}

/* $0311: blank the screen, reset scrolling, hide the sprites (uploaded by one
 * interrupt, with interrupts enabled just for it) and clear the name table.
 * Returns with interrupts disabled. */
LIFTED(clearVDPTablesAndDisableScreen, 0x0311) {
    write_vdp_register1(ram8(v_VDPRegister1Value) & ~VDP_R1_DISPLAY_VISIBLE);
    ram16(v_horizontalScroll) = 0;
    ram16(v_verticalScroll) = 0;
    ram16(v_horizontalScrollSpeed) = 0;
    ram16(v_verticalScrollSpeed) = 0;
    hide_all_sprites();
    vdp_set_address(VDP_REGISTER(8, 0));
    vdp_set_address(VDP_REGISTER(9, 0));

    cpu.iff1 = cpu.iff2 = 1;
    cpu.a = IRQ_UPLOAD_SPRITES;
    CALL_ROUTINE(f_waitForInterrupt);
    cpu.iff1 = cpu.iff2 = 0;

    /* clearNameTable ($017C): 32x28 entries of 0. */
    cpu.de = VDP_VRAM_WRITE(VRAM_NAME_TABLE);
    cpu.bc = 0x0700;
    cpu.l = 0;
    TAIL_CALL(f_fillVram);
}

/* $04A1: read the 4 bytes after HL [VDP command word, count, source low byte]
 * into DE, B and A; returns HL pointing at the last one. Only used by
 * unusedCodeB. */
LIFTED(fillRegisters, 0x04A1) {
    uint16_t record = (uint16_t)(cpu.hl + 1);
    cpu.de = rd16(record);
    cpu.b = rd8((uint16_t)(record + 2));
    cpu.a = rd8((uint16_t)(record + 3));
    cpu.hl = (uint16_t)(record + 3);
    LIFTED_RETURN();
}

/* $04AA (never called): HL = [count] followed by `count` records
 * [VDP command word, entry count, source word]; copies each run of name-table
 * tile bytes with the attribute byte v_nametableCopyFlags. */
LIFTED(unusedCodeB, 0x04AA) {
    uint8_t records = rd8(cpu.hl);
    const uint8_t c = cpu.c; /* kept on the stack by the original */
    do {
        cpu.b = records;
        CALL_ROUTINE(f_fillRegisters);
        uint16_t next = (uint16_t)(cpu.hl + 1);
        cpu.hl = (uint16_t)((rd8(next) << 8) | cpu.a);
        CALL_ROUTINE(f_copyNametableEntriesToVRAM);
        cpu.hl = next;
        cpu.c = c;
    } while (--records);
    cpu.b = 0;
    LIFTED_RETURN();
}

/* $04BB (never called): load the 64 characters of the 1bpp font of bank 2 as
 * tiles $20-$5F in colour A. */
LIFTED(unusedCodeC, 0x04BB) {
    /* EX AF,AF' around the bank switch leaves A' = the bank number. */
    cpu.af_ = (uint16_t)((SLOT2_BANK2 << 8) | (cpu.af_ & 0xFF));
    wr8(MAPPER_SLOT2, SLOT2_BANK2);
    cpu.de = VDP_VRAM_WRITE(0x0400);
    cpu.bc = 0x0200;
    cpu.hl = ONE_BPP_FONT;
    TAIL_CALL(f_load1bppTiles);
}
