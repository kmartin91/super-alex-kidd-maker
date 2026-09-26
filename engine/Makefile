CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -Isrc -Ithird_party
# SDL_CONFIG: another SDL2 (e.g. the static one built for the desktop app);
# STATIC=1 links it into the game, which then runs on machines without SDL2.
SDL_CONFIG ?= sdl2-config
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
ifeq ($(STATIC),1)
SDL_LIBS := $(shell $(SDL_CONFIG) --static-libs)
ifeq ($(OS),Windows_NT)
SDL_LIBS += -static
endif
else
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)
endif

BUILD := build
RT_SRC := src/rt/machine.c src/rt/vdp.c src/rt/psg.c src/rt/runtime.c src/rt/shadow.c src/rt/mod.c src/rt/hdpack.c src/rt/maker.c
GEN_SRC := src/gen/gen_bank0.c src/gen/gen_bank1.c src/gen/gen_bank2.c src/gen/gen_dispatch.c src/gen/gen_signatures.c
# LIFTED_EXTRA: work-in-progress lifted modules (e.g. src/game/wip/audio/*.c)
LIFTED_SRC := $(wildcard src/game/*.c src/game/*/*.c) $(LIFTED_EXTRA)
GAME_SRC := $(RT_SRC) $(GEN_SRC) $(LIFTED_SRC)
HEADERS := $(wildcard src/rt/*.h src/gen/*.h src/game/*.h src/game/*/*.h)

.PHONY: all clean gen test web web-tools

all: $(BUILD)/alexkidd $(BUILD)/lockstep $(BUILD)/insn_test $(BUILD)/shadow $(BUILD)/leveldump $(BUILD)/entityicons $(BUILD)/tileharvest

gen:
	python3 recomp/recomp.py

$(BUILD)/alexkidd: $(GAME_SRC) src/platform/main_sdl.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(GAME_SRC) src/platform/main_sdl.c $(SDL_LIBS)

$(BUILD)/lockstep: $(GAME_SRC) tests/lockstep.c tests/scenario.c third_party/z80-superzazu/z80.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ithird_party/z80-superzazu -o $@ $(GAME_SRC) tests/lockstep.c tests/scenario.c third_party/z80-superzazu/z80.c

# Every lifted routine runs side by side with its generated translation.
$(BUILD)/shadow: $(GAME_SRC) tests/shadow.c tests/scenario.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRT_SHADOW -o $@ $(GAME_SRC) tests/shadow.c tests/scenario.c

$(BUILD)/insn_test: $(GAME_SRC) tests/insn_test.c tests/gen_insn_tests.c third_party/z80-superzazu/z80.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ithird_party/z80-superzazu -Itests -o $@ $(GAME_SRC) tests/insn_test.c tests/gen_insn_tests.c third_party/z80-superzazu/z80.c

test: $(BUILD)/lockstep $(BUILD)/insn_test $(BUILD)/shadow
	./$(BUILD)/insn_test ../original.sms 500
	./$(BUILD)/shadow ../original.sms --mode idle --frames 3000
	./$(BUILD)/shadow ../original.sms --mode play --frames 8000 --seed 1
	./$(BUILD)/lockstep ../original.sms --relaxed --mode idle --frames 3000
	./$(BUILD)/lockstep ../original.sms --relaxed --mode play --frames 20000 --seed 1

# Headless level video-state dump for the level editor.
$(BUILD)/leveldump: $(GAME_SRC) tools/leveldump.c tools/capture.c tools/capture.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(GAME_SRC) tools/leveldump.c tools/capture.c

# Entity appearance capture for the level editor.
$(BUILD)/entityicons: $(GAME_SRC) tools/entityicons.c tools/capture.c tools/capture.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(GAME_SRC) tools/entityicons.c tools/capture.c

# Reference graphics pack of the original game (to repaint).
$(BUILD)/tileharvest: $(GAME_SRC) tools/tileharvest.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(GAME_SRC) tools/tileharvest.c

# WebAssembly engine for the level editor (needs Emscripten: brew install emscripten).
WEB_OUT := ../maker/public/engine
web: $(WEB_OUT)/alexkidd.js
$(WEB_OUT)/alexkidd.js: $(GAME_SRC) src/platform/main_web.c $(HEADERS)
	mkdir -p $(WEB_OUT)
	emcc $(CPPFLAGS) -std=c11 -O2 -Wno-unused-parameter -o $@ $(GAME_SRC) src/platform/main_web.c \
	  -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH \
	  -sMODULARIZE -sEXPORT_NAME=AlexKiddEngine -sENVIRONMENT=web \
	  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_web_set_rom,_web_play,_web_stop,_web_set_joy,_web_press_pause,_web_ram \
	  -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,HEAP16,HEAPU32

# Captures for the level editor (level graphics, enemy pictures), run in a Web
# Worker. No Asyncify, so much lighter to build than the player above.
web-tools: $(WEB_OUT)/tools.js
$(WEB_OUT)/tools.js: $(GAME_SRC) tools/capture.c tools/capture.h src/platform/tools_web.c $(HEADERS)
	mkdir -p $(WEB_OUT)
	emcc $(CPPFLAGS) -std=c11 -O2 -Wno-unused-parameter -o $@ $(GAME_SRC) tools/capture.c src/platform/tools_web.c \
	  -sALLOW_MEMORY_GROWTH -sMODULARIZE -sEXPORT_NAME=AlexKiddTools -sENVIRONMENT=web,worker \
	  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_web_tools_set_rom,_web_level_video,_web_entity_icon \
	  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
