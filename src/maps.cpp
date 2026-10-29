#include "maps.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int MAX_MAPS = 16;
char g_name[MAX_MAPS][40];
char g_note[MAX_MAPS][120];
bool g_boss[MAX_MAPS];
int g_world[MAX_MAPS];
char* g_custom[MAX_MAPS];   // testo del file personalizzato, se c'e' (anche se non valido)
Level g_tmp;                // per leggere i nomi senza toccare la mappa in gioco

char* read_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  const size_t cap = 64 * 1024;
  char* buf = (char*)malloc(cap + 1);
  if (!buf) { fclose(f); return 0; }
  const size_t n = fread(buf, 1, cap, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}

}  // namespace

void maps_init(void) {
  const char* dir = getenv("PUPAZZI_MAPS");
  for (int i = 0; i < MAP_TEXT_COUNT && i < MAX_MAPS; ++i) {
    free(g_custom[i]);
    g_custom[i] = 0;
    g_note[i][0] = 0;
    if (dir && *dir) {
      char path[512];
      snprintf(path, sizeof(path), "%s/%s.txt", dir, MAP_TEXTS[i].id);
      g_custom[i] = read_file(path);
    }
    char err[160];
    const char* text = MAP_TEXTS[i].text;
    if (g_custom[i]) {
      if (level_parse(&g_tmp, g_custom[i], err, sizeof(err))) {
        text = g_custom[i];
        plat_log("mappa %s: uso il file personalizzato", MAP_TEXTS[i].id);
      } else {
        snprintf(g_note[i], sizeof(g_note[i]), "%s.txt non valida: %.90s", MAP_TEXTS[i].id, err);
        plat_log("mappa %s: file personalizzato non valido (%s), uso quella di serie", MAP_TEXTS[i].id, err);
        free(g_custom[i]);
        g_custom[i] = 0;
      }
    }
    if (!level_parse(&g_tmp, text, err, sizeof(err))) {
      plat_log("ERRORE: la mappa incorporata %s non e' valida: %s", MAP_TEXTS[i].id, err);
      snprintf(g_name[i], sizeof(g_name[i]), "%s (errore)", MAP_TEXTS[i].id);
      continue;
    }
    snprintf(g_name[i], sizeof(g_name[i]), "%s", g_tmp.name);
    g_boss[i] = g_tmp.boss != BOSS_NONE;
    g_world[i] = g_tmp.world;
  }
}

int maps_count(void) { return MAP_TEXT_COUNT < MAX_MAPS ? MAP_TEXT_COUNT : MAX_MAPS; }
const char* maps_id(int i) { return MAP_TEXTS[i].id; }
const char* maps_name(int i) { return g_name[i]; }
bool maps_is_boss(int i) { return g_boss[i]; }
int maps_world(int i) { return g_world[i]; }
const char* maps_note(int i) { return g_note[i]; }

bool maps_load(int i, Level* L, char* note, int notelen) {
  if (note && notelen > 0) snprintf(note, (size_t)notelen, "%s", g_note[i]);
  char err[160];
  if ((g_custom[i] && level_parse(L, g_custom[i], err, sizeof(err))) || level_parse(L, MAP_TEXTS[i].text, err, sizeof(err))) {
    snprintf(L->id, sizeof(L->id), "%s", MAP_TEXTS[i].id);
    return true;
  }
  plat_log("ERRORE: la mappa %s non si carica: %s", MAP_TEXTS[i].id, err);
  return false;
}

uint64_t maps_content_hash(void) {
  // FNV-1a su nome e testo di ogni mappa, come si caricano (il file personalizzato se e' valido).
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const char* s, size_t n) {
    for (size_t k = 0; k < n; ++k) h = (h ^ (uint8_t)s[k]) * 1099511628211ull;
  };
  for (int i = 0; i < maps_count(); ++i) {
    const char* id = MAP_TEXTS[i].id;
    const char* text = g_custom[i] ? g_custom[i] : MAP_TEXTS[i].text;
    mix(id, strlen(id) + 1);
    mix(text, strlen(text) + 1);
  }
  return h;
}
