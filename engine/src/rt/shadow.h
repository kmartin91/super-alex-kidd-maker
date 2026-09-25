/*
 * Shadow testing of lifted routines (builds with -DRT_SHADOW).
 *
 * Each call to a routine that has a lifted version snapshots the machine,
 * runs the generated translation, restores the snapshot, runs the lifted
 * version, and compares: stack pointer, the routine's output registers
 * (from its signature), RAM (minus the dead stack area), VDP, PSG, mapper
 * and runtime state. Execution then continues from the generated result so
 * the game stays on the original trajectory.
 */
#ifndef RT_SHADOW_H
#define RT_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#include "runtime.h"

typedef struct ShadowStats {
    uint16_t addr;
    const char *name;
    GameFn lifted;
    long calls;
    long mismatches;
} ShadowStats;

void rt_shadow_register(uint16_t addr, GameFn fn, const char *name);
void rt_shadow_call(uint16_t addr, GameFn generated);

/* 0 outside shadow sessions; nesting depth otherwise. */
int rt_shadow_depth(void);
/* Frames elapsed since the outermost session started, along the current run.
 * Saved and restored with snapshots so both runs see the same frame numbers. */
extern long rt_shadow_frame_index;

/* Maximum nesting of compared calls (deeper lifted calls run generated code). */
extern int rt_shadow_max_depth;
/* Directory where failing entry states are saved (NULL: don't save). */
extern const char *rt_shadow_fail_dir;
/* Report and totals. */
int rt_shadow_report(void);
int rt_shadow_registered_count(void);
const ShadowStats *rt_shadow_stats(int i);

/* Replays a saved failing state: runs both versions and prints the differences. */
int rt_shadow_replay(const char *path);

/* Whole-machine snapshot (also used by the harness). */
typedef struct MachineSnapshot MachineSnapshot;
MachineSnapshot *rt_snapshot_new(void);
void rt_snapshot_take(MachineSnapshot *s);
void rt_snapshot_restore(const MachineSnapshot *s);
void rt_snapshot_free(MachineSnapshot *s);

#endif
