// Renderer software.
//
// Percorso di un fotogramma:
//  1. per ogni oggetto: scarto se fuori vista, vertici in spazio di clip, scarto delle facce
//     rivolte altrove, luce per faccia, ritaglio sul piano vicino e sulla banda di guardia,
//     proiezione e preparazione del triangolo (pendenze dei lati, piano della profondita');
//  2. i triangoli vengono distribuiti in bande orizzontali;
//  3. le bande vengono rasterizzate in parallelo: ogni banda e' di un solo thread, quindi non
//     servono lucchetti e il risultato e' identico qualunque sia il numero di thread.
//
// Profondita': si memorizza znear / w (piu' grande = piu' vicino), lineare sullo schermo.
// Copertura: un pixel appartiene al triangolo se il suo centro e' dentro; i lati condivisi
// vengono valutati con la stessa identica espressione dai due triangoli, quindi non restano
// fessure ne' pixel disegnati due volte (serve -ffp-contract=off).
#include "swr.h"
#include "plat.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(SWR_NO_SIMD) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define SWR_SIMD_NEON 1
#elif !defined(SWR_NO_SIMD) && defined(__SSE2__)
#include <emmintrin.h>
#define SWR_SIMD_SSE2 1
#endif

#define SWR_MAX_THREADS 8
#define SWR_MAX_BANDS 64
#define SWR_GUARD 4.0f              // banda di guardia: quante volte lo schermo
#define SWR_MAX_TRIS (1 << 17)

namespace {

struct SV { float x, y, z; };       // vertice a schermo: pixel, z = znear / w
struct CV { float x, y, w; };       // vertice in spazio di clip

struct RTri {
  float ax, ay, bx, by;             // vertice piu' alto e vertice intermedio
  float sl, su, sb;                 // dx/dy del lato lungo (a-c), superiore (a-b), inferiore (b-c)
  int16_t iy0, iy1, iy2, pad;       // righe [iy0, iy1) con il lato superiore, [iy1, iy2) con l'inferiore
  uint32_t color;
  double z0, dzdx, dzdy;            // piano della profondita', riferito al vertice a
};

enum {
  OC_LEFT = 1, OC_RIGHT = 2, OC_BOTTOM = 4, OC_TOP = 8, OC_NEAR = 16,
  OC_GLEFT = 32, OC_GRIGHT = 64, OC_GBOTTOM = 128, OC_GTOP = 256,
  OC_FRUSTUM = 31, OC_CLIP = 16 | 32 | 64 | 128 | 256
};

int g_maxw, g_maxh;
uint32_t* g_color;
float* g_depth;
RTri* g_tris;
int g_ntris, g_overflow, g_clipped;
uint32_t* g_bin;
size_t g_bin_cap;
int g_band_start[SWR_MAX_BANDS + 1];
int g_band_px[SWR_MAX_BANDS];
const Hud* g_hud;      // interfaccia del fotogramma in corso (o nessuna)
int g_hud_k;
uint32_t* g_row_color;
uint8_t* g_row_band;
int g_W, g_H, g_band_h, g_nbands;
float g_Wf, g_Hf, g_zn;
bool g_count;
SV* g_sv;
CV* g_cv;
uint16_t* g_oc;
int g_vcap;
Plane g_fr[5];

// --- thread ---------------------------------------------------------------
pthread_t g_th[SWR_MAX_THREADS];
int g_nworkers, g_max_threads = 1, g_threads = 1;
pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cv_work = PTHREAD_COND_INITIALIZER;
pthread_cond_t g_cv_done = PTHREAD_COND_INITIALIZER;
unsigned g_gen;
int g_job_workers, g_pending;
bool g_quit;
volatile int g_next_band;

inline int iceil(float v) {
  const int i = (int)v;
  return i + (v > (float)i);
}

// --- campate --------------------------------------------------------------
// Per ogni pixel: z = z0 + i * dz; se e' piu' vicino di quello gia' presente, scrive profondita'
// e colore. Quattro pixel per volta con NEON (ARM) o SSE2 (PC); le istruzioni vettoriali
// fanno le stesse operazioni, nello stesso ordine, della versione scalare, quindi il risultato
// e' identico bit per bit. Su ARM a 32 bit il compilatore non vettorizza da solo i calcoli in
// virgola mobile, per questo le istruzioni sono scritte a mano.
// "room" e' quanti pixel restano fino alla fine della riga: la coda della campata viene
// elaborata con una maschera solo se i quattro pixel toccati stanno tutti nella stessa riga
// (righe diverse possono appartenere a thread diversi).
inline __attribute__((always_inline))
void span_flat(uint32_t* __restrict c, float* __restrict zb, int n, int room, float z0, float dz, uint32_t color) {
  int i = 0;
#if defined(SWR_SIMD_NEON)
  static const int32_t k_iota[4] = { 0, 1, 2, 3 };
  static const uint32_t k_tail[4][4] = { { 0, 0, 0, 0 }, { ~0u, 0, 0, 0 }, { ~0u, ~0u, 0, 0 }, { ~0u, ~0u, ~0u, 0 } };
  const float32x4_t vz0 = vdupq_n_f32(z0), vdz = vdupq_n_f32(dz);
  const uint32x4_t vcol = vdupq_n_u32(color);
  const int32x4_t vfour = vdupq_n_s32(4);
  int32x4_t vi = vld1q_s32(k_iota);
  for (; i + 4 <= n; i += 4) {
    const float32x4_t z = vaddq_f32(vz0, vmulq_f32(vcvtq_f32_s32(vi), vdz));
    const float32x4_t zo = vld1q_f32(zb + i);
    const uint32x4_t co = vld1q_u32(c + i);
    const uint32x4_t m = vcgtq_f32(z, zo);
    vst1q_f32(zb + i, vbslq_f32(m, z, zo));
    vst1q_u32(c + i, vbslq_u32(m, vcol, co));
    vi = vaddq_s32(vi, vfour);
  }
  if (i < n && room - i >= 4) {
    const float32x4_t z = vaddq_f32(vz0, vmulq_f32(vcvtq_f32_s32(vi), vdz));
    const float32x4_t zo = vld1q_f32(zb + i);
    const uint32x4_t co = vld1q_u32(c + i);
    const uint32x4_t m = vandq_u32(vcgtq_f32(z, zo), vld1q_u32(k_tail[n - i]));
    vst1q_f32(zb + i, vbslq_f32(m, z, zo));
    vst1q_u32(c + i, vbslq_u32(m, vcol, co));
    return;
  }
#elif defined(SWR_SIMD_SSE2)
  static const uint32_t k_tail[4][4] = { { 0, 0, 0, 0 }, { ~0u, 0, 0, 0 }, { ~0u, ~0u, 0, 0 }, { ~0u, ~0u, ~0u, 0 } };
  const __m128 vz0 = _mm_set1_ps(z0), vdz = _mm_set1_ps(dz);
  const __m128i vcol = _mm_set1_epi32((int)color);
  const __m128i vfour = _mm_set1_epi32(4);
  __m128i vi = _mm_setr_epi32(0, 1, 2, 3);
  for (; i + 4 <= n; i += 4) {
    const __m128 z = _mm_add_ps(vz0, _mm_mul_ps(_mm_cvtepi32_ps(vi), vdz));
    const __m128 zo = _mm_loadu_ps(zb + i);
    const __m128i co = _mm_loadu_si128((const __m128i*)(c + i));
    const __m128 m = _mm_cmpgt_ps(z, zo);
    const __m128i mi = _mm_castps_si128(m);
    _mm_storeu_ps(zb + i, _mm_or_ps(_mm_and_ps(m, z), _mm_andnot_ps(m, zo)));
    _mm_storeu_si128((__m128i*)(c + i), _mm_or_si128(_mm_and_si128(mi, vcol), _mm_andnot_si128(mi, co)));
    vi = _mm_add_epi32(vi, vfour);
  }
  if (i < n && room - i >= 4) {
    const __m128 z = _mm_add_ps(vz0, _mm_mul_ps(_mm_cvtepi32_ps(vi), vdz));
    const __m128 zo = _mm_loadu_ps(zb + i);
    const __m128i co = _mm_loadu_si128((const __m128i*)(c + i));
    const __m128 m = _mm_and_ps(_mm_cmpgt_ps(z, zo), _mm_loadu_ps((const float*)k_tail[n - i]));
    const __m128i mi = _mm_castps_si128(m);
    _mm_storeu_ps(zb + i, _mm_or_ps(_mm_and_ps(m, z), _mm_andnot_ps(m, zo)));
    _mm_storeu_si128((__m128i*)(c + i), _mm_or_si128(_mm_and_si128(mi, vcol), _mm_andnot_si128(mi, co)));
    return;
  }
#else
  (void)room;
#endif
  for (; i < n; ++i) {
    const float z = z0 + (float)i * dz;
    const float zo = zb[i];
    const uint32_t co = c[i];
    const bool pass = z > zo;
    zb[i] = pass ? z : zo;
    c[i] = pass ? color : co;
  }
}

inline void span_count(uint32_t* c, int n) {
  for (int i = 0; i < n; ++i) c[i] += 1;
}

inline void raster_rows(const RTri& t, int ys, int ye, int& px) {
  const int W = g_W;
  const float Wf = g_Wf;
  uint32_t* crow = g_color + (size_t)ys * W;
  float* zrow = g_depth + (size_t)ys * W;
  const float dz = (float)t.dzdx;
  for (int y = ys; y < ye; ++y, crow += W, zrow += W) {
    const float yc = (float)y + 0.5f;
    const float xa = t.ax + (yc - t.ay) * t.sl;
    const float xb = y < t.iy1 ? t.ax + (yc - t.ay) * t.su : t.bx + (yc - t.by) * t.sb;
    float lo = (xa < xb ? xa : xb) - 0.5f;
    float hi = (xa < xb ? xb : xa) - 0.5f;
    if (lo < 0.0f) lo = 0.0f;
    if (hi > Wf) hi = Wf;
    if (!(hi > lo)) continue;
    const int xl = iceil(lo), xr = iceil(hi);
    const int n = xr - xl;
    if (n <= 0) continue;
    px += n;
    if (g_count) { span_count(crow + xl, n); continue; }
    const double zs = t.z0 + ((double)xl + 0.5 - (double)t.ax) * t.dzdx + ((double)yc - (double)t.ay) * t.dzdy;
    span_flat(crow + xl, zrow + xl, n, W - xl, (float)zs, dz, t.color);
  }
}

void raster_band(int b) {
  const int W = g_W;
  const int y0 = b * g_band_h;
  int y1 = y0 + g_band_h;
  if (y1 > g_H) y1 = g_H;
  if (g_count) {
    memset(g_color + (size_t)y0 * W, 0, (size_t)(y1 - y0) * W * sizeof(uint32_t));
  } else {
    for (int y = y0; y < y1; ++y) {
      const uint32_t c = g_row_color[y];
      uint32_t* p = g_color + (size_t)y * W;
      for (int x = 0; x < W; ++x) p[x] = c;
    }
  }
  memset(g_depth + (size_t)y0 * W, 0, (size_t)(y1 - y0) * W * sizeof(float));
  int px = 0;
  const uint32_t* idx = g_bin + g_band_start[b];
  const int n = g_band_start[b + 1] - g_band_start[b];
  for (int i = 0; i < n; ++i) {
    const RTri& t = g_tris[idx[i]];
    const int ys = t.iy0 > y0 ? t.iy0 : y0;
    const int ye = t.iy2 < y1 ? t.iy2 : y1;
    raster_rows(t, ys, ye, px);
  }
  g_band_px[b] = px;
  // L'interfaccia sopra, nella stessa striscia (le strisce vanno in parallelo).
  if (g_hud && !g_count) hud_draw_sw(g_hud, g_color, W, g_H, g_hud_k, y0, y1);
}

void run_bands(void) {
  for (;;) {
    const int b = __sync_fetch_and_add(&g_next_band, 1);
    if (b >= g_nbands) break;
    raster_band(b);
  }
}

void* worker_main(void* arg) {
  const int idx = (int)(intptr_t)arg;
  unsigned seen = 0;
  pthread_mutex_lock(&g_mu);
  for (;;) {
    while (!g_quit && g_gen == seen) pthread_cond_wait(&g_cv_work, &g_mu);
    if (g_quit) break;
    seen = g_gen;
    if (idx >= g_job_workers) continue;
    pthread_mutex_unlock(&g_mu);
    run_bands();
    pthread_mutex_lock(&g_mu);
    if (--g_pending == 0) pthread_cond_signal(&g_cv_done);
  }
  pthread_mutex_unlock(&g_mu);
  return 0;
}

void raster_all(void) {
  g_next_band = 0;
  int nw = g_threads - 1;
  if (nw > g_nworkers) nw = g_nworkers;
  if (nw > 0) {
    pthread_mutex_lock(&g_mu);
    g_job_workers = nw;
    g_pending = nw;
    g_gen++;
    pthread_cond_broadcast(&g_cv_work);
    pthread_mutex_unlock(&g_mu);
  }
  run_bands();
  if (nw > 0) {
    pthread_mutex_lock(&g_mu);
    while (g_pending > 0) pthread_cond_wait(&g_cv_done, &g_mu);
    pthread_mutex_unlock(&g_mu);
  }
}

// --- geometria ------------------------------------------------------------
inline void project(const CV& c, SV* s) {
  const float inv = 1.0f / c.w;
  s->x = (c.x * inv * 0.5f + 0.5f) * g_Wf;
  s->y = (0.5f - c.y * inv * 0.5f) * g_Hf;
  s->z = g_zn * inv;
}

inline void emit_tri(const SV& p0, const SV& p1, const SV& p2, uint32_t color) {
  const SV* a = &p0;
  const SV* b = &p1;
  const SV* c = &p2;
  const SV* t;
  if (b->y < a->y) { t = a; a = b; b = t; }
  if (c->y < a->y) { t = a; a = c; c = t; }
  if (c->y < b->y) { t = b; b = c; c = t; }
  const int iy0 = iceil(a->y - 0.5f), iy2 = iceil(c->y - 0.5f);
  if (iy0 >= iy2) return;                         // non tocca il centro di nessuna riga
  const int y0 = iy0 < 0 ? 0 : iy0;
  const int y2 = iy2 > g_H ? g_H : iy2;
  if (y0 >= y2) return;
  float minx = a->x < b->x ? a->x : b->x;
  if (c->x < minx) minx = c->x;
  float maxx = a->x > b->x ? a->x : b->x;
  if (c->x > maxx) maxx = c->x;
  if (maxx <= 0.5f || minx > g_Wf - 0.5f) return;
  const double abx = (double)(b->x - a->x), aby = (double)(b->y - a->y);
  const double acx = (double)(c->x - a->x), acy = (double)(c->y - a->y);
  const double det = abx * acy - acx * aby;
  if (det == 0.0) return;
  if (g_ntris >= SWR_MAX_TRIS) { g_overflow++; return; }
  RTri& r = g_tris[g_ntris++];
  r.ax = a->x; r.ay = a->y; r.bx = b->x; r.by = b->y;
  // Ogni lato e' descritto dal suo vertice piu' alto e da dx/dy calcolato sempre allo stesso modo.
  r.sl = (float)(acx / acy);
  r.su = aby > 0.0 ? (float)(abx / aby) : 0.0f;
  const float bcy = c->y - b->y;
  r.sb = bcy > 0.0f ? (float)((double)(c->x - b->x) / (double)bcy) : 0.0f;
  int iy1 = iceil(b->y - 0.5f);
  if (iy1 < -32000) iy1 = -32000;
  if (iy1 > 32000) iy1 = 32000;
  r.iy0 = (int16_t)y0; r.iy1 = (int16_t)iy1; r.iy2 = (int16_t)y2; r.pad = 0;
  r.color = color;
  const double abz = (double)(b->z - a->z), acz = (double)(c->z - a->z);
  const double inv = 1.0 / det;
  r.dzdx = (abz * acy - acz * aby) * inv;
  r.dzdy = (acz * abx - abz * acx) * inv;
  r.z0 = (double)a->z;
}

inline float plane_dist(const CV& v, int pl) {
  switch (pl) {
    case 0: return v.w - g_zn;
    case 1: return v.x + SWR_GUARD * v.w;
    case 2: return SWR_GUARD * v.w - v.x;
    case 3: return v.y + SWR_GUARD * v.w;
    default: return SWR_GUARD * v.w - v.y;
  }
}

// Intersezione sempre calcolata dal vertice interno verso quello esterno: due triangoli che
// condividono il lato ottengono lo stesso identico punto.
inline CV isect(const CV& in, const CV& out, float din, float dout) {
  const float t = din / (din - dout);
  CV r;
  r.x = in.x + t * (out.x - in.x);
  r.y = in.y + t * (out.y - in.y);
  r.w = in.w + t * (out.w - in.w);
  return r;
}

void clip_emit(const CV& a, const CV& b, const CV& c, uint32_t color) {
  g_clipped++;
  CV p0[12], p1[12];
  CV* in = p0;
  CV* out = p1;
  int n = 3;
  in[0] = a; in[1] = b; in[2] = c;
  for (int pl = 0; pl < 5; ++pl) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
      const CV& cur = in[i];
      const CV& nxt = in[i + 1 == n ? 0 : i + 1];
      const float dc = plane_dist(cur, pl), dn = plane_dist(nxt, pl);
      if (dc >= 0.0f) {
        out[m++] = cur;
        if (dn < 0.0f) out[m++] = isect(cur, nxt, dc, dn);
      } else if (dn >= 0.0f) {
        out[m++] = isect(nxt, cur, dn, dc);
      }
    }
    n = m;
    if (n < 3) return;
    CV* tmp = in; in = out; out = tmp;
  }
  SV s[12];
  for (int i = 0; i < n; ++i) project(in[i], &s[i]);
  for (int i = 1; i + 1 < n; ++i) emit_tri(s[0], s[i], s[i + 1], color);
}

void ensure_vcap(int n) {
  if (n <= g_vcap) return;
  g_vcap = n + 256;
  g_sv = (SV*)realloc(g_sv, sizeof(SV) * (size_t)g_vcap);
  g_cv = (CV*)realloc(g_cv, sizeof(CV) * (size_t)g_vcap);
  g_oc = (uint16_t*)realloc(g_oc, sizeof(uint16_t) * (size_t)g_vcap);
}

void draw_item(const DrawItem& it, const FrameDesc& f, RenderStats* st) {
  const Mesh& m = *it.mesh;
  if (sphere_outside(g_fr, xf_point(it.xf, m.center), m.radius * xf_max_scale(it.xf))) return;
  st->items_drawn++;
  st->tris_in += m.nt;
  ensure_vcap(m.nv);
  float M[3][4];
  mvp_rows013(f.cam.vp, it.xf, M);
  const float zn = g_zn;
  for (int i = 0; i < m.nv; ++i) {
    const Vec3 v = m.v[i];
    CV c;
    c.x = M[0][0] * v.x + M[0][1] * v.y + M[0][2] * v.z + M[0][3];
    c.y = M[1][0] * v.x + M[1][1] * v.y + M[1][2] * v.z + M[1][3];
    c.w = M[2][0] * v.x + M[2][1] * v.y + M[2][2] * v.z + M[2][3];
    g_cv[i] = c;
    unsigned oc = 0;
    if (c.x < -c.w) oc |= OC_LEFT;
    if (c.x > c.w) oc |= OC_RIGHT;
    if (c.y < -c.w) oc |= OC_BOTTOM;
    if (c.y > c.w) oc |= OC_TOP;
    const float gw = SWR_GUARD * c.w;
    if (c.x < -gw) oc |= OC_GLEFT;
    if (c.x > gw) oc |= OC_GRIGHT;
    if (c.y < -gw) oc |= OC_GBOTTOM;
    if (c.y > gw) oc |= OC_GTOP;
    if (c.w < zn) oc |= OC_NEAR;
    else project(c, &g_sv[i]);
    g_oc[i] = (uint16_t)oc;
  }
  // Camera portata nello spazio dell'oggetto e cofattori per le normali: valgono anche con la
  // scala, e sono gli stessi calcoli del renderer GPU (stessi colori).
  ItemSpace sp;
  item_space(it.xf, f.cam.eye, &sp);
  for (int i = 0; i < m.nt; ++i) {
    const MeshTri& t = m.t[i];
    const unsigned oa = g_oc[t.a], ob = g_oc[t.b], oc = g_oc[t.c];
    if (oa & ob & oc & OC_FRUSTUM) continue;                 // tutto fuori dallo stesso lato
    if (!tri_facing(t, m.v, sp)) continue;                   // faccia rivolta altrove
    const uint32_t color = shade_tri(t, sp, f.light, it.flash, it.dim);
    if (!((oa | ob | oc) & OC_CLIP)) emit_tri(g_sv[t.a], g_sv[t.b], g_sv[t.c], color);
    else clip_emit(g_cv[t.a], g_cv[t.b], g_cv[t.c], color);
  }
}

void bin_tris(void) {
  int cnt[SWR_MAX_BANDS + 1];
  for (int b = 0; b <= g_nbands; ++b) cnt[b] = 0;
  for (int i = 0; i < g_ntris; ++i) {
    const RTri& t = g_tris[i];
    const int b0 = g_row_band[t.iy0], b1 = g_row_band[t.iy2 - 1];
    for (int b = b0; b <= b1; ++b) cnt[b]++;
  }
  size_t total = 0;
  for (int b = 0; b < g_nbands; ++b) {
    g_band_start[b] = (int)total;
    total += (size_t)cnt[b];
  }
  g_band_start[g_nbands] = (int)total;
  if (total > g_bin_cap) {
    g_bin_cap = total + total / 2 + 1024;
    g_bin = (uint32_t*)realloc(g_bin, sizeof(uint32_t) * g_bin_cap);
  }
  int cur[SWR_MAX_BANDS];
  for (int b = 0; b < g_nbands; ++b) cur[b] = g_band_start[b];
  for (int i = 0; i < g_ntris; ++i) {
    const RTri& t = g_tris[i];
    const int b0 = g_row_band[t.iy0], b1 = g_row_band[t.iy2 - 1];
    for (int b = b0; b <= b1; ++b) g_bin[cur[b]++] = (uint32_t)i;
  }
}

void setup_res(int W, int H) {
  g_W = W; g_H = H;
  g_Wf = (float)W; g_Hf = (float)H;
  g_band_h = (H + 19) / 20;
  if (g_band_h < 8) g_band_h = 8;
  g_nbands = (H + g_band_h - 1) / g_band_h;
  if (g_nbands > SWR_MAX_BANDS) {
    g_nbands = SWR_MAX_BANDS;
    g_band_h = (H + SWR_MAX_BANDS - 1) / SWR_MAX_BANDS;
  }
  for (int y = 0; y < H; ++y) g_row_band[y] = (uint8_t)(y / g_band_h);
}

void* alloc_aligned(size_t bytes) {
  void* p = 0;
  if (posix_memalign(&p, 64, bytes) != 0) return 0;
  memset(p, 0, bytes);
  return p;
}

}  // namespace

bool swr_init(int max_w, int max_h) {
  g_maxw = max_w; g_maxh = max_h;
  g_color = (uint32_t*)alloc_aligned(sizeof(uint32_t) * (size_t)max_w * max_h);
  g_depth = (float*)alloc_aligned(sizeof(float) * (size_t)max_w * max_h);
  g_tris = (RTri*)alloc_aligned(sizeof(RTri) * SWR_MAX_TRIS);
  g_row_color = (uint32_t*)alloc_aligned(sizeof(uint32_t) * (size_t)max_h);
  g_row_band = (uint8_t*)alloc_aligned((size_t)max_h);
  if (!g_color || !g_depth || !g_tris || !g_row_color || !g_row_band) return false;
  g_W = g_H = 0;
  g_count = false;
  // Si contano le CPU presenti, non quelle accese in questo momento: alcuni kernel spengono i
  // core quando il sistema e' fermo e li riaccendono sotto carico. I thread in piu' dormono.
  long ncpu = sysconf(_SC_NPROCESSORS_CONF);
  const long online = sysconf(_SC_NPROCESSORS_ONLN);
  if (online > ncpu) ncpu = online;
  // PUPAZZI_MAX_THREADS: forza il massimo (serve alle prove su macchine con poche CPU).
  const char* forced = getenv("PUPAZZI_MAX_THREADS");
  if (forced && *forced) ncpu = atol(forced);
  if (ncpu < 1) ncpu = 1;
  if (ncpu > SWR_MAX_THREADS) ncpu = SWR_MAX_THREADS;
  g_max_threads = (int)ncpu;
  g_quit = false;
  g_gen = 0;
  g_nworkers = 0;
  for (int i = 0; i < g_max_threads - 1; ++i) {
    if (pthread_create(&g_th[g_nworkers], 0, worker_main, (void*)(intptr_t)g_nworkers) != 0) break;
    g_nworkers++;
  }
  g_max_threads = g_nworkers + 1;
  g_threads = g_max_threads > 4 ? 4 : g_max_threads;
  return true;
}

void swr_shutdown(void) {
  pthread_mutex_lock(&g_mu);
  g_quit = true;
  pthread_cond_broadcast(&g_cv_work);
  pthread_mutex_unlock(&g_mu);
  for (int i = 0; i < g_nworkers; ++i) pthread_join(g_th[i], 0);
  g_nworkers = 0;
  free(g_color); g_color = 0;
  free(g_depth); g_depth = 0;
  free(g_tris); g_tris = 0;
  free(g_row_color); g_row_color = 0;
  free(g_row_band); g_row_band = 0;
  free(g_bin); g_bin = 0; g_bin_cap = 0;
  free(g_sv); g_sv = 0;
  free(g_cv); g_cv = 0;
  free(g_oc); g_oc = 0;
  g_vcap = 0;
}

void swr_set_threads(int n) {
  if (n < 1) n = 1;
  if (n > g_max_threads) n = g_max_threads;
  g_threads = n;
}
int swr_get_threads(void) { return g_threads; }
int swr_max_threads(void) { return g_max_threads; }
void swr_debug_count_mode(bool on) { g_count = on; }
const float* swr_debug_depth(void) { return g_depth; }
int swr_debug_clipped(void) { return g_clipped; }

const uint32_t* swr_render(const FrameDesc* f, RenderStats* st) {
  const double t0 = plat_now_ms();
  int W = f->w, H = f->h;
  if (W > g_maxw) W = g_maxw;
  if (H > g_maxh) H = g_maxh;
  if (W != g_W || H != g_H) setup_res(W, H);
  const int tr = (int)((f->sky_top >> 16) & 255), tg = (int)((f->sky_top >> 8) & 255), tb = (int)(f->sky_top & 255);
  const int br = (int)((f->sky_bot >> 16) & 255), bg = (int)((f->sky_bot >> 8) & 255), bb = (int)(f->sky_bot & 255);
  for (int y = 0; y < H; ++y) {
    const int k = H > 1 ? y * 256 / (H - 1) : 0;
    const uint32_t r = (uint32_t)(tr + (((br - tr) * k) >> 8));
    const uint32_t g = (uint32_t)(tg + (((bg - tg) * k) >> 8));
    const uint32_t b = (uint32_t)(tb + (((bb - tb) * k) >> 8));
    g_row_color[y] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  g_zn = f->cam.znear;
  frustum_planes(f->cam.vp, g_zn, g_fr);
  g_ntris = 0;
  g_overflow = 0;
  g_clipped = 0;
  st->items_in += f->nitems;
  for (int i = 0; i < f->nitems; ++i) draw_item(f->items[i], *f, st);
  bin_tris();
  const double t1 = plat_now_ms();
  g_hud = f->hud;
  g_hud_k = f->hud_scale;
  if (g_hud) hud_prepare();
  raster_all();      // con l'interfaccia, striscia per striscia
  g_hud = 0;
  const double t2 = plat_now_ms();
  const double t3 = t2;
  int px = 0;
  for (int b = 0; b < g_nbands; ++b) px += g_band_px[b];
  st->tris_drawn += g_ntris;
  st->overflow += g_overflow;
  st->pixels += px;
  st->ms_geo += t1 - t0;
  st->ms_ras += t2 - t1;
  st->ms_hud += t3 - t2;
  return g_color;
}
