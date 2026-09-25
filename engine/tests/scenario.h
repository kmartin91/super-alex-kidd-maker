/*
 * Input scenarios shared by the test harnesses.
 *
 *   --mode idle|play      idle: never touch the pad; play: start, then random input
 *   --seed S              random input seed
 *   --frames N            stop after N frames
 *   --level N             set v_level when a level starts; --lives: infinite lives
 *   --script FILE         lines "<frame> keys <U D L R 1 2|->", "<frame> pause",
 *                         "<frame> poke <addr> <val>", "<frame> peek <addr> <len>", "<frame> shot"
 *   --shots DIR --every K screenshots (PPM) of the port every K frames
 *   --mod PATCH           apply a mod patch (AKMOD1) to the ROM before running
 */
#ifndef TESTS_SCENARIO_H
#define TESTS_SCENARIO_H

#include <stdbool.h>
#include <stdint.h>

extern long sc_max_frames;
extern uint32_t sc_seed;
extern const char *sc_mode;
extern int sc_start_level;
extern const char *sc_shot_dir;
extern int sc_shot_every;

/* Consumes the options above; returns false on an unknown option. */
bool scenario_parse_arg(int argc, char **argv, int *i);
void scenario_init(void);

/* Called at every frame boundary before input is chosen. `poke` applies a RAM
 * write to every machine under test. Returns the pad state and whether the
 * pause button (NMI) is pressed this frame. */
void scenario_frame(long frame, void (*poke)(uint16_t addr, uint8_t v), uint8_t *joy, bool *nmi);

void scenario_write_shot(long frame);
uint8_t *scenario_load_rom(const char *path, uint32_t *size);

#endif
