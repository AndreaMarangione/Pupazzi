// Carte (0.4): all'inizio di ogni mappa e dopo ogni zona ripulita, se ne segue un'altra, ogni
// giocatore sceglie 1 carta su 3, che vale fino alla fine della mappa (resta anche se la zona si
// rifa'; se la mappa ricomincia da capo si riparte senza carte). Mentre la scelta e' aperta il
// gioco e' fermo come nella pausa; ognuno sceglie sul suo riquadro con su/giu' e A (o START
// breve); chi e' senza vite non sceglie. Qui ci sono anche gli effetti che non stanno meglio
// altrove: scudo, vita extra, speciale generoso, bruciature e vapore delle combo.
#include "game_int.h"
#include "lang.h"
#include "plat.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const CardDef CARDS[NCARDS] = {
  { "", "", NULL, false },
  { N_("BOMBAROLO"), N_("porti fino a 4 bombe e ne hai subito una in più"), NULL, false },
  { N_("PASSO SVELTO"), N_("+15% di velocità"), NULL, false },
  { N_("PELLE DURA"), N_("+30 di vita massima, subito piena"), NULL, false },
  { N_("RICARICA LAMPO"), N_("la barra del colpo si ricarica il 40% più in fretta"), NULL, false },
  { N_("SUPER CARICA"), N_("lo speciale si carica il 35% più in fretta"), NULL, false },
  { N_("COLPI PESANTI"), N_("+25% di danni del colpo normale"), NULL, false },
  { N_("LUNGA GITTATA"), N_("+25% di gittata del colpo normale"), NULL, false },
  { N_("MERENDA"), N_("ogni mostro sconfitto ti ridà il doppio della vita"), NULL, false },
  { N_("SCUDO"), N_("a ogni zona uno scudo ferma il primo colpo che prendi"), NULL, false },
  { N_("VITA EXTRA"), N_("una vita in più per questa mappa"), NULL, false },
  { N_("CALAMITA"), N_("prendi talismani e bombe da 3 metri"), NULL, false },
  { N_("BOMBE GIGANTI"), N_("le tue bombe fanno il 50% di danni in più su un'area più larga"),
    N_("le tue bombe: +50% di danni, area più larga"), false },
  { N_("SPECIALE GENEROSO"), N_("quando usi lo speciale i compagni vicini recuperano 25 vita"),
    N_("il tuo speciale dà 25 di vita ai compagni vicini"), false },
  { N_("PERFORANTE"), N_("i colpi normali attraversano un mostro in più"), NULL, false },
  { N_("RIMBALZO"), N_("i colpi normali rimbalzano una volta su muri, massi e alberi"), NULL, false },
  { N_("COLPI INFUOCATI"), N_("con Zolfo: i tuoi colpi incendiano i mostri"), NULL, true },
  { N_("COLPI GELATI"), N_("con Marzio: i tuoi colpi rallentano i mostri"), NULL, true },
  { N_("SCHEGGE ROVENTI"), N_("con Zolfo: le tue schegge incendiano i mostri"), NULL, true },
  { N_("SCHEGGE GELATE"), N_("con Marzio: le tue schegge rallentano i mostri"), NULL, true },
  { N_("VAPORE"), N_("ghiaccio + fuoco: il mostro rallentato che brucia scoppia in vapore"),
    N_("ghiaccio + fuoco sullo stesso mostro: scoppia in vapore"), true },
};

namespace {

bool g_on = true;                       // game_set_cards_enabled: vale per tutta la sessione

const float BURN_DMG = 1.0f;            // ogni BURN_EVERY fotogrammi: 4 al secondo per 3 secondi
const int BURN_FRAMES = 180, BURN_EVERY = 15;
const float ICE_SLOW = 0.7f;            // combo di ghiaccio: -30% per 1,5 s (Marzio: -40%)
const int ICE_FRAMES = 90;
const float VAPOR_DMG = 30.0f, VAPOR_R = 1.6f;
const int VAPOR_CD = 90;                // lo stesso mostro scoppia al massimo ogni 1,5 s
const float GENEROUS_R = 6.0f, GENEROUS_HP = 25.0f;
const int SHIELD_INVULN = 45;           // dopo lo scudo: niente secondo colpo dallo stesso attacco

// --- proposta ---------------------------------------------------------------------

// Un compagno con il personaggio ch ancora in gioco (chi e' senza vite non conta: non rientra).
bool partner(int i, int ch) {
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (j != i && g.pl[j].active && !g.pl[j].out && g.pl[j].ch == ch) return true;
  return false;
}

// Chi tiene VAPORE (vale per tutta la squadra): chi ha colpito, se ce l'ha, se no il primo.
int vapor_owner(int hitter) {
  if (hitter >= 0 && hitter < MAX_PLAYERS && g.pl[hitter].active && has_card(g.pl[hitter], CA_VAPORE)) return hitter;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (g.pl[j].active && has_card(g.pl[j], CA_VAPORE)) return j;
  return -1;
}

bool eligible(int i, int c) {
  const Player& p = g.pl[i];
  if (has_card(p, c)) return false;
  switch (c) {
    case CA_GENEROSO: return count_in_play() >= 2;
    case CA_PERFORANTE: case CA_RIMBALZO: return p.ch != CH_ZOLFO;   // la fiamma attraversa gia'
    case CA_COLPI_FUOCO: return p.ch == CH_ASTRO && partner(i, CH_ZOLFO);
    case CA_COLPI_GELO: return p.ch == CH_ASTRO && partner(i, CH_MARZIO);
    case CA_SCHEGGE_FUOCO: return p.ch == CH_MOCHI && partner(i, CH_ZOLFO);
    case CA_SCHEGGE_GELO: return p.ch == CH_MOCHI && partner(i, CH_MARZIO);
    case CA_VAPORE:
      if (vapor_owner(-1) >= 0) return false;                        // c'e' gia' per tutti
      return (p.ch == CH_MARZIO && partner(i, CH_ZOLFO)) || (p.ch == CH_ZOLFO && partner(i, CH_MARZIO));
    default: return true;
  }
}

// Toglie a caso una carta dall'elenco (una sola chiamata al generatore).
uint8_t draw_from(uint8_t* list, int* n) {
  const int k = (int)(rng_next() % (uint32_t)*n);
  const uint8_t c = list[k];
  list[k] = list[--*n];
  return c;
}

void make_offer(int i) {
  CardOffer& o = g.offer[i];
  memset(&o, 0, sizeof(o));
  uint8_t basic[NCARDS], combo[NCARDS];
  int nb = 0, nc = 0;
  for (int c = 1; c < NCARDS; ++c) {
    if (!eligible(i, c)) continue;
    if (CARDS[c].combo) combo[nc++] = (uint8_t)c;
    else basic[nb++] = (uint8_t)c;
  }
  // Con almeno una combo possibile, meta' delle volte una delle tre e' una combo.
  bool one_combo = false;
  if (nc > 0) one_combo = rng_f() < 0.5f;
  while (o.n < (one_combo ? 2 : 3) && nb > 0) o.card[o.n++] = draw_from(basic, &nb);
  bool got_combo = false;
  while (o.n < 3 && nc > 0 && (one_combo || nb == 0)) {
    o.card[o.n++] = draw_from(combo, &nc);
    got_combo = true;
  }
  if (got_combo && o.n > 1) {          // la combo non sempre in fondo
    const int pos = (int)(rng_next() % (uint32_t)o.n);
    const uint8_t t = o.card[pos];
    o.card[pos] = o.card[o.n - 1];
    o.card[o.n - 1] = t;
  }
  o.on = o.n > 0;
  char buf[96];
  int len = 0;
  buf[0] = 0;
  for (int k = 0; k < o.n && len < (int)sizeof(buf); ++k)
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s%s", k ? ", " : "", CARDS[o.card[k]].name);
  plat_log("carte per G%d (%s): %s", i + 1, CHARS[g.pl[i].ch].name, o.n ? buf : "nessuna");
}

void card_take(int i, int c) {
  Player& p = g.pl[i];
  p.cards |= 1u << c;
  switch (c) {
    case CA_BOMBAROLO:
      if (p.bombs < pl_bombs_cap(p)) p.bombs++;
      break;
    case CA_PELLE:
      p.hp_max += 30.0f;
      if (p.alive) p.hp = p.hp_max;     // chi e' a terra la trova quando rientra
      break;
    case CA_SUPER:
      if (p.charge > pl_special_cost(p)) p.charge = pl_special_cost(p);
      break;
    case CA_SCUDO:
      p.shield = true;
      break;
    case CA_VITA:
      p.lives++;                         // subito, per questa mappa
      plat_log("G%d: una vita in piu' (vite %d)", i + 1, p.lives);
      break;
    default: break;
  }
  plat_log("G%d (%s) prende la carta %s", i + 1, CHARS[p.ch].name, CARDS[c].name);
}

// --- riquadri (0.5.2: stile Orbita, in pixel a 640x480) ----------------------------------

const float PANEL_Y = 86.0f, PANEL_H = 330.0f, LINE = 17.0f;
const int MAX_LINES = 8;
struct Lines { char s[MAX_LINES][ui::WRAP_LEN]; int n; bool cut; };

// Divide s in righe larghe al massimo w pixel (faccia face): a capo sugli spazi e, in cinese e
// giapponese, tra un carattere e l'altro. cut: non ci sta in MAX_LINES righe.
void wrap(const char* s, float w, int face, Lines* out) { out->n = ui::wrap(s, w, face, 0.3f, out->s, MAX_LINES, &out->cut); }

// Nome da mostrare in un riquadro largo w: quello vero, oppure (riquadri stretti dei 4 giocatori)
// uno piu' corto.
const char* card_label(int c, float w, int face) {
  const char* full = tr(CARDS[c].name);
  if (hud_text_w(face, full, 0.8f) <= w) return full;
  switch (c) {
    case CA_GENEROSO: return tr("GENEROSO");
    case CA_COLPI_FUOCO: return tr("INFUOCATI");
    case CA_COLPI_GELO: return tr("GELATI");
    case CA_SCHEGGE_FUOCO: return tr("ROVENTI");
    case CA_SCHEGGE_GELO: return tr("GELATE");
    default: return full;
  }
}

const char* card_desc(int c, bool narrow) { return tr(narrow && CARDS[c].brief ? CARDS[c].brief : CARDS[c].desc); }

inline uint32_t O(uint32_t rgb) { return HUD_OPAQUE(rgb); }

struct PanelFit { float bottom; int desc_lines; bool name_cut, head_cut, cut; };

// Dorso di tre carte coperte a ventaglio (0.5.4), centrate in (cx, cy), larghe w: prima le due ai
// lati, poi quella in mezzo, piu' in alto, con la stella.
void card_backs(Hud* h, float cx, float cy, float w, float alpha = 1.0f) {
  const float hh = floorf(w * 1.4f);
  static const int ORDER[3] = { 0, 2, 1 };
  for (int j = 0; j < 3; ++j) {
    const int k = ORDER[j];
    const float x = floorf(cx + (float)(k - 1) * w * 0.42f - w * 0.5f), y = floorf(cy - hh * 0.5f + (k == 1 ? -5.0f : 0.0f));
    if (alpha >= 1.0f) {
      ui::card_sel(h, x, y, w, hh, 6.0f);
    } else {
      hud_box_grad(h, x, y, w, hh, 6.0f, hud_a(ui::CARD_A, alpha), hud_a(ui::CARD_B, alpha), 160.0f, 1.0f);
      hud_frame_grad(h, x, y, w, hh, 6.0f, 2.0f, hud_a(ui::L1, alpha), hud_a(ui::L2, alpha), 135.0f);
    }
  }
  const int icon = w >= 40.0f ? HI_STAR22 : HI_STAR16;
  const float iw = (float)hud_icon_w(icon), ih = (float)hud_icon_h(icon);
  hud_icon(h, icon, floorf(cx - iw * 0.5f), floorf(cy - 5.0f - ih * 0.5f), hud_a(ui::STAR, alpha));
}

// Riquadro di un giocatore: "G1 ASTRO", le tre carte, la descrizione di quella col cursore e in
// fondo le carte gia' prese; dopo la scelta "PRESA!". Con h NULL misura soltanto (prove).
PanelFit draw_panel(Hud* h, int i, int ch, const CardOffer& o, uint32_t owned, bool others, float px, float pw) {
  PanelFit fit = { 0.0f, 0, false, false, false };
  const bool narrow = pw < 180.0f;
  const float pad = narrow ? 12.0f : 14.0f;    // nei riquadri stretti (4 giocatori) un po' di posto in piu'
  const float py = PANEL_Y, inner = pw - 2.0f * pad, ix = px + pad, cx = px + pw * 0.5f;
  const int nface = narrow ? HF_S13 : HF_S15, dface = narrow ? HF_S12 : HF_S13;
  char buf[512];
  // In alto G1, la faccina e il nome dell'eroe: nei riquadri stretti piu' stretti e, se il nome non
  // ci sta lo stesso (マルツィオ), senza la faccina.
  const char* hero = tr(CHARS[ch].name);
  const float htrack = narrow ? 1.0f : 1.5f, gw = ui::gp_w(i), hw = hud_text_w(HF_B13, hero, htrack);
  const float fx = ix + gw + (narrow ? 6.0f : 8.0f);
  float hx = fx + 20.0f + (narrow ? 4.0f : 6.0f);
  const bool with_face = hx + hw <= px + pw - pad;
  if (!with_face) hx = ix + gw + 8.0f;
  fit.head_cut = hx + hw > px + pw - pad;
  if (h) {
    if (o.done) ui::panel(h, px, py, pw, PANEL_H);
    else ui::panel(h, px, py, pw, PANEL_H, 16.0f, ui::BASE, false);
    ui::gp(h, ix, py + 22.0f, i);
    if (with_face) ui::face(h, fx, py + 12.0f, 20, ch, 0);
    ui::text(h, hx, py + 22.0f, HF_B13, O(ui::TX), hero, htrack);
  }
  Lines L;
  if (!o.shown && !o.done) {
    // 0.5.4: carte ancora coperte finche' questo giocatore non preme A.
    const float cw = narrow ? 36.0f : 46.0f;
    if (h) card_backs(h, cx, py + 132.0f, cw);
    Lines W;
    W.n = ui::wrap(tr("per scegliere"), inner, HF_S13, 0.5f, W.s, 2, &W.cut);
    const float ky = py + 214.0f;
    if (h) ui::key(h, cx - 11.0f, ky, "A");
    for (int k = 0; k < W.n; ++k)
      if (h) ui::text(h, cx, ky + 26.0f + 15.0f * (float)k, HF_S13, O(ui::TX), W.s[k], 0.5f, 0);
    fit.cut = W.cut;
    fit.bottom = ky + 26.0f + 15.0f * (float)W.n;
    return fit;
  }
  if (o.done) {
    const int c = o.card[o.sel];
    if (h) ui::text(h, cx, py + 70.0f, HF_B22, O(ui::L1), tr("PRESA!"), 3.0f, 0);
    fit.name_cut |= hud_text_w(HF_B22, tr("PRESA!"), 3.0f) > inner;
    const char* name = card_label(c, inner, nface);
    fit.name_cut |= hud_text_w(nface, name, 0.8f) > inner;
    if (h) ui::text(h, cx, py + 104.0f, nface, O(CARDS[c].combo ? ui::A2 : ui::TX), name, 0.8f, 0);
    wrap(card_desc(c, narrow), inner, dface, &L);
    float y = py + 130.0f;
    for (int k = 0; k < L.n; ++k, y += LINE)
      if (h) ui::text(h, cx, y, dface, O(ui::MU), L.s[k], 0.3f, 0);
    fit.desc_lines = L.n;
    fit.cut = L.cut;
    fit.bottom = y;
    if (others) {
      // In fondo, lampeggiando: in una riga o (lingue piu' lunghe, riquadri stretti) in due.
      Lines W;
      W.n = ui::wrap(tr("aspetta gli altri"), inner, HF_S13, 0.5f, W.s, 2, &W.cut);
      const float wy = py + PANEL_H - 22.0f - 15.0f * (float)(W.n - 1);
      for (int k = 0; k < W.n; ++k)
        if (h && ((g.ui_tick / 20u) & 1u) == 0u) ui::text(h, cx, wy + 15.0f * (float)k, HF_S13, O(ui::DIS), W.s[k], 0.5f, 0);
      fit.cut |= W.cut || wy - 8.0f < y;
    }
    return fit;
  }
  for (int k = 0; k < o.n; ++k) {
    const int c = o.card[k];
    const bool sel = k == o.sel;
    const float y = py + 44.0f + 36.0f * (float)k;
    if (h && sel) ui::bar_sel(h, px + 8.0f, y, pw - 16.0f, 32.0f);
    const char* name = card_label(c, inner, nface);
    fit.name_cut |= hud_text_w(nface, name, 0.8f) > inner;
    const uint32_t col = O(sel ? ui::ON : (CARDS[c].combo ? ui::A2 : ui::TX));
    if (h) ui::text(h, ix, y + 16.0f, nface, col, name, 0.8f);
    // Etichetta COMBO a destra, se c'e' posto accanto al nome.
    const char* combo = tr("COMBO");
    const float tw = hud_text_w(HF_B10, combo, 1.0f) + 15.0f, nw = hud_text_w(nface, name, 0.8f);
    if (h && CARDS[c].combo && ix + nw + 8.0f + tw <= px + pw - pad)
      ui::tag(h, px + pw - pad, y + 16.0f, combo, sel ? ui::ON : ui::A2, HF_B10, 1.0f, 6.0f, 18.0f, 1);
  }
  // Descrizione della carta col cursore (le combo con la loro etichetta sopra).
  const int c = o.card[o.sel];
  float y = py + 44.0f + 36.0f * 3.0f + 14.0f;
  if (h) ui::sep(h, ix, y - 8.0f, inner);
  if (CARDS[c].combo) {
    if (h) ui::text(h, ix, y + 6.0f, HF_B11, O(ui::A2), tr("COMBO"), 1.5f);
    y += LINE;
  }
  wrap(card_desc(c, narrow), inner, dface, &L);
  for (int k = 0; k < L.n; ++k, y += LINE)
    if (h) ui::text(h, ix, y + 6.0f, dface, O(ui::MU), L.s[k], 0.3f);
  fit.desc_lines = L.n;
  fit.cut = L.cut;
  fit.bottom = y;
  // In fondo: le carte gia' prese in questa mappa (se i nomi non ci stanno, solo quante sono:
  // succede con molte zone, nelle mappe personalizzate).
  if (owned) {
    char names[384], tmp[384];
    int count = 0;
    names[0] = 0;
    for (int k = 1; k < NCARDS; ++k)
      if ((owned >> k) & 1u) {
        if (count) snprintf(tmp, sizeof(tmp), tr("%s, %s"), names, tr(CARDS[k].name));   // in cinese e giapponese con 、
        else snprintf(tmp, sizeof(tmp), "%s", tr(CARDS[k].name));
        memcpy(names, tmp, sizeof(names));
        count++;
      }
    snprintf(buf, sizeof(buf), tr("hai già: %s"), names);
    wrap(buf, inner, HF_S12, &L);
    float oy = py + PANEL_H - 16.0f - 15.0f * (float)(L.n - 1);
    if (L.cut || oy - 10.0f < y) {
      if (count == 1) snprintf(buf, sizeof(buf), "%s", tr("hai già 1 carta"));
      else snprintf(buf, sizeof(buf), tr("hai già %d carte"), count);
      wrap(buf, inner, HF_S12, &L);
      oy = py + PANEL_H - 16.0f - 15.0f * (float)(L.n - 1);
    }
    fit.cut |= L.cut || oy - 10.0f < y;
    for (int k = 0; k < L.n; ++k, oy += 15.0f)
      if (h) ui::text(h, ix, oy, HF_S12, O(ui::DIS), L.s[k], 0.3f);
    fit.bottom = py + PANEL_H - 8.0f;
  }
  return fit;
}

// Riquadri affiancati dentro il margine del televisore: 1 giocatore 400 pixel, 2 da 280, 3 da 184, 4 da 138.
void panel_geom(int n, int k, float* px, float* pw) {
  static const float PW[MAX_PLAYERS + 1] = { 0.0f, 400.0f, 280.0f, 184.0f, 138.0f };   // dentro i 576 del margine
  static const float GAP[MAX_PLAYERS + 1] = { 0.0f, 0.0f, 16.0f, 12.0f, 8.0f };
  const float total = PW[n] * (float)n + GAP[n] * (float)(n - 1);
  *pw = PW[n];
  *px = floorf((640.0f - total) * 0.5f) + (PW[n] + GAP[n]) * (float)k;
}

}  // namespace

// --- interfaccia ----------------------------------------------------------------------

void game_set_cards_enabled(bool on) { g_on = on; }
bool cards_enabled(void) { return g_on; }

void cards_map_start(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    p.cards = 0;
    p.shield = false;
  }
  memset(g.offer, 0, sizeof(g.offer));
  g.cards_open = false;
  g.cards_t = g.cards_due = 0;
}

void cards_offer(void) {
  g.cards_due = 0;
  if (!g_on || g.screen != SC_PLAY) return;
  bool any = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.pl[i].active || g.pl[i].out) {     // senza vite: niente carte, non rientra in questa mappa
      memset(&g.offer[i], 0, sizeof(CardOffer));
      continue;
    }
    make_offer(i);
    any |= g.offer[i].on;
  }
  if (!any) return;
  g.cards_open = true;
  g.cards_t = g.cards_age = 0;
  g.cards_seq++;
  snd_ui(SND_CARD_OPEN);
  plat_log("carte: si sceglie (mappa %d, zona %d)", g.map_idx + 1, (g.phase == PH_CLEAR ? g.zone + 1 : g.zone) + 1);
}

void cards_zone_cleared(void) {
  if (g_on) g.cards_due = CARDS_CLEAR_DELAY;
}

void cards_zone_start(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.pl[i].active && has_card(g.pl[i], CA_SCUDO)) g.pl[i].shield = true;
}

void cards_update(void) {
  bool all = true;
  // Rete di sicurezza: un pad spento non deve tenere fermo il gioco per sempre (la pausa qui non si apre).
  const bool late = ++g.cards_age >= CARDS_TIMEOUT;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    CardOffer& o = g.offer[i];
    if (!o.on || o.done) continue;
    if (!g.pl[i].active) {             // uscito (la pausa e' chiusa: non dovrebbe succedere)
      o.done = true;
      continue;
    }
    // 0.5.4: prima il banner "RITIRA LE TUE CARTE"; ognuno preme A (una pressione nuova, dopo
    // CARDS_BANNER_MIN fotogrammi) e vede le sue tre carte. Quella A non sceglie.
    if (!o.shown) {
      if (late || (g.cards_age > CARDS_BANNER_MIN && (pad_pressed(i, PAD_A) || g.start_tap[i]))) {
        o.shown = true;
        snd_ui(SND_CARD_OPEN);
      }
      if (!late) {                     // dopo un minuto (late) le vede e prende subito quella col cursore
        all = false;
        continue;
      }
    }
    if (g.in[i].nav == NAV_UP) { o.sel = (int8_t)((o.sel + o.n - 1) % o.n); snd_ui(SND_UI_MOVE); }
    if (g.in[i].nav == NAV_DOWN) { o.sel = (int8_t)((o.sel + 1) % o.n); snd_ui(SND_UI_MOVE); }
    if (pad_pressed(i, PAD_A) || g.start_tap[i] || late) {
      if (late) plat_log("G%d non ha scelto entro un minuto: prende quella col cursore", i + 1);
      o.done = true;
      snd_ui(SND_CARD_PICK);
      card_take(i, o.card[o.sel]);
    } else {
      all = false;
    }
  }
  if (!all) {
    g.cards_t = 0;
    return;
  }
  if (++g.cards_t < CARDS_CLOSE_FRAMES) return;
  g.cards_open = false;
  // Come dopo la pausa: i tasti ancora premuti (A, START) non contano come pressioni nuove.
  for (int j = 0; j < MAX_PLAYERS; ++j) g.pl[j].prev_btn = g.in[j].btn;
  plat_log("carte scelte: si riparte");
}

// 0.5.4: prima delle carte, finche' nessuno ha premuto A, un banner al centro con le carte coperte:
// "RITIRA LE TUE CARTE", sotto A per scegliere. Entra in un attimo, sopra il gioco fermo.
void cards_banner(Hud* h) {
  // Sopra il centro dello schermo, dove di solito c'e' il gruppo.
  const float a = fminf(1.0f, (float)(g.cards_age + 1) / 10.0f), cy = 160.0f;
  ui::overlay(h, 0.55f * a);
  hud_box_grad(h, 0, cy - 52.0f, 320, 104.0f, 0, hud_a(ui::SCRIM, 0.0f), hud_a(ui::SCRIM, 0.85f * a), 90.0f);
  hud_box_grad(h, 320, cy - 52.0f, 320, 104.0f, 0, hud_a(ui::SCRIM, 0.85f * a), hud_a(ui::SCRIM, 0.0f), 90.0f);
  card_backs(h, 320.0f, cy - 96.0f, 34.0f, a);
  char t[256];
  ui::fit(t, sizeof(t), tr("RITIRA LE TUE CARTE"), HF_T32, 3.0f, 560.0f, "...");
  ui::title(h, 320, cy - 34.0f, HF_T32, t, 3.0f, true, a);
  const char* sub = tr("per scegliere");
  const float tw = hud_text_w(HF_S15, sub, 0.5f), x = 320.0f - (22.0f + 8.0f + tw) * 0.5f;
  ui::key(h, x, cy + 30.0f, "A", ui::L1);
  ui::text(h, x + 30.0f, cy + 30.0f, HF_S15, hud_a(ui::TX, a), sub, 0.5f);
}

void cards_hud(Hud* h) {
  int who[MAX_PLAYERS], n = 0;
  bool waiting = false, opened = false;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.offer[i].on) {
      who[n++] = i;
      waiting |= !g.offer[i].done;
      opened |= g.offer[i].shown || g.offer[i].done;
    }
  if (!opened) {
    cards_banner(h);
    return;
  }
  ui::overlay(h, 0.72f);
  ui::title(h, 320, 20, HF_T32, tr("SCEGLI UNA CARTA"), 3.0f);
  ui::text(h, 320, 70, HF_S14, HUD_OPAQUE(ui::FTX), tr("vale fino alla fine della mappa"), 1.0f, 0);
  for (int k = 0; k < n; ++k) {
    const int i = who[k];
    float px, pw;
    panel_geom(n, k, &px, &pw);
    draw_panel(h, i, g.pl[i].ch, g.offer[i], g.pl[i].cards, waiting, px, pw);
  }
  const char* it[4] = { "↑↓", tr("sceglie"), "A", tr("prende") };
  ui::footer(h, it, 4);
}

void card_lose_all(int i) {
  Player& p = g.pl[i];
  p.cards = 0;
  p.shield = false;
}

uint8_t card_shot_fx(int i, int kind) {
  if (i < 0 || i >= MAX_PLAYERS) return 0;
  const Player& p = g.pl[i];
  uint8_t fx = 0;
  if (kind == SK_BULLET) {
    if (has_card(p, CA_COLPI_FUOCO)) fx |= SFX_BURN;
    if (has_card(p, CA_COLPI_GELO)) fx |= SFX_SLOW;
  } else if (kind == SK_SHARD) {
    if (has_card(p, CA_SCHEGGE_FUOCO)) fx |= SFX_BURN;
    if (has_card(p, CA_SCHEGGE_GELO)) fx |= SFX_SLOW;
  }
  return fx;
}

void card_shot_mods(int i, Shot* s) {
  const Player& p = g.pl[i];
  if (!p.cards) return;
  if (has_card(p, CA_PESANTI)) s->dmg *= 1.25f;
  if (s->kind != SK_FLAME) {
    if (has_card(p, CA_PERFORANTE)) s->pierce = 1;
    if (has_card(p, CA_RIMBALZO)) s->bounce = 1;
  }
  s->fx = card_shot_fx(i, s->kind);
}

void card_shot_hit(int k, const Shot& s) {
  Monster& m = g.mo[k];
  // VAPORE: prima si guarda com'era il mostro, poi si aggiunge quello che porta questo colpo.
  if (s.kind == SK_FLAME || (s.fx & SFX_BURN)) card_elem_hit(k, s.owner, EL_FIRE);
  if (m.alive && (s.kind == SK_FROST || (s.fx & SFX_SLOW))) card_elem_hit(k, s.owner, EL_SNOW);
  if (!m.alive) return;
  if (s.fx & SFX_BURN) {
    if (m.burn_t <= 0) puff(monster_center(m), PK_FLAME, PC_FIRE, v3(0, 1.5f, 0), 0.8f, 16);
    m.burn_t = BURN_FRAMES;            // si rinnova, non si somma
    m.burn_by = s.owner;
  }
  if (s.fx & SFX_SLOW) {
    if (m.slow_t <= 0 || m.slow_k >= ICE_SLOW) {    // non toglie un rallentamento piu' forte
      m.slow_k = ICE_SLOW;
      if (m.slow_t < ICE_FRAMES) m.slow_t = ICE_FRAMES;
    }
    puff(s.pos, PK_SNOW, PC_SNOW, v3(0, 0.6f, 0), 0.8f, 14);
  }
}

void card_elem_hit(int k, int owner, int elem) {
  Monster& m = g.mo[k];
  if (!m.alive || m.rise > 0 || m.vapor_t > 0) return;
  if (elem == EL_FIRE ? m.slow_t <= 0 : (elem != EL_SNOW || m.burn_t <= 0)) return;
  const int by = vapor_owner(owner);
  if (by < 0) return;
  // Il mostro rallentato che brucia scoppia in vapore: danni a chi gli sta intorno (lui compreso).
  m.vapor_t = VAPOR_CD;
  const Vec3 at = m.pos, c = monster_center(m);
  int hit = 0;
  for (int j = 0; j < MAX_MONSTERS; ++j) {
    Monster& o = g.mo[j];
    if (!o.alive || o.rise > 0 || dist2d(o.pos, at) - monster_r(o) > VAPOR_R) continue;
    Vec3 dir = v3(o.pos.x - at.x, 0.0f, o.pos.z - at.z);
    dir = length(dir) > 1e-3f ? normalize(dir) : v3(0, 0, 0);
    damage_monster(j, VAPOR_DMG, by, dir, 2.0f, false);
    hit++;
  }
  for (int q = 0; q < 10; ++q) {         // nuvola bianca che si allarga e sale
    const float a = D_TWO_PI * (float)q / 10.0f;
    puff(c, PK_PUFF, PC_WHITE, v3(d_cos(a) * 3.4f, 1.2f + 0.3f * (float)(q & 1), d_sin(a) * 3.4f), 2.4f, 28);
  }
  puff(c + v3(0, 0.3f, 0), PK_PUFF, PC_WHITE, v3(0, 2.6f, 0), 3.4f, 34);
  puff(c + v3(0, 0.1f, 0), PK_PUFF, PC_SNOW, v3(0, 1.6f, 0), 3.0f, 30);
  add_shake(0.06f);
  snd_at(SND_EXPLODE_SMALL, at);
  plat_log("vapore! un mostro scoppia (G%d): %d colpiti", by + 1, hit);
}

void card_burn_tick(int k) {
  Monster& m = g.mo[k];
  m.burn_t--;
  if (m.burn_t % 5 == 0) {             // fiammelle che salgono dal mostro
    const float a = (float)(g.tick + (unsigned)k * 7u) * 1.7f, r = monster_r(m) * 0.7f;
    puff(monster_center(m) + v3(d_cos(a) * r, 0.15f, d_sin(a) * r), PK_FLAME, PC_FIRE, v3(0, 1.6f, 0), 1.2f, 16);
  }
  if (m.burn_t % BURN_EVERY != 0) return;
  const int by = m.burn_by >= 0 && m.burn_by < MAX_PLAYERS && g.pl[m.burn_by].active ? m.burn_by : -1;
  damage_monster(k, BURN_DMG, by, v3(0, 0, 0), 0.0f, false);     // le bruciature non caricano lo speciale
  if (m.alive) card_elem_hit(k, by, EL_FIRE);
}

bool card_shield_hit(int i) {
  Player& p = g.pl[i];
  if (!p.shield) return false;
  p.shield = false;
  p.invuln = SHIELD_INVULN;
  p.sq_v -= 1.5f;
  const Vec3 c = player_chest(p);
  burst(c, 12, i, 3.5f);
  burst(c, 6, PC_WHITE, 2.5f);
  snd_at(SND_CAPSULE_HIT, p.pos);
  plat_log("giocatore %d: lo scudo ferma il colpo (mappa %d, zona %d)", i + 1, g.map_idx + 1, g.zone + 1);
  return true;
}

void card_special_used(int i) {
  const Player& p = g.pl[i];
  if (!has_card(p, CA_GENEROSO)) return;
  int n = 0;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (j == i || !q.active || !q.alive || q.hp >= q.hp_max || dist2d(q.pos, p.pos) > GENEROUS_R) continue;
    heal_player(j, GENEROUS_HP);
    burst(player_chest(q), 6, PC_GREEN, 2.0f);
    n++;
  }
  if (n) plat_log("giocatore %d: speciale generoso, %d compagni curati", i + 1, n);
}

// Prove: ogni carta, scelta o appena presa, sta nel suo riquadro con 1, 2, 3 e 4 giocatori, anche
// con cinque carte lunghe gia' prese; e il nome di ogni eroe in alto.
bool cards_layout_ok(char* why, int n, bool all) {
  why[0] = 0;
  bool good = true;
  // Carte gia' prese: i nomi piu' lunghi, fino a 5 (una mappa personalizzata ha al massimo 6 zone,
  // quindi al massimo 6 scelte).
  const uint32_t owned = (1u << CA_GENEROSO) | (1u << CA_SCHEGGE_GELO) | (1u << CA_SCHEGGE_FUOCO) | (1u << CA_COLPI_FUOCO) |
                         (1u << CA_RICARICA);
  for (int np = 1; np <= MAX_PLAYERS; ++np) {
    float px, pw;
    panel_geom(np, np - 1, &px, &pw);
    float x0, w0;
    panel_geom(np, 0, &x0, &w0);
    if (x0 < ui::SAFE_X || px + pw > 640.0f - ui::SAFE_X) {
      snprintf(why, (size_t)n, "%d giocatori: riquadri fuori dal margine del televisore", np);
      return false;
    }
    for (int ch = 0; ch < NCHARS; ++ch)        // ogni eroe: il nome in alto
      for (int c = 1; c < NCARDS; ++c)
        for (int st = 0; st < 3; ++st) {               // coperte, da scegliere, presa
          CardOffer o;
          memset(&o, 0, sizeof(o));
          o.card[0] = (uint8_t)c;
          o.card[1] = CA_GENEROSO;
          o.card[2] = CA_SCHEGGE_GELO;
          o.n = 3;
          o.on = true;
          o.shown = st >= 1;
          o.done = st == 2;
          const char* what = st == 0 ? " (coperte)" : (st == 2 ? " (presa)" : "");
          const PanelFit f = draw_panel(NULL, 3, ch, o, owned & ~(1u << c), true, px, pw);
          if (f.cut || f.name_cut || f.head_cut || f.desc_lines > 6 || f.bottom > PANEL_Y + PANEL_H) {
            const char* hc = f.head_cut ? ", nome dell'eroe tagliato" : "";
            if (good)
              snprintf(why, (size_t)n, "%d giocatori, %s, %s%s: %d righe, testo fino a %.1f%s%s%s", np, CHARS[ch].name, CARDS[c].name,
                       what, f.desc_lines, (double)f.bottom, f.name_cut ? ", nome tagliato" : "", hc, f.cut ? ", testo tagliato" : "");
            if (all)
              printf("    %d giocatori, %s, %s%s: %d righe, testo fino a %.1f%s%s%s\n", np, tr(CHARS[ch].name), tr(CARDS[c].name), what,
                     f.desc_lines, (double)f.bottom, f.name_cut ? ", nome tagliato" : "", hc, f.cut ? ", testo tagliato" : "");
            good = false;
            if (!all) return false;
          }
        }
  }
  return good;
}
