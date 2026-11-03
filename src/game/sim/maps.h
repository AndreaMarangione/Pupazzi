// Campaign maps: built into the core (generated from maps/*.txt) and replaceable by files.
#pragma once
#include "game/sim/level.h"

struct MapText { const char* id; const char* text; };
extern const MapText MAP_TEXTS[];     // in campaign order (build/gen/maps_data.cpp)
extern const int MAP_TEXT_COUNT;

void maps_init(void);                 // reads the names (and the custom files, if there are any)
int maps_count(void);
const char* maps_id(int i);
const char* maps_name(int i);         // name for the menus
bool maps_is_boss(int i);
int maps_world(int i);
// Loads map i: from the file <PUPAZZI_MAPS>/<id>.txt if it exists and is valid, otherwise the
// built-in one. note receives a notice to show in the menus (empty if all is well).
bool maps_load(int i, Level* L, char* note, int notelen);
const char* maps_note(int i);         // notice from the last read of map i
// Hash of the maps in use (names and texts): two consoles play a LAN game only with the same
// ones (0.5.4).
uint64_t maps_content_hash(void);
