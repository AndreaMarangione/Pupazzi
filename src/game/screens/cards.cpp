// The card choice (0.4) on screen: first the banner, then everyone's three cards in their own
// panel, chosen with up/down and A (or a short START); the game stands still meanwhile. The rules of
// the cards (what is offered, what each card does) are in game/sim/cards.cpp.
#include "game/screens/screens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace screens;

namespace {

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
PanelFit draw_panel(const Game& g, Hud* h, int i, int ch, const CardOffer& o, uint32_t owned, bool others, float px, float pw) {
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

void cards_update(Game& g) {
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
      if (late || (g.cards_age > CARDS_BANNER_MIN && (pad_pressed(g, i, PAD_A) || g.start_tap[i]))) {
        o.shown = true;
        ev_sound_ui(g, SND_CARD_OPEN);
      }
      if (!late) {                     // dopo un minuto (late) le vede e prende subito quella col cursore
        all = false;
        continue;
      }
    }
    if (g.in[i].nav == NAV_UP) { o.sel = (int8_t)((o.sel + o.n - 1) % o.n); ev_sound_ui(g, SND_UI_MOVE); }
    if (g.in[i].nav == NAV_DOWN) { o.sel = (int8_t)((o.sel + 1) % o.n); ev_sound_ui(g, SND_UI_MOVE); }
    if (pad_pressed(g, i, PAD_A) || g.start_tap[i] || late) {
      if (late) plat_log("G%d non ha scelto entro un minuto: prende quella col cursore", i + 1);
      o.done = true;
      ev_sound_ui(g, SND_CARD_PICK);
      card_take(g, i, o.card[o.sel]);
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
static void cards_banner(const Game& g, Hud* h) {
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

void cards_hud(const Game& g, Hud* h) {
  int who[MAX_PLAYERS], n = 0;
  bool waiting = false, opened = false;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.offer[i].on) {
      who[n++] = i;
      waiting |= !g.offer[i].done;
      opened |= g.offer[i].shown || g.offer[i].done;
    }
  if (!opened) {
    cards_banner(g, h);
    return;
  }
  ui::overlay(h, 0.72f);
  ui::title(h, 320, 20, HF_T32, tr("SCEGLI UNA CARTA"), 3.0f);
  ui::text(h, 320, 70, HF_S14, HUD_OPAQUE(ui::FTX), tr("vale fino alla fine della mappa"), 1.0f, 0);
  for (int k = 0; k < n; ++k) {
    const int i = who[k];
    float px, pw;
    panel_geom(n, k, &px, &pw);
    draw_panel(g, h, i, g.pl[i].ch, g.offer[i], g.pl[i].cards, waiting, px, pw);
  }
  const char* it[4] = { "↑↓", tr("sceglie"), "A", tr("prende") };
  ui::footer(h, it, 4);
}

// Prove: ogni carta, scelta o appena presa, sta nel suo riquadro con 1, 2, 3 e 4 giocatori, anche
// con cinque carte lunghe gia' prese; e il nome di ogni eroe in alto.
bool cards_layout_ok(Game& g, char* why, int n, bool all) {
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
          const PanelFit f = draw_panel(g, NULL, 3, ch, o, owned & ~(1u << c), true, px, pw);
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
