#ifndef INSN_TESTS_H
#define INSN_TESTS_H

#include <stdint.h>

enum { INSN_PLAIN, INSN_BRANCH, INSN_COND_ONLY };
enum { INSN_MASK_XY = 1 };

typedef struct InsnTest {
    uint16_t addr;
    uint8_t size;
    uint8_t kind;
    uint8_t flags;
    int (*run)(void);
    const char *text;
} InsnTest;

extern const InsnTest insn_tests[];
extern const int insn_test_count;

#endif
