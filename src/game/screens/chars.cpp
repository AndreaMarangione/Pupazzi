// Hero choice: everyone with their own pad.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

const int CHARS_START_DELAY = 50;

// Opening the hero choice: whoever already plays starts from their hero; nobody is ready yet.
void chars_enter(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    g.ready[i] = false;
    g.joined[i] = g.pl[i].active;
    if (g.pl[i].active) g.pick[i] = g.pl[i].ch;
  }
  g.chars_t = 0;
}

void chars_update(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.joined[i]) {
      if (confirm(g, i)) {
        g.joined[i] = true;
        g.ready[i] = false;
        ev_sound_ui(g, SND_UI_OK);
      }
      continue;
    }
    if (!g.ready[i]) {
      if (g.in[i].nav == NAV_LEFT) { g.pick[i] = (g.pick[i] + NCHARS - 1) % NCHARS; ev_sound_ui(g, SND_UI_MOVE); }
      if (g.in[i].nav == NAV_RIGHT) { g.pick[i] = (g.pick[i] + 1) % NCHARS; ev_sound_ui(g, SND_UI_MOVE); }
      if (confirm(g, i)) { g.ready[i] = true; ev_sound_ui(g, SND_UI_OK); }
      else if (back(g, i)) {
        ev_sound_ui(g, SND_UI_BACK);
        g.joined[i] = false;
        if (g.pl[i].active) player_leave(g, i);
        bool any = false;
        for (int j = 0; j < MAX_PLAYERS; ++j) any |= g.joined[j];
        if (!any) { go_screen(g, SC_MAPS); return; }
      }
    } else if (back(g, i)) {
      g.ready[i] = false;
      ev_sound_ui(g, SND_UI_BACK);
    }
  }
  int joined = 0, ready = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    joined += g.joined[i];
    ready += g.joined[i] && g.ready[i];
  }
  if (joined > 0 && ready == joined) {
    if (++g.chars_t >= CHARS_START_DELAY) {
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (g.joined[i]) player_join(g, i, g.pick[i], false);
        else if (g.pl[i].active) player_leave(g, i);
      }
      start_map(g, g.map_sel);
    }
  } else {
    g.chars_t = 0;
  }
}

void chars_hud(const Game& g, Hud* h) {
  char buf[256], name[128];
  ui::scrim_top(h);
  ui::scrim_bottom(h, 160.0f);
  ui::title(h, 320, 22, HF_T38, tr("CHI GIOCA?"), 3.0f);
  hud_upper(name, tr(g.L.name), sizeof(name));
  const int w = maps_world(g.map_sel);
  snprintf(buf, sizeof(buf), tr("%s · %s · GIRO %d"), name, tr(WORLD_NAMES[w >= 0 && w < NWORLDS ? w : 0]), world_giro(w));
  {
    char fitted[256];
    ui::fit(fitted, sizeof(fitted), buf, HF_S15, 2.0f, 576.0f);
    ui::text(h, 321, 81, HF_S15, A(ui::SCRIM, 0.9f), fitted, 2.0f, 0);
    ui::text(h, 320, 80, HF_S15, O(ui::FTX), fitted, 2.0f, 0);
  }
  // The attack and the special of whoever chooses first.
  int show = -1;
  for (int i = 0; i < MAX_PLAYERS && show < 0; ++i)
    if (g.joined[i] && !g.ready[i]) show = i;
  if (show >= 0) {
    // Two lines above the heads: who (name, where from, what type), then attack and special.
    const CharDef& cd = CHARS[g.pick[show]];
    const char *nm = tr(cd.name), *normal = tr(cd.normal), *special = tr(cd.special);
    snprintf(buf, sizeof(buf), "%s · %s", tr(cd.who), tr(cd.role));
    const float w1 = 24.0f + 10.0f + hud_text_w(HF_B22, nm, 2.0f) + 12.0f + hud_text_w(HF_S13, buf, 0.5f);
    const float w2 = 2.0f * (34.0f + 8.0f) + hud_text_w(HF_S13, normal, 0.3f) + 20.0f + hud_text_w(HF_S13, special, 0.3f) + 2.0f;   // buttons 34 wide
    const float W = fminf(fmaxf(fmaxf(w1, w2) + 40.0f, 420.0f), 576.0f), X = floorf(320.0f - W * 0.5f);
    ui::panel(h, X, 94, W, 60, 12.0f);
    float x = X + 20.0f;
    x += ui::gp(h, x, 112, show) + 10.0f;
    ui::text(h, x, 112, HF_B22, O(ui::TX), nm, 2.0f);
    x += hud_text_w(HF_B22, nm, 2.0f) + 12.0f;
    char fitted[256];
    ui::text(h, x, 113, HF_S13, O(ui::MU), ui::fit(fitted, sizeof(fitted), buf, HF_S13, 0.5f, X + W - 20.0f - x), 0.5f);
    x = X + 20.0f;
    x += ui::keycap(h, x, 137, "R1", false) + 8.0f;
    ui::text(h, x, 137, HF_S13, O(ui::TX), normal, 0.3f);
    x += hud_text_w(HF_S13, normal, 0.3f) + 20.0f;
    x += ui::keycap(h, x, 137, "L1", false) + 8.0f;
    ui::text(h, x, 137, HF_S13, O(ui::L1), ui::fit(fitted, sizeof(fitted), special, HF_S13, 0.3f, X + W - 20.0f - x), 0.3f);
  }
  // One box per player, at the bottom.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const float x = 32.5f + 147.0f * (float)i, y = 346.0f, W = 134.0f, H = 82.0f, cx = x + W * 0.5f;
    if (!g.joined[i]) {
      ui::panel_off(h, x, y, W, H, 12.0f);
      ui::gp(h, x + 12.0f, y + 16.0f, i);
      if (!net_console(g, i)) {                   // LAN game: no console in this place
        ui::text(h, cx, y + 52.0f, HF_S13, O(ui::DIS), tr("nessuna console"), 0.5f, 0);
        continue;
      }
      if (blinking(g, 20)) {
        const char* t = tr("entra");
        const float kw = 22.0f + 7.0f + hud_text_w(HF_S13, t, 0.5f);
        ui::key(h, cx - kw * 0.5f, y + 52.0f, "A");
        ui::text(h, cx - kw * 0.5f + 29.0f, y + 52.0f, HF_S13, O(ui::MU), t, 0.5f);
      }
      continue;
    }
    const CharDef& cd = CHARS[g.pick[i]];
    if (g.ready[i]) ui::card_sel(h, x, y, W, H, 12.0f);
    else ui::panel(h, x, y, W, H, 12.0f);
    ui::gp(h, x + 12.0f, y + 16.0f, i);
    ui::face(h, x + W - 12.0f - 24.0f, y + 5.0f, 24, g.pick[i], 0);
    const char* nm = tr(cd.name);
    ui::text(h, cx, y + 46.0f, HF_B16, O(ui::TX), nm, 2.0f, 0);
    if (!g.ready[i]) {
      const float nw = hud_text_w(HF_B16, nm, 2.0f);
      ui::chev(h, cx - nw * 0.5f - 18.0f, y + 40.0f, false, ui::L1);
      ui::chev(h, cx + nw * 0.5f + 10.0f, y + 40.0f, true, ui::L1);
      const char* t = tr("pronto");
      const float kw = 22.0f + 7.0f + hud_text_w(HF_S13, t, 0.5f);
      ui::key(h, cx - kw * 0.5f, y + 66.0f, "A");
      ui::text(h, cx - kw * 0.5f + 29.0f, y + 66.0f, HF_S13, O(ui::MU), t, 0.5f);
    } else {
      ui::text(h, cx, y + 66.0f, HF_B13, O(ui::L1), tr("PRONTO!"), 2.0f, 0);
    }
  }
  if (g.chars_t > 0) ui::title(h, 320, 234, HF_T44, tr("SI PARTE!"), 6.0f, false);
  const char* it[6] = { "←→", tr("personaggio"), "A", tr("pronto"), "B", tr("esci") };
  ui::footer(h, it, 6);
}

}  // namespace screens
