// LAN game screens (0.5.4): create or join, then the waiting room.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// GIOCO IN RETE: crea una partita (poi il salvataggio con cui giocarla) o unisciti (si cerca subito).
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
      g.session.lobby_join = true;
    }
  } else if (any_back(g)) {
    ev_sound_ui(g, SND_UI_BACK);
    back_to_title(g, TI_NET);
  }
}

// Sala d'attesa: chi ha creato la partita la fa cominciare (A, con almeno un'altra console) o la
// annulla (B); chi cerca sceglie una partita (A) o torna indietro (B); chi si e' unito aspetta (B esce).
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
      const int nav = any_nav(g);
      if (v.ngames > 0) {
        if (nav == NAV_UP) g.lobby_sel = (g.lobby_sel + v.ngames - 1) % v.ngames;
        if (nav == NAV_DOWN) g.lobby_sel = (g.lobby_sel + 1) % v.ngames;
      }
      if (g.lobby_sel >= v.ngames) g.lobby_sel = v.ngames > 0 ? v.ngames - 1 : 0;
      if (ok && v.ngames > 0) {
        ev_sound_ui(g, SND_UI_OK);
        net_req(g, NR_JOIN, g.lobby_sel);
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
    default:      // la rete non si e' aperta (o si e' chiusa): di nuovo GIOCO IN RETE
      if (g.screen_t > 3) {
        go_screen(g, SC_NET);
        g.menu_sel = g.session.lobby_join ? 1 : 0;
      }
      break;
  }
}

// Indirizzo di questa console nella rete (o l'avviso che non c'e' una rete), centrato su cx; in
// basso, sopra il piede (sulla velatura scura: sopra gli eroi non si leggerebbe).
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

// Sottotitolo sopra un pannello (testo con l'ombra, centrato).
void subtitle(Hud* h, float cy, const char* s) {
  char t[256];
  ui::fit(t, sizeof(t), s, HF_S15, 1.0f, 576.0f);
  ui::text(h, 321, cy + 1.0f, HF_S15, A(ui::SCRIM, 0.9f), t, 1.0f, 0);
  ui::text(h, 320, cy, HF_S15, O(ui::FTX), t, 1.0f, 0);
}

// Nome di una partita in rete: quello dato o "PARTITA N".
const char* net_game_name(const NetGame& gm, char* buf, int n) {
  if (gm.name[0]) return gm.name;
  snprintf(buf, (size_t)n, tr("PARTITA %d"), gm.slot + 1);
  return buf;
}

// "TERRA · GIRO 1" di una partita in rete.
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
    // A sinistra chi gioca: uno che crea, due che si uniscono.
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

// Sala d'attesa: i 4 posti, chi c'e' e chi e' questa console.
void lobby_places(Hud* h, const NetView& v, float PX, float PY, float PW) {
  const float RH = 46.0f;
  ui::panel(h, PX, PY, PW, 16.0f + RH * (float)MAX_PLAYERS);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const float y = PY + 8.0f + RH * (float)i, cy = y + RH * 0.5f;
    if (i > 0) ui::sep(h, PX + 20.0f, y, PW - 40.0f);
    const bool here = ((v.present >> i) & 1u) != 0u;
    float x = PX + 20.0f;
    ui::gp(h, x, cy, i, HF_B13);
    x += 30.0f + 12.0f;                        // G1...G4 non sono larghi uguali: i nomi incolonnati
    char buf[96];
    snprintf(buf, sizeof(buf), tr("GIOCATORE %d"), i + 1);
    ui::text(h, x, cy, HF_B16, O(here ? ui::TX : ui::DIS), buf, 1.5f);
    x += hud_text_w(HF_B16, buf, 1.5f) + 12.0f;
    if (here && i == v.local) ui::tag(h, x, cy, tr("TU"), ui::A2, HF_B11, 1.5f, 6.0f, 19.0f);
    const char* st = !here ? tr("posto libero") : (i == 0 ? tr("ha creato la partita") : tr("collegato"));
    ui::text(h, PX + PW - 20.0f, cy, HF_S13, O(here ? ui::L1 : ui::DIS), st, 0.5f, 1);
  }
}

void lobby_hud(const Game& g, Hud* h) {
  const NetView& v = net_view(g);
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  char buf[256], nm[64], where[128];
  if (v.mode == NV_SEARCH || (v.mode == NV_OFF && g.session.lobby_join)) {
    ui::title(h, 320, 26, HF_T32, tr("PARTITE IN RETE"), 3.0f);
    // "cerco nella stessa Wi-Fi..." con i puntini che crescono.
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
    if (g.session.info_t > 0) {
      toast(h, g.session.info, ui::L1);
      ui::version(h);
    } else if (v.ngames > 0) {
      footer(g, h, tr("unisciti"), tr("indietro"));
    } else {
      const char* it[2] = { "B", tr("indietro") };
      ui::footer(h, it, 2);
      ui::version(h);
    }
    return;
  }
  // Chi ha creato la partita, o chi si e' unito: la partita e i 4 posti.
  ui::title(h, 320, 26, HF_T32, tr("PARTITA IN RETE"), 3.0f);
  net_game_where(v.game, where, sizeof(where));
  snprintf(buf, sizeof(buf), "%s · %s", net_game_name(v.game, nm, sizeof(nm)), where);
  subtitle(h, 80.0f, buf);
  const float PX = 120.0f, PW = 400.0f, PY = 102.0f;
  lobby_places(h, v, PX, PY, PW);
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
    ui::version(h);
  } else if (v.mode == NV_HOST) {
    footer(g, h, tr("inizia"), tr("annulla"));
  } else {
    const char* it[2] = { "B", v.mode == NV_JOINING ? tr("annulla") : tr("esci") };
    ui::footer(h, it, 2);
    ui::version(h);
  }
}

}  // namespace screens

using namespace screens;

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
