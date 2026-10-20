// Lingue: la tabella dei testi (lang_data.h) e la ricerca per testo italiano (formato in lang.h).
#include "lang.h"

namespace {
int g_lang = LANG_IT;
const char* const NAMES[NLANGS] = { "Italiano", "English", "Español", "Português", "中文", "日本語", "हिन्दी" };
}  // namespace

const char* tr(const char* it) { return it; }
void lang_set(int l) { g_lang = l >= 0 && l < NLANGS ? l : LANG_IT; }
int lang_get(void) { return g_lang; }
const char* lang_name(int l) { return l >= 0 && l < NLANGS ? NAMES[l] : NAMES[0]; }
bool lang_cjk(void) { return g_lang == LANG_ZH || g_lang == LANG_JA; }
