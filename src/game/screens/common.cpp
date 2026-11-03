// Shared by the screens: menu input (A confirms, B goes back, a short START counts as A), notices,
// footers and small widgets of the Orbita style.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

bool confirm(Game& g, int i) { return pad_pressed(g, i, PAD_A) || g.start_tap[i]; }

bool back(Game& g, int i) { return pad_pressed(g, i, PAD_B); }

bool any_confirm(Game& g, int* who) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (confirm(g, i)) { *who = i; return true; }
  return false;
}

bool any_back(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (back(g, i)) return true;
  return false;
}

int any_nav(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.in[i].nav) {
      ev_sound_ui(g, SND_UI_MOVE);
      return g.in[i].nav;
    }
  return NAV_NONE;
}

void info(Game& g, const char* s) {
  snprintf(g.session.info, sizeof(g.session.info), "%s", s);
  g.session.info_t = 120;
}

// Le schermate si preparano prima di andarci: gia' il primo fotogramma e' quello giusto (posti
// aggiornati, cursore al suo posto, nessuna finestra aperta). All'avvio il titolo si prepara nel
// suo primo aggiornamento (i salvataggi si aprono dopo).
void back_to_title(Game& g, int item) {
  go_screen(g, SC_TITLE);
  slots_refresh(g);
  g.menu_sel = item;
}

bool blinking(const Game& g, unsigned period) { return ((g.ui_tick / period) & 1u) == 0u; }

// Avviso al posto del piede (in arrivo, mappa chiusa, errore di una mappa personalizzata): fino a
// tre righe, larghe al massimo 440 pixel (a destra resta il posto per il numero della versione).
void toast(Hud* h, const char* s, uint32_t color) {
  char L[3][ui::WRAP_LEN];
  const int n = ui::wrap(s, 440.0f, HF_S14, 0.5f, L, 3);
  float tw = 0.0f;
  for (int k = 0; k < n; ++k) tw = fmaxf(tw, hud_text_w(HF_S14, L[k], 0.5f));
  const float w = tw + 40.0f, hh = 12.0f + 18.0f * (float)n, y = 480.0f - ui::SAFE_Y - hh;
  ui::panel(h, 320.0f - w * 0.5f, y, w, hh, 8.0f, ui::SOLID);
  for (int k = 0; k < n; ++k) ui::text(h, 320.0f, y + 15.0f + 18.0f * (float)k, HF_S14, O(color), L[k], 0.5f, 0);
}

void footer(const Game& g, Hud* h, const char* a_label, const char* b_label) {
  if (g.session.info_t > 0) {
    toast(h, g.session.info, ui::L1);
  } else {
    const char* it[4] = { "A", a_label, "B", b_label };
    ui::footer(h, it, b_label ? 4 : 2);
  }
  ui::version(h);
}

// "A si'   B no" centrati su cy (come le finestre per entrare).
void yes_no(Hud* h, float cy) {
  const char* yes = tr("sì");
  const char* no = tr("no");
  const float ww = 22.0f + 7.0f + hud_text_w(HF_S14, yes, 0.5f) + 34.0f + 22.0f + 7.0f + hud_text_w(HF_S14, no, 0.5f);
  float kx = 320.0f - ww * 0.5f;
  kx += ui::key(h, kx, cy, "A") + 7.0f;
  ui::text(h, kx, cy, HF_S14, O(ui::TX), yes, 0.5f);
  kx += hud_text_w(HF_S14, yes, 0.5f) + 34.0f;
  kx += ui::key(h, kx, cy, "B") + 7.0f;
  ui::text(h, kx, cy, HF_S14, O(ui::TX), no, 0.5f);
}

}  // namespace screens
