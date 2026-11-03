// During play: the pause (with the controls panel) and the windows to join a game already
// started.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

using namespace screens;

// After the pause or after joining the game, the button used to confirm is still pressed: for
// the game it must not count as a new press (A would drop a bomb, B would jump).
static void swallow_held(Game& g, int i) { g.pl[i].prev_btn = g.in[i].btn; }

// --- GRAFICA (0.5.6): the graphics settings of this console --------------------------------------

// One step: dir -1 or 1 stops at the ends, 0 (A) goes on to the next one, back to lo after hi.
static int step_in(int v, int lo, int hi, int dir) {
  if (dir < 0) return v > lo ? v - 1 : lo;
  if (dir > 0) return v < hi ? v + 1 : hi;
  return v < hi ? v + 1 : lo;
}

static const int RES_W[4] = { 320, 640, 960, 1280 };

// Item r of GRAFICA changed on this console by player who (dir: -1 left, 1 right, 0 A): into
// save_data (the view and the core take it from there at the next frame; written when the pause
// closes). False if nothing changed (at an end, antialiasing not available).
static bool gfx_change(Game& g, int r, int dir, int who) {
  SaveData* sd = save_data();
  int v;
  switch (r) {
    case GF_DIST:
    case GF_PITCH: {
      int& c = r == GF_DIST ? sd->cam_dist : sd->cam_pitch;
      v = step_in(c, -SAVE_CAM_STEPS, SAVE_CAM_STEPS, dir);
      if (v == c) return false;
      c = v;
      plat_log("grafica: %s della telecamera %+d (giocatore %d)", r == GF_DIST ? "distanza" : "inclinazione", v, who + 1);
      return true;
    }
    case GF_AA: {
      // NO, 4X, 8X as far as the GPU goes.
      if (g.aa_max <= 0) return false;
      int vals[3], nv = 0;
      vals[nv++] = 0;
      vals[nv++] = g.aa_max >= 4 ? 4 : g.aa_max;
      if (g.aa_max >= 8) vals[nv++] = 8;
      const int now = game_aa_wanted(g);
      int k = 0;
      while (k < nv - 1 && vals[k] != now) k++;
      v = step_in(k, 0, nv - 1, dir);
      if (vals[v] == now) return false;
      g.aa_level = vals[v];
      sd->antialias = (uint8_t)vals[v];
      if (vals[v] > 0) plat_log("grafica: antialias %dx (giocatore %d)", vals[v], who + 1);
      else plat_log("grafica: antialias spento (giocatore %d)", who + 1);
      return true;
    }
    case GF_RES: {
      int k = 1;
      for (int i = 0; i < 4; ++i)
        if (RES_W[i] == sd->res) k = i;
      v = step_in(k, 0, 3, dir);
      if (RES_W[v] == sd->res) return false;
      sd->res = RES_W[v];
      plat_log("grafica: risoluzione %dx%d (giocatore %d)", sd->res, sd->res * 3 / 4, who + 1);
      return true;
    }
    case GF_DETAIL:
      v = step_in(sd->detail ? 1 : 0, 0, 1, dir);
      if (v == (sd->detail ? 1 : 0)) return false;
      sd->detail = (uint8_t)v;
      plat_log("grafica: dettagli %s (giocatore %d)", v ? "alti" : "bassi", who + 1);
      return true;
    case GF_MARGIN:
      v = step_in(sd->margin, 0, SAVE_MARGIN_MAX, dir);
      if (v == sd->margin) return false;
      sd->margin = (uint8_t)v;
      plat_log("grafica: margine TV %d (giocatore %d)", v, who + 1);
      return true;
    default: return false;
  }
}

static void unpause(Game& g) {
  g.paused = false;
  g.pause_help = g.pause_gfx = false;
  for (int j = 0; j < MAX_PLAYERS; ++j) swallow_held(g, j);
}

void play_menus_update(Game& g) {
  // Short START: pause (whoever opens it controls it), or closes it.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.start_tap[i] || g.pl[i].joining) continue;
    if (!g.paused) {
      if (!g.pl[i].active) continue;
      g.paused = true;
      g.pause_help = g.pause_gfx = false;
      g.pause_owner = i;
      g.pause_sel = 0;
      ev_sound_ui(g, SND_PAUSE);
      plat_log("pausa (giocatore %d)", i + 1);
    } else {
      unpause(g);
      plat_log("fine della pausa");
    }
    break;
  }
  if (g.paused) {
    int o = g.pause_owner;
    if (!g.pl[o].active)
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active) { o = i; break; }
    g.pause_owner = o;
    // In a LAN game the graphics and the volumes belong to whoever controls the pause: they are
    // changed only on that player's console.
    const bool here = !g.net || o == net_local(g);
    // Controls panel open: A or B closes it (START also closes the pause, see above).
    if (g.pause_help) {
      if (back(g, o) || pad_pressed(g, o, PAD_A)) {
        g.pause_help = false;
        ev_sound_ui(g, SND_UI_BACK);
      }
    } else if (g.pause_gfx) {
      // GRAFICA (0.5.6): up/down go through the items, left/right change the value (stopping at the
      // ends), A moves on to the next one (back to the first after the last); B back to the pause.
      if (back(g, o)) {
        g.pause_gfx = false;
        ev_sound_ui(g, SND_UI_BACK);
      } else {
        if (g.in[o].nav == NAV_UP) { g.gfx_sel = (g.gfx_sel + GFX_ITEMS - 1) % GFX_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
        if (g.in[o].nav == NAV_DOWN) { g.gfx_sel = (g.gfx_sel + 1) % GFX_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
        const int dir = g.in[o].nav == NAV_LEFT ? -1 : (g.in[o].nav == NAV_RIGHT ? 1 : (pad_pressed(g, o, PAD_A) ? 0 : 2));
        if (dir != 2 && here && gfx_change(g, g.gfx_sel, dir, o)) {
          ev_sound_ui(g, SND_UI_MOVE);
          settings_changed(g);
        }
      }
    } else {
      // Items: resume, controls, aim, graphics, music, sound effects, leave game, back to menu.
      if (g.in[o].nav == NAV_UP) { g.pause_sel = (g.pause_sel + PAUSE_ITEMS - 1) % PAUSE_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
      if (g.in[o].nav == NAV_DOWN) { g.pause_sel = (g.pause_sel + 1) % PAUSE_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
      const bool left = g.in[o].nav == NAV_LEFT, right = g.in[o].nav == NAV_RIGHT || pad_pressed(g, o, PAD_A);
      // Aim: left/right (or A) choose how responsive the right stick of the player in control is.
      if (g.pause_sel == PS_AIM) {
        int lvl = aim_level_of(g, o);
        if (left) lvl = (lvl + NAIMLEVELS - 1) % NAIMLEVELS;
        if (right) lvl = (lvl + 1) % NAIMLEVELS;
        if (lvl != g.aim_level[o]) {
          g.aim_level[o] = (uint8_t)lvl;
          if (!g.net && o < SAVE_PLAYERS) save_data()->aim[o] = (uint8_t)lvl;
          else if (g.net && o == net_local(g)) save_data()->aim[0] = (uint8_t)lvl;   // LAN game: the aim of this console
          ev_sound_ui(g, SND_UI_MOVE);
          settings_changed(g);
          plat_log("giocatore %d: mira %s", o + 1, AIM_TUNE[lvl].name);
        }
      }
      // Volumes (for all, from 0 to 10): left/right; A wraps around.
      if ((g.pause_sel == PS_MUSIC || g.pause_sel == PS_SFX) && (left || right) && here) {
        SaveData* sd = save_data();
        uint8_t& v = g.pause_sel == PS_MUSIC ? sd->music_vol : sd->sfx_vol;
        if (left) v = (uint8_t)(v > 0 ? v - 1 : 0);
        else if (g.in[o].nav == NAV_RIGHT) v = (uint8_t)(v < 10 ? v + 1 : 10);
        else v = (uint8_t)(v < 10 ? v + 1 : 0);
        audio_set_volume(sd->music_vol, sd->sfx_vol);
        ev_sound_ui(g, SND_UI_MOVE);
        settings_changed(g);
        plat_log("volume %s %d", g.pause_sel == PS_MUSIC ? "musica" : "effetti", (int)v);
      }
      if (back(g, o)) {
        ev_sound_ui(g, SND_UI_BACK);
        unpause(g);
      } else if (pad_pressed(g, o, PAD_A) && g.pause_sel == PS_CONTROLS) {
        g.pause_help = true;
        ev_sound_ui(g, SND_UI_OK);
      } else if (pad_pressed(g, o, PAD_A) && g.pause_sel == PS_GFX) {
        g.pause_gfx = true;
        g.gfx_sel = 0;
        ev_sound_ui(g, SND_UI_OK);
      } else if (pad_pressed(g, o, PAD_A) && (g.pause_sel == PS_RESUME || g.pause_sel == PS_LEAVE || g.pause_sel == PS_MENU)) {
        ev_sound_ui(g, SND_UI_OK);
        if (g.pause_sel == PS_RESUME) {
          unpause(g);
        } else if (g.pause_sel == PS_LEAVE) {
          player_leave(g, o);
          g.joined[o] = g.ready[o] = false;
          unpause(g);
          if (count_active(g) == 0) {
            back_to_title(g, TI_NEW);
            return;
          }
        } else {
          plat_log("partita interrotta dalla pausa");
          g.pause_help = g.pause_gfx = false;
          go_screen(g, SC_MAPS);
          return;
        }
      }
    }
  }
  // Windows to join a game already started: the others keep playing.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (p.active) continue;
    if (!p.joining) {
      const uint16_t pressed = (uint16_t)(g.in[i].btn & ~g.in[i].prev & ~((1u << PAD_START) | (1u << PAD_SELECT)));
      if (pressed) {
        p.joining = true;
        p.join_pick = g.pick[i];
      }
      continue;
    }
    if (g.in[i].nav == NAV_LEFT) p.join_pick = (p.join_pick + NCHARS - 1) % NCHARS;
    if (g.in[i].nav == NAV_RIGHT) p.join_pick = (p.join_pick + 1) % NCHARS;
    if (confirm(g, i)) {
      const int ch = p.join_pick;
      g.pick[i] = ch;
      g.joined[i] = true;
      player_join(g, i, ch, true);
      swallow_held(g, i);
    } else if (back(g, i)) {
      p.joining = false;
    }
  }
}

// Windows of whoever joins a game already started (also during the pause: then over the pause).
static bool join_panels(const Game& g, Hud* h) {
  bool any = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (p.active || !p.joining) continue;
    any = true;
    const float x = 32.5f + 147.0f * (float)i, y = 348.0f, W = 134.0f, H = 84.0f, cx = x + W * 0.5f;
    ui::panel(h, x, y, W, H, 12.0f, ui::SOLID, true);
    const float gw = ui::gp(h, x + 12.0f, y + 18.0f, i);
    ui::text(h, x + 20.0f + gw, y + 18.0f, HF_B13, O(ui::L1), tr("ENTRA"), 2.0f);
    const char* name = tr(CHARS[p.join_pick].name);
    const float nw = hud_text_w(HF_B16, name, 2.0f);
    ui::text(h, cx, y + 44.0f, HF_B16, O(ui::TX), name, 2.0f, 0);
    ui::chev(h, cx - nw * 0.5f - 18.0f, y + 38.0f, false, ui::L1);
    ui::chev(h, cx + nw * 0.5f + 10.0f, y + 38.0f, true, ui::L1);
    float kx = x + 14.0f;
    kx += ui::key(h, kx, y + 66.0f, "A") + 5.0f;
    ui::text(h, kx, y + 66.0f, HF_S13, O(ui::MU), tr("sì"), 0.5f);
    kx = x + W * 0.5f + 6.0f;
    kx += ui::key(h, kx, y + 66.0f, "B") + 5.0f;
    ui::text(h, kx, y + 66.0f, HF_S13, O(ui::MU), tr("no"), 0.5f);
  }
  return any;
}

namespace {

// Controls panel (COMANDI item of the pause).
void pause_help_hud(Hud* h) {
  ui::overlay(h, 0.5f);
  const float x = 60.0f, y = 96.0f, W = 520.0f, H = 236.0f;
  ui::panel(h, x, y, W, H, 16.0f, ui::SOLID, true);
  ui::text(h, x + 22.0f, y + 31.0f, HF_B24, O(ui::TX), tr("COMANDI"), 5.0f);
  const char* close = tr("chiudi");
  const float cw = hud_text_w(HF_S13, close, 1.0f);
  ui::key(h, x + W - 22.0f - cw - 29.0f, y + 31.0f, "B");
  ui::text(h, x + W - 22.0f, y + 31.0f, HF_S13, O(ui::MU), close, 1.0f, 1);
  ui::sep(h, x + 22.0f, y + 59.0f, W - 44.0f);
  const float c0 = x + 24.0f, c1 = x + 24.0f + 216.0f;   // the right column is the long one
  struct Row { const char* k1; const char* k2; bool stick; const char* label; bool hold; };
  static const Row ROWS[8] = {
    { "L", 0, true, N_("muoversi"), false }, { "R", 0, true, N_("mirare"), false },
    { "R1", 0, false, N_("colpo"), true }, { "L1", 0, false, N_("speciale"), false },
    { "R2", 0, false, N_("salto"), false }, { "L2", 0, false, N_("lascia una bomba"), false },
    { "START", 0, false, N_("pausa"), false }, { "SELECT", "START", false, N_("esci"), true },
  };
  for (int k = 0; k < 8; ++k) {
    const Row& r = ROWS[k];
    float px = k < 4 ? c0 : c1;
    const float cy = y + 66.0f + 18.0f + 36.0f * (float)(k % 4);
    const bool small = r.k1[1] && r.k1[2];
    if (r.stick) px += ui::stick(h, px, cy, r.k1) + 10.0f;
    else px += ui::keycap(h, px, cy, r.k1, small) + 10.0f;
    if (r.k2) {
      ui::text(h, px, cy, HF_S12, O(ui::MU), "+", 0.8f);
      px += hud_text_w(HF_S12, "+", 0.8f) + 10.0f;
      px += ui::keycap(h, px, cy, r.k2, true) + 10.0f;
    }
    // The name and (buttons to hold) TIENI up to the edge of the panel.
    const char* hold = tr("TIENI");
    const float hw = r.hold ? hud_text_w(HF_S12, hold, 0.8f) + 10.0f : 0.0f;
    char label[256];
    ui::fit(label, sizeof(label), tr(r.label), HF_S15, 0.6f, x + W - 22.0f - px - hw);
    ui::text(h, px, cy, HF_S15, O(ui::TX), label, 0.6f);
    if (r.hold) {
      px += hud_text_w(HF_S15, label, 0.6f) + 10.0f;
      ui::text(h, px, cy, HF_S12, O(ui::MU), hold, 0.8f);
    }
  }
}

// GRAFICA popup (0.5.6), at the bottom so that the game shows above it (at the top while someone is
// joining: their windows are at the bottom): the title with B to close, two columns of three items
// (up/down go through all six in order), at the bottom what the selected one does. In a LAN game,
// on the other consoles, who is changing their own graphics. The values are those of this console.
const float GX = 32.0f, GW = 576.0f, GH = 176.0f, GROW = 28.0f;

// Value of item r, on the right up to rx: notches (distance and pitch from -2 to 2, margin from 0
// to 4) or text; when selected with the arrows at the sides (left/right).
void gfx_value(const Game& g, Hud* h, int r, float rx, float cy, bool sel) {
  const SaveData* sd = save_data();
  const uint32_t vc = O(sel ? ui::ON : ui::MU);
  char v[32];
  v[0] = 0;
  int notches = 0, filled = 0;
  bool off = false;
  switch (r) {
    case GF_DIST: notches = 5; filled = sd->cam_dist + 3; break;
    case GF_PITCH: notches = 5; filled = sd->cam_pitch + 3; break;
    case GF_MARGIN: notches = SAVE_MARGIN_MAX; filled = sd->margin; break;
    case GF_AA:
      if (g.aa_max <= 0) {
        snprintf(v, sizeof(v), "%s", tr("ASSENTE"));
        off = true;
      } else if (game_aa_wanted(g) > 0) {
        snprintf(v, sizeof(v), "%dX", game_aa_wanted(g));
      } else {
        snprintf(v, sizeof(v), "%s", tr("NO"));
      }
      break;
    case GF_RES: snprintf(v, sizeof(v), "%d\xC3\x97%d", sd->res, sd->res * 3 / 4); break;
    case GF_DETAIL: snprintf(v, sizeof(v), "%s", sd->detail ? tr("ALTI") : tr("BASSI")); break;
    default: break;
  }
  const float NW = 7.0f;   // notch width (2 between them)
  const float vw = notches ? (float)notches * (NW + 2.0f) - 2.0f : hud_text_w(HF_S13, v, 0.8f);
  const bool arrows = sel && !off;
  const float vx = arrows ? rx - 16.0f : rx;
  if (notches) ui::segs(h, vx - vw, cy, notches, filled, NW, 10.0f, sel ? ui::ON : ui::L1, sel ? 0x8A3A5A : ui::OFF, !sel);
  else ui::text(h, vx, cy, HF_S13, off ? O(sel ? ui::ON : ui::DIS) : vc, v, 0.8f, 1);
  if (arrows) {
    ui::chev(h, rx - 8.0f, cy - 6.0f, true, ui::ON);
    ui::chev(h, vx - vw - 16.0f, cy - 6.0f, false, ui::ON);
  }
}

// Width of the value of item r with the arrows (the label stops before it).
float gfx_value_w(const Game& g, int r) {
  switch (r) {
    case GF_DIST:
    case GF_PITCH: return 5.0f * 9.0f - 2.0f + 32.0f;
    case GF_MARGIN: return (float)SAVE_MARGIN_MAX * 9.0f - 2.0f + 32.0f;
    case GF_AA:     // not available: without the arrows
      return g.aa_max <= 0 ? hud_text_w(HF_S13, tr("ASSENTE"), 0.8f) : fmaxf(hud_text_w(HF_S13, tr("NO"), 0.8f), hud_text_w(HF_S13, "8X", 0.8f)) + 32.0f;
    case GF_RES: return hud_text_w(HF_S13, "1280\xC3\x97" "960", 0.8f) + 32.0f;
    case GF_DETAIL: return fmaxf(hud_text_w(HF_S13, tr("ALTI"), 0.8f), hud_text_w(HF_S13, tr("BASSI"), 0.8f)) + 32.0f;
    default: return 0.0f;
  }
}

// The four corners of the picture, while the TV margin is being chosen: with the margin right all
// four are visible on the TV.
void corner_marks(Hud* h) {
  const float L = 30.0f, T = 4.0f, I = 6.0f;
  const uint32_t c = O(ui::L1);
  hud_rect(h, I, I, L, T, c);                       // top left
  hud_rect(h, I, I, T, L, c);
  hud_rect(h, 640.0f - I - L, I, L, T, c);          // top right
  hud_rect(h, 640.0f - I - T, I, T, L, c);
  hud_rect(h, I, 480.0f - I - T, L, T, c);          // bottom left
  hud_rect(h, I, 480.0f - I - L, T, L, c);
  hud_rect(h, 640.0f - I - L, 480.0f - I - T, L, T, c);   // bottom right
  hud_rect(h, 640.0f - I - T, 480.0f - I - L, T, L, c);
}

void pause_gfx_hud(const Game& g, Hud* h, bool joining) {
  const float GY = joining ? 80.0f : 480.0f - ui::SAFE_Y - GH;
  if (g.gfx_sel == GF_MARGIN) corner_marks(h);
  ui::panel(h, GX, GY, GW, GH, 16.0f, ui::SOLID, true);
  // Title and B to close.
  const char* close = tr("chiudi");
  const float cw = hud_text_w(HF_S13, close, 1.0f);
  char t[256];
  ui::fit(t, sizeof(t), tr("GRAFICA"), HF_B24, 5.0f, GW - 44.0f - cw - 40.0f);
  ui::text(h, GX + 22.0f, GY + 25.0f, HF_B24, O(ui::TX), t, 5.0f);
  ui::key(h, GX + GW - 22.0f - cw - 29.0f, GY + 25.0f, "B");
  ui::text(h, GX + GW - 22.0f, GY + 25.0f, HF_S13, O(ui::MU), close, 1.0f, 1);
  ui::sep(h, GX + 22.0f, GY + 47.0f, GW - 44.0f);
  // The items, in two columns.
  static const char* const LABEL[GFX_ITEMS] = { N_("DISTANZA"), N_("INCLINAZIONE"), N_("ANTIALIAS"), N_("RISOLUZIONE"), N_("DETTAGLI"),
                                                N_("MARGINE TV") };
  const float CW = (GW - 44.0f - 12.0f) * 0.5f;
  for (int r = 0; r < GFX_ITEMS; ++r) {
    const float x = GX + 22.0f + (r < 3 ? 0.0f : CW + 12.0f), y = GY + 54.0f + GROW * (float)(r % 3), cy = y + 13.0f;
    const bool sel = r == g.gfx_sel;
    if (sel) ui::bar_sel(h, x, y, CW, GROW - 2.0f);
    char lab[256];
    ui::fit(lab, sizeof(lab), tr(LABEL[r]), HF_S15, 0.8f, CW - 20.0f - gfx_value_w(g, r));
    ui::text(h, x + 10.0f, cy, HF_S15, O(sel ? ui::ON : ui::TX), lab, 0.8f);
    gfx_value(g, h, r, x + CW - 10.0f, cy, sel);
  }
  // At the bottom: what the selected item does; on the other consoles of a LAN game, who is
  // changing it (on their own console).
  const float hy = GY + 54.0f + GROW * 3.0f + 4.0f;
  if (g.net && g.pause_owner != net_local(g)) {
    const char* s = tr("sta cambiando la grafica della sua console");
    const float gw = ui::gp_w(g.pause_owner);
    char line[256];
    ui::fit(line, sizeof(line), s, HF_S13, 0.5f, GW - 44.0f - gw - 8.0f);
    const float x = GX + GW * 0.5f - (gw + 8.0f + hud_text_w(HF_S13, line, 0.5f)) * 0.5f;
    ui::gp(h, x, hy + 10.0f, g.pause_owner);
    ui::text(h, x + gw + 8.0f, hy + 10.0f, HF_S13, O(ui::MU), line, 0.5f);
    return;
  }
  static const char* const HELP[GFX_ITEMS] = {
    N_("la telecamera più vicina o più lontana dai personaggi"),
    N_("la telecamera più di lato o più dall'alto"),
    N_("bordi più lisci; la scheda grafica lavora di più"),
    N_("più nitida, ma la scheda grafica lavora di più: lo schermo della console è 640×480"),
    N_("bassi: niente ombre né paesaggio lontano, metà delle particelle"),
    N_("se la TV taglia i bordi: rimpicciolisci finché vedi i quattro angoli"),
  };
  char lines[2][ui::WRAP_LEN];
  const int n = ui::wrap(tr(HELP[g.gfx_sel]), GW - 44.0f, HF_S13, 0.3f, lines, 2);
  for (int k = 0; k < n; ++k) ui::text(h, GX + GW * 0.5f, hy + 10.0f + 16.0f * (float)k - (n > 1 ? 8.0f : 0.0f), HF_S13, O(ui::MU), lines[k], 0.3f, 0);
}

}  // namespace

bool gfx_layout_ok(Game& g, char* why, int n) {
  why[0] = 0;
  // The pause item, up to its arrow.
  const float IW = 336.0f - 24.0f;
  if (hud_text_w(HF_S16, tr("GRAFICA"), 1.0f) > IW - 26.0f - 20.0f) {
    snprintf(why, (size_t)n, "voce GRAFICA della pausa troppo lunga");
    return false;
  }
  if (hud_text_w(HF_B24, tr("GRAFICA"), 5.0f) > GW - 44.0f - hud_text_w(HF_S13, tr("chiudi"), 1.0f) - 40.0f) {
    snprintf(why, (size_t)n, "titolo del riquadro troppo lungo");
    return false;
  }
  static const char* const LABEL[GFX_ITEMS] = { "DISTANZA", "INCLINAZIONE", "ANTIALIAS", "RISOLUZIONE", "DETTAGLI", "MARGINE TV" };
  static const char* const HELP[GFX_ITEMS] = {
    "la telecamera più vicina o più lontana dai personaggi", "la telecamera più di lato o più dall'alto",
    "bordi più lisci; la scheda grafica lavora di più",
    "più nitida, ma la scheda grafica lavora di più: lo schermo della console è 640×480",
    "bassi: niente ombre né paesaggio lontano, metà delle particelle",
    "se la TV taglia i bordi: rimpicciolisci finché vedi i quattro angoli",
  };
  const float CW = (GW - 44.0f - 12.0f) * 0.5f;
  const int aa = g.aa_max;
  for (int pass = 0; pass < 2; ++pass) {       // with and without antialiasing ("ASSENTE")
    g.aa_max = pass ? 0 : 8;
    for (int r = 0; r < GFX_ITEMS; ++r) {
      const float room = CW - 20.0f - gfx_value_w(g, r);
      if (hud_text_w(HF_S15, tr(LABEL[r]), 0.8f) > room) {
        snprintf(why, (size_t)n, "voce %s: %.0f pixel, ce ne sono %.0f", LABEL[r], (double)hud_text_w(HF_S15, tr(LABEL[r]), 0.8f), (double)room);
        g.aa_max = aa;
        return false;
      }
    }
  }
  g.aa_max = aa;
  for (int r = 0; r < GFX_ITEMS; ++r) {
    char lines[2][ui::WRAP_LEN];
    bool cut = false;
    ui::wrap(tr(HELP[r]), GW - 44.0f, HF_S13, 0.3f, lines, 2, &cut);
    if (cut) {
      snprintf(why, (size_t)n, "spiegazione di %s piu' lunga di due righe", LABEL[r]);
      return false;
    }
  }
  if (hud_text_w(HF_S13, tr("sta cambiando la grafica della sua console"), 0.5f) > GW - 44.0f - ui::gp_w(3) - 8.0f) {
    snprintf(why, (size_t)n, "riga delle altre console troppo lunga");
    return false;
  }
  return true;
}

void play_menus_hud(const Game& g, Hud* h) {
  if (!g.paused) {
    join_panels(g, h);
    return;
  }
  bool joining = false, waiting = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    joining |= !g.pl[i].active && g.pl[i].joining;
    waiting |= !g.pl[i].active && !g.pl[i].joining && net_console(g, i);
  }
  // With GRAFICA open the veil is lighter: the game shows above the popup while the camera changes.
  ui::overlay(h, g.pause_gfx ? 0.3f : 0.72f);
  const float ta = g.pause_help || g.pause_gfx ? 0.3f : 1.0f;
  ui::title(h, 320, 22, HF_T44, tr("PAUSA"), 8.0f, true, ta);
  if (g.pause_gfx) {
    pause_gfx_hud(g, h, joining);
  } else if (!g.pause_help) {
    // While someone is joining, their windows sit at the bottom (from 348): the items shrink and
    // move up into the place of "COMANDA", so that they are all visible.
    const float RH = joining ? 26.0f : 32.0f, SEP = joining ? 7.0f : 11.0f;
    if (!joining) {
      const char* boss = tr("COMANDA");
      const float gw = 24.0f, tw = hud_text_w(HF_S13, boss, 2.0f), x0 = 320.0f - (gw + 8.0f + tw) * 0.5f;
      ui::gp(h, x0, 85.0f, g.pause_owner);
      ui::text(h, x0 + gw + 8.0f, 85.0f, HF_S13, O(ui::FTX), boss, 2.0f);
    }
    // The items.
    const float PX = 152.0f, PY = joining ? 80.0f : 104.0f, PW = 336.0f, IX = PX + 12.0f, IW = PW - 24.0f;
    const float PH = joining ? 24.0f + RH * (float)PAUSE_ITEMS + 2.0f * SEP : 312.0f;
    ui::panel(h, PX, PY, PW, PH);
    float y = PY + 12.0f;
    for (int k = 0; k < PAUSE_ITEMS; ++k) {
      if (k == PS_AIM || k == PS_LEAVE) {
        ui::sep(h, IX + 6.0f, y + SEP * 0.5f - 0.5f, IW - 12.0f);
        y += SEP;
      }
      const bool sel = k == g.pause_sel;
      const float cy = y + RH * 0.5f;
      if (sel) ui::bar_sel(h, IX, y, IW, RH);
      const uint32_t lc = O(sel ? ui::ON : (k >= PS_LEAVE ? ui::A2 : ui::TX)), vc = O(sel ? ui::ON : ui::MU);
      static const char* const LABEL[PAUSE_ITEMS] = { N_("RIPRENDI"), N_("COMANDI"), N_("MIRA"), N_("GRAFICA"), N_("MUSICA"),
                                                      N_("EFFETTI"), N_("LASCIA LA PARTITA"), N_("TORNA AL MENU") };
      // Item name up to the value on the right (aim: the arrows and the name; volumes: the notches).
      static const float VALUE_W[PAUSE_ITEMS] = { 0.0f, 20.0f, 150.0f, 20.0f, 100.0f, 100.0f, 0.0f, 0.0f };
      char lab[256];
      ui::text(h, IX + 14.0f, cy, HF_S16, lc, ui::fit(lab, sizeof(lab), tr(LABEL[k]), HF_S16, 1.0f, IW - 26.0f - VALUE_W[k]), 1.0f);
      const float rx = IX + IW - 12.0f;
      switch (k) {
        case PS_CONTROLS:
        case PS_GFX: ui::chev(h, rx - 8.0f, cy - 6.0f, true, sel ? ui::ON : ui::L1); break;
        case PS_AIM: {
          const char* v = tr(aim_tune(g, g.pause_owner).label);
          if (sel) {
            const float vw = hud_text_w(HF_S13, v, 0.8f);
            ui::chev(h, rx - 8.0f, cy - 6.0f, true, ui::ON);
            ui::text(h, rx - 16.0f, cy, HF_S13, vc, v, 0.8f, 1);
            ui::chev(h, rx - 24.0f - vw - 8.0f, cy - 6.0f, false, ui::ON);
          } else {
            ui::text(h, rx, cy, HF_S13, vc, v, 0.8f, 1);
          }
          break;
        }
        case PS_MUSIC:
        case PS_SFX: {
          const int v = k == PS_MUSIC ? save_data()->music_vol : save_data()->sfx_vol;
          ui::segs(h, rx - 88.0f, cy, 10, v, 7.0f, 10.0f, sel ? ui::ON : ui::L1, sel ? 0x8A3A5A : ui::OFF, !sel);
          break;
        }
        default: break;
      }
      y += RH;
    }
  } else {
    pause_help_hud(h);
  }
  // Whoever is not playing can join (also with the controls panel open, which ends up on top).
  if (joining) {
    join_panels(g, h);
  } else if (waiting && !g.pause_gfx) {
    const char* msg = tr("premete un tasto per entrare");
    const float cy = 480.0f - ui::SAFE_Y - 11.0f;
    float ww = 4.0f + hud_text_w(HF_S13, msg, 1.0f);
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active && !g.pl[i].joining && net_console(g, i)) ww += 30.0f;
    float x = 320.0f - ww * 0.5f;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active && !g.pl[i].joining && net_console(g, i)) x += ui::gp(h, x, cy, i) + 6.0f;
    ui::text(h, x + 4.0f, cy, HF_S13, O(ui::FTX), msg, 1.0f);
  }
}
