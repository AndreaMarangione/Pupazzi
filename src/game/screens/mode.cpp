// COME SI GIOCA?: together (the campaign) or against (still to come).
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// The two cards sit side by side: switch with right/left (and also up/down). In a LAN game B opens
// the window "finire la partita in rete?" (yes takes all the consoles back to the title).
void mode_update(Game& g) {
  int who;
  if (g.net_pop) {
    if (any_confirm(g, &who)) {
      ev_sound_ui(g, SND_UI_OK);
      g.net_pop = false;
      plat_log("partita in rete finita da COME SI GIOCA? (giocatore %d)", who + 1);
      back_to_title(g, TI_NET);
    } else if (any_back(g)) {
      ev_sound_ui(g, SND_UI_BACK);
      g.net_pop = false;
    }
    return;
  }
  const int nav = any_nav(g);
  if (nav != NAV_NONE) g.menu_sel ^= 1;
  if (any_confirm(g, &who)) {
    if (g.menu_sel == 0) {
      ev_sound_ui(g, SND_UI_OK);
      go_screen(g, SC_MAPS);
    } else {
      ev_sound_ui(g, SND_UI_BACK);
      info(g, tr("CONTRO arriva con la versione 0.6"));
    }
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    if (g.net) g.net_pop = true;
    else back_to_title(g, TI_NEW);
  }
}

// Card of Come si gioca: Insieme (the 4 heroes) or Contro (2 against 2, coming soon).
void mode_card(Hud* h, float x, float y, bool insieme, bool sel) {
  const float W = 262.0f, H = 112.0f, cx0 = x + 20.0f;
  if (sel) ui::card_sel(h, x, y, W, H);
  else ui::panel_off(h, x, y, W, H);
  char t[256];
  if (insieme) {
    for (int k = 0; k < 4; ++k) ui::avatar(h, cx0 + 27.0f * (float)k, y + 13.0f, k, false);
    ui::text(h, cx0, y + 64.0f, HF_T25, O(ui::TX), tr("INSIEME"), 3.0f);
    ui::text(h, cx0, y + 91.5f, HF_S14, O(ui::L1), ui::fit(t, sizeof(t), tr("tutti insieme contro i mostri"), HF_S14, 0.5f, W - 40.0f), 0.5f);
  } else {
    ui::avatar(h, cx0, y + 13.0f, 0, true);
    ui::avatar(h, cx0 + 27.0f, y + 13.0f, 1, true);
    const float vx = cx0 + 59.0f + 8.0f, vw = hud_text_w(HF_B12, "VS", 1.0f);
    ui::text(h, vx, y + 29.0f, HF_B12, A(ui::TX, 0.45f), "VS", 1.0f);
    ui::avatar(h, vx + vw + 8.0f, y + 13.0f, 2, true);
    ui::avatar(h, vx + vw + 35.0f, y + 13.0f, 3, true);
    ui::text(h, cx0, y + 64.0f, HF_T25, O(ui::DIS), tr("CONTRO"), 3.0f);
    ui::text(h, cx0, y + 91.5f, HF_S14, O(ui::DIS), tr("2 contro 2"), 0.5f);
    ui::tag(h, x + W - 16.0f, y + H - 26.5f, tr("IN ARRIVO"), ui::A2, HF_B11, 1.5f, 6.0f, 21.0f, 1);
  }
}

void mode_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 36, HF_T38, tr("COME SI GIOCA?"), 3.0f);
  mode_card(h, 52, 308, true, g.menu_sel == 0);
  mode_card(h, 326, 308, false, g.menu_sel == 1);
  if (!g.net_pop) {
    footer(g, h, tr("conferma"), g.net ? tr("esci dalla rete") : tr("indietro"));
    return;
  }
  // In a LAN game: B asks whether to end the game for everyone.
  ui::overlay(h, 0.5f);
  const float W = 380.0f, X = 130.0f, Y = 162.0f, H = 146.0f;
  ui::panel(h, X, Y, W, H, 16.0f, ui::SOLID, true);
  char t[256];
  ui::fit(t, sizeof(t), tr("FINIRE LA PARTITA IN RETE?"), HF_B16, 1.5f, W - 44.0f);
  ui::text(h, 320.0f, Y + 32.0f, HF_B16, O(ui::TX), t, 1.5f, 0);
  ui::fit(t, sizeof(t), tr("tutte le console tornano al titolo"), HF_S13, 0.5f, W - 44.0f);
  ui::text(h, 320.0f, Y + 66.0f, HF_S13, O(ui::MU), t, 0.5f, 0);
  yes_no(h, Y + 116.0f);
  ui::version(h, PUPAZZI_VERSION);
}

}  // namespace screens
