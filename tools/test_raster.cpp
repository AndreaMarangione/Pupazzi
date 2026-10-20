// Prova del rasterizzatore: una stanza chiusa, con le pareti tassellate a caso, vista
// dall'interno deve coprire ogni pixel esattamente una volta. Qualunque fessura o doppia
// copertura (lati condivisi, ritaglio sul piano vicino o sulla banda di guardia, scarto delle
// facce) fa fallire la prova. In piu' controlla che la profondita' resti nei limiti attesi e
// che l'immagine non dipenda dal numero di thread.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/swr.h"

static uint32_t g_rng = 0x1234567u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

static const float S = 5.0f;
static const int N = 6;

static Mesh build_room(void) {
  MeshBuilder mb;
  mb_init(&mb);
  const int L = N + 1;
  int* idx = (int*)malloc(sizeof(int) * L * L * L);
  for (int i = 0; i < L * L * L; ++i) idx[i] = -1;
  const float cell = 2.0f * S / (float)N;
  // Vertici sulla superficie del cubo; quelli interni a una faccia o a uno spigolo vengono
  // spostati restando sulla faccia o sullo spigolo.
  for (int k = 0; k < L; ++k)
    for (int j = 0; j < L; ++j)
      for (int i = 0; i < L; ++i) {
        const int c[3] = { i, j, k };
        if (i != 0 && i != N && j != 0 && j != N && k != 0 && k != N) continue;
        float p[3];
        for (int a = 0; a < 3; ++a) {
          p[a] = -S + cell * (float)c[a];
          if (c[a] != 0 && c[a] != N) p[a] += frange(-0.2f, 0.2f) * cell;
        }
        idx[(k * L + j) * L + i] = mb_vert(&mb, v3(p[0], p[1], p[2]));
      }
  for (int axis = 0; axis < 3; ++axis)
    for (int side = 0; side < 2; ++side) {
      const int u_axis = (axis + 1) % 3, v_axis = (axis + 2) % 3;
      Vec3 inward = v3(0, 0, 0);
      const float sgn = side ? -1.0f : 1.0f;
      if (axis == 0) inward.x = sgn;
      else if (axis == 1) inward.y = sgn;
      else inward.z = sgn;
      for (int u = 0; u < N; ++u)
        for (int v = 0; v < N; ++v) {
          int q[4];
          for (int corner = 0; corner < 4; ++corner) {
            int c[3];
            c[axis] = side ? N : 0;
            c[u_axis] = u + ((corner == 1 || corner == 2) ? 1 : 0);
            c[v_axis] = v + ((corner >= 2) ? 1 : 0);
            q[corner] = idx[(c[2] * L + c[1]) * L + c[0]];
          }
          int t[2][3];
          if (rnd() & 1) { t[0][0] = q[0]; t[0][1] = q[1]; t[0][2] = q[2]; t[1][0] = q[0]; t[1][1] = q[2]; t[1][2] = q[3]; }
          else { t[0][0] = q[0]; t[0][1] = q[1]; t[0][2] = q[3]; t[1][0] = q[1]; t[1][1] = q[2]; t[1][2] = q[3]; }
          for (int k = 0; k < 2; ++k) {
            const Vec3 n = cross(mb.v[t[k][1]] - mb.v[t[k][0]], mb.v[t[k][2]] - mb.v[t[k][0]]);
            const uint32_t color = 0x202020u + (rnd() & 0xDFDFDFu);
            if (dot(n, inward) > 0.0f) mb_tri(&mb, t[k][0], t[k][1], t[k][2], color);
            else mb_tri(&mb, t[k][0], t[k][2], t[k][1], color);
          }
        }
    }
  free(idx);
  return mb_finish(&mb);
}

int main(int argc, char** argv) {
  const int views = argc > 1 ? atoi(argv[1]) : 300;
  static const int RES[3][2] = { { 320, 240 }, { 640, 480 }, { 960, 720 } };
  if (!swr_init(960, 720)) { printf("swr_init fallita\n"); return 1; }
  int bad_views = 0;
  long long holes = 0, doubles = 0, depth_bad = 0, thread_diff = 0, clipped_tris = 0;
  int clipped_views = 0;
  uint32_t* ref = (uint32_t*)malloc(sizeof(uint32_t) * 960 * 720);
  for (int view = 0; view < views; ++view) {
    Mesh room = build_room();
    const int r = (int)(rnd() % 3);
    const int W = RES[r][0], H = RES[r][1];
    // Trasformazione rigida a caso dell'oggetto, e telecamera portata nello stesso spazio.
    Xform xf = xf_mul(xf_translate(v3(frange(-30, 30), frange(-30, 30), frange(-30, 30))),
                      xf_mul(xf_rot_y(frange(-3.1f, 3.1f)), xf_rot_x(frange(-3.1f, 3.1f))));
    if (view % 5 == 0) xf = xf_identity();
    const float margin = (view & 1) ? 0.45f : 2.0f;       // meta' delle viste rasenti alle pareti
    const Vec3 eye_l = v3(frange(-S + margin, S - margin), frange(-S + margin, S - margin), frange(-S + margin, S - margin));
    const float yaw = frange(-3.1f, 3.1f), pitch = frange(-1.3f, 1.3f);
    const Vec3 dir_l = v3(d_sin(yaw) * d_cos(pitch), d_sin(pitch), d_cos(yaw) * d_cos(pitch));
    const Vec3 eye = xf_point(xf, eye_l), target = xf_point(xf, eye_l + dir_l);
    // look_at usa la verticale del mondo: si evita di guardare quasi lungo di essa.
    const Vec3 d = normalize(target - eye);
    if (fabsf(d.y) > 0.97f) { mesh_free(&room); --view; continue; }

    DrawItem item;
    item.mesh = &room;
    item.xf = xf;
    item.flags = 0;
    FrameDesc f = FrameDesc();
    f.w = W;
    f.h = H;
    // Campo visivo limitato in modo che nessun punto visibile delle pareti stia prima del piano vicino.
    camera_setup(&f.cam, eye, target, frange(0.6f, 1.4f), (float)W / (float)H, 0.25f, 300.0f);
    f.items = &item;
    f.nitems = 1;
    f.light = normalize(v3(0.3f, 0.8f, 0.5f));
    f.sky_top = f.sky_bot = 0xFF00FF;
    f.hud = 0;
    f.hud_scale = 1;

    RenderStats st;
    memset(&st, 0, sizeof(st));
    swr_set_threads(1 + (int)(rnd() % 4));
    swr_debug_count_mode(true);
    const uint32_t* px = swr_render(&f, &st);
    long long h0 = 0, h2 = 0;
    for (int i = 0; i < W * H; ++i) {
      if (px[i] == 0) h0++;
      else if (px[i] != 1) h2++;
    }
    clipped_tris += swr_debug_clipped();
    if (swr_debug_clipped() > 0) clipped_views++;
    // Passata normale: profondita' nei limiti, e stessa immagine con 1 thread e con tutti.
    swr_debug_count_mode(false);
    memset(&st, 0, sizeof(st));
    swr_set_threads(1);
    px = swr_render(&f, &st);
    memcpy(ref, px, sizeof(uint32_t) * (size_t)W * H);
    const float* z = swr_debug_depth();
    // znear / w: al massimo 1 (piano vicino), al minimo la diagonale della stanza.
    const float zmax = 1.0f, zmin = 0.25f / (2.0f * S * 1.7321f + 0.5f);
    long long zb = 0;
    for (int i = 0; i < W * H; ++i)
      if (!(z[i] >= zmin * 0.999f && z[i] <= zmax * 1.001f)) zb++;
    memset(&st, 0, sizeof(st));
    swr_set_threads(swr_max_threads());
    px = swr_render(&f, &st);
    long long td = 0;
    for (int i = 0; i < W * H; ++i)
      if (px[i] != ref[i]) td++;
    if (h0 || h2 || zb || td) {
      bad_views++;
      if (bad_views <= 10)
        printf("vista %d (%dx%d): buchi=%lld doppi=%lld profondita' fuori limiti=%lld differenze tra thread=%lld\n", view, W, H, h0, h2, zb, td);
    }
    holes += h0;
    doubles += h2;
    depth_bad += zb;
    thread_diff += td;
    mesh_free(&room);
  }
  free(ref);
  swr_shutdown();
  printf("viste=%d fallite=%d | buchi=%lld doppi=%lld profondita'=%lld thread=%lld | viste con ritaglio=%d (%lld triangoli) | thread massimi=%d\n",
         views, bad_views, holes, doubles, depth_bad, thread_diff, clipped_views, clipped_tris, swr_max_threads());
  return bad_views ? 1 : 0;
}
