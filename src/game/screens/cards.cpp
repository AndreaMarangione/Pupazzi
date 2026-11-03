// The card choice (0.4) on screen: first the banner, then everyone's three cards in their own
// panel, chosen with up/down and A (or a short START); the game stands still meanwhile. The rules of
// the cards (what is offered, what each card does) are in game/sim/cards.cpp.
#include "game/screens/screens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace screens;

namespace {

// --- panels (0.5.2: Orbita style, in pixels at 640x480) ----------------------------------

const float PANEL_Y = 86.0f, PANEL_H = 330.0f, LINE = 17.0f;
const int MAX_LINES = 8;
struct Lines { char s[MAX_LINES][ui::WRAP_LEN]; int n; bool cut; };

// Splits s into lines at most w pixels wide (font face face): breaking at spaces and, in Chinese
// and Japanese, between one character and the next. cut: it does not fit in MAX_LINES lines.
void wrap(const char* s, float w, int face, Lines* out) { out->n = ui::wrap(s, w, face, 0.3f, out->s, MAX_LINES, &out->cut); }

// Name to show in a panel w wide: the real one, or (narrow panels with 4 players) a shorter one.
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

// Backs of three face-down cards in a fan (0.5.4), centered at (cx, cy), w wide: first the two at
// the sides, then the middle one, higher up, with the star.
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

// Panel of a player: "G1 ASTRO", the three cards, the description of the one under the cursor and
// at the bottom the cards already taken; after the choice "PRESA!". h NULL: measures only (tests).
PanelFit draw_panel(const Game& g, Hud* h, int i, int ch, const CardOffer& o, uint32_t owned, bool others, float px, float pw) {
  PanelFit fit = { 0.0f, 0, false, false, false };
  const bool narrow = pw < 180.0f;
  const float pad = narrow ? 12.0f : 14.0f;    // in the narrow panels (4 players) a little more room
  const float py = PANEL_Y, inner = pw - 2.0f * pad, ix = px + pad, cx = px + pw * 0.5f;
  const int nface = narrow ? HF_S13 : HF_S15, dface = narrow ? HF_S12 : HF_S13;
  char buf[512];
  // At the top G1, the little face and the hero's name: in the narrow panels tighter and, if the
  // name still does not fit (マルツィオ), without the little face.
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
    // 0.5.4: cards still face down until this player presses A.
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
      // At the bottom, blinking: on one line or (longer languages, narrow panels) on two.
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
    // COMBO label on the right, if there is room next to the name.
    const char* combo = tr("COMBO");
    const float tw = hud_text_w(HF_B10, combo, 1.0f) + 15.0f, nw = hud_text_w(nface, name, 0.8f);
    if (h && CARDS[c].combo && ix + nw + 8.0f + tw <= px + pw - pad)
      ui::tag(h, px + pw - pad, y + 16.0f, combo, sel ? ui::ON : ui::A2, HF_B10, 1.0f, 6.0f, 18.0f, 1);
  }
  // Description of the card under the cursor (the combos with their label above).
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
  // At the bottom: the cards already taken in this map (if the names do not fit, only how many:
  // it happens with many zones, in custom maps).
  if (owned) {
    char names[384], tmp[384];
    int count = 0;
    names[0] = 0;
    for (int k = 1; k < NCARDS; ++k)
      if ((owned >> k) & 1u) {
        if (count) snprintf(tmp, sizeof(tmp), tr("%s, %s"), names, tr(CARDS[k].name));   // in Chinese and Japanese with 、
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

// Panels side by side inside the TV margin: 1 player 400 pixels, 2 of 280, 3 of 184, 4 of 138.
void panel_geom(int n, int k, float* px, float* pw) {
  static const float PW[MAX_PLAYERS + 1] = { 0.0f, 400.0f, 280.0f, 184.0f, 138.0f };   // within the 576 of the margin
  static const float GAP[MAX_PLAYERS + 1] = { 0.0f, 0.0f, 16.0f, 12.0f, 8.0f };
  const float total = PW[n] * (float)n + GAP[n] * (float)(n - 1);
  *pw = PW[n];
  *px = floorf((640.0f - total) * 0.5f) + (PW[n] + GAP[n]) * (float)k;
}

}  // namespace

void cards_update(Game& g) {
  bool all = true;
  // Safety net: a pad that is off must not keep the game frozen forever (the pause does not open here).
  const bool late = ++g.cards_age >= CARDS_TIMEOUT;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    CardOffer& o = g.offer[i];
    if (!o.on || o.done) continue;
    if (!g.pl[i].active) {             // left (the pause is closed: it should not happen)
      o.done = true;
      continue;
    }
    // 0.5.4: first the banner "RITIRA LE TUE CARTE"; each player presses A (a new press, after
    // CARDS_BANNER_MIN frames) and sees their three cards. That A does not choose.
    if (!o.shown) {
      if (late || (g.cards_age > CARDS_BANNER_MIN && (pad_pressed(g, i, PAD_A) || g.start_tap[i]))) {
        o.shown = true;
        ev_sound_ui(g, SND_CARD_OPEN);
      }
      if (!late) {                     // after a minute (late): sees them, takes the one under the cursor
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
  // As after the pause: the buttons still pressed (A, START) do not count as new presses.
  for (int j = 0; j < MAX_PLAYERS; ++j) g.pl[j].prev_btn = g.in[j].btn;
  plat_log("carte scelte: si riparte");
}

// 0.5.4: before the cards, until someone has pressed A, a banner in the center with the face-down
// cards: "RITIRA LE TUE CARTE", below it A to choose. It comes in quickly, over the frozen game.
static void cards_banner(const Game& g, Hud* h) {
  // Above the center of the screen, where the group usually is.
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

// Tests: every card, being chosen or just taken, fits in its panel with 1, 2, 3 and 4 players,
// also with five long cards already taken; and the name of every hero at the top.
bool cards_layout_ok(Game& g, char* why, int n, bool all) {
  why[0] = 0;
  bool good = true;
  // Cards already taken: the longest names, up to 5 (a custom map has at most 6 zones, so at
  // most 6 choices).
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
    for (int ch = 0; ch < NCHARS; ++ch)        // every hero: the name at the top
      for (int c = 1; c < NCARDS; ++c)
        for (int st = 0; st < 3; ++st) {               // face down, to choose, taken
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
