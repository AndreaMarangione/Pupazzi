// Descrizione di un fotogramma, comune al renderer software e a quello GPU.
#pragma once
#include "vmath.h"
#include "mesh.h"
#include "hud.h"

enum { ITEM_STATIC = 1 };   // geometria che non cambia mai (il renderer GPU la carica una volta sola)

// flash: 0..255, quanto il colore va verso il bianco (lampeggio di chi viene colpito);
// dim: 0..255, quanto va verso un grigio freddo (chi e' a terra).
// Valgono solo per gli oggetti mobili: quelli fissi il renderer GPU li carica una volta sola.
struct DrawItem { const Mesh* mesh; Xform xf; uint32_t flags; uint8_t flash = 0; uint8_t dim = 0; };

struct Camera { Vec3 eye; Mat4 view, proj, vp; float znear, zfar; };

struct FrameDesc {
  int w, h;
  Camera cam;
  const DrawItem* items; int nitems;
  Vec3 light;                    // direzione unitaria dalla superficie verso la luce
  uint32_t sky_top, sky_bot;     // sfumatura del cielo
  const Hud* hud; int hud_scale;
  int aa = 0;                    // campioni dell'antialias: 0 (niente), 4 o 8 (solo renderer GPU, fin dove la GPU arriva)
};

struct RenderStats {
  int items_in, items_drawn;
  int tris_in, tris_drawn;
  int draw_calls, overflow;
  int pixels;                    // pixel esaminati dal rasterizzatore (misura del ridisegno)
  double ms_geo, ms_ras, ms_hud;
};

static inline void camera_setup(Camera* c, Vec3 eye, Vec3 target, float fovy, float aspect, float zn, float zf) {
  c->eye = eye;
  c->znear = zn;
  c->zfar = zf;
  c->view = mat4_look_at(eye, target, v3(0, 1, 0));
  c->proj = mat4_perspective(fovy, aspect, zn, zf);
  c->vp = mat4_mul(c->proj, c->view);
}

// Spazio dell'oggetto per luce e scarto delle facce, valido anche con scala (squash & stretch):
// C sono i cofattori della parte 3x3 (C * n = normale in spazio mondo, a meno della lunghezza)
// e cam_m la camera portata nello spazio dell'oggetto con l'inversa vera.
struct ItemSpace { float C[9]; Vec3 cam_m; };

static inline void item_space(const Xform& x, Vec3 eye, ItemSpace* s) {
  const float* r = x.r;
  float* C = s->C;
  C[0] = r[4] * r[8] - r[5] * r[7]; C[1] = r[5] * r[6] - r[3] * r[8]; C[2] = r[3] * r[7] - r[4] * r[6];
  C[3] = r[2] * r[7] - r[1] * r[8]; C[4] = r[0] * r[8] - r[2] * r[6]; C[5] = r[1] * r[6] - r[0] * r[7];
  C[6] = r[1] * r[5] - r[2] * r[4]; C[7] = r[2] * r[3] - r[0] * r[5]; C[8] = r[0] * r[4] - r[1] * r[3];
  const float det = r[0] * C[0] + r[1] * C[1] + r[2] * C[2];
  const float k = det != 0.0f ? 1.0f / det : 0.0f;
  const Vec3 d = eye - x.p;
  s->cam_m = v3((C[0] * d.x + C[3] * d.y + C[6] * d.z) * k,
                (C[1] * d.x + C[4] * d.y + C[7] * d.z) * k,
                (C[2] * d.x + C[5] * d.y + C[8] * d.z) * k);
}

// Faccia rivolta verso la camera?
static inline bool tri_facing(const MeshTri& t, const Vec3* v, const ItemSpace& s) {
  return dot(t.n, v[t.a] - s.cam_m) < 0.0f;
}

// Luce da cartone: tre toni netti (in luce, mezzaombra, ombra fredda). Le facce TRI_UNLIT
// hanno il loro colore pieno e non lampeggiano. flash: 0..255 verso il bianco; dim: verso il grigio.
static inline uint32_t shade_tri(const MeshTri& t, const ItemSpace& s, Vec3 light, unsigned flash, unsigned dim) {
  uint32_t r = t.r, g = t.g, b = t.bl;
  if (dim) {
    const int l = (int)((r * 77u + g * 150u + b * 29u) >> 8);
    const int tr = (l * 170) >> 8, tg = (l * 176) >> 8, tb = (l * 200) >> 8;
    r = (uint32_t)((int)r + ((tr - (int)r) * (int)dim) / 256);
    g = (uint32_t)((int)g + ((tg - (int)g) * (int)dim) / 256);
    b = (uint32_t)((int)b + ((tb - (int)b) * (int)dim) / 256);
  }
  if (t.flags & TRI_UNLIT) return 0xFF000000u | (r << 16) | (g << 8) | b;
  const float* C = s.C;
  const Vec3 n = v3(C[0] * t.n.x + C[1] * t.n.y + C[2] * t.n.z,
                    C[3] * t.n.x + C[4] * t.n.y + C[5] * t.n.z,
                    C[6] * t.n.x + C[7] * t.n.y + C[8] * t.n.z);
  const float l2 = dot(n, n);
  const float ndl = l2 > 0.0f ? dot(n, light) / sqrtf(l2) : 0.0f;
  uint32_t kr, kg, kb;
  if (ndl > 0.45f) { kr = 256; kg = 252; kb = 244; }        // in luce, appena calda
  else if (ndl > -0.05f) { kr = 206; kg = 204; kb = 214; }  // mezzaombra
  else { kr = 146; kg = 148; kb = 176; }                     // ombra, fredda
  r = (r * kr) >> 8; g = (g * kg) >> 8; b = (b * kb) >> 8;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  if (flash) {
    r += ((255 - r) * flash) >> 8;
    g += ((255 - g) * flash) >> 8;
    b += ((255 - b) * flash) >> 8;
  }
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}
