// Menu: titolo, salvataggi (con la tastiera per il nome), lingua, modalita', scelta della mappa (con
// il giro e le stelle del pianeta), scelta dei personaggi (ognuno col suo pad), fine mappa con le
// statistiche e il giro; durante la partita la pausa (con il riquadro dei comandi) e le finestre per
// entrare.
// Nei menu A conferma e B torna indietro; START breve vale come A.
// Dalla 0.5.2 il disegno e' nello stile Orbita (ui.h), in pixel a 640x480 come i mockup.
#include "game_int.h"
#include "lang.h"
#include "maps.h"
#include "plat.h"
#include "save.h"
#include "sysinfo.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

namespace {

const int CHARS_START_DELAY = 50;
int g_info_t;                 // avviso temporaneo nei menu ("in arrivo")
char g_info[256];

bool confirm(int i) { return pad_pressed(i, PAD_A) || g.start_tap[i]; }
bool back(int i) { return pad_pressed(i, PAD_B); }
bool any_confirm(int* who) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (confirm(i)) { *who = i; return true; }
  return false;
}
bool any_back(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (back(i)) return true;
  return false;
}
int any_nav(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.in[i].nav) {
      snd_ui(SND_UI_MOVE);
      return g.in[i].nav;
    }
  return NAV_NONE;
}
void info(const char* s) {
  snprintf(g_info, sizeof(g_info), "%s", s);
  g_info_t = 120;
}

// --- salvataggi (0.5.3) ----------------------------------------------------------------------

// Cosa si mostra di ogni posto: il pianeta piu' lontano raggiunto con il suo giro, le stelle del giro,
// le mappe aperte, il tempo di gioco.
struct SlotView { int state, world, giro, stars, smax, open, total; uint32_t seconds; char name[SAVE_NAME_LEN]; };
SlotView g_slot_view[SAVE_SLOTS];

void slots_refresh(void) {
  save_write();                    // la partita attiva com'e' adesso (il tempo di gioco)
  static SaveData d;
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    SlotView& v = g_slot_view[i];
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

// Nome da mostrare: quello dato o "PARTITA N".
const char* slot_name(int i, char* buf, int n) {
  if (i >= 0 && i < SAVE_SLOTS && g_slot_view[i].state == SAVE_LETTO && g_slot_view[i].name[0]) return g_slot_view[i].name;
  snprintf(buf, (size_t)n, tr("PARTITA %d"), i + 1);
  return buf;
}

int first_free_slot(void) {
  for (int i = 0; i < SAVE_SLOTS; ++i)
    if (g_slot_view[i].state == SAVE_NUOVO) return i;
  return -1;
}

int used_slots(void) {
  int n = 0;
  for (int i = 0; i < SAVE_SLOTS; ++i) n += g_slot_view[i].state != SAVE_NUOVO;
  return n;
}

// Tastiera del nome (0.5.4): per una partita nuova (che si crea solo quando si conferma il nome) o
// per cambiare il nome di una che c'e'.
void enter_name(int mode, int slot, int who, bool overwrite) {
  g.name_mode = mode;
  g.name_slot = slot;
  g.name_overwrite = overwrite;
  g.menu_owner = who;
  g.slots_pop = SP_NONE;
  g.name_buf[0] = 0;
  if (mode == NM_RENAME) memcpy(g.name_buf, g_slot_view[slot].name, sizeof(g.name_buf));
  g.name_buf[SAVE_NAME_LEN - 1] = 0;
  g.name_row = 2;                    // sulla A
  g.name_col = 0;
  go_screen(SC_NAME);
}

// Le schermate si preparano prima di andarci: gia' il primo fotogramma e' quello giusto (posti
// aggiornati, cursore al suo posto, nessuna finestra aperta). All'avvio il titolo si prepara nel
// suo primo aggiornamento (i salvataggi si aprono dopo).
void back_to_title(int item) {
  go_screen(SC_TITLE);
  slots_refresh();
  g.menu_sel = item;
}

void enter_slots(int mode) {
  g.slots_mode = mode;
  go_screen(SC_SLOTS);
  slots_refresh();
  g.slots_pop = SP_NONE;
  g.slots_sel = 0;
  if (mode != SL_OVERWRITE)                         // il cursore sul primo posto usato
    while (g.slots_sel < SAVE_SLOTS - 1 && g_slot_view[g.slots_sel].state == SAVE_NUOVO) g.slots_sel++;
}

void enter_lang(void) {
  go_screen(SC_LANG);
  g.lang_sel = lang_get();
}

void title_update(void) {
  if (g.screen_t == 1) slots_refresh();
  const int nav = any_nav();
  if (nav == NAV_UP) g.menu_sel = (g.menu_sel + TITLE_ITEMS - 1) % TITLE_ITEMS;
  if (nav == NAV_DOWN) g.menu_sel = (g.menu_sel + 1) % TITLE_ITEMS;
  int who;
  if (!any_confirm(&who)) return;
  switch (g.menu_sel) {
    case TI_NEW: {
      snd_ui(SND_UI_OK);
      const int free = first_free_slot();
      if (free >= 0) enter_name(NM_NEW, free, who, false);
      else enter_slots(SL_OVERWRITE);
      break;
    }
    case TI_LOAD:
    case TI_MANAGE:
      if (used_slots() == 0) {
        snd_ui(SND_UI_BACK);
        info(tr("nessun salvataggio: comincia una nuova partita"));
      } else {
        snd_ui(SND_UI_OK);
        g.menu_owner = who;
        enter_slots(g.menu_sel == TI_LOAD ? SL_LOAD : SL_MANAGE);
      }
      break;
    case TI_LANG:
      snd_ui(SND_UI_OK);
      enter_lang();
      break;
    default:
      snd_ui(SND_UI_OK);
      plat_log("scelto Esci dal titolo");
      game_request_quit();
      break;
  }
}

void slots_update(void) {
  const int nav = any_nav();
  int who;
  const bool ok = any_confirm(&who), no = !ok && any_back();
  const int sel = g.slots_sel;
  const SlotView& v = g_slot_view[sel];
  char buf[256];
  switch (g.slots_pop) {
    case SP_NONE:
      if (nav == NAV_UP) g.slots_sel = (sel + SAVE_SLOTS - 1) % SAVE_SLOTS;
      if (nav == NAV_DOWN) g.slots_sel = (sel + 1) % SAVE_SLOTS;
      if (ok) {
        if (g.slots_mode == SL_OVERWRITE) {
          snd_ui(SND_UI_OK);
          g.slots_pop = SP_OVERWRITE;
        } else if (v.state == SAVE_NUOVO) {
          snd_ui(SND_UI_BACK);
          info(tr("posto libero"));
        } else if (g.slots_mode == SL_LOAD) {
          snd_ui(SND_UI_OK);
          save_slot_load(sel);
          g.menu_owner = who;
          go_screen(SC_MODE);
        } else {
          snd_ui(SND_UI_OK);
          g.slots_pop = SP_ACTIONS;
          g.slots_pop_sel = 0;
        }
      } else if (no) {
        snd_ui(SND_UI_BACK);
        back_to_title(g.slots_mode == SL_LOAD ? TI_LOAD : (g.slots_mode == SL_MANAGE ? TI_MANAGE : TI_NEW));
      }
      break;
    case SP_ACTIONS:
      if (nav == NAV_UP) g.slots_pop_sel = (g.slots_pop_sel + SA_COUNT - 1) % SA_COUNT;
      if (nav == NAV_DOWN) g.slots_pop_sel = (g.slots_pop_sel + 1) % SA_COUNT;
      if (ok) {
        if (g.slots_pop_sel == SA_DELETE) {
          snd_ui(SND_UI_OK);
          g.slots_pop = SP_DELETE;
          break;
        }
        if (g.slots_pop_sel == SA_RENAME) {
          g.slots_pop = SP_NONE;
          if (v.state != SAVE_LETTO) {
            snd_ui(SND_UI_BACK);
            info(tr("salvataggio illeggibile: non si può rinominare"));
          } else {
            snd_ui(SND_UI_OK);
            enter_name(NM_RENAME, sel, who, false);
          }
          break;
        }
        const int free = first_free_slot();
        if (free < 0) {
          snd_ui(SND_UI_BACK);
          info(tr("nessun posto libero: elimina prima un salvataggio"));
        } else if (save_slot_copy(sel, free)) {
          snd_ui(SND_UI_OK);
          snprintf(buf, sizeof(buf), tr("copiato nel posto %d"), free + 1);
          info(buf);
          slots_refresh();
        } else {
          snd_ui(SND_UI_BACK);
          info(tr("copia non riuscita"));
        }
        g.slots_pop = SP_NONE;
      } else if (no) {
        snd_ui(SND_UI_BACK);
        g.slots_pop = SP_NONE;
      }
      break;
    default:   // SP_DELETE, SP_OVERWRITE: A si', B no
      if (ok) {
        if (g.slots_pop == SP_OVERWRITE) {
          snd_ui(SND_UI_OK);
          enter_name(NM_NEW, sel, who, true);
          break;
        }
        const bool done = save_slot_delete(sel);
        snd_ui(done ? SND_UI_OK : SND_UI_BACK);
        snprintf(buf, sizeof(buf), done ? tr("salvataggio %d eliminato") : tr("non riesco a eliminare il salvataggio %d"), sel + 1);
        info(buf);
        slots_refresh();
        g.slots_pop = SP_NONE;
        if (used_slots() == 0) back_to_title(TI_NEW);    // non resta niente da gestire
      } else if (no) {
        snd_ui(SND_UI_BACK);
        g.slots_pop = SP_NONE;
      }
      break;
  }
}

void lang_update(void) {
  const int nav = any_nav();
  if (nav == NAV_UP) g.lang_sel = (g.lang_sel + NLANGS - 1) % NLANGS;
  if (nav == NAV_DOWN) g.lang_sel = (g.lang_sel + 1) % NLANGS;
  int who;
  if (any_confirm(&who)) {
    snd_ui(SND_UI_OK);
    if (g.lang_sel != lang_get()) {
      g_info_t = 0;                                // un avviso ancora a schermo era nella lingua di prima
      lang_set(g.lang_sel);
      save_data()->lang = (uint8_t)g.lang_sel;
      save_write();
      plat_log("lingua: %s", save_lang_id(g.lang_sel));
    }
    back_to_title(TI_LANG);
  } else if (any_back()) {
    snd_ui(SND_UI_BACK);
    back_to_title(TI_LANG);
  }
}

// --- nome della partita (0.5.4) ------------------------------------------------------------------

// Tastiera a schermo: quattro righe da dieci caratteri e la riga dei comandi (SPAZIO sulle prime
// quattro colonne, CANCELLA sulle tre dopo, OK sulle ultime tre).
const char KB_KEYS[4][11] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-.!" };
const int KB_ROWS = 5, KB_COLS = 10, KB_NAME_MAX = SAVE_NAME_LEN - 1;
enum { KA_SPACE = 0, KA_DEL, KA_OK };
const int KA_COL0[3] = { 0, 4, 7 }, KA_COLS[3] = { 4, 3, 3 };
int kb_action(int col) { return col < 4 ? KA_SPACE : (col < 7 ? KA_DEL : KA_OK); }

void name_type(char c) {
  const int len = (int)strlen(g.name_buf);
  if (len >= KB_NAME_MAX || (c == ' ' && (len == 0 || g.name_buf[len - 1] == ' '))) {
    snd_ui(SND_UI_BACK);                         // pieno, o spazio all'inizio o doppio
    return;
  }
  g.name_buf[len] = c;
  g.name_buf[len + 1] = 0;
  snd_ui(SND_UI_MOVE);
}

void name_cancel(void) {
  snd_ui(SND_UI_BACK);
  const int slot = g.name_slot;
  if (g.name_mode == NM_RENAME || g.name_overwrite) {
    enter_slots(g.name_mode == NM_RENAME ? SL_MANAGE : SL_OVERWRITE);
    g.slots_sel = slot;
  } else {
    back_to_title(TI_NEW);
  }
}

void name_done(void) {
  snd_ui(SND_UI_OK);
  const int slot = g.name_slot;
  if (g.name_mode == NM_NEW) {
    save_slot_new(slot, g.name_buf);             // la partita nasce adesso, con il suo nome
    go_screen(SC_MODE);
    return;
  }
  const bool ok = save_slot_rename(slot, g.name_buf);
  enter_slots(SL_MANAGE);
  g.slots_sel = slot;
  if (!ok) {
    char buf[256];
    snprintf(buf, sizeof(buf), tr("non riesco a rinominare il salvataggio %d"), slot + 1);
    info(buf);
  }
}

void name_update(void) {
  const int nav = any_nav();
  int& r = g.name_row;
  int& c = g.name_col;
  if (nav == NAV_UP) r = (r + KB_ROWS - 1) % KB_ROWS;
  if (nav == NAV_DOWN) r = (r + 1) % KB_ROWS;
  if (nav == NAV_LEFT) c = r == 4 ? KA_COL0[(kb_action(c) + 2) % 3] : (c + KB_COLS - 1) % KB_COLS;
  if (nav == NAV_RIGHT) c = r == 4 ? KA_COL0[(kb_action(c) + 1) % 3] : (c + 1) % KB_COLS;
  bool a = false, b = false, done = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    a |= pad_pressed(i, PAD_A);
    b |= pad_pressed(i, PAD_B);
    done |= g.start_tap[i];
  }
  if (done) {
    name_done();
    return;
  }
  if (a) {
    if (r < 4) {
      name_type(KB_KEYS[r][c]);
    } else if (kb_action(c) == KA_SPACE) {
      name_type(' ');
    } else if (kb_action(c) == KA_OK) {
      name_done();
      return;
    } else {
      b = true;                                  // CANCELLA come B, ma senza uscire
      if (!g.name_buf[0]) {
        snd_ui(SND_UI_BACK);
        return;
      }
    }
  }
  if (b) {
    const int len = (int)strlen(g.name_buf);
    if (len == 0) {
      name_cancel();
      return;
    }
    g.name_buf[len - 1] = 0;
    snd_ui(SND_UI_BACK);
  }
}

// Le due schede stanno affiancate: si cambia con destra/sinistra (e anche su/giu').
void mode_update(void) {
  const int nav = any_nav();
  if (nav != NAV_NONE) g.menu_sel ^= 1;
  int who;
  if (any_confirm(&who)) {
    if (g.menu_sel == 0) {
      snd_ui(SND_UI_OK);
      go_screen(SC_MAPS);
    } else {
      snd_ui(SND_UI_BACK);
      info(tr("CONTRO arriva con la versione 0.6"));
    }
  } else if (any_back()) {
    snd_ui(SND_UI_BACK);
    back_to_title(TI_NEW);
  }
}

void maps_update(void) {
  const int n = maps_count();
  const int nav = any_nav();
  if (nav == NAV_UP) { g.map_sel = (g.map_sel + n - 1) % n; g.map_load_t = 0; }
  if (nav == NAV_DOWN) { g.map_sel = (g.map_sel + 1) % n; g.map_load_t = 0; }
  if (g.map_shown != g.map_sel && ++g.map_load_t > 10) {
    load_map_preview(g.map_sel);
    update_camera(true);
  }
  int who;
  if (any_confirm(&who)) {
    if (!map_unlocked(g.map_sel)) {
      snd_ui(SND_UI_BACK);
      const int w = maps_world(g.map_sel);
      if (map_in_world(g.map_sel) > 0 && world_first_map(w) >= 0 && map_unlocked(world_first_map(w))) {
        info(tr("si apre finendo la mappa prima"));
      } else if (w > 0 && w < NWORLDS) {
        char buf[128];
        snprintf(buf, sizeof(buf), tr("si apre battendo il boss di %s"), tr(WORLD_NAMES[w - 1]));
        info(buf);
      } else {
        info(tr("mappa chiusa"));
      }
      return;
    }
    snd_ui(SND_UI_OK);
    load_map_preview(g.map_sel);
    go_screen(SC_CHARS);
  } else if (any_back()) {
    snd_ui(SND_UI_BACK);
    go_screen(SC_MODE);
  }
}

void chars_update(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.joined[i]) {
      if (confirm(i)) {
        g.joined[i] = true;
        g.ready[i] = false;
        snd_ui(SND_UI_OK);
      }
      continue;
    }
    if (!g.ready[i]) {
      if (g.in[i].nav == NAV_LEFT) { g.pick[i] = (g.pick[i] + NCHARS - 1) % NCHARS; snd_ui(SND_UI_MOVE); }
      if (g.in[i].nav == NAV_RIGHT) { g.pick[i] = (g.pick[i] + 1) % NCHARS; snd_ui(SND_UI_MOVE); }
      if (confirm(i)) { g.ready[i] = true; snd_ui(SND_UI_OK); }
      else if (back(i)) {
        snd_ui(SND_UI_BACK);
        g.joined[i] = false;
        if (g.pl[i].active) player_leave(i);
        bool any = false;
        for (int j = 0; j < MAX_PLAYERS; ++j) any |= g.joined[j];
        if (!any) { go_screen(SC_MAPS); return; }
      }
    } else if (back(i)) {
      g.ready[i] = false;
      snd_ui(SND_UI_BACK);
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
        if (g.joined[i]) player_join(i, g.pick[i], false);
        else if (g.pl[i].active) player_leave(i);
      }
      start_map(g.map_sel);
    }
  } else {
    g.chars_t = 0;
  }
}

// Fine mappa: le stelle compaiono una alla volta (con il loro suono), poi il giro; A vale dopo
// l'ultima stella.
const int STAR_T0 = 40, STAR_STEP = 26, STATS_READY = STAR_T0 + 2 * STAR_STEP + 12;

void stats_update(void) {
  for (int k = 0; k < 3; ++k)
    if (g.screen_t == STAR_T0 + STAR_STEP * k && ((g.stars_got >> k) & 1)) snd_ui(SND_STAR);
  if (g.screen_t == STATS_READY && g.giro_up) snd_ui(SND_ZONE_CLEAR);
  if (g.screen_t < STATS_READY) return;
  int who;
  if (any_confirm(&who)) {
    snd_ui(SND_UI_OK);
    // Avanti nella campagna: la mappa dopo dello stesso pianeta (finita questa, e' aperta). Dopo
    // il boss di un pianeta si va al pianeta dopo, se c'e' gia', altrimenti si torna alla prima
    // mappa di questo.
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
    load_map_preview(next);
    go_screen(SC_CHARS);
  } else if (any_back()) {
    snd_ui(SND_UI_BACK);
    go_screen(SC_MAPS);
  }
}

// --- disegno -------------------------------------------------------------------------------

inline uint32_t O(uint32_t rgb) { return HUD_OPAQUE(rgb); }
inline uint32_t A(uint32_t rgb, float a) { return hud_a(rgb, a); }
bool blinking(unsigned period) { return ((g.ui_tick / period) & 1u) == 0u; }

// Avviso al posto del piede (in arrivo, mappa chiusa, errore di una mappa personalizzata): fino a
// tre righe, larghe al massimo 440 pixel (a destra resta il posto per il numero della versione).
void toast(Hud* h, const char* s, uint32_t color) {
  char L[3][ui::WRAP_LEN];
  const int n = ui::wrap(s, 440.0f, HF_S14, 0.5f, L, 3);
  float tw = 0.0f;
  for (int k = 0; k < n; ++k) tw = fmaxf(tw, hud_text_w(HF_S14, L[k], 0.5f));
  const float w = tw + 40.0f, hh = 12.0f + 18.0f * (float)n, y = 480.0f - ui::SAFE_Y - hh;
  ui::panel(h, 320.0f - w * 0.5f, y, w, hh, 8.0f, ui::SOLID);
  for (int k = 0; k < n; ++k) ui::text(h, 320.0f, y + 15.0f + 18.0f * (float)k, HF_S14, O(color), L[k], 0.5f, 0);
}

void footer(Hud* h, const char* a_label, const char* b_label) {
  if (g_info_t > 0) {
    toast(h, g_info, ui::L1);
  } else {
    const char* it[4] = { "A", a_label, "B", b_label };
    ui::footer(h, it, b_label ? 4 : 2);
  }
  ui::version(h);
}

void title_hud(Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  // L'orbita intorno al nome, con il pianetino ciano.
  HudShape r;
  memset(&r, 0, sizeof(r));
  r.type = HS_RING; r.x = 40; r.y = 22; r.w = 560; r.h = 132; r.ring = 2.0f; r.soft = 12.0f; r.c0 = r.c1 = A(0xFF785A, 0.45f);
  hud_shape(h, r);
  hud_ring(h, 320, 88, 280, 66, 2.0f, ui::RING);
  hud_ellipse(h, 558, 127, 7, 7, A(ui::A2, 0.9f), 10.0f);
  hud_ellipse(h, 558, 127, 7, 7, O(ui::A2));
  const float base = hud_mid(HF_LOGO, 44.0f + 40.0f);
  hud_text(h, 320, base + 3.0f, HF_LOGO, O(ui::TDROP), "PUPAZZI", 9.0f, 0);
  hud_text_glow(h, 320, base, HF_LOGO, ui::TGLOW, "PUPAZZI", 9.0f, 0);
  hud_text(h, 320, base, HF_LOGO, 0xFFFFFFFFu, "PUPAZZI", 9.0f, 0);
  const float cy = 164.0f + 9.75f;
  char tag[256];
  ui::fit(tag, sizeof(tag), tr("DA 1 A 4 AMICI CONTRO I MOSTRI"), HF_S15, 2.0f, 576.0f);
  ui::text(h, 320, cy + 1.0f, HF_S15, A(ui::SCRIM, 0.9f), tag, 2.0f, 0);
  ui::text(h, 320, cy, HF_S15, O(ui::FTX), tag, 2.0f, 0);
  // Le voci a destra (gli eroi stanno a sinistra: la telecamera del titolo e' spostata).
  const float PX = 372.0f, PW = 236.0f, PY = 198.0f, RH = 38.0f, IX = PX + 10.0f, IW = PW - 20.0f;
  ui::panel(h, PX, PY, PW, 16.0f + RH * (float)TITLE_ITEMS);
  static const char* const LABEL[TITLE_ITEMS] = { N_("NUOVA PARTITA"), N_("CARICA SALVATAGGIO"), N_("GESTISCI SALVATAGGI"), N_("LINGUA"),
                                                  N_("ESCI") };
  const bool none = used_slots() == 0;
  for (int k = 0; k < TITLE_ITEMS; ++k) {
    const float y = PY + 8.0f + RH * (float)k, cy2 = y + RH * 0.5f;
    const bool sel = k == g.menu_sel, off = none && (k == TI_LOAD || k == TI_MANAGE);
    if (sel) ui::bar_sel(h, IX, y + 2.0f, IW, RH - 4.0f);
    const uint32_t c = O(sel ? ui::ON : (off ? ui::DIS : (k == TI_QUIT ? ui::A2 : ui::TX)));
    float tx = IX + 14.0f, room = IW - 28.0f;
    if (k == TI_LANG) {                        // il mappamondo: si trova anche senza capire la lingua
      ui::globe(h, tx + 7.5f, cy2, sel ? ui::ON : ui::L1);
      tx += 23.0f;
      room -= 23.0f;
      const char* ln = lang_name(lang_get());
      const float lw = hud_text_w(HF_S13, ln, 0.5f);
      ui::text(h, IX + IW - 14.0f, cy2, HF_S13, O(sel ? ui::ON : ui::MU), ln, 0.5f, 1);
      room -= lw + 10.0f;
    }
    ui::fit(tag, sizeof(tag), tr(LABEL[k]), HF_S16, 1.0f, room);
    ui::text(h, tx, cy2, HF_S16, c, tag, 1.0f);
  }
  if (g_info_t > 0) toast(h, g_info, ui::L1);
  else {
    const char* it[2] = { "A", tr("conferma") };
    ui::footer(h, it, 2);
  }
  ui::version(h);
}

// Durata del gioco: "12 min", "1 h 05 min".
void play_time(char* buf, int n, uint32_t s) {
  const uint32_t m = s / 60u;
  if (m < 60u) snprintf(buf, (size_t)n, tr("%u min"), m);
  else snprintf(buf, (size_t)n, tr("%u h %02u min"), m / 60u, m % 60u);
}

// Un posto: numero, nome; sotto pianeta e giro, stelle, mappe aperte, tempo; vuoto o illeggibile.
void slot_row(Hud* h, float x, float y, float w, float hh, int i, bool sel) {
  const SlotView& v = g_slot_view[i];
  char buf[256], t[64];
  if (sel) ui::bar_sel(h, x, y, w, hh);
  const float cy = y + hh * 0.5f;
  const uint32_t mc = O(sel ? ui::ON : ui::TX), sub = O(sel ? ui::ON : ui::MU);
  // Il numero in un tondo.
  hud_ring(h, x + 30.0f, cy, 16.0f, 16.0f, 2.0f, O(sel ? ui::ON : ui::L1));
  snprintf(buf, sizeof(buf), "%d", i + 1);
  ui::text(h, x + 30.0f, cy, HF_B16, O(sel ? ui::ON : ui::L1), buf, 0.0f, 0);
  const float tx = x + 62.0f;
  if (v.state == SAVE_NUOVO) {
    ui::text(h, tx, cy, HF_B16, O(sel ? ui::ON : ui::DIS), tr("POSTO LIBERO"), 1.5f);
    return;
  }
  if (v.state == SAVE_ILLEGGIBILE) {
    ui::text(h, tx, cy - 9.0f, HF_B16, O(sel ? ui::ON : ui::L2), tr("ILLEGGIBILE"), 1.5f);
    ui::text(h, tx, cy + 11.0f, HF_S13, sub, tr("si riparte dall'inizio"), 0.5f);
    return;
  }
  char wn[96], nm[64];
  ui::text(h, tx, cy - 9.0f, HF_B16, mc, slot_name(i, nm, sizeof(nm)), 1.5f);
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
  // Il pianeta a destra.
  if (v.world == 0) ui::earth(h, x + w - 40.0f, cy - 13.0f);
  else ui::planet(h, x + w - 34.0f, cy - 7.5f, v.world);
}

// Finestra sopra i posti: titolo e una o due righe; con le voci (azioni) o "A si' / B no".
void slots_popup(Hud* h) {
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
             ui::fit(t, sizeof(t), slot_name(i, nm, sizeof(nm)), HF_S14, 1.0f, W - 58.0f - nw), 1.0f);
    ui::sep(h, X + 22.0f, Y + 56.0f, W - 44.0f);
    static const char* const ACT[SA_COUNT] = { N_("DUPLICA"), N_("RINOMINA"), N_("ELIMINA") };
    static const char* const HINT[SA_COUNT] = { N_("una copia nel primo posto libero"), N_("gli dai un nome nuovo"),
                                                N_("lo cancella per sempre") };
    for (int k = 0; k < SA_COUNT; ++k) {
      const float y = Y + 66.0f + 48.0f * (float)k;           // righe alte anche per cinese, giapponese e hindi
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
  const SlotView& v = g_slot_view[i];
  if (v.state == SAVE_LETTO) {
    char wn[96], t[64], nm[64];
    ui::text(h, 320.0f, Y + 58.0f, HF_S14, O(ui::L1), ui::fit(fitted, sizeof(fitted), slot_name(i, nm, sizeof(nm)), HF_S14, 1.0f, W - 44.0f),
             1.0f, 0);
    hud_upper(wn, tr(WORLD_NAMES[v.world]), sizeof(wn));
    play_time(t, sizeof(t), v.seconds);
    snprintf(buf, sizeof(buf), tr("%s, giro %d: %d stelle, %s di gioco"), wn, v.giro, v.stars, t);
    ui::fit(fitted, sizeof(fitted), buf, HF_S13, 0.5f, W - 44.0f);
    ui::text(h, 320.0f, Y + 80.0f, HF_S13, O(ui::MU), fitted, 0.5f, 0);
  }
  // A si', B no (come le finestre per entrare).
  const char* yes = tr("sì");
  const char* no = tr("no");
  const float ww = 22.0f + 7.0f + hud_text_w(HF_S14, yes, 0.5f) + 34.0f + 22.0f + 7.0f + hud_text_w(HF_S14, no, 0.5f);
  float kx = 320.0f - ww * 0.5f;
  kx += ui::key(h, kx, Y + 116.0f, "A") + 7.0f;
  ui::text(h, kx, Y + 116.0f, HF_S14, O(ui::TX), yes, 0.5f);
  kx += hud_text_w(HF_S14, yes, 0.5f) + 34.0f;
  kx += ui::key(h, kx, Y + 116.0f, "B") + 7.0f;
  ui::text(h, kx, Y + 116.0f, HF_S14, O(ui::TX), no, 0.5f);
}

void slots_hud(Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  static const char* const TITLE[3] = { N_("CARICA SALVATAGGIO"), N_("GESTISCI SALVATAGGI"), N_("DOVE SALVARE?") };
  char t[256];
  ui::fit(t, sizeof(t), tr(TITLE[g.slots_mode]), HF_T32, 3.0f, 560.0f, "...");
  ui::title(h, 320, 26, HF_T32, t, 3.0f);
  float PY = 86.0f;
  if (g.slots_mode == SL_OVERWRITE) {
    ui::fit(t, sizeof(t), tr("i posti sono pieni: la partita nuova prende il posto di quella scelta"), HF_S14, 0.5f, 576.0f);
    ui::text(h, 321, 79, HF_S14, A(ui::SCRIM, 0.9f), t, 0.5f, 0);
    ui::text(h, 320, 78, HF_S14, O(ui::FTX), t, 0.5f, 0);
    PY = 96.0f;
  }
  const float PX = 84.0f, PW = 472.0f, RH = 62.0f, GAP = 6.0f;
  ui::panel(h, PX, PY, PW, 20.0f + RH * (float)SAVE_SLOTS + GAP * (float)(SAVE_SLOTS - 1));
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    const float y = PY + 10.0f + (RH + GAP) * (float)i;
    if (i > 0) ui::sep(h, PX + 24.0f, y - GAP * 0.5f, PW - 48.0f);
    slot_row(h, PX + 10.0f, y, PW - 20.0f, RH, i, i == g.slots_sel);
  }
  if (g.slots_pop != SP_NONE) {
    slots_popup(h);
    return;
  }
  if (g_info_t > 0) {
    toast(h, g_info, ui::L1);
    ui::version(h);
    return;
  }
  static const char* const ACT[3] = { N_("carica"), N_("scegli"), N_("sovrascrivi") };
  const char* it[4] = { "A", tr(ACT[g.slots_mode]), "B", tr("indietro") };
  ui::footer(h, it, 4);
  ui::version(h);
}

void lang_hud(Hud* h) {
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
    hud_cjk(l == LANG_JA ? HUD_CJK_JP : HUD_CJK_SC);       // 中文 e 日本語 ognuno con i suoi caratteri
    ui::text(h, IX + 32.0f, cy, HF_S16, O(sel ? ui::ON : ui::TX), lang_name(l), 1.0f);
    hud_cjk(keep);
  }
  const char* it[4] = { "A", tr("scegli"), "B", tr("indietro") };
  ui::footer(h, it, 4);
  ui::version(h);
}

// Tastiera del nome: il campo con il nome (o "PARTITA N" in grigio finche' e' vuoto) e il cursore che
// lampeggia, sotto la tastiera.
void name_hud(Hud* h) {
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
  if (blinking(16)) hud_box(h, cx, fcy - 12.0f, 3.0f, 24.0f, 0.0f, O(ui::L1));
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
  if (g_info_t > 0) {
    toast(h, g_info, ui::L1);
  } else {
    const char* it[6] = { "A", tr("scrivi"), "B", tr("cancella"), "START", tr("fatto") };
    ui::footer(h, it, 6);
  }
  ui::version(h);
}

// Scheda di Come si gioca: Insieme (i 4 eroi) o Contro (2 contro 2, in arrivo).
void mode_card(Hud* h, float x, float y, bool insieme, bool sel) {
  const float W = 262.0f, H = 112.0f, cx0 = x + 20.0f;
  if (sel) ui::card_sel(h, x, y, W, H);
  else ui::panel_off(h, x, y, W, H);
  char t[256];
  if (insieme) {
    for (int k = 0; k < 4; ++k) ui::avatar(h, cx0 + 27.0f * (float)k, y + 13.0f, k, false);
    ui::text(h, cx0, y + 64.0f, HF_T25, O(ui::TX), tr("INSIEME"), 3.0f);
    ui::text(h, cx0, y + 91.5f, HF_S14, O(ui::L1), ui::fit(t, sizeof(t), tr("tutti insieme contro i mostri"), HF_S14, 0.5f, W - 40.0f), 0.5f);
  } else {
    ui::avatar(h, cx0, y + 13.0f, 0, true);
    ui::avatar(h, cx0 + 27.0f, y + 13.0f, 1, true);
    const float vx = cx0 + 59.0f + 8.0f, vw = hud_text_w(HF_B12, "VS", 1.0f);
    ui::text(h, vx, y + 29.0f, HF_B12, A(ui::TX, 0.45f), "VS", 1.0f);
    ui::avatar(h, vx + vw + 8.0f, y + 13.0f, 2, true);
    ui::avatar(h, vx + vw + 35.0f, y + 13.0f, 3, true);
    ui::text(h, cx0, y + 64.0f, HF_T25, O(ui::DIS), tr("CONTRO"), 3.0f);
    ui::text(h, cx0, y + 91.5f, HF_S14, O(ui::DIS), tr("2 contro 2"), 0.5f);
    ui::tag(h, x + W - 16.0f, y + H - 26.5f, tr("IN ARRIVO"), ui::A2, HF_B11, 1.5f, 6.0f, 21.0f, 1);
  }
}

void mode_hud(Hud* h) {
  ui::scrim_top(h);
  ui::scrim_bottom(h);
  ui::title(h, 320, 36, HF_T38, tr("COME SI GIOCA?"), 3.0f);
  mode_card(h, 52, 308, true, g.menu_sel == 0);
  mode_card(h, 326, 308, false, g.menu_sel == 1);
  footer(h, tr("conferma"), tr("indietro"));
}

// Stelle del giro di un pianeta: quante, quante ne servono, cosa dire a destra.
struct Giro { int stars, max, need, giro; bool last, ready; };
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

void maps_hud(Hud* h) {
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
  const bool note = maps_note(g.map_sel)[0] && g_info_t <= 0;
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
    footer(h, tr("gioca"), tr("indietro"));
  }
}

void chars_hud(Hud* h) {
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
  // Il colpo e lo speciale di chi sceglie per primo.
  int show = -1;
  for (int i = 0; i < MAX_PLAYERS && show < 0; ++i)
    if (g.joined[i] && !g.ready[i]) show = i;
  if (show >= 0) {
    // Due righe sopra le teste: chi e' (nome, da dove viene, che tipo), poi colpo e speciale.
    const CharDef& cd = CHARS[g.pick[show]];
    const char *nm = tr(cd.name), *normal = tr(cd.normal), *special = tr(cd.special);
    snprintf(buf, sizeof(buf), "%s · %s", tr(cd.who), tr(cd.role));
    const float w1 = 24.0f + 10.0f + hud_text_w(HF_B22, nm, 2.0f) + 12.0f + hud_text_w(HF_S13, buf, 0.5f);
    const float w2 = 2.0f * (34.0f + 8.0f) + hud_text_w(HF_S13, normal, 0.3f) + 20.0f + hud_text_w(HF_S13, special, 0.3f) + 2.0f;   // tasti larghi 34
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
  // Un riquadro per giocatore, in basso.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const float x = 32.5f + 147.0f * (float)i, y = 346.0f, W = 134.0f, H = 82.0f, cx = x + W * 0.5f;
    if (!g.joined[i]) {
      ui::panel_off(h, x, y, W, H, 12.0f);
      ui::gp(h, x + 12.0f, y + 16.0f, i);
      if (blinking(20)) {
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

void stats_hud(Hud* h) {
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
  // Tabella: tutti quelli che hanno giocato (anche chi e' rimasto senza vite).
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
  // Stelle della mappa, una alla volta: quelle prese ora "saltano"; NUOVA! se nel giro non c'era.
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
  // Il giro del pianeta: dopo il boss com'e' andata, altrimenti le stelle del giro.
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

}  // namespace

void menu_update(void) {
  if (g_info_t > 0) g_info_t--;
  switch (g.screen) {
    case SC_TITLE: title_update(); break;
    case SC_MODE: mode_update(); break;
    case SC_MAPS: maps_update(); break;
    case SC_CHARS: chars_update(); break;
    case SC_STATS: stats_update(); break;
    case SC_SLOTS: slots_update(); break;
    case SC_LANG: lang_update(); break;
    case SC_NAME: name_update(); break;
    default: break;
  }
}

// Dopo la pausa o l'ingresso in partita il tasto con cui si e' confermato e' ancora premuto:
// per il gioco non deve contare come una pressione nuova (A lascerebbe una bomba, B salterebbe).
static void swallow_held(int i) { g.pl[i].prev_btn = g.in[i].btn; }

static void unpause(void) {
  g.paused = false;
  g.pause_help = false;
  for (int j = 0; j < MAX_PLAYERS; ++j) swallow_held(j);
}

void play_menus_update(void) {
  // START breve: pausa (chi la apre la comanda), oppure la chiude.
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.start_tap[i] || g.pl[i].joining) continue;
    if (!g.paused) {
      if (!g.pl[i].active) continue;
      g.paused = true;
      g.pause_help = false;
      g.pause_owner = i;
      g.pause_sel = 0;
      snd_ui(SND_PAUSE);
      plat_log("pausa (giocatore %d)", i + 1);
    } else {
      unpause();
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
      if (back(o) || pad_pressed(o, PAD_A)) {
        g.pause_help = false;
        snd_ui(SND_UI_BACK);
      }
    } else {
      // Voci: riprendi, comandi, mira, antialias, musica, effetti, lascia la partita, torna al menu.
      if (g.in[o].nav == NAV_UP) { g.pause_sel = (g.pause_sel + PAUSE_ITEMS - 1) % PAUSE_ITEMS; snd_ui(SND_UI_MOVE); }
      if (g.in[o].nav == NAV_DOWN) { g.pause_sel = (g.pause_sel + 1) % PAUSE_ITEMS; snd_ui(SND_UI_MOVE); }
      const bool left = g.in[o].nav == NAV_LEFT, right = g.in[o].nav == NAV_RIGHT || pad_pressed(o, PAD_A);
      // Mira: con sinistra/destra (o A) si sceglie quanto e' pronta la levetta destra di chi comanda.
      if (g.pause_sel == PS_AIM) {
        int lvl = aim_level_of(o);
        if (left) lvl = (lvl + NAIMLEVELS - 1) % NAIMLEVELS;
        if (right) lvl = (lvl + 1) % NAIMLEVELS;
        if (lvl != g.aim_level[o]) {
          g.aim_level[o] = (uint8_t)lvl;
          if (o < SAVE_PLAYERS) save_data()->aim[o] = (uint8_t)lvl;
          snd_ui(SND_UI_MOVE);
          settings_changed();
          plat_log("giocatore %d: mira %s", o + 1, AIM_TUNE[lvl].name);
        }
      }
      // Antialias (bordi lisci, per tutti): sinistra/destra o A lo accendono e lo spengono.
      // Antialias (per tutti, dalla 0.5.4): NO, 4X, 8X fin dove arriva la GPU; sinistra/destra o A.
      if (g.pause_sel == PS_AA && g.aa_max > 0 && (left || right)) {
        int vals[3], nv = 0;
        vals[nv++] = 0;
        vals[nv++] = g.aa_max >= 4 ? 4 : g.aa_max;
        if (g.aa_max >= 8) vals[nv++] = 8;
        const int now = game_aa_wanted();
        int k = 0;
        while (k < nv - 1 && vals[k] != now) k++;
        k = left ? (k + nv - 1) % nv : (k + 1) % nv;
        g.aa_level = vals[k];
        save_data()->antialias = (uint8_t)vals[k];
        snd_ui(SND_UI_MOVE);
        settings_changed();
        if (vals[k] > 0) plat_log("antialias %dx (giocatore %d)", vals[k], o + 1);
        else plat_log("antialias spento (giocatore %d)", o + 1);
      }
      // Volumi (per tutti, da 0 a 10): sinistra/destra; A fa il giro.
      if ((g.pause_sel == PS_MUSIC || g.pause_sel == PS_SFX) && (left || right)) {
        SaveData* sd = save_data();
        uint8_t& v = g.pause_sel == PS_MUSIC ? sd->music_vol : sd->sfx_vol;
        if (left) v = (uint8_t)(v > 0 ? v - 1 : 0);
        else if (g.in[o].nav == NAV_RIGHT) v = (uint8_t)(v < 10 ? v + 1 : 10);
        else v = (uint8_t)(v < 10 ? v + 1 : 0);
        audio_set_volume(sd->music_vol, sd->sfx_vol);
        snd_ui(SND_UI_MOVE);
        settings_changed();
        plat_log("volume %s %d", g.pause_sel == PS_MUSIC ? "musica" : "effetti", (int)v);
      }
      if (back(o)) {
        snd_ui(SND_UI_BACK);
        unpause();
      } else if (pad_pressed(o, PAD_A) && g.pause_sel == PS_CONTROLS) {
        g.pause_help = true;
        snd_ui(SND_UI_OK);
      } else if (pad_pressed(o, PAD_A) && (g.pause_sel == PS_RESUME || g.pause_sel == PS_LEAVE || g.pause_sel == PS_MENU)) {
        snd_ui(SND_UI_OK);
        if (g.pause_sel == PS_RESUME) {
          unpause();
        } else if (g.pause_sel == PS_LEAVE) {
          player_leave(o);
          g.joined[o] = g.ready[o] = false;
          unpause();
          if (count_active() == 0) {
            back_to_title(TI_NEW);
            return;
          }
        } else {
          plat_log("partita interrotta dalla pausa");
          g.pause_help = false;
          go_screen(SC_MAPS);
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
    if (confirm(i)) {
      const int ch = p.join_pick;
      g.pick[i] = ch;
      g.joined[i] = true;
      player_join(i, ch, true);
      swallow_held(i);
    } else if (back(i)) {
      p.joining = false;
    }
  }
}

void menu_build_hud(Hud* h, const Camera& cam) {
  (void)cam;
  switch (g.screen) {
    case SC_TITLE: title_hud(h); break;
    case SC_MODE: mode_hud(h); break;
    case SC_MAPS: maps_hud(h); break;
    case SC_CHARS: chars_hud(h); break;
    case SC_STATS: stats_hud(h); break;
    case SC_SLOTS: slots_hud(h); break;
    case SC_LANG: lang_hud(h); break;
    case SC_NAME: name_hud(h); break;
    default: break;
  }
  // La batteria della console in alto a destra (0.5.4), sopra i titoli.
  const SysBattery bat = sys_battery(plat_now_ms());
  if (bat.present) ui::battery(h, 640.0f - ui::SAFE_X, 17.0f, bat.percent, bat.charging, g.ui_tick);
}

// Finestre di chi entra a partita iniziata (anche in pausa: allora sopra la pausa).
bool join_panels(Hud* h) {
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

void play_menus_hud(Hud* h) {
  if (!g.paused) {
    join_panels(h);
    return;
  }
  bool joining = false, waiting = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    joining |= !g.pl[i].active && g.pl[i].joining;
    waiting |= !g.pl[i].active && !g.pl[i].joining;
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
          const char* v = tr(aim_tune(g.pause_owner).label);
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
            const int now = game_aa_wanted();
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
    join_panels(h);
  } else if (waiting) {
    const char* msg = tr("premete un tasto per entrare");
    const float cy = 480.0f - ui::SAFE_Y - 11.0f;
    float ww = 4.0f + hud_text_w(HF_S13, msg, 1.0f);
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active && !g.pl[i].joining) ww += 30.0f;
    float x = 320.0f - ww * 0.5f;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active && !g.pl[i].joining) x += ui::gp(h, x, cy, i) + 6.0f;
    ui::text(h, x + 4.0f, cy, HF_S13, O(ui::FTX), msg, 1.0f);
  }
}
