/* Register bit masks shared with engine/recomp/liveness.py (same order as REGS there). */
#ifndef RT_REGS_H
#define RT_REGS_H

#include <stdint.h>

enum {
    RB_A = 1u << 0, RB_B = 1u << 1, RB_C = 1u << 2, RB_D = 1u << 3, RB_E = 1u << 4,
    RB_H = 1u << 5, RB_L = 1u << 6, RB_IXH = 1u << 7, RB_IXL = 1u << 8,
    RB_IYH = 1u << 9, RB_IYL = 1u << 10,
    RB_FS = 1u << 11, RB_FZ = 1u << 12, RB_FH = 1u << 13, RB_FP = 1u << 14,
    RB_FN = 1u << 15, RB_FC = 1u << 16,
    RB_A_ = 1u << 17, RB_F_ = 1u << 18, RB_B_ = 1u << 19, RB_C_ = 1u << 20,
    RB_D_ = 1u << 21, RB_E_ = 1u << 22, RB_H_ = 1u << 23, RB_L_ = 1u << 24,
};

/* in: registers a routine reads; out: registers it modifies that callers read
 * afterwards; live: every register callers read afterwards (out plus the ones
 * the routine must preserve). */
typedef struct GameSignature {
    uint16_t addr;
    uint32_t in;
    uint32_t out;
    uint32_t live;
} GameSignature;

extern const GameSignature game_signatures[];
extern const int game_signature_count;

#endif
