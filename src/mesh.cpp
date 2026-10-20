#include "mesh.h"
#include <stdlib.h>
#include <string.h>

static uint32_t g_next_mesh_id = 1;
static int g_overflow;

void mb_init(MeshBuilder* b) { memset(b, 0, sizeof(*b)); }

int mesh_overflow_count(void) { return g_overflow; }

int mb_vert(MeshBuilder* b, Vec3 p) {
  if (b->nv >= 65535) {          // gli indici sono a 16 bit: oltre si corromperebbe la mesh
    g_overflow++;
    return 0;
  }
  if (b->nv == b->capv) {
    b->capv = b->capv ? b->capv * 2 : 64;
    b->v = (Vec3*)realloc(b->v, sizeof(Vec3) * (size_t)b->capv);
  }
  b->v[b->nv] = p;
  return b->nv++;
}

void mb_tri(MeshBuilder* b, int i0, int i1, int i2, uint32_t rgb) {
  if (b->nt == b->capt) {
    b->capt = b->capt ? b->capt * 2 : 64;
    b->t = (MeshTri*)realloc(b->t, sizeof(MeshTri) * (size_t)b->capt);
  }
  MeshTri* t = &b->t[b->nt++];
  t->a = (uint16_t)i0; t->b = (uint16_t)i1; t->c = (uint16_t)i2;
  t->r = (uint8_t)(rgb >> 16); t->g = (uint8_t)(rgb >> 8); t->bl = (uint8_t)rgb; t->flags = b->flags;
  t->n = normalize(cross(b->v[i1] - b->v[i0], b->v[i2] - b->v[i0]));
}

void mb_quad(MeshBuilder* b, int i0, int i1, int i2, int i3, uint32_t rgb) {
  mb_tri(b, i0, i1, i2, rgb);
  mb_tri(b, i0, i2, i3, rgb);
}

// Parallelepipedo allineato agli assi, facce rivolte verso l'esterno.
void mb_box(MeshBuilder* b, Vec3 mn, Vec3 mx, uint32_t rgb) {
  int i[8];
  for (int k = 0; k < 8; ++k)
    i[k] = mb_vert(b, v3((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z));
  mb_quad(b, i[1], i[3], i[7], i[5], rgb);   // +X
  mb_quad(b, i[0], i[4], i[6], i[2], rgb);   // -X
  mb_quad(b, i[2], i[6], i[7], i[3], rgb);   // +Y
  mb_quad(b, i[0], i[1], i[5], i[4], rgb);   // -Y
  mb_quad(b, i[4], i[5], i[7], i[6], rgb);   // +Z
  mb_quad(b, i[0], i[2], i[3], i[1], rgb);   // -Z
}

Mesh mb_finish(MeshBuilder* b) {
  Mesh m;
  m.v = b->v; m.nv = b->nv; m.t = b->t; m.nt = b->nt;
  m.id = g_next_mesh_id++;
  Vec3 mn = v3(0, 0, 0), mx = v3(0, 0, 0);
  for (int i = 0; i < m.nv; ++i) {
    const Vec3 p = m.v[i];
    if (i == 0) { mn = p; mx = p; continue; }
    if (p.x < mn.x) mn.x = p.x;
    if (p.y < mn.y) mn.y = p.y;
    if (p.z < mn.z) mn.z = p.z;
    if (p.x > mx.x) mx.x = p.x;
    if (p.y > mx.y) mx.y = p.y;
    if (p.z > mx.z) mx.z = p.z;
  }
  m.center = (mn + mx) * 0.5f;
  float r2 = 0.0f;
  for (int i = 0; i < m.nv; ++i) {
    const Vec3 d = m.v[i] - m.center;
    const float l2 = dot(d, d);
    if (l2 > r2) r2 = l2;
  }
  m.radius = sqrtf(r2) * 1.0001f + 1e-4f;
  memset(b, 0, sizeof(*b));
  return m;
}

void mesh_free(Mesh* m) {
  free(m->v);
  free(m->t);
  memset(m, 0, sizeof(*m));
}

// --- forme ------------------------------------------------------------------

// Triangolo di un solido convesso, girato in modo da guardare lontano da ctr.
static void tri_out(MeshBuilder* b, int i0, int i1, int i2, Vec3 ctr, uint32_t rgb) {
  const Vec3 n = cross(b->v[i1] - b->v[i0], b->v[i2] - b->v[i0]);
  const Vec3 mid = (b->v[i0] + b->v[i1] + b->v[i2]) * (1.0f / 3.0f);
  if (dot(n, mid - ctr) < 0.0f) mb_tri(b, i0, i2, i1, rgb);
  else mb_tri(b, i0, i1, i2, rgb);
}

void mb_ellipsoid(MeshBuilder* b, Vec3 c, Vec3 r, int seg_u, int seg_v, uint32_t rgb) {
  if (seg_u < 3) seg_u = 3;
  if (seg_v < 2) seg_v = 2;
  const int top = mb_vert(b, v3(c.x, c.y + r.y, c.z));
  const int first_ring = b->nv;
  for (int k = 1; k < seg_v; ++k) {
    const float th = D_PI * (float)k / (float)seg_v, st = d_sin(th), ct = d_cos(th);
    for (int j = 0; j < seg_u; ++j) {
      const float ph = D_TWO_PI * (float)j / (float)seg_u;
      mb_vert(b, v3(c.x + r.x * st * d_cos(ph), c.y + r.y * ct, c.z + r.z * st * d_sin(ph)));
    }
  }
  const int bottom = mb_vert(b, v3(c.x, c.y - r.y, c.z));
  const int rings = seg_v - 1;
  for (int j = 0; j < seg_u; ++j) {
    const int j1 = (j + 1) % seg_u;
    tri_out(b, top, first_ring + j, first_ring + j1, c, rgb);
    for (int k = 0; k + 1 < rings; ++k) {
      const int a0 = first_ring + k * seg_u + j, a1 = first_ring + k * seg_u + j1;
      const int b0 = a0 + seg_u, b1 = a1 + seg_u;
      tri_out(b, a0, b0, b1, c, rgb);
      tri_out(b, a0, b1, a1, c, rgb);
    }
    const int l0 = first_ring + (rings - 1) * seg_u + j, l1 = first_ring + (rings - 1) * seg_u + j1;
    tri_out(b, bottom, l1, l0, c, rgb);
  }
}

void mb_cylinder(MeshBuilder* b, Vec3 base, float r0, float r1, float h, int seg, uint32_t rgb) {
  if (seg < 3) seg = 3;
  const Vec3 ctr = v3(base.x, base.y + 0.5f * h, base.z);
  const int bc = mb_vert(b, base);
  const int tc = mb_vert(b, v3(base.x, base.y + h, base.z));
  const int lo = b->nv;
  for (int j = 0; j < seg; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)seg;
    mb_vert(b, v3(base.x + r0 * d_cos(ph), base.y, base.z + r0 * d_sin(ph)));
  }
  const int hi = b->nv;
  if (r1 > 0.0f)
    for (int j = 0; j < seg; ++j) {
      const float ph = D_TWO_PI * (float)j / (float)seg;
      mb_vert(b, v3(base.x + r1 * d_cos(ph), base.y + h, base.z + r1 * d_sin(ph)));
    }
  for (int j = 0; j < seg; ++j) {
    const int j1 = (j + 1) % seg;
    tri_out(b, bc, lo + j1, lo + j, ctr, rgb);                 // fondo
    if (r1 > 0.0f) {
      tri_out(b, lo + j, lo + j1, hi + j1, ctr, rgb);
      tri_out(b, lo + j, hi + j1, hi + j, ctr, rgb);
      tri_out(b, tc, hi + j, hi + j1, ctr, rgb);              // coperchio
    } else {
      tri_out(b, lo + j, lo + j1, tc, ctr, rgb);              // cono
    }
  }
}

void mb_bevel_box(MeshBuilder* b, Vec3 mn, Vec3 mx, float bev, uint32_t rgb) {
  const float lo[3] = { mn.x, mn.y, mn.z }, hi[3] = { mx.x, mx.y, mx.z };
  float bv = bev;
  for (int a = 0; a < 3; ++a)
    if (bv > 0.45f * (hi[a] - lo[a])) bv = 0.45f * (hi[a] - lo[a]);
  // 24 vertici: 4 per faccia, rientrati di bv sugli altri due assi.
  int idx[3][2][2][2];   // [asse][lato][segno asse (a+1)%3][segno asse (a+2)%3]
  for (int a = 0; a < 3; ++a)
    for (int s = 0; s < 2; ++s)
      for (int su = 0; su < 2; ++su)
        for (int sw = 0; sw < 2; ++sw) {
          float p[3];
          const int u = (a + 1) % 3, w = (a + 2) % 3;
          p[a] = s ? hi[a] : lo[a];
          p[u] = su ? hi[u] - bv : lo[u] + bv;
          p[w] = sw ? hi[w] - bv : lo[w] + bv;
          idx[a][s][su][sw] = mb_vert(b, v3(p[0], p[1], p[2]));
        }
  const Vec3 ctr = (mn + mx) * 0.5f;
  // Vertice della faccia (a, s) che sta dalla parte "segni" sugli altri due assi.
  auto V = [&](int a, int s, const int sg[3]) { return idx[a][s][sg[(a + 1) % 3]][sg[(a + 2) % 3]]; };
  for (int a = 0; a < 3; ++a)
    for (int s = 0; s < 2; ++s) {
      const int q0 = idx[a][s][0][0], q1 = idx[a][s][1][0], q2 = idx[a][s][1][1], q3 = idx[a][s][0][1];
      tri_out(b, q0, q1, q2, ctr, rgb);
      tri_out(b, q0, q2, q3, ctr, rgb);
    }
  for (int a = 0; a < 3; ++a)
    for (int u = a + 1; u < 3; ++u) {
      const int w = 3 - a - u;
      for (int s = 0; s < 2; ++s)
        for (int su = 0; su < 2; ++su) {
          int g0[3], g1[3];
          g0[a] = s; g0[u] = su; g0[w] = 0;
          g1[a] = s; g1[u] = su; g1[w] = 1;
          const int p1 = V(a, s, g0), p2 = V(a, s, g1), p3 = V(u, su, g1), p4 = V(u, su, g0);
          tri_out(b, p1, p2, p3, ctr, rgb);
          tri_out(b, p1, p3, p4, ctr, rgb);
        }
    }
  for (int sx = 0; sx < 2; ++sx)
    for (int sy = 0; sy < 2; ++sy)
      for (int sz = 0; sz < 2; ++sz) {
        const int sg[3] = { sx, sy, sz };
        tri_out(b, V(0, sx, sg), V(1, sy, sg), V(2, sz, sg), ctr, rgb);
      }
}

void mb_disc(MeshBuilder* b, Vec3 c, float r, int seg, uint32_t rgb) {
  if (seg < 3) seg = 3;
  const int ci = mb_vert(b, c);
  const int first = b->nv;
  for (int j = 0; j < seg; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)seg;
    mb_vert(b, v3(c.x + r * d_cos(ph), c.y, c.z + r * d_sin(ph)));
  }
  for (int j = 0; j < seg; ++j) mb_tri(b, ci, first + (j + 1) % seg, first + j, rgb);   // verso l'alto
}

void mb_ring(MeshBuilder* b, Vec3 c, float r0, float r1, int seg, uint32_t rgb) {
  if (seg < 3) seg = 3;
  const int first = b->nv;
  for (int j = 0; j < seg; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)seg, cp = d_cos(ph), sp = d_sin(ph);
    mb_vert(b, v3(c.x + r0 * cp, c.y, c.z + r0 * sp));
    mb_vert(b, v3(c.x + r1 * cp, c.y, c.z + r1 * sp));
  }
  for (int j = 0; j < seg; ++j) {
    const int j1 = (j + 1) % seg;
    const int i0 = first + 2 * j, o0 = i0 + 1, i1 = first + 2 * j1, o1 = i1 + 1;
    mb_tri(b, i0, i1, o1, rgb);
    mb_tri(b, i0, o1, o0, rgb);
  }
}

void mb_apply(MeshBuilder* b, int first_v, int first_t, const Xform& xf) {
  for (int i = first_v; i < b->nv; ++i) b->v[i] = xf_point(xf, b->v[i]);
  for (int i = first_t; i < b->nt; ++i) {
    MeshTri& t = b->t[i];
    t.n = normalize(cross(b->v[t.b] - b->v[t.a], b->v[t.c] - b->v[t.a]));
  }
}

void mb_outline(MeshBuilder* b, int first_t, float t, uint32_t rgb) {
  const int nt = b->nt, nv = b->nv;
  if (first_t >= nt) return;
  Vec3* acc = (Vec3*)calloc((size_t)nv, sizeof(Vec3));
  int* map = (int*)malloc(sizeof(int) * (size_t)nv);
  if (!acc || !map) { free(acc); free(map); return; }
  for (int i = 0; i < nv; ++i) map[i] = -1;
  for (int i = first_t; i < nt; ++i) {
    const MeshTri tr = b->t[i];
    const Vec3 n = cross(b->v[tr.b] - b->v[tr.a], b->v[tr.c] - b->v[tr.a]);
    acc[tr.a] = acc[tr.a] + n;
    acc[tr.b] = acc[tr.b] + n;
    acc[tr.c] = acc[tr.c] + n;
  }
  for (int i = first_t; i < nt; ++i) {
    const MeshTri tr = b->t[i];
    const int ids[3] = { tr.a, tr.b, tr.c };
    for (int k = 0; k < 3; ++k)
      if (map[ids[k]] < 0) map[ids[k]] = mb_vert(b, b->v[ids[k]] + normalize(acc[ids[k]]) * t);
  }
  const uint8_t saved = b->flags;
  b->flags = (uint8_t)(saved | TRI_UNLIT);
  for (int i = first_t; i < nt; ++i) {
    const MeshTri tr = b->t[i];
    mb_tri(b, map[tr.a], map[tr.c], map[tr.b], rgb);
  }
  b->flags = saved;
  free(acc);
  free(map);
}
