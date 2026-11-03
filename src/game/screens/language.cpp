// Language screen (0.5.3).
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

void enter_lang(Game& g) {
  go_screen(g, SC_LANG);
  g.lang_sel = lang_get();
}

void lang_update(Game& g) {
  const int nav = any_nav(g);
  if (nav == NAV_UP) g.lang_sel = (g.lang_sel + NLANGS - 1) % NLANGS;
  if (nav == NAV_DOWN) g.lang_sel = (g.lang_sel + 1) % NLANGS;
  int who;
  if (any_confirm(g, &who)) {
    ev_sound_ui(g, SND_UI_OK);
    if (g.lang_sel != lang_get()) {
      g.session.info_t = 0;                                // a notice still on screen was in the prior language
      lang_set(g.lang_sel);
      save_data()->lang = (uint8_t)g.lang_sel;
      save_write();
      plat_log("lingua: %s", save_lang_id(g.lang_sel));
    }
    back_to_title(g, TI_LANG);
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    back_to_title(g, TI_LANG);
  }
}

void lang_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 26, HF_T32, tr("LINGUA"), 3.0f);
  const float RH = 38.0f, PW = 300.0f, PX = 170.0f, PY = 82.0f, IX = PX + 10.0f, IW = PW - 20.0f;
  ui::panel(h, PX, PY, PW, 16.0f + RH * (float)NLANGS);
  for (int l = 0; l < NLANGS; ++l) {
    const float y = PY + 8.0f + RH * (float)l, cy = y + RH * 0.5f;
    const bool sel = l == g.lang_sel, cur = l == lang_get();
    if (sel) ui::bar_sel(h, IX, y + 2.0f, IW, RH - 4.0f);
    if (cur) hud_ellipse(h, IX + 16.0f, cy, 4.0f, 4.0f, O(sel ? ui::ON : ui::L1));
    const int keep = hud_cjk_get();
    hud_cjk(l == LANG_JA ? HUD_CJK_JP : HUD_CJK_SC);       // 中文 and 日本語 each with its own glyphs
    ui::text(h, IX + 32.0f, cy, HF_S16, O(sel ? ui::ON : ui::TX), lang_name(l), 1.0f);
    hud_cjk(keep);
  }
  const char* it[4] = { "A", tr("scegli"), "B", tr("indietro") };
  ui::footer(h, it, 4);
  ui::version(h);
}

}  // namespace screens
