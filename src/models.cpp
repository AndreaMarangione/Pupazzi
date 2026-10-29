// Modelli in stile cartone, costruiti con forme arrotondate (ellissoidi, cilindri, scatole
// smussate) e un contorno scuro a "scafo rovesciato". Tutti guardano verso +Z, origine ai piedi.
// Dalla 0.5: gli eroi del sistema solare, i mostri e il boss della Terra, la capsula con
// l'esploratore, i colpi nuovi (fucilone, razzi, cristalli di luna, ghiaccio secco, giavellotti).
#include "models.h"
#include <string.h>

Models M;
const uint32_t PLAYER_RGB[NSETS] = { 0xE8463C, 0x3C82E8, 0x3FC04A, 0xF2C230, 0x8A8E94 };
const uint32_t PW_RGB[4] = { 0x3FD8FF, 0xFF4A3A, 0xFFC83C, 0xB06CFF };
const uint32_t PARTICLE_RGB[NPCOL] = { 0xE8463C, 0x3C82E8, 0x3FC04A, 0xF2C230, 0xFFFFFF, 0xFFD23C, 0x4CD964, 0x9A9EA4,
                                       0xB57A45, 0xF0D060, 0xEAF6FF, 0xFF7A1F, 0x2A2630, 0x5DBF4F, 0xE04040, 0x8A6A4A,
                                       0xD5EFA2, 0xE8892A, 0xC67C4E, 0x22B8A0, 0xF7EED9, 0xE6C68A, 0xA89A88, 0xD9A040,
                                       0xD9CBFF, 0x6FEAFF, 0xFFF07A };

uint32_t rgb_mix(uint32_t a, uint32_t b, float t) {
  uint32_t o = 0;
  for (int sh = 16; sh >= 0; sh -= 8) {
    const float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
    o |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
  }
  return o;
}

namespace {

const uint32_t INK = 0x1B1622;        // contorno di personaggi e mostri
const float OL = 0.03f;               // spessore del contorno

MeshBuilder B;                        // costruttore in uso
int g_mark;                           // primo triangolo ancora senza contorno

void begin(void) { mb_init(&B); g_mark = 0; }
Mesh end(void) { return mb_finish(&B); }
void outline(float t = OL, uint32_t c = INK) {
  mb_outline(&B, g_mark, t, c);
  g_mark = B.nt;
}
void skip_outline(void) { g_mark = B.nt; }
void unlit(bool on) { B.flags = on ? TRI_UNLIT : 0; }

void ell(float x, float y, float z, float rx, float ry, float rz, uint32_t c, int su = 8, int sv = 5) {
  mb_ellipsoid(&B, v3(x, y, z), v3(rx, ry, rz), su, sv, c);
}
void ball(float x, float y, float z, float r, uint32_t c, int su = 6, int sv = 4) { ell(x, y, z, r, r, r, c, su, sv); }
void bbox(float x0, float y0, float z0, float x1, float y1, float z1, float bev, uint32_t c) {
  if (bev > 0.0f) mb_bevel_box(&B, v3(x0, y0, z0), v3(x1, y1, z1), bev, c);
  else mb_box(&B, v3(x0, y0, z0), v3(x1, y1, z1), c);
}
// Cilindro o cono (r1 = 0) dalla base lungo la direzione dir.
void cyl(Vec3 base, Vec3 dir, float r0, float r1, float len, uint32_t c, int seg = 6) {
  const int fv = B.nv, ft = B.nt;
  mb_cylinder(&B, v3(0, 0, 0), r0, r1, len, seg, c);
  mb_apply(&B, fv, ft, xf_mul(xf_translate(base), xf_align_y(normalize(dir))));
}
void cone(Vec3 base, Vec3 dir, float r, float len, uint32_t c, int seg = 5) { cyl(base, dir, r, 0.0f, len, c, seg); }
// Ellissoide ruotato: semiassi r lungo gli assi locali, poi la rotazione rot.
void ell_rot(Vec3 c, Vec3 r, const Xform& rot, uint32_t col, int su = 6, int sv = 4) {
  const int fv = B.nv, ft = B.nt;
  mb_ellipsoid(&B, v3(0, 0, 0), r, su, sv, col);
  mb_apply(&B, fv, ft, xf_mul(xf_translate(c), rot));
}
// Prisma aperto (senza basi) da a a b, per sbarre e fili: le estremita' finiscono dentro altri pezzi.
void tube(Vec3 a, Vec3 b, float r, uint32_t c, int seg = 6) {
  const int fv = B.nv, ft = B.nt;
  const float len = length(b - a);
  for (int j = 0; j < seg; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)seg;
    mb_vert(&B, v3(r * d_cos(ph), 0.0f, r * d_sin(ph)));
    mb_vert(&B, v3(r * d_cos(ph), len, r * d_sin(ph)));
  }
  for (int j = 0; j < seg; ++j) {
    const int a0 = fv + 2 * j, a1 = fv + 2 * ((j + 1) % seg);
    mb_quad(&B, a0, a0 + 1, a1 + 1, a1, c);
  }
  mb_apply(&B, fv, ft, xf_mul(xf_translate(a), xf_align_y(normalize(b - a))));
}
// Parallelepipedo di semilati h centrato in c, ruotato da rot.
void box_rot(Vec3 c, Vec3 h, const Xform& rot, uint32_t col, float bev = 0.0f) {
  const int fv = B.nv, ft = B.nt;
  bbox(-h.x, -h.y, -h.z, h.x, h.y, h.z, bev, col);
  mb_apply(&B, fv, ft, xf_mul(xf_translate(c), rot));
}
// Puntino di luce da cartone (occhi, vetri, metalli): bianco e senza luce.
void shine(float x, float y, float z, float r, uint32_t c = 0xFFFFFF) {
  unlit(true);
  ell(x, y, z, r, r, r * 0.5f, c, 4, 2);
  unlit(false);
}
// Occhio da cartone: bianco con la pupilla e il puntino di luce, rivolto verso +Z.
void eye(float x, float y, float z, float r, float pupil, uint32_t white = 0xFFFFFF, uint32_t pup = 0x1A1520, bool hi = true) {
  ell(x, y, z, r, r * 1.15f, r * 0.6f, white, 6, 4);
  ell(x, y - r * 0.1f, z + r * 0.45f, pupil, pupil * 1.2f, pupil * 0.6f, pup, 5, 3);
  if (hi) shine(x - pupil * 0.35f, y + pupil * 0.3f, z + r * 0.45f + pupil * 0.62f, pupil * 0.36f);
}

// Triangolo rivolto lontano da ctr (verso ctr se in = true).
void tri_from(int i0, int i1, int i2, Vec3 ctr, uint32_t c, bool in = false) {
  const Vec3 n = cross(B.v[i1] - B.v[i0], B.v[i2] - B.v[i0]);
  const Vec3 mid = (B.v[i0] + B.v[i1] + B.v[i2]) * (1.0f / 3.0f);
  const bool out = dot(n, mid - ctr) >= 0.0f;
  if (out != in) mb_tri(&B, i0, i1, i2, c);
  else mb_tri(&B, i0, i2, i1, c);
}

// Guscio di ellissoide con un'apertura rotonda attorno alla direzione hole (coseno dell'apertura
// hc; hc > 1: nessuna apertura). in = true: la parete di dentro, che si vede dall'apertura.
void shell(Vec3 c, Vec3 r, int su, int sv, Vec3 hole, float hc, uint32_t col, bool in) {
  const int first = B.nv;
  for (int k = 0; k <= sv; ++k) {
    const float th = D_PI * (float)k / (float)sv, st = d_sin(th), ct = d_cos(th);
    for (int j = 0; j < su; ++j) {
      const float ph = D_TWO_PI * (float)j / (float)su;
      mb_vert(&B, v3(c.x + r.x * st * d_cos(ph), c.y + r.y * ct, c.z + r.z * st * d_sin(ph)));
    }
  }
  for (int k = 0; k < sv; ++k)
    for (int j = 0; j < su; ++j) {
      const int j1 = (j + 1) % su;
      const int a = first + k * su + j, b = first + k * su + j1, cc = first + (k + 1) * su + j1, d = first + (k + 1) * su + j;
      const Vec3 mid = (B.v[a] + B.v[b] + B.v[cc] + B.v[d]) * 0.25f - c;
      if (dot(normalize(v3(mid.x / r.x, mid.y / r.y, mid.z / r.z)), hole) > hc) continue;
      if (k > 0) tri_from(a, b, cc, c, col, in);            // al polo i primi due vertici coincidono
      if (k < sv - 1) tri_from(a, cc, d, c, col, in);
    }
}

// Ciambella attorno all'asse Y locale (raggio R, spessore rt), portata da xf.
void torus(const Xform& xf, float R, float rt, int su, int sv, uint32_t col) {
  const int fv = B.nv, ft = B.nt;
  for (int j = 0; j < su; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)su;
    for (int k = 0; k < sv; ++k) {
      const float th = D_TWO_PI * (float)k / (float)sv, rr = R + rt * d_cos(th);
      mb_vert(&B, v3(rr * d_cos(ph), rt * d_sin(th), rr * d_sin(ph)));
    }
  }
  for (int j = 0; j < su; ++j) {
    const float pm = D_TWO_PI * ((float)j + 0.5f) / (float)su;
    const Vec3 ctr = v3(R * d_cos(pm), 0.0f, R * d_sin(pm));
    for (int k = 0; k < sv; ++k) {
      const int j1 = (j + 1) % su, k1 = (k + 1) % sv;
      const int a = fv + j * sv + k, b = fv + j1 * sv + k, c = fv + j1 * sv + k1, d = fv + j * sv + k1;
      tri_from(a, b, c, ctr, col);
      tri_from(a, c, d, ctr, col);
    }
  }
  mb_apply(&B, fv, ft, xf);
}

// Poligono convesso (x, y) estruso lungo Z da z0 a z1, portato da xf.
void prism(const float (*p)[2], int n, float z0, float z1, const Xform& xf, uint32_t col) {
  const int fv = B.nv, ft = B.nt;
  float cx = 0.0f, cy = 0.0f;
  for (int k = 0; k < n; ++k) { cx += p[k][0]; cy += p[k][1]; }
  cx /= (float)n;
  cy /= (float)n;
  const Vec3 ctr = v3(cx, cy, 0.5f * (z0 + z1));
  const int c0 = mb_vert(&B, v3(cx, cy, z0)), c1 = mb_vert(&B, v3(cx, cy, z1));
  const int base = B.nv;
  for (int k = 0; k < n; ++k) {
    mb_vert(&B, v3(p[k][0], p[k][1], z0));
    mb_vert(&B, v3(p[k][0], p[k][1], z1));
  }
  for (int k = 0; k < n; ++k) {
    const int a0 = base + 2 * k, a1 = a0 + 1, b0 = base + 2 * ((k + 1) % n), b1 = b0 + 1;
    tri_from(c0, a0, b0, ctr, col);
    tri_from(c1, a1, b1, ctr, col);
    tri_from(a0, b0, b1, ctr, col);
    tri_from(a0, b1, a1, ctr, col);
  }
  mb_apply(&B, fv, ft, xf);
}

// Lama a sezione di rombo lungo +Z: dalla base (z0, mezza larghezza w0) alla punta in z2,
// larghezza w1 in z1, spessore della costola h (lungo X). Due toni di luce: si legge come metallo.
void blade(float z0, float w0, float z1, float w1, float z2, float h, const Xform& xf, uint32_t col) {
  const int fv = B.nv, ft = B.nt;
  const float zs[2] = { z0, z1 }, ws[2] = { w0, w1 };
  int v[2][4];
  for (int s = 0; s < 2; ++s) {
    v[s][0] = mb_vert(&B, v3(0, -ws[s], zs[s]));
    v[s][1] = mb_vert(&B, v3(h, 0, zs[s]));
    v[s][2] = mb_vert(&B, v3(0, ws[s], zs[s]));
    v[s][3] = mb_vert(&B, v3(-h, 0, zs[s]));
  }
  const int tip = mb_vert(&B, v3(0, 0, z2));
  const Vec3 ctr = v3(0, 0, 0.5f * (z0 + z1));
  for (int k = 0; k < 4; ++k) {
    const int k1 = (k + 1) & 3;
    tri_from(v[0][k], v[0][k1], v[1][k1], ctr, col);
    tri_from(v[0][k], v[1][k1], v[1][k], ctr, col);
    tri_from(v[1][k], v[1][k1], tip, v3(0, 0, z1), col);
  }
  tri_from(v[0][0], v[0][1], v[0][2], v3(0, 0, z1), col);
  tri_from(v[0][0], v[0][2], v[0][3], v3(0, 0, z1), col);
  mb_apply(&B, fv, ft, xf);
}

// Superficie di rotazione chiusa ai due capi (il primo e l'ultimo punto del profilo hanno raggio 0):
// pr[k] = (y, raggio); l'asse si piega di (bx, bz) con il quadrato dell'altezza sopra y_bend (la punta
// della fiamma che si arriccia); zk schiaccia le sezioni lungo Z. Portata da xf. cols (se c'e'): un
// colore per ogni fascia tra due punti del profilo (n - 1), per le sfumature a gradini.
void lathe(const float (*pr)[2], int n, int seg, float bx, float bz, float y_bend, float zk, const Xform& xf, uint32_t col,
           const uint32_t* cols = 0) {
  const int fv = B.nv, ft = B.nt;
  const float ytop = pr[n - 1][0];
  auto axis = [&](float y) {
    const float t = clampf((y - y_bend) / (ytop - y_bend), 0.0f, 1.0f);
    return v3(bx * t * t, y, bz * t * t);
  };
  const int bot = mb_vert(&B, axis(pr[0][0]));
  const int first = B.nv;
  for (int k = 1; k < n - 1; ++k) {
    const Vec3 c = axis(pr[k][0]);
    for (int j = 0; j < seg; ++j) {
      const float ph = D_TWO_PI * (float)j / (float)seg;
      mb_vert(&B, v3(c.x + pr[k][1] * d_cos(ph), c.y, c.z + zk * pr[k][1] * d_sin(ph)));
    }
  }
  const int top = mb_vert(&B, axis(ytop));
  const int rings = n - 2;
  for (int j = 0; j < seg; ++j) {
    const int j1 = (j + 1) % seg;
    tri_from(bot, first + j, first + j1, axis(pr[1][0]), cols ? cols[0] : col);
    for (int k = 0; k + 1 < rings; ++k) {
      const Vec3 c = axis(0.5f * (pr[k + 1][0] + pr[k + 2][0]));
      const int a0 = first + k * seg + j, a1 = first + k * seg + j1, b0 = a0 + seg, b1 = a1 + seg;
      tri_from(a0, b0, b1, c, cols ? cols[k + 1] : col);
      tri_from(a0, b1, a1, c, cols ? cols[k + 1] : col);
    }
    tri_from(top, first + (rings - 1) * seg + j1, first + (rings - 1) * seg + j, axis(pr[n - 2][0]), cols ? cols[n - 2] : col);
  }
  mb_apply(&B, fv, ft, xf);
}

Mesh flat_ring(float r0, float r1, int seg, uint32_t c, float y = 0.03f) {
  begin();
  unlit(true);
  mb_ring(&B, v3(0, y, 0), r0, r1, seg, c);
  return end();
}
Mesh flat_disc(float r, int seg, uint32_t c, float y = 0.02f) {
  begin();
  unlit(true);
  mb_disc(&B, v3(0, y, 0), r, seg, c);
  return end();
}

// --- eroi -------------------------------------------------------------------------

// ASTRO, astronauta della Terra (equilibrato): tuta bianca imbottita con i dettagli arancio e blu,
// casco tondo con la visiera dorata, zainetto con le bombole, stivali lunari; il fucilone fa
// parte del braccio destro e punta avanti quando il braccio si alza. Colore del giocatore:
// spalline, cintura e la striscia sul casco (si vede bene dall'alto).
void build_astro(CharMesh& m) {
  const uint32_t suit = 0xF2F4F8, suit2 = 0xC9D2DE, orange = 0xFF8126, blue = 0x2E6BD6, visor = 0xF7B53A;
  const uint32_t boot = 0xD5DCE6, sole = 0x3B4250, gun = 0x4A5562, gun2 = 0x2C333C, cell = 0x7CF4FF;
  m.sh_x = 0.29f; m.sh_y = 0.80f; m.hip_x = 0.12f; m.hip_y = 0.36f; m.arm_len = 0.40f; m.top = 1.5f;
  m.rest_r = -1.05f; m.swing_r = 0.2f;
  begin();
  ell(0, 0.62f, 0, 0.28f, 0.31f, 0.235f, suit, 8, 5);               // tuta: y 0.31..0.93
  bbox(-0.19f, 0.47f, -0.35f, 0.19f, 0.9f, -0.13f, 0.06f, suit2);     // zainetto
  cyl(v3(-0.1f, 0.5f, -0.37f), v3(0, 1, 0), 0.055f, 0.055f, 0.36f, orange, 6);   // bombole
  cyl(v3(0.1f, 0.5f, -0.37f), v3(0, 1, 0), 0.055f, 0.055f, 0.36f, orange, 6);
  ell(0, 1.15f, 0, 0.32f, 0.30f, 0.31f, suit, 10, 5);               // casco: y 0.85..1.45
  outline();
  cyl(v3(0, 0.84f, 0), v3(0, 1, 0), 0.215f, 0.2f, 0.08f, orange, 8);   // anello del collo
  cyl(v3(-0.3f, 1.15f, 0), v3(-1, 0, 0), 0.085f, 0.085f, 0.05f, blue, 6);   // dischi ai lati del casco
  cyl(v3(0.3f, 1.15f, 0), v3(1, 0, 0), 0.085f, 0.085f, 0.05f, blue, 6);
  torus(xf_mul(xf_translate(v3(0, 1.13f, 0.222f)), xf_rot_x(D_HALF_PI)), 0.205f, 0.034f, 12, 4, blue);   // bordo della visiera
  ell(0, 1.13f, 0.155f, 0.21f, 0.2f, 0.17f, visor, 10, 5);          // visiera dorata
  unlit(true);
  ell_rot(v3(-0.085f, 1.215f, 0.292f), v3(0.07f, 0.02f, 0.02f), xf_rot_z(0.55f), 0xFFFFFF, 5, 2);   // riflessi
  ell(0.1f, 1.055f, 0.3f, 0.022f, 0.022f, 0.012f, 0xFFF6D8, 4, 2);
  unlit(false);
  bbox(-0.11f, 0.53f, 0.17f, 0.11f, 0.73f, 0.25f, 0.0f, suit2);      // pannello sul petto
  ball(-0.05f, 0.665f, 0.252f, 0.026f, 0xE8463C, 4, 3);
  ball(0.04f, 0.665f, 0.252f, 0.026f, blue, 4, 3);
  bbox(-0.07f, 0.565f, 0.245f, 0.07f, 0.595f, 0.258f, 0.0f, orange);
  skip_outline();
  m.body = end();

  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.14f, 0, 0.09f, 0.17f, 0.09f, suit, 6, 4);            // manica imbottita
    cyl(v3(0, -0.29f, 0), v3(0, -1, 0), 0.088f, 0.088f, 0.05f, orange, 6);   // polsino
    ell(0, -0.385f, 0.01f, 0.085f, 0.08f, 0.085f, blue, 5, 4);     // guanto
    outline();
    if (s == 1) {
      // Fucilone: costruito con la canna verso +Z e il sopra verso +Y (impugnatura nell'origine),
      // poi portato nel braccio: canna lungo il braccio (verso -Y), sopra verso +Z, un po' verso
      // il corpo (i colpi partono dal centro).
      const Xform rf = xf_mul(xf_translate(v3(0, -0.385f, 0.0f)), xf_mul(xf_rot_z(-0.16f), xf_rot_x(D_HALF_PI)));
      int fv = B.nv, ft = B.nt;
      bbox(-0.07f, 0.03f, -0.16f, 0.07f, 0.19f, 0.3f, 0.04f, gun);          // corpo
      bbox(-0.05f, 0.03f, -0.38f, 0.05f, 0.16f, -0.14f, 0.0f, orange);      // calcio
      cyl(v3(0, 0.11f, 0.28f), v3(0, 0, 1), 0.06f, 0.06f, 0.2f, gun2, 6);    // canna
      cyl(v3(0, 0.11f, 0.46f), v3(0, 0, 1), 0.08f, 0.08f, 0.07f, orange, 6); // bocca
      bbox(-0.035f, -0.12f, -0.07f, 0.035f, 0.05f, 0.03f, 0.0f, gun2);      // impugnatura
      mb_apply(&B, fv, ft, rf);
      outline(0.025f);
      fv = B.nv; ft = B.nt;
      unlit(true);
      cyl(v3(0, 0.19f, -0.1f), v3(0, 0, 1), 0.045f, 0.045f, 0.28f, cell, 6);   // cella di energia accesa
      unlit(false);
      bbox(-0.075f, 0.085f, 0.06f, 0.075f, 0.125f, 0.24f, 0.0f, orange);    // strisce ai lati
      bbox(-0.015f, 0.19f, 0.2f, 0.015f, 0.25f, 0.26f, 0.0f, gun2);         // mirino
      mb_apply(&B, fv, ft, rf);
      skip_outline();
      m.tip = xf_point(rf, v3(0, 0.11f, 0.56f));
    }
    (s ? m.arm_r : m.arm_l) = end();
  }
  begin();
  cyl(v3(0, 0, 0), v3(0, -1, 0), 0.095f, 0.09f, 0.2f, suit, 6);
  ell(0, -0.255f, 0.035f, 0.115f, 0.1f, 0.15f, boot, 6, 4);           // stivale lunare
  bbox(-0.105f, -0.36f, -0.1f, 0.105f, -0.31f, 0.18f, 0.0f, sole);
  outline();
  ell(0, -0.2f, 0.03f, 0.105f, 0.03f, 0.13f, orange, 6, 2);           // fascia arancio
  skip_outline();
  m.leg = end();
  for (int i = 0; i < 4; ++i) {
    const uint32_t c = PLAYER_RGB[i];
    begin();
    ell(-0.28f, 0.84f, 0, 0.115f, 0.075f, 0.125f, c, 6, 3);          // spalline
    ell(0.28f, 0.84f, 0, 0.115f, 0.075f, 0.125f, c, 6, 3);
    ell(0, 0.43f, 0, 0.235f, 0.05f, 0.2f, c, 8, 2);                   // cintura
    ell(0, 1.19f, -0.03f, 0.06f, 0.28f, 0.3f, c, 5, 6);               // striscia sul casco
    outline();
    m.accent[i] = end();
  }
}

// MOCHI, coniglio della Luna (resistente): tondo e morbido, lilla chiarissimo con la pancia
// bianca, orecchie lunghe (quella piegata e' a parte e ballonzola), occhioni, naso rosa, borsetta
// con un cristallo di luna. Colore del giocatore: sciarpa (con la coda dietro) e fiocco sull'orecchio.
void build_mochi(CharMesh& m) {
  const uint32_t fur = 0xE0D0FA, belly = 0xFFFFFF, pink = 0xF7A9CB, nose = 0xF0719F, pupil = 0x3B2257;
  const uint32_t bag = 0x6A57B8, strap = 0x8F74D8, crystal = 0xD6C8FF, cheek = 0xF6A0BE;
  m.sh_x = 0.27f; m.sh_y = 0.62f; m.hip_x = 0.15f; m.hip_y = 0.17f; m.arm_len = 0.27f; m.top = 1.6f;
  m.rest_r = 0.0f; m.swing_r = 0.9f;
  begin();
  ell(0, 0.43f, 0, 0.34f, 0.36f, 0.31f, fur, 8, 5);                  // corpo: y 0.07..0.79
  ell(0, 0.97f, 0.02f, 0.31f, 0.27f, 0.28f, fur, 9, 6);              // testa: y 0.70..1.24
  ell_rot(v3(-0.12f, 1.42f, -0.03f), v3(0.075f, 0.24f, 0.055f), xf_rot_z(0.14f), fur, 6, 5);   // orecchio dritto
  ell_rot(v3(0.115f, 1.32f, -0.03f), v3(0.072f, 0.14f, 0.053f), xf_rot_z(-0.18f), fur, 6, 4);   // base dell'orecchio piegato
  ball(0, 0.33f, -0.31f, 0.1f, belly, 5, 3);                          // codino
  bbox(0.24f, 0.24f, -0.07f, 0.38f, 0.44f, 0.11f, 0.0f, bag);         // borsetta
  outline();
  ell(0, 0.40f, 0.12f, 0.25f, 0.27f, 0.21f, belly, 8, 4);             // pancia bianca
  ell_rot(v3(-0.115f, 1.43f, 0.008f), v3(0.042f, 0.18f, 0.028f), xf_rot_z(0.14f), pink, 5, 3);   // dentro l'orecchio
  ell_rot(v3(0.11f, 1.33f, 0.008f), v3(0.04f, 0.1f, 0.026f), xf_rot_z(-0.18f), pink, 5, 3);
  eye(-0.115f, 1.0f, 0.252f, 0.085f, 0.058f, 0xFFFFFF, pupil);
  eye(0.115f, 1.0f, 0.252f, 0.085f, 0.058f, 0xFFFFFF, pupil);
  ell(0, 0.915f, 0.293f, 0.038f, 0.026f, 0.022f, nose, 5, 3);
  ell(0, 0.878f, 0.283f, 0.042f, 0.009f, 0.012f, pupil, 5, 2);        // bocca
  bbox(-0.022f, 0.835f, 0.262f, 0.022f, 0.872f, 0.287f, 0.0f, belly);  // dentini
  ell(-0.2f, 0.92f, 0.215f, 0.05f, 0.03f, 0.025f, cheek, 5, 2);
  ell(0.2f, 0.92f, 0.215f, 0.05f, 0.03f, 0.025f, cheek, 5, 2);
  // Tracolla sopra la pancia e cristallo di luna che spunta dalla borsetta (sempre acceso).
  tube(v3(0.3f, 0.44f, 0.08f), v3(0.13f, 0.6f, 0.3f), 0.018f, strap, 4);
  tube(v3(0.13f, 0.6f, 0.3f), v3(-0.12f, 0.72f, 0.245f), 0.018f, strap, 4);
  tube(v3(-0.12f, 0.72f, 0.245f), v3(-0.25f, 0.76f, 0.0f), 0.018f, strap, 4);
  unlit(true);
  ell_rot(v3(0.33f, 0.5f, 0.03f), v3(0.045f, 0.1f, 0.045f), xf_rot_z(-0.25f), crystal, 4, 2);
  ell_rot(v3(0.27f, 0.47f, -0.01f), v3(0.03f, 0.065f, 0.03f), xf_rot_z(0.35f), 0xF2EDFF, 4, 2);
  unlit(false);
  skip_outline();
  m.body = end();
  // Meta' alta dell'orecchio piegato: ricade in fuori e in avanti, si muove da sola.
  m.fx_at = v3(0.14f, 1.44f, -0.03f);
  begin();
  {
    const Vec3 d = normalize(v3(0.8f, 0.3f, 0.35f));
    ell_rot(m.fx_at + d * 0.13f, v3(0.068f, 0.15f, 0.05f), xf_align_y(d), fur, 6, 4);
    outline();
    ell_rot(m.fx_at + d * 0.13f + v3(0, -0.02f, 0.03f), v3(0.04f, 0.11f, 0.025f), xf_align_y(d), pink, 5, 3);
    skip_outline();
  }
  m.fx = end();

  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.11f, 0.02f, 0.085f, 0.15f, 0.085f, fur, 6, 4);
    ball(0, -0.23f, 0.03f, 0.07f, belly, 5, 3);
    outline();
    (s ? m.arm_r : m.arm_l) = end();
  }
  m.tip = v3(0, -0.27f, 0.05f);
  begin();
  ell(0, -0.09f, 0.08f, 0.105f, 0.08f, 0.19f, fur, 7, 4);             // piedone da coniglio
  outline();
  m.leg = end();
  for (int i = 0; i < 4; ++i) {
    const uint32_t c = PLAYER_RGB[i], c2 = rgb_mix(c, 0x000000, 0.18f);
    begin();
    ell(0, 0.755f, 0.01f, 0.245f, 0.065f, 0.235f, c, 9, 3);           // sciarpa
    ell(0.11f, 0.71f, 0.225f, 0.06f, 0.05f, 0.04f, c, 5, 3);           // nodo
    ell_rot(v3(0.15f, 0.6f, 0.245f), v3(0.045f, 0.1f, 0.025f), xf_rot_z(0.3f), c2, 5, 3);
    ell_rot(v3(0.07f, 0.6f, -0.29f), v3(0.06f, 0.14f, 0.025f), xf_rot_x(-0.25f), c2, 5, 3);   // coda della sciarpa
    ell_rot(v3(-0.185f, 1.25f, 0.0f), v3(0.065f, 0.04f, 0.035f), xf_rot_z(0.5f), c, 4, 3);    // fiocco
    ell_rot(v3(-0.065f, 1.25f, 0.0f), v3(0.065f, 0.04f, 0.035f), xf_rot_z(-0.5f), c, 4, 3);
    ball(-0.125f, 1.245f, 0.01f, 0.035f, c2, 4, 2);
    outline(0.022f);
    m.accent[i] = end();
  }
}

// MARZIO, marziano delle calotte polari fatto di ghiaccio secco (controllo): tutto a faccette
// azzurro ghiaccio, macchie di polvere rossa di Marte, un occhione rosso, punte di brina; le due
// antenne con la punta accesa sono a parte e molleggiano. Colore del giocatore: cintura e placca.
void build_marzio(CharMesh& m) {
  const uint32_t ice = 0xBFE8FF, ice2 = 0x9AD3F2, ice_hi = 0xE2F5FF, rust = 0xC9573A, rust2 = 0xA8452E;
  const uint32_t iris = 0xE8452C, tip = 0xFF5A3C, frost = 0xF6FCFF, mouth = 0x23345A;
  m.sh_x = 0.29f; m.sh_y = 0.66f; m.hip_x = 0.14f; m.hip_y = 0.17f; m.arm_len = 0.36f; m.top = 1.58f;
  m.rest_r = 0.0f; m.swing_r = 0.9f;
  begin();
  ell(0, 0.46f, 0, 0.31f, 0.36f, 0.29f, ice, 7, 4);                   // corpo a faccette: y 0.10..0.82
  ell(0, 1.0f, 0.01f, 0.34f, 0.29f, 0.31f, ice_hi, 8, 5);             // testa: y 0.71..1.29
  cone(v3(-0.25f, 1.12f, -0.1f), v3(-0.7f, 0.8f, -0.3f), 0.06f, 0.15f, frost, 4);   // punte di brina
  cone(v3(-0.13f, 1.25f, -0.17f), v3(-0.3f, 1.0f, -0.5f), 0.05f, 0.13f, frost, 4);
  cone(v3(0.24f, 1.1f, -0.16f), v3(0.6f, 0.7f, -0.5f), 0.05f, 0.12f, frost, 4);
  cone(v3(-0.26f, 0.7f, -0.08f), v3(-0.9f, 0.6f, -0.2f), 0.05f, 0.12f, frost, 4);
  cone(v3(0.27f, 0.62f, -0.1f), v3(0.9f, 0.5f, -0.3f), 0.045f, 0.11f, frost, 4);
  outline();
  ell(-0.16f, 0.24f, 0.2f, 0.13f, 0.1f, 0.08f, rust, 5, 3);           // polvere rossa di Marte
  ell(0.19f, 0.16f, -0.13f, 0.12f, 0.08f, 0.1f, rust2, 5, 3);
  ell(0.1f, 0.62f, -0.22f, 0.11f, 0.08f, 0.08f, rust, 5, 3);
  ell(0.15f, 1.23f, -0.07f, 0.14f, 0.07f, 0.14f, rust, 5, 3);
  ell(-0.24f, 0.94f, 0.12f, 0.07f, 0.06f, 0.06f, rust2, 4, 3);
  // Occhione: bianco, iride rossa, pupilla, luce; sopracciglio di brina; bocca piccola.
  ell(0, 1.02f, 0.25f, 0.16f, 0.17f, 0.085f, 0xFFFFFF, 8, 5);
  ell(0, 1.0f, 0.315f, 0.1f, 0.105f, 0.035f, iris, 7, 4);
  ell(0, 0.995f, 0.347f, 0.05f, 0.062f, 0.02f, 0x1A1520, 6, 3);
  shine(-0.035f, 1.04f, 0.365f, 0.025f);
  shine(0.04f, 0.96f, 0.36f, 0.013f);
  ell(0, 1.17f, 0.235f, 0.165f, 0.045f, 0.075f, ice2, 7, 3);
  ell_rot(v3(-0.035f, 0.828f, 0.285f), v3(0.042f, 0.014f, 0.02f), xf_rot_z(-0.35f), mouth, 4, 2);   // sorriso
  ell_rot(v3(0.035f, 0.828f, 0.285f), v3(0.042f, 0.014f, 0.02f), xf_rot_z(0.35f), mouth, 4, 2);
  skip_outline();
  m.body = end();
  // Antenne con la punta accesa (molleggiano attorno a fx_at).
  m.fx_at = v3(0, 1.25f, -0.03f);
  begin();
  for (int s = -1; s <= 1; s += 2) {
    const float sx = (float)s;
    tube(v3(sx * 0.08f, 1.2f, -0.02f), v3(sx * 0.17f, 1.48f, -0.07f), 0.022f, ice2, 5);
    ball(sx * 0.08f, 1.24f, -0.02f, 0.045f, ice2, 5, 3);
  }
  outline(0.02f);
  unlit(true);
  for (int s = -1; s <= 1; s += 2) ball((float)s * 0.175f, 1.52f, -0.075f, 0.06f, tip, 6, 4);
  unlit(false);
  outline(0.018f, 0x5A1408);
  for (int s = -1; s <= 1; s += 2) shine((float)s * 0.175f - 0.02f, 1.545f, -0.02f, 0.016f);
  skip_outline();
  m.fx = end();

  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.14f, 0.0f, 0.085f, 0.17f, 0.085f, ice, 5, 3);
    ell(0, -0.31f, 0.02f, 0.08f, 0.075f, 0.08f, ice_hi, 5, 3);
    outline();
    (s ? m.arm_r : m.arm_l) = end();
  }
  m.tip = v3(0, -0.36f, 0.05f);
  begin();
  ell(0, -0.08f, 0.04f, 0.11f, 0.09f, 0.14f, ice2, 6, 3);
  outline();
  ell(0.02f, -0.04f, 0.09f, 0.06f, 0.035f, 0.06f, rust, 4, 2);
  skip_outline();
  m.leg = end();
  for (int i = 0; i < 4; ++i) {
    const uint32_t c = PLAYER_RGB[i];
    begin();
    ell(0, 0.40f, 0, 0.322f, 0.055f, 0.302f, c, 7, 2);                // cintura
    cyl(v3(0, 0.40f, 0.27f), v3(0, 0, 1), 0.08f, 0.08f, 0.05f, rgb_mix(c, 0xFFFFFF, 0.35f), 7);   // placca
    outline(0.022f);
    ell(0, 0.40f, 0.322f, 0.035f, 0.035f, 0.008f, 0xFFFFFF, 5, 2);
    skip_outline();
    m.accent[i] = end();
  }
}

// ZOLFO, fiamma viva delle nubi di zolfo di Venere (lanciafiamme): goccia di fuoco giallo zolfo e
// arancio con il contorno arancio scuro, la punta che tremola (a parte), occhi di brace, piedi di
// roccia lavica, braccia a lingua di fuoco. Colore del giocatore: anello attorno al collo.
void build_zolfo(CharMesh& m) {
  const uint32_t flame_lo = 0xFF8A1C, flame_hi = 0xFFCB3A, core = 0xFFF2A8, edge = 0x8C2F06;
  const uint32_t rock = 0x4C3B37, lava = 0xFF6A1A, coal = 0x2E1206, ember = 0xFFE07A;
  m.sh_x = 0.33f; m.sh_y = 0.64f; m.hip_x = 0.15f; m.hip_y = 0.2f; m.arm_len = 0.4f; m.top = 1.6f;
  m.rest_r = 0.0f; m.swing_r = 0.9f;
  begin();
  // Goccia di fuoco: sotto arancio rossastro, sopra giallo zolfo (a fasce, come una sfumatura).
  static const float BODY[10][2] = { { 0.18f, 0.0f }, { 0.2f, 0.17f }, { 0.26f, 0.29f }, { 0.36f, 0.355f }, { 0.5f, 0.38f },
                                     { 0.64f, 0.365f }, { 0.78f, 0.32f }, { 0.9f, 0.255f }, { 1.0f, 0.16f }, { 1.05f, 0.0f } };
  static const uint32_t BODY_C[9] = { 0xF2541B, 0xF7651E, 0xFC7A20, 0xFF8E22, 0xFFA126, 0xFFB22A, 0xFFC22E, 0xFFCF34, 0xFFCF34 };
  lathe(BODY, 10, 12, 0.0f, 0.0f, 1.0f, 0.92f, xf_identity(), 0, BODY_C);   // y 0.18..1.05
  outline(OL, edge);
  // Fiamma chiara dentro la pancia (sempre accesa), occhi di brace, sorriso.
  unlit(true);
  {
    static const float CORE[6][2] = { { 0.26f, 0.0f }, { 0.3f, 0.13f }, { 0.42f, 0.18f }, { 0.56f, 0.14f }, { 0.68f, 0.07f }, { 0.78f, 0.0f } };
    lathe(CORE, 6, 8, -0.05f, 0.0f, 0.45f, 0.45f, xf_translate(v3(0.0f, 0.0f, 0.26f)), core);
  }
  unlit(false);
  ell(-0.125f, 0.8f, 0.27f, 0.062f, 0.088f, 0.035f, coal, 6, 4);
  ell(0.125f, 0.8f, 0.27f, 0.062f, 0.088f, 0.035f, coal, 6, 4);
  unlit(true);
  ell(-0.113f, 0.822f, 0.298f, 0.025f, 0.034f, 0.012f, ember, 4, 2);
  ell(0.137f, 0.822f, 0.298f, 0.025f, 0.034f, 0.012f, ember, 4, 2);
  unlit(false);
  ell_rot(v3(-0.13f, 0.905f, 0.262f), v3(0.06f, 0.014f, 0.02f), xf_rot_z(-0.3f), coal, 4, 2);   // sopracciglia
  ell_rot(v3(0.13f, 0.905f, 0.262f), v3(0.06f, 0.014f, 0.02f), xf_rot_z(0.3f), coal, 4, 2);
  ell(0, 0.685f, 0.315f, 0.06f, 0.026f, 0.02f, coal, 6, 2);             // sorriso
  skip_outline();
  m.body = end();
  // Parte alta della fiamma, accesa: la punta che si arriccia e due lingue ai lati; tremola attorno a fx_at.
  m.fx_at = v3(0, 0.9f, 0);
  begin();
  {
    static const float TOP[9][2] = { { 0.78f, 0.0f }, { 0.82f, 0.24f }, { 0.95f, 0.262f }, { 1.06f, 0.25f }, { 1.16f, 0.215f },
                                     { 1.25f, 0.165f }, { 1.34f, 0.105f }, { 1.43f, 0.05f }, { 1.56f, 0.0f } };
    static const uint32_t TOP_C[8] = { 0xFFB62C, 0xFFB62C, 0xFFC232, 0xFFCE3A, 0xFFDA46, 0xFFE458, 0xFFEC78, 0xFFF4A8 };
    static const float TONGUE[6][2] = { { 0.0f, 0.0f }, { 0.05f, 0.095f }, { 0.15f, 0.09f }, { 0.25f, 0.06f }, { 0.34f, 0.025f }, { 0.42f, 0.0f } };
    static const uint32_t TONGUE_C[5] = { 0xFFB62C, 0xFFC232, 0xFFD040, 0xFFE05A, 0xFFEE88 };
    unlit(true);
    lathe(TOP, 9, 10, 0.3f, -0.1f, 0.9f, 0.9f, xf_identity(), 0, TOP_C);
    lathe(TONGUE, 6, 7, 0.08f, 0.0f, 0.1f, 0.9f, xf_mul(xf_translate(v3(-0.17f, 0.98f, -0.02f)), xf_rot_z(0.32f)), 0, TONGUE_C);
    lathe(TONGUE, 6, 7, -0.06f, 0.0f, 0.1f, 0.9f, xf_mul(xf_translate(v3(0.2f, 0.9f, 0.02f)), xf_mul(xf_rot_z(-0.42f), xf_scale(0.75f, 0.75f, 0.75f))), 0, TONGUE_C);
    unlit(false);
    outline(OL, edge);
  }
  m.fx = end();

  for (int s = 0; s < 2; ++s) {
    begin();
    ell_rot(v3(0, -0.15f, 0.02f), v3(0.095f, 0.18f, 0.085f), xf_rot_x(0.15f), flame_lo, 6, 4);
    cone(v3(0, -0.27f, 0.05f), v3(0, -1.0f, 0.45f), 0.075f, 0.17f, flame_hi, 6);
    outline(OL, edge);
    (s ? m.arm_r : m.arm_l) = end();
  }
  m.tip = v3(0, -0.4f, 0.1f);
  begin();
  ell(0, -0.115f, 0.04f, 0.125f, 0.09f, 0.155f, rock, 6, 3);          // piede di roccia lavica
  outline();
  unlit(true);
  bbox(-0.07f, -0.04f, 0.04f, 0.07f, -0.025f, 0.075f, 0.0f, lava);
  bbox(-0.015f, -0.045f, 0.04f, 0.015f, -0.025f, 0.17f, 0.0f, lava);
  unlit(false);
  skip_outline();
  m.leg = end();
  for (int i = 0; i < 4; ++i) {
    const uint32_t c = PLAYER_RGB[i];
    begin();
    torus(xf_translate(v3(0, 0.9f, 0.0f)), 0.265f, 0.042f, 12, 4, c);   // anello attorno al collo
    ell(0, 0.88f, 0.29f, 0.055f, 0.055f, 0.035f, rgb_mix(c, 0xFFFFFF, 0.4f), 5, 3);   // gemma
    outline(0.022f);
    m.accent[i] = end();
  }
}

// --- mostri della Terra -----------------------------------------------------------------

// SCORPIONE (corridore, dalla 0.5.1 al posto dello sciacallo): scorpione del deserto color ambra,
// piatto e largo: addome a segmenti con le fasce scure (si vedono dall'alto), carapace con gli
// occhietti neri, due chele grandi tenute avanti con il dito mobile a parte, 8 zampette scure (una
// mesh sola disegnata 8 volte), coda a segmenti che si inarca sulla schiena e pungiglione scuro con
// la punta rosso fuoco accesa. Perni SCORPION_* in models.h.
const uint32_t SC_AMBER = 0xE8892A, SC_AMBER2 = 0xF6A93E, SC_BAND = 0x8A4418, SC_DARK = 0x4A2210, SC_STING = 0x2E1A12;
void build_scorpion(void) {
  const uint32_t black = 0x17121A;
  begin();
  {
    // Addome lungo Z, dalla coda (dietro) al carapace: sezioni a esagono schiacciato, fasce scure
    // tra un segmento e l'altro.
    static const float ABD[9][2] = { { 0.0f, 0.0f }, { 0.1f, 0.17f }, { 0.16f, 0.19f }, { 0.29f, 0.235f }, { 0.35f, 0.245f },
                                     { 0.47f, 0.27f }, { 0.53f, 0.275f }, { 0.67f, 0.262f }, { 0.76f, 0.0f } };
    static const uint32_t ABD_C[8] = { SC_AMBER, SC_BAND, SC_AMBER, SC_BAND, SC_AMBER, SC_BAND, SC_AMBER, SC_AMBER };
    lathe(ABD, 9, 6, 0.0f, 0.0f, 1.0f, 0.5f, xf_mul(xf_translate(v3(0, 0.3f, -0.64f)), xf_rot_x(D_HALF_PI)), 0, ABD_C);
  }
  ell(0, 0.32f, 0.2f, 0.215f, 0.13f, 0.2f, SC_AMBER2, 7, 3);          // carapace: y 0.19..0.45
  outline();
  ball(-0.05f, 0.44f, 0.27f, 0.04f, black, 4, 2);                     // occhietti neri in cima al carapace
  ball(0.05f, 0.44f, 0.27f, 0.04f, black, 4, 2);
  skip_outline();
  M.scorpion_body = end();

  // Zampa destra (verso +X dal perno all'anca): sale al ginocchio e scende al piede a terra. Sottile
  // e scura, senza contorno: da lontano e' un tratto d'inchiostro come il contorno (e costa la meta').
  begin();
  tube(v3(-0.03f, 0.0f, 0.0f), v3(0.19f, 0.13f, 0.0f), 0.045f, SC_BAND, 3);
  cone(v3(0.19f, 0.13f, 0.0f), v3(0.16f, -0.4f, 0.0f), 0.045f, 0.43f, SC_BAND, 3);
  skip_outline();
  M.scorpion_leg = end();

  // Chele (sistema della spalla): braccio in fuori, avambraccio in avanti, mano grossa e dito fisso
  // scuro di fuori; il dito mobile (di dentro) e' a parte e si apre ruotando sul suo perno.
  for (int side = 0; side < 2; ++side) {
    const float sx = side ? 1.0f : -1.0f;
    begin();
    tube(v3(0, 0, 0), v3(sx * 0.2f, 0.04f, 0.08f), 0.048f, SC_AMBER, 3);
    tube(v3(sx * 0.2f, 0.04f, 0.08f), v3(sx * 0.17f, 0.06f, 0.31f), 0.052f, SC_AMBER, 3);
    ell_rot(v3(sx * 0.145f, 0.07f, 0.41f), v3(0.11f, 0.085f, 0.15f), xf_rot_y(-0.18f * sx), SC_AMBER2, 6, 3);
    cone(v3(sx * 0.185f, 0.07f, 0.5f), v3(-sx * 0.3f, 0.0f, 1.0f), 0.045f, 0.2f, SC_DARK, 4);
    outline();
    (side ? M.scorpion_claw_r : M.scorpion_claw_l) = end();
    begin();
    cone(v3(0, 0, -0.02f), v3(sx * 0.22f, 0.0f, 1.0f), 0.042f, 0.19f, SC_DARK, 4);
    outline(0.025f);
    (side ? M.scorpion_finger_r : M.scorpion_finger_l) = end();
  }

  // Segmenti della coda lungo +Z (si toccano un po'), sempre piu' sottili e scuri verso la punta.
  for (int k = 0; k < SCORPION_SEGS; ++k) {
    const float w = 0.1f - 0.008f * (float)k;
    begin();
    ell_rot(v3(0, 0, 0.5f * SCORPION_SEG), v3(w, 0.62f * SCORPION_SEG, w * 0.9f), xf_rot_x(D_HALF_PI),
            rgb_mix(SC_AMBER, SC_BAND, 0.12f * (float)k), 5, 3);
    outline();
    M.scorpion_tail[k] = end();
  }
  // Pungiglione: vescicola gonfia e uncino scuro che si piega verso il dentro della curva (+Y), punta accesa.
  begin();
  ell_rot(v3(0, 0.0f, 0.1f), v3(0.085f, 0.115f, 0.08f), xf_rot_x(D_HALF_PI), SC_DARK, 5, 3);
  cone(v3(0, 0.03f, 0.18f), v3(0, 0.75f, 1.0f), 0.045f, 0.17f, SC_STING, 4);
  outline();
  unlit(true);
  ball(SCORPION_STING_TIP.x, SCORPION_STING_TIP.y - 0.01f, SCORPION_STING_TIP.z - 0.01f, 0.05f, 0xFF4A1E, 4, 2);
  unlit(false);
  outline(0.02f, 0x5A1408);
  M.scorpion_sting = end();
}

// ARCIERE DI TERRACOTTA (tiratore, dalla 0.5.1 al posto del lama): compagno del guerriero
// nell'esercito di Xi'an, della stessa argilla ma piu' snello: veste al ginocchio, corazzina a
// placche con i ribattini, crocchia in cima alla testa sul lato destro, occhi accesi, faretra sulla
// schiena con le penne rosse e bianche delle frecce (si vedono dall'alto). L'arco (legno scuro, punte a ricciolo
// di bronzo) si tiene con la sinistra; la destra tende la corda. Per tirare si inginocchia come
// l'arciere inginocchiato di Xi'an: per questo braccio destro e gambe sono a due pezzi.
// Perni ARCHER_* in models.h.
void build_archer(void) {
  const uint32_t clay = 0xC67C4E, clay2 = 0xA45E38, clay3 = 0xDC9A6C, armor = 0x7C4A33, rivet = 0xE3AE80;
  const uint32_t hair = 0x6B3C26, robe = 0xB46C45, glow = 0xFFD24A, leather = 0x5E3A24;
  const uint32_t feather = 0xE8402E, feather2 = 0xF6E8D0;
  begin();
  cyl(v3(0, 0.44f, 0), v3(0, 1, 0), 0.25f, 0.2f, 0.44f, robe, 7);     // veste: y 0.44..0.88
  ell(0, 1.0f, 0, 0.255f, 0.27f, 0.185f, armor, 7, 4);                // corazzina: y 0.73..1.27
  ell(-0.25f, 1.16f, 0, 0.1f, 0.075f, 0.115f, armor, 5, 2);           // spallacci
  ell(0.25f, 1.16f, 0, 0.1f, 0.075f, 0.115f, armor, 5, 2);
  ell(0, 1.4f, 0.02f, 0.145f, 0.16f, 0.145f, clay3, 6, 4);            // testa: y 1.24..1.56
  ell(0, 1.46f, -0.025f, 0.155f, 0.12f, 0.145f, hair, 6, 3);          // capelli raccolti
  ell_rot(v3(0.065f, 1.6f, -0.03f), v3(0.075f, 0.065f, 0.07f), xf_rot_z(-0.5f), hair, 5, 3);   // crocchia a destra
  // Faretra di cuoio sulla schiena, di traverso (bocca in alto a destra), con le penne delle frecce.
  cyl(v3(-0.08f, 0.74f, -0.21f), v3(0.24f, 1.0f, -0.12f), 0.075f, 0.085f, 0.62f, leather, 5);
  {
    const Vec3 top = v3(-0.08f, 0.74f, -0.21f) + normalize(v3(0.24f, 1.0f, -0.12f)) * 0.62f;
    for (int k = 0; k < 3; ++k) {
      const Vec3 b = top + v3(0.045f * (float)(k - 1), 0.0f, 0.03f * (float)((k + 1) % 3 - 1));
      const Vec3 d = normalize(v3(0.24f + 0.15f * (float)(k - 1), 1.0f, -0.12f + 0.1f * (float)(k - 1)));
      ell_rot(b + d * 0.1f, v3(0.045f, 0.085f, 0.02f), xf_align_y(d), (k & 1) ? feather2 : feather, 4, 2);
    }
  }
  outline();
  // Placche, ribattini, cintura, nastro della crocchia, cinghia della faretra.
  ell(0, 0.92f, 0, 0.245f, 0.014f, 0.178f, clay2, 7, 2);
  ell(0, 1.09f, 0, 0.245f, 0.014f, 0.178f, clay2, 7, 2);
  for (int c = -1; c <= 1; c += 2) {
    const float y = 1.0f, a = 0.33f * (float)c;
    ball(0.255f * d_sin(a), y, 0.185f * d_cos(a), 0.024f, rivet, 4, 2);
  }
  ell(0, 0.8f, 0, 0.215f, 0.045f, 0.165f, clay2, 7, 2);              // cintura
  ball(0.08f, 1.665f, -0.035f, 0.035f, feather, 4, 2);                // nastro rosso
  tube(v3(-0.24f, 1.18f, 0.06f), v3(0.2f, 0.78f, 0.16f), 0.024f, leather, 4);
  // Faccia: occhi accesi, sopracciglia severe, naso, baffetti.
  unlit(true);
  ell(-0.055f, 1.43f, 0.15f, 0.03f, 0.014f, 0.018f, glow, 5, 2);
  ell(0.055f, 1.43f, 0.15f, 0.03f, 0.014f, 0.018f, glow, 5, 2);
  unlit(false);
  ell_rot(v3(-0.058f, 1.465f, 0.143f), v3(0.045f, 0.013f, 0.018f), xf_rot_z(-0.3f), hair, 4, 2);
  ell_rot(v3(0.058f, 1.465f, 0.143f), v3(0.045f, 0.013f, 0.018f), xf_rot_z(0.3f), hair, 4, 2);
  ell(0, 1.385f, 0.165f, 0.024f, 0.036f, 0.024f, clay3, 4, 2);
  ell_rot(v3(-0.038f, 1.34f, 0.152f), v3(0.042f, 0.013f, 0.016f), xf_rot_z(0.35f), hair, 4, 2);
  ell_rot(v3(0.038f, 1.34f, 0.152f), v3(0.042f, 0.013f, 0.016f), xf_rot_z(-0.35f), hair, 4, 2);
  skip_outline();
  M.archer_body = end();

  // Braccio sinistro (dell'arco): manica, avambraccio con il pugno chiuso sull'impugnatura.
  begin();
  ell(0, -0.17f, 0, 0.085f, 0.19f, 0.085f, robe, 6, 3);
  ell(0, -0.4f, 0.01f, 0.072f, 0.125f, 0.072f, clay, 5, 3);
  outline();
  tube(v3(0, -0.29f, 0.01f), v3(0, -0.37f, 0.01f), 0.082f, armor, 5);   // bracciale
  skip_outline();
  M.archer_arm_l = end();
  // Braccio destro: braccio (fino al gomito) e avambraccio con il pugno che tiene la corda.
  begin();
  ell(0, -0.13f, 0, 0.085f, 0.16f, 0.085f, robe, 6, 3);
  outline();
  M.archer_upper_r = end();
  begin();
  ell(0, -0.12f, 0.0f, 0.068f, 0.14f, 0.068f, clay, 5, 3);
  ball(0, -0.25f, 0.0f, 0.072f, clay3, 5, 2);
  outline();
  M.archer_fore_r = end();
  // Gambe: coscia (pantaloni) e stinco con la scarpa.
  begin();
  ell(0, -0.165f, 0, 0.09f, 0.2f, 0.09f, robe, 5, 2);
  outline();
  M.archer_thigh = end();
  begin();
  ell(0, -0.15f, 0.0f, 0.075f, 0.18f, 0.075f, clay2, 5, 3);
  ell(0, -0.3f, 0.05f, 0.085f, 0.05f, 0.13f, armor, 5, 2);
  outline();
  M.archer_shin = end();

  // Arco: impugnatura nell'origine, flettenti di legno scuro che piegano verso la corda (-Z) e
  // punte a ricciolo di bronzo; la corda (a parte) va dalle punte alla mano destra.
  begin();
  {
    const uint32_t wood = 0x4E3020, bronze = 0xD49A3A, wrap = 0xD8402E;
    static const float LIMB[4][2] = { { 0.0f, 0.04f }, { 0.22f, -0.02f }, { 0.42f, -0.09f }, { ARCHER_BOW_TIP, ARCHER_STRING_Z } };
    for (int s = -1; s <= 1; s += 2) {
      const float sy = (float)s;
      for (int k = 0; k < 3; ++k)
        tube(v3(0, sy * LIMB[k][0], LIMB[k][1]), v3(0, sy * LIMB[k + 1][0], LIMB[k + 1][1]), 0.044f - 0.007f * (float)k, wood, 3);
      cone(v3(0, sy * (ARCHER_BOW_TIP - 0.03f), ARCHER_STRING_Z), v3(0, sy * 0.6f, 1.0f), 0.034f, 0.15f, bronze, 3);   // ricciolo
    }
    tube(v3(0, -0.085f, 0.04f), v3(0, 0.085f, 0.04f), 0.052f, wrap, 5);   // impugnatura fasciata di rosso
    outline(0.028f);
  }
  M.bow = end();
  // Mezza corda: un filo lungo 1 lungo +Y (il disegno lo stira tra una punta e la mano).
  begin();
  tube(v3(0, 0, 0), v3(0, 1, 0), 0.009f, 0xF2E6C8, 3);
  M.bow_string = end();
}

// SCARABEO (sciame): corazza turchese metallica con i fregi d'oro, elitre aperte, corno d'oro,
// occhietti rossi accesi, zampette scure; le ali sottili battono a parte.
void build_scarab(void) {
  const uint32_t shell = 0x1FB59E, shell2 = 0x14806F, gold = 0xF2C230, dark = 0x1D2A2C, wing = 0xE4F2EC;
  begin();
  ell(0, -0.02f, -0.03f, 0.13f, 0.09f, 0.2f, dark, 5, 3);             // corpo sotto le elitre
  ell(0, 0.03f, 0.13f, 0.12f, 0.075f, 0.09f, shell2, 5, 3);           // scudo del torace
  ell(0, 0.0f, 0.24f, 0.075f, 0.06f, 0.06f, dark, 4, 3);              // testa
  ell_rot(v3(-0.1f, 0.07f, -0.06f), v3(0.1f, 0.04f, 0.19f), xf_mul(xf_rot_z(0.45f), xf_rot_x(-0.15f)), shell, 6, 4);   // elitre
  ell_rot(v3(0.1f, 0.07f, -0.06f), v3(0.1f, 0.04f, 0.19f), xf_mul(xf_rot_z(-0.45f), xf_rot_x(-0.15f)), shell, 6, 4);
  cone(v3(0, 0.02f, 0.27f), v3(0, 0.75f, 1.0f), 0.035f, 0.13f, gold, 4);   // corno
  outline(0.022f);
  for (int s = -1; s <= 1; s += 2) {
    const float sx = (float)s;
    ell_rot(v3(sx * 0.105f, 0.106f, -0.06f), v3(0.022f, 0.012f, 0.17f), xf_mul(xf_rot_z(-0.45f * sx), xf_rot_x(-0.15f)), gold, 4, 2);
    for (int k = 0; k < 3; ++k) {
      const float z = 0.08f - 0.09f * (float)k;
      tube(v3(sx * 0.07f, -0.05f, z), v3(sx * 0.17f, -0.16f, z - 0.04f + 0.03f * (float)k), 0.014f, dark, 3);
    }
  }
  ell(0, 0.075f, 0.14f, 0.06f, 0.02f, 0.04f, gold, 4, 2);
  unlit(true);
  ball(-0.045f, 0.02f, 0.285f, 0.022f, 0xFF4A3A, 4, 2);
  ball(0.045f, 0.02f, 0.285f, 0.022f, 0xFF4A3A, 4, 2);
  unlit(false);
  skip_outline();
  M.scarab_body = end();
  for (int s = 0; s < 2; ++s) {
    const float sx = s ? 1.0f : -1.0f;
    begin();
    ell(sx * 0.2f, 0.05f, -0.09f, 0.19f, 0.012f, 0.085f, wing, 6, 2);
    outline(0.012f, 0x5A8A90);
    (s ? M.scarab_wing_r : M.scarab_wing_l) = end();
  }
}

// GUERRIERO DI TERRACOTTA (grosso): statua d'argilla che cammina, corazza a placche con i ribattini,
// capelli raccolti con la crocchia di lato, baffi, occhi accesi, qualche crepa. Braccia, gambe e
// alabarda sono a parte (perni WARRIOR_* in models.h).
void build_warrior(void) {
  const uint32_t clay = 0xC67C4E, clay2 = 0xA45E38, clay3 = 0xDC9A6C, armor = 0x7C4A33, rivet = 0xE3AE80;
  const uint32_t hair = 0x6B3C26, robe = 0xB46C45, glow = 0xFFD24A, crack = 0x4A2A1A;
  begin();
  cyl(v3(0, 0.40f, 0), v3(0, 1, 0), 0.37f, 0.30f, 0.5f, robe, 9);    // veste lunga: y 0.40..0.90
  ell(0, 1.12f, 0, 0.42f, 0.42f, 0.31f, clay, 8, 5);                 // busto: y 0.70..1.54
  ell(0, 1.07f, 0, 0.445f, 0.31f, 0.33f, armor, 8, 4);              // corazza: y 0.76..1.38
  ell(-0.45f, 1.33f, 0, 0.17f, 0.11f, 0.18f, armor, 6, 3);           // spallacci
  ell(0.45f, 1.33f, 0, 0.17f, 0.11f, 0.18f, armor, 6, 3);
  ell(0, 1.66f, 0.02f, 0.2f, 0.22f, 0.2f, clay3, 7, 5);              // testa: y 1.44..1.88
  ell(0, 1.73f, -0.03f, 0.21f, 0.17f, 0.2f, hair, 7, 3);             // capelli raccolti
  ball(0.09f, 1.93f, -0.03f, 0.09f, hair, 5, 3);                      // crocchia di lato
  outline();
  // Righe di placche con i ribattini, cintura.
  ell(0, 0.97f, 0, 0.432f, 0.016f, 0.322f, clay2, 8, 2);
  ell(0, 1.17f, 0, 0.432f, 0.016f, 0.322f, clay2, 8, 2);
  for (int r = 0; r < 3; ++r) {
    const float y = 0.88f + 0.2f * (float)r;
    const float ry = (y - 1.07f) / 0.31f, k = sqrtf(fmaxf(0.0f, 1.0f - ry * ry));
    for (int c = -1; c <= 1; c += 2) {
      const float a = 0.3f * (float)c;
      ball(0.445f * k * d_sin(a), y, 0.33f * k * d_cos(a), 0.026f, rivet, 4, 2);
    }
  }
  ell(-0.2f, 1.66f, 0.0f, 0.03f, 0.05f, 0.035f, clay3, 4, 2);        // orecchie
  ell(0.2f, 1.66f, 0.0f, 0.03f, 0.05f, 0.035f, clay3, 4, 2);
  ell(0, 0.86f, 0, 0.36f, 0.05f, 0.29f, clay2, 8, 2);               // cintura
  bbox(-0.06f, 0.82f, 0.27f, 0.06f, 0.9f, 0.3f, 0.0f, rivet);        // fibbia
  ball(0.09f, 1.85f, -0.03f, 0.05f, clay2, 5, 3);                     // nastro della crocchia
  // Faccia: occhi accesi, sopracciglia severe, naso, baffi spioventi, bocca.
  unlit(true);
  ell(-0.072f, 1.69f, 0.202f, 0.036f, 0.016f, 0.02f, glow, 5, 2);
  ell(0.072f, 1.69f, 0.202f, 0.036f, 0.016f, 0.02f, glow, 5, 2);
  unlit(false);
  ell_rot(v3(-0.075f, 1.73f, 0.195f), v3(0.056f, 0.015f, 0.02f), xf_rot_z(-0.28f), hair, 4, 2);
  ell_rot(v3(0.075f, 1.73f, 0.195f), v3(0.056f, 0.015f, 0.02f), xf_rot_z(0.28f), hair, 4, 2);
  ell(0, 1.635f, 0.225f, 0.03f, 0.045f, 0.03f, clay3, 4, 3);
  ell_rot(v3(-0.05f, 1.585f, 0.208f), v3(0.058f, 0.017f, 0.02f), xf_rot_z(0.4f), hair, 4, 2);
  ell_rot(v3(0.05f, 1.585f, 0.208f), v3(0.058f, 0.017f, 0.02f), xf_rot_z(-0.4f), hair, 4, 2);
  ell(0, 1.548f, 0.2f, 0.035f, 0.008f, 0.012f, crack, 4, 2);
  // Crepe dell'argilla.
  box_rot(v3(-0.2f, 1.25f, 0.27f), v3(0.006f, 0.07f, 0.01f), xf_mul(xf_rot_y(-0.6f), xf_rot_z(0.4f)), crack);
  box_rot(v3(-0.23f, 1.17f, 0.25f), v3(0.006f, 0.05f, 0.01f), xf_mul(xf_rot_y(-0.6f), xf_rot_z(-0.5f)), crack);
  skip_outline();
  M.warrior_body = end();
  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.2f, 0, 0.12f, 0.24f, 0.12f, robe, 6, 3);               // manica
    ell(0, -0.46f, 0.01f, 0.105f, 0.15f, 0.105f, clay, 5, 3);        // avambraccio
    ball(0, -0.62f, 0.03f, 0.1f, clay3, 5, 3);                        // pugno
    outline();
    cyl(v3(0, -0.4f, 0.01f), v3(0, -1, 0), 0.115f, 0.11f, 0.1f, armor, 6);   // bracciale
    skip_outline();
    (s ? M.warrior_arm_r : M.warrior_arm_l) = end();
  }
  begin();
  cyl(v3(0, 0, 0), v3(0, -1, 0), 0.13f, 0.11f, 0.48f, robe, 6);       // pantaloni
  ell(0, -0.535f, 0.05f, 0.135f, 0.085f, 0.19f, armor, 5, 3);         // scarpa
  outline();
  M.warrior_leg = end();
  // Alabarda (ji): impugnatura nell'origine, asta lungo +Y, lama a mezzaluna verso +Z.
  begin();
  {
    const uint32_t pole = 0x5B3A22, bronze = 0xD49A3A, tassel = 0xD8402E;
    cyl(v3(0, -0.75f, 0), v3(0, 1, 0), 0.035f, 0.035f, 2.4f, pole, 6);
    cone(v3(0, 1.62f, 0), v3(0, 1, 0), 0.07f, 0.42f, bronze, 4);
    cone(v3(0, 1.45f, -0.02f), v3(0, 0.25f, -1.0f), 0.04f, 0.16f, bronze, 4);
    static const float AXE[5][2] = { { 0.02f, 1.32f }, { 0.3f, 1.22f }, { 0.4f, 1.42f }, { 0.3f, 1.62f }, { 0.02f, 1.56f } };
    prism(AXE, 5, -0.025f, 0.025f, xf_rot_y(-D_HALF_PI), bronze);
    cyl(v3(0, 1.28f, 0), v3(0, 1, 0), 0.055f, 0.055f, 0.06f, bronze, 6);
    cone(v3(0, -0.75f, 0), v3(0, -1, 0), 0.045f, 0.08f, bronze, 4);
    outline(0.022f);
    ell(0, 1.17f, 0, 0.065f, 0.12f, 0.065f, tassel, 6, 3);            // nappa rossa
    outline(0.02f);
  }
  M.halberd = end();
}

// --- boss: il Gladiatore gigante ------------------------------------------------------------

const uint32_t GL_SKIN = 0xD9976A, GL_SKIN2 = 0xC07E55, GL_BRONZE = 0xD9A040, GL_BRONZE2 = 0xA8742A, GL_RED = 0xCC2830;
const uint32_t GL_LEATHER = 0x7A4A2C, GL_GOLD = 0xF2C230, GL_STEEL = 0xDDE5EE, GL_DARK = 0x24160E, GL_CLOTH = 0xF0E6D2;
const uint32_t GL_WOOD = 0x8A5A32, GL_IRON = 0x6A6E78;

// Alto quasi 5 m: petto muscoloso, cintura di bronzo, gonnellino con le strisce di cuoio, faretra
// con i giavellotti sulla schiena, fodero al fianco. Testa (elmo di bronzo con la cresta rossa e la
// visiera a grata), braccia, gambe, gladio e scudo sono pezzi a parte (perni BOSS_* in models.h).
void build_boss(void) {
  begin();
  cyl(v3(0, 1.22f, 0), v3(0, 1, 0), 0.8f, 0.68f, 0.66f, GL_CLOTH, 10);   // gonnellino: y 1.22..1.88
  for (int k = 0; k < 10; ++k) {                                        // strisce di cuoio
    const float a = D_TWO_PI * ((float)k + 0.5f) / 10.0f;
    const Vec3 p = v3(0.79f * d_sin(a), 1.47f, 0.79f * d_cos(a));
    box_rot(p, v3(0.15f, 0.26f, 0.03f), xf_mul(xf_rot_y(a), xf_rot_x(-0.12f)), GL_LEATHER);
  }
  cyl(v3(0, 1.82f, 0), v3(0, 1, 0), 0.74f, 0.74f, 0.3f, GL_BRONZE, 10);   // cintura: y 1.82..2.12
  ell(0, 2.78f, 0, 0.98f, 0.8f, 0.62f, GL_SKIN, 11, 6);                // busto: y 1.98..3.58
  ell(-0.34f, 3.0f, 0.4f, 0.38f, 0.27f, 0.22f, GL_SKIN, 7, 4);         // pettorali
  ell(0.34f, 3.0f, 0.4f, 0.38f, 0.27f, 0.22f, GL_SKIN, 7, 4);
  ell(0, 2.48f, 0.42f, 0.42f, 0.4f, 0.2f, GL_SKIN, 7, 3);             // pancia
  cyl(v3(0, 3.38f, -0.03f), v3(0, 1, 0), 0.36f, 0.32f, 0.3f, GL_SKIN, 8);   // collo
  ell(1.0f, 3.42f, 0, 0.44f, 0.25f, 0.44f, GL_BRONZE, 8, 4);           // spallaccio del braccio della spada
  cyl(v3(0.35f, 2.15f, -0.66f), v3(0.28f, 1.0f, -0.12f), 0.2f, 0.22f, 1.15f, GL_LEATHER, 8);   // faretra
  {
    const int fv = B.nv, ft = B.nt;                                      // fodero (gladio dentro: vedi boss_scabbard)
    bbox(-0.085f, -0.21f, 0.1f, 0.085f, 0.21f, 1.75f, 0.0f, GL_LEATHER);
    cone(v3(0, 0, 1.74f), v3(0, 0, 1), 0.16f, 0.28f, GL_BRONZE, 4);
    mb_apply(&B, fv, ft, boss_scabbard());
  }
  outline(0.05f);
  // Giavellotti nella faretra.
  for (int k = 0; k < 3; ++k) {
    const Vec3 b = v3(0.62f + 0.09f * (float)(k - 1), 3.2f, -0.8f + 0.06f * (float)(k - 1));
    const Vec3 d = normalize(v3(0.3f + 0.08f * (float)(k - 1), 1.0f, -0.12f));
    cyl(b, d, 0.035f, 0.035f, 0.9f, GL_WOOD, 5);
    cone(b + d * 0.9f, d, 0.07f, 0.24f, GL_BRONZE, 4);
  }
  outline(0.03f);
  // Cinghia di cuoio sul petto, con le borchie; ombelico, ribattini e fibbia della cintura.
  {
    const Vec3 sp[4] = { v3(0.9f, 3.35f, 0.18f), v3(0.42f, 3.02f, 0.66f), v3(-0.32f, 2.5f, 0.66f), v3(-0.82f, 2.2f, 0.3f) };
    for (int k = 0; k < 3; ++k) tube(sp[k], sp[k + 1], 0.07f, GL_LEATHER, 6);
    for (int k = 0; k < 3; ++k) {
      const Vec3 c = (sp[k] + sp[k + 1]) * 0.5f;
      ball(c.x, c.y, c.z + 0.05f, 0.045f, GL_GOLD, 4, 2);
    }
  }
  ell(0, 2.3f, 0.6f, 0.04f, 0.05f, 0.02f, GL_SKIN2, 4, 2);
  for (int k = 0; k < 8; ++k) {
    const float a = D_TWO_PI * ((float)k + 0.5f) / 8.0f;
    ball(0.75f * d_sin(a), 1.97f, 0.75f * d_cos(a), 0.055f, GL_BRONZE2, 4, 2);
  }
  bbox(-0.17f, 1.86f, 0.7f, 0.17f, 2.08f, 0.78f, 0.03f, GL_GOLD);
  skip_outline();
  M.boss_body = end();

  // Testa: elmo (galea) con tesa larga, visiera a grata con gli occhi accesi, cresta rossa.
  begin();
  ell(0, 4.05f, 0.0f, 0.47f, 0.45f, 0.47f, GL_BRONZE, 10, 5);         // calotta: y 3.60..4.50
  cyl(v3(0, 3.95f, 0.0f), v3(0, 1, 0), 0.7f, 0.64f, 0.07f, GL_BRONZE, 10);   // tesa
  ell(0, 3.78f, 0.16f, 0.42f, 0.32f, 0.36f, GL_BRONZE2, 9, 4);        // visiera: y 3.46..4.10
  ell(0, 4.47f, -0.04f, 0.1f, 0.12f, 0.46f, GL_BRONZE, 6, 4);         // supporto della cresta
  ell(0, 4.8f, -0.1f, 0.16f, 0.4f, 0.66f, GL_RED, 7, 6);              // cresta: y 4.40..5.20
  outline(0.045f);
  for (int k = 0; k < 5; ++k) {                                         // ciuffi della cresta
    const float a = -1.2f + 0.6f * (float)k;
    ball(0, 4.8f + 0.36f * d_cos(a), -0.1f + 0.6f * d_sin(a), 0.14f, GL_RED, 4, 3);
  }
  outline(0.03f);
  ell(-0.16f, 3.88f, 0.48f, 0.11f, 0.085f, 0.06f, GL_DARK, 6, 3);     // fori per gli occhi
  ell(0.16f, 3.88f, 0.48f, 0.11f, 0.085f, 0.06f, GL_DARK, 6, 3);
  unlit(true);
  ell(-0.16f, 3.88f, 0.52f, 0.075f, 0.055f, 0.02f, 0xFF5A2A, 5, 2);
  ell(0.16f, 3.88f, 0.52f, 0.075f, 0.055f, 0.02f, 0xFF5A2A, 5, 2);
  unlit(false);
  for (int r = 0; r < 2; ++r)                                          // grata
    for (int c = -2; c <= 2; c += (r ? 1 : 2)) {
      const float x = 0.085f * (float)c, y = 3.72f - 0.1f * (float)r;
      const float ry = (y - 3.78f) / 0.32f, rx = x / 0.42f;
      const float z = 0.16f + 0.36f * sqrtf(fmaxf(0.0f, 1.0f - ry * ry - rx * rx));
      ball(x, y, z, 0.026f, GL_DARK, 4, 2);
    }
  ell(0, 3.6f, 0.45f, 0.06f, 0.06f, 0.03f, GL_GOLD, 5, 2);
  shine(-0.22f, 4.3f, 0.3f, 0.05f, 0xFFF2C0);
  skip_outline();
  M.boss_head = end();

  // Braccio della spada: muscolo, manica a fasce di bronzo, pugno. Braccio dello scudo: polsiera.
  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.42f, 0, 0.3f, 0.5f, 0.3f, GL_SKIN, 8, 4);
    if (s) {
      for (int k = 0; k < 4; ++k)
        cyl(v3(0, -0.52f - 0.2f * (float)k, 0.01f), v3(0, -1, 0), 0.27f - 0.012f * (float)k, 0.255f - 0.012f * (float)k, 0.18f,
            (k & 1) ? GL_BRONZE2 : GL_BRONZE, 8);
    } else {
      ell(0, -0.95f, 0.02f, 0.25f, 0.35f, 0.25f, GL_SKIN, 7, 4);
      cyl(v3(0, -1.08f, 0.02f), v3(0, -1, 0), 0.24f, 0.23f, 0.2f, GL_LEATHER, 8);
    }
    ell(0, -1.45f, 0.05f, 0.25f, 0.22f, 0.25f, GL_SKIN, 7, 4);         // pugno
    outline(0.045f);
    (s ? M.boss_arm_r : M.boss_arm_l) = end();
  }
  // Gambe: coscia, schiniere di bronzo (sinistra) o fasce di stoffa (destra), sandalo.
  for (int s = 0; s < 2; ++s) {
    begin();
    ell(0, -0.3f, 0.0f, 0.3f, 0.45f, 0.3f, GL_SKIN, 7, 4);
    if (s == 0) {
      cyl(v3(0, -0.68f, 0.03f), v3(0, -1, 0), 0.28f, 0.22f, 0.62f, GL_BRONZE, 8);
    } else {
      ell(0, -0.95f, 0.02f, 0.24f, 0.4f, 0.24f, GL_SKIN, 7, 3);
      for (int k = 0; k < 2; ++k) cyl(v3(0, -0.74f - 0.24f * (float)k, 0.02f), v3(0, -1, 0), 0.25f, 0.24f, 0.12f, GL_CLOTH, 7);
    }
    ell(0, -1.38f, 0.12f, 0.25f, 0.13f, 0.37f, GL_LEATHER, 7, 3);
    outline(0.045f);
    (s ? M.boss_leg_r : M.boss_leg_l) = end();
  }
  // Gladio: impugnatura nell'origine, lama verso +Z (larga lungo Y, sottile lungo X).
  begin();
  cyl(v3(0, 0, -0.3f), v3(0, 0, 1), 0.075f, 0.075f, 0.42f, GL_LEATHER, 6);
  ball(0, 0, -0.34f, 0.12f, GL_GOLD, 6, 4);
  bbox(-0.12f, -0.27f, 0.1f, 0.12f, 0.27f, 0.21f, 0.04f, GL_GOLD);
  blade(0.2f, 0.15f, 1.45f, 0.13f, 1.85f, 0.05f, xf_identity(), GL_STEEL);
  outline(0.035f);
  shine(0.0f, 0.06f, 0.85f, 0.04f);
  skip_outline();
  M.boss_sword = end();
  // Scudo (scutum): curvo, rosso con il bordo d'oro, umbone di bronzo e due fulmini d'oro.
  // Impugnatura nell'origine, davanti verso +Z.
  begin();
  {
    const float R = 1.15f, A = 0.48f, y0 = -0.88f, y1 = 0.88f, th = 0.08f, zc = 0.4f - R;
    const int N = 6;
    int fr[N + 1][2], bk[N + 1][2];
    for (int k = 0; k <= N; ++k) {
      const float a = -A + 2.0f * A * (float)k / (float)N, sa = d_sin(a), ca = d_cos(a);
      fr[k][0] = mb_vert(&B, v3(R * sa, y0, zc + R * ca));
      fr[k][1] = mb_vert(&B, v3(R * sa, y1, zc + R * ca));
      bk[k][0] = mb_vert(&B, v3((R - th) * sa, y0, zc + (R - th) * ca));
      bk[k][1] = mb_vert(&B, v3((R - th) * sa, y1, zc + (R - th) * ca));
    }
    const Vec3 ctr = v3(0, 0, zc + R - 0.5f * th);
    for (int k = 0; k < N; ++k) {
      tri_from(fr[k][0], fr[k + 1][0], fr[k + 1][1], ctr, GL_RED);
      tri_from(fr[k][0], fr[k + 1][1], fr[k][1], ctr, GL_RED);
      tri_from(bk[k][0], bk[k + 1][0], bk[k + 1][1], ctr, GL_LEATHER);
      tri_from(bk[k][0], bk[k + 1][1], bk[k][1], ctr, GL_LEATHER);
      tri_from(fr[k][1], fr[k + 1][1], bk[k + 1][1], ctr, GL_RED);
      tri_from(fr[k][1], bk[k + 1][1], bk[k][1], ctr, GL_RED);
      tri_from(fr[k][0], fr[k + 1][0], bk[k + 1][0], ctr, GL_RED);
      tri_from(fr[k][0], bk[k + 1][0], bk[k][0], ctr, GL_RED);
    }
    for (int e = 0; e <= N; e += N) {
      tri_from(fr[e][0], fr[e][1], bk[e][1], ctr, GL_RED);
      tri_from(fr[e][0], bk[e][1], bk[e][0], ctr, GL_RED);
    }
    outline(0.04f);
    // Bordo d'oro, umbone, fulmini.
    for (int k = 0; k < N; ++k)
      for (int e = 0; e < 2; ++e) {
        const float a0 = -A + 2.0f * A * (float)k / (float)N, a1 = -A + 2.0f * A * (float)(k + 1) / (float)N, y = e ? y1 : y0;
        tube(v3(R * d_sin(a0), y, zc + R * d_cos(a0)), v3(R * d_sin(a1), y, zc + R * d_cos(a1)), 0.05f, GL_GOLD, 4);
      }
    for (int e = -1; e <= 1; e += 2) {
      const float a = A * (float)e;
      tube(v3(R * d_sin(a), y0, zc + R * d_cos(a)), v3(R * d_sin(a), y1, zc + R * d_cos(a)), 0.05f, GL_GOLD, 4);
    }
    ell(0, 0, 0.4f, 0.2f, 0.2f, 0.14f, GL_BRONZE, 8, 4);
    bbox(-0.04f, -0.8f, 0.39f, 0.04f, 0.8f, 0.425f, 0.0f, GL_GOLD);
    for (int e = -1; e <= 1; e += 2)
      for (int k = 0; k < 3; ++k) {
        const float a = 0.27f * (float)e, y = 0.4f - 0.3f * (float)k;
        box_rot(v3(R * d_sin(a), y, zc + R * d_cos(a) + 0.01f), v3(0.025f, 0.16f, 0.02f),
                xf_mul(xf_rot_y(a), xf_rot_z((k & 1) ? -0.7f : 0.7f)), GL_GOLD);
      }
    shine(-0.07f, 0.07f, 0.54f, 0.04f);
    skip_outline();
  }
  M.boss_shield = end();
}

// --- colpi, oggetti, segni ------------------------------------------------------

// Quadrilatero orizzontale rivolto verso l'alto (segni a terra).
void flat_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t col) {
  const int ia = mb_vert(&B, a), ib = mb_vert(&B, b), ic = mb_vert(&B, c), id = mb_vert(&B, d);
  const Vec3 below = (a + b + c + d) * 0.25f - v3(0, 1, 0);
  tri_from(ia, ib, ic, below, col);
  tri_from(ia, ic, id, below, col);
}

// Segni a terra del gladiatore (0.5.1), rossi come il cerchio dei giavellotti: il settore del
// fendente (raggio 1, BOSS_SWEEP_HALF per parte attorno a +Z) e la corsia della carica.
void build_boss_marks(void) {
  const uint32_t RED = 0xFF3A2A, FILL = 0xC8402A, HIT = 0xFFE07A, ARROW = 0xFF7A50;
  const int N = 16;
  const float H = BOSS_SWEEP_HALF, y = 0.04f;
  begin();
  unlit(true);
  for (int k = 0; k < N; ++k) {                          // arco esterno
    const float a0 = -H + 2.0f * H * (float)k / (float)N, a1 = -H + 2.0f * H * (float)(k + 1) / (float)N;
    flat_quad(v3(0.93f * d_sin(a0), y, 0.93f * d_cos(a0)), v3(d_sin(a0), y, d_cos(a0)), v3(d_sin(a1), y, d_cos(a1)),
              v3(0.93f * d_sin(a1), y, 0.93f * d_cos(a1)), RED);
  }
  for (int e = -1; e <= 1; e += 2) {                     // i due lati
    const float a = H * (float)e, sx = d_sin(a), sz = d_cos(a), px = sz * 0.022f, pz = -sx * 0.022f;
    flat_quad(v3(0.12f * sx - px, y, 0.12f * sz - pz), v3(0.12f * sx + px, y, 0.12f * sz + pz), v3(0.96f * sx + px, y, 0.96f * sz + pz),
              v3(0.96f * sx - px, y, 0.96f * sz - pz), RED);
  }
  M.mark_sector = end();
  for (int v = 0; v < 2; ++v) {                          // riempimento; lampo del colpo
    begin();
    unlit(true);
    const int c = mb_vert(&B, v3(0, 0.03f, 0));
    int prev = mb_vert(&B, v3(d_sin(-H), 0.03f, d_cos(-H)));
    for (int k = 1; k <= N; ++k) {
      const float a = -H + 2.0f * H * (float)k / (float)N;
      const int nv = mb_vert(&B, v3(d_sin(a), 0.03f, d_cos(a)));
      tri_from(c, prev, nv, v3(0, -1, 0), v ? HIT : FILL);
      prev = nv;
    }
    (v ? M.mark_sector_hit : M.mark_sector_fill) = end();
  }
  begin();                                               // scia della spada: arco sottile, si disegna in alto
  unlit(true);
  for (int k = 0; k < N; ++k) {
    const float a0 = -H + 2.0f * H * (float)k / (float)N, a1 = -H + 2.0f * H * (float)(k + 1) / (float)N;
    const float w0 = 0.86f + 0.1f * (float)k / (float)N, w1 = 0.86f + 0.1f * (float)(k + 1) / (float)N;   // piu' sottile verso la fine
    flat_quad(v3(w0 * d_sin(a0), 0.0f, w0 * d_cos(a0)), v3(d_sin(a0), 0.0f, d_cos(a0)), v3(d_sin(a1), 0.0f, d_cos(a1)),
              v3(w1 * d_sin(a1), 0.0f, w1 * d_cos(a1)), 0xFFFFFF);
  }
  M.mark_swoosh = end();
  // Corsia verso +Z: riempimento largo 2 (si scala con la mezza larghezza) e lungo 1, bordo lungo 1,
  // fondo, freccia, segno dell'urto (una stella a punte).
  begin();
  unlit(true);
  flat_quad(v3(-1.0f, 0.03f, 0.0f), v3(1.0f, 0.03f, 0.0f), v3(1.0f, 0.03f, 1.0f), v3(-1.0f, 0.03f, 1.0f), FILL);
  M.mark_lane_fill = end();
  begin();
  unlit(true);
  flat_quad(v3(-0.07f, y, 0.0f), v3(0.07f, y, 0.0f), v3(0.07f, y, 1.0f), v3(-0.07f, y, 1.0f), RED);
  M.mark_lane_edge = end();
  begin();
  unlit(true);
  flat_quad(v3(-1.06f, y, -0.16f), v3(1.06f, y, -0.16f), v3(1.06f, y, 0.0f), v3(-1.06f, y, 0.0f), RED);
  M.mark_lane_end = end();
  begin();
  unlit(true);
  for (int s = -1; s <= 1; s += 2) {
    const float lx = 0.72f * (float)s, lz = -0.55f, l = sqrtf(lx * lx + lz * lz), nx = -lz / l * 0.1f, nz = lx / l * 0.1f;
    flat_quad(v3(lx - nx, 0.045f, lz - nz), v3(lx + nx, 0.045f, lz + nz), v3(nx, 0.045f, nz), v3(-nx, 0.045f, -nz), ARROW);
  }
  M.mark_chevron = end();
  begin();
  unlit(true);
  {
    const int c = mb_vert(&B, v3(0, 0.05f, 0));
    const int P = 9;
    int first = -1, prev = -1;
    for (int k = 0; k < 2 * P; ++k) {
      const float a = D_PI * (float)k / (float)P, r = (k & 1) ? 0.42f : 0.95f;
      const int nv = mb_vert(&B, v3(r * d_sin(a), 0.05f, r * d_cos(a)));
      if (prev >= 0) tri_from(c, prev, nv, v3(0, -1, 0), RED);
      else first = nv;
      prev = nv;
    }
    tri_from(c, prev, first, v3(0, -1, 0), RED);
  }
  M.mark_impact = end();
}

void build_shots(void) {
  // Fucilone di Astro: proiettile di energia azzurra con la scia.
  begin();
  unlit(true);
  ell(0, 0, 0.02f, 0.075f, 0.075f, 0.14f, 0x9AF8FF, 6, 3);
  ell(0, 0, -0.16f, 0.045f, 0.045f, 0.17f, 0x3FC0FF, 5, 2);
  unlit(false);
  outline(0.018f, 0x0E3A6A);
  M.bullet = end();
  // Razzo a ricerca: corpo bianco, punta arancio, alette blu; la fiamma dello scarico e' a parte.
  begin();
  ell(0, 0, -0.02f, 0.085f, 0.085f, 0.2f, 0xF4F6FA, 8, 4);
  cone(v3(0, 0, 0.12f), v3(0, 0, 1), 0.074f, 0.17f, 0xFF7A26, 8);
  cyl(v3(0, 0, -0.27f), v3(0, 0, 1), 0.06f, 0.05f, 0.06f, 0x3B4250, 6);
  for (int k = 0; k < 4; ++k) box_rot(v3(0, 0, -0.16f), v3(0.15f, 0.012f, 0.06f), xf_rot_z(D_HALF_PI * 0.5f + D_HALF_PI * (float)k), 0x2E6BD6);
  outline(0.022f);
  ball(0, 0.072f, 0.03f, 0.028f, 0x2E6BD6, 4, 3);
  skip_outline();
  M.rocket = end();
  begin();
  unlit(true);
  cone(v3(0, 0, -0.26f), v3(0, 0, -1), 0.07f, 0.3f, 0xFFA42A, 7);
  cone(v3(0, 0, -0.26f), v3(0, 0, -1), 0.04f, 0.18f, 0xFFF2A0, 5);
  M.rocket_fire = end();
  // Mochi: scheggia di cristallo di luna lilla.
  begin();
  ell(0, 0, 0.02f, 0.085f, 0.085f, 0.27f, 0xD9CBFF, 5, 2);
  outline(0.018f, 0x4A2E8A);
  shine(-0.03f, 0.04f, 0.06f, 0.028f);
  skip_outline();
  M.shard = end();
  // Marzio: palla di ghiaccio secco a faccette; il vapore bianco gira attorno (a parte).
  begin();
  ball(0, 0, 0, 0.15f, 0xCDEEFF, 6, 4);
  outline(0.022f, 0x3A6A98);
  shine(-0.05f, 0.06f, 0.12f, 0.03f);
  skip_outline();
  M.frost = end();
  begin();
  unlit(true);
  for (int k = 0; k < 3; ++k) {
    const float a = D_TWO_PI * (float)k / 3.0f;
    ball(0.19f * d_cos(a), 0.03f * (float)(k - 1), 0.19f * d_sin(a), 0.06f, 0xF4FAFF, 5, 3);
  }
  M.frost_mist = end();
  begin();
  ball(0, 0, 0, 0.22f, 0xFF8A2A, 7, 5);
  outline(0.03f, 0x5A1A08);
  unlit(true);
  ball(0.05f, 0.08f, 0.1f, 0.1f, 0xFFD24A, 5, 3);
  unlit(false);
  skip_outline();
  M.lava = end();
  // Freccia dell'arciere di terracotta (verso +Z, origine a meta'): asta chiara, punta di bronzo,
  // tre penne rosse e bianche (sottili, senza contorno). Piu' grande del vero: si deve leggere da 30 m.
  begin();
  tube(v3(0, 0, -0.55f), v3(0, 0, 0.36f), 0.032f, 0xE8C088, 4);
  cyl(v3(0, 0, 0.33f), v3(0, 0, 1), 0.065f, 0.0f, 0.22f, 0xD49A3A, 4);
  outline(0.024f);
  for (int k = 0; k < 3; ++k) {
    const float a = D_TWO_PI * (float)k / 3.0f + 0.5f, ca = d_cos(a), sa = d_sin(a);
    const uint32_t c = k ? 0xE8402E : 0xF6E8D0;
    const int p0 = mb_vert(&B, v3(0.025f * ca, 0.025f * sa, -0.56f)), p1 = mb_vert(&B, v3(0.12f * ca, 0.12f * sa, -0.5f));
    const int p2 = mb_vert(&B, v3(0.025f * ca, 0.025f * sa, -0.22f));
    mb_tri(&B, p0, p1, p2, c);                 // le due facce della penna
    mb_tri(&B, p0, p2, p1, c);
  }
  skip_outline();
  M.arrow = end();
  // Giavellotto del gladiatore: asta di legno, collo di ferro, punta di bronzo (verso +Z).
  begin();
  cyl(v3(0, 0, -1.0f), v3(0, 0, 1), 0.052f, 0.052f, 1.5f, GL_WOOD, 5);
  cyl(v3(0, 0, 0.5f), v3(0, 0, 1), 0.03f, 0.03f, 0.35f, GL_IRON, 4);
  cyl(v3(0, 0, 0.45f), v3(0, 0, 1), 0.068f, 0.068f, 0.09f, GL_BRONZE, 6);
  cone(v3(0, 0, 0.83f), v3(0, 0, 1), 0.09f, 0.24f, GL_BRONZE, 4);
  outline(0.025f);
  M.javelin = end();
}

void build_items(void) {
  begin();
  {
    const int ft = B.nt;
    const int up = mb_vert(&B, v3(0, 0.34f, 0)), dn = mb_vert(&B, v3(0, -0.34f, 0));
    int e[4];
    for (int k = 0; k < 4; ++k) {
      const float a = D_HALF_PI * (float)k;
      e[k] = mb_vert(&B, v3(0.24f * d_cos(a), 0, 0.24f * d_sin(a)));
    }
    for (int k = 0; k < 4; ++k) {
      const int a = e[k], b = e[(k + 1) & 3];
      mb_tri(&B, up, b, a, k & 1 ? 0x3CE07A : 0x5CF09A);
      mb_tri(&B, dn, a, b, k & 1 ? 0x22A055 : 0x2CB862);
    }
    // Normali verso l'esterno: se il primo triangolo guarda dentro, si girano tutti.
    if (dot(B.t[ft].n, B.v[B.t[ft].a]) < 0.0f)
      for (int i = ft; i < B.nt; ++i) {
        MeshTri& t = B.t[i];
        const uint16_t tmp = t.b;
        t.b = t.c;
        t.c = tmp;
        t.n = t.n * -1.0f;
      }
  }
  outline(0.03f, 0x0E4A26);
  M.talisman = end();
  M.talisman_glow = flat_ring(0.28f, 0.42f, 12, 0xB8FFD0, 0.025f);
  begin();
  ball(0, 0.22f, 0, 0.22f, 0x2C2A34, 8, 6);
  outline(0.025f, 0x0A0A10);
  ball(-0.07f, 0.32f, 0.12f, 0.05f, 0x9A9AB0, 4, 3);
  cyl(v3(0, 0.42f, 0), v3(0.3f, 1.0f, 0), 0.06f, 0.06f, 0.06f, 0x8A8A9A, 6);
  cyl(v3(0.02f, 0.47f, 0), v3(0.5f, 1.0f, 0), 0.015f, 0.015f, 0.1f, 0xC8A060, 3);
  skip_outline();
  M.bomb = end();
  begin();
  unlit(true);
  ball(0, 0, 0, 0.07f, 0xFFE45C, 4, 3);
  M.bomb_spark = end();

  {
    static const float R[4] = { 0.32f, 0.46f, 0.66f, 1.5f };
    static const int SEG[4] = { 10, 10, 12, 16 };
    for (int k = 0; k < 4; ++k) {
      M.shadow[k] = flat_disc(R[k], SEG[k], 0x4E6E3C);
      M.shadow_tone[0][k] = flat_disc(R[k], SEG[k], 0xA88A5A);     // sabbia
      M.shadow_tone[1][k] = flat_disc(R[k], SEG[k], 0x6E6C66);     // pietra
    }
  }
  for (int i = 0; i < 4; ++i) {
    M.ring[i] = flat_ring(0.40f, 0.50f, 16, PLAYER_RGB[i]);
    M.ring_big[i] = flat_ring(0.62f, 0.74f, 18, PLAYER_RGB[i]);
    const uint32_t tint = rgb_mix(PLAYER_RGB[i], 0xFFFFFF, 0.55f);
    begin();
    unlit(true);
    {
      const int a = mb_vert(&B, v3(-0.07f, 0.04f, 0.0f)), b = mb_vert(&B, v3(-0.07f, 0.04f, 0.32f));
      const int c = mb_vert(&B, v3(0.07f, 0.04f, 0.32f)), d = mb_vert(&B, v3(0.07f, 0.04f, 0.0f));
      mb_quad(&B, a, b, c, d, tint);
    }
    M.aim_dash[i] = end();
    M.aim_ring[i] = flat_ring(0.9f, 1.0f, 24, tint, 0.04f);
  }
  M.mark_lob = flat_ring(0.88f, 1.0f, 20, 0xFF3A2A, 0.035f);
  M.mark_fill = flat_disc(1.0f, 20, 0xC8402A, 0.03f);
  M.mark_rise = flat_ring(0.25f, 0.55f, 12, 0x6A4A2E, 0.025f);
  M.mark_blast = flat_ring(0.9f, 1.0f, 24, 0xFFE07A, 0.05f);
  build_boss_marks();
  // Crepa nel terreno lungo Z (i segni del colpo di spada sono a 0,8 m l'uno dall'altro: i pezzi si
  // toccano e fanno una crepa sola), con la riga accesa dentro; poi le pietre a punta.
  {
    static const float ZZ[4][2] = { { 0.0f, -0.5f }, { 0.09f, -0.17f }, { -0.09f, 0.17f }, { 0.0f, 0.5f } };
    for (int v = 0; v < 2; ++v) {
      begin();
      unlit(true);
      const float y = v ? 0.036f : 0.03f, w = v ? 0.042f : 0.1f;
      const uint32_t c = v ? 0xFF8A2A : 0x2E2018;
      // Tre tratti a zigzag, allungati di w ai capi perche' si sovrappongano negli angoli.
      for (int k = 0; k < 3; ++k) {
        const float dx = ZZ[k + 1][0] - ZZ[k][0], dz = ZZ[k + 1][1] - ZZ[k][1], l = sqrtf(dx * dx + dz * dz);
        const float ux = dx / l, uz = dz / l, nx = -uz, nz = ux;
        const float e0 = k == 0 ? 0.0f : w * 0.8f, e1 = k == 2 ? 0.0f : w * 0.8f;
        const float x0 = ZZ[k][0] - ux * e0, z0 = ZZ[k][1] - uz * e0, x1 = ZZ[k + 1][0] + ux * e1, z1 = ZZ[k + 1][1] + uz * e1;
        const int a = mb_vert(&B, v3(x0 - nx * w, y, z0 - nz * w)), b = mb_vert(&B, v3(x0 + nx * w, y, z0 + nz * w));
        const int cc = mb_vert(&B, v3(x1 + nx * w, y, z1 + nz * w)), d = mb_vert(&B, v3(x1 - nx * w, y, z1 - nz * w));
        mb_tri(&B, a, b, cc, c);           // verso l'alto
        mb_tri(&B, a, cc, d, c);
      }
      if (v == 0) {                       // rametti della crepa
        static const float BR[3][4] = { { 0.09f, -0.17f, 0.3f, -0.08f }, { -0.09f, 0.17f, -0.3f, 0.08f }, { 0.0f, -0.36f, -0.17f, -0.44f } };
        for (const auto& r : BR) {
          const float dx = r[2] - r[0], dz = r[3] - r[1], l = sqrtf(dx * dx + dz * dz), nx = -dz / l * 0.055f, nz = dx / l * 0.055f;
          const int a = mb_vert(&B, v3(r[0] - nx, y, r[1] - nz)), b = mb_vert(&B, v3(r[0] + nx, y, r[1] + nz));
          const int t = mb_vert(&B, v3(r[2], y, r[3]));
          mb_tri(&B, a, b, t, c);
        }
      }
      (v ? M.mark_crack_glow : M.mark_crack) = end();
    }
  }
  begin();
  {
    const uint32_t st = 0xB4A590, st2 = 0x8E806E;
    cone(v3(0.1f, -0.1f, 0.05f), v3(0.22f, 1.0f, 0.1f), 0.21f, 1.15f, st, 5);
    cone(v3(-0.15f, -0.1f, 0.0f), v3(-0.32f, 1.0f, 0.15f), 0.17f, 0.85f, st2, 5);
    cone(v3(0.0f, -0.1f, -0.17f), v3(0.05f, 1.0f, -0.4f), 0.15f, 0.66f, st, 4);
    box_rot(v3(0.22f, 0.05f, -0.2f), v3(0.09f, 0.07f, 0.08f), xf_rot_y(0.6f), st2);
    box_rot(v3(-0.24f, 0.05f, 0.18f), v3(0.08f, 0.06f, 0.07f), xf_rot_y(-0.4f), st);
    outline(0.03f);
  }
  M.rock_spikes = end();
  begin();
  unlit(true);
  ball(0, 0, 0, 1.0f, 0xFFD24A, 10, 6);
  M.blast = end();
  // Lampo del fucilone: stella azzurra rivolta avanti (+Z).
  begin();
  unlit(true);
  ell(0, 0, 0.04f, 0.11f, 0.11f, 0.07f, 0xE8FFFF, 6, 3);
  for (int k = 0; k < 4; ++k) {
    const float a = D_HALF_PI * (float)k + 0.4f;
    cone(v3(0, 0, 0.03f), v3(d_cos(a), d_sin(a), 0.25f), 0.045f, 0.2f, 0x7FEFFF, 3);
  }
  cone(v3(0, 0, 0.05f), v3(0, 0, 1), 0.06f, 0.26f, 0xBFFAFF, 4);
  M.muzzle = end();
  begin();
  bbox(-0.06f, 0.0f, -0.06f, 0.06f, 0.32f, 0.06f, 0.02f, 0xFF3A2A);
  bbox(-0.06f, -0.16f, -0.06f, 0.06f, -0.05f, 0.06f, 0.02f, 0xFF3A2A);
  outline(0.025f, 0x2A0A0A);
  M.alert = end();
  M.snow_ring = flat_ring(0.94f, 1.0f, 28, 0xDDF2FF, 0.05f);
  begin();
  ball(0, 0, 0, 0.12f, 0xD9DDE3, 7, 5);                // sbuffo di fumo (capsula aperta)
  M.smoke = end();
  // Stellina gialla (stordito): due rombi incrociati.
  begin();
  unlit(true);
  ell(0, 0, 0, 0.11f, 0.035f, 0.03f, 0xFFE45C, 4, 2);
  ell(0, 0, 0, 0.035f, 0.11f, 0.03f, 0xFFE45C, 4, 2);
  unlit(false);
  outline(0.012f, 0x6A4A08);
  M.stars = end();
}

// --- capsula dell'esploratore ----------------------------------------------------------------
// Navicella bianca e arancio su tre zampette, aperta in alto: la cupola di vetro e' solo il telaio
// con due riflessi, cosi' con la telecamera inclinata di 56 gradi si vede l'esploratore dentro.

const uint32_t CP_WHITE = 0xF1F3F7, CP_ORANGE = 0xFF7F2A, CP_DARK = 0x4A505C, CP_IN = 0x7A8494, CP_FRAME = 0xB9C6D6;
const float CP_RIM_Y = 0.62f, CP_RIM_R = 0.36f;

// Scafo a scodella con la parete di dentro, pavimento, fascia arancio, zampette.
// Bonus delle casse (0.5.4): moneta dal bordo scuro con il disegno bianco del bonus, in rilievo sulle
// due facce (dietro specchiato, cosi' girando si legge sempre uguale). Disegni fatti di pezzi convessi:
// >> per VELOCITA', una fiamma per FURIA, uno scudo per INVINCIBILE, un fulmine per CARICA.
// Barra da (ax, ay) a (bx, by) larga 2t, sulla faccia tra z0 e z1 (sx: -1 la faccia di dietro).
void coin_bar(float ax, float ay, float bx, float by, float t, float z0, float z1, float sx, uint32_t col) {
  const float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), nx = -dy / l * t, ny = dx / l * t;
  float p[4][2] = { { ax + nx, ay + ny }, { bx + nx, by + ny }, { bx - nx, by - ny }, { ax - nx, ay - ny } };
  for (int k = 0; k < 4; ++k) p[k][0] *= sx;
  if (sx < 0.0f) {                       // giro al contrario: il poligono resta nel verso giusto
    float q[4][2];
    for (int k = 0; k < 4; ++k) { q[k][0] = p[3 - k][0]; q[k][1] = p[3 - k][1]; }
    prism(q, 4, z0, z1, xf_identity(), col);
  } else {
    prism(p, 4, z0, z1, xf_identity(), col);
  }
}

void coin_poly(const float (*p)[2], int n, float z0, float z1, uint32_t col) { prism(p, n, z0, z1, xf_identity(), col); }

void build_powers(void) {
  const float R = 0.26f, T = 0.09f;
  for (int k = 0; k < 4; ++k) {
    const uint32_t c = PW_RGB[k], dark = rgb_mix(c, 0x101018, 0.45f);
    const uint32_t ink = k == 2 ? 0x5A3410 : 0xFFFFFF;    // disegno bianco, sull'oro marrone scuro
    begin();
    cyl(v3(0, 0, -0.5f * T), v3(0, 0, 1), R, R, T, dark, 16);
    cyl(v3(0, 0, -0.5f * T - 0.006f), v3(0, 0, 1), R * 0.8f, R * 0.8f, T + 0.012f, c, 16);
    outline(0.025f);
    const float zf = 0.5f * T + 0.004f, ze = zf + 0.02f;
    for (int face = 0; face < 2; ++face) {
      const float sx = face ? -1.0f : 1.0f, z0 = face ? -ze : zf, z1 = face ? -zf : ze;
      switch (k) {
        case 0:                            // VELOCITA': >>
          for (int j = 0; j < 2; ++j) {
            const float x = -0.075f + 0.09f * (float)j;
            coin_bar(x - 0.035f, 0.1f, x + 0.04f, 0.0f, 0.026f, z0, z1, sx, ink);
            coin_bar(x + 0.04f, 0.0f, x - 0.035f, -0.1f, 0.026f, z0, z1, sx, ink);
          }
          break;
        case 1: {                          // FURIA: fiamma (bianca, il cuore giallo)
          static const float F[8][2] = { { 0.0f, 0.14f }, { 0.065f, 0.03f }, { 0.085f, -0.04f }, { 0.055f, -0.105f },
                                         { 0.0f, -0.125f }, { -0.055f, -0.105f }, { -0.085f, -0.04f }, { -0.065f, 0.03f } };
          static const float G[6][2] = { { 0.0f, 0.04f }, { 0.04f, -0.04f }, { 0.03f, -0.085f }, { -0.03f, -0.085f }, { -0.04f, -0.04f },
                                         { 0.0f, 0.04f } };
          coin_poly(F, 8, z0, z1, ink);
          coin_poly(G, 5, face ? z0 - 0.008f : z0 + 0.008f, face ? z1 - 0.008f : z1 + 0.008f, 0xFFE45C);
          break;
        }
        case 2: {                          // INVINCIBILE: scudo
          static const float S[7][2] = { { -0.085f, 0.115f }, { 0.085f, 0.115f }, { 0.085f, 0.0f }, { 0.05f, -0.08f }, { 0.0f, -0.125f },
                                         { -0.05f, -0.08f }, { -0.085f, 0.0f } };
          coin_poly(S, 7, z0, z1, ink);
          break;
        }
        default:                           // CARICA: fulmine
          coin_bar(0.05f, 0.13f, -0.025f, 0.005f, 0.026f, z0, z1, sx, ink);
          coin_bar(-0.035f, 0.005f, 0.035f, 0.005f, 0.024f, z0, z1, sx, ink);
          coin_bar(0.025f, 0.005f, -0.05f, -0.13f, 0.026f, z0, z1, sx, ink);
          break;
      }
    }
    skip_outline();
    M.pw_coin[k] = end();
    M.pw_ring[k] = flat_ring(0.62f, 0.74f, 20, c);
  }
}

void pod_hull(void) {
  const Vec3 c = v3(0, 0.42f, 0), r = v3(0.43f, 0.36f, 0.43f);
  shell(c, r, 12, 6, v3(0, 1, 0), 0.55f, CP_WHITE, false);
  torus(xf_translate(v3(0, 0.4f, 0)), 0.43f, 0.045f, 12, 4, CP_ORANGE);
  for (int k = 0; k < 3; ++k) {                        // zampette
    const float a = D_TWO_PI * (float)k / 3.0f + 0.5f;
    const Vec3 d = v3(d_sin(a), 0, d_cos(a));
    tube(d * 0.3f + v3(0, 0.22f, 0), d * 0.47f + v3(0, 0.04f, 0), 0.03f, CP_DARK, 5);
    cyl(d * 0.47f, v3(0, 1, 0), 0.075f, 0.06f, 0.04f, CP_DARK, 6);
  }
  outline();
  shell(c, v3(0.395f, 0.33f, 0.395f), 12, 6, v3(0, 1, 0), 0.55f, CP_IN, true);
  torus(xf_translate(v3(0, CP_RIM_Y, 0)), CP_RIM_R + 0.01f, 0.035f, 12, 3, CP_ORANGE);   // bordo
  mb_disc(&B, v3(0, CAPSULE_FLOOR, 0), 0.34f, 10, 0x5E6878);              // pavimento
  for (int k = 0; k < 4; ++k) {                       // oblo' finti e ribattini sulla fascia
    const float a = D_HALF_PI * (float)k + 0.25f;
    ball(0.445f * d_sin(a), 0.4f, 0.445f * d_cos(a), 0.03f, 0xFFFFFF, 4, 2);
  }
  skip_outline();
}

// Arco del telaio della cupola sul piano verticale che forma l'angolo yaw con X.
void dome_arc(float yaw, float a0, float a1, const Xform& xf) {
  const int fv = B.nv, ft = B.nt;
  const int N = 6;
  for (int k = 0; k < N; ++k) {
    const float t0 = a0 + (a1 - a0) * (float)k / (float)N, t1 = a0 + (a1 - a0) * (float)(k + 1) / (float)N;
    const Vec3 p0 = v3(CP_RIM_R * d_cos(t0) * d_cos(yaw), CP_RIM_Y + 0.42f * d_sin(t0), CP_RIM_R * d_cos(t0) * d_sin(yaw));
    const Vec3 p1 = v3(CP_RIM_R * d_cos(t1) * d_cos(yaw), CP_RIM_Y + 0.42f * d_sin(t1), CP_RIM_R * d_cos(t1) * d_sin(yaw));
    tube(p0, p1, 0.026f, CP_FRAME, 4);
  }
  mb_apply(&B, fv, ft, xf);
}

void build_capsule(void) {
  begin();
  pod_hull();
  // Telaio della cupola: due archi incrociati in diagonale (davanti resta libero per la faccia).
  dome_arc(D_HALF_PI * 0.5f, -0.05f, D_PI + 0.05f, xf_identity());
  dome_arc(-D_HALF_PI * 0.5f, -0.05f, D_PI + 0.05f, xf_identity());
  ball(0, CP_RIM_Y + 0.42f, 0, 0.05f, CP_FRAME, 5, 3);
  // Antenna dietro a destra (la lucina in cima e' a parte e lampeggia).
  tube(v3(0.24f, CP_RIM_Y - 0.02f, -0.24f), v3(0.3f, CAPSULE_LIGHT_Y - 0.04f, -0.3f), 0.018f, CP_DARK, 4);
  outline(0.022f);
  // Riflessi del vetro.
  unlit(true);
  {
    const float yaw = -2.2f;
    for (int k = 0; k < 3; ++k) {
      const float t0 = 0.55f + 0.17f * (float)k, t1 = t0 + 0.17f;
      const Vec3 p0 = v3(0.37f * d_cos(t0) * d_cos(yaw), CP_RIM_Y + 0.43f * d_sin(t0), -0.37f * d_cos(t0) * d_sin(yaw));
      const Vec3 p1 = v3(0.37f * d_cos(t1) * d_cos(yaw), CP_RIM_Y + 0.43f * d_sin(t1), -0.37f * d_cos(t1) * d_sin(yaw));
      tube(p0, p1, 0.018f, 0xFFFFFF, 4);
    }
    ball(0.37f * d_cos(1.15f) * d_cos(yaw) + 0.02f, CP_RIM_Y + 0.43f * d_sin(1.15f) + 0.02f, -0.37f * d_cos(1.15f) * d_sin(yaw), 0.022f, 0xFFFFFF, 4, 2);
  }
  unlit(false);
  skip_outline();
  M.capsule = end();
  begin();
  unlit(true);
  ball(0.3f, CAPSULE_LIGHT_Y, -0.3f, 0.06f, 0xFF3A2A, 6, 3);
  unlit(false);
  outline(0.015f, 0x4A0A0A);
  M.capsule_light = end();
}

// Capsula aperta: telaio saltato via (un arco piegato a terra), antenna storta, bruciature sul bordo.
void build_capsule_open(void) {
  begin();
  pod_hull();
  dome_arc(D_HALF_PI * 0.5f, -0.05f, 0.9f, xf_identity());                 // moncone ancora attaccato
  dome_arc(-D_HALF_PI * 0.5f, -0.05f, 0.55f, xf_identity());
  // Arco staccato, a terra accanto alla capsula.
  dome_arc(0.0f, 0.1f, D_PI - 0.1f, xf_mul(xf_translate(v3(0.62f, 0.035f, 0.42f)),
                                           xf_mul(xf_rot_y(0.9f), xf_mul(xf_rot_x(-D_HALF_PI), xf_translate(v3(0, -CP_RIM_Y, 0))))));
  tube(v3(0.24f, CP_RIM_Y - 0.02f, -0.24f), v3(0.4f, 1.0f, -0.22f), 0.018f, CP_DARK, 4);   // antenna storta
  outline(0.022f);
  for (int k = 0; k < 5; ++k) {                                            // bruciature sul bordo
    const float a = 0.9f * (float)k + 0.3f;
    ell(CP_RIM_R * d_sin(a), CP_RIM_Y + 0.025f, CP_RIM_R * d_cos(a), 0.08f, 0.03f, 0.08f, 0x3A3A42, 5, 2);
  }
  skip_outline();
  M.capsule_open = end();
}

// Esploratore: bambino astronauta alto 0,75 m con la tuta arancio e il casco a bolla (vetro azzurro
// con un'apertura davanti per la faccia), braccia in alto ("aiuto!"). Un pezzo solo: lo anima il salto.
void build_explorer(void) {
  const uint32_t suit = 0xFF8A2A, suit2 = 0xE06A1A, white = 0xF4F6FA, skin = 0xFFD2B0, hair = 0x8A4E2A;
  const uint32_t glass = 0xBFE6FA, glass_in = 0x8FC2DC, blush = 0xF7939F, dark = 0x2B2238;
  begin();
  ell(-0.07f, 0.045f, 0.03f, 0.06f, 0.045f, 0.08f, white, 5, 3);        // stivaletti
  ell(0.07f, 0.045f, 0.03f, 0.06f, 0.045f, 0.08f, white, 5, 3);
  cyl(v3(-0.07f, 0.07f, 0), v3(0, 1, 0), 0.05f, 0.05f, 0.14f, suit, 5);
  cyl(v3(0.07f, 0.07f, 0), v3(0, 1, 0), 0.05f, 0.05f, 0.14f, suit, 5);
  ell(0, 0.31f, 0, 0.14f, 0.16f, 0.12f, suit, 6, 4);                  // corpo: y 0.15..0.47
  bbox(-0.09f, 0.22f, -0.17f, 0.09f, 0.42f, -0.08f, 0.02f, white);     // zainetto
  for (int s = -1; s <= 1; s += 2) {                                    // braccia in alto
    const float sx = (float)s;
    const Vec3 sh = v3(sx * 0.11f, 0.4f, 0.0f), d = normalize(v3(sx * 0.65f, 1.0f, 0.15f));
    cyl(sh, d, 0.045f, 0.04f, 0.2f, suit, 5);
    ball(sh.x + d.x * 0.23f, sh.y + d.y * 0.23f, sh.z + d.z * 0.23f, 0.052f, white, 5, 3);
  }
  ball(0, 0.58f, 0.01f, 0.12f, skin, 7, 4);                              // testa
  ell(0, 0.63f, -0.02f, 0.125f, 0.09f, 0.12f, hair, 6, 3);
  outline();
  ell(0, 0.29f, 0.115f, 0.05f, 0.05f, 0.015f, white, 5, 2);            // stemma
  ell(0, 0.29f, 0.125f, 0.025f, 0.025f, 0.01f, 0x2E6BD6, 4, 2);
  ell(0, 0.18f, 0.0f, 0.13f, 0.03f, 0.11f, suit2, 7, 2);               // cintura
  ball(-0.045f, 0.59f, 0.115f, 0.022f, dark, 4, 3);                       // occhi
  ball(0.045f, 0.59f, 0.115f, 0.022f, dark, 4, 3);
  shine(-0.05f, 0.598f, 0.135f, 0.007f);
  shine(0.04f, 0.598f, 0.135f, 0.007f);
  ell(0, 0.54f, 0.113f, 0.035f, 0.016f, 0.012f, 0xC2384A, 5, 2);       // bocca aperta
  ell(-0.08f, 0.55f, 0.09f, 0.025f, 0.015f, 0.015f, blush, 4, 2);
  ell(0.08f, 0.55f, 0.09f, 0.025f, 0.015f, 0.015f, blush, 4, 2);
  torus(xf_translate(v3(0, 0.45f, 0)), 0.12f, 0.03f, 8, 3, white);      // collare del casco
  skip_outline();
  // Casco a bolla: vetro azzurro con l'apertura davanti, parete di dentro, riflesso.
  const Vec3 hc = v3(0, 0.58f, 0.0f);
  const Vec3 hole = normalize(v3(0, 0.2f, 1.0f));
  shell(hc, v3(0.19f, 0.19f, 0.19f), 9, 5, hole, 0.45f, glass, false);
  outline(0.02f, 0x3A5A7A);
  shell(hc, v3(0.18f, 0.18f, 0.18f), 9, 5, hole, 0.45f, glass_in, true);
  unlit(true);
  ell_rot(v3(-0.1f, 0.7f, 0.06f), v3(0.05f, 0.016f, 0.02f), xf_mul(xf_rot_y(-0.5f), xf_rot_z(0.7f)), 0xFFFFFF, 4, 2);
  unlit(false);
  skip_outline();
  M.explorer = end();
}

void build_particles(void) {
  for (int i = 0; i < NPCOL; ++i) {
    begin();
    bbox(-0.07f, -0.07f, -0.07f, 0.07f, 0.07f, 0.07f, 0.0f, PARTICLE_RGB[i]);
    M.cube[i] = end();
    begin();
    ball(0, 0, 0, 0.12f, PARTICLE_RGB[i], 5, 3);
    M.ball[i] = end();
    begin();
    unlit(true);
    ball(0, 0, 0, 0.12f, PARTICLE_RGB[i], 4, 3);
    M.glow[i] = end();
  }
}

void free_char(CharMesh& c) {
  mesh_free(&c.body);
  mesh_free(&c.arm_l);
  mesh_free(&c.arm_r);
  mesh_free(&c.leg);
  mesh_free(&c.fx);
  for (int i = 0; i < 4; ++i) mesh_free(&c.accent[i]);
}

}  // namespace

void models_build(void) {
  memset(&M, 0, sizeof(M));
  build_astro(M.ch[CH_ASTRO]);
  build_mochi(M.ch[CH_MOCHI]);
  build_marzio(M.ch[CH_MARZIO]);
  build_zolfo(M.ch[CH_ZOLFO]);
  build_scorpion();
  build_archer();
  build_scarab();
  build_warrior();
  build_boss();
  build_shots();
  build_items();
  build_powers();
  build_capsule();
  build_capsule_open();
  build_explorer();
  build_particles();
}

void models_free(void) {
  for (int c = 0; c < NCHARS; ++c) free_char(M.ch[c]);
  for (int k = 0; k < SCORPION_SEGS; ++k) mesh_free(&M.scorpion_tail[k]);
  Mesh* rest[] = { &M.scorpion_body, &M.scorpion_leg, &M.scorpion_claw_l, &M.scorpion_claw_r, &M.scorpion_finger_l,
                   &M.scorpion_finger_r, &M.scorpion_sting, &M.archer_body, &M.archer_arm_l, &M.archer_upper_r, &M.archer_fore_r,
                   &M.archer_thigh, &M.archer_shin, &M.bow, &M.bow_string,
                   &M.scarab_body, &M.scarab_wing_l, &M.scarab_wing_r, &M.warrior_body, &M.warrior_arm_l, &M.warrior_arm_r,
                   &M.warrior_leg, &M.halberd, &M.boss_body, &M.boss_head, &M.boss_arm_l, &M.boss_arm_r, &M.boss_leg_l,
                   &M.boss_leg_r, &M.boss_sword, &M.boss_shield, &M.bullet, &M.rocket, &M.rocket_fire, &M.shard, &M.frost,
                   &M.frost_mist, &M.lava, &M.arrow, &M.javelin, &M.talisman, &M.talisman_glow, &M.bomb, &M.bomb_spark,
                   &M.mark_lob, &M.mark_fill, &M.mark_crack, &M.mark_crack_glow, &M.rock_spikes, &M.mark_rise, &M.mark_blast,
                   &M.mark_sector, &M.mark_sector_fill, &M.mark_sector_hit, &M.mark_swoosh, &M.mark_lane_fill, &M.mark_lane_edge,
                   &M.mark_lane_end, &M.mark_chevron, &M.mark_impact,
                   &M.blast, &M.muzzle, &M.alert, &M.snow_ring, &M.stars, &M.smoke,
                   &M.capsule, &M.capsule_open, &M.capsule_light, &M.explorer };
  for (Mesh* m : rest) mesh_free(m);
  for (int i = 0; i < 4; ++i) {
    mesh_free(&M.shadow[i]);
    mesh_free(&M.shadow_tone[0][i]);
    mesh_free(&M.shadow_tone[1][i]);
    mesh_free(&M.ring[i]);
    mesh_free(&M.ring_big[i]);
    mesh_free(&M.aim_dash[i]);
    mesh_free(&M.aim_ring[i]);
    mesh_free(&M.pw_coin[i]);
    mesh_free(&M.pw_ring[i]);
  }
  for (int i = 0; i < NPCOL; ++i) {
    mesh_free(&M.cube[i]);
    mesh_free(&M.ball[i]);
    mesh_free(&M.glow[i]);
  }
}
