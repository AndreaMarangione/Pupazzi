// Text, icons and shapes over the image (HUD and menus).
//
// Since 0.5.2 (Orbita style) everything is given in pixels of the 640x480 reference resolution,
// decimals included; each renderer multiplies by its scale (hud_scale / 2: 1 at 640x480).
//
// Text: in UTF-8, with the glyphs of src/font_hud.h already at the right size (faces HF_*, for
// example HF_S16 = SemiBold 16 pixels). It is positioned on the baseline; track is the extra
// spacing between letters (like letter-spacing, but without the one after the last letter). The
// title faces also have the glow (hud_text_glow), computed at startup by blurring the glyphs.
//
// Shapes: rectangles with two cut corners (top left and bottom right), ellipses, rings and
// parallelograms, with edges feathered over one pixel or soft (soft: glows and shadows), filled
// with a color or a linear gradient between two colors (angle as in CSS: 90 from left to right,
// 180 from top to bottom, 135 from the top-left corner). Colors 0xAARRGGBB.
// The same math is in the software renderer (here) and in the GPU renderer's shader (glr.cpp).
#pragma once
#include <stdint.h>
#include "engine/font_hud.h"

enum { HUD_RECT = 0, HUD_DIM = 1, HUD_GLYPH = 2, HUD_SHAPE = 3 };
enum { HS_BOX = 1, HS_ELLIPSE = 2, HS_RING = 3, HS_PARA = 4 };

struct HudCmd {
  int16_t x, y, w, h;   // pixels at 640x480: for glyphs the rectangle of the image, for shapes the bounding box
  uint32_t argb;
  uint16_t idx;         // glyph (HUD_GLYPHS) or shape (Hud::shape)
  uint8_t kind, pad;
};

struct HudShape {
  float x, y, w, h;     // box of the shape
  float cut;            // HS_BOX: side of the cut of the top-left and bottom-right corners
  float inset;          // shape shrunk by this much on every side (the inside of a border)
  float soft;           // 0: sharp edge (one feathered pixel); otherwise how soft it is (glow)
  float skew;           // HS_PARA: x shifts by skew for every pixel below the center
  float ring;           // HS_RING: thickness; HS_BOX and HS_PARA: if set, only the outline, this wide
  float ang;            // gradient angle (degrees, as in CSS)
  uint32_t c0, c1;      // gradient colors (equal: solid color)
  uint8_t type;
  uint8_t hollow;       // only around: where the shape is solid nothing is drawn (glows under opaque shapes)
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

// Simple rectangles: solid (rgb, opaque) and darkened (the background at 3/8).
void hud_rect(Hud* h, float x, float y, float w, float hh, uint32_t rgb);
void hud_dim(Hud* h, float x, float y, float w, float hh);

// Text. align: -1 from x rightward, 0 centered on x, 1 ends at x.
float hud_text_w(int face, const char* s, float track = 0.0f);
void hud_text(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
void hud_text_glow(Hud* h, float x, float base, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
void hud_printf(Hud* h, float x, float base, int face, uint32_t argb, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
int hud_face_size(int face);
int hud_face_asc(int face);
int hud_face_desc(int face);
// Baseline to center a line of text vertically on cy (like CSS lines).
float hud_mid(int face, float cy);
// Uppercase, also for the accented letters of Latin-1 (UTF-8).
void hud_upper(char* dst, const char* src, int n);

// Scripts besides Latin (since 0.5.3): Simplified Chinese and Japanese (one glyph per code
// point) and Hindi (words already composed, see tools/make_font.py). Chinese characters have two
// designs: hud_cjk picks the Chinese or the Japanese one for the texts that follow (lang_set
// does it; the language page changes it to write 中文 and 日本語).
enum { HUD_CJK_SC = 0, HUD_CJK_JP = 1, HUD_DEVA = 2 };
void hud_cjk(int script);
int hud_cjk_get(void);
// Characters drawn as '?' because no face has them, and Hindi words that are not among the
// already composed ones (drawn letter by letter); reset: start again from zero.
int hud_missing(bool reset);
// Code point of the UTF-8 character at s, and s moves forward (0 at the end; 0xFFFD if invalid).
uint32_t hud_utf8_next(const char*& s);
inline bool hud_is_deva(uint32_t cp) { return (cp >= 0x900 && cp <= 0x97F) || cp == 0x200C || cp == 0x200D; }
inline bool hud_is_cjk(uint32_t cp) {
  return (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Icons (HI_* in font_hud.h), from the top-left corner.
void hud_icon(Hud* h, int icon, float x, float y, uint32_t argb);
int hud_icon_w(int icon);
int hud_icon_h(int icon);

// Shapes.
void hud_shape(Hud* h, const HudShape& s);
void hud_box(Hud* h, float x, float y, float w, float hh, float cut, uint32_t argb, float inset = 0.0f);
void hud_box_grad(Hud* h, float x, float y, float w, float hh, float cut, uint32_t c0, uint32_t c1, float ang, float inset = 0.0f);
void hud_glow(Hud* h, float x, float y, float w, float hh, float cut, float soft, uint32_t argb);
void hud_ellipse(Hud* h, float cx, float cy, float rx, float ry, uint32_t argb, float soft = 0.0f);
void hud_ring(Hud* h, float cx, float cy, float rx, float ry, float t, uint32_t argb);
void hud_frame(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t argb);   // outline only
void hud_frame_grad(Hud* h, float x, float y, float w, float hh, float cut, float t, uint32_t c0, uint32_t c1, float ang);
void hud_para(Hud* h, float x, float y, float w, float hh, float skew, uint32_t argb);

// Signed distance from the edge of a shape (positive outside) in output pixels, for the output
// point (px, py), with scale s (output pixels per pixel at 640x480); and how much it covers.
float hud_shape_sdf(const HudShape& sh, float s, float px, float py);
float hud_shape_cover(float d, float soft_px);

// Drawing into the software renderer's framebuffer (XRGB8888, stride = W pixels); k = hud_scale.
// Only the rows from y0 to y1 excluded (y1 < 0: to the bottom): the renderer does it in strips, in
// parallel, after calling hud_prepare once.
void hud_prepare(void);
void hud_draw_sw(const Hud* h, uint32_t* fb, int W, int H, int k, int y0 = 0, int y1 = -1);

// For the GPU renderer: atlas (8-bit alpha, HUD_ATLAS_W x HUD_ATLAS_H, glows included).
const uint8_t* hud_atlas(int* w, int* h);
void hud_glyph_uv(int glyph, float* u0, float* v0, float* u1, float* v1);
void hud_solid_uv(float* u, float* v);   // a point inside the solid block
