// Lingue: la tabella dei testi (lang_data.h, da lang/testi.txt) e la ricerca per testo italiano
// (formato in lang.h). Si cerca per hash FNV-1a del testo, poi si confronta il testo: un testo che
// non c'e' (nome di una mappa personalizzata, numeri) resta com'e'.
#include "lang.h"
#include "hud.h"
#include "lang_data.h"
#include <string.h>

namespace {

int g_lang = LANG_IT;
const char* const NAMES[NLANGS] = { "Italiano", "English", "Español", "Português", "中文", "日本語", "हिन्दी" };

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

int find(const char* it) {
  if (!it || !*it) return -1;
  const uint32_t h = fnv(it);
  int lo = 0, hi = LANG_NTEXTS - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    if (LANG_HASH[mid] < h) lo = mid + 1;
    else hi = mid - 1;
  }
  for (int i = lo; i < LANG_NTEXTS && LANG_HASH[i] == h; ++i)   // stesso hash: si guarda il testo
    if (!strcmp(LANG_TEXT[i][0], it)) return i;
  return -1;
}

}  // namespace

const char* tr_in(int l, const char* it) {
  if (l <= LANG_IT || l >= NLANGS) return it;
  const int i = find(it);
  return i >= 0 ? LANG_TEXT[i][l] : it;
}

const char* tr(const char* it) { return tr_in(g_lang, it); }

bool tr_known(const char* it) { return find(it) >= 0; }

void lang_set(int l) {
  g_lang = l >= 0 && l < NLANGS ? l : LANG_IT;
  hud_cjk(g_lang == LANG_JA ? HUD_CJK_JP : HUD_CJK_SC);
}

int lang_get(void) { return g_lang; }
const char* lang_name(int l) { return l >= 0 && l < NLANGS ? NAMES[l] : NAMES[0]; }
bool lang_cjk(void) { return g_lang == LANG_ZH || g_lang == LANG_JA; }
