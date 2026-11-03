// Percorsi: campo di distanze su una griglia di celle da mezzo metro che copre la mappa.
// I mostri scendono il campo per arrivare ai giocatori girando attorno agli ostacoli; le celle
// che toccano un ostacolo costano un po' di piu', cosi' i percorsi stanno lontani dagli spigoli.
#include "game/sim/game_int.h"
#include <string.h>

namespace {

const float CELL = 0.5f;

inline float fx0(Game& g) { return -(float)g.L.W * 0.5f; }
inline float fz0(Game& g) { return -(float)g.L.H; }

int cell_of(Game& g, float x, float z) {
  const float fx = (x - fx0(g)) / CELL, fz = (z - fz0(g)) / CELL;
  if (fx < 0.0f || fz < 0.0f) return -1;
  const int cx = (int)fx, cz = (int)fz;
  if (cx >= g.session.flow_w || cz >= g.session.flow_h) return -1;
  return cz * g.session.flow_w + cx;
}

void build_blocks(Game& g) {
  g.session.flow_w = g.L.W * 2;
  g.session.flow_h = g.L.H * 2;
  for (int z = 0; z < g.session.flow_h; ++z)
    for (int x = 0; x < g.session.flow_w; ++x) g.session.flow_blk[z * g.session.flow_w + x] = level_blocks(&g.L, x / 2, z / 2, 0.0f, PL_STEP, BODY_WALK | BODY_PIT) ? 1 : 0;   // anche il cenote
  for (int z = 0; z < g.session.flow_h; ++z)
    for (int x = 0; x < g.session.flow_w; ++x) {
      uint8_t& b = g.session.flow_blk[z * g.session.flow_w + x];
      if (b) continue;
      if ((x > 0 && g.session.flow_blk[z * g.session.flow_w + x - 1] == 1) || (x + 1 < g.session.flow_w && g.session.flow_blk[z * g.session.flow_w + x + 1] == 1) ||
          (z > 0 && g.session.flow_blk[(z - 1) * g.session.flow_w + x] == 1) || (z + 1 < g.session.flow_h && g.session.flow_blk[(z + 1) * g.session.flow_w + x] == 1))
        b = 2;
    }
}

inline bool free_cell(Game& g, int c) { return g.session.flow_blk[c] != 1; }

}  // namespace

// Dijkstra a secchi: passi da 10 in orizzontale e verticale, 14 in diagonale, +6 per le celle
// vicine agli ostacoli. Le partenze arrivano fino a 20 e un passo aggiunge al massimo 20, quindi
// in coda non ci sono mai piu' di 41 valori diversi: bastano 64 secchi.
void flow_build(Game& g, uint16_t* field, const Vec3* goals, int ngoals, int zone) {
  enum { NB = 64, CAP = 4096 };   // celle alla stessa distanza: al massimo un fronte d'onda
  static uint16_t qb[NB][CAP];
  int qn[NB];
  memset(qn, 0, sizeof(qn));
  build_blocks(g);
  int bx0 = 0, bz0 = 0, bx1 = g.session.flow_w - 1, bz1 = g.session.flow_h - 1;
  if (zone >= 0 && zone < g.L.nzones) {
    const ZoneDef& zd = g.L.zones[zone];
    bx0 = zd.x0 * 2 - 2; bx1 = zd.x1 * 2 + 3;
    bz0 = zd.z0 * 2 - 2; bz1 = zd.z1 * 2 + 3;
    if (bx0 < 0) bx0 = 0;
    if (bz0 < 0) bz0 = 0;
    if (bx1 >= g.session.flow_w) bx1 = g.session.flow_w - 1;
    if (bz1 >= g.session.flow_h) bz1 = g.session.flow_h - 1;
  }
  for (int i = 0; i < g.session.flow_w * g.session.flow_h; ++i) field[i] = FLOW_FAR;
  int pending = 0;
#define FLOW_PUSH(c, d) do { \
    if ((d) < field[c]) { \
      field[c] = (uint16_t)(d); \
      const int b_ = (d) & (NB - 1); \
      if (qn[b_] < CAP) { qb[b_][qn[b_]++] = (uint16_t)(c); pending++; } \
    } \
  } while (0)
  for (int k = 0; k < ngoals; ++k) {
    const int c = cell_of(g, goals[k].x, goals[k].z);
    if (c < 0) continue;
    if (free_cell(g, c)) { FLOW_PUSH(c, 0); continue; }
    // Obiettivo sopra un ostacolo (una cassa, una piattaforma): partono le celle libere vicine.
    const int cx = c % g.session.flow_w, cz = c / g.session.flow_w;
    int rmin = -1;
    for (int r = 1; r <= 8; ++r) {
      for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx) {
          if (dx > -r && dx < r && dz > -r && dz < r) continue;
          const int x = cx + dx, z = cz + dz;
          if (x < bx0 || z < bz0 || x > bx1 || z > bz1) continue;
          const int n = z * g.session.flow_w + x;
          if (!free_cell(g, n)) continue;
          if (rmin < 0) rmin = r;
          FLOW_PUSH(n, (r - rmin) * 10);
        }
      if (rmin >= 0 && r >= rmin + 2) break;
    }
  }
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  for (int d = 0; pending > 0; ++d) {
    const int b = d & (NB - 1);
    for (int k = 0; k < qn[b]; ++k) {
      const int c = qb[b][k];
      if (field[c] != d) continue;
      const int cx = c % g.session.flow_w, cz = c / g.session.flow_w;
      for (int j = 0; j < 8; ++j) {
        const int x = cx + DX[j], z = cz + DZ[j];
        if (x < bx0 || z < bz0 || x > bx1 || z > bz1) continue;
        const int n = z * g.session.flow_w + x;
        if (!free_cell(g, n)) continue;
        if (j >= 4 && (!free_cell(g, cz * g.session.flow_w + x) || !free_cell(g, z * g.session.flow_w + cx))) continue;   // niente angoli tagliati
        const int nd = d + (j < 4 ? 10 : 14) + (g.session.flow_blk[n] == 2 ? 6 : 0);
        FLOW_PUSH(n, nd);
      }
    }
    pending -= qn[b];
    qn[b] = 0;
  }
#undef FLOW_PUSH
}

bool flow_dir(Game& g, const uint16_t* field, float x, float z, float* dx, float* dz) {
  const int c = cell_of(g, x, z);
  if (c < 0) return false;
  const int cx = c % g.session.flow_w, cz = c / g.session.flow_w;
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  int best = free_cell(g, c) ? field[c] : FLOW_FAR, bn = -1;
  for (int j = 0; j < 8; ++j) {
    const int nx = cx + DX[j], nz = cz + DZ[j];
    if (nx < 0 || nz < 0 || nx >= g.session.flow_w || nz >= g.session.flow_h) continue;
    const int n = nz * g.session.flow_w + nx;
    if (!free_cell(g, n)) continue;
    if (j >= 4 && (!free_cell(g, cz * g.session.flow_w + nx) || !free_cell(g, nz * g.session.flow_w + cx))) continue;
    if (field[n] < best) { best = field[n]; bn = n; }
  }
  if (bn < 0) return false;
  const float tx = fx0(g) + ((float)(bn % g.session.flow_w) + 0.5f) * CELL, tz = fz0(g) + ((float)(bn / g.session.flow_w) + 0.5f) * CELL;
  const float ex = tx - x, ez = tz - z, l = sqrtf(ex * ex + ez * ez);
  if (l < 1e-4f) return false;
  *dx = ex / l;
  *dz = ez / l;
  return true;
}

bool walk_clear(Game& g, Vec3 a, Vec3 b, float r, bool flying) {
  float ex = b.x - a.x, ez = b.z - a.z;
  const float l = sqrtf(ex * ex + ez * ez);
  if (l < 1e-4f) return true;
  ex /= l;
  ez /= l;
  const float px = -ez * r, pz = ex * r;
  for (int k = -1; k <= 1; ++k) {
    const Vec3 p0 = v3(a.x + px * (float)k, 0.0f, a.z + pz * (float)k);
    const Vec3 p1 = v3(b.x + px * (float)k, 0.0f, b.z + pz * (float)k);
    if (level_walk_blocked(&g.L, p0, p1, flying)) return false;
  }
  return true;
}
