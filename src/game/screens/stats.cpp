// End of a map: statistics, stars and the round.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// End of the map: the stars appear one at a time (with their sound), then the round; A works
// after the last star.
const int STAR_T0 = 40, STAR_STEP = 26, STATS_READY = STAR_T0 + 2 * STAR_STEP + 12;

void stats_update(Game& g) {
  for (int k = 0; k < 3; ++k)
    if (g.screen_t == STAR_T0 + STAR_STEP * k && ((g.stars_got >> k) & 1)) ev_sound_ui(g, SND_STAR);
  if (g.screen_t == STATS_READY && g.giro_up) ev_sound_ui(g, SND_ZONE_CLEAR);
  if (g.screen_t < STATS_READY) return;
  int who;
  if (any_confirm(g, &who)) {
    ev_sound_ui(g, SND_UI_OK);
    // Forward in the campaign: the next map of the same planet (with this one finished, it is
    // open). After the boss of a planet it goes to the next planet, if it already exists, otherwise
    // back to the first map of this one.
    const int w = maps_world(g.map_idx);
    int next = -1;
    for (int i = g.map_idx + 1; i < maps_count() && next < 0; ++i)
      if (maps_world(i) == w) next = i;
    if (next < 0) next = g.map_idx + 1;
    if (next >= maps_count() || !map_unlocked(next)) {
      const int first = world_first_map(w);
      next = first >= 0 ? first : 0;
      plat_log("si torna alla prima mappa di %s (giro %d)", WORLD_NAMES[w >= 0 && w < NWORLDS ? w : 0], world_giro(w));
    }
    g.map_sel = next;
    load_map_preview(g, next);
    go_screen(g, SC_CHARS);
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    go_screen(g, SC_MAPS);
  }
}

void stats_hud(const Game& g, Hud* h) {
  char buf[256], name[128];
  const bool boss = maps_is_boss(g.map_idx);
  const int w = maps_world(g.map_idx), wn = w >= 0 && w < NWORLDS ? w : 0;
  const char* wname = tr(WORLD_NAMES[wn]);
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 22, HF_T32, boss ? tr("BOSS SCONFITTO!") : tr("MAPPA COMPLETATA!"), 3.0f);
  hud_upper(buf, tr(g.L.name), sizeof(buf));
  ui::fit(name, sizeof(name), buf, HF_S15, 2.0f, 576.0f);
  ui::text(h, 321, 75, HF_S15, A(ui::SCRIM, 0.9f), name, 2.0f, 0);
  ui::text(h, 320, 74, HF_S15, O(ui::FTX), name, 2.0f, 0);
  // Table: everyone who played (also whoever ended up out of lives).
  int rows = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) rows += g.pl[i].active;
  const float TX0 = 88.0f, TW = 464.0f, TY = 92.0f, TH = 34.0f + 28.0f * (float)rows + 6.0f;
  ui::panel(h, TX0, TY, TW, TH);
  static const float COL[3] = { 372.0f, 452.0f, 532.0f };
  static const char* const HEAD[3] = { N_("SCONFITTI"), N_("DANNI"), N_("CADUTE") };
  for (int k = 0; k < 3; ++k) {
    char col[64];
    ui::text(h, COL[k], TY + 18.0f, HF_S12, O(ui::MU), ui::fit(col, sizeof(col), tr(HEAD[k]), HF_S12, 1.0f, 76.0f), 1.0f, 1);
  }
  float y = TY + 34.0f;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    const float cy = y + 14.0f;
    const bool mvp = i == g.mvp;
    if (mvp) ui::star(h, HI_STAR16, TX0 + 12.0f, cy - 8.0f, true);
    ui::face(h, TX0 + 34.0f, cy - 10.0f, 20, p.ch, 0);
    float x = TX0 + 60.0f;
    x += ui::gp(h, x, cy, i) + 8.0f;
    ui::text(h, x, cy, HF_S15, O(mvp ? ui::L1 : ui::TX), tr(CHARS[p.ch].name), 1.0f);
    const int v[3] = { p.kills, (int)(p.dmg_done + 0.5f), p.deaths };
    for (int k = 0; k < 3; ++k) {
      snprintf(buf, sizeof(buf), "%d", v[k]);
      ui::text(h, COL[k], cy, HF_S16, O(mvp ? ui::L1 : ui::TX), buf, 0.5f, 1);
    }
    y += 28.0f;
  }
  // Map stars, one at a time: those taken now "pop"; NUOVA! if it was not there in the round.
  static const char* const STAR_TXT[3] = { N_("mappa finita"), N_("esploratore salvato"), N_("senza ricominciare la mappa") };
  const float SY = TY + TH + 12.0f;
  ui::panel(h, TX0, SY, TW, 106.0f);
  for (int k = 0; k < 3; ++k) {
    const float cy = SY + 23.0f + 30.0f * (float)k;
    const bool shown = g.screen_t >= STAR_T0 + STAR_STEP * k;
    const bool got = shown && ((g.stars_got >> k) & 1);
    const float pop = got ? clampf((float)(g.screen_t - STAR_T0 - STAR_STEP * k) / 8.0f, 0.0f, 1.0f) : 1.0f;
    const int icon = pop >= 1.0f ? HI_STAR22 : (pop > 0.66f ? HI_STAR26 : (pop > 0.33f ? HI_STAR30 : HI_STAR34));
    const float s = (float)hud_icon_w(icon);
    ui::star(h, icon, TX0 + 34.0f - s * 0.5f, cy - s * 0.5f, got, O(ui::DIM2));
    ui::text(h, TX0 + 60.0f, cy, HF_S15, O(got ? ui::TX : ui::MU), tr(STAR_TXT[k]), 0.5f);
    if (got && ((g.stars_new >> k) & 1)) ui::tag(h, TX0 + TW - 16.0f, cy, tr("NUOVA!"), ui::A2, HF_B11, 1.5f, 6.0f, 21.0f, 1);
  }
  // The round of the planet: after the boss, how it went; otherwise the stars of the round.
  if (g.screen_t >= STATS_READY) {
    const float GY = SY + 106.0f + 12.0f;
    const float pop = clampf((float)(g.screen_t - STATS_READY) / 10.0f, 0.0f, 1.0f);
    if (boss && g.giro_done && g.giro_up) {
      ui::panel(h, TX0, GY, TW, 52.0f, 16.0f, ui::BASE, true);
      snprintf(buf, sizeof(buf), tr("GIRO %d APERTO!"), g.giro_was + 1);
      ui::text(h, 320, GY + 17.0f + 3.0f * (1.0f - pop), HF_B22, O(ui::L1), buf, 3.0f, 0);
      snprintf(buf, sizeof(buf), tr("%s: stelle da capo, mostri più forti"), wname);
      ui::text(h, 320, GY + 40.0f, HF_S13, O(ui::TX), buf, 0.5f, 0);
    } else {
      ui::panel(h, TX0, GY, TW, 52.0f);
      if (boss && g.giro_done) {
        if (g.giro_was >= SAVE_GIRO_MAX) {
          snprintf(buf, sizeof(buf), tr("%s: giro %d, l'ultimo!"), wname, g.giro_was);
          ui::text(h, 320, GY + 26.0f, HF_S15, O(ui::L1), buf, 0.5f, 0);
        } else {
          snprintf(buf, sizeof(buf), tr("per il giro %d servono %d stelle: ne hai %d"), g.giro_was + 1, giro_need(wn), g.giro_stars);
          ui::text(h, 320, GY + 17.0f, HF_S14, O(ui::L2), buf, 0.5f, 0);
          ui::text(h, 320, GY + 37.0f, HF_S13, O(ui::MU), tr("rifai le mappe per le stelle che mancano"), 0.5f, 0);
        }
      } else {
        const Giro gr = giro_of(w);
        snprintf(buf, sizeof(buf), tr("%s · GIRO %d"), wname, world_giro(w));
        ui::text(h, TX0 + 20.0f, GY + 26.0f, HF_B13, O(ui::L1), buf, 2.0f);
        ui::star(h, HI_STAR15, TX0 + 200.0f, GY + 18.5f, true);
        snprintf(buf, sizeof(buf), "%d/%d", gr.stars, gr.max);
        ui::text(h, TX0 + 222.0f, GY + 26.0f, HF_S14, O(ui::TX), buf, 1.0f);
        giro_text(buf, sizeof(buf), gr);
        ui::text(h, TX0 + TW - 20.0f, GY + 26.0f, HF_S13, O(gr.ready ? ui::L1 : ui::MU), buf, 1.0f, 1);
      }
    }
    const char* it[4] = { "A", tr("avanti"), "B", tr("mappe") };
    ui::footer(h, it, 4);
  }
}

}  // namespace screens
