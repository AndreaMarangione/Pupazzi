#include "ui.h"
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui {

const uint32_t PLAYER[4] = { 0xE8463C, 0x3C82E8, 0x3FC04A, 0xF2C230 };

namespace {

inline uint32_t A(uint32_t rgb, float a) { return hud_a(rgb, a); }
inline uint32_t O(uint32_t rgb) { return HUD_OPAQUE(rgb); }
inline uint32_t clear_of(uint32_t argb) { return argb & 0xFFFFFFu; }   // stesso colore, trasparente

uint32_t mix(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int s = 0; s <= 16; s += 8) {
    const float ca = (float)((a >> s) & 255u), cb = (float)((b >> s) & 255u);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  }
  return out;
}

// Grigio con la stessa luminosita' (un po' schiacciata verso il mezzo), come le facce di chi e' fuori.
uint32_t gray(uint32_t c) {
  const unsigned r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
  const unsigned v = 60u + (r * 77u + g * 150u + b * 29u) / 256u * 100u / 255u;
  return (v << 16) | (v << 8) | v;
}

uint32_t alpha_mul(uint32_t argb, float a) {
  const float al = (float)(argb >> 24) * a;
  return ((uint32_t)(al + 0.5f) << 24) | (argb & 0xFFFFFFu);
}

const float SKEW = 0.2679f;   // tan(15 gradi): le tacche inclinate dei mockup (skewX(-15deg))

}  // namespace

void scrim_top(Hud* h, float hh) { hud_box_grad(h, 0, 0, 640, hh, 0, A(SCRIM, 0.7f), A(SCRIM, 0.0f), 180.0f); }
void scrim_bottom(Hud* h, float hh) { hud_box_grad(h, 0, 480.0f - hh, 640, hh, 0, A(SCRIM, 0.0f), A(SCRIM, 0.72f), 180.0f); }
void overlay(Hud* h, float a) { hud_box(h, 0, 0, 640, 480, 0, A(SCRIM, a)); }

const char* fit(char* dst, int n, const char* s, int face, float track, float w, const char* ell) {
  snprintf(dst, (size_t)n, "%s", s);
  if (hud_text_w(face, dst, track) <= w) return dst;
  const int el = (int)strlen(ell);
  int len = (int)strlen(dst);
  while (len > 0) {
    do { --len; } while (len > 0 && ((unsigned char)dst[len] & 0xC0u) == 0x80u);   // un carattere intero
    while (len > 0 && dst[len - 1] == ' ') --len;
    if (len + el + 1 > n) continue;
    memcpy(dst + len, ell, (size_t)el + 1);
    if (hud_text_w(face, dst, track) <= w) return dst;
  }
  snprintf(dst, (size_t)n, "%s", ell);
  return dst;
}

int wrap(const char* s, float w, int face, float track, char (*out)[WRAP_LEN], int max) {
  int n = 0;
  while (*s == ' ') ++s;
  while (*s && n < max) {
    int cut = 0;
    for (int e = 0;;) {
      while (s[e] == ' ') ++e;
      while (s[e] && s[e] != ' ') ++e;
      if (e >= WRAP_LEN - 1) break;
      char tmp[WRAP_LEN];
      memcpy(tmp, s, (size_t)e);
      tmp[e] = 0;
      if (cut > 0 && hud_text_w(face, tmp, track) > w) break;
      cut = e;
      if (!s[e]) break;
    }
    if (cut == 0) {                                  // una parola sola piu' lunga della riga
      while (cut < WRAP_LEN - 1 && s[cut] && s[cut] != ' ') ++cut;
      while (cut > 0 && ((unsigned char)s[cut] & 0xC0u) == 0x80u) --cut;
      if (cut == 0) cut = 1;
    }
    char line[WRAP_LEN];
    memcpy(line, s, (size_t)cut);
    line[cut] = 0;
    s += cut;
    while (*s == ' ') ++s;
    if (*s && n == max - 1) {                        // non ci sta tutto: l'ultima riga con i puntini
      char rest[2 * WRAP_LEN];
      snprintf(rest, sizeof(rest), "%s %s", line, s);
      fit(out[n], WRAP_LEN, rest, face, track, w);
      return n + 1;
    }
    fit(out[n], WRAP_LEN, line, face, track, w);
    ++n;
  }
  return n;
}

void text(Hud* h, float x, float cy, int face, uint32_t argb, const char* s, float track, int align) {
  hud_text(h, x, hud_mid(face, cy), face, argb, s, track, align);
}

void title(Hud* h, float cx, float top, int face, const char* s, float track, bool deco, float alpha) {
  const float size = (float)hud_face_size(face), cy = top + size * 0.5f, base = hud_mid(face, cy);
  const float tw = hud_text_w(face, s, track);
  float tx = cx - tw * 0.5f;
  if (deco) {
    const float total = tw + 2.0f * (6.0f + 12.0f + 34.0f + 12.0f);
    float x = cx - total * 0.5f;
    for (int side = 0; side < 2; ++side) {
      const float dot = side == 0 ? x : x + total - 6.0f;
      const float ln = side == 0 ? x + 18.0f : x + total - 18.0f - 34.0f;
      hud_ellipse(h, dot + 3.0f, cy, 3.0f, 3.0f, A(L1, 0.8f * alpha), 8.0f);
      hud_ellipse(h, dot + 3.0f, cy, 3.0f, 3.0f, A(L1, alpha));
      if (side == 0) hud_box_grad(h, ln, cy - 1.0f, 34.0f, 2.0f, 0, A(L1, 0.0f), A(L1, alpha), 90.0f);
      else hud_box_grad(h, ln, cy - 1.0f, 34.0f, 2.0f, 0, A(L1, alpha), A(L1, 0.0f), 90.0f);
    }
    tx = x + 64.0f;
  }
  hud_text(h, tx, base + 2.0f, face, A(TDROP, alpha), s, track);
  hud_text_glow(h, tx, base, face, alpha_mul(TGLOW, alpha), s, track);
  hud_text(h, tx, base, face, A(0xFFFFFF, alpha), s, track);
}

// Pannelli come nei mockup (fondo quasi opaco sopra il bordo sfumato, che traspare appena), ma in
// due passate sole: il fondo gia' mescolato con il bordo, opaco, che arriva un pixel sotto la
// cornice, poi la cornice. Lo stesso risultato senza disegnare due volte tutto l'interno.
void panel(Hud* h, float x, float y, float w, float hh, float cut, uint32_t fill, bool glow, float bw) {
  if (glow) hud_glow(h, x, y, w, hh, cut, 10.0f, GLOW);
  const float fa = (float)(fill >> 24) / 255.0f;
  hud_box_grad(h, x, y, w, hh, cut, O(mix(L1, fill, fa)), O(mix(L2, fill, fa)), 135.0f, bw - 1.0f);
  hud_frame_grad(h, x, y, w, hh, cut, bw, O(L1), O(L2), 135.0f);
}

void panel_off(Hud* h, float x, float y, float w, float hh, float cut, uint32_t border, uint32_t fill) {
  hud_box(h, x, y, w, hh, cut, O(mix(border, fill, (float)(fill >> 24) / 255.0f)), 0.5f);
  hud_frame(h, x, y, w, hh, cut, 1.5f, O(border));
}

void card_sel(Hud* h, float x, float y, float w, float hh, float cut) {
  hud_glow(h, x, y, w, hh, cut, 10.0f, GLOW);
  hud_box_grad(h, x, y, w, hh, cut, O(CARD_A), O(CARD_B), 160.0f, 1.0f);
  hud_frame_grad(h, x, y, w, hh, cut, 2.0f, O(L1), O(L2), 135.0f);
}

void bar_sel(Hud* h, float x, float y, float w, float hh, float cut, bool glow) {
  if (glow) hud_glow(h, x, y, w, hh, cut, 10.0f, GLOW);
  hud_box_grad(h, x, y, w, hh, cut, O(L1), O(L2), 90.0f);
}

void button(Hud* h, float cx, float y, float w, float hh, const char* s, int face, float track, bool sel) {
  const float x = cx - w * 0.5f;
  if (sel) {
    bar_sel(h, x, y, w, hh, 12.0f, true);
  } else {
    hud_box(h, x, y, w, hh, 12.0f, BASE);
    hud_frame(h, x, y, w, hh, 12.0f, 1.5f, O(DIM));
  }
  text(h, cx, y + hh * 0.5f, face, O(sel ? ON : BTX), s, track, 0);
}

void sep(Hud* h, float x, float y, float w) {
  hud_box_grad(h, x, y, w * 0.5f, 1.0f, 0, clear_of(SEPC), SEPC, 90.0f);
  hud_box_grad(h, x + w * 0.5f, y, w * 0.5f, 1.0f, 0, SEPC, clear_of(SEPC), 90.0f);
}

float key(Hud* h, float x, float cy, const char* s, uint32_t color) {
  hud_ring(h, x + 11.0f, cy, 11.0f, 11.0f, 1.5f, O(color));
  text(h, x + 11.0f, cy, HF_B12, O(color), s, 0.0f, 0);
  return 22.0f;
}

void footer(Hud* h, const char* const* items, int n) {
  const float cy = 480.0f - SAFE_Y - 11.0f;
  float x = SAFE_X;
  for (int i = 0; i + 1 < n; i += 2) {
    if (i > 0) x += 8.0f;
    x += key(h, x, cy, items[i]) + 7.0f;
    text(h, x, cy, HF_S14, O(FTX), items[i + 1], 0.5f);
    x += hud_text_w(HF_S14, items[i + 1], 0.5f) + 7.0f;
  }
}

void version(Hud* h) { text(h, 640.0f - SAFE_X, 480.0f - SAFE_Y - 11.0f, HF_S13, A(FTX, 0.75f), PUPAZZI_VERSION, 1.0f, 1); }

float tag(Hud* h, float x, float cy, const char* s, uint32_t color, int face, float track, float padx, float hh, int align) {
  const float w = hud_text_w(face, s, track) + 2.0f * padx + 3.0f;
  if (align == 0) x -= w * 0.5f;
  else if (align == 1) x -= w;
  hud_frame(h, x, cy - hh * 0.5f, w, hh, 0, 1.5f, O(color));
  text(h, x + w * 0.5f, cy, face, O(color), s, track, 0);
  return w;
}

float gp(Hud* h, float x, float cy, int player, int face) {
  char buf[4];
  snprintf(buf, sizeof(buf), "G%d", (player & 3) + 1);
  const float w = fmaxf(24.0f, hud_text_w(face, buf, 0.5f) + 8.0f);
  hud_box(h, x, cy - 9.0f, w, 18.0f, 4.0f, O(PLAYER[player & 3]));
  text(h, x + w * 0.5f, cy, face, O((player & 3) == 3 ? 0x06102E : 0xFFFFFF), buf, 0.5f, 0);
  return w;
}

float keycap(Hud* h, float x, float cy, const char* s, bool small) {
  const int face = small ? HF_B10 : HF_B12;
  const float track = small ? 1.0f : 0.5f, hh = small ? 22.0f : 24.0f;
  const float w = fmaxf(34.0f, hud_text_w(face, s, track) + 17.0f);
  hud_frame(h, x, cy - hh * 0.5f, w, hh, 5.0f, 1.5f, O(L1));
  text(h, x + w * 0.5f, cy, face, O(L1), s, track, 0);
  return w;
}

float stick(Hud* h, float x, float cy, const char* s) {
  hud_ring(h, x + 13.0f, cy, 13.0f, 13.0f, 1.5f, O(L1));
  hud_ellipse(h, x + 13.0f, cy, 6.5f, 6.5f, KCFILL);
  text(h, x + 13.0f, cy, HF_B11, O(L1), s, 0.0f, 0);
  return 26.0f;
}

void face(Hud* h, float x, float y, int px, int ch, int look, float a) {
  static const uint32_t RGB[4][3] = {
    { 0xE9EEF5, 0x24324A, 0x9ED8FF },   // Astro: casco, visiera, riflesso
    { 0xE4E0F4, 0xF59AB8, 0x2A2230 },   // Mochi: testa e orecchie, rosa, occhi e naso
    { 0xE8673C, 0xFFFFFF, 0x1E1A28 },   // Marzio: testa e antenne, occhio, pupilla e bocca
    { 0xFF7A2A, 0xFFE14D, 0x5A1A08 },   // Zolfo: fiamma, cuore della fiamma, occhi di brace
  };
  if (ch < 0 || ch > 3) return;
  const int base = px <= 20 ? HI_FACE20_0 : (px <= 24 ? HI_FACE24_0 : (px <= 30 ? HI_FACE30_0 : HI_FACE40_0));
  for (int l = 0; l < 3; ++l) {
    uint32_t c = RGB[ch][l];
    if (look == 1) c = mix(c, 0x161A22, 0.5f);
    else if (look == 2) c = gray(c);
    hud_icon(h, base + 3 * ch + l, x, y, A(c, a));
  }
}

void avatar(Hud* h, float x, float y, int ch, bool gray_) {
  const float a = gray_ ? 0.45f : 1.0f;
  hud_ellipse(h, x + 16.0f, y + 16.0f, 16.0f, 16.0f, A(gray_ ? gray(AVBG) : AVBG, a));
  hud_ring(h, x + 16.0f, y + 16.0f, 16.0f, 16.0f, 1.5f, A(gray_ ? gray(L1) : L1, a));
  face(h, x + 4.0f, y + 4.0f, 24, ch, gray_ ? 2 : 0, a);
}

void star(Hud* h, int icon, float x, float y, bool on, uint32_t off_color) {
  if (on) {
    hud_icon(h, icon, x, y, O(STAR));
  } else {
    // vuote: il contorno (alle misure piccole) o la stella spenta
    const int outline = icon == HI_STAR16 ? HI_STAR16O : icon;
    hud_icon(h, outline, x, y, (off_color >> 24) ? off_color : O(off_color));
  }
}

float stars3(Hud* h, float x, float cy, unsigned got, bool on_sel) {
  for (int k = 0; k < 3; ++k)
    star(h, HI_STAR16, x + 19.0f * (float)k, cy - 8.0f, ((got >> k) & 1u) != 0, on_sel ? A(ON, 0.45f) : O(DIM2));
  return 54.0f;
}

void segs(Hud* h, float x, float cy, int n, int filled, float w, float hh, uint32_t on, uint32_t off, bool glow) {
  for (int i = 0; i < n; ++i) {
    const float px = x + (float)i * (w + 2.0f);
    if (i < filled) {
      if (glow) {
        HudShape s;
        memset(&s, 0, sizeof(s));
        s.type = HS_PARA; s.x = px; s.y = cy - hh * 0.5f; s.w = w; s.h = hh; s.skew = SKEW; s.soft = 6.0f;
        s.c0 = s.c1 = A(on, 0.55f);
        hud_shape(h, s);
      }
      hud_para(h, px, cy - hh * 0.5f, w, hh, SKEW, O(on));
    } else {
      hud_para(h, px, cy - hh * 0.5f, w, hh, SKEW, O(off));
    }
  }
}

void toggle(Hud* h, float x, float cy, bool on) {
  const uint32_t c = on ? L1 : DIM2;
  hud_frame(h, x, cy - 9.0f, 34.0f, 18.0f, 5.0f, 1.5f, O(c));
  hud_box(h, on ? x + 19.5f : x + 4.5f, cy - 4.5f, 10.0f, 9.0f, 0, O(c));
}

void planet(Hud* h, float x, float y, int w, float a) {
  switch (w) {
    case 1: hud_icon(h, HI_PL_BODY, x, y, A(0xBFC5D2, a)); hud_icon(h, HI_PL_CRATERS, x, y, A(0x9097A8, a)); break;
    case 2: hud_icon(h, HI_PL_BODY, x, y, A(0xE5533D, a)); hud_icon(h, HI_PL_STREAK, x, y, A(0xB83A28, a)); break;
    case 3: hud_icon(h, HI_PL_BODY, x, y, A(0xF2C46B, a)); break;
    case 4: hud_icon(h, HI_PL_BODY, x, y, A(0xA89A8C, a)); hud_icon(h, HI_PL_SPOT, x, y, A(0x857868, a)); break;
    case 5: hud_icon(h, HI_PL_BODY, x, y, A(0xE8A15C, a)); hud_icon(h, HI_PL_BANDS, x, y, A(0xB8763A, a)); break;
    case 6: hud_icon(h, HI_SAT_BODY, x, y, A(0xE9D08A, a)); hud_icon(h, HI_SAT_RING, x, y, A(0xCDB06A, a)); break;
    case 7: hud_icon(h, HI_PL_BODY, x, y, A(0x7FD8E8, a)); break;
    case 8: hud_icon(h, HI_PL_BODY, x, y, A(0x4D74E8, a)); break;
    default: hud_icon(h, HI_PL_BODY, x, y, A(0x2F6FE0, a)); break;
  }
}

void earth(Hud* h, float x, float y) {
  hud_icon(h, HI_EARTH_SEA, x, y, O(0x2F6FE0));
  hud_icon(h, HI_EARTH_LAND, x, y, O(0x3FC04A));
  hud_icon(h, HI_EARTH_RING, x, y, A(L1, 0.7f));
}

void chev(Hud* h, float x, float y, bool right, uint32_t color) {
  hud_icon(h, right ? HI_CHEV_R : HI_CHEV_L, x, y, (color >> 24) ? color : O(color));
}

}  // namespace ui
