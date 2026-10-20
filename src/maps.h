// Mappe della campagna: incorporate nel core (generate da maps/*.txt) e sostituibili da file.
#pragma once
#include "level.h"

struct MapText { const char* id; const char* text; };
extern const MapText MAP_TEXTS[];     // in ordine di campagna (build/gen/maps_data.cpp)
extern const int MAP_TEXT_COUNT;

void maps_init(void);                 // legge i nomi (e i file personalizzati, se ci sono)
int maps_count(void);
const char* maps_id(int i);
const char* maps_name(int i);         // nome per i menu
bool maps_is_boss(int i);
int maps_world(int i);
// Carica la mappa i: dal file <PUPAZZI_MAPS>/<id>.txt se c'e' ed e' valido, altrimenti quella
// incorporata. note riceve un avviso da mostrare nei menu (vuoto se va tutto bene).
bool maps_load(int i, Level* L, char* note, int notelen);
const char* maps_note(int i);         // avviso dell'ultima lettura della mappa i
