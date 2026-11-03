// Title screen (0.5.3): new game, load, LAN game (0.5.4), manage saves, language, exit.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

void title_update(Game& g) {
  if (g.screen_t == 1) slots_refresh(g);
  const int nav = any_nav(g);
  if (nav == NAV_UP) g.menu_sel = (g.menu_sel + TITLE_ITEMS - 1) % TITLE_ITEMS;
  if (nav == NAV_DOWN) g.menu_sel = (g.menu_sel + 1) % TITLE_ITEMS;
  int who;
  if (!any_confirm(g, &who)) return;
  switch (g.menu_sel) {
    case TI_NEW: {
      ev_sound_ui(g, SND_UI_OK);
      const int free = first_free_slot(g);
      if (free >= 0) enter_name(g, NM_NEW, free, who, false);
      else enter_slots(g, SL_OVERWRITE);
      break;
    }
    case TI_LOAD:
    case TI_MANAGE:
      if (used_slots(g) == 0) {
        ev_sound_ui(g, SND_UI_BACK);
        info(g, tr("nessun salvataggio: comincia una nuova partita"));
      } else {
        ev_sound_ui(g, SND_UI_OK);
        g.menu_owner = who;
        enter_slots(g, g.menu_sel == TI_LOAD ? SL_LOAD : SL_MANAGE);
      }
      break;
    case TI_LANG:
      ev_sound_ui(g, SND_UI_OK);
      enter_lang(g);
      break;
    case TI_NET:
      ev_sound_ui(g, SND_UI_OK);
      g.menu_owner = who;
      go_screen(g, SC_NET);
      break;
    default:
      ev_sound_ui(g, SND_UI_OK);
      plat_log("scelto Esci dal titolo");
      game_request_quit(g);
      break;
  }
}

void title_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  // The orbit around the name, with the little cyan planet.
  HudShape r;
  memset(&r, 0, sizeof(r));
  r.type = HS_RING; r.x = 40; r.y = 22; r.w = 560; r.h = 132; r.ring = 2.0f; r.soft = 12.0f; r.c0 = r.c1 = A(0xFF785A, 0.45f);
  hud_shape(h, r);
  hud_ring(h, 320, 88, 280, 66, 2.0f, ui::RING);
  hud_ellipse(h, 558, 127, 7, 7, A(ui::A2, 0.9f), 10.0f);
  hud_ellipse(h, 558, 127, 7, 7, O(ui::A2));
  const float base = hud_mid(HF_LOGO, 44.0f + 40.0f);
  hud_text(h, 320, base + 3.0f, HF_LOGO, O(ui::TDROP), "PUPAZZI", 9.0f, 0);
  hud_text_glow(h, 320, base, HF_LOGO, ui::TGLOW, "PUPAZZI", 9.0f, 0);
  hud_text(h, 320, base, HF_LOGO, 0xFFFFFFFFu, "PUPAZZI", 9.0f, 0);
  const float cy = 164.0f + 9.75f;
  char tag[256];
  ui::fit(tag, sizeof(tag), tr("DA 1 A 4 AMICI CONTRO I MOSTRI"), HF_S15, 2.0f, 576.0f);
  ui::text(h, 320, cy + 1.0f, HF_S15, A(ui::SCRIM, 0.9f), tag, 2.0f, 0);
  ui::text(h, 320, cy, HF_S15, O(ui::FTX), tag, 2.0f, 0);
  // The items on the right (the heroes are on the left: the title camera is shifted).
  const float PX = 372.0f, PW = 236.0f, PY = 196.0f, RH = 34.0f, IX = PX + 10.0f, IW = PW - 20.0f;
  ui::panel(h, PX, PY, PW, 16.0f + RH * (float)TITLE_ITEMS);
  static const char* const LABEL[TITLE_ITEMS] = { N_("NUOVA PARTITA"), N_("CARICA SALVATAGGIO"), N_("GIOCO IN RETE"), N_("GESTISCI SALVATAGGI"),
                                                  N_("LINGUA"), N_("ESCI") };
  const bool none = used_slots(g) == 0;
  for (int k = 0; k < TITLE_ITEMS; ++k) {
    const float y = PY + 8.0f + RH * (float)k, cy2 = y + RH * 0.5f;
    const bool sel = k == g.menu_sel, off = none && (k == TI_LOAD || k == TI_MANAGE);
    if (sel) ui::bar_sel(h, IX, y + 2.0f, IW, RH - 4.0f);
    const uint32_t c = O(sel ? ui::ON : (off ? ui::DIS : (k == TI_QUIT ? ui::A2 : ui::TX)));
    float tx = IX + 14.0f, room = IW - 28.0f;
    if (k == TI_LANG) {                        // the globe: it can be found even without understanding the language
      ui::globe(h, tx + 7.5f, cy2, sel ? ui::ON : ui::L1);
      tx += 23.0f;
      room -= 23.0f;
      const char* ln = lang_name(lang_get());
      const float lw = hud_text_w(HF_S13, ln, 0.5f);
      ui::text(h, IX + IW - 14.0f, cy2, HF_S13, O(sel ? ui::ON : ui::MU), ln, 0.5f, 1);
      room -= lw + 10.0f;
    }
    ui::fit(tag, sizeof(tag), tr(LABEL[k]), HF_S16, 1.0f, room);
    ui::text(h, tx, cy2, HF_S16, c, tag, 1.0f);
  }
  if (g.session.info_t > 0) toast(h, g.session.info, ui::L1);
  else {
    const char* it[2] = { "A", tr("conferma") };
    ui::footer(h, it, 2);
  }
  ui::version(h, PUPAZZI_VERSION);
}

}  // namespace screens
