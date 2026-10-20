// Testo, disegni e forme sopra l'immagine (HUD e menu).
//
// Dalla 0.5.2 (stile Orbita) tutto si da' in pixel della risoluzione di riferimento 640x480, anche
// con i decimali; ogni renderer moltiplica per la sua scala (hud_scale / 2: 1 a 640x480).
//
// Testo: in UTF-8, con i caratteri di src/font_hud.h gia' alla misura giusta (facce HF_*, per
// esempio HF_S16 = SemiBold 16 pixel). Si posiziona sulla linea di base; track e' la spaziatura in
// piu' tra le lettere (come letter-spacing, ma senza quella dopo l'ultima). Le facce dei titoli hanno
// anche l'alone (hud_text_glow), calcolato all'avvio sfocando i caratteri.
//
// Forme: rettangoli con due angoli tagliati (in alto a sinistra e in basso a destra), ellissi,
// anelli e parallelogrammi, con i bordi sfumati di un pixel oppure morbidi (soft: aloni e ombre),
// riempiti con un colore o una sfumatura lineare tra due colori (angolo come nei CSS: 90 da sinistra
// a destra, 180 dall'alto in basso, 135 dall'angolo in alto a sinistra). Colori 0xAARRGGBB.
// La stessa matematica e' nel renderer software (qui) e nello shader del renderer GPU (glr.cpp).
#pragma once
#include <stdint.h>
#include "font_hud.h"

enum { HUD_RECT = 0, HUD_DIM = 1, HUD_GLYPH = 2, HUD_SHAPE = 3 };
enum { HS_BOX = 1, HS_ELLIPSE = 2, HS_RING = 3, HS_PARA = 4 };

struct HudCmd {
  int16_t x, y, w, h;   // pixel a 640x480: per i caratteri il rettangolo del disegno, per le forme l'ingombro
  uint32_t argb;
  uint16_t idx;         // carattere (HUD_GLYPHS) o forma (Hud::shape)
  uint8_t kind, pad;
};

struct HudShape {
  float x, y, w, h;     // riquadro della forma
  float cut;            // HS_BOX: lato del taglio degli angoli in alto a sinistra e in basso a destra
  float inset;          // forma rimpicciolita di tanto da ogni lato (l'interno di un bordo)
  float soft;           // 0: bordo netto (un pixel sfumato); altrimenti quanto e' morbido (alone)
  float skew;           // HS_PARA: x si sposta di skew per ogni pixel sotto il centro
  float ring;           // HS_RING: spessore; HS_BOX e HS_PARA: se c'e', solo il contorno largo tanto
  float ang;            // angolo della sfumatura (gradi, come nei CSS)
  uint32_t c0, c1;      // colori della sfumatura (uguali: tinta unita)
  uint8_t type;
  uint8_t hollow;       // solo intorno: dove la forma e' piena non si disegna (aloni sotto forme opache)
};

#define HUD_MAX_CMDS 6144
#define HUD_MAX_SHAPES 1536
struct Hud { HudCmd cmd[HUD_MAX_CMDS]; HudShape shape[HUD_MAX_SHAPES]; int n, ns; };

static inline uint32_t hud_a(uint32_t rgb, float a) {
  const float c = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
  return ((uint32_t)(c * 255.0f + 0.5f) << 24) | (rgb & 0xFFFFFFu);
}
#define HUD_OPAQUE(rgb) (0xFF000000u | ((uint32_t)(rgb) & 0xFFFFFFu))

void hud_reset(Hud* h);

// Rettangoli semplici: pieno (rgb, opaco) e scurito (lo sfondo a 3/8).
void hud_rect(Hud* h, float x, float y, float w, float hh, uint32_t rgb);
void hud_dim(Hud* h, float x, float y, float w, float hh);

// Testo. align: -1 da x verso destra, 0 centrato su x, 1 finisce a x.
float hud_text_w(int face, const char* s, float track = 0.0f);
void hud_text(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
void hud_text_glow(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
void hud_printf(Hud* h, float x, float base, int face, uint32_t argb, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
int hud_face_size(int face);
int hud_face_asc(int face);
int hud_face_desc(int face);
// Linea di base per centrare in altezza una riga di testo su cy (come le righe dei CSS).
float hud_mid(int face, float cy);
// Maiuscole anche per le lettere accentate di Latin-1 (UTF-8).
void hud_upper(char* dst, const char* src, int n);

// Disegni (HI_* in font_hud.h), dall'angolo in alto a sinistra.
void hud_icon(Hud* h, int icon, float x, float y, uint32_t argb);
int hud_icon_w(int icon);
int hud_icon_h(int icon);

// Forme.
void hud_shape(Hud* h, const HudShape& s);
void hud_box(Hud* h, float x, float y, float w, float hh, float cut, uint32_t argb, float inset = 0.0f);
void hud_box_grad(Hud* h, float x, float y, float w, float hh, float cut, uint32_t c0, uint32_t c1, float ang, float inset = 0.0f);
void hud_glow(Hud* h, float x, float y, float w, float hh, float cut, float soft, uint32_t argb);
void hud_ellipse(Hud* h, float cx, float cy, float rx, float ry, uint32_t argb, float soft = 0.0f);
void hud_ring(Hud* h, float cx, float cy, float rx, float ry, float t, uint32_t argb);
void hud_frame(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t argb);   // solo il contorno
void hud_frame_grad(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t c0, uint32_t c1, float ang);
void hud_para(Hud* h, float x, float y, float w, float hh, float skew, uint32_t argb);

// Distanza con segno dal bordo di una forma (positiva fuori) in pixel d'uscita, per il punto
// (px, py) d'uscita, con la scala s (pixel d'uscita per pixel a 640x480); e quanto copre.
float hud_shape_sdf(const HudShape& sh, float s, float px, float py);
float hud_shape_cover(float d, float soft_px);

// Disegno nel framebuffer del renderer software (XRGB8888, passo = W pixel); k = hud_scale. Solo le
// righe da y0 a y1 escluso (y1 < 0: fino in fondo): il renderer lo fa a strisce, in parallelo,
// dopo aver chiamato una volta hud_prepare.
void hud_prepare(void);
void hud_draw_sw(const Hud* h, uint32_t* fb, int W, int H, int k, int y0 = 0, int y1 = -1);

// Per il renderer GPU: atlante (alfa a 8 bit, HUD_ATLAS_W x HUD_ATLAS_H, aloni compresi).
const uint8_t* hud_atlas(int* w, int* h);
void hud_glyph_uv(int glyph, float* u0, float* v0, float* u1, float* v1);
void hud_solid_uv(float* u, float* v);   // un punto dentro il blocco pieno
