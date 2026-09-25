/*
 * VDP transfer routines (bank 0, $0008-$01D5).
 *
 * The original routines take their arguments in registers; each LIFTED entry
 * point below unpacks them, does the work with the helpers, and leaves the
 * registers its callers read afterwards exactly as the Z80 code did.
 */
#include "game/vdp_io.h"

#include "game/lift.h"
#include "game/ram.h"

void vdp_set_address(uint16_t control_word) {
    io_out(VDP_CONTROL, (uint8_t)control_word);
    io_out(VDP_CONTROL, (uint8_t)(control_word >> 8));
}

void vdp_write(uint8_t value) { io_out(VDP_DATA, value); }

uint8_t vdp_read(void) { return io_in(VDP_DATA); }

void vdp_write_bytes(uint16_t src, uint32_t count) {
    while (count--) vdp_write(rd8(src++));
}

void vdp_fill(uint8_t value, uint32_t count) {
    while (count--) vdp_write(value);
}

/* $0008 (rst $08): DE = control word. Leaves A = D. */
LIFTED(setVdpAddress, 0x0008) {
    vdp_set_address(cpu.de);
    cpu.a = cpu.d;
    LIFTED_RETURN();
}

/* $0010 (rst $10): HL = table of words, A = index. Returns HL = table[A], BC = 2*A. */
LIFTED(loadAthPointer, 0x0010) {
    uint8_t offset = (uint8_t)(cpu.a * 2);
    uint16_t entry = (uint16_t)(cpu.hl + offset);
    cpu.bc = offset;
    cpu.a = rd8(entry);
    cpu.hl = rd16(entry);
    LIFTED_RETURN();
}

/* $0030 (rst $30): copy B bytes (0 = 256) from HL to the VDP at control word DE. */
LIFTED(memcpyToVRAM, 0x0030) {
    uint32_t count = cpu.b ? cpu.b : 256;
    vdp_set_address(cpu.de);
    vdp_write_bytes(cpu.hl, count);
    cpu.a = cpu.d;
    cpu.hl = (uint16_t)(cpu.hl + count);
    cpu.bc = VDP_DATA;
    LIFTED_RETURN();
}

/* $013F: write A to the VDP at control word DE. */
LIFTED(writeAToVRAM, 0x013F) {
    vdp_set_address(cpu.de);
    vdp_write(cpu.a);
    LIFTED_RETURN();
}

/* $0145: copy BC bytes from HL to the VDP at control word DE. */
LIFTED(copyBytesToVRAM, 0x0145) {
    uint32_t count = bc_count(cpu.bc);
    vdp_set_address(cpu.de);
    vdp_write_bytes(cpu.hl, count);
    cpu.hl = (uint16_t)(cpu.hl + count);
    cpu.bc = VDP_DATA;
    cpu.a = 0;
    cpu.f = FLAG_Z | FLAG_N; /* as left by the final DEC A */
    LIFTED_RETURN();
}

/* $0159: B name-table entries from HL (tile bytes) with the attribute byte
 * v_nametableCopyFlags, to the VDP at control word DE. */
LIFTED(copyNametableEntriesToVRAM, 0x0159) {
    uint8_t attributes = ram8(v_nametableCopyFlags);
    uint32_t count = cpu.b ? cpu.b : 256;
    vdp_set_address(cpu.de);
    for (uint32_t i = 0; i < count; i++) {
        vdp_write(rd8(cpu.hl++));
        vdp_write(attributes);
    }
    cpu.a = attributes;
    cpu.bc = VDP_DATA;
    LIFTED_RETURN();
}

/* $0168: read BC bytes from the VDP at control word DE into HL. */
LIFTED(copyBytesFromVRAM, 0x0168) {
    uint32_t count = bc_count(cpu.bc);
    vdp_set_address(cpu.de);
    for (uint32_t i = 0; i < count; i++) wr8(cpu.hl++, vdp_read());
    cpu.bc = VDP_DATA;
    cpu.a = 0;
    cpu.f = FLAG_Z | FLAG_N;
    LIFTED_RETURN();
}

/* $0184: write BC copies of L to the VDP at control word DE. */
LIFTED(fillVram, 0x0184) {
    uint32_t count = bc_count(cpu.bc);
    vdp_set_address(cpu.de);
    vdp_fill(cpu.l, count);
    cpu.a = cpu.l;
    cpu.bc = 0;
    LIFTED_RETURN();
}

/* $0193: copy a block of B rows of C bytes from HL to the name table at DE. */
LIFTED(copyNameTableBlockToVram, 0x0193) {
    uint8_t rows = cpu.b, width = cpu.c;
    uint16_t dst = cpu.de;
    do {
        vdp_set_address(dst);
        vdp_write_bytes(cpu.hl, width ? width : 256);
        cpu.hl = (uint16_t)(cpu.hl + (width ? width : 256));
        dst = (uint16_t)(dst + NAMETABLE_ROW_BYTES);
    } while (--rows);
    cpu.de = dst;
    cpu.a = (uint8_t)((dst - NAMETABLE_ROW_BYTES) >> 8);
    cpu.bc = (uint16_t)(0x0000 | width);
    LIFTED_RETURN();
}

/* $01A7: like copyNameTableBlockToVram, one tile byte from HL per entry and the
 * attribute byte A (saved to v_nametableCopyFlags). */
LIFTED(copyNameTableBlockToVramWithFlag, 0x01A7) {
    uint8_t attributes = cpu.a;
    uint8_t rows = cpu.b, width = cpu.c;
    uint16_t dst = cpu.de;
    ram8(v_nametableCopyFlags) = attributes;
    do {
        vdp_set_address(dst);
        for (int i = 0; i < (width ? width : 256); i++) {
            vdp_write(rd8(cpu.hl++));
            vdp_write(attributes);
        }
        dst = (uint16_t)(dst + NAMETABLE_ROW_BYTES);
    } while (--rows);
    cpu.de = dst;
    cpu.a = attributes;
    cpu.bc = width;
    LIFTED_RETURN();
}
