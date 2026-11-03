// On-screen keyboard for the name of a game (0.5.4): new game, rename, new game for a LAN game.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// Tastiera del nome (0.5.4): per una partita nuova (che si crea solo quando si conferma il nome) o
// per cambiare il nome di una che c'e'.
void enter_name(Game& g, int mode, int slot, int who, bool overwrite) {
  g.name_mode = mode;
  g.name_slot = slot;
  g.name_overwrite = overwrite;
  g.menu_owner = who;
  g.slots_pop = SP_NONE;
  g.name_buf[0] = 0;
  if (mode == NM_RENAME) memcpy(g.name_buf, g.session.slot_view[slot].name, sizeof(g.name_buf));
  g.name_buf[SAVE_NAME_LEN - 1] = 0;
  g.name_row = 2;                    // sulla A
  g.name_col = 0;
  go_screen(g, SC_NAME);
}

// Tastiera a schermo: quattro righe da dieci caratteri e la riga dei comandi (SPAZIO sulle prime
// quattro colonne, CANCELLA sulle tre dopo, OK sulle ultime tre).
const char KB_KEYS[4][11] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-.!" };

const int KB_ROWS = 5, KB_COLS = 10, KB_NAME_MAX = SAVE_NAME_LEN - 1;

enum { KA_SPACE = 0, KA_DEL, KA_OK };

const int KA_COL0[3] = { 0, 4, 7 }, KA_COLS[3] = { 4, 3, 3 };

int kb_action(int col) { return col < 4 ? KA_SPACE : (col < 7 ? KA_DEL : KA_OK); }

void name_type(Game& g, char c) {
  const int len = (int)strlen(g.name_buf);
  if (len >= KB_NAME_MAX || (c == ' ' && (len == 0 || g.name_buf[len - 1] == ' '))) {
    ev_sound_ui(g, SND_UI_BACK);                         // pieno, o spazio all'inizio o doppio
    return;
  }
  g.name_buf[len] = c;
  g.name_buf[len + 1] = 0;
  ev_sound_ui(g, SND_UI_MOVE);
}

void name_cancel(Game& g) {
  ev_sound_ui(g, SND_UI_BACK);
  const int slot = g.name_slot;
  if (g.name_mode == NM_RENAME || g.name_overwrite || g.name_mode == NM_NET) {
    enter_slots(g, g.name_mode == NM_RENAME ? SL_MANAGE : (g.name_mode == NM_NET ? SL_NET : SL_OVERWRITE));
    g.slots_sel = slot;
  } else {
    back_to_title(g, TI_NEW);
  }
}

void name_done(Game& g) {
  ev_sound_ui(g, SND_UI_OK);
  const int slot = g.name_slot;
  if (g.name_mode == NM_NEW || g.name_mode == NM_NET) {
    save_slot_new(slot, g.name_buf);             // la partita nasce adesso, con il suo nome
    if (g.name_mode == NM_NET) {                 // ... e si crea la partita in rete
      net_req(g, NR_HOST, slot);
      go_screen(g, SC_LOBBY);
      g.session.lobby_join = false;
    } else {
      go_screen(g, SC_MODE);
    }
    return;
  }
  const bool ok = save_slot_rename(slot, g.name_buf);
  enter_slots(g, SL_MANAGE);
  g.slots_sel = slot;
  if (!ok) {
    char buf[256];
    snprintf(buf, sizeof(buf), tr("non riesco a rinominare il salvataggio %d"), slot + 1);
    info(g, buf);
  }
}

void name_update(Game& g) {
  const int nav = any_nav(g);
  int& r = g.name_row;
  int& c = g.name_col;
  if (nav == NAV_UP) r = (r + KB_ROWS - 1) % KB_ROWS;
  if (nav == NAV_DOWN) r = (r + 1) % KB_ROWS;
  if (nav == NAV_LEFT) c = r == 4 ? KA_COL0[(kb_action(c) + 2) % 3] : (c + KB_COLS - 1) % KB_COLS;
  if (nav == NAV_RIGHT) c = r == 4 ? KA_COL0[(kb_action(c) + 1) % 3] : (c + 1) % KB_COLS;
  bool a = false, b = false, done = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    a |= pad_pressed(g, i, PAD_A);
    b |= pad_pressed(g, i, PAD_B);
    done |= g.start_tap[i];
  }
  if (done) {
    name_done(g);
    return;
  }
  if (a) {
    if (r < 4) {
      name_type(g, KB_KEYS[r][c]);
    } else if (kb_action(c) == KA_SPACE) {
      name_type(g, ' ');
    } else if (kb_action(c) == KA_OK) {
      name_done(g);
      return;
    } else {
      b = true;                                  // CANCELLA come B, ma senza uscire
      if (!g.name_buf[0]) {
        ev_sound_ui(g, SND_UI_BACK);
        return;
      }
    }
  }
  if (b) {
    const int len = (int)strlen(g.name_buf);
    if (len == 0) {
      name_cancel(g);
      return;
    }
    g.name_buf[len - 1] = 0;
    ev_sound_ui(g, SND_UI_BACK);
  }
}

// Tastiera del nome: il campo con il nome (o "PARTITA N" in grigio finche' e' vuoto) e il cursore che
// lampeggia, sotto la tastiera.
void name_hud(const Game& g, Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 26, HF_T32, tr("NOME DELLA PARTITA"), 3.0f);
  char buf[64];
  const float FX = 150.0f, FY = 74.0f, FW = 340.0f, FH = 52.0f, fcy = FY + FH * 0.5f;
  ui::panel(h, FX, FY, FW, FH, 12.0f, ui::SOLID, true);
  const int len = (int)strlen(g.name_buf);
  float cx = FX + 20.0f;
  if (len == 0) {
    snprintf(buf, sizeof(buf), tr("PARTITA %d"), g.name_slot + 1);   // finche' e' vuoto, in grigio dopo il cursore
    ui::text(h, cx + 9.0f, fcy, HF_B22, O(ui::DIS), buf, 3.0f);
  } else {
    ui::text(h, cx, fcy, HF_B22, O(ui::TX), g.name_buf, 3.0f);
    cx += hud_text_w(HF_B22, g.name_buf, 3.0f) + 3.0f;
  }
  if (blinking(g, 16)) hud_box(h, cx, fcy - 12.0f, 3.0f, 24.0f, 0.0f, O(ui::L1));
  snprintf(buf, sizeof(buf), "%d/%d", len, KB_NAME_MAX);
  ui::text(h, FX + FW - 16.0f, fcy, HF_S13, O(ui::MU), buf, 0.5f, 1);
  // La tastiera.
  const float KW = 44.0f, KH = 38.0f, KG = 4.0f, PX = 70.0f, PY = 146.0f;
  const float PW = 24.0f + KW * (float)KB_COLS + KG * (float)(KB_COLS - 1), PH = 24.0f + KH * (float)KB_ROWS + KG * (float)(KB_ROWS - 1);
  ui::panel(h, PX, PY, PW, PH);
  for (int r = 0; r < KB_ROWS; ++r) {
    const float y = PY + 12.0f + (KH + KG) * (float)r;
    const int nk = r < 4 ? KB_COLS : 3;
    for (int k = 0; k < nk; ++k) {
      const int c0 = r < 4 ? k : KA_COL0[k], span = r < 4 ? 1 : KA_COLS[k];
      const float x = PX + 12.0f + (KW + KG) * (float)c0, w = KW * (float)span + KG * (float)(span - 1);
      const bool sel = r == g.name_row && (r < 4 ? k == g.name_col : k == kb_action(g.name_col));
      if (sel) {
        ui::bar_sel(h, x, y, w, KH, 6.0f, true);
      } else {
        hud_box(h, x, y, w, KH, 6.0f, ui::BASE);
        hud_frame(h, x, y, w, KH, 6.0f, 1.5f, O(ui::DIM));
      }
      if (r < 4) {
        const char ch[2] = { KB_KEYS[r][k], 0 };
        ui::text(h, x + w * 0.5f, y + KH * 0.5f, HF_B16, O(sel ? ui::ON : ui::TX), ch, 0.0f, 0);
      } else {
        static const char* const ACT[3] = { N_("SPAZIO"), N_("CANCELLA"), N_("OK") };
        char t[128];
        ui::fit(t, sizeof(t), tr(ACT[k]), HF_S14, 1.0f, w - 12.0f);
        ui::text(h, x + w * 0.5f, y + KH * 0.5f, HF_S14, O(sel ? ui::ON : (k == KA_OK ? ui::L1 : ui::TX)), t, 1.0f, 0);
      }
    }
  }
  if (g.session.info_t > 0) {
    toast(h, g.session.info, ui::L1);
  } else {
    const char* it[6] = { "A", tr("scrivi"), "B", tr("cancella"), "START", tr("fatto") };
    ui::footer(h, it, 6);
  }
  ui::version(h);
}

}  // namespace screens
