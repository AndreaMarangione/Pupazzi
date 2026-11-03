// LAN game screens (0.5.4): create or join, then the waiting room.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// GIOCO IN RETE: create a game (then the save to play it with) or join (the search starts at once).
void net_update(Game& g) {
  const int nav = any_nav(g);
  if (nav != NAV_NONE) g.menu_sel ^= 1;
  int who;
  if (any_confirm(g, &who)) {
    ev_sound_ui(g, SND_UI_OK);
    g.menu_owner = who;
    if (g.menu_sel == 0) {
      enter_slots(g, SL_NET);
    } else {
      net_req(g, NR_SEARCH, 0);
      go_screen(g, SC_LOBBY);
      g.lobby_sel = 0;
      g.lobby_pop = false;
      g.session.lobby_join = true;
    }
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    back_to_title(g, TI_NET);
  }
}

// Position of the chosen game (identity and LAN game: two consoles can host the same saved game) in
// the list of those found, -1 if it is no longer there.
int net_game_index(const Game& g) {
  const NetView& v = net_view(g);
  for (int k = 0; k < v.ngames; ++k)
    if (v.games[k].id == g.join_id && v.games[k].session == g.join_session) return k;
  return -1;
}

// Joins the chosen game, saving it on this console too in slot slot (-1 not); the game may have
// gone in the meantime.
void net_join(Game& g, int slot) {
  const int k = net_game_index(g);
  if (k < 0) {
    ev_sound_ui(g, SND_UI_BACK);
    info(g, tr("la partita non risponde"));
    return;
  }
  ev_sound_ui(g, SND_UI_OK);
  net_req(g, NR_JOIN, k + (slot >= 0 ? 256 * (slot + 1) : 0));
}

// Waiting room: the host starts the game (A, with at least one other console) or cancels it (B);
// whoever is searching picks a game (A: since 0.5.6 first "save it here too?") or goes back (B);
// whoever has joined waits (B leaves).
void lobby_update(Game& g) {
  const NetView& v = net_view(g);
  if (v.error_seq != g.session.net_err_seen) {
    g.session.net_err_seen = v.error_seq;
    char buf[256];
    if (v.error != NE_NONE) {
      ev_sound_ui(g, SND_UI_BACK);
      info(g, net_error_text(g, v.error, buf, sizeof(buf)));
    }
  }
  int who;
  const bool ok = any_confirm(g, &who), no = !ok && any_back(g);
  switch (v.mode) {
    case NV_HOST: {
      int n = 0;
      for (int i = 0; i < MAX_PLAYERS; ++i) n += (v.present >> i) & 1;
      if (ok) {
        if (n < 2) {
          ev_sound_ui(g, SND_UI_BACK);
          info(g, tr("aspetta almeno un'altra console"));
        } else {
          ev_sound_ui(g, SND_UI_OK);
          net_req(g, NR_START, 0);
        }
      } else if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        net_req(g, NR_LEAVE, 0);
        go_screen(g, SC_NET);
      }
      break;
    }
    case NV_SEARCH: {
      if (g.lobby_pop) {
        // Save it here too? A yes (with all the slots full: first which one to replace), B no.
        if (ok || no) {
          g.lobby_pop = false;
          if (ok && g.join_slot < 0) {
            ev_sound_ui(g, SND_UI_OK);
            enter_slots(g, SL_JOIN);
          } else {
            net_join(g, ok ? g.join_slot : -1);
          }
        }
        break;
      }
      const int nav = any_nav(g);
      if (v.ngames > 0) {
        if (nav == NAV_UP) g.lobby_sel = (g.lobby_sel + v.ngames - 1) % v.ngames;
        if (nav == NAV_DOWN) g.lobby_sel = (g.lobby_sel + 1) % v.ngames;
      }
      if (g.lobby_sel >= v.ngames) g.lobby_sel = v.ngames > 0 ? v.ngames - 1 : 0;
      if (ok && v.ngames > 0) {
        ev_sound_ui(g, SND_UI_OK);
        const NetGame& gm = v.games[g.lobby_sel];
        if (gm.state == NG_OPEN && gm.id && save_path()[0]) {     // (with nowhere to save, no question)
          // 0.5.6: first the question; the slot: the one that already has this game, or the first free one.
          g.lobby_pop = true;
          g.join_id = gm.id;
          g.join_session = gm.session;
          slots_refresh(g);
          g.join_slot = save_slot_with_id(gm.id);
          g.join_linked = g.join_slot >= 0;
          if (!g.join_linked) g.join_slot = first_free_slot(g);
        } else {
          net_req(g, NR_JOIN, g.lobby_sel);    // it cannot be joined: the reason comes back as a notice
        }
      } else if (ok) {
        ev_sound_ui(g, SND_UI_BACK);
      } else if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        net_req(g, NR_LEAVE, 0);
        go_screen(g, SC_NET);
        g.menu_sel = 1;
      }
      break;
    }
    case NV_JOINING:
    case NV_JOINED:
      if (no) {
        ev_sound_ui(g, SND_UI_BACK);
        net_req(g, NR_LEAVE, 0);
      }
      break;
    default:      // the network did not open (or it closed): back to GIOCO IN RETE
      g.lobby_pop = false;
      if (g.screen_t > 3) {
        go_screen(g, SC_NET);
        g.menu_sel = g.session.lobby_join ? 1 : 0;
      }
      break;
  }
}

// Address of this console on the network (or the notice that there is no network), centered on cx;
// at the bottom, above the footer (on the dark scrim: over the heroes it would not be readable).
const float NET_IP_Y = 414.0f, NET_HINT_Y = 390.0f;

void net_ip_line(const Game& g, Hud* h, float cx, float cy) {
  const NetView& v = net_view(g);
  char buf[160];
  if (v.ip[0]) snprintf(buf, sizeof(buf), tr("questa console: %s"), v.ip);
  else snprintf(buf, sizeof(buf), "%s", tr("nessuna rete: collega la console alla Wi-Fi"));
  char t[192];
  ui::fit(t, sizeof(t), buf, HF_S13, 0.5f, 540.0f);
  const float w = 18.0f + 8.0f + hud_text_w(HF_S13, t, 0.5f);
  const float x = cx - w * 0.5f;
  hud_icon(h, HI_WIFI, x + 1.0f, cy - 6.0f, A(ui::SCRIM, 0.9f));
  hud_icon(h, HI_WIFI, x, cy - 7.0f, O(v.ip[0] ? ui::L1 : ui::L2));
  ui::text(h, x + 27.0f, cy + 1.0f, HF_S13, A(ui::SCRIM, 0.9f), t, 0.5f);
  ui::text(h, x + 26.0f, cy, HF_S13, O(v.ip[0] ? ui::FTX : ui::L2), t, 0.5f);
}

// Subtitle above a panel (text with the shadow, centered).
void subtitle(Hud* h, float cy, const char* s) {
  char t[256];
  ui::fit(t, sizeof(t), s, HF_S15, 1.0f, 576.0f);
  ui::text(h, 321, cy + 1.0f, HF_S15, A(ui::SCRIM, 0.9f), t, 1.0f, 0);
  ui::text(h, 320, cy, HF_S15, O(ui::FTX), t, 1.0f, 0);
}

// Name of a LAN game: the one given or "PARTITA N".
const char* net_game_name(const NetGame& gm, char* buf, int n) {
  if (gm.name[0]) return gm.name;
  snprintf(buf, (size_t)n, tr("PARTITA %d"), gm.slot + 1);
  return buf;
}

// "TERRA · GIRO 1" of a LAN game.
void net_game_where(const NetGame& gm, char* buf, int n) {
  char wn[96];
  const int w = gm.world >= 0 && gm.world < NWORLDS ? gm.world : 0;
  hud_upper(wn, tr(WORLD_NAMES[w]), sizeof(wn));
  snprintf(buf, (size_t)n, tr("%s · GIRO %d"), wn, gm.giro);
}

void net_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 26, HF_T32, tr("GIOCO IN RETE"), 3.0f);
  subtitle(h, 80.0f, tr("ognuno sul suo schermo, nella stessa Wi-Fi"));
  const float PX = 110.0f, PW = 420.0f, PY = 104.0f, RH = 74.0f, GAP = 6.0f;
  ui::panel(h, PX, PY, PW, 20.0f + RH * 2.0f + GAP);
  static const char* const LABEL[2] = { N_("CREA UNA PARTITA"), N_("UNISCITI A UNA PARTITA") };
  static const char* const HINT[2] = { N_("le altre console la trovano da sole"), N_("cerca le partite delle altre console") };
  for (int k = 0; k < 2; ++k) {
    const float y = PY + 10.0f + (RH + GAP) * (float)k, cy = y + RH * 0.5f;
    const bool sel = k == g.menu_sel;
    if (k > 0) ui::sep(h, PX + 24.0f, y - GAP * 0.5f, PW - 48.0f);
    if (sel) ui::bar_sel(h, PX + 10.0f, y, PW - 20.0f, RH);
    // On the left, who plays: one who creates, two who join.
    if (k == 0) {
      ui::avatar(h, PX + 30.0f, cy - 16.0f, 0, false);
    } else {
      ui::avatar(h, PX + 22.0f, cy - 16.0f, 1, false);
      ui::avatar(h, PX + 40.0f, cy - 16.0f, 2, false);
    }
    char t[256];
    ui::text(h, PX + 92.0f, cy - 10.0f, HF_B16, O(sel ? ui::ON : ui::TX), ui::fit(t, sizeof(t), tr(LABEL[k]), HF_B16, 1.5f, PW - 116.0f), 1.5f);
    ui::text(h, PX + 92.0f, cy + 13.0f, HF_S13, O(sel ? ui::ON : ui::MU), ui::fit(t, sizeof(t), tr(HINT[k]), HF_S13, 0.5f, PW - 116.0f), 0.5f);
  }
  net_ip_line(g, h, 320.0f, NET_IP_Y);
  footer(g, h, tr("conferma"), tr("indietro"));
}

// Waiting room: the 4 places, who is there and which one is this console.
void lobby_places(Hud* h, const NetView& v, float PX, float PY, float PW) {
  const float RH = 46.0f;
  ui::panel(h, PX, PY, PW, 16.0f + RH * (float)MAX_PLAYERS);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const float y = PY + 8.0f + RH * (float)i, cy = y + RH * 0.5f;
    if (i > 0) ui::sep(h, PX + 20.0f, y, PW - 40.0f);
    const bool here = ((v.present >> i) & 1u) != 0u;
    float x = PX + 20.0f;
    ui::gp(h, x, cy, i, HF_B13);
    x += 30.0f + 12.0f;                        // G1...G4 are not equally wide: the names aligned in a column
    char buf[96];
    snprintf(buf, sizeof(buf), tr("GIOCATORE %d"), i + 1);
    ui::text(h, x, cy, HF_B16, O(here ? ui::TX : ui::DIS), buf, 1.5f);
    x += hud_text_w(HF_B16, buf, 1.5f) + 12.0f;
    if (here && i == v.local) ui::tag(h, x, cy, tr("TU"), ui::A2, HF_B11, 1.5f, 6.0f, 19.0f);
    const char* st = !here ? tr("posto libero") : (i == 0 ? tr("ha creato la partita") : tr("collegato"));
    ui::text(h, PX + PW - 20.0f, cy, HF_S13, O(here ? ui::L1 : ui::DIS), st, 0.5f, 1);
  }
}

// "Salvare la partita anche su questa console?" (0.5.6), over the games found: the game, where it
// would go (the slot that already has it, a free one, or the choice when they are all full), A yes
// / B no.
void join_question(const Game& g, Hud* h) {
  ui::overlay(h, 0.5f);
  const float W = 440.0f, X = 320.0f - W * 0.5f, Y = 140.0f, H = 176.0f;
  ui::panel(h, X, Y, W, H, 16.0f, ui::SOLID, true);
  char L[2][ui::WRAP_LEN];
  const int n = ui::wrap(tr("SALVARE LA PARTITA ANCHE SU QUESTA CONSOLE?"), W - 44.0f, HF_B16, 1.0f, L, 2);
  for (int k = 0; k < n; ++k) ui::text(h, 320.0f, Y + 30.0f + 22.0f * (float)k - (n > 1 ? 0.0f : -11.0f), HF_B16, O(ui::TX), L[k], 1.0f, 0);
  char buf[256], t[256], nm[64], where[128];
  const int k = net_game_index(g);
  if (k >= 0) {
    const NetGame& gm = net_view(g).games[k];
    net_game_where(gm, where, sizeof(where));
    snprintf(buf, sizeof(buf), "%s · %s", net_game_name(gm, nm, sizeof(nm)), where);
    ui::text(h, 320.0f, Y + 84.0f, HF_S14, O(ui::L1), ui::fit(t, sizeof(t), buf, HF_S14, 1.0f, W - 44.0f), 1.0f, 0);
  }
  if (g.join_linked) snprintf(buf, sizeof(buf), tr("c'è già nel salvataggio %d: si aggiorna"), g.join_slot + 1);
  else if (g.join_slot >= 0) snprintf(buf, sizeof(buf), tr("va nel salvataggio %d, che è libero"), g.join_slot + 1);
  else snprintf(buf, sizeof(buf), "%s", tr("i posti sono pieni: poi scegli quale sostituire"));
  ui::text(h, 320.0f, Y + 108.0f, HF_S13, O(ui::MU), ui::fit(t, sizeof(t), buf, HF_S13, 0.5f, W - 44.0f), 0.5f, 0);
  yes_no(h, Y + 146.0f);
}

void lobby_hud(const Game& g, Hud* h) {
  const NetView& v = net_view(g);
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  char buf[256], nm[64], where[128];
  if (v.mode == NV_SEARCH || (v.mode == NV_OFF && g.session.lobby_join)) {
    ui::title(h, 320, 26, HF_T32, tr("PARTITE IN RETE"), 3.0f);
    // "cerco nella stessa Wi-Fi..." with the growing dots.
    const int dots = (int)((g.ui_tick / 20u) % 4u);
    snprintf(buf, sizeof(buf), "%s%s", tr("cerco nella stessa Wi-Fi"), dots == 0 ? "" : (dots == 1 ? "." : (dots == 2 ? ".." : "...")));
    subtitle(h, 80.0f, buf);
    const float PX = 100.0f, PW = 440.0f, PY = 104.0f;
    if (v.ngames == 0) {
      ui::panel(h, PX, PY, PW, 104.0f);
      ui::text(h, 320.0f, PY + 34.0f, HF_B16, O(ui::MU), tr("NESSUNA PARTITA PER ORA"), 1.5f, 0);
      char L[2][ui::WRAP_LEN];
      const int nl = ui::wrap(tr("su un'altra console: GIOCO IN RETE, poi CREA UNA PARTITA"), PW - 48.0f, HF_S13, 0.5f, L, 2);
      for (int k = 0; k < nl; ++k) ui::text(h, 320.0f, PY + 64.0f + 18.0f * (float)k, HF_S13, O(ui::FTX), L[k], 0.5f, 0);
      net_ip_line(g, h, 320.0f, NET_IP_Y);
    } else {
      const float RH = 52.0f;
      ui::panel(h, PX, PY, PW, 16.0f + RH * (float)v.ngames);
      for (int k = 0; k < v.ngames; ++k) {
        const NetGame& gm = v.games[k];
        const float y = PY + 8.0f + RH * (float)k, cy = y + RH * 0.5f;
        const bool sel = k == g.lobby_sel, open = gm.state == NG_OPEN;
        if (k > 0 && !sel && k - 1 != g.lobby_sel) ui::sep(h, PX + 20.0f, y, PW - 40.0f);
        if (sel) ui::bar_sel(h, PX + 10.0f, y + 2.0f, PW - 20.0f, RH - 4.0f);
        const uint32_t tc = O(sel ? ui::ON : (open ? ui::TX : ui::DIS)), sc = O(sel ? ui::ON : ui::MU);
        ui::text(h, PX + 26.0f, cy - 9.0f, HF_B16, tc, net_game_name(gm, nm, sizeof(nm)), 1.5f);
        net_game_where(gm, where, sizeof(where));
        snprintf(buf, sizeof(buf), "%s · %d/%d", where, gm.players, MAX_PLAYERS);
        ui::text(h, PX + 26.0f, cy + 11.0f, HF_S13, sc, buf, 0.5f);
        const char* tag = 0;
        char ver[32];
        switch (gm.state) {
          case NG_FULL: tag = tr("PIENA"); break;
          case NG_STARTED: tag = tr("GIÀ COMINCIATA"); break;
          case NG_MAPS: tag = tr("MAPPE DIVERSE"); break;
          case NG_VERSION: snprintf(ver, sizeof(ver), "v%s", gm.version); tag = ver; break;
          default: break;
        }
        if (tag) ui::tag(h, PX + PW - 26.0f, cy, tag, sel ? ui::ON : ui::A2, HF_B11, 1.5f, 6.0f, 21.0f, 1);
        else if (sel) ui::chev(h, PX + PW - 34.0f, cy - 6.0f, true, ui::ON);
      }
      net_ip_line(g, h, 320.0f, NET_IP_Y);
    }
    if (g.lobby_pop) {
      join_question(g, h);
      return;
    }
    if (g.session.info_t > 0) {
      toast(h, g.session.info, ui::L1);
      ui::version(h, PUPAZZI_VERSION);
    } else if (v.ngames > 0) {
      footer(g, h, tr("unisciti"), tr("indietro"));
    } else {
      const char* it[2] = { "B", tr("indietro") };
      ui::footer(h, it, 2);
      ui::version(h, PUPAZZI_VERSION);
    }
    return;
  }
  // The host, or whoever has joined: the game and the 4 places.
  ui::title(h, 320, 26, HF_T32, tr("PARTITA IN RETE"), 3.0f);
  net_game_where(v.game, where, sizeof(where));
  snprintf(buf, sizeof(buf), "%s · %s", net_game_name(v.game, nm, sizeof(nm)), where);
  subtitle(h, 80.0f, buf);
  const float PX = 120.0f, PW = 400.0f, PY = 102.0f;
  lobby_places(h, v, PX, PY, PW);
  if (v.save >= 0) {
    // 0.5.6: where this console saves the game too.
    snprintf(buf, sizeof(buf), tr("si salva anche qui, nel salvataggio %d"), v.save + 1);
    char t[256];
    ui::fit(t, sizeof(t), buf, HF_S13, 0.5f, 576.0f);
    ui::text(h, 321, 337.0f, HF_S13, A(ui::SCRIM, 0.9f), t, 0.5f, 0);
    ui::text(h, 320, 336.0f, HF_S13, O(ui::FTX), t, 0.5f, 0);
  }
  const char* hint;
  if (v.mode == NV_HOST) hint = tr("sulle altre console: GIOCO IN RETE, poi UNISCITI");
  else if (v.mode == NV_JOINING) hint = tr("mi unisco alla partita...");
  else hint = tr("aspetta: la fa cominciare chi l'ha creata");
  char t[256];
  ui::fit(t, sizeof(t), hint, HF_S14, 0.5f, 576.0f);
  ui::text(h, 321, NET_HINT_Y + 1.0f, HF_S14, A(ui::SCRIM, 0.9f), t, 0.5f, 0);
  ui::text(h, 320, NET_HINT_Y, HF_S14, O(v.mode == NV_HOST ? ui::FTX : ui::L1), t, 0.5f, 0);
  net_ip_line(g, h, 320.0f, NET_IP_Y);
  if (g.session.info_t > 0) {
    toast(h, g.session.info, ui::L1);
    ui::version(h, PUPAZZI_VERSION);
  } else if (v.mode == NV_HOST) {
    footer(g, h, tr("inizia"), tr("annulla"));
  } else {
    const char* it[2] = { "B", v.mode == NV_JOINING ? tr("annulla") : tr("esci") };
    ui::footer(h, it, 2);
    ui::version(h, PUPAZZI_VERSION);
  }
}

}  // namespace screens

using namespace screens;

bool join_layout_ok(char* why, int n) {
  why[0] = 0;
  char L[2][ui::WRAP_LEN], buf[256];
  bool cut = false;
  ui::wrap(tr("SALVARE LA PARTITA ANCHE SU QUESTA CONSOLE?"), 440.0f - 44.0f, HF_B16, 1.0f, L, 2, &cut);
  if (cut) {
    snprintf(why, (size_t)n, "domanda piu' lunga di due righe");
    return false;
  }
  const char* sub[3] = { tr("c'è già nel salvataggio %d: si aggiorna"), tr("va nel salvataggio %d, che è libero"),
                         tr("i posti sono pieni: poi scegli quale sostituire") };
  for (int k = 0; k < 3; ++k) {
    snprintf(buf, sizeof(buf), sub[k], 4);
    if (hud_text_w(HF_S13, buf, 0.5f) > 440.0f - 44.0f) {
      snprintf(why, (size_t)n, "riga %d della domanda troppo lunga", k + 1);
      return false;
    }
  }
  if (hud_text_w(HF_S14, tr("i posti sono pieni: la partita in rete prende il posto di quella scelta"), 0.5f) > 576.0f) {
    snprintf(why, (size_t)n, "sottotitolo dei salvataggi troppo lungo");
    return false;
  }
  snprintf(buf, sizeof(buf), tr("si salva anche qui, nel salvataggio %d"), 4);
  if (hud_text_w(HF_S13, buf, 0.5f) > 576.0f) {
    snprintf(why, (size_t)n, "riga della sala d'attesa troppo lunga");
    return false;
  }
  return true;
}

const char* net_error_text(Game& g, int e, char* buf, int n) {
  const char* s = "";
  switch (e) {
    case NE_VERSION:
      snprintf(buf, (size_t)n, tr("versione diversa: su quella console c'è la %s"), net_view(g).game.version);
      return buf;
    case NE_MAPS: s = tr("mappe diverse sulle due console: non si può giocare insieme"); break;
    case NE_FULL: s = tr("la partita è piena"); break;
    case NE_STARTED: s = tr("la partita è già cominciata"); break;
    case NE_NO_ANSWER: s = tr("la partita non risponde"); break;
    case NE_HOST_LEFT: s = tr("la console che ha creato la partita è uscita"); break;
    case NE_LOST: s = tr("collegamento perso con la console che ha creato la partita"); break;
    case NE_KICKED: s = tr("la console che ha creato la partita non ti sentiva più"); break;
    case NE_DESYNC: s = tr("partita in rete interrotta: le console non facevano più la stessa partita"); break;
    case NE_SLOW: s = tr("partita in rete interrotta: questa console era rimasta troppo indietro"); break;
    case NE_SOCKET: s = tr("non riesco a usare la rete su questa console"); break;
    case NE_ALONE: s = tr("le altre console sono uscite: continui da solo"); break;
    case NE_OVER:
    case NE_CLOSED: s = tr("partita in rete finita"); break;
    default: break;
  }
  snprintf(buf, (size_t)n, "%s", s);
  return buf;
}
