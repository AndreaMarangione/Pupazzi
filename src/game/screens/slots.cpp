// Saves screen (0.5.3): load, manage (copy, rename, delete), choose which game to overwrite or
// which save to play in a LAN game, or (0.5.6) which one to replace with the LAN game joined. Each slot shows the farthest planet reached with its round, the
// stars of that round, the open maps and the play time.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

void slots_refresh(Game& g) {
  save_write();                    // the active game as it is now (the play time)
  static SaveData d;
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    SlotView& v = g.session.slot_view[i];
    memset(&v, 0, sizeof(v));
    v.state = save_slot_peek(i, &d);
    int w = 0;
    for (int k = 1; k < NWORLDS; ++k)
      if (world_maps(k) > 0 && (d.maps_open[k] > 0 || d.giro[k] > 1)) w = k;
    v.world = w;
    v.giro = d.giro[w];
    v.total = world_maps(w);
    v.smax = 3 * v.total;
    for (int m = 0; m < maps_count(); ++m)
      if (maps_world(m) == w) v.stars += star_count(save_stars_in(&d, maps_id(m)));
    v.open = d.maps_open[w] < v.total ? d.maps_open[w] : v.total;
    if (w == 0 && v.open < 1) v.open = 1;
    v.seconds = d.seconds;
    memcpy(v.name, d.name, sizeof(v.name));
  }
}

// Name to show: the one given or "PARTITA N".
const char* slot_name(const Game& g, int i, char* buf, int n) {
  if (i >= 0 && i < SAVE_SLOTS && g.session.slot_view[i].state == SAVE_LETTO && g.session.slot_view[i].name[0]) return g.session.slot_view[i].name;
  snprintf(buf, (size_t)n, tr("PARTITA %d"), i + 1);
  return buf;
}

int first_free_slot(Game& g) {
  for (int i = 0; i < SAVE_SLOTS; ++i)
    if (g.session.slot_view[i].state == SAVE_NUOVO) return i;
  return -1;
}

int used_slots(const Game& g) {
  int n = 0;
  for (int i = 0; i < SAVE_SLOTS; ++i) n += g.session.slot_view[i].state != SAVE_NUOVO;
  return n;
}

void enter_slots(Game& g, int mode) {
  g.slots_mode = mode;
  go_screen(g, SC_SLOTS);
  slots_refresh(g);
  g.slots_pop = SP_NONE;
  g.slots_sel = 0;
  if (mode == SL_NET) {                             // LAN game: the active game, or the first used one, or the first slot
    if (save_slot() >= 0) g.slots_sel = save_slot();
    else
      for (int i = SAVE_SLOTS - 1; i >= 0; --i)
        if (g.session.slot_view[i].state != SAVE_NUOVO) g.slots_sel = i;
  } else if (mode != SL_OVERWRITE && mode != SL_JOIN) {   // the cursor on the first used slot
    while (g.slots_sel < SAVE_SLOTS - 1 && g.session.slot_view[g.slots_sel].state == SAVE_NUOVO) g.slots_sel++;
  }
}

void slots_update(Game& g) {
  const int nav = any_nav(g);
  int who;
  const bool ok = any_confirm(g, &who), no = !ok && any_back(g);
  const int sel = g.slots_sel;
  const SlotView& v = g.session.slot_view[sel];
  char buf[256];
  switch (g.slots_pop) {
    case SP_NONE:
      if (nav == NAV_UP) g.slots_sel = (sel + SAVE_SLOTS - 1) % SAVE_SLOTS;
      if (nav == NAV_DOWN) g.slots_sel = (sel + 1) % SAVE_SLOTS;
      if (ok) {
        if (g.slots_mode == SL_OVERWRITE || g.slots_mode == SL_JOIN) {
          ev_sound_ui(g, SND_UI_OK);
          g.slots_pop = SP_OVERWRITE;
        } else if (g.slots_mode == SL_NET) {
          // LAN game: with an existing save the game is created at once; a free slot first gets the name.
          ev_sound_ui(g, SND_UI_OK);
          if (v.state == SAVE_NUOVO) {
            enter_name(g, NM_NET, sel, who, false);
          } else {
            save_slot_load(sel);
            net_req(g, NR_HOST, sel);
            go_screen(g, SC_LOBBY);
            g.session.lobby_join = false;
          }
        } else if (v.state == SAVE_NUOVO) {
          ev_sound_ui(g, SND_UI_BACK);
          info(g, tr("posto libero"));
        } else if (g.slots_mode == SL_LOAD) {
          ev_sound_ui(g, SND_UI_OK);
          save_slot_load(sel);
          g.menu_owner = who;
          go_screen(g, SC_MODE);
        } else {
          ev_sound_ui(g, SND_UI_OK);
          g.slots_pop = SP_ACTIONS;
          g.slots_pop_sel = 0;
        }
      } else if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        if (g.slots_mode == SL_NET) go_screen(g, SC_NET);
        else if (g.slots_mode == SL_JOIN) go_screen(g, SC_LOBBY);      // back to the games found
        else back_to_title(g, g.slots_mode == SL_LOAD ? TI_LOAD : (g.slots_mode == SL_MANAGE ? TI_MANAGE : TI_NEW));
      }
      break;
    case SP_ACTIONS:
      if (nav == NAV_UP) g.slots_pop_sel = (g.slots_pop_sel + SA_COUNT - 1) % SA_COUNT;
      if (nav == NAV_DOWN) g.slots_pop_sel = (g.slots_pop_sel + 1) % SA_COUNT;
      if (ok) {
        if (g.slots_pop_sel == SA_DELETE) {
          ev_sound_ui(g, SND_UI_OK);
          g.slots_pop = SP_DELETE;
          break;
        }
        if (g.slots_pop_sel == SA_RENAME) {
          g.slots_pop = SP_NONE;
          if (v.state != SAVE_LETTO) {
            ev_sound_ui(g, SND_UI_BACK);
            info(g, tr("salvataggio illeggibile: non si può rinominare"));
          } else {
            ev_sound_ui(g, SND_UI_OK);
            enter_name(g, NM_RENAME, sel, who, false);
          }
          break;
        }
        const int free = first_free_slot(g);
        if (free < 0) {
          ev_sound_ui(g, SND_UI_BACK);
          info(g, tr("nessun posto libero: elimina prima un salvataggio"));
        } else if (save_slot_copy(sel, free)) {
          ev_sound_ui(g, SND_UI_OK);
          snprintf(buf, sizeof(buf), tr("copiato nel posto %d"), free + 1);
          info(g, buf);
          slots_refresh(g);
        } else {
          ev_sound_ui(g, SND_UI_BACK);
          info(g, tr("copia non riuscita"));
        }
        g.slots_pop = SP_NONE;
      } else if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        g.slots_pop = SP_NONE;
      }
      break;
    default:   // SP_DELETE, SP_OVERWRITE: A yes, B no
      if (ok) {
        if (g.slots_pop == SP_OVERWRITE && g.slots_mode == SL_JOIN) {
          // The LAN game will take this slot when it starts (until then nothing is touched).
          g.slots_pop = SP_NONE;
          go_screen(g, SC_LOBBY);
          net_join(g, sel);
          break;
        }
        if (g.slots_pop == SP_OVERWRITE) {
          ev_sound_ui(g, SND_UI_OK);
          enter_name(g, NM_NEW, sel, who, true);
          break;
        }
        const bool done = save_slot_delete(sel);
        ev_sound_ui(g, done ? SND_UI_OK : SND_UI_BACK);
        snprintf(buf, sizeof(buf), done ? tr("salvataggio %d eliminato") : tr("non riesco a eliminare il salvataggio %d"), sel + 1);
        info(g, buf);
        slots_refresh(g);
        g.slots_pop = SP_NONE;
        if (used_slots(g) == 0) back_to_title(g, TI_NEW);    // nothing left to manage
      } else if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        g.slots_pop = SP_NONE;
      }
      break;
  }
}

// Play duration: "12 min", "1 h 05 min".
void play_time(char* buf, int n, uint32_t s) {
  const uint32_t m = s / 60u;
  if (m < 60u) snprintf(buf, (size_t)n, tr("%u min"), m);
  else snprintf(buf, (size_t)n, tr("%u h %02u min"), m / 60u, m % 60u);
}

// A slot: number, name; below, planet and round, stars, open maps, time; empty or unreadable.
void slot_row(const Game& g, Hud* h, float x, float y, float w, float hh, int i, bool sel) {
  const SlotView& v = g.session.slot_view[i];
  char buf[256], t[64];
  if (sel) ui::bar_sel(h, x, y, w, hh);
  const float cy = y + hh * 0.5f;
  const uint32_t mc = O(sel ? ui::ON : ui::TX), sub = O(sel ? ui::ON : ui::MU);
  // The number in a circle.
  hud_ring(h, x + 30.0f, cy, 16.0f, 16.0f, 2.0f, O(sel ? ui::ON : ui::L1));
  snprintf(buf, sizeof(buf), "%d", i + 1);
  ui::text(h, x + 30.0f, cy, HF_B16, O(sel ? ui::ON : ui::L1), buf, 0.0f, 0);
  const float tx = x + 62.0f;
  if (v.state == SAVE_NUOVO) {
    // In a LAN game a free slot is a new game (first the name).
    if (g.slots_mode == SL_NET) ui::text(h, tx, cy, HF_B16, O(sel ? ui::ON : ui::L1), tr("NUOVA PARTITA"), 1.5f);
    else ui::text(h, tx, cy, HF_B16, O(sel ? ui::ON : ui::DIS), tr("POSTO LIBERO"), 1.5f);
    return;
  }
  if (v.state == SAVE_ILLEGGIBILE) {
    ui::text(h, tx, cy - 9.0f, HF_B16, O(sel ? ui::ON : ui::L2), tr("ILLEGGIBILE"), 1.5f);
    ui::text(h, tx, cy + 11.0f, HF_S13, sub, tr("si riparte dall'inizio"), 0.5f);
    return;
  }
  char wn[96], nm[64];
  ui::text(h, tx, cy - 9.0f, HF_B16, mc, slot_name(g, i, nm, sizeof(nm)), 1.5f);
  hud_upper(wn, tr(WORLD_NAMES[v.world]), sizeof(wn));
  snprintf(t, sizeof(t), tr("%s · GIRO %d"), wn, v.giro);
  float lx = tx;
  ui::text(h, lx, cy + 11.0f, HF_S13, O(sel ? ui::ON : ui::L1), ui::fit(buf, sizeof(buf), t, HF_S13, 0.5f, 120.0f), 0.5f);
  lx += hud_text_w(HF_S13, buf, 0.5f) + 12.0f;
  ui::star(h, HI_STAR15, lx, cy + 11.0f - 7.5f, true, O(ui::DIM2));
  lx += 21.0f;
  snprintf(buf, sizeof(buf), "%d/%d", v.stars, v.smax);
  ui::text(h, lx, cy + 11.0f, HF_S13, mc, buf, 0.5f);
  lx += hud_text_w(HF_S13, buf, 0.5f) + 14.0f;
  snprintf(buf, sizeof(buf), tr("%d/%d mappe"), v.open, v.total);
  ui::text(h, lx, cy + 11.0f, HF_S13, sub, buf, 0.5f);
  lx += hud_text_w(HF_S13, buf, 0.5f) + 14.0f;
  play_time(t, sizeof(t), v.seconds);
  ui::text(h, lx, cy + 11.0f, HF_S13, sub, t, 0.5f);
  // The planet on the right.
  if (v.world == 0) ui::earth(h, x + w - 40.0f, cy - 13.0f);
  else ui::planet(h, x + w - 34.0f, cy - 7.5f, v.world);
}

// Window over the slots: title and one or two lines; with the items (actions) or "A si' / B no".
void slots_popup(const Game& g, Hud* h) {
  char buf[256];
  ui::overlay(h, 0.5f);
  const float W = 380.0f, X = 130.0f;
  const int i = g.slots_sel;
  if (g.slots_pop == SP_ACTIONS) {
    const float Y = 122.0f, H = 224.0f;
    ui::panel(h, X, Y, W, H, 16.0f, ui::SOLID, true);
    char nm[64], t[256];
    snprintf(buf, sizeof(buf), tr("SALVATAGGIO %d"), i + 1);
    ui::text(h, X + 22.0f, Y + 30.0f, HF_B22, O(ui::TX), buf, 3.0f);
    const float nw = hud_text_w(HF_B22, buf, 3.0f);
    ui::text(h, X + 22.0f + nw + 14.0f, Y + 31.0f, HF_S14, O(ui::L1),
             ui::fit(t, sizeof(t), slot_name(g, i, nm, sizeof(nm)), HF_S14, 1.0f, W - 58.0f - nw), 1.0f);
    ui::sep(h, X + 22.0f, Y + 56.0f, W - 44.0f);
    static const char* const ACT[SA_COUNT] = { N_("DUPLICA"), N_("RINOMINA"), N_("ELIMINA") };
    static const char* const HINT[SA_COUNT] = { N_("una copia nel primo posto libero"), N_("gli dai un nome nuovo"),
                                                N_("lo cancella per sempre") };
    for (int k = 0; k < SA_COUNT; ++k) {
      const float y = Y + 66.0f + 48.0f * (float)k;           // tall rows, also for Chinese, Japanese and Hindi
      const bool sel = k == g.slots_pop_sel;
      if (sel) ui::bar_sel(h, X + 12.0f, y, W - 24.0f, 44.0f);
      ui::text(h, X + 26.0f, y + 14.0f, HF_S16, O(sel ? ui::ON : (k == SA_DELETE ? ui::L2 : ui::TX)), tr(ACT[k]), 1.0f);
      ui::text(h, X + 26.0f, y + 32.0f, HF_S12, O(sel ? ui::ON : ui::MU), ui::fit(t, sizeof(t), tr(HINT[k]), HF_S12, 0.5f, W - 52.0f), 0.5f);
    }
    return;
  }
  const float Y = 162.0f, H = 146.0f;
  ui::panel(h, X, Y, W, H, 16.0f, ui::SOLID, true);
  char q[256], fitted[256];
  snprintf(q, sizeof(q), g.slots_pop == SP_DELETE ? tr("ELIMINARE IL SALVATAGGIO %d?") : tr("SOVRASCRIVERE IL SALVATAGGIO %d?"), i + 1);
  ui::fit(fitted, sizeof(fitted), q, HF_B16, 1.5f, W - 44.0f);
  ui::text(h, 320.0f, Y + 32.0f, HF_B16, O(g.slots_pop == SP_DELETE ? ui::L2 : ui::TX), fitted, 1.5f, 0);
  const SlotView& v = g.session.slot_view[i];
  if (v.state == SAVE_LETTO) {
    char wn[96], t[64], nm[64];
    ui::text(h, 320.0f, Y + 58.0f, HF_S14, O(ui::L1), ui::fit(fitted, sizeof(fitted), slot_name(g, i, nm, sizeof(nm)), HF_S14, 1.0f, W - 44.0f),
             1.0f, 0);
    hud_upper(wn, tr(WORLD_NAMES[v.world]), sizeof(wn));
    play_time(t, sizeof(t), v.seconds);
    snprintf(buf, sizeof(buf), tr("%s, giro %d: %d stelle, %s di gioco"), wn, v.giro, v.stars, t);
    ui::fit(fitted, sizeof(fitted), buf, HF_S13, 0.5f, W - 44.0f);
    ui::text(h, 320.0f, Y + 80.0f, HF_S13, O(ui::MU), fitted, 0.5f, 0);
  }
  yes_no(h, Y + 116.0f);
}

void slots_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  static const char* const TITLE[5] = { N_("CARICA SALVATAGGIO"), N_("GESTISCI SALVATAGGI"), N_("DOVE SALVARE?"), N_("CON QUALE SALVATAGGIO?"),
                                        N_("DOVE SALVARE?") };
  char t[256];
  ui::fit(t, sizeof(t), tr(TITLE[g.slots_mode]), HF_T32, 3.0f, 560.0f, "...");
  ui::title(h, 320, 26, HF_T32, t, 3.0f);
  float PY = 86.0f;
  if (g.slots_mode == SL_OVERWRITE || g.slots_mode == SL_NET || g.slots_mode == SL_JOIN) {
    ui::fit(t, sizeof(t),
            g.slots_mode == SL_NET    ? tr("la partita in rete si salva qui, su questa console")
            : g.slots_mode == SL_JOIN ? tr("i posti sono pieni: la partita in rete prende il posto di quella scelta")
                                      : tr("i posti sono pieni: la partita nuova prende il posto di quella scelta"),
            HF_S14, 0.5f, 576.0f);
    ui::text(h, 321, 79, HF_S14, A(ui::SCRIM, 0.9f), t, 0.5f, 0);
    ui::text(h, 320, 78, HF_S14, O(ui::FTX), t, 0.5f, 0);
    PY = 96.0f;
  }
  const float PX = 84.0f, PW = 472.0f, RH = 62.0f, GAP = 6.0f;
  ui::panel(h, PX, PY, PW, 20.0f + RH * (float)SAVE_SLOTS + GAP * (float)(SAVE_SLOTS - 1));
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    const float y = PY + 10.0f + (RH + GAP) * (float)i;
    if (i > 0) ui::sep(h, PX + 24.0f, y - GAP * 0.5f, PW - 48.0f);
    slot_row(g, h, PX + 10.0f, y, PW - 20.0f, RH, i, i == g.slots_sel);
  }
  if (g.slots_pop != SP_NONE) {
    slots_popup(g, h);
    return;
  }
  if (g.session.info_t > 0) {
    toast(h, g.session.info, ui::L1);
    ui::version(h, PUPAZZI_VERSION);
    return;
  }
  static const char* const ACT[5] = { N_("carica"), N_("scegli"), N_("sovrascrivi"), N_("scegli"), N_("sovrascrivi") };
  const char* it[4] = { "A", tr(ACT[g.slots_mode]), "B", tr("indietro") };
  ui::footer(h, it, 4);
  ui::version(h, PUPAZZI_VERSION);
}

}  // namespace screens
