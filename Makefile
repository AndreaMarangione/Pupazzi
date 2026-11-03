# Pupazzi - libretro core
#
#   make            core for this PC with the system compiler, plus the test tools
#   make test       automatic tests without a console
#   make test-gl    GPU renderer tests on a software OpenGL ES (Mesa)
#   make test-audio sound tests (every effect and every tune in build/wav)
#   make test-net   LAN play tests (0.5.4): several copies of the game on this machine
#   make console    cores for the console: ARM 32 bit (stock RetroArch) and ARM 64 bit
#   make pc         core for 64-bit Linux PCs that does not depend on this machine's glibc
#   make dist       everything deploy.sh needs: dist/APPS (console) and dist/pc
#
# console, pc and dist use zig as cross compiler:  pip install ziglang
#
# Sources (paths relative to src/, which is also the include root):
#   platform/     libretro glue (core.cpp), log and clock, battery and CPU load
#   engine/       maths, meshes, software and GPU renderers, sound synthesizer, interface, texts
#   game/         public game API (game.h) and save files
#   game/sim/     the deterministic game: rules, heroes, monsters, boss, cards, levels, bots
#   game/view/    drawing of the game: scene, models, level geometry
#   game/screens/ menus and screens
#   net/          LAN play

SRC := src/platform/core.cpp src/game/sim/game.cpp src/game/view/game_draw.cpp src/game/view/view.cpp src/game/view/camera.cpp src/game/sim/player.cpp \
       src/game/sim/combat.cpp src/game/sim/monsters.cpp src/game/sim/flow.cpp src/game/screens/menu.cpp \
       src/game/sim/bot.cpp src/game/sim/cards.cpp src/game/sim/level.cpp src/game/view/level_mesh.cpp \
       src/game/sim/maps.cpp src/game/view/models.cpp src/engine/swr.cpp src/engine/glr.cpp src/engine/hud.cpp \
       src/engine/ui.cpp src/engine/lang.cpp src/engine/mesh.cpp src/platform/plat.cpp src/game/save.cpp \
       src/engine/audio.cpp src/platform/sysinfo.cpp src/net/net.cpp
HDR := $(shell find src -name '*.h')
# Campaign maps, in order: embedded in the core and copied into the mappe/ folder on the console.
MAPS := maps/terra1.txt maps/terra2.txt maps/terra3.txt maps/terra_boss.txt
GEN := build/gen/maps_data.cpp

# -ffp-contract=off: the same arithmetic on every platform (see src/engine/dmath.h and src/engine/swr.cpp).
# No stack unwinding tables and no 64-bit integers converted to floating point: on ARM 32 bit they
# would pull in the compiler support routines, which with zig bring a slow memset instead of
# glibc's (the arm32 rule checks it).
CXXFLAGS := -std=c++17 -O2 -fPIC -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-math-errno \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -ffp-contract=off -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Isrc
LDLIBS := -lm -lpthread -ldl
# Tools and tests: the same flags, built as programs.
TOOLFLAGS = $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS))

# Build identifier, written in the core's log and in the results.
VERSION := $(shell sed -n 's/^\#define PUPAZZI_VERSION "\([^"]*\)".*/\1/p' src/game/game.h)
BUILD_ID ?= $(shell date +%Y%m%d)
CXXFLAGS += -DPUPAZZI_BUILD='"$(BUILD_ID)"'

ZIG ?= python3 -m ziglang
# glibc 2.28 as baseline: the console has 2.35, so there is room.
ARM32 := -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a53
ARM64 := -target aarch64-linux-gnu.2.28 -mcpu=cortex_a53
PC64  := -target x86_64-linux-gnu.2.28

# The 32-bit build must take memset and memcpy from glibc (see above).
CHECK_ARM32 = @if command -v readelf >/dev/null 2>&1; then \
	  for f in memset memcpy; do \
	    readelf --dyn-syms -W $@ | grep -q " UND $$f@" || { echo "ERRORE: $@ non importa $$f da glibc"; rm -f $@; exit 1; }; \
	  done; fi

.PHONY: all native tools console arm32 arm64 pc dist test test-gl test-audio test-net clean
all: native tools
native: build/native/pupazzi_libretro.so
tools: build/native/host build/native/test_raster build/native/test_aim build/native/test_save build/native/test_rules build/native/test_maps build/native/test_boss \
       build/native/test_lang build/native/test_sys build/native/test_netin
console: arm32 arm64
arm32: build/arm32/pupazzi_libretro.so
arm64: build/arm64/pupazzi_libretro.so
pc: build/pc/pupazzi_libretro.so

# $(1) folder, $(2) C++ compiler, $(3) linker, $(4) link options, $(5) final check
define TARGET
build/$(1)/%.o: src/%.cpp $$(HDR)
	@mkdir -p $$(@D)
	$(2) $$(CXXFLAGS) -c -o $$@ $$<
build/$(1)/maps_data.o: $$(GEN) src/game/sim/maps.h src/game/sim/level.h
	@mkdir -p build/$(1)
	$(2) $$(CXXFLAGS) -c -o $$@ $$<
build/$(1)/pupazzi_libretro.so: $$(patsubst src/%.cpp,build/$(1)/%.o,$$(SRC)) build/$(1)/maps_data.o
	$(3) -shared -o $$@ $$^ $(4) $$(LDLIBS)
	$(5)
endef

$(GEN): $(MAPS) tools/embed_maps.sh
	@mkdir -p build/gen
	sh tools/embed_maps.sh $(MAPS) > $@

$(eval $(call TARGET,native,$(CXX),$(CC),,))
$(eval $(call TARGET,arm32,$(ZIG) c++ $(ARM32),$(ZIG) cc $(ARM32),-s,$$(CHECK_ARM32)))
$(eval $(call TARGET,arm64,$(ZIG) c++ $(ARM64),$(ZIG) cc $(ARM64),-s,))
$(eval $(call TARGET,pc,$(ZIG) c++ $(PC64),$(ZIG) cc $(PC64),-s,))

# Test frontend: loads the core like RetroArch (optionally with an OpenGL ES context from Mesa).
build/native/host: tools/host.cpp src/platform/libretro.h
	@mkdir -p build/native
	$(CXX) -std=c++17 -O2 -Wall -DHOST_GL -Isrc -o $@ $< -ldl

build/native/test_raster: tools/test_raster.cpp src/engine/swr.cpp src/engine/mesh.cpp src/engine/hud.cpp src/platform/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_raster.cpp src/engine/swr.cpp src/engine/mesh.cpp src/engine/hud.cpp src/platform/plat.cpp -lm -lpthread

# Aim tests: the game without a frontend (no core.cpp and no renderers).
GAME_SRC := $(filter-out src/platform/core.cpp src/engine/swr.cpp src/engine/glr.cpp src/net/net.cpp,$(SRC))
build/native/test_aim: tools/test_aim.cpp $(GAME_SRC) $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_aim.cpp $(GAME_SRC) $(GEN) -lm -lpthread

# Rules tests (lives, maps from the start, rounds): the game without a frontend plus the software
# renderer (with --immagini it writes the HUD screens).
build/native/test_rules: tools/test_rules.cpp $(GAME_SRC) src/engine/swr.cpp $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_rules.cpp $(GAME_SRC) src/engine/swr.cpp $(GEN) -lm -lpthread

# Map tests (0.5.1): no way out of the zones (fixed checks, random players, targeted cases).
build/native/test_maps: tools/test_maps.cpp $(GAME_SRC) $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_maps.cpp $(GAME_SRC) $(GEN) -lm -lpthread

# Giant Gladiator tests (0.5.1): movement, attacks, phases, restarts (with --immagini the moves, with
# --lotte whole fights with the bots).
build/native/test_boss: tools/test_boss.cpp $(GAME_SRC) src/engine/swr.cpp $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_boss.cpp $(GAME_SRC) src/engine/swr.cpp $(GEN) -lm -lpthread

# Language tests (0.5.3): texts, glyphs, line breaks.
build/native/test_lang: tools/test_lang.cpp src/engine/lang.cpp src/engine/hud.cpp src/engine/ui.cpp src/game/save.cpp src/platform/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_lang.cpp src/engine/lang.cpp src/engine/hud.cpp src/engine/ui.cpp src/game/save.cpp src/platform/plat.cpp -lm -lpthread

# Save tests: only save.cpp (and plat.cpp for the log), in a temporary folder.
build/native/test_save: tools/test_save.cpp src/game/save.cpp src/platform/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_save.cpp src/game/save.cpp src/platform/plat.cpp

# Tests of the commands that reach the host of a LAN game (0.5.4): src/net/net.cpp without a network.
build/native/test_netin: tools/test_netin.cpp src/net/net.cpp $(GAME_SRC) $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_netin.cpp $(GAME_SRC) $(GEN) -lm -lpthread

# Battery and CPU tests (0.5.4) with a fake /sys and /proc.
build/native/test_sys: tools/test_sys.cpp src/platform/sysinfo.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(TOOLFLAGS) -o $@ tools/test_sys.cpp src/platform/sysinfo.cpp

test: native tools
	python3 tools/make_lang.py --controlla
	./build/native/test_lang
	./build/native/test_sys
	./build/native/test_netin
	./build/native/test_raster 300
	./build/native/test_aim
	./build/native/test_save
	./build/native/test_rules
	./build/native/test_maps
	./build/native/test_boss
	./build/native/host build/native/pupazzi_libretro.so --frames 120 --quiet

# Sound tests: every effect and every tune in build/wav (levels, peaks, time per frame).
build/native/test_audio: tools/test_audio.cpp src/engine/audio.cpp src/engine/audio.h
	@mkdir -p build/native
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc -o $@ tools/test_audio.cpp src/engine/audio.cpp

test-audio: build/native/test_audio
	@mkdir -p build/wav
	./build/native/test_audio build/wav

# GPU renderer tests on a software OpenGL ES: they need the Mesa libraries (usually already there).
test-gl: native tools
	sh tools/test_gl.sh

# LAN play: 2, 3 and 4 copies of the game playing together with UDP messages on the same machine
# (lost and late messages, consoles that leave or switch off, different version...): the games must
# stay the same frame by frame. A few minutes.
test-net: native tools
	sh tools/test_net.sh

# Folder ready for the console (to be copied into Roms/APPS) and core for the PC.
dist: console pc
	rm -rf dist
	mkdir -p dist/APPS/pupazzi/arm32 dist/APPS/pupazzi/arm64 dist/APPS/pupazzi/mappe/originali dist/APPS/Imgs dist/pc
	cp console/Pupazzi.sh dist/APPS/
	cp maps/LEGGIMI.txt dist/APPS/pupazzi/mappe/
	cp $(MAPS) dist/APPS/pupazzi/mappe/originali/
	cp console/pupazzi/run.sh dist/APPS/pupazzi/
	cp console/Imgs/Pupazzi.png dist/APPS/Imgs/
	cp build/arm32/pupazzi_libretro.so dist/APPS/pupazzi/arm32/
	cp build/arm64/pupazzi_libretro.so dist/APPS/pupazzi/arm64/
	cp build/pc/pupazzi_libretro.so dist/pc/
	echo "$(VERSION) build $(BUILD_ID)" > dist/APPS/pupazzi/VERSIONE

clean:
	rm -rf build dist
