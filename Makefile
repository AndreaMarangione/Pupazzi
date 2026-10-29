# Pupazzi - core libretro
#
#   make            core per questo PC con il compilatore di sistema, piu' gli strumenti di prova
#   make test       prove automatiche senza console
#   make test-gl    prove del renderer GPU su un OpenGL ES software (Mesa)
#   make test-audio prove del sonoro (effetti e brani in build/wav)
#   make test-net   prove del gioco in rete (0.5.4): piu' copie del gioco su questa macchina
#   make console    core per la console: ARM a 32 bit (RetroArch di serie) e ARM a 64 bit
#   make pc         core per PC Linux a 64 bit che non dipende dalla glibc di questa macchina
#   make dist       tutto cio' che serve a deploy.sh: dist/APPS (console) e dist/pc
#
# console, pc e dist usano zig come compilatore incrociato:  pip install ziglang

SRC := src/core.cpp src/game.cpp src/game_draw.cpp src/player.cpp src/combat.cpp src/monsters.cpp \
       src/flow.cpp src/menu.cpp src/bot.cpp src/cards.cpp src/level.cpp src/level_mesh.cpp src/maps.cpp src/models.cpp \
       src/swr.cpp src/glr.cpp src/hud.cpp src/ui.cpp src/lang.cpp src/mesh.cpp src/plat.cpp src/save.cpp src/audio.cpp \
       src/sysinfo.cpp src/net.cpp
HDR := $(wildcard src/*.h)
# Mappe della campagna, in ordine: incorporate nel core e copiate nella cartella mappe/ della console.
MAPS := maps/terra1.txt maps/terra2.txt maps/terra3.txt maps/terra_boss.txt
GEN := build/gen/maps_data.cpp

# -ffp-contract=off: stessa aritmetica su tutte le piattaforme (vedi src/dmath.h e src/swr.cpp).
# Niente tabelle di svolgimento dello stack e niente interi a 64 bit convertiti in virgola mobile:
# su ARM a 32 bit tirerebbero dentro le routine di supporto del compilatore, che con zig
# portano con se' una memset lenta al posto di quella di glibc (la regola arm32 lo controlla).
CXXFLAGS := -std=c++17 -O2 -fPIC -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-math-errno \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -ffp-contract=off -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter
LDLIBS := -lm -lpthread -ldl

# Identificativo della build, scritto nel log del core e nei risultati.
VERSION := $(shell sed -n 's/^\#define PUPAZZI_VERSION "\([^"]*\)".*/\1/p' src/game.h)
BUILD_ID ?= $(shell date +%Y%m%d)
CXXFLAGS += -DPUPAZZI_BUILD='"$(BUILD_ID)"'

ZIG ?= python3 -m ziglang
# glibc 2.28 come base: la console ha la 2.35, cosi' resta margine.
ARM32 := -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a53
ARM64 := -target aarch64-linux-gnu.2.28 -mcpu=cortex_a53
PC64  := -target x86_64-linux-gnu.2.28

# La build a 32 bit deve prendere memset e memcpy da glibc (vedi sopra).
CHECK_ARM32 = @if command -v readelf >/dev/null 2>&1; then \
	  for f in memset memcpy; do \
	    readelf --dyn-syms -W $@ | grep -q " UND $$f@" || { echo "ERRORE: $@ non importa $$f da glibc"; rm -f $@; exit 1; }; \
	  done; fi

.PHONY: all native tools console arm32 arm64 pc dist test test-gl test-audio test-net clean
all: native tools
native: build/native/pupazzi_libretro.so
tools: build/native/host build/native/test_raster build/native/test_aim build/native/test_save build/native/test_rules build/native/test_maps build/native/test_boss \
       build/native/test_lang build/native/test_sys
console: arm32 arm64
arm32: build/arm32/pupazzi_libretro.so
arm64: build/arm64/pupazzi_libretro.so
pc: build/pc/pupazzi_libretro.so

# $(1) cartella, $(2) compilatore C++, $(3) collegatore, $(4) opzioni di collegamento, $(5) controllo finale
define TARGET
build/$(1)/%.o: src/%.cpp $$(HDR)
	@mkdir -p build/$(1)
	$(2) $$(CXXFLAGS) -c -o $$@ $$<
build/$(1)/maps_data.o: $$(GEN) src/maps.h src/level.h
	@mkdir -p build/$(1)
	$(2) $$(CXXFLAGS) -Isrc -c -o $$@ $$<
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

build/native/host: tools/host.cpp src/libretro.h
	@mkdir -p build/native
	$(CXX) -std=c++17 -O2 -Wall -DHOST_GL -o $@ $< -ldl

build/native/test_raster: tools/test_raster.cpp src/swr.cpp src/mesh.cpp src/hud.cpp src/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -o $@ tools/test_raster.cpp src/swr.cpp src/mesh.cpp src/hud.cpp src/plat.cpp -lm -lpthread

# Prove della mira: il gioco senza frontend (niente core.cpp e niente renderer).
GAME_SRC := $(filter-out src/core.cpp src/swr.cpp src/glr.cpp src/net.cpp,$(SRC))
build/native/test_aim: tools/test_aim.cpp $(GAME_SRC) $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_aim.cpp $(GAME_SRC) $(GEN) -lm -lpthread

# Prove delle regole (vite, mappe da capo, giri): il gioco senza frontend e il renderer software
# (con --immagini scrive le schermate dell'HUD).
build/native/test_rules: tools/test_rules.cpp $(GAME_SRC) src/swr.cpp $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_rules.cpp $(GAME_SRC) src/swr.cpp $(GEN) -lm -lpthread

# Prove delle mappe (0.5.1): niente fughe dalle zone (controlli fissi, giocatori a caso, casi mirati).
build/native/test_maps: tools/test_maps.cpp $(GAME_SRC) $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_maps.cpp $(GAME_SRC) $(GEN) -lm -lpthread

# Prove del Gladiatore gigante (0.5.1): movimento, attacchi, fasi, ripartenze (con --immagini le mosse,
# con --lotte partite intere con i giocatori automatici).
build/native/test_boss: tools/test_boss.cpp $(GAME_SRC) src/swr.cpp $(GEN) $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_boss.cpp $(GAME_SRC) src/swr.cpp $(GEN) -lm -lpthread

# Prove delle lingue (0.5.3): testi, caratteri, a capo.
build/native/test_lang: tools/test_lang.cpp src/lang.cpp src/hud.cpp src/ui.cpp src/save.cpp src/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_lang.cpp src/lang.cpp src/hud.cpp src/ui.cpp src/save.cpp src/plat.cpp -lm -lpthread

# Prove del salvataggio: solo save.cpp (e plat.cpp per il registro), in una cartella temporanea.
build/native/test_save: tools/test_save.cpp src/save.cpp src/plat.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_save.cpp src/save.cpp src/plat.cpp

# Prove di batteria e CPU (0.5.4) con una /sys e una /proc finte.
build/native/test_sys: tools/test_sys.cpp src/sysinfo.cpp $(HDR)
	@mkdir -p build/native
	$(CXX) $(filter-out -fPIC -fvisibility=hidden,$(CXXFLAGS)) -Isrc -o $@ tools/test_sys.cpp src/sysinfo.cpp

test: native tools
	python3 tools/make_lang.py --controlla
	./build/native/test_lang
	./build/native/test_sys
	./build/native/test_raster 300
	./build/native/test_aim
	./build/native/test_save
	./build/native/test_rules
	./build/native/test_maps
	./build/native/test_boss
	./build/native/host build/native/pupazzi_libretro.so --frames 120 --quiet

# Prove del renderer GPU su un OpenGL ES software: servono le librerie Mesa (di solito ci sono gia').
# Prove del sonoro: ogni effetto e ogni brano in build/wav (livelli, picchi, tempo per fotogramma).
build/native/test_audio: tools/test_audio.cpp src/audio.cpp src/audio.h
	@mkdir -p build/native
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc -o $@ tools/test_audio.cpp src/audio.cpp

test-audio: build/native/test_audio
	@mkdir -p build/wav
	./build/native/test_audio build/wav

test-gl: native tools
	sh tools/test_gl.sh

# Gioco in rete: 2, 3 e 4 copie del gioco che giocano insieme con messaggi UDP sulla stessa macchina
# (messaggi persi e in ritardo, console che escono o si spengono, versione diversa...): le partite
# devono restare uguali fotogramma per fotogramma. Qualche minuto.
test-net: native tools
	sh tools/test_net.sh

# Cartella pronta per la console (va copiata dentro Roms/APPS) e core per il PC.
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
