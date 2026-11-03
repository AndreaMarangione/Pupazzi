// During play: the pause (with the controls panel) and the windows to join a game already
// started.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

using namespace screens;

// Dopo la pausa o l'ingresso in partita il tasto con cui si e' confermato e' ancora premuto:
// per il gioco non deve contare come una pressione nuova (A lascerebbe una bomba, B salterebbe).
static void swallow_held(Game& g, int i) { g.pl[i].prev_btn = g.in[i].btn; }

static void unpause(Game& g) {
  g.paused = false;
  g.pause_help = false;
  for (int j = 0; j < MAX_PLAYERS; ++j) swallow_held(g, j);
}

void play_menus_update(Game& g) {
  // START breve: pausa (chi la apre la comanda), oppure la chiude.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.start_tap[i] || g.pl[i].joining) continue;
    if (!g.paused) {
      if (!g.pl[i].active) continue;
      g.paused = true;
      g.pause_help = false;
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
    // Riquadro dei comandi aperto: A o B lo chiudono (START chiude anche la pausa, vedi sopra).
    if (g.pause_help) {
      if (back(g, o) || pad_pressed(g, o, PAD_A)) {
        g.pause_help = false;
        ev_sound_ui(g, SND_UI_BACK);
      }
    } else {
      // Voci: riprendi, comandi, mira, antialias, musica, effetti, lascia la partita, torna al menu.
      if (g.in[o].nav == NAV_UP) { g.pause_sel = (g.pause_sel + PAUSE_ITEMS - 1) % PAUSE_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
      if (g.in[o].nav == NAV_DOWN) { g.pause_sel = (g.pause_sel + 1) % PAUSE_ITEMS; ev_sound_ui(g, SND_UI_MOVE); }
      const bool left = g.in[o].nav == NAV_LEFT, right = g.in[o].nav == NAV_RIGHT || pad_pressed(g, o, PAD_A);
      // In rete antialias e volumi sono di chi comanda la pausa: si cambiano solo sulla sua console.
      const bool here = !g.net || o == net_local(g);
      // Mira: con sinistra/destra (o A) si sceglie quanto e' pronta la levetta destra di chi comanda.
      if (g.pause_sel == PS_AIM) {
        int lvl = aim_level_of(g, o);
        if (left) lvl = (lvl + NAIMLEVELS - 1) % NAIMLEVELS;
        if (right) lvl = (lvl + 1) % NAIMLEVELS;
        if (lvl != g.aim_level[o]) {
          g.aim_level[o] = (uint8_t)lvl;
          if (!g.net && o < SAVE_PLAYERS) save_data()->aim[o] = (uint8_t)lvl;
          else if (g.net && o == net_local(g)) save_data()->aim[0] = (uint8_t)lvl;   // in rete: la mira di questa console
          ev_sound_ui(g, SND_UI_MOVE);
          settings_changed(g);
          plat_log("giocatore %d: mira %s", o + 1, AIM_TUNE[lvl].name);
        }
      }
      // Antialias (bordi lisci, per tutti): sinistra/destra o A lo accendono e lo spengono.
      // Antialias (per tutti, dalla 0.5.4): NO, 4X, 8X fin dove arriva la GPU; sinistra/destra o A.
      if (g.pause_sel == PS_AA && g.aa_max > 0 && (left || right) && here) {
        int vals[3], nv = 0;
        vals[nv++] = 0;
        vals[nv++] = g.aa_max >= 4 ? 4 : g.aa_max;
        if (g.aa_max >= 8) vals[nv++] = 8;
        const int now = game_aa_wanted(g);
        int k = 0;
        while (k < nv - 1 && vals[k] != now) k++;
        k = left ? (k + nv - 1) % nv : (k + 1) % nv;
        g.aa_level = vals[k];
        save_data()->antialias = (uint8_t)vals[k];
        ev_sound_ui(g, SND_UI_MOVE);
        settings_changed(g);
        if (vals[k] > 0) plat_log("antialias %dx (giocatore %d)", vals[k], o + 1);
        else plat_log("antialias spento (giocatore %d)", o + 1);
      }
      // Volumi (per tutti, da 0 a 10): sinistra/destra; A fa il giro.
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
          g.pause_help = false;
          go_screen(g, SC_MAPS);
          return;
        }
      }
    }
  }
  // Finestre per entrare a partita iniziata: gli altri continuano a giocare.
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

// Finestre di chi entra a partita iniziata (anche in pausa: allora sopra la pausa).
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

// Riquadro dei comandi (voce COMANDI della pausa).
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
  const float c0 = x + 24.0f, c1 = x + 24.0f + 216.0f;   // la colonna di destra e' quella lunga
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
    // Il nome e (tasti da tenere) TIENI fino al bordo del riquadro.
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

}  // namespace

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
  ui::overlay(h, 0.72f);
  const float ta = g.pause_help ? 0.3f : 1.0f;
  ui::title(h, 320, 22, HF_T44, tr("PAUSA"), 8.0f, true, ta);
  if (!g.pause_help) {
    // Mentre qualcuno entra le sue finestre stanno in basso (da 348): le voci si stringono e salgono
    // al posto di "COMANDA", cosi' si vedono tutte.
    const float RH = joining ? 26.0f : 32.0f, SEP = joining ? 7.0f : 11.0f;
    if (!joining) {
      const char* boss = tr("COMANDA");
      const float gw = 24.0f, tw = hud_text_w(HF_S13, boss, 2.0f), x0 = 320.0f - (gw + 8.0f + tw) * 0.5f;
      ui::gp(h, x0, 85.0f, g.pause_owner);
      ui::text(h, x0 + gw + 8.0f, 85.0f, HF_S13, O(ui::FTX), boss, 2.0f);
    }
    // Le voci.
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
      static const char* const LABEL[PAUSE_ITEMS] = { N_("RIPRENDI"), N_("COMANDI"), N_("MIRA"), N_("ANTIALIAS"), N_("MUSICA"),
                                                      N_("EFFETTI"), N_("LASCIA LA PARTITA"), N_("TORNA AL MENU") };
      // Il nome della voce fino al valore a destra (mira: le freccette e il nome; volumi: le tacche).
      static const float VALUE_W[PAUSE_ITEMS] = { 0.0f, 20.0f, 150.0f, 80.0f, 100.0f, 100.0f, 0.0f, 0.0f };
      char lab[256];
      ui::text(h, IX + 14.0f, cy, HF_S16, lc, ui::fit(lab, sizeof(lab), tr(LABEL[k]), HF_S16, 1.0f, IW - 26.0f - VALUE_W[k]), 1.0f);
      const float rx = IX + IW - 12.0f;
      switch (k) {
        case PS_CONTROLS: ui::chev(h, rx - 8.0f, cy - 6.0f, true, sel ? ui::ON : ui::L1); break;
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
        case PS_AA:
          if (g.aa_max <= 0) {
            ui::text(h, rx, cy, HF_S13, O(sel ? ui::ON : ui::DIS), tr("ASSENTE"), 0.8f, 1);
          } else {
            char v[16];
            const int now = game_aa_wanted(g);
            if (now > 0) snprintf(v, sizeof(v), "%dX", now);
            else snprintf(v, sizeof(v), "%s", tr("NO"));
            if (sel) {
              const float vw = hud_text_w(HF_S13, v, 0.8f);
              ui::chev(h, rx - 8.0f, cy - 6.0f, true, ui::ON);
              ui::text(h, rx - 16.0f, cy, HF_S13, vc, v, 0.8f, 1);
              ui::chev(h, rx - 24.0f - vw - 8.0f, cy - 6.0f, false, ui::ON);
            } else {
              ui::text(h, rx, cy, HF_S13, vc, v, 0.8f, 1);
            }
          }
          break;
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
  // Chi non gioca puo' entrare (anche con il riquadro dei comandi aperto, che finisce sopra).
  if (joining) {
    join_panels(g, h);
  } else if (waiting) {
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
