// Map choice, with the round and the stars of the planet.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

void maps_update(Game& g) {
  const int n = maps_count();
  const int nav = any_nav(g);
  if (nav == NAV_UP) { g.map_sel = (g.map_sel + n - 1) % n; g.map_load_t = 0; }
  if (nav == NAV_DOWN) { g.map_sel = (g.map_sel + 1) % n; g.map_load_t = 0; }
  if (g.map_shown != g.map_sel && ++g.map_load_t > 10) {
    load_map_preview(g, g.map_sel);
    ev_camera(g, CAM_CUT);
  }
  int who;
  if (any_confirm(g, &who)) {
    if (!map_unlocked(g.map_sel)) {
      ev_sound_ui(g, SND_UI_BACK);
      const int w = maps_world(g.map_sel);
      if (map_in_world(g.map_sel) > 0 && world_first_map(w) >= 0 && map_unlocked(world_first_map(w))) {
        info(g, tr("si apre finendo la mappa prima"));
      } else if (w > 0 && w < NWORLDS) {
        char buf[128];
        snprintf(buf, sizeof(buf), tr("si apre battendo il boss di %s"), tr(WORLD_NAMES[w - 1]));
        info(g, buf);
      } else {
        info(g, tr("mappa chiusa"));
      }
      return;
    }
    ev_sound_ui(g, SND_UI_OK);
    load_map_preview(g, g.map_sel);
    go_screen(g, SC_CHARS);
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    go_screen(g, SC_MODE);
  }
}

Giro giro_of(int w) {
  Giro r;
  r.stars = world_stars(w);
  r.max = 3 * world_maps(w);
  r.need = giro_need(w);
  r.giro = world_giro(w);
  r.last = r.giro >= SAVE_GIRO_MAX;
  r.ready = !r.last && r.stars >= r.need;
  return r;
}

void giro_text(char* buf, int n, const Giro& gr) {
  if (gr.last) snprintf(buf, (size_t)n, tr("GIRO %d, L'ULTIMO"), gr.giro);
  else if (gr.ready) snprintf(buf, (size_t)n, tr("BOSS → GIRO %d"), gr.giro + 1);
  else snprintf(buf, (size_t)n, tr("%d → GIRO %d"), gr.need, gr.giro + 1);
}

void maps_hud(const Game& g, Hud* h) {
  char buf[128], name[128];
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 28, HF_T32, tr("SCEGLI LA MAPPA"), 3.0f);
  int w = maps_world(g.map_sel);
  if (w < 0 || w >= NWORLDS) w = 0;
  int ids[24], n = 0;
  for (int i = 0; i < maps_count() && n < 24; ++i)
    if (maps_world(i) == w) ids[n++] = i;
  int soon[NWORLDS], ns = 0;
  for (int k = 0; k < NWORLDS; ++k)
    if (world_maps(k) == 0) soon[ns++] = k;
  // Con la nota di una mappa personalizzata (fino a tre righe al posto del piede) niente pianeti in arrivo.
  const bool note = maps_note(g.map_sel)[0] && g.session.info_t <= 0;
  if (note) ns = 0;
  const int soon_rows = (ns + 3) / 4;
  const float PX = 120.0f, PW = 400.0f, IX = PX + 14.0f, IW = PW - 28.0f;
  float PH = 14.0f + 30.0f + 17.0f + 36.0f * (float)n + 2.0f * (float)(n - 1) + 32.0f + 14.0f;
  if (ns) PH += 21.0f + 22.0f + 17.0f * (float)soon_rows + 6.0f * (float)(soon_rows - 1);
  const float PY = 76.0f;
  ui::panel(h, PX, PY, PW, PH);
  // Pianeta e giro.
  float y = PY + 14.0f;
  if (w == 0) ui::earth(h, IX + 4.0f, y + 2.0f);
  else ui::planet(h, IX + 9.5f, y + 7.5f, w);
  ui::text(h, IX + 40.0f, y + 15.0f, HF_B22, O(ui::TX), tr(WORLD_NAMES[w]), 3.0f);
  snprintf(buf, sizeof(buf), tr("GIRO %d"), world_giro(w));
  ui::tag(h, IX + IW - 4.0f, y + 15.0f, buf, ui::L1, HF_B13, 2.0f, 8.0f, 23.0f, 1);
  y += 30.0f + 10.0f;
  ui::sep(h, IX, y, IW);
  y += 7.0f;
  // Le mappe.
  for (int k = 0; k < n; ++k) {
    const int i = ids[k];
    const bool sel = i == g.map_sel, open = map_unlocked(i);
    const float cy = y + 18.0f;
    if (sel) ui::bar_sel(h, IX, y, IW, 36.0f);
    const uint32_t tc = O(sel ? ui::ON : (open ? ui::TX : ui::DIS));
    float nx = IX + 10.0f;
    if (maps_is_boss(i)) {
      nx += ui::tag(h, nx, cy, tr("BOSS"), sel ? ui::ON : ui::A2, HF_B12, 1.0f, 5.0f, 20.0f) + 10.0f;
    } else {
      snprintf(buf, sizeof(buf), "%02d", map_in_world(i) + 1);
      ui::text(h, nx, cy, HF_S13, O(sel ? ui::ON : ui::L1), buf);
      nx += 32.0f;
    }
    hud_upper(buf, tr(maps_name(i)), sizeof(buf));
    ui::fit(name, sizeof(name), buf, HF_S17, 0.8f, IX + IW - 12.0f - 54.0f - 10.0f - nx);   // fino alle stelle
    ui::text(h, nx, cy, HF_S17, tc, name, 0.8f);
    if (open) ui::stars3(h, IX + IW - 12.0f - 54.0f, cy, save_stars(maps_id(i)), sel);
    else ui::text(h, IX + IW - 12.0f, cy, HF_S13, tc, tr("CHIUSA"), 1.0f, 1);
    y += 38.0f;
  }
  y -= 2.0f;
  // Stelle del giro: quante ne servono per il giro dopo.
  const Giro gr = giro_of(w);
  y += 12.0f;
  const float cy = y + 10.0f;
  ui::star(h, HI_STAR15, IX + 6.0f, cy - 7.5f, true);
  snprintf(buf, sizeof(buf), "%d/%d", gr.stars, gr.max);
  ui::text(h, IX + 31.0f, cy, HF_S14, O(ui::TX), buf, 1.0f);
  const float lx = IX + 31.0f + hud_text_w(HF_S14, buf, 1.0f) + 10.0f;
  giro_text(buf, sizeof(buf), gr);
  ui::text(h, IX + IW - 6.0f, cy, HF_S13, O(gr.ready ? ui::L1 : ui::MU), buf, 1.0f, 1);
  const float rx = IX + IW - 6.0f - hud_text_w(HF_S13, buf, 1.0f) - 10.0f;
  const int segs = gr.last ? (gr.max < 12 ? gr.max : 12) : (gr.need < 12 ? gr.need : 12);
  const int fill = gr.stars < segs ? gr.stars : segs;
  const float sw = 16.0f * (float)segs - 2.0f;
  if (segs > 0 && rx - lx >= sw) ui::segs(h, lx + (rx - lx - sw) * 0.5f, cy, segs, fill, 14.0f, 10.0f, ui::STAR, ui::OFF, true);
  y += 20.0f;
  // I pianeti che arrivano con le prossime versioni.
  if (ns) {
    y += 12.0f;
    ui::sep(h, IX, y, IW);
    y += 9.0f;
    ui::text(h, IX + 6.0f, y + 8.0f, HF_S12, O(ui::MU), tr("IN ARRIVO"), 2.0f);
    y += 22.0f;
    // Quattro colonne larghe quanto serve (Saturno ha gli anelli), lo spazio che avanza tra l'una e l'altra.
    float cw[4] = { 0, 0, 0, 0 }, sum = 0.0f;
    for (int k = 0; k < ns; ++k) {
      const float iw = soon[k] == 6 ? 19.0f : 15.0f;
      cw[k % 4] = fmaxf(cw[k % 4], iw + 7.0f + hud_text_w(HF_S13, tr(WORLD_NAMES[soon[k]]), 0.8f));
    }
    for (int c = 0; c < 4; ++c) sum += cw[c];
    const float gap = fmaxf((IW - 12.0f - sum) / 3.0f, 8.0f);
    for (int k = 0; k < ns; ++k) {
      float px = IX + 6.0f;
      for (int c = 0; c < k % 4; ++c) px += cw[c] + gap;
      const float pcy = y + 8.5f + 23.0f * (float)(k / 4);
      ui::planet(h, px, pcy - 7.5f, soon[k]);
      const float iw = soon[k] == 6 ? 19.0f : 15.0f;
      ui::text(h, px + iw + 7.0f, pcy, HF_S13, O(ui::MU), tr(WORLD_NAMES[soon[k]]), 0.8f);
    }
  }
  // Mappa personalizzata con un problema: la nota al posto del piede.
  if (note) {
    toast(h, maps_note(g.map_sel), ui::A2);
    ui::version(h);
  } else {
    footer(g, h, tr("gioca"), tr("indietro"));
  }
}

}  // namespace screens
