#include "hud.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

HudCmd* push(Hud* h) { return h->n < HUD_MAX_CMDS ? &h->cmd[h->n++] : 0; }

int16_t px16(float v) {
  const float r = floorf(v + 0.5f);
  return (int16_t)(r < -32000.0f ? -32000.0f : (r > 32000.0f ? 32000.0f : r));
}

// UTF-8 -> posto nelle tabelle delle facce (0..255): Latin-1 cosi' com'e', qualche simbolo da 0x80
// (vedi tools/make_font.py). -1: carattere che non si sa disegnare; -2: fine del testo.
int next_slot(const char*& s) {
  const unsigned char c = (unsigned char)*s;
  if (!c) return -2;
  if (c < 0x80) {
    ++s;
    return c == 0x7F ? -1 : c;
  }
  unsigned cp;
  int n;
  if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; n = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; n = 2; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; n = 3; }
  else { ++s; return -1; }
  ++s;
  for (int i = 0; i < n; ++i) {
    const unsigned char d = (unsigned char)*s;
    if ((d & 0xC0) != 0x80) return -1;
    cp = (cp << 6) | (d & 0x3Fu);
    ++s;
  }
  if (cp >= 0xA0 && cp <= 0xFF) return (int)cp;
  switch (cp) {
    case 0x2192: return 0x80;
    case 0x2190: return 0x81;
    case 0x2191: return 0x82;
    case 0x2193: return 0x83;
    case 0x2019: return 0x84;
    case 0x2026: return 0x85;
    case 0x2022: return 0x86;
    default: return -1;
  }
}

// Carattere della faccia per il posto: nelle facce dei titoli (solo maiuscole) le minuscole
// diventano maiuscole; cio' che manca diventa '?'.
int glyph_of(int face, int sl) {
  const uint16_t* map = HUD_FACE_MAP[face];
  if (map[sl] != 0xFFFF) return map[sl];
  int up = -1;
  if (sl >= 'a' && sl <= 'z') up = sl - 32;
  else if (sl >= 0xE0 && sl <= 0xFE && sl != 0xF7) up = sl - 32;
  if (up >= 0 && map[up] != 0xFFFF) return map[up];
  return map['?'] != 0xFFFF ? map['?'] : -1;
}

int kern16(int face, int a, int b) {
  const HudFace& f = HUD_FACES[face];
  int lo = f.kern_first, hi = f.kern_first + f.kern_n - 1;
  const int key = (a << 8) | b;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const int k = (HUD_KERN[mid].a << 8) | HUD_KERN[mid].b;
    if (k == key) return HUD_KERN[mid].k16;
    if (k < key) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

// Scorre il testo: fn(carattere, penna) per ognuno; restituisce la larghezza.
template <class F>
float layout(int face, const char* s, float track, F fn) {
  float pen = 0.0f;
  int prev = -1;
  for (const char* p = s;;) {
    const int sl = next_slot(p);
    if (sl == -2) break;
    if (sl < 0) continue;
    const int g = glyph_of(face, sl);
    if (g < 0) continue;
    if (prev >= 0) pen += track + (float)kern16(face, prev, sl) * (1.0f / 16.0f);
    fn(g, pen);
    pen += (float)HUD_GLYPHS[g].adv16 * (1.0f / 16.0f);
    prev = sl;
  }
  return pen;
}

void push_glyph(Hud* h, int g, float x, float y, uint32_t argb) {
  const HudGlyph& G = HUD_GLYPHS[g];
  if (!G.w || !G.h) return;
  HudCmd* c = push(h);
  if (!c) return;
  c->x = px16(x + (float)G.ox);
  c->y = px16(y + (float)G.oy);
  c->w = G.w;
  c->h = G.h;
  c->argb = argb;
  c->idx = (uint16_t)g;
  c->kind = HUD_GLYPH;
  c->pad = 0;
}

void text_impl(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track, int align, bool glow) {
  if (face < 0 || face >= HUD_NFACES || !s) return;
  if (glow && !HUD_FACES[face].glow) return;
  if (align != -1) x -= hud_text_w(face, s, track) * (align == 0 ? 0.5f : 1.0f);
  // La penna si arrotonda al pixel per ogni carattere: con il filtro lineare della GPU i caratteri
  // restano uguali a quelli del renderer software.
  layout(face, s, track, [&](int g, float pen) {
    const float px = floorf(x + pen + 0.5f);
    if (glow) {
      if (HUD_GLYPHS[g].glow != 0xFFFF) push_glyph(h, HUD_GLYPHS[g].glow, px, base, argb);
    } else {
      push_glyph(h, g, px, base, argb);
    }
  });
}

// src su dst con l'opacita' a (0..255): round((src*a + dst*(255-a)) / 255) esatto per ogni canale,
// senza divisioni (rosso e blu insieme nella stessa parola, come in "Three wrongs make a right").
inline uint32_t blend(uint32_t dst, uint32_t src, unsigned a) {
  const unsigned ia = 255u - a;
  uint32_t rb = (src & 0xFF00FFu) * a + (dst & 0xFF00FFu) * ia + 0x800080u;
  rb = ((rb + ((rb >> 8) & 0xFF00FFu)) >> 8) & 0xFF00FFu;
  uint32_t g = ((src >> 8) & 255u) * a + ((dst >> 8) & 255u) * ia + 128u;
  g = (g + (g >> 8)) >> 8;
  return 0xFF000000u | rb | (g << 8);
}

// Da pixel a 640x480 a pixel d'uscita con scala k (per difetto, anche per i negativi).
inline int to_px(int v, int k) {
  const int p = v * k;
  return p >= 0 ? p / 2 : -((-p + 1) / 2);
}

float ellipse_sdf(float qx, float qy, float rx, float ry) {
  if (rx <= 0.0f || ry <= 0.0f) return 1e9f;
  const float kx = qx / rx, ky = qy / ry;
  const float k0 = sqrtf(kx * kx + ky * ky);
  const float ux = kx / rx, uy = ky / ry;
  const float k1 = sqrtf(ux * ux + uy * uy);
  return k1 > 1e-5f ? k0 * (k0 - 1.0f) / k1 : -fminf(rx, ry);
}

inline float mx(float a, float b) { return a > b ? a : b; }   // come fmaxf senza NaN, ma in linea

// La distanza dal bordo (vedi hud_shape_sdf): qui per poterla mettere in linea nei cicli.
inline float shape_sdf(const HudShape& sh, float s, float px, float py) {
  const float hx = sh.w * 0.5f * s, hy = sh.h * 0.5f * s;
  float qx = px - (sh.x * s + hx), qy = py - (sh.y * s + hy);
  float d;
  if (sh.type == HS_ELLIPSE) {
    d = ellipse_sdf(qx, qy, hx, hy);
  } else if (sh.type == HS_RING) {
    const float t = sh.ring * s;
    d = fabsf(ellipse_sdf(qx, qy, hx - 0.5f * t, hy - 0.5f * t)) - 0.5f * t;
  } else {
    if (sh.type == HS_PARA) qx += sh.skew * qy;
    d = mx(fabsf(qx) - hx, fabsf(qy) - hy);
    const float c = sh.cut * s;
    if (c > 0.0f) {
      const float tl = (c - (qx + hx) - (qy + hy)) * 0.70710678f;
      const float br = (c - (hx - qx) - (hy - qy)) * 0.70710678f;
      d = mx(d, mx(tl, br));
    }
    const float r = sh.ring * s;          // contorno largo r dentro il bordo (cornici, etichette)
    if (r > 0.0f) d = fabsf(d + 0.5f * r) - 0.5f * r;
  }
  return d + sh.inset * s;
}

uint8_t g_atlas[HUD_ATLAS_W * HUD_ATLAS_H];
bool g_atlas_ok;

// Alone di un carattere: il carattere al centro del riquadro (margine HUD_GLOW_PAD), sfocato con
// tre passate di media su 9 pixel in orizzontale e tre in verticale (quasi una gaussiana, sigma 4,5),
// poi rafforzato di 8/5. Solo aritmetica intera: uguale su ogni macchina.
void make_glow(const HudGlyph& w, const HudGlyph& src) {
  static uint16_t a[256 * 256], b[256 * 256];
  const int W = w.w, H = w.h, P = HUD_GLOW_PAD;
  memset(a, 0, sizeof(uint16_t) * (size_t)(W * H));
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) a[(y + P) * W + x + P] = g_atlas[(src.y + y) * HUD_ATLAS_W + src.x + x];
  for (int pass = 0; pass < 6; ++pass) {
    const bool horiz = pass < 3;
    const int n = horiz ? W : H, lines = horiz ? H : W;
    for (int l = 0; l < lines; ++l) {
      unsigned sum = 0;
      auto at = [&](int i) -> unsigned { return (i < 0 || i >= n) ? 0u : (horiz ? a[l * W + i] : a[i * W + l]); };
      for (int i = -4; i <= 4; ++i) sum += at(i);
      for (int i = 0; i < n; ++i) {
        const uint16_t v = (uint16_t)((sum + 4u) / 9u);
        if (horiz) b[l * W + i] = v;
        else b[i * W + l] = v;
        sum += at(i + 5);
        sum -= at(i - 4);
      }
    }
    memcpy(a, b, sizeof(uint16_t) * (size_t)(W * H));
  }
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const unsigned v = (unsigned)a[y * W + x] * 8u / 5u;
      g_atlas[(w.y + y) * HUD_ATLAS_W + w.x + x] = (uint8_t)(v > 255u ? 255u : v);
    }
}

void build_atlas(void) {
  memset(g_atlas, 0, sizeof(g_atlas));
  for (int i = 0; i < HUD_NGLYPHS; ++i) {
    const HudGlyph& g = HUD_GLYPHS[i];
    if (!g.w || g.data == 0xFFFFFFFFu) continue;
    const uint8_t* src = HUD_NIB + g.data;
    int k = 0;
    for (int y = 0; y < g.h; ++y)
      for (int x = 0; x < g.w; ++x, ++k) {
        const uint8_t v = src[k >> 1];
        g_atlas[(g.y + y) * HUD_ATLAS_W + g.x + x] = (uint8_t)(((k & 1) ? (v >> 4) : (v & 15u)) * 17u);
      }
  }
  for (int i = 0; i < HUD_NGLYPHS; ++i) {
    const HudGlyph& g = HUD_GLYPHS[i];
    if (g.w && g.data == 0xFFFFFFFFu && g.glow != 0xFFFF) make_glow(g, HUD_GLYPHS[g.glow]);
  }
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) g_atlas[(HUD_SOLID_Y + y) * HUD_ATLAS_W + HUD_SOLID_X + x] = 255;
  g_atlas_ok = true;
}

// Copertura morbida (aloni): 1 / (1 + exp(3.404 t)) con t = d / soft, in tabella da -2 a 2 (passo
// 1/64, interpolata). Fuori da li', una volta moltiplicata per l'opacita' (al massimo 255) e
// arrotondata, vale proprio 1 o 0: lo stesso risultato di expf.
float g_soft_lut[257];
bool g_soft_ok;

inline float soft_cover(float t) {
  if (t <= -2.0f) return 1.0f;
  if (t >= 2.0f) return 0.0f;
  const float f = (t + 2.0f) * 64.0f;
  const int i = (int)f;
  if (i >= 256) return g_soft_lut[256];   // t appena sotto 2: (t + 2) * 64 puo' arrotondare a 256
  const float r = f - (float)i;
  return g_soft_lut[i] + (g_soft_lut[i + 1] - g_soft_lut[i]) * r;
}

// Forma nel framebuffer software: per ogni pixel del riquadro la distanza dal bordo e la sfumatura.
// Per i rettangoli e i parallelogrammi la distanza cambia al massimo di 1 da un pixel al vicino:
// dove la forma e' piena (o vuota) per n pixel di fila, quei pixel si fanno senza ricalcolarla.
void draw_shape(const HudShape& sh, uint32_t* fb, int W, int H, int k, const HudCmd& c, int ry0, int ry1) {
  const int x0 = to_px(c.x, k), y0 = to_px(c.y, k);
  const int x1 = to_px(c.x + c.w, k) + 1, y1 = to_px(c.y + c.h, k) + 1;
  const int xa = x0 < 0 ? 0 : x0, ya = y0 < ry0 ? ry0 : y0;
  const int xb = x1 > W ? W : x1, yb = y1 > (ry1 < H ? ry1 : H) ? (ry1 < H ? ry1 : H) : y1;
  if (xa >= xb || ya >= yb) return;
  const float s = (float)k * 0.5f, soft = sh.soft * s, isoft = soft > 0.0f ? 1.0f / soft : 0.0f;
  const float cx = (sh.x + sh.w * 0.5f) * s, cy = (sh.y + sh.h * 0.5f) * s;
  const bool runs = sh.type == HS_BOX || sh.type == HS_PARA;
  const float edge = soft > 0.0f ? 2.0f * soft : 0.5f;   // oltre questa distanza: pieno o vuoto
  float ca[4], cb[4];
  for (int i = 0; i < 4; ++i) {
    ca[i] = (float)((sh.c0 >> (8 * i)) & 255u);
    cb[i] = (float)((sh.c1 >> (8 * i)) & 255u);
  }
  const bool grad = sh.c0 != sh.c1;
  const uint32_t flat_rgb = sh.c0 & 0xFFFFFFu;
  const unsigned flat_a = sh.c0 >> 24;
  float dx = 0.0f, dy = 0.0f, inv = 0.0f;
  if (grad) {
    const float an = sh.ang * 0.017453292f;
    dx = sinf(an);
    dy = -cosf(an);
    const float L = fabsf(sh.w * s * dx) + fabsf(sh.h * s * dy);
    inv = L > 1e-3f ? 1.0f / L : 0.0f;
  }
  for (int y = ya; y < yb; ++y) {
    const float py = (float)y + 0.5f;
    uint32_t* row = fb + (long)y * W;
    for (int x = xa; x < xb;) {
      const float d = shape_sdf(sh, s, (float)x + 0.5f, py);
      float cov;
      int run = 1;
      if (d >= edge) {
        if (runs) run = (int)(d - edge) + 1;
        x += run;
        continue;
      }
      if (d <= -edge) {
        cov = 1.0f;
        if (runs) run = (int)(-edge - d) + 1;
        if (run > xb - x) run = xb - x;
        if (sh.hollow) {
          x += run;
          continue;
        }
      } else {
        cov = soft > 0.0f ? soft_cover(d * isoft) : 0.5f - d;
      }
      if (cov >= 1.0f && run >= 4) {
        // Tratto pieno: tinta unita (sfondo per l'opacita' gia' moltiplicato) o sfumatura
        // in virgola fissa (dentro la forma la sfumatura non esce da 0..1).
        const int xe = x + run;
        if (!grad) {
          if (flat_a >= 255u) {
            const uint32_t v = 0xFF000000u | flat_rgb;
            for (; x < xe; ++x) row[x] = v;
          } else if (flat_a) {
            const unsigned ia = 255u - flat_a;
            const uint32_t srb = (flat_rgb & 0xFF00FFu) * flat_a + 0x800080u, sg = ((flat_rgb >> 8) & 255u) * flat_a + 128u;
            for (; x < xe; ++x) {
              const uint32_t d0 = row[x];
              uint32_t rb = srb + (d0 & 0xFF00FFu) * ia;
              rb = ((rb + ((rb >> 8) & 0xFF00FFu)) >> 8) & 0xFF00FFu;
              uint32_t g8 = sg + ((d0 >> 8) & 255u) * ia;
              g8 = (g8 + (g8 >> 8)) >> 8;
              row[x] = 0xFF000000u | rb | (g8 << 8);
            }
          }
          x = xe;
          continue;
        }
        const float t0 = (((float)x + 0.5f - cx) * dx + (py - cy) * dy) * inv + 0.5f, tdx = dx * inv;
        const float t1 = t0 + tdx * (float)(run - 1);
        if (t0 >= 0.0f && t0 <= 1.0f && t1 >= 0.0f && t1 <= 1.0f) {
          int32_t C[4], dC[4];
          for (int i = 0; i < 4; ++i) {
            C[i] = (int32_t)((ca[i] + (cb[i] - ca[i]) * t0 + 0.5f) * 65536.0f);
            dC[i] = (int32_t)((cb[i] - ca[i]) * tdx * 65536.0f);
          }
          for (; x < xe; ++x) {
            const unsigned a = (unsigned)(C[3] >> 16);
            const uint32_t rgb = ((uint32_t)(C[2] >> 16) << 16) | ((uint32_t)(C[1] >> 16) << 8) | (uint32_t)(C[0] >> 16);
            if (a >= 255u) row[x] = 0xFF000000u | rgb;
            else if (a) row[x] = blend(row[x], rgb, a);
            for (int i = 0; i < 4; ++i) C[i] += dC[i];
          }
          continue;
        }
      }
      for (const int xe = x + run; x < xe; ++x) {
        uint32_t rgb;
        unsigned a;
        if (grad) {
          float t = (((float)x + 0.5f - cx) * dx + (py - cy) * dy) * inv + 0.5f;
          t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
          float col[4];
          for (int i = 0; i < 4; ++i) col[i] = ca[i] + (cb[i] - ca[i]) * t;
          a = (unsigned)(col[3] * cov + 0.5f);
          rgb = ((uint32_t)(col[2] + 0.5f) << 16) | ((uint32_t)(col[1] + 0.5f) << 8) | (uint32_t)(col[0] + 0.5f);
        } else {
          a = cov >= 1.0f ? flat_a : (unsigned)((float)flat_a * cov + 0.5f);
          rgb = flat_rgb;
        }
        if (!a) continue;
        row[x] = a >= 255u ? (0xFF000000u | rgb) : blend(row[x], rgb, a);
      }
    }
  }
}

}  // namespace

// --- comandi --------------------------------------------------------------------------------

void hud_reset(Hud* h) { h->n = h->ns = 0; }

void hud_rect(Hud* h, float x, float y, float w, float hh, uint32_t rgb) {
  HudCmd* c = push(h);
  if (!c) return;
  c->x = px16(x); c->y = px16(y); c->w = px16(x + w) - c->x; c->h = px16(y + hh) - c->y;
  c->argb = HUD_OPAQUE(rgb); c->idx = 0; c->kind = HUD_RECT; c->pad = 0;
}

void hud_dim(Hud* h, float x, float y, float w, float hh) {
  HudCmd* c = push(h);
  if (!c) return;
  c->x = px16(x); c->y = px16(y); c->w = px16(x + w) - c->x; c->h = px16(y + hh) - c->y;
  c->argb = 0; c->idx = 0; c->kind = HUD_DIM; c->pad = 0;
}

float hud_text_w(int face, const char* s, float track) {
  if (face < 0 || face >= HUD_NFACES || !s) return 0.0f;
  return layout(face, s, track, [](int, float) {});
}

void hud_text(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track, int align) {
  text_impl(h, x, base, face, argb, s, track, align, false);
}

void hud_text_glow(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track, int align) {
  text_impl(h, x, base, face, argb, s, track, align, true);
}

void hud_printf(Hud* h, float x, float base, int face, uint32_t argb, const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  hud_text(h, x, base, face, argb, buf);
}

int hud_face_size(int face) { return face >= 0 && face < HUD_NFACES ? HUD_FACES[face].size : 0; }
int hud_face_asc(int face) { return face >= 0 && face < HUD_NFACES ? HUD_FACES[face].asc : 0; }
int hud_face_desc(int face) { return face >= 0 && face < HUD_NFACES ? HUD_FACES[face].desc : 0; }
float hud_mid(int face, float cy) { return cy + (float)(hud_face_asc(face) - hud_face_desc(face)) * 0.5f; }

void hud_upper(char* dst, const char* src, int n) {
  int k = 0;
  for (int i = 0; src[i] && k < n - 1; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
    else if (c == 0xC3 && k < n - 2 && src[i + 1]) {
      // U+00E0..U+00FE (meno la divisione U+00F7): la maiuscola e' 0x20 prima.
      unsigned char d = (unsigned char)src[i + 1];
      if (d >= 0xA0 && d <= 0xBE && d != 0xB7) d = (unsigned char)(d - 0x20);
      dst[k++] = (char)c;
      dst[k++] = (char)d;
      ++i;
      continue;
    }
    dst[k++] = (char)c;
  }
  dst[k] = 0;
}

void hud_icon(Hud* h, int icon, float x, float y, uint32_t argb) {
  if (icon < 0 || icon >= HUD_NGLYPHS) return;
  push_glyph(h, icon, floorf(x + 0.5f), floorf(y + 0.5f), argb);
}
int hud_icon_w(int icon) { return icon >= 0 && icon < HUD_NGLYPHS ? HUD_GLYPHS[icon].w : 0; }
int hud_icon_h(int icon) { return icon >= 0 && icon < HUD_NGLYPHS ? HUD_GLYPHS[icon].h : 0; }

void hud_shape(Hud* h, const HudShape& s) {
  if (h->ns >= HUD_MAX_SHAPES || s.w <= 0.0f || s.h <= 0.0f) return;
  HudCmd* c = push(h);
  if (!c) return;
  const float m = s.soft > 0.0f ? s.soft * 1.5f + 1.0f : 1.0f;
  const float mx = m + (s.type == HS_PARA ? fabsf(s.skew) * s.h * 0.5f : 0.0f);   // gli angoli inclinati sporgono
  const float x0 = floorf(s.x - mx), y0 = floorf(s.y - m), x1 = ceilf(s.x + s.w + mx), y1 = ceilf(s.y + s.h + m);
  c->x = px16(x0); c->y = px16(y0); c->w = px16(x1 - x0); c->h = px16(y1 - y0);
  c->argb = s.c0; c->idx = (uint16_t)h->ns; c->kind = HUD_SHAPE; c->pad = 0;
  h->shape[h->ns++] = s;
}

static HudShape mk(uint8_t type, float x, float y, float w, float hh, uint32_t c0, uint32_t c1, float ang) {
  HudShape s;
  memset(&s, 0, sizeof(s));
  s.type = type; s.x = x; s.y = y; s.w = w; s.h = hh; s.c0 = c0; s.c1 = c1; s.ang = ang;
  return s;
}

void hud_box(Hud* h, float x, float y, float w, float hh, float cut, uint32_t argb, float inset) {
  HudShape s = mk(HS_BOX, x, y, w, hh, argb, argb, 0.0f);
  s.cut = cut;
  s.inset = inset;
  hud_shape(h, s);
}

void hud_box_grad(Hud* h, float x, float y, float w, float hh, float cut, uint32_t c0, uint32_t c1, float ang, float inset) {
  HudShape s = mk(HS_BOX, x, y, w, hh, c0, c1, ang);
  s.cut = cut;
  s.inset = inset;
  hud_shape(h, s);
}

// L'alone sta sempre sotto una forma opaca uguale: dentro non serve disegnarlo.
void hud_glow(Hud* h, float x, float y, float w, float hh, float cut, float soft, uint32_t argb) {
  HudShape s = mk(HS_BOX, x, y, w, hh, argb, argb, 0.0f);
  s.cut = cut;
  s.soft = soft;
  s.hollow = 1;
  hud_shape(h, s);
}

void hud_ellipse(Hud* h, float cx, float cy, float rx, float ry, uint32_t argb, float soft) {
  HudShape s = mk(HS_ELLIPSE, cx - rx, cy - ry, 2.0f * rx, 2.0f * ry, argb, argb, 0.0f);
  s.soft = soft;
  hud_shape(h, s);
}

void hud_ring(Hud* h, float cx, float cy, float rx, float ry, float t, uint32_t argb) {
  HudShape s = mk(HS_RING, cx - rx, cy - ry, 2.0f * rx, 2.0f * ry, argb, argb, 0.0f);
  s.ring = t;
  hud_shape(h, s);
}

void hud_frame(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t argb) {
  HudShape s = mk(HS_BOX, x, y, w, hh, argb, argb, 0.0f);
  s.cut = cut;
  s.ring = t;
  hud_shape(h, s);
}

void hud_frame_grad(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t c0, uint32_t c1, float ang) {
  HudShape s = mk(HS_BOX, x, y, w, hh, c0, c1, ang);
  s.cut = cut;
  s.ring = t;
  hud_shape(h, s);
}

void hud_para(Hud* h, float x, float y, float w, float hh, float skew, uint32_t argb) {
  HudShape s = mk(HS_PARA, x, y, w, hh, argb, argb, 0.0f);
  s.skew = skew;
  hud_shape(h, s);
}

float hud_shape_sdf(const HudShape& sh, float s, float px, float py) { return shape_sdf(sh, s, px, py); }

float hud_shape_cover(float d, float soft_px) {
  if (soft_px > 0.0f) return 1.0f / (1.0f + expf(3.404f * d / soft_px));
  const float a = 0.5f - d;
  return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// --- renderer software ------------------------------------------------------------------------

void hud_prepare(void) {
  if (!g_atlas_ok) build_atlas();
  if (!g_soft_ok) {
    for (int i = 0; i <= 256; ++i) g_soft_lut[i] = 1.0f / (1.0f + expf(3.404f * ((float)i / 64.0f - 2.0f)));
    g_soft_ok = true;
  }
}

void hud_draw_sw(const Hud* h, uint32_t* fb, int W, int H, int k, int ry0, int ry1) {
  hud_prepare();
  if (ry1 < 0 || ry1 > H) ry1 = H;
  if (ry0 < 0) ry0 = 0;
  for (int i = 0; i < h->n; ++i) {
    const HudCmd& c = h->cmd[i];
    if (c.kind == HUD_SHAPE) {
      if (c.idx < h->ns) draw_shape(h->shape[c.idx], fb, W, H, k, c, ry0, ry1);
      continue;
    }
    const int x0 = to_px(c.x, k), y0 = to_px(c.y, k);
    const int x1 = to_px(c.x + c.w, k), y1 = to_px(c.y + c.h, k);
    const int xa = x0 < 0 ? 0 : x0, ya = y0 < ry0 ? ry0 : y0;
    const int xb = x1 > W ? W : x1, yb = y1 > ry1 ? ry1 : y1;
    if (xa >= xb || ya >= yb) continue;
    const uint32_t color = 0xFF000000u | (c.argb & 0xFFFFFFu);
    if (c.kind == HUD_RECT) {
      for (int y = ya; y < yb; ++y) {
        uint32_t* p = fb + (long)y * W;
        for (int x = xa; x < xb; ++x) p[x] = color;
      }
    } else if (c.kind == HUD_DIM) {
      for (int y = ya; y < yb; ++y) {
        uint32_t* p = fb + (long)y * W;
        for (int x = xa; x < xb; ++x) {
          const uint32_t v = p[x];
          p[x] = 0xFF000000u | (((v >> 2) & 0x3F3F3Fu) + ((v >> 3) & 0x1F1F1Fu));   // 3/8 della luminosita'
        }
      }
    } else if (c.idx < HUD_NGLYPHS) {
      // Carattere o disegno: ogni pixel dello schermo prende il punto piu' vicino dell'atlante
      // (a 640x480 un pixel dell'atlante per pixel dello schermo).
      const HudGlyph& G = HUD_GLYPHS[c.idx];
      const int qw = x1 - x0, qh = y1 - y0;
      if (qw <= 0 || qh <= 0) continue;
      const unsigned ca = c.argb >> 24;
      if (qw == G.w && qh == G.h) {   // pixel su pixel (640x480): senza divisioni
        for (int y = ya; y < yb; ++y) {
          const uint8_t* src = g_atlas + (long)(G.y + y - y0) * HUD_ATLAS_W + G.x - x0;
          uint32_t* p = fb + (long)y * W;
          for (int x = xa; x < xb; ++x) {
            const unsigned v = src[x];
            if (!v) continue;
            const unsigned a = ca == 255u ? v : (v * ca + 127u) / 255u;
            if (a) p[x] = a >= 255u ? color : blend(p[x], color, a);
          }
        }
        continue;
      }
      for (int y = ya; y < yb; ++y) {
        int ty = ((y - y0) * G.h + G.h / 2) / qh;
        if (ty >= G.h) ty = G.h - 1;
        const uint8_t* src = g_atlas + (long)(G.y + ty) * HUD_ATLAS_W + G.x;
        uint32_t* p = fb + (long)y * W;
        for (int x = xa; x < xb; ++x) {
          int tx = ((x - x0) * G.w + G.w / 2) / qw;
          if (tx >= G.w) tx = G.w - 1;
          const unsigned a = ((unsigned)src[tx] * ca + 127u) / 255u;
          if (a) p[x] = a >= 255u ? color : blend(p[x], color, a);
        }
      }
    }
  }
}

const uint8_t* hud_atlas(int* w, int* h) {
  if (!g_atlas_ok) build_atlas();
  *w = HUD_ATLAS_W;
  *h = HUD_ATLAS_H;
  return g_atlas;
}

void hud_glyph_uv(int glyph, float* u0, float* v0, float* u1, float* v1) {
  const HudGlyph& G = HUD_GLYPHS[glyph >= 0 && glyph < HUD_NGLYPHS ? glyph : 0];
  *u0 = (float)G.x / (float)HUD_ATLAS_W;
  *v0 = (float)G.y / (float)HUD_ATLAS_H;
  *u1 = (float)(G.x + G.w) / (float)HUD_ATLAS_W;
  *v1 = (float)(G.y + G.h) / (float)HUD_ATLAS_H;
}

void hud_solid_uv(float* u, float* v) {
  *u = ((float)HUD_SOLID_X + 4.0f) / (float)HUD_ATLAS_W;
  *v = ((float)HUD_SOLID_Y + 4.0f) / (float)HUD_ATLAS_H;
}
