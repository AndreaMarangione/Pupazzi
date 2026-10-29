// Geometria fissa della mappa nello stile della mappa (Piramidi, Chichen Itza, Muraglia, Colosseo).
//
// Terreno senza scacchiera: ogni casella, anche quelle del paesaggio attorno, ha una "chiave" (tipo
// di superficie e tono). Il terreno si costruisce sulla griglia duale, quadrati di 1 m centrati sugli
// spigoli delle caselle: dove le chiavi cambiano gli angoli si arrotondano (raggio fino a mezzo
// metro: una casella d'acqua da sola diventa un cerchio, i gradini di una diagonale un'onda), dove
// non cambiano i quadrati si uniscono in rettangoli grandi. I toni vengono da un rumore a bassa
// frequenza (macchie larghe qualche metro, poco contrasto). Le quote diverse (muri, piattaforme,
// acqua) si raccordano con un profilo che corre lungo il bordo arrotondato (cornici, fasce, rive,
// schiuma). Le collisioni restano sulle caselle (level.cpp): le forme si scostano dalle caselle al
// massimo di circa 0,2 m.
//
// La geometria e' divisa in blocchi di 8x8 caselle (il renderer scarta quelli fuori
// dall'inquadratura); il paesaggio lontano sta in L->scenery. Attorno alla mappa (0.5.1) si
// disegna solo la striscia che la telecamera di gioco puo' mostrare (LV_MARGIN_*: terreno
// economico e poche sagome), oltre il terreno piatto e qualche forma grande solo dove la vedono i
// menu (a sud) e la fine della mappa (a nord). Cespugli, erba alta, casse e
// trampolini sono oggetti mobili: qui si costruiscono le loro mesh (L->dyn_mesh), li disegna
// game_draw.cpp. Nulla di tutto questo tocca lo stato della partita: la varieta' viene da funzioni
// hash delle caselle, mai dal generatore di numeri casuali del gioco.
#include "level.h"
#include <stdlib.h>
#include <string.h>

namespace {

// --- utilita' --------------------------------------------------------------------------------

uint32_t hash2(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ (uint32_t)s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
float hf(int x, int y, int s) { return (float)(hash2(x, y, s) & 0xFFFFFFu) * (1.0f / 16777216.0f); }   // [0, 1)
float hs(int x, int y, int s) { return hf(x, y, s) * 2.0f - 1.0f; }                                    // [-1, 1)

// Rumore a valori, liscio, in [0, 1].
float vnoise(float x, float z, int seed) {
  const float fx = floorf(x), fz = floorf(z);
  const int ix = (int)fx, iz = (int)fz;
  float tx = x - fx, tz = z - fz;
  tx = tx * tx * (3.0f - 2.0f * tx);
  tz = tz * tz * (3.0f - 2.0f * tz);
  const float a = hf(ix, iz, seed), b = hf(ix + 1, iz, seed), c = hf(ix, iz + 1, seed), d = hf(ix + 1, iz + 1, seed);
  return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), tz);
}
// Due ottave: macchie di circa "scale" metri.
float fbm(float x, float z, int seed, float scale) {
  return 0.68f * vnoise(x / scale, z / scale, seed) + 0.32f * vnoise(x / (scale * 0.41f), z / (scale * 0.41f), seed + 101);
}

uint32_t mix(uint32_t a, uint32_t b, float t) {
  uint32_t o = 0;
  for (int sh = 16; sh >= 0; sh -= 8) {
    const float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
    float v = ca + (cb - ca) * t + 0.5f;
    v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
    o |= (uint32_t)v << sh;
  }
  return o;
}
uint32_t lighten(uint32_t c, float t) { return t >= 0.0f ? mix(c, 0xFFFFFF, t) : mix(c, 0x000000, -t); }

// Triangolo con la faccia verso "facing"; scarta quelli degeneri.
void tri_f(MeshBuilder* mb, Vec3 a, Vec3 b, Vec3 c, Vec3 facing, uint32_t col) {
  const Vec3 n = cross(b - a, c - a);
  if (dot(n, n) < 1e-12f) return;
  const int ia = mb_vert(mb, a), ib = mb_vert(mb, b), ic = mb_vert(mb, c);
  if (dot(n, facing) < 0.0f) mb_tri(mb, ia, ic, ib, col);
  else mb_tri(mb, ia, ib, ic, col);
}
void quad_f(MeshBuilder* mb, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 facing, uint32_t col) {
  tri_f(mb, p0, p1, p2, facing, col);
  tri_f(mb, p0, p2, p3, facing, col);
}
// Ventaglio di un poligono convesso (p[0] e' il perno), con i vertici in comune.
void fan_f(MeshBuilder* mb, const Vec3* p, int n, Vec3 facing, uint32_t col) {
  if (n < 3) return;
  int idx[48];
  if (n > 48) n = 48;
  for (int i = 0; i < n; ++i) idx[i] = -1;
  idx[0] = mb_vert(mb, p[0]);
  for (int i = 1; i + 1 < n; ++i) {
    const Vec3 nn = cross(p[i] - p[0], p[i + 1] - p[0]);
    if (dot(nn, nn) < 1e-12f) continue;
    if (idx[i] < 0) idx[i] = mb_vert(mb, p[i]);
    if (idx[i + 1] < 0) idx[i + 1] = mb_vert(mb, p[i + 1]);
    if (dot(nn, facing) < 0.0f) mb_tri(mb, idx[0], idx[i + 1], idx[i], col);
    else mb_tri(mb, idx[0], idx[i], idx[i + 1], col);
  }
}
void quad_up(MeshBuilder* mb, float x0, float z0, float x1, float z1, float y, uint32_t c) {
  quad_f(mb, v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1), v3(0, 1, 0), c);
}
void unlit(MeshBuilder* mb, bool on) { mb->flags = on ? (uint8_t)(mb->flags | TRI_UNLIT) : (uint8_t)(mb->flags & ~TRI_UNLIT); }

// Pezzo di mesh costruito attorno all'origine, poi messo al suo posto (ruotato e scalato).
struct Part { int v0, t0; };
Part part(const MeshBuilder* mb) { Part p; p.v0 = mb->nv; p.t0 = mb->nt; return p; }
void place(MeshBuilder* mb, Part p, Vec3 pos, float yaw, float s) {
  mb_apply(mb, p.v0, p.t0, xf_mul(xf_translate(pos), xf_mul(xf_rot_y(yaw), xf_scale(s, s, s))));
}
void place_xf(MeshBuilder* mb, Part p, const Xform& xf) { mb_apply(mb, p.v0, p.t0, xf); }
void outline(MeshBuilder* mb, Part p, float t, uint32_t c) { mb_outline(mb, p.t0, t, c); }

// Cassa con gli spigoli smussati e il contorno.
void obox(MeshBuilder* mb, Vec3 mn, Vec3 mx, float bev, uint32_t c, uint32_t line, float t) {
  const Part p = part(mb);
  if (bev > 0.0f) mb_bevel_box(mb, mn, mx, bev, c);
  else mb_box(mb, mn, mx, c);
  if (t > 0.0f) outline(mb, p, t, line);
}
// Cassa senza il fondo (10 triangoli): per i dettagli appoggiati su qualcosa.
void box5(MeshBuilder* mb, Vec3 mn, Vec3 mx, uint32_t c) {
  const Vec3 q[8] = { v3(mn.x, mn.y, mn.z), v3(mx.x, mn.y, mn.z), v3(mx.x, mn.y, mx.z), v3(mn.x, mn.y, mx.z),
                      v3(mn.x, mx.y, mn.z), v3(mx.x, mx.y, mn.z), v3(mx.x, mx.y, mx.z), v3(mn.x, mx.y, mx.z) };
  int i[8];
  for (int k = 0; k < 8; ++k) i[k] = mb_vert(mb, q[k]);
  mb_quad(mb, i[4], i[7], i[6], i[5], c);   // +Y
  mb_quad(mb, i[1], i[5], i[6], i[2], c);   // +X
  mb_quad(mb, i[0], i[3], i[7], i[4], c);   // -X
  mb_quad(mb, i[3], i[2], i[6], i[7], c);   // +Z
  mb_quad(mb, i[0], i[4], i[5], i[1], c);   // -Z
}
// --- stili ----------------------------------------------------------------------------------

// Tipi di terreno (la chiave di una casella e' tipo * 4 + tono).
enum { TK_FLOOR = 0, TK_PATH, TK_BED, TK_DIRT, TK_WATER, TK_WALL, TK_PLAT, TK_OUT, TK_PIT, TK_PAVE, TK_BLOCK, TK_NKINDS };
const int NKEYS = TK_NKINDS * 4;

struct Style {
  // terreno: colore di base dei tipi e quanto cambiano i toni
  uint32_t floor, path, bed, dirt, water, water_deep, out, out2, pave;
  float floor_var, out_var;
  uint32_t foam, bank;
  // muri
  uint32_t wall, wall_top, wall_dark, wall_band, wall_band2, wall_trim, wall_line;
  float wall_round;
  // piattaforme
  uint32_t plat_side, plat_top, plat_trim;
  float plat_round;
  // oggetti
  uint32_t trunk, trunk_dark, leaf0, leaf1, leaf_line;
  uint32_t rock, rock_dark, rock_line;
  uint32_t low, low_top;
  uint32_t block, block_top;
  uint32_t deco0, deco1, deco2;
  uint32_t gate, gate_dark, gate_metal;
  uint32_t outline;
};

const Style STYLES[NSTYLES] = {
  // Piramidi: sabbia calda, lastre di calcare, arenaria, acqua turchese.
  { 0xE8C489, 0xF1E2C2, 0xB7B66A, 0xD6AE72, 0x3DAAD3, 0x2E8FC0, 0xEDC98E, 0xE2B978, 0xD9C7A2, 0.05f, 0.06f,
    0xE4F7FF, 0xB98E57,
    0xCF9455, 0xF4E4C4, 0x9C6E3E, 0x2F6FB0, 0xC4483A, 0xF8EACD, 0x8E6436, 0.10f,
    0xD5A86E, 0xEEDAB2, 0xF8EACD, 0.10f,
    0x9C6B3D, 0x734C2B, 0x5DA73E, 0x4A9233, 0x1F3D16,
    0xD3AB74, 0xB48A57, 0x5E4326,
    0xD8B07A, 0xEBCB97,
    0xDDB27A, 0xEECB95,
    0xC77B47, 0x8E6436, 0x5A8A3A,
    0xD5A86E, 0x8E6436, 0x5B5F66,
    0x4A321C },
  // Chichen Itza: prato della giungla, piazze e sacbe di calcare chiaro, pietra maya con i fregi
  // dipinti (rosso e blu maya), cenote verde scuro.
  { 0x6CB24A, 0xE6DFC9, 0x5C9C3C, 0x7C6C4A, 0x3DA79E, 0x2C8C86, 0x4C8E39, 0x447F33, 0xD9D1BB, 0.06f, 0.05f,
    0xE4F7F0, 0x8A7A5A,
    0xCDC4A8, 0xE0D8BF, 0x8E856E, 0x3E9DB0, 0xB0412E, 0xEAE3CC, 0x6E6650, 0.10f,
    0xC4BBA0, 0xDCD4BB, 0xEAE3CC, 0.06f,
    0x8C7C64, 0x6A5C48, 0x3E9A3E, 0x2E8434, 0x143A1A,
    0xC2BAA0, 0x948C74, 0x4A4436,
    0xC9C0A4, 0xDDD5BC,
    0xCBC2A6, 0xDFD7BF,
    0xC2683A, 0x2FB08A, 0xE8C04A,
    0x8A5A34, 0x5A3A22, 0x4A4E50,
    0x2A2820 },
  // Grande Muraglia: colline verdi, mattoni grigi, pini, bambu'.
  { 0x92C766, 0xC9BB98, 0x9DB85C, 0x8A7754, 0x48A9CC, 0x3690BA, 0x84BB5C, 0x76AC52, 0xB7B3A8, 0.05f, 0.07f,
    0xE4F7FF, 0x8C8678,
    0x9A9792, 0xB2AFA8, 0x6C6A66, 0xB33A2E, 0x7E7C78, 0xC5C1B9, 0x55534F, 0.08f,
    0x9C9993, 0xB8B4AB, 0xC9C5BC, 0.08f,
    0x6B4A33, 0x4E3523, 0x3A7D45, 0x2E6B3A, 0x163020,
    0x9B968C, 0x7C776E, 0x35322D,
    0x9A9792, 0xB2AFA8,
    0xA19D95, 0xB9B5AC,
    0xC0392B, 0xE8B53A, 0x2E2A26,
    0xB33A2E, 0x4A4E57, 0x3A3D44,
    0x262624 },
  // Colosseo: sabbia chiara dell'arena, travertino, marmo.
  { 0xEAD8AA, 0xE0CB98, 0xC6BA78, 0xCFB282, 0x4AAFD3, 0x3A96C0, 0xD9C9A4, 0xCDBB94, 0xD8CCB2, 0.04f, 0.05f,
    0xE4F7FF, 0xB09872,
    0xD6C8AA, 0xEFE7D6, 0x9C8D70, 0xA34A3A, 0x7A2E26, 0xF4EEE0, 0x8C7C60, 0.5f,
    0xD2C4A6, 0xE9E0CC, 0xF4EEE0, 0.08f,
    0x6E5A3A, 0x4E3E26, 0x2F6B3A, 0x3E7D45, 0x16301C,
    0xE4DCCB, 0xC2B8A3, 0x5A5244,
    0xD6C8AA, 0xEFE7D6,
    0x9A6A40, 0xB27C4C,
    0xC77B47, 0xA34A3A, 0x6B6E73,
    0x8B5E3C, 0x5A3A22, 0x50545A,
    0x3A3026 },
};

// --- costruzione: stato comune ----------------------------------------------------------------

const int EW_MAX = LV_MAX_W + 2 * LV_MARGIN_X, EH_MAX = LV_MAX_H + LV_MARGIN_N + LV_MARGIN_S;

struct Build {
  Level* L;
  const Style* S;
  int style;
  int MW, MN, ew, eh;     // margine a ovest e a nord, dimensioni della griglia estesa (mappa + paesaggio)
  bool pits;              // la mappa ha il cenote: il piano sotto tutto scende sotto la sua acqua
  float ox, oz;           // mondo = griglia estesa + (ox, oz)
  uint8_t key[EH_MAX][EW_MAX];
  float ky[NKEYS];        // quota della superficie
  uint32_t krgb[NKEYS];
  uint8_t kprio[NKEYS];   // chi resta unito negli incroci a scacchiera
  float kround[NKEYS];    // raggio degli angoli
  float far_y;            // quota del terreno lontano
  // Blocchi di muro di almeno 2x2 caselle: piramidi, obelischi, capanne, torri, statue.
  int nblocks;
  int16_t blk[64][4];     // x, z, w, h (caselle della mappa)
  int8_t blk_at[LV_MAX_H][LV_MAX_W];
};
Build B;

inline int kkind(int k) { return k >> 2; }
inline int cell_ext(int i, int j) {   // tipo di casella della griglia estesa (C_VOID fuori dalla mappa)
  const int cx = i - B.MW, cz = j - B.MN;
  if (cx < 0 || cz < 0 || cx >= B.L->W || cz >= B.L->H) return C_VOID;
  return B.L->cell[cz][cx];
}
inline int cell_or_void(const Level* L, int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= L->W || cz >= L->H) return C_VOID;
  return L->cell[cz][cx];
}
inline int keyat(int i, int j) {
  i = i < 0 ? 0 : (i >= B.ew ? B.ew - 1 : i);
  j = j < 0 ? 0 : (j >= B.eh ? B.eh - 1 : j);
  return B.key[j][i];
}
inline Vec3 wpos(float ex, float y, float ez) { return v3(ex + B.ox, y, ez + B.oz); }
// Centro della casella della mappa (cx, cz) in coordinate del mondo.
inline Vec3 ccenter(int cx, int cz) { return cell_center(B.L, cx, cz); }

// Distanza (in caselle, Chebyshev) dalla mappa di una casella della griglia estesa; 0 dentro.
int dist_out(int i, int j) {
  const int cx = i - B.MW, cz = j - B.MN;
  int dx = cx < 0 ? -cx : (cx >= B.L->W ? cx - B.L->W + 1 : 0);
  int dz = cz < 0 ? -cz : (cz >= B.L->H ? cz - B.L->H + 1 : 0);
  return dx > dz ? dx : dz;
}

// --- chiavi del terreno -----------------------------------------------------------------------

void setup_keys(void) {
  const Style& S = *B.S;
  const uint32_t base[TK_NKINDS] = { S.floor, S.path, S.bed, S.dirt, S.water, S.wall_top, S.plat_top, S.out, S.out2, S.pave, S.wall_top };
  for (int k = 0; k < NKEYS; ++k) {
    const int kind = kkind(k), tone = k & 3;
    float y = 0.0f, rr = 0.5f;
    int prio = 1;
    uint32_t c = base[kind];
    switch (kind) {
      case TK_FLOOR: prio = 2; c = lighten(c, (float)(tone - 1) * S.floor_var); break;
      case TK_OUT: prio = 1; c = lighten(c, (float)(tone - 1) * S.out_var); break;
      case TK_PIT: y = PIT_WATER_Y; prio = 7; rr = 0.18f; c = tone ? 0x1E4234 : 0x24493A; break;   // acqua del cenote, giu' in fondo
      case TK_PAVE: prio = 3; c = lighten(c, (float)(tone - 1) * 0.04f); break;
      case TK_PATH: prio = 5; c = lighten(c, tone ? -0.035f : 0.0f); break;
      case TK_BED: prio = 4; break;
      case TK_DIRT: prio = 4; c = lighten(c, tone ? -0.06f : 0.0f); break;
      case TK_WATER: y = WATER_Y; prio = 7; c = tone ? S.water_deep : S.water; break;
      case TK_WALL: y = WALL_H; prio = 9; rr = S.wall_round; c = lighten(c, tone == 1 ? -0.09f : (tone == 2 ? 0.07f : 0.0f)); break;
      case TK_PLAT: y = PLAT_H; prio = 8; rr = S.plat_round; c = lighten(c, tone ? -0.04f : 0.0f); break;
      case TK_BLOCK:   // zoccolo delle piramidi, delle statue e del primo gradone dei templi maya
        y = B.style == STYLE_PIRAMIDI ? 0.55f : (B.style == STYLE_COLOSSEO ? 0.9f : (B.style == STYLE_CHICHENITZA ? 0.7f : WALL_H));
        prio = 9; rr = 0.06f;
        c = B.style == STYLE_PIRAMIDI ? S.wall_trim : S.wall_top; break;
    }
    B.ky[k] = y;
    B.krgb[k] = c;
    B.kprio[k] = (uint8_t)(prio * 4 + tone);
    B.kround[k] = rr;
  }
}

int tone3(float n) { return n < 0.40f ? 0 : (n < 0.62f ? 1 : 2); }

// Blocchi di muro (rettangoli pieni di almeno 2x2 caselle), dal piu' grande.
void find_blocks(void) {
  const Level* L = B.L;
  B.nblocks = 0;
  memset(B.blk_at, -1, sizeof(B.blk_at));
  for (int pass = 0; pass < 2; ++pass)
    for (int cz = 0; cz < L->H; ++cz)
      for (int cx = 0; cx < L->W; ++cx) {
        if (L->cell[cz][cx] != C_WALL || B.blk_at[cz][cx] >= 0) continue;
        int w = 0;
        while (cx + w < L->W && L->cell[cz][cx + w] == C_WALL && B.blk_at[cz][cx + w] < 0 && w < 8) w++;
        int h = 1;
        for (bool ok = true; ok && cz + h < L->H && h < 8;) {
          for (int a = 0; a < w; ++a)
            if (L->cell[cz + h][cx + a] != C_WALL || B.blk_at[cz + h][cx + a] >= 0) { ok = false; break; }
          if (ok) h++;
        }
        // al primo giro solo i blocchi grandi (3x3 e piu'), al secondo anche i 2x2
        const int need = pass == 0 ? 3 : 2;
        if (w < need || h < need || B.nblocks >= 64) continue;
        // il blocco deve essere staccato dagli altri muri sui lati (non un pezzo di muro spesso)
        int touching = 0;
        for (int a = -1; a <= w; ++a) {
          if (cell_or_void(L, cx + a, cz - 1) == C_WALL) touching++;
          if (cell_or_void(L, cx + a, cz + h) == C_WALL) touching++;
        }
        for (int b2 = 0; b2 < h; ++b2) {
          if (cell_or_void(L, cx - 1, cz + b2) == C_WALL) touching++;
          if (cell_or_void(L, cx + w, cz + b2) == C_WALL) touching++;
        }
        if (touching > 2) continue;
        for (int b2 = 0; b2 < h; ++b2)
          for (int a = 0; a < w; ++a) B.blk_at[cz + b2][cx + a] = (int8_t)B.nblocks;
        B.blk[B.nblocks][0] = (int16_t)cx; B.blk[B.nblocks][1] = (int16_t)cz;
        B.blk[B.nblocks][2] = (int16_t)w; B.blk[B.nblocks][3] = (int16_t)h;
        B.nblocks++;
      }
}

// Chiave di ogni casella della griglia estesa.
void compute_keys(void) {
  find_blocks();
  for (int j = 0; j < B.eh; ++j)
    for (int i = 0; i < B.ew; ++i) {
      const int c = cell_ext(i, j);
      const float wx = (float)i + B.ox, wz = (float)j + B.oz;
      int kind = TK_FLOOR, tone = tone3(fbm(wx, wz, 11, 7.0f));
      switch (c) {
        case C_VOID: {
          // Fuori: qualche macchia vicino ai muri, poi un tono solo (si uniscono in rettangoli
          // grandi e costano poco). Il Colosseo ha il lastricato attorno all'arena.
          kind = TK_OUT;
          const int d = dist_out(i, j);
          tone = d >= 5 || (B.style == STYLE_CHICHENITZA && d >= 2) ? 1 : tone3(fbm(wx, wz, 23, 10.0f));
          if (B.style == STYLE_COLOSSEO) { kind = TK_PAVE; tone = 1; }
          break;
        }
        case C_PIT: kind = TK_PIT; tone = 0; break;
        case C_FLOOR: case C_BUSH: {
          // orlo di calcare chiaro attorno al cenote
          bool rim = false;
          for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) rim |= cell_ext(i + dx, j + dz) == C_PIT;
          if (rim) { kind = TK_PAVE; tone = fbm(wx, wz, 43, 2.0f) < 0.5f ? 1 : 2; }
          break;
        }
        case C_PATH: case C_GATE: kind = TK_PATH; tone = fbm(wx, wz, 31, 5.0f) < 0.5f ? 0 : 1; break;
        case C_GRASS: kind = TK_BED; tone = 0; break;
        case C_TREE: case C_ROCK: kind = TK_DIRT; tone = 0; break;
        case C_WATER: {
          kind = TK_WATER;
          bool inner = true;
          for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
              if (cell_ext(i + dx, j + dz) != C_WATER && dist_out(i + dx, j + dz) == 0) inner = false;
          tone = inner ? 1 : 0;
          break;
        }
        case C_WALL: {
          kind = TK_WALL;
          tone = 0;
          const int b2 = B.blk_at[j - B.MN][i - B.MW];
          if (b2 >= 0 && B.style != STYLE_MURAGLIA) kind = TK_BLOCK;
          break;
        }
        case C_PLATFORM:   // cammino di ronda a chiazze, i podi di pietra di un colore solo
          kind = TK_PLAT;
          tone = B.style == STYLE_MURAGLIA && fbm(wx, wz, 41, 3.0f) > 0.56f ? 1 : 0;
          break;
        default: break;
      }
      if (B.style == STYLE_COLOSSEO && c != C_VOID && kind == TK_FLOOR) tone = tone3(fbm(wx, wz, 11, 10.0f));
      B.key[j][i] = (uint8_t)(kind * 4 + tone);
    }
}

// --- profili lungo i bordi -------------------------------------------------------------------

struct Profile {
  int n;
  float o[16], y[16];      // scostamento verso l'esterno (verso la parte bassa) e quota
  uint32_t c[15];
  uint8_t ul[15];          // tratto senza luce
  int nh;                  // contorno: punti (0 = niente)
  float ho[4], hy[4];
  uint32_t hc;
};
void pf_pt(Profile* p, float o, float y) { p->o[p->n] = o; p->y[p->n] = y; p->n++; }
void pf_seg(Profile* p, float o, float y, uint32_t c, bool ul = false) {   // tratto fino al punto (o, y)
  p->c[p->n - 1] = c;
  p->ul[p->n - 1] = ul ? 1 : 0;
  pf_pt(p, o, y);
}
void pf_hull(Profile* p, float t, uint32_t c, float omax, float ybot, float ytop) {
  p->nh = 3;
  p->ho[0] = omax + t; p->hy[0] = ybot;
  p->ho[1] = omax + t; p->hy[1] = ytop - 0.06f;
  p->ho[2] = t * 0.6f; p->hy[2] = ytop + t;
  p->hc = c;
}

// Profilo del bordo tra la parte alta (chiave hi) e quella bassa (lo). false: niente da disegnare.
bool make_profile(Profile* p, int hi, int lo) {
  const Style& S = *B.S;
  memset(p, 0, sizeof(*p));
  const int kh = kkind(hi), kl = kkind(lo);
  const float yh = B.ky[hi], yl = B.ky[lo];
  if (kh == TK_PATH && fabsf(yh - yl) < 0.001f) {
    // fascia scura lungo il bordo del sentiero, appena sopra il terreno
    pf_pt(p, 0.0f, 0.016f);
    pf_seg(p, -0.12f, 0.016f, lighten(B.krgb[hi], -0.13f));
    return true;
  }
  if (yh <= yl + 0.001f) return false;
  const float ybot = yl - 0.12f;
  if (kh == TK_WALL) {
    switch (B.style) {
      case STYLE_PIRAMIDI:
        pf_pt(p, 0.04f, ybot);
        pf_seg(p, 0.04f, 0.22f, S.wall);
        pf_seg(p, 0.05f, 0.24f, S.wall_dark);
        pf_seg(p, 0.05f, 1.06f, S.wall);
        pf_seg(p, 0.06f, 1.08f, S.wall_dark);
        pf_seg(p, 0.06f, 1.24f, S.wall_band);
        pf_seg(p, 0.05f, 1.26f, S.wall_dark);
        pf_seg(p, 0.05f, 1.34f, S.wall);
        pf_seg(p, 0.12f, 1.47f, S.wall_trim);
        pf_seg(p, 0.12f, 1.53f, S.wall_trim);
        pf_seg(p, 0.0f, yh, S.wall_top);
        pf_hull(p, 0.045f, S.outline, 0.12f, ybot, yh);
        return true;
      case STYLE_CHICHENITZA:
        // muro maya: zoccolo a scarpa, corpo liscio, fregio dipinto tra due cornici (la greca a
        // gradini la disegna wall_dressing), cornice alta sporgente
        pf_pt(p, 0.13f, ybot);
        pf_seg(p, 0.13f, 0.14f, S.wall_dark);
        pf_seg(p, 0.06f, 0.52f, S.wall);
        pf_seg(p, 0.06f, 0.56f, S.wall_line, true);
        pf_seg(p, 0.05f, 0.92f, lighten(S.wall, 0.04f));
        pf_seg(p, 0.08f, 0.96f, S.wall_trim);
        pf_seg(p, 0.08f, 1.00f, S.wall_trim);
        pf_seg(p, 0.07f, 1.30f, S.wall_band);
        pf_seg(p, 0.10f, 1.34f, S.wall_trim);
        pf_seg(p, 0.10f, 1.40f, S.wall_trim);
        pf_seg(p, 0.06f, 1.44f, S.wall_line, true);
        pf_seg(p, 0.06f, 1.54f, S.wall);
        pf_seg(p, 0.0f, yh, S.wall_top);
        pf_hull(p, 0.045f, S.outline, 0.13f, ybot, yh);
        return true;
      case STYLE_MURAGLIA:
        pf_pt(p, 0.06f, ybot);
        pf_seg(p, 0.06f, 0.36f, S.wall_dark);
        pf_seg(p, 0.05f, 0.38f, S.wall_line, true);
        pf_seg(p, 0.05f, 0.76f, S.wall);
        pf_seg(p, 0.05f, 0.78f, S.wall_line, true);
        pf_seg(p, 0.05f, 1.16f, S.wall);
        pf_seg(p, 0.07f, 1.20f, S.wall_line);
        pf_seg(p, 0.07f, 1.30f, S.wall_trim);
        pf_seg(p, 0.0f, 1.33f, S.wall_trim);
        pf_seg(p, 0.0f, yh, S.wall);
        pf_hull(p, 0.045f, S.outline, 0.07f, ybot, yh);
        return true;
      default:   // Colosseo: podio dell'arena, rivestito di marmo, con la fascia rossa
        pf_pt(p, 0.05f, ybot);
        pf_seg(p, 0.05f, 0.16f, S.wall_dark);
        pf_seg(p, 0.03f, 0.20f, S.wall_trim);
        pf_seg(p, 0.03f, 1.10f, S.wall);
        pf_seg(p, 0.035f, 1.12f, S.wall_line);
        pf_seg(p, 0.035f, 1.30f, S.wall_band);
        pf_seg(p, 0.03f, 1.32f, S.wall_line);
        pf_seg(p, 0.03f, 1.42f, S.wall);
        pf_seg(p, 0.11f, 1.50f, S.wall_trim);
        pf_seg(p, 0.11f, 1.55f, S.wall_trim);
        pf_seg(p, 0.0f, yh, S.wall_top);
        pf_hull(p, 0.045f, S.outline, 0.11f, ybot, yh);
        return true;
    }
  }
  if (kh == TK_PLAT) {
    switch (B.style) {
      case STYLE_MURAGLIA:   // cammino di ronda: mattoni e un cordolo basso sui bordi
        pf_pt(p, 0.05f, ybot);
        pf_seg(p, 0.05f, 0.24f, S.wall_dark);
        pf_seg(p, 0.05f, 0.26f, S.wall_line, true);
        pf_seg(p, 0.05f, 0.52f, S.plat_side);
        pf_seg(p, 0.05f, 0.54f, S.wall_line, true);
        pf_seg(p, 0.05f, yh + 0.13f, S.plat_side);
        pf_seg(p, -0.11f, yh + 0.13f, S.wall_trim);
        pf_seg(p, -0.11f, yh, lighten(S.plat_side, -0.08f));
        pf_hull(p, 0.04f, S.outline, 0.05f, ybot, yh + 0.13f);
        return true;
      case STYLE_CHICHENITZA:   // piattaforma a gradoni: un gradino basso, poi la parete fino al piano
        pf_pt(p, 0.16f, ybot);
        pf_seg(p, 0.16f, 0.34f, S.plat_side);
        pf_seg(p, 0.17f, 0.38f, S.plat_trim);
        pf_seg(p, 0.04f, 0.38f, S.plat_top);
        pf_seg(p, 0.04f, 0.42f, S.wall_line, true);
        pf_seg(p, 0.035f, 0.74f, lighten(S.plat_side, 0.05f));
        pf_seg(p, 0.07f, 0.78f, S.plat_trim);
        pf_seg(p, 0.0f, yh, S.plat_top);
        pf_hull(p, 0.04f, S.outline, 0.17f, ybot, yh);
        return true;
      default:   // podio di pietra: zoccolo, fasce e un bordino rialzato attorno al piano
        pf_pt(p, 0.07f, ybot);
        pf_seg(p, 0.07f, 0.12f, S.wall_dark);
        pf_seg(p, 0.035f, 0.16f, S.plat_trim);
        pf_seg(p, 0.035f, 0.40f, S.plat_side);
        pf_seg(p, 0.035f, 0.42f, S.wall_line, true);
        pf_seg(p, 0.035f, 0.66f, lighten(S.plat_side, 0.04f));
        pf_seg(p, 0.08f, 0.70f, S.plat_trim);
        pf_seg(p, 0.08f, yh + 0.045f, S.plat_trim);
        pf_seg(p, -0.07f, yh + 0.045f, S.plat_trim);
        pf_seg(p, -0.07f, yh, lighten(S.plat_side, -0.06f));
        pf_hull(p, 0.04f, S.outline, 0.08f, ybot, yh + 0.045f);
        return true;
    }
  }
  if (kh == TK_BLOCK) {   // zoccolo di piramidi, obelischi e statue
    pf_pt(p, 0.05f, ybot);
    pf_seg(p, 0.05f, yh - 0.16f, S.wall);
    pf_seg(p, 0.09f, yh - 0.12f, S.wall_trim);
    pf_seg(p, 0.09f, yh - 0.05f, S.wall_trim);
    pf_seg(p, 0.0f, yh, B.krgb[hi]);
    pf_hull(p, 0.04f, S.outline, 0.09f, ybot, yh);
    return true;
  }
  if (kl == TK_PIT) {
    // Cenote: dall'acqua scura in fondo la parete di roccia sale a strati, sempre piu' chiara
    // (giu' in fondo senza luce: e' in ombra), fino all'orlo di calcare. L'orlo e' il bordo della
    // casella: niente sporgenze sopra il pozzo (chi ci sta sopra col centro cade).
    pf_pt(p, 0.0f, PIT_WATER_Y - 0.12f);
    pf_seg(p, 0.0f, -1.85f, 0x2C3A2C, true);
    pf_seg(p, 0.0f, -1.25f, 0x4A5440, true);
    pf_seg(p, 0.0f, -1.18f, 0x3A4232, true);
    pf_seg(p, 0.0f, -0.62f, 0x8C8670);
    pf_seg(p, 0.0f, -0.56f, 0x6E6A56, true);
    pf_seg(p, 0.0f, -0.10f, 0xB8B096);
    pf_seg(p, 0.0f, yh, 0xE2DAC2);
    pf_hull(p, 0.03f, S.outline, 0.0f, -0.25f, yh);
    return true;
  }
  if (kl == TK_WATER) {   // riva: schiuma appena sopra l'acqua e la sponda
    pf_pt(p, 0.13f, WATER_Y + 0.014f);
    pf_seg(p, 0.0f, WATER_Y + 0.014f, S.foam, true);
    pf_seg(p, 0.0f, yh, S.bank);
    return true;
  }
  // gradino di terreno (paesaggio)
  pf_pt(p, 0.0f, ybot);
  pf_seg(p, 0.0f, yh, S.bank);
  return true;
}

// Fa scorrere il profilo lungo una spezzata pt[0..n-1] con le normali nr[] (verso la parte bassa).
void sweep(MeshBuilder* mb, const Profile& p, const Vec3* pt, const Vec3* nr, int n) {
  for (int k = 0; k + 1 < p.n; ++k) {
    const float do_ = p.o[k + 1] - p.o[k], dy = p.y[k + 1] - p.y[k];
    unlit(mb, p.ul[k] != 0);
    for (int i = 0; i + 1 < n; ++i) {
      const Vec3 a0 = pt[i] + nr[i] * p.o[k] + v3(0, p.y[k], 0), a1 = pt[i + 1] + nr[i + 1] * p.o[k] + v3(0, p.y[k], 0);
      const Vec3 b0 = pt[i] + nr[i] * p.o[k + 1] + v3(0, p.y[k + 1], 0), b1 = pt[i + 1] + nr[i + 1] * p.o[k + 1] + v3(0, p.y[k + 1], 0);
      const Vec3 nm = normalize(nr[i] + nr[i + 1]);
      const Vec3 facing = nm * dy + v3(0, -do_, 0);
      quad_f(mb, a0, a1, b1, b0, facing, p.c[k]);
    }
  }
  unlit(mb, false);
  if (p.nh >= 2) {
    // Contorno: lo stesso giro un po' piu' fuori, rovesciato e scuro (si vede solo sul bordo).
    unlit(mb, true);
    for (int k = 0; k + 1 < p.nh; ++k) {
      const float do_ = p.ho[k + 1] - p.ho[k], dy = p.hy[k + 1] - p.hy[k];
      for (int i = 0; i + 1 < n; ++i) {
        const Vec3 a0 = pt[i] + nr[i] * p.ho[k] + v3(0, p.hy[k], 0), a1 = pt[i + 1] + nr[i + 1] * p.ho[k] + v3(0, p.hy[k], 0);
        const Vec3 b0 = pt[i] + nr[i] * p.ho[k + 1] + v3(0, p.hy[k + 1], 0), b1 = pt[i + 1] + nr[i + 1] * p.ho[k + 1] + v3(0, p.hy[k + 1], 0);
        const Vec3 nm = normalize(nr[i] + nr[i + 1]);
        const Vec3 facing = (nm * dy + v3(0, -do_, 0)) * -1.0f;
        quad_f(mb, a0, a1, b1, b0, facing, p.hc);
      }
    }
    unlit(mb, false);
  }
}

// --- terreno sulla griglia duale -------------------------------------------------------------

// Pezzo dritto di bordo (meta' lato di casella) da unire ai vicini allineati.
struct Straight {
  uint8_t axis;     // 0: lungo x (lato orizzontale), 1: lungo z
  int8_t sgn;       // la parte bassa sta dalla parte + (sud o est) o -
  uint8_t hi, lo;
  float line, a, b; // coordinata fissa e intervallo lungo l'asse (griglia estesa)
};
Straight g_st[2048];
int g_nst;

// Punti dell'arco che arrotonda l'angolo del quarto (sx, sz) attorno allo spigolo (vx, vz):
// da P1 (sul lato verticale) a P2 (sul lato orizzontale), centro in (vx + r sx, vz + r sz).
void arc_pts(Vec3* out, float vx, float vz, float y, float r, float sx, float sz, int nseg) {
  for (int i = 0; i <= nseg; ++i) {
    const float t = D_HALF_PI * (float)i / (float)nseg;
    out[i] = wpos(vx + r * sx - r * sx * d_cos(t), y, vz + r * sz - r * sz * d_sin(t));
  }
}

// Bordo del sentiero lastricato verso il terreno alla stessa quota: una fascia scura sottile.
bool path_band(int a, int b) {
  const int ka = kkind(a), kb = kkind(b);
  if (B.style == STYLE_COLOSSEO || ka != TK_PATH || kb == TK_PATH) return false;
  return kb == TK_FLOOR || kb == TK_BED || kb == TK_DIRT || kb == TK_OUT;
}
// Parte "alta" del bordo tra a e b (quella da cui parte il profilo): 1 se a, -1 se b, 0 niente.
int edge_side(int a, int b) {
  if (B.ky[a] > B.ky[b] + 0.001f) return 1;
  if (B.ky[b] > B.ky[a] + 0.001f) return -1;
  if (path_band(a, b)) return 1;
  if (path_band(b, a)) return -1;
  return 0;
}

// Bordo ad arco tra la chiave dentro l'arco e quella fuori: il profilo subito.
void edge_arc(MeshBuilder* mb, int inside, int outside, float vx, float vz, float r, float sx, float sz, int nseg) {
  int hi, lo;
  bool convex;
  const int side = edge_side(inside, outside);
  if (side > 0) { hi = inside; lo = outside; convex = true; }
  else if (side < 0) { hi = outside; lo = inside; convex = false; }
  else return;
  Profile pf;
  if (!make_profile(&pf, hi, lo)) return;
  Vec3 pt[8], nr[8];
  arc_pts(pt, vx, vz, 0.0f, r, sx, sz, nseg);
  for (int i = 0; i <= nseg; ++i) {
    const float t = D_HALF_PI * (float)i / (float)nseg;
    const Vec3 u = v3(-sx * d_cos(t), 0.0f, -sz * d_sin(t));
    nr[i] = convex ? u : u * -1.0f;
  }
  sweep(mb, pf, pt, nr, nseg + 1);
}

// Bordo dritto: va nella lista e si unisce ai pezzi allineati con lo stesso profilo.
void edge_straight(int ka, int kb, uint8_t axis, float line, float a, float b, int sgn_b) {
  // ka dalla parte "-", kb dalla parte "+" dell'asse perpendicolare
  if (b - a < 1e-4f) return;
  int hi, lo, sgn;
  const int side = edge_side(ka, kb);
  if (side > 0) { hi = ka; lo = kb; sgn = sgn_b; }
  else if (side < 0) { hi = kb; lo = ka; sgn = -sgn_b; }
  else return;
  if (g_nst >= (int)(sizeof(g_st) / sizeof(g_st[0]))) return;
  Straight& s = g_st[g_nst++];
  s.axis = axis; s.sgn = (int8_t)sgn; s.hi = (uint8_t)hi; s.lo = (uint8_t)lo; s.line = line; s.a = a; s.b = b;
}

// Il profilo dipende solo dal tipo e dalla quota della parte bassa, non dal suo tono.
inline int lo_class(int k) { return kkind(k); }

int cmp_straight(const void* pa, const void* pb) {
  const Straight& a = *(const Straight*)pa;
  const Straight& b = *(const Straight*)pb;
  if (a.axis != b.axis) return a.axis - b.axis;
  if (a.line != b.line) return a.line < b.line ? -1 : 1;
  if (a.sgn != b.sgn) return a.sgn - b.sgn;
  if (a.hi != b.hi) return a.hi - b.hi;
  if (lo_class(a.lo) != lo_class(b.lo)) return lo_class(a.lo) - lo_class(b.lo);
  return a.a < b.a ? -1 : (a.a > b.a ? 1 : 0);
}

void wall_dressing(MeshBuilder* mb, Vec3 p0, Vec3 p1, Vec3 n, int hi, int lo);
void pit_dressing(MeshBuilder* mb, Vec3 p0, Vec3 p1, Vec3 n);
void pyramid(MeshBuilder* mb, Vec3 c, float hx, float hz, float hgt, uint32_t col, uint32_t cap, float capk, uint32_t line, float t);

void flush_straights(MeshBuilder* mb) {
  qsort(g_st, (size_t)g_nst, sizeof(Straight), cmp_straight);
  for (int i = 0; i < g_nst;) {
    int j = i;
    float b = g_st[i].b;
    while (j + 1 < g_nst && g_st[j + 1].axis == g_st[i].axis && g_st[j + 1].line == g_st[i].line && g_st[j + 1].sgn == g_st[i].sgn &&
           g_st[j + 1].hi == g_st[i].hi && lo_class(g_st[j + 1].lo) == lo_class(g_st[i].lo) && fabsf(g_st[j + 1].a - b) < 1e-4f) {
      j++;
      b = g_st[j].b;
    }
    const Straight& s = g_st[i];
    Profile pf;
    if (make_profile(&pf, s.hi, s.lo)) {
      Vec3 pt[2], nr[2];
      if (s.axis == 0) {
        pt[0] = wpos(s.a, 0, s.line); pt[1] = wpos(b, 0, s.line);
        nr[0] = nr[1] = v3(0, 0, (float)s.sgn);
      } else {
        pt[0] = wpos(s.line, 0, s.a); pt[1] = wpos(s.line, 0, b);
        nr[0] = nr[1] = v3((float)s.sgn, 0, 0);
      }
      sweep(mb, pf, pt, nr, 2);
      wall_dressing(mb, pt[0], pt[1], nr[0], s.hi, s.lo);
      if (kkind(s.lo) == TK_PIT) pit_dressing(mb, pt[0], pt[1], nr[0]);
    }
    i = j + 1;
  }
  g_nst = 0;
}

// Griglia fine (mezzo metro) del blocco in costruzione: i quarti senza arrotondamento si
// segnano qui e alla fine si uniscono in rettangoli (anche lungo i muri e i sentieri).
const int FINE = 2 * (LV_CHUNK + 1);
int16_t g_fine[FINE][FINE];
int g_fvi0, g_fvj0;

// Quadrato duale attorno allo spigolo (vi, vj) della griglia estesa: q[0] NO, q[1] NE, q[2] SO, q[3] SE.
// Un quarto diverso da tutti e due i vicini di lato e' un angolo: si arrotonda, e il raccordo
// prende la chiave dei vicini. Nelle scacchiere resta unita la coppia con la priorita' piu' alta.
void dual_square(MeshBuilder* mb, int vi, int vj, const int q[4]) {
  bool rnd[4] = { false, false, false, false };
  int fil[4] = { 0, 0, 0, 0 };
  float rad[4] = { 0, 0, 0, 0 };
  int nseg[4] = { 3, 3, 3, 3 };
  for (int s = 0; s < 4; ++s) {
    const int a = s ^ 1, b = s ^ 2, d = s ^ 3;
    if (q[a] == q[b] && q[b] == q[d] && q[s] != q[a]) { rnd[s] = true; fil[s] = q[a]; }
  }
  const bool e03 = q[0] == q[3], e12 = q[1] == q[2];
  if (!rnd[0] && !rnd[1] && !rnd[2] && !rnd[3]) {
    if (e03 && e12 && q[0] != q[1]) {
      if (B.kprio[q[0]] > B.kprio[q[1]]) { rnd[1] = rnd[2] = true; fil[1] = fil[2] = q[0]; }
      else { rnd[0] = rnd[3] = true; fil[0] = fil[3] = q[1]; }
    } else if (e03 && !e12 && q[1] != q[0] && q[2] != q[0]) {
      rnd[1] = rnd[2] = true; fil[1] = fil[2] = q[0];
    } else if (e12 && !e03 && q[0] != q[1] && q[3] != q[1]) {
      rnd[0] = rnd[3] = true; fil[0] = fil[3] = q[1];
    }
  }
  for (int s = 0; s < 4; ++s)
    if (rnd[s]) {
      const float r0 = B.kround[q[s]], r1 = B.kround[fil[s]];
      rad[s] = r0 < r1 ? r0 : r1;
      nseg[s] = rad[s] < 0.2f ? 1 : (kkind(q[s]) == kkind(fil[s]) ? 2 : 3);   // angoli piccoli: smusso; tra due toni basta meno
    }
  const float vx = (float)vi, vz = (float)vj;
  for (int s = 0; s < 4; ++s) {
    const int cx = vi - 1 + (s & 1), cz = vj - 1 + (s >> 1);
    if (cx < 0 || cz < 0 || cx >= B.ew || cz >= B.eh) continue;   // fuori dalla griglia estesa
    if (!rnd[s]) {
      g_fine[2 * (vj - g_fvj0) + (s >> 1)][2 * (vi - g_fvi0) + (s & 1)] = (int16_t)q[s];
      continue;
    }
    const float sx = (s & 1) ? 1.0f : -1.0f, sz = (s & 2) ? 1.0f : -1.0f, r = rad[s];
    const float y = B.ky[q[s]];
    Vec3 pts[16];
    int n = 0;
    pts[n++] = wpos(vx + 0.5f * sx, y, vz + 0.5f * sz);   // centro della casella
    pts[n++] = wpos(vx, y, vz + 0.5f * sz);
    arc_pts(pts + n, vx, vz, y, r, sx, sz, nseg[s]);
    n += nseg[s] + 1;
    pts[n++] = wpos(vx + 0.5f * sx, y, vz);
    fan_f(mb, pts, n, v3(0, 1, 0), B.krgb[q[s]]);
    // il raccordo dell'angolo, del colore di chi sta attorno
    const float yf = B.ky[fil[s]];
    Vec3 fp[8];
    fp[0] = wpos(vx, yf, vz);
    arc_pts(fp + 1, vx, vz, yf, r, sx, sz, nseg[s]);
    fan_f(mb, fp, nseg[s] + 2, v3(0, 1, 0), B.krgb[fil[s]]);
    edge_arc(mb, q[s], fil[s], vx, vz, r, sx, sz, nseg[s]);
  }
  // Lati tra quarti diversi (la parte non assorbita da un arrotondamento).
  // verticali: N tra 0 e 1, S tra 2 e 3; orizzontali: O tra 0 e 2, E tra 1 e 3
  static const int SP[4][3] = { { 0, 1, -1 }, { 2, 3, 1 }, { 0, 2, -1 }, { 1, 3, 1 } };
  for (int k = 0; k < 4; ++k) {
    const int a = SP[k][0], b = SP[k][1], dir = SP[k][2];
    if (q[a] == q[b]) continue;
    float r0 = rnd[a] ? rad[a] : (rnd[b] ? rad[b] : 0.0f);
    if (r0 >= 0.499f) continue;
    if (k < 2) {
      // lato verticale x = vx, da vz verso vz + 0.5 * dir; a a ovest, b a est
      if (vi <= 0 || vi >= B.ew) continue;
      const float z0 = dir < 0 ? vz - 0.5f : vz + r0, z1 = dir < 0 ? vz - r0 : vz + 0.5f;
      edge_straight(q[a], q[b], 1, vx, fmaxf(z0, 0.0f), fminf(z1, (float)B.eh), 1);
    } else {
      if (vj <= 0 || vj >= B.eh) continue;
      const float x0 = dir < 0 ? vx - 0.5f : vx + r0, x1 = dir < 0 ? vx - r0 : vx + 0.5f;
      edge_straight(q[a], q[b], 0, vz, fmaxf(x0, 0.0f), fminf(x1, (float)B.ew), 1);
    }
  }
}

// Terreno di un blocco: spigoli [vi0, vi1) x [vj0, vj1).
void build_ground(MeshBuilder* mb, int vi0, int vj0, int vi1, int vj1) {
  const int nw = 2 * (vi1 - vi0), nh = 2 * (vj1 - vj0);
  g_fvi0 = vi0;
  g_fvj0 = vj0;
  for (int j = 0; j < nh; ++j)
    for (int i = 0; i < nw; ++i) g_fine[j][i] = -1;
  for (int vj = vj0; vj < vj1; ++vj)
    for (int vi = vi0; vi < vi1; ++vi) {
      const int q[4] = { keyat(vi - 1, vj - 1), keyat(vi, vj - 1), keyat(vi - 1, vj), keyat(vi, vj) };
      if (q[0] == q[1] && q[0] == q[2] && q[0] == q[3]) {
        for (int s = 0; s < 4; ++s) {
          const int cx = vi - 1 + (s & 1), cz = vj - 1 + (s >> 1);
          if (cx < 0 || cz < 0 || cx >= B.ew || cz >= B.eh) continue;
          g_fine[2 * (vj - vj0) + (s >> 1)][2 * (vi - vi0) + (s & 1)] = (int16_t)q[0];
        }
        continue;
      }
      dual_square(mb, vi, vj, q);
    }
  // Rettangoli il piu' grandi possibile sulla griglia fine.
  for (int j = 0; j < nh; ++j)
    for (int i = 0; i < nw; ++i) {
      const int k = g_fine[j][i];
      if (k < 0) continue;
      int w = 1, h = 1;
      while (i + w < nw && g_fine[j][i + w] == k) w++;
      for (bool ok = true; ok && j + h < nh;) {
        for (int a = 0; a < w; ++a)
          if (g_fine[j + h][i + a] != k) { ok = false; break; }
        if (ok) h++;
      }
      for (int b = 0; b < h; ++b)
        for (int a = 0; a < w; ++a) g_fine[j + b][i + a] = -1;
      const float x0 = (float)vi0 - 0.5f + 0.5f * (float)i, x1 = x0 + 0.5f * (float)w;
      const float z0 = (float)vj0 - 0.5f + 0.5f * (float)j, z1 = z0 + 0.5f * (float)h;
      quad_up(mb, x0 + B.ox, z0 + B.oz, x1 + B.ox, z1 + B.oz, B.ky[k], B.krgb[k]);
    }
  flush_straights(mb);
}

// --- decorazioni dei muri ---------------------------------------------------------------------

// Chiave del terreno nel punto (x, z) del mondo.
int key_world(float x, float z) { return keyat((int)floorf(x - B.ox), (int)floorf(z - B.oz)); }
bool outside_kind(int k) { const int kd = kkind(k); return kd == TK_OUT || kd == TK_PAVE; }

void wall_dressing(MeshBuilder* mb, Vec3 p0, Vec3 p1, Vec3 n, int hi, int lo) {
  if (kkind(hi) != TK_WALL) return;
  const Style& S = *B.S;
  const Vec3 d = p1 - p0;
  const float len = length(d);
  if (len < 0.2f) return;
  const Vec3 u = d * (1.0f / len);
  const Vec3 mid = (p0 + p1) * 0.5f;
  // muro di bordo: dall'altra parte c'e' il paesaggio (questo e' il lato verso la mappa)
  const bool inner = outside_kind(key_world(mid.x - n.x * 1.5f, mid.z - n.z * 1.5f)) && !outside_kind(lo);
  if (B.style == STYLE_MURAGLIA) {
    if (inner) {
      // verso la mappa un parapetto basso e continuo
      const Vec3 a = p0 - n * 0.16f, b = p1 - n * 0.02f;
      box5(mb, v3(fminf(a.x, b.x), WALL_H - 0.02f, fminf(a.z, b.z)), v3(fmaxf(a.x, b.x), WALL_H + 0.16f, fmaxf(a.z, b.z)), S.wall_trim);
      // e ogni tanto una torretta sopra il muro di bordo (solo estetica: sul muro non si sale)
      for (float t = 3.5f; t < len - 1.0f; t += 9.0f) {
        const Vec3 c = p0 + u * t - n * 0.5f;
        if (fabsf(fmodf(fabsf(c.x) * 0.37f + fabsf(c.z) * 0.61f, 1.0f)) > 0.8f) continue;
        const Part q = part(mb);
        mb_box(mb, v3(-0.62f, WALL_H - 0.05f, -0.62f), v3(0.62f, WALL_H + 0.9f, 0.62f), lighten(S.wall, 0.04f));
        box5(mb, v3(-0.68f, WALL_H + 0.9f, -0.68f), v3(0.68f, WALL_H + 1.0f, 0.68f), S.wall_trim);
        pyramid(mb, v3(0, WALL_H + 1.0f, 0), 0.85f, 0.85f, 0.6f, S.gate_dark, lighten(S.gate_dark, 0.1f), 0.25f, 0, 0.0f);
        place(mb, q, c, 0.0f, 1.0f);
        outline(mb, q, 0.04f, S.outline);
      }
      return;
    }
    // merli con le feritoie sul lato esterno (e sui due lati dei muri dentro la mappa)
    const int nm = (int)(len / 0.7f + 0.5f);
    if (nm < 1) return;
    const float step = len / (float)nm;
    for (int i = 0; i < nm; ++i) {
      const Vec3 c = p0 + u * (step * ((float)i + 0.5f)) - n * 0.1f;
      const float ex = fabsf(u.x) * 0.19f + fabsf(n.x) * 0.09f, ez = fabsf(u.z) * 0.19f + fabsf(n.z) * 0.09f;
      box5(mb, v3(c.x - ex, WALL_H - 0.02f, c.z - ez), v3(c.x + ex, WALL_H + 0.32f, c.z + ez), (i & 1) ? S.wall : lighten(S.wall, 0.05f));
    }
  } else if (B.style == STYLE_PIRAMIDI && n.z > 0.5f) {
    // Giunti verticali dei blocchi, sfalsati per corso, sul lato che si vede.
    unlit(mb, true);
    for (int row = 0; row < 2; ++row) {
      const float y0 = row ? 0.26f : 0.66f, y1 = row ? 0.64f : 1.04f;
      const float ph = row ? 0.0f : 0.55f;
      for (float t = ph + 0.35f; t < len - 0.1f; t += 1.1f) {
        const Vec3 c = p0 + u * t + n * 0.062f;
        quad_f(mb, c + v3(0, y0, 0), c + u * 0.035f + v3(0, y0, 0), c + u * 0.035f + v3(0, y1, 0), c + v3(0, y1, 0), n, S.wall_dark);
      }
    }
    unlit(mb, false);
  } else if (B.style == STYLE_CHICHENITZA && n.z > 0.5f) {
    // Greca a gradini sul fregio dipinto, sul lato che si vede: per ogni mezzo metro un gradino rosso
    // (in basso, su, in alto) sul fondo blu.
    unlit(mb, true);
    const int nu = (int)(len / 0.52f);
    const float st = nu > 0 ? len / (float)nu : len;
    for (int k = 0; k < nu; ++k) {
      const Vec3 c = p0 + u * (st * (float)k + 0.06f) + n * 0.074f;
      auto bar = [&](float t0, float t1, float y0, float y1) {
        quad_f(mb, c + u * t0 + v3(0, y0, 0), c + u * t1 + v3(0, y0, 0), c + u * t1 + v3(0, y1, 0), c + u * t0 + v3(0, y1, 0), n, S.wall_band2);
      };
      bar(0.0f, 0.30f, 1.04f, 1.10f);
      bar(0.24f, 0.30f, 1.10f, 1.26f);
      bar(0.24f, 0.46f, 1.20f, 1.26f);
    }
    unlit(mb, false);
  }
}

// Liane che pendono lungo la parete del cenote che si vede (verso sud): strisce verdi un po' storte.
void pit_dressing(MeshBuilder* mb, Vec3 p0, Vec3 p1, Vec3 n) {
  if (n.z < 0.5f) return;
  const Vec3 d = p1 - p0;
  const float len = length(d);
  if (len < 0.3f) return;
  const Vec3 u = d * (1.0f / len);
  int k = 0;
  for (float t = 0.2f + 0.3f * hf((int)(p0.x * 7.0f), (int)(p0.z * 7.0f), 1); t < len - 0.1f; t += 0.45f + 0.4f * hf(k, (int)(p0.z * 5.0f), 2), ++k) {
    const float l = 0.9f + 1.0f * hf(k, (int)(p0.x * 5.0f), 3), w = 0.07f + 0.04f * hf(k, 1, 4);
    const Vec3 a = p0 + u * t + n * 0.012f, b = a + u * (0.08f * hs(k, 2, 5)) + v3(0, -0.5f * l, 0);
    const Vec3 c = b + u * (0.06f * hs(k, 3, 6)) + v3(0, -0.5f * l, 0);
    const uint32_t col = (k & 1) ? 0x4E9E3E : 0x3E8A34;
    quad_f(mb, a - u * w + v3(0, 0.02f, 0), a + u * w + v3(0, 0.02f, 0), b + u * w, b - u * w, n, col);
    quad_f(mb, b - u * w, b + u * w, c + u * (w * 0.5f), c - u * (w * 0.5f), n, lighten(col, -0.08f));
    tri_f(mb, b + u * w, b + u * (w + 0.12f) + v3(0, 0.08f, 0), b + u * w + v3(0, -0.1f, 0), n, 0x4FA040);   // una foglia
  }
}

// --- oggetti fissi delle caselle ----------------------------------------------------------------

// Cilindro senza i coperchi (tronchi, pali): 2 * seg triangoli.
void pipe(MeshBuilder* mb, Vec3 a, Vec3 b, float r0, float r1, int seg, uint32_t c) {
  const Vec3 d = normalize(b - a);
  const Vec3 h = fabsf(d.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0);
  const Vec3 e1 = normalize(cross(d, h)), e2 = cross(e1, d);
  const int base = mb->nv;
  for (int j = 0; j < seg; ++j) {
    const float ph = D_TWO_PI * (float)j / (float)seg, cp = d_cos(ph), sp = d_sin(ph);
    mb_vert(mb, a + e1 * (r0 * cp) + e2 * (r0 * sp));
    mb_vert(mb, b + e1 * (r1 * cp) + e2 * (r1 * sp));
  }
  const Vec3 mid = (a + b) * 0.5f;
  for (int j = 0; j < seg; ++j) {
    const int j1 = (j + 1) % seg;
    const int a0 = base + 2 * j, a1 = base + 2 * j1, b0 = a0 + 1, b1 = a1 + 1;
    const Vec3 out = (mb->v[a0] + mb->v[a1]) * 0.5f - mid;
    const Vec3 n = cross(mb->v[a1] - mb->v[a0], mb->v[b0] - mb->v[a0]);
    if (dot(n, out) >= 0.0f) { mb_tri(mb, a0, a1, b1, c); mb_tri(mb, a0, b1, b0, c); }
    else { mb_tri(mb, a0, b1, a1, c); mb_tri(mb, a0, b0, b1, c); }
  }
}

// Foglia di palma da un lato solo (si vede dall'alto): 4 triangoli.
void frond(MeshBuilder* mb, Vec3 a, Vec3 tip, float w, float droop, uint32_t c, uint32_t c2) {
  const Vec3 d = tip - a;
  Vec3 side = normalize(cross(d, v3(0, 1, 0)));
  if (dot(side, side) < 0.5f) side = v3(1, 0, 0);
  const Vec3 mid = a + d * 0.45f + v3(0, droop, 0);
  const Vec3 l = mid + side * w, r = mid - side * w;
  const Vec3 up = v3(0, 1, 0);
  tri_f(mb, a, l, mid, up, c);
  tri_f(mb, a, mid, r, up, c2);
  tri_f(mb, mid, l, tip, up, c);
  tri_f(mb, mid, tip, r, up, c2);
}

// Palma da datteri: tronco a segmenti un po' storto, ciuffo di foglie che ricadono.
void palm(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  const float lean = 0.16f + 0.24f * (float)(h % 100u) / 100.0f, yaw = (float)((h >> 8) % 628u) * 0.01f;
  const Vec3 dir = v3(lean * d_cos(yaw), 1.0f, lean * d_sin(yaw));
  const int segs = detailed ? 3 : 2;
  const float hgt = 2.7f * s;
  Vec3 prev = at;
  const Part pt = part(mb);
  for (int i = 0; i < segs; ++i) {
    const float t1 = (float)(i + 1) / (float)segs, t0 = (float)i / (float)segs;
    const Vec3 nx = at + v3(dir.x * hgt * t1 * t1, hgt * t1, dir.z * hgt * t1 * t1);
    pipe(mb, prev, nx, (0.18f - 0.06f * t0) * s, (0.16f - 0.06f * t1) * s, detailed ? 6 : 4, (i & 1) ? S.trunk : S.trunk_dark);
    prev = nx;
  }
  if (detailed) outline(mb, pt, 0.03f, S.outline);
  const Vec3 top = prev;
  const int nf = detailed ? 8 : 6;
  for (int i = 0; i < nf; ++i) {
    const float a = yaw + D_TWO_PI * (float)i / (float)nf + 0.2f * hs((int)h, i, 3);
    const float l = (1.3f + 0.3f * hf((int)h, i, 4)) * s;
    const Vec3 tip = top + v3(l * d_cos(a), -0.6f * s - 0.3f * s * hf((int)h, i, 5), l * d_sin(a));
    frond(mb, top + v3(0, 0.06f, 0), tip, 0.22f * s, 0.34f * s, (i & 1) ? S.leaf0 : S.leaf1, (i & 1) ? S.leaf1 : lighten(S.leaf0, 0.08f));
  }
  if (detailed) mb_ellipsoid(mb, top + v3(0.06f * s, -0.14f * s, 0.04f * s), v3(0.17f * s, 0.13f * s, 0.17f * s), 5, 2, S.deco0);
}

// Albero a chioma tonda; nel Colosseo il cipresso, alto e stretto.
void round_tree(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  const Part pt = part(mb);
  pipe(mb, at, at + v3(0, 1.35f * s, 0), 0.17f * s, 0.12f * s, detailed ? 6 : 4, S.trunk);
  if (detailed) outline(mb, pt, 0.03f, S.outline);
  const Part pc = part(mb);
  const float yaw = (float)(h % 628u) * 0.01f;
  if (B.style == STYLE_COLOSSEO) {
    mb_ellipsoid(mb, at + v3(0, 2.2f * s, 0), v3(0.5f * s, 1.45f * s, 0.5f * s), detailed ? 7 : 5, detailed ? 5 : 4, S.leaf1);
  } else {
    mb_ellipsoid(mb, at + v3(0, 1.85f * s, 0), v3(0.95f * s, 0.78f * s, 0.95f * s), detailed ? 8 : 6, detailed ? 4 : 3, (h & 1u) ? S.leaf0 : S.leaf1);
    if (detailed)
      mb_ellipsoid(mb, at + v3(0.32f * s * d_cos(yaw), 2.4f * s, 0.32f * s * d_sin(yaw)), v3(0.55f * s, 0.46f * s, 0.55f * s), 6, 3, S.leaf0);
  }
  if (detailed) outline(mb, pc, 0.045f, S.leaf_line);
}

// Pino cinese: rami a strati piatti.
void pine(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  const Part pt = part(mb);
  const float lean = 0.12f * hs((int)h, 1, 9);
  pipe(mb, at, at + v3(lean, 2.5f * s, 0.08f * hs((int)h, 2, 9)), 0.16f * s, 0.09f * s, detailed ? 5 : 4, S.trunk);
  if (detailed) outline(mb, pt, 0.03f, S.outline);
  const Part pc = part(mb);
  const int layers = detailed ? 3 : 2;
  for (int i = 0; i < layers; ++i) {
    const float t = (float)i / (float)(layers - 1);
    const float y = (1.3f + 1.15f * t) * s, r = (1.05f - 0.45f * t) * s;
    const float ox = 0.25f * s * hs((int)h, i, 7), oz = 0.25f * s * hs((int)h, i, 8);
    mb_ellipsoid(mb, at + v3(lean * t + ox, y, oz), v3(r, 0.27f * s, r * 0.85f), detailed ? 7 : 6, 2, (i & 1) ? S.leaf0 : S.leaf1);
  }
  if (detailed) outline(mb, pc, 0.045f, S.leaf_line);
}

// Ceiba (Chichen Itza): tronco dritto e chiaro con le radici a contrafforte, chioma larga e piatta.
void ceiba(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  const Part pt = part(mb);
  const float hgt = 2.35f * s;
  const Vec3 top = at + v3(0.07f * hs((int)h, 1, 9) * s, hgt, 0.07f * hs((int)h, 2, 9) * s);
  pipe(mb, at, top, 0.2f * s, 0.13f * s, detailed ? 6 : 4, S.trunk);
  if (detailed) {
    for (int k = 0; k < 3; ++k) {   // radici a contrafforte: tre lame
      const float a = (float)(h % 628u) * 0.01f + D_TWO_PI * (float)k / 3.0f;
      const Vec3 d = v3(d_cos(a), 0.0f, d_sin(a));
      const Vec3 b0 = at + d * (0.1f * s), b1 = at + d * (0.55f * s), t = at + d * (0.08f * s) + v3(0, 0.75f * s, 0);
      tri_f(mb, b0, b1, t, v3(-d.z, 0, d.x), S.trunk_dark);
      tri_f(mb, b0, t, b1, v3(d.z, 0, -d.x), S.trunk_dark);
    }
    outline(mb, pt, 0.03f, S.outline);
  }
  const Part pc = part(mb);
  mb_ellipsoid(mb, top + v3(0, 0.2f * s, 0), v3(1.3f * s, 0.42f * s, 1.2f * s), detailed ? 9 : 7, detailed ? 3 : 2, (h & 1u) ? S.leaf0 : S.leaf1);
  if (detailed) mb_ellipsoid(mb, top + v3(0.35f * s, 0.5f * s, -0.2f * s), v3(0.72f * s, 0.32f * s, 0.66f * s), 7, 2, S.leaf0);
  if (detailed) outline(mb, pc, 0.045f, S.leaf_line);
}

void tree_at(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  switch (B.style) {
    case STYLE_PIRAMIDI: palm(mb, S, at, s, h, detailed); break;
    case STYLE_MURAGLIA: pine(mb, S, at, s, h, detailed); break;
    case STYLE_CHICHENITZA:
      if ((h >> 5) % 5u < 3u) ceiba(mb, S, at, s, h, detailed);
      else palm(mb, S, at, s * 1.05f, h, detailed);
      break;
    default: round_tree(mb, S, at, s, h, detailed); break;
  }
}

// Masso irregolare (ellissoide schiacciato e un sasso accanto).
void boulder(MeshBuilder* mb, const Style& S, Vec3 at, float s, uint32_t h, bool detailed) {
  const Part p = part(mb);
  const float k = 0.85f + 0.3f * (float)(h % 100u) / 100.0f;
  mb_ellipsoid(mb, v3(0, 0.36f * s * k, 0), v3(0.52f * s, 0.5f * s * k, 0.44f * s), detailed ? 6 : 5, 3, S.rock);
  if (detailed) mb_ellipsoid(mb, v3(0.32f * s, 0.16f * s, 0.24f * s), v3(0.24f * s, 0.22f * s, 0.22f * s), 5, 2, S.rock_dark);
  place(mb, p, at, (float)((h >> 9) % 628u) * 0.01f, 1.0f);
  if (detailed) outline(mb, p, 0.035f, S.rock_line);
}

// Rocchio di colonna in piedi o a terra, con le scanalature accennate (7 lati).
void column_drum(MeshBuilder* mb, const Style& S, Vec3 at, float r, float hgt, bool lying, float yaw, uint32_t c) {
  const Part p = part(mb);
  mb_cylinder(mb, v3(0, 0, 0), r, r, hgt, 7, c);
  if (lying) place_xf(mb, p, xf_mul(xf_translate(at + v3(0, r * 0.92f, 0)), xf_mul(xf_rot_y(yaw), xf_mul(xf_rot_z(D_HALF_PI), xf_translate(v3(0, -hgt * 0.5f, 0))))));
  else place(mb, p, at, yaw, 1.0f);
  outline(mb, p, 0.03f, S.rock_line);
}

// Sfinge su un piedistallo, rivolta verso yaw: corpo di leone sdraiato, testa col copricapo a righe.
void sphinx(MeshBuilder* mb, const Style& S, Vec3 at, float yaw, float sc) {
  const Part p = part(mb);
  const uint32_t st = lighten(S.rock, 0.06f), dk = S.rock_dark;
  mb_box(mb, v3(-0.36f, 0.0f, -0.48f), v3(0.36f, 0.26f, 0.48f), dk);                 // piedistallo
  mb_box(mb, v3(-0.22f, 0.26f, -0.44f), v3(0.22f, 0.56f, 0.18f), st);                // corpo sdraiato
  mb_box(mb, v3(-0.19f, 0.26f, 0.12f), v3(-0.05f, 0.38f, 0.46f), st);                // zampe davanti
  mb_box(mb, v3(0.05f, 0.26f, 0.12f), v3(0.19f, 0.38f, 0.46f), st);
  mb_box(mb, v3(-0.16f, 0.5f, 0.02f), v3(0.16f, 1.02f, 0.3f), st);                   // petto e testa
  mb_box(mb, v3(-0.22f, 0.72f, -0.02f), v3(0.22f, 1.07f, 0.22f), S.wall_band);       // copricapo
  mb_box(mb, v3(-0.23f, 0.5f, 0.06f), v3(-0.15f, 0.84f, 0.2f), S.wall_band);         // bande ai lati
  mb_box(mb, v3(0.15f, 0.5f, 0.06f), v3(0.23f, 0.84f, 0.2f), S.wall_band);
  mb_box(mb, v3(-0.13f, 0.66f, 0.2f), v3(0.13f, 0.98f, 0.34f), st);                  // viso
  outline(mb, p, 0.03f, S.rock_line);
  // righe d'oro sul copricapo e coda arrotolata
  mb_box(mb, v3(-0.225f, 0.98f, -0.025f), v3(0.225f, 1.0f, 0.225f), 0xF2C94C);
  mb_box(mb, v3(-0.225f, 0.86f, -0.025f), v3(0.225f, 0.88f, 0.225f), 0xF2C94C);
  mb_box(mb, v3(0.1f, 0.26f, -0.47f), v3(0.24f, 0.33f, -0.3f), st);
  place(mb, p, at, yaw, sc);
}

// Statua spezzata: testa enorme caduta a terra.
void fallen_head(MeshBuilder* mb, const Style& S, Vec3 at, float yaw) {
  const Part p = part(mb);
  mb_box(mb, v3(-0.42f, 0.0f, -0.38f), v3(0.42f, 0.62f, 0.26f), S.wall_band);
  mb_box(mb, v3(-0.27f, 0.04f, 0.1f), v3(0.27f, 0.58f, 0.44f), lighten(S.rock, 0.06f));
  mb_box(mb, v3(-0.07f, 0.2f, 0.4f), v3(0.07f, 0.36f, 0.52f), S.rock);
  place_xf(mb, p, xf_mul(xf_translate(at), xf_mul(xf_rot_y(yaw), xf_rot_z(0.3f))));
  outline(mb, p, 0.03f, S.rock_line);
  const Part q = part(mb);
  mb_box(mb, v3(-0.43f, 0.3f, -0.39f), v3(0.43f, 0.34f, 0.27f), 0xF2C94C);
  place_xf(mb, q, xf_mul(xf_translate(at), xf_mul(xf_rot_y(yaw), xf_rot_z(0.3f))));
}

// Chac Mool: figura sdraiata sui gomiti, ginocchia alzate, il piatto delle offerte sulla pancia.
void chac_mool(MeshBuilder* mb, const Style& S, Vec3 at, float yaw) {
  const Part p = part(mb);
  const uint32_t st = S.rock, dk = S.rock_dark;
  mb_box(mb, v3(-0.44f, 0.0f, -0.3f), v3(0.44f, 0.16f, 0.3f), dk);                       // basamento
  mb_box(mb, v3(-0.36f, 0.16f, -0.19f), v3(0.08f, 0.36f, 0.19f), st);                    // busto
  mb_box(mb, v3(-0.44f, 0.28f, -0.16f), v3(-0.22f, 0.6f, 0.16f), st);                    // spalle alzate
  mb_box(mb, v3(-0.47f, 0.5f, -0.13f), v3(-0.25f, 0.76f, 0.13f), lighten(st, 0.06f));    // testa
  mb_box(mb, v3(0.04f, 0.16f, -0.18f), v3(0.23f, 0.56f, -0.04f), st);                    // ginocchia
  mb_box(mb, v3(0.04f, 0.16f, 0.04f), v3(0.23f, 0.56f, 0.18f), st);
  mb_box(mb, v3(0.23f, 0.16f, -0.18f), v3(0.4f, 0.3f, 0.18f), st);                       // piedi
  outline(mb, p, 0.03f, S.rock_line);
  mb_cylinder(mb, v3(-0.13f, 0.36f, 0.0f), 0.16f, 0.19f, 0.06f, 7, S.deco2);              // il piatto
  place(mb, p, at, yaw, 1.15f);
}

// Testa del serpente piumato (Kukulkan): grande testa di pietra con le fauci aperte, verso yaw.
void serpent_head(MeshBuilder* mb, const Style& S, Vec3 at, float yaw, float sc) {
  const Part p = part(mb);
  const uint32_t st = S.rock, dk = S.rock_dark;
  mb_box(mb, v3(-0.34f, 0.0f, -0.42f), v3(0.34f, 0.16f, 0.36f), dk);                     // base
  mb_box(mb, v3(-0.3f, 0.16f, -0.4f), v3(0.3f, 0.66f, 0.1f), st);                        // cranio
  mb_box(mb, v3(-0.26f, 0.16f, 0.1f), v3(0.26f, 0.3f, 0.5f), st);                        // mascella
  mb_box(mb, v3(-0.28f, 0.42f, 0.06f), v3(0.28f, 0.62f, 0.5f), st);                      // muso
  mb_box(mb, v3(-0.33f, 0.62f, -0.38f), v3(0.33f, 0.74f, 0.02f), lighten(st, 0.07f));    // piume sopra gli occhi
  outline(mb, p, 0.03f, S.rock_line);
  mb_box(mb, v3(-0.21f, 0.3f, 0.3f), v3(0.21f, 0.42f, 0.49f), 0x2A2420);                 // bocca aperta
  mb_box(mb, v3(-0.23f, 0.5f, -0.02f), v3(-0.12f, 0.59f, 0.08f), 0x2A2420);              // occhi
  mb_box(mb, v3(0.12f, 0.5f, -0.02f), v3(0.23f, 0.59f, 0.08f), 0x2A2420);
  mb_box(mb, v3(-0.05f, 0.31f, 0.46f), v3(0.05f, 0.34f, 0.64f), S.wall_band2);           // lingua
  place(mb, p, at, yaw, sc);
}

// Stele: lastra di pietra con i glifi dipinti sulla faccia.
void stela(MeshBuilder* mb, const Style& S, Vec3 at, float yaw) {
  const Part p = part(mb);
  mb_box(mb, v3(-0.36f, 0.0f, -0.2f), v3(0.36f, 0.14f, 0.2f), S.rock_dark);
  mb_box(mb, v3(-0.27f, 0.14f, -0.12f), v3(0.27f, 1.36f, 0.12f), S.rock);
  outline(mb, p, 0.03f, S.rock_line);
  for (int k = 0; k < 3; ++k) {
    const float y = 0.34f + 0.33f * (float)k;
    mb_box(mb, v3(-0.2f, y, 0.12f), v3(-0.02f, y + 0.22f, 0.135f), k == 1 ? S.wall_band2 : S.rock_dark);
    mb_box(mb, v3(0.02f, y, 0.12f), v3(0.2f, y + 0.22f, 0.135f), k == 1 ? S.rock_dark : S.wall_band);
  }
  place(mb, p, at, yaw, 1.0f);
}

// Colonna del Tempio dei Guerrieri (le Mille Colonne): pilastro quadrato con i guerrieri scolpiti.
void maya_column(MeshBuilder* mb, const Style& S, Vec3 at) {
  const Part p = part(mb);
  mb_box(mb, v3(-0.32f, 0.0f, -0.32f), v3(0.32f, 0.12f, 0.32f), S.rock_dark);
  mb_box(mb, v3(-0.24f, 0.12f, -0.24f), v3(0.24f, 1.42f, 0.24f), S.rock);
  mb_box(mb, v3(-0.32f, 1.42f, -0.32f), v3(0.32f, 1.56f, 0.32f), S.wall_trim);
  outline(mb, p, 0.03f, S.rock_line);
  unlit(mb, true);
  for (int k = 0; k < 2; ++k) {
    const float y = 0.32f + 0.52f * (float)k;
    quad_f(mb, v3(-0.17f, y, 0.243f), v3(0.17f, y, 0.243f), v3(0.17f, y + 0.36f, 0.243f), v3(-0.17f, y + 0.36f, 0.243f), v3(0, 0, 1),
           k ? S.wall_band : S.rock_line);
  }
  unlit(mb, false);
  place(mb, p, at, 0.0f, 1.0f);
}

// Masso / rovina della casella 'o' secondo lo stile.
void rock_cell(MeshBuilder* mb, const Style& S, int cx, int cz, Vec3 c, uint32_t h) {
  const Level* L = B.L;
  const float yaw = (float)((h >> 9) % 628u) * 0.01f;
  const Vec3 at = c + v3(0.08f * hs(cx, cz, 41), 0, 0.08f * hs(cx, cz, 42));
  switch (B.style) {
    case STYLE_PIRAMIDI: {
      // Accanto al sentiero: una sfinge rivolta verso il sentiero (il viale delle sfingi).
      int px = 0, pz = 0;
      for (int r = 1; r <= 2 && !px && !pz; ++r) {
        if (cell_or_void(L, cx + r, cz) == C_PATH) px = 1;
        else if (cell_or_void(L, cx - r, cz) == C_PATH) px = -1;
        else if (cell_or_void(L, cx, cz + r) == C_PATH) pz = 1;
        else if (cell_or_void(L, cx, cz - r) == C_PATH) pz = -1;
      }
      if (px || pz) {
        sphinx(mb, S, c, d_atan2((float)px, (float)pz), 1.2f);
        return;
      }
      const uint32_t v = h % 4u;
      if (v == 0) {
        column_drum(mb, S, at, 0.3f, 0.8f, false, yaw, S.rock);
        column_drum(mb, S, at + v3(0, 0.8f, 0), 0.36f, 0.12f, false, yaw, S.rock_dark);
      } else if (v == 1) {
        obox(mb, at + v3(-0.42f, 0, -0.36f), at + v3(0.38f, 0.55f, 0.36f), 0.0f, S.rock, S.rock_line, 0.03f);
        const Part p = part(mb);
        mb_box(mb, v3(-0.28f, 0.0f, -0.22f), v3(0.28f, 0.4f, 0.22f), S.rock_dark);
        place(mb, p, at + v3(0.04f, 0.55f, 0.02f), yaw * 0.3f, 1.0f);
        outline(mb, p, 0.03f, S.rock_line);
      } else if (v == 2) {
        fallen_head(mb, S, at, yaw);
      } else {
        column_drum(mb, S, at, 0.3f, 0.95f, true, yaw, S.rock);
        column_drum(mb, S, at + v3(0.12f, 0.0f, -0.28f), 0.24f, 0.3f, false, yaw, S.rock_dark);
      }
      return;
    }
    case STYLE_COLOSSEO: {
      const uint32_t v = h % 3u;
      if (v == 0) {
        column_drum(mb, S, at, 0.32f, 1.1f, false, yaw, S.rock);
        obox(mb, at + v3(-0.36f, 1.1f, -0.36f), at + v3(0.36f, 1.25f, 0.36f), 0.0f, S.rock_dark, S.rock_line, 0.03f);
      } else if (v == 1) {
        column_drum(mb, S, at, 0.3f, 0.95f, true, yaw, S.rock);
        column_drum(mb, S, at + v3(0.15f, 0.0f, 0.25f), 0.2f, 0.25f, false, yaw, S.rock_dark);
      } else {
        obox(mb, at + v3(-0.45f, 0, -0.35f), at + v3(0.45f, 0.5f, 0.35f), 0.0f, S.rock_dark, S.rock_line, 0.03f);
        column_drum(mb, S, at + v3(0, 0.5f, 0), 0.24f, 0.35f, false, yaw, S.rock);
      }
      return;
    }
    case STYLE_CHICHENITZA: {
      // Colonne in fila (o.o): le Mille Colonne. Accanto a un sentiero: testa del serpente rivolta
      // verso il sentiero. Altrove Chac Mool o stele.
      const bool row = cell_or_void(L, cx + 2, cz) == C_ROCK || cell_or_void(L, cx - 2, cz) == C_ROCK ||
                       cell_or_void(L, cx, cz + 2) == C_ROCK || cell_or_void(L, cx, cz - 2) == C_ROCK;
      if (row) {
        maya_column(mb, S, c);
        return;
      }
      int px = 0, pz = 0;
      for (int r = 1; r <= 2 && !px && !pz; ++r) {
        if (cell_or_void(L, cx, cz + r) == C_PATH) pz = 1;
        else if (cell_or_void(L, cx + r, cz) == C_PATH) px = 1;
        else if (cell_or_void(L, cx - r, cz) == C_PATH) px = -1;
        else if (cell_or_void(L, cx, cz - r) == C_PATH) pz = -1;
      }
      if (px || pz) {
        if (h % 3u == 0) chac_mool(mb, S, c, d_atan2((float)px, (float)pz) + D_HALF_PI);
        else serpent_head(mb, S, c, d_atan2((float)px, (float)pz), 1.15f);
        return;
      }
      if (h % 2u) stela(mb, S, at, 0.2f * hs(cx, cz, 44));
      else chac_mool(mb, S, at, yaw);
      return;
    }
    default:
      boulder(mb, S, at, 1.2f + 0.15f * hs(cx, cz, 43), h, true);
      return;
  }
}

// Muretto basso '=' secondo lo stile: tratti continui verso i vicini.
void low_wall(MeshBuilder* mb, const Style& S, int cx, int cz, Vec3 c, uint32_t h) {
  const Level* L = B.L;
  auto lowish = [&](int a, int b) { const int t = cell_or_void(L, a, b); return t == C_LOW || t == C_WALL; };
  const bool w = lowish(cx - 1, cz), e = lowish(cx + 1, cz), n = lowish(cx, cz - 1), s = lowish(cx, cz + 1);
  const bool any = w || e || n || s;
  if (B.style == STYLE_PIRAMIDI && !any) {
    // Rocchi di colonna in fila (o uno solo).
    column_drum(mb, S, c + v3(0.06f * hs(cx, cz, 2), 0, 0.06f * hs(cx, cz, 3)), 0.34f, LOW_H, false, (float)(h % 628u) * 0.01f, S.low);
    return;
  }
  const float t = B.style == STYLE_MURAGLIA ? 0.2f : 0.17f;
  const float top = LOW_H;
  // pezzo centrale e bracci verso i vicini
  float x0 = c.x - t, x1 = c.x + t, z0 = c.z - t, z1 = c.z + t;
  if (w || !any) x0 = c.x - 0.5f;
  if (e || !any) x1 = c.x + 0.5f;
  const uint32_t col = S.low, ctop = S.low_top;
  if (x1 - x0 > 2.0f * t + 0.01f) obox(mb, v3(x0, -0.05f, z0), v3(x1, top - 0.06f, z1), 0.0f, col, S.outline, 0.03f);
  if (n) obox(mb, v3(c.x - t, -0.05f, c.z - 0.5f), v3(c.x + t, top - 0.06f, c.z), 0.0f, col, S.outline, 0.03f);
  if (s) obox(mb, v3(c.x - t, -0.05f, c.z), v3(c.x + t, top - 0.06f, c.z + 0.5f), 0.0f, col, S.outline, 0.03f);
  if (!(x1 - x0 > 2.0f * t + 0.01f) && !n && !s) obox(mb, v3(x0, -0.05f, z0), v3(x1, top - 0.06f, z1), 0.0f, col, S.outline, 0.03f);
  // copertina
  const float ct = t + 0.04f;
  if (x1 - x0 > 2.0f * t + 0.01f) box5(mb, v3(x0, top - 0.07f, c.z - ct), v3(x1, top, c.z + ct), ctop);
  if (n) box5(mb, v3(c.x - ct, top - 0.07f, c.z - 0.5f), v3(c.x + ct, top, c.z), ctop);
  if (s) box5(mb, v3(c.x - ct, top - 0.07f, c.z), v3(c.x + ct, top, c.z + 0.5f), ctop);
  if (B.style == STYLE_MURAGLIA && (x1 - x0 > 0.9f)) {
    for (int k = 0; k < 2; ++k) {
      const float mx = c.x - 0.25f + 0.5f * (float)k;
      box5(mb, v3(mx - 0.1f, top, c.z - t), v3(mx + 0.1f, top + 0.16f, c.z + t), S.low);
    }
  }
  if (B.style == STYLE_CHICHENITZA) {
    // Campo della pelota: a meta' di un muretto lungo (almeno 5 caselle in fila) l'anello di pietra,
    // girato verso la telecamera.
    int a = 0, b = 0;
    const bool vert = n || s;
    while (cell_or_void(L, cx - (vert ? 0 : a + 1), cz - (vert ? a + 1 : 0)) == C_LOW) a++;
    while (cell_or_void(L, cx + (vert ? 0 : b + 1), cz + (vert ? b + 1 : 0)) == C_LOW) b++;
    if (a + b + 1 >= 5 && (a == b || a == b + 1)) {
      const Part p = part(mb);
      mb_box(mb, v3(-0.07f, top - 0.02f, -0.07f), v3(0.07f, top + 0.16f, 0.07f), S.low_top);
      for (int side = 0; side < 2; ++side) {
        const Part q = part(mb);
        mb_ring(mb, v3(0, 0, 0), 0.11f, 0.27f, 10, side ? S.low : lighten(S.low, 0.06f));
        place_xf(mb, q, xf_mul(xf_translate(v3(0, top + 0.4f, side ? -0.02f : 0.02f)), xf_rot_x(side ? -D_HALF_PI : D_HALF_PI)));
      }
      place(mb, p, c, 0.0f, 1.0f);
      outline(mb, p, 0.025f, S.outline);
    }
  }
}

// Blocco da 1 m 'c' secondo lo stile.
void block_cell(MeshBuilder* mb, const Style& S, int cx, int cz, Vec3 c, uint32_t h) {
  const float yaw = 0.07f * hs(cx, cz, 61);
  if (B.style == STYLE_COLOSSEO) {
    // cassa di legno delle scorte dei gladiatori
    const Part p = part(mb);
    mb_bevel_box(mb, v3(-0.45f, 0.0f, -0.45f), v3(0.45f, CRATE_H, 0.45f), 0.05f, S.block);
    outline(mb, p, 0.035f, S.outline);
    for (int k = 0; k < 2; ++k) {
      const float y = k ? 0.72f : 0.22f;
      mb_box(mb, v3(-0.465f, y, -0.465f), v3(0.465f, y + 0.08f, 0.465f), S.trunk_dark);
    }
    mb_box(mb, v3(-0.46f, CRATE_H - 0.01f, -0.06f), v3(0.46f, CRATE_H + 0.01f, 0.06f), S.trunk_dark);
    place(mb, p, c, yaw, 1.0f);
    return;
  }
  const Part p = part(mb);
  mb_bevel_box(mb, v3(-0.46f, -0.02f, -0.46f), v3(0.46f, CRATE_H, 0.46f), 0.07f, S.block);
  place(mb, p, c, yaw, 1.0f);
  outline(mb, p, 0.035f, S.outline);
  const Part q = part(mb);
  box5(mb, v3(-0.38f, CRATE_H - 0.02f, -0.38f), v3(0.38f, CRATE_H + 0.012f, 0.38f), S.block_top);
  if (B.style == STYLE_CHICHENITZA) {
    // blocco scolpito: due glifi sulla faccia verso la telecamera
    unlit(mb, true);
    for (int k = 0; k < 2; ++k) {
      const float x = -0.34f + 0.38f * (float)k;
      quad_f(mb, v3(x, 0.22f, 0.465f), v3(x + 0.3f, 0.22f, 0.465f), v3(x + 0.3f, 0.7f, 0.465f), v3(x, 0.7f, 0.465f), v3(0, 0, 1), S.rock_line);
      quad_f(mb, v3(x + 0.07f, 0.32f, 0.468f), v3(x + 0.23f, 0.32f, 0.468f), v3(x + 0.23f, 0.6f, 0.468f), v3(x + 0.07f, 0.6f, 0.468f), v3(0, 0, 1),
             k ? S.wall_band2 : S.wall_band);
    }
    unlit(mb, false);
  }
  place(mb, q, c, yaw, 1.0f);
}

// --- decorazioni sparse ------------------------------------------------------------------------

void tuft(MeshBuilder* mb, Vec3 at, float hgt, uint32_t c, uint32_t h) {
  for (int k = 0; k < 3; ++k) {
    const float a = (float)k * 2.1f + (float)(h % 7u);
    const float dx = 0.07f * d_cos(a), dz = 0.07f * d_sin(a);
    const Vec3 b0 = at + v3(-dz, 0.0f, dx), b1 = at + v3(dz, 0.0f, -dx), tip = at + v3(dx * 1.8f, hgt, dz * 1.8f);
    tri_f(mb, b0, b1, tip, v3(dz, 0.3f, -dx) * -1.0f, c);
    tri_f(mb, b0, tip, b1, v3(dz, 0.3f, -dx), mix(c, 0x000000, 0.15f));
  }
}
void flower(MeshBuilder* mb, Vec3 at, uint32_t petal, uint32_t heart) {
  unlit(mb, true);
  mb_disc(mb, at + v3(0, 0.03f, 0), 0.1f, 5, petal);
  mb_disc(mb, at + v3(0, 0.035f, 0), 0.04f, 4, heart);
  unlit(mb, false);
}
void pebble(MeshBuilder* mb, Vec3 at, float s, uint32_t c) {
  // sasso piatto: quattro facce verso l'alto
  const Vec3 top = at + v3(0.02f * s, 0.08f * s, 0.0f);
  const Vec3 q[4] = { at + v3(-0.12f * s, 0, -0.02f * s), at + v3(0.0f, 0, -0.1f * s), at + v3(0.13f * s, 0, 0.01f * s), at + v3(0.01f * s, 0, 0.09f * s) };
  for (int k = 0; k < 4; ++k) tri_f(mb, q[k], q[(k + 1) & 3], top, (q[k] + q[(k + 1) & 3]) * 0.5f - at + v3(0, 0.2f, 0), k & 1 ? c : lighten(c, 0.1f));
}
void reeds(MeshBuilder* mb, const Style& S, Vec3 at, uint32_t h) {
  for (int k = 0; k < 4; ++k) {
    const float a = (float)k * 1.7f + (float)(h % 5u);
    const Vec3 b = at + v3(0.12f * d_cos(a), 0, 0.12f * d_sin(a));
    const float hg = 0.45f + 0.2f * hf((int)h, k, 3);
    const Vec3 tip = b + v3(0.1f * d_cos(a), hg, 0.1f * d_sin(a));
    const Vec3 side = v3(-d_sin(a), 0, d_cos(a)) * 0.035f;
    tri_f(mb, b - side, b + side, tip, v3(d_cos(a), 0.2f, d_sin(a)), (k & 1) ? S.leaf0 : S.leaf1);
    tri_f(mb, b - side, tip, b + side, v3(-d_cos(a), 0.2f, -d_sin(a)), mix(S.leaf1, 0x000000, 0.2f));
  }
}

// Decorazione piccola su una casella di terreno libero (a gruppi, guidati dal rumore).
void ground_deco(MeshBuilder* mb, const Style& S, int cx, int cz, Vec3 c, int ccell) {
  const float cl = fbm(c.x, c.z, 77, 5.0f);
  if (cl < 0.52f) return;
  const uint32_t h = hash2(cx, cz, 91);
  const float p = (cl - 0.52f) * 1.6f;
  if (hf(cx, cz, 92) > p) return;
  const Vec3 at = c + v3(0.35f * hs(cx, cz, 93), 0, 0.35f * hs(cx, cz, 94));
  const uint32_t v = h % 8u;
  switch (B.style) {
    case STYLE_PIRAMIDI:
      if (ccell == C_PATH) { if (v < 3) pebble(mb, at, 0.8f, lighten(S.path, -0.12f)); return; }
      if (v < 3) pebble(mb, at, 1.0f, S.rock_dark);
      else if (v < 6) tuft(mb, at, 0.22f, 0xA9A24E, h);
      else pebble(mb, at, 0.7f, S.rock);
      return;
    case STYLE_CHICHENITZA: case STYLE_MURAGLIA:
      if (ccell == C_PATH) { if (v < 3) pebble(mb, at, 0.8f, lighten(S.path, -0.15f)); return; }
      if (v < 2) flower(mb, at, (h >> 4) & 1u ? 0xFFF4C8 : (B.style == STYLE_MURAGLIA ? 0xF7A8C8 : 0xF25A3A), 0xE8913A);
      else if (v < 6) tuft(mb, at, 0.25f, lighten(S.floor, -0.25f), h);
      else pebble(mb, at, 0.9f, S.rock);
      return;
    default:
      if (v < 2) pebble(mb, at, 0.8f, S.rock_dark);
      return;
  }
}

// Botola di legno nella sabbia (i punti da cui escono i mostri, nel Colosseo).
void trapdoor(MeshBuilder* mb, const Style& S, Vec3 c) {
  const float y = 0.028f;
  const Part p = part(mb);
  mb_box(mb, v3(-0.46f, -0.02f, -0.46f), v3(0.46f, y, 0.46f), S.gate_dark);
  for (int k = 0; k < 4; ++k) {
    const float x0 = -0.42f + 0.215f * (float)k;
    mb_box(mb, v3(x0, y, -0.42f), v3(x0 + 0.19f, y + 0.02f, 0.42f), k & 1 ? S.gate : lighten(S.gate, 0.06f));
  }
  mb_box(mb, v3(-0.44f, y + 0.02f, -0.3f), v3(0.44f, y + 0.035f, -0.22f), S.gate_metal);
  mb_box(mb, v3(-0.44f, y + 0.02f, 0.22f), v3(0.44f, y + 0.035f, 0.3f), S.gate_metal);
  place(mb, p, c, 0.0f, 1.0f);
}

// Oggetti fissi del blocco di caselle [cx0, cx1) x [cz0, cz1) della mappa.
void build_cells(MeshBuilder* mb, int cx0, int cz0, int cx1, int cz1) {
  const Level* L = B.L;
  const Style& S = *B.S;
  for (int cz = cz0; cz < cz1; ++cz)
    for (int cx = cx0; cx < cx1; ++cx) {
      const int c = cell_or_void(L, cx, cz);
      if (c == C_VOID) continue;
      const Vec3 at = ccenter(cx, cz);
      const uint32_t h = hash2(cx, cz, 1);
      switch (c) {
        case C_TREE: {
          const Vec3 p = at + v3(0.12f * hs(cx, cz, 5), 0, 0.12f * hs(cx, cz, 6));
          tree_at(mb, S, p, 1.0f + 0.12f * hs(cx, cz, 7), h, true);
          break;
        }
        case C_ROCK: rock_cell(mb, S, cx, cz, at, h); break;
        case C_LOW: low_wall(mb, S, cx, cz, at, h); break;
        case C_CRATE: block_cell(mb, S, cx, cz, at, h); break;
        case C_FLOOR: case C_PATH: ground_deco(mb, S, cx, cz, at, c); break;
        default: break;
      }
    }
  if (B.style == STYLE_COLOSSEO)
    for (int z = 0; z < L->nzones; ++z)
      for (int i = 0; i < L->zones[z].nspawn; ++i) {
        const int sx = L->zones[z].spawn[i][0], sz = L->zones[z].spawn[i][1];
        if (sx >= cx0 && sx < cx1 && sz >= cz0 && sz < cz1) trapdoor(mb, S, ccenter(sx, sz));
      }
  // Canne lungo le rive (Piramidi e stagni della giungla): dove il rumore lo vuole, sulle caselle
  // libere accanto all'acqua.
  if (B.style == STYLE_PIRAMIDI || B.style == STYLE_CHICHENITZA)
    for (int cz = cz0; cz < cz1; ++cz)
      for (int cx = cx0; cx < cx1; ++cx) {
        if (cell_or_void(L, cx, cz) != C_FLOOR) continue;
        bool wet = false;
        for (int k = 0; k < 4; ++k) {
          static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
          if (cell_or_void(L, cx + D[k][0], cz + D[k][1]) == C_WATER) wet = true;
        }
        if (!wet || hf(cx, cz, 71) > 0.45f) continue;
        reeds(mb, S, ccenter(cx, cz) + v3(0.3f * hs(cx, cz, 72), 0, 0.3f * hs(cx, cz, 73)), hash2(cx, cz, 74));
      }
}

// Riflessi chiari sparsi sull'acqua aperta (anche quella che continua fuori dalla mappa).
void water_glints(MeshBuilder* mb, int i0, int j0, int i1, int j1) {
  unlit(mb, true);
  for (int j = j0; j < j1; ++j)
    for (int i = i0; i < i1; ++i) {
      const int k = B.key[j][i];
      const bool pit = kkind(k) == TK_PIT;
      if ((kkind(k) != TK_WATER || (k & 3) == 0) && !pit) continue;
      if (hf(i, j, 601) > (pit ? 0.35f : 0.22f)) continue;
      const float x = (float)i + 0.2f + 0.6f * hf(i, j, 602) + B.ox, z = (float)j + 0.3f + 0.4f * hf(i, j, 603) + B.oz;
      const float l = 0.18f + 0.16f * hf(i, j, 604), y = (pit ? PIT_WATER_Y : WATER_Y) + 0.012f;
      quad_f(mb, v3(x - l, y, z), v3(x + l, y, z), v3(x + l - 0.06f, y, z + 0.035f), v3(x - l + 0.06f, y, z + 0.035f), v3(0, 1, 0), pit ? 0x5E8F6A : 0xE6F7FF);
    }
  unlit(mb, false);
}

// --- paesaggio vicino (caselle fuori dalla mappa) ------------------------------------------------

// Poche cose piccole ed economiche sulla striscia attorno alla mappa (le sagome grandi, dune,
// colline e la chioma della giungla, le mette il paesaggio dello stile): ciuffi, sassi, qualche
// palma o pino rado. Niente contro i muri del bordo e niente nel Colosseo (le gradinate).
void build_margin(MeshBuilder* mb, int i0, int j0, int i1, int j1) {
  const Style& S = *B.S;
  if (B.style == STYLE_COLOSSEO || B.style == STYLE_CHICHENITZA) return;
  for (int j = j0; j < j1; ++j)
    for (int i = i0; i < i1; ++i) {
      if (cell_ext(i, j) != C_VOID) continue;
      const int d = dist_out(i, j);
      if (d < 2 || d > 11) continue;
      // niente dove la mappa ha caselle vuote accanto (dentro la griglia)
      bool near = false;
      for (int dz = -1; dz <= 1 && !near; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
          if (cell_ext(i + dx, j + dz) != C_VOID) { near = true; break; }
      if (near) continue;
      const float wx = (float)i + B.ox + 0.5f, wz = (float)j + B.oz + 0.5f;
      const uint32_t h = hash2(i, j, 301);
      const float cl = fbm(wx, wz, 303, 6.0f);
      const float r = hf(i, j, 302);
      const Vec3 at = v3(wx + 0.6f * hs(i, j, 304), 0.0f, wz + 0.6f * hs(i, j, 305));
      const float tree_p = (B.style == STYLE_PIRAMIDI ? 0.012f : 0.02f) * (cl > 0.5f ? 1.6f : 0.3f);
      if (r < tree_p) tree_at(mb, S, at, 0.9f + 0.35f * hf(i, j, 306), h, false);
      else if (r < tree_p + 0.015f) boulder(mb, S, at, 0.8f + 0.5f * hf(i, j, 307), h, false);
      else if (r < tree_p + 0.06f && cl > 0.45f) tuft(mb, at, 0.27f, B.style == STYLE_PIRAMIDI ? 0xA9A24E : lighten(S.out, -0.25f), h);
    }
}

// --- forme grandi -------------------------------------------------------------------------------

// Cupola: meta' superiore di un ellissoide con la base sul piano y = c.y (dune, colline, nebbia).
void dome(MeshBuilder* mb, Vec3 c, float rx, float hgt, float rz, int seg, int rings, uint32_t col) {
  const int top = mb_vert(mb, c + v3(0, hgt, 0));
  const int first = mb->nv;
  for (int k = 1; k <= rings; ++k) {
    const float th = D_HALF_PI * (float)k / (float)rings, st = d_sin(th), ct = d_cos(th);
    for (int j = 0; j < seg; ++j) {
      const float ph = D_TWO_PI * (float)j / (float)seg;
      mb_vert(mb, c + v3(rx * st * d_cos(ph), hgt * ct, rz * st * d_sin(ph)));
    }
  }
  for (int j = 0; j < seg; ++j) {
    const int j1 = (j + 1) % seg;
    mb_tri(mb, top, first + j1, first + j, col);
    for (int k = 0; k + 1 < rings; ++k) {
      const int a0 = first + k * seg + j, a1 = first + k * seg + j1, b0 = a0 + seg, b1 = a1 + seg;
      mb_tri(mb, a0, a1, b1, col);
      mb_tri(mb, a0, b1, b0, col);
    }
  }
}
// Quota di una cupola nel punto (x, z), -1 fuori.
float dome_h(Vec3 c, float rx, float hgt, float rz, float x, float z) {
  const float u = (x - c.x) / rx, w = (z - c.z) / rz, d2 = u * u + w * w;
  if (d2 >= 1.0f) return -1.0f;
  return c.y + hgt * sqrtf(1.0f - d2);
}

// Piramide a base rettangolare con la punta di un altro colore.
void pyramid(MeshBuilder* mb, Vec3 c, float hx, float hz, float hgt, uint32_t col, uint32_t cap, float capk, uint32_t line, float t) {
  const Part p = part(mb);
  const Vec3 apex = c + v3(0, hgt, 0);
  const Vec3 b[4] = { c + v3(-hx, 0, -hz), c + v3(hx, 0, -hz), c + v3(hx, 0, hz), c + v3(-hx, 0, hz) };
  int ib[4], it[4];
  for (int k = 0; k < 4; ++k) ib[k] = mb_vert(mb, b[k]);
  for (int k = 0; k < 4; ++k) it[k] = mb_vert(mb, b[k] + (apex - b[k]) * (1.0f - capk));
  const int ia = mb_vert(mb, apex);
  for (int k = 0; k < 4; ++k) {
    const int k1 = (k + 1) & 3;
    mb_tri(mb, ib[k], it[k1], ib[k1], col);
    mb_tri(mb, ib[k], it[k], it[k1], col);
    mb_tri(mb, it[k], ia, it[k1], cap);
  }
  if (t > 0.0f) outline(mb, p, t, line);
}

// Prisma a base quadrata che si stringe in alto, senza fondo (obelischi, torri lontane).
void taper_box(MeshBuilder* mb, Vec3 c, float h0, float h1, float y0, float y1, uint32_t col) {
  int lo[4], hi[4];
  const float sx[4] = { -1, 1, 1, -1 }, sz[4] = { -1, -1, 1, 1 };
  for (int k = 0; k < 4; ++k) lo[k] = mb_vert(mb, c + v3(sx[k] * h0, y0, sz[k] * h0));
  for (int k = 0; k < 4; ++k) hi[k] = mb_vert(mb, c + v3(sx[k] * h1, y1, sz[k] * h1));
  for (int k = 0; k < 4; ++k) {
    const int k1 = (k + 1) & 3;
    mb_tri(mb, lo[k], hi[k1], lo[k1], col);
    mb_tri(mb, lo[k], hi[k], hi[k1], col);
  }
  mb_tri(mb, hi[0], hi[2], hi[1], col);
  mb_tri(mb, hi[0], hi[3], hi[2], col);
}

void obelisk(MeshBuilder* mb, const Style& S, Vec3 c, float y0, float hgt, float w, uint32_t col) {
  const Part p = part(mb);
  taper_box(mb, c, w, w * 0.62f, y0, y0 + hgt, col);
  pyramid(mb, c + v3(0, y0 + hgt, 0), w * 0.62f, w * 0.62f, w * 0.9f, 0xF2C94C, 0xF2C94C, 0.5f, 0, 0.0f);
  outline(mb, p, 0.05f, S.outline);
}

// Merli lungo il bordo di un rettangolo [x0, x1] x [z0, z1] alla quota y.
void merlons(MeshBuilder* mb, const Style& S, float x0, float z0, float x1, float z1, float y, float hgt) {
  const float w = 0.3f, d = 0.16f, step = 0.62f;
  const Part p = part(mb);
  for (int side = 0; side < 4; ++side) {
    const bool hx = side < 2;
    const float a0 = hx ? x0 : z0, a1 = hx ? x1 : z1;
    const int n = (int)((a1 - a0) / step + 0.5f);
    if (n < 1) continue;
    const float st = (a1 - a0) / (float)n;
    for (int i = 0; i < n; ++i) {
      if (side >= 2 && (i == 0 || i == n - 1)) continue;   // gli angoli li fanno i lati lunghi
      const float u = a0 + st * ((float)i + 0.5f);
      Vec3 mn, mx;
      if (hx) {
        const float z = side == 0 ? z0 : z1 - d;
        mn = v3(u - 0.5f * w, y, z); mx = v3(u + 0.5f * w, y + hgt, z + d);
      } else {
        const float x = side == 2 ? x0 : x1 - d;
        mn = v3(x, y, u - 0.5f * w); mx = v3(x + d, y + hgt, u + 0.5f * w);
      }
      box5(mb, mn, mx, S.wall);
    }
  }
  outline(mb, p, 0.03f, S.outline);
}

// Torretta di guardia della Muraglia sopra un blocco di muro.
void watch_tower(MeshBuilder* mb, const Style& S, float x0, float z0, float x1, float z1, bool big) {
  const float y0 = WALL_H, k = 0.32f;
  const float rx0 = x0 + k, rz0 = z0 + k, rx1 = x1 - k, rz1 = z1 - k, top = y0 + 1.05f;
  obox(mb, v3(rx0, y0 - 0.05f, rz0), v3(rx1, top, rz1), 0.03f, lighten(S.wall, 0.04f), S.outline, 0.04f);
  // feritoie sul lato sud e cornice
  unlit(mb, true);
  for (float x = rx0 + 0.45f; x < rx1 - 0.3f; x += 0.7f)
    quad_f(mb, v3(x - 0.09f, y0 + 0.35f, rz1 + 0.004f), v3(x + 0.09f, y0 + 0.35f, rz1 + 0.004f), v3(x + 0.09f, y0 + 0.75f, rz1 + 0.004f),
           v3(x - 0.09f, y0 + 0.75f, rz1 + 0.004f), v3(0, 0, 1), 0x2A2826);
  unlit(mb, false);
  box5(mb, v3(rx0 - 0.06f, top - 0.04f, rz0 - 0.06f), v3(rx1 + 0.06f, top + 0.08f, rz1 + 0.06f), S.wall_trim);
  merlons(mb, S, rx0 - 0.06f, rz0 - 0.06f, rx1 + 0.06f, rz1 + 0.06f, top + 0.08f, 0.32f);
  if (!big) return;
  // padiglione col tetto di tegole e le gronde all'insu'
  const float px0 = rx0 + 0.45f, pz0 = rz0 + 0.45f, px1 = rx1 - 0.45f, pz1 = rz1 - 0.45f, py = top + 0.08f;
  for (int c = 0; c < 4; ++c) {
    const float x = (c & 1) ? px1 - 0.08f : px0 + 0.08f, z = (c & 2) ? pz1 - 0.08f : pz0 + 0.08f;
    obox(mb, v3(x - 0.07f, py, z - 0.07f), v3(x + 0.07f, py + 0.85f, z + 0.07f), 0.0f, S.gate, S.outline, 0.025f);
  }
  const Part p = part(mb);
  const float e = 0.35f, ry = py + 0.85f, ridge = ry + 0.75f;
  const Vec3 c0 = v3(px0 - e, ry + 0.12f, pz0 - e), c1 = v3(px1 + e, ry + 0.12f, pz0 - e), c2 = v3(px1 + e, ry + 0.12f, pz1 + e), c3 = v3(px0 - e, ry + 0.12f, pz1 + e);
  const Vec3 m0 = v3(0.5f * (px0 + px1), ry - 0.02f, pz0 - e), m1 = v3(px1 + e, ry - 0.02f, 0.5f * (pz0 + pz1));
  const Vec3 m2 = v3(0.5f * (px0 + px1), ry - 0.02f, pz1 + e), m3 = v3(px0 - e, ry - 0.02f, 0.5f * (pz0 + pz1));
  const Vec3 apex = v3(0.5f * (px0 + px1), ridge, 0.5f * (pz0 + pz1));
  tri_f(mb, c0, m0, apex, v3(0, 1, -1), S.gate_dark);
  tri_f(mb, m0, c1, apex, v3(0, 1, -1), S.gate_dark);
  tri_f(mb, c1, m1, apex, v3(1, 1, 0), S.gate_dark);
  tri_f(mb, m1, c2, apex, v3(1, 1, 0), S.gate_dark);
  tri_f(mb, c2, m2, apex, v3(0, 1, 1), lighten(S.gate_dark, 0.08f));
  tri_f(mb, m2, c3, apex, v3(0, 1, 1), lighten(S.gate_dark, 0.08f));
  tri_f(mb, c3, m3, apex, v3(-1, 1, 0), S.gate_dark);
  tri_f(mb, m3, c0, apex, v3(-1, 1, 0), S.gate_dark);
  outline(mb, p, 0.04f, S.outline);
  mb_ellipsoid(mb, apex + v3(0, 0.08f, 0), v3(0.12f, 0.14f, 0.12f), 5, 3, S.deco1);
}

// Statua di marmo su un piedistallo (Colosseo): figura con la lancia.
void statue(MeshBuilder* mb, const Style& S, Vec3 c, float y0, float yaw) {
  const Part p = part(mb);
  const uint32_t m = 0xF2EDE2, m2 = 0xDCD5C6;
  mb_cylinder(mb, v3(0, y0, 0), 0.42f, 0.3f, 1.3f, 8, m2);                      // veste
  mb_ellipsoid(mb, v3(0, y0 + 1.55f, 0), v3(0.36f, 0.34f, 0.26f), 7, 4, m);       // spalle e torso
  mb_ellipsoid(mb, v3(0, y0 + 2.08f, 0.02f), v3(0.19f, 0.22f, 0.19f), 6, 4, m);   // testa
  mb_ellipsoid(mb, v3(0, y0 + 2.18f, -0.03f), v3(0.21f, 0.12f, 0.2f), 6, 3, 0xC9B98E);   // corona d'alloro
  mb_cylinder(mb, v3(0.4f, y0 + 1.5f, 0.05f), 0.08f, 0.07f, 0.55f, 5, m);         // braccio alzato
  outline(mb, p, 0.04f, S.outline);
  const Part q = part(mb);
  mb_cylinder(mb, v3(0.48f, y0 + 0.35f, 0.05f), 0.035f, 0.035f, 2.4f, 4, 0xB08A40);   // lancia
  mb_cylinder(mb, v3(0.48f, y0 + 2.75f, 0.05f), 0.08f, 0.0f, 0.3f, 4, 0xD9B860);
  place(mb, p, c, yaw, 1.0f);
  place(mb, q, c, yaw, 1.0f);
}

// Gradone: tronco di piramide dal rettangolo [x0, x1] x [z0, z1] alla quota y0 a quello rientrato di
// "in" alla quota y1 (fianchi col colore col, piano con top): 10 triangoli.
void tier(MeshBuilder* mb, float x0, float z0, float x1, float z1, float in, float y0, float y1, uint32_t col, uint32_t top) {
  const Vec3 b[4] = { v3(x0, y0, z0), v3(x1, y0, z0), v3(x1, y0, z1), v3(x0, y0, z1) };
  const Vec3 t[4] = { v3(x0 + in, y1, z0 + in), v3(x1 - in, y1, z0 + in), v3(x1 - in, y1, z1 - in), v3(x0 + in, y1, z1 - in) };
  const Vec3 out[4] = { v3(0, 0, -1), v3(1, 0, 0), v3(0, 0, 1), v3(-1, 0, 0) };
  for (int k = 0; k < 4; ++k) {
    const int k1 = (k + 1) & 3;
    quad_f(mb, b[k], b[k1], t[k1], t[k], out[k] + v3(0, 0.2f, 0), k == 2 ? col : lighten(col, k == 0 ? -0.08f : -0.03f));
  }
  quad_f(mb, t[0], t[1], t[2], t[3], v3(0, 1, 0), top);
}

// Scalinata sul lato (sx, sz) di un tempio maya: dal piede (centro m0, quota y0) alla cima (m1, y1),
// larga w, con i gradini segnati e le balaustre; serpent: le teste di Kukulkan in basso.
void maya_stair(MeshBuilder* mb, const Style& S, Vec3 m0, Vec3 m1, Vec3 side, float w, bool serpent, float yaw) {
  const Vec3 h = side * (0.5f * w);
  const Vec3 up = m1 - m0;
  const Vec3 facing = normalize(cross(side, up));
  quad_f(mb, m0 - h, m0 + h, m1 + h, m1 - h, facing.y < 0.0f ? facing * -1.0f : facing, lighten(S.wall_top, -0.02f));
  unlit(mb, true);
  const int ns = 9;
  for (int k = 1; k < ns; ++k) {
    const float t = (float)k / (float)ns;
    const Vec3 a = m0 + up * t + v3(0, 0.012f, 0), b = m0 + up * (t + 0.035f) + v3(0, 0.012f, 0);
    quad_f(mb, a - h * 0.96f, a + h * 0.96f, b + h * 0.96f, b - h * 0.96f, v3(0, 1, 0), S.wall_dark);
  }
  unlit(mb, false);
  for (int k = -1; k <= 1; k += 2) {   // balaustre
    const Vec3 e0 = m0 + h * (float)k, e1 = m1 + h * (float)k, o = side * (0.14f * (float)k);
    quad_f(mb, e0 + v3(0, 0.12f, 0), e1 + v3(0, 0.12f, 0), e1 + o + v3(0, 0.12f, 0), e0 + o + v3(0, 0.12f, 0), v3(0, 1, 0), S.wall_trim);
    quad_f(mb, e0 + o, e1 + o, e1 + o + v3(0, 0.12f, 0), e0 + o + v3(0, 0.12f, 0), side * (float)k, S.wall);
    if (serpent) serpent_head(mb, S, e0 + o * 0.5f, yaw, 0.62f);
  }
}

// El Castillo (Chichen Itza), sopra un blocco di muro grande: gradoni, scalinate al centro dei lati
// che si vedono (sud con le teste del serpente, est, ovest) e il tempio in cima con le porte.
void el_castillo(MeshBuilder* mb, const Style& S, float x0, float z0, float x1, float z1) {
  const Part p = part(mb);
  const float w = x1 - x0, d = z1 - z0, sh = fminf(w, d);
  const int tiers = 5;
  const float th = 0.62f, step_in = sh * 0.075f;
  float y = 0.7f;
  for (int k = 0; k < tiers; ++k) {
    const float e = 0.1f + step_in * (float)k;
    tier(mb, x0 + e, z0 + e, x1 - e, z1 - e, 0.07f, y, y + th, lighten(S.wall, (k & 1) ? -0.13f : -0.07f), S.wall_top);
    y += th;
  }
  const float et = 0.1f + step_in * (float)(tiers - 1) + 0.07f;   // bordo del piano in cima
  const float xm = 0.5f * (x0 + x1), zm = 0.5f * (z0 + z1), sw = 0.24f * sh;
  maya_stair(mb, S, v3(xm, 0.7f, z1 + 0.05f), v3(xm, y, z1 - et), v3(1, 0, 0), sw, true, 0.0f);
  maya_stair(mb, S, v3(x1 + 0.05f, 0.7f, zm), v3(x1 - et, y, zm), v3(0, 0, 1), sw, false, 0.0f);
  maya_stair(mb, S, v3(x0 - 0.05f, 0.7f, zm), v3(x0 + et, y, zm), v3(0, 0, -1), sw, false, 0.0f);
  // tempio in cima: corpo, fregio, tetto
  const float tw = 0.36f * w, td = 0.36f * d;
  obox(mb, v3(xm - 0.5f * tw, y, zm - 0.5f * td), v3(xm + 0.5f * tw, y + 0.95f, zm + 0.5f * td), 0.0f, S.wall, S.outline, 0.0f);
  box5(mb, v3(xm - 0.5f * tw - 0.06f, y + 0.95f, zm - 0.5f * td - 0.06f), v3(xm + 0.5f * tw + 0.06f, y + 1.25f, zm + 0.5f * td + 0.06f), S.wall_band);
  box5(mb, v3(xm - 0.5f * tw - 0.1f, y + 1.25f, zm - 0.5f * td - 0.1f), v3(xm + 0.5f * tw + 0.1f, y + 1.36f, zm + 0.5f * td + 0.1f), S.wall_trim);
  outline(mb, p, 0.05f, S.outline);
  unlit(mb, true);   // tre porte verso sud e la greca sul fregio
  for (int k = -1; k <= 1; ++k) {
    const float xc = xm + (float)k * tw * 0.3f, hw = k ? 0.12f : 0.18f;
    quad_f(mb, v3(xc - hw, y + 0.02f, zm + 0.5f * td + 0.005f), v3(xc + hw, y + 0.02f, zm + 0.5f * td + 0.005f),
           v3(xc + hw, y + 0.72f, zm + 0.5f * td + 0.005f), v3(xc - hw, y + 0.72f, zm + 0.5f * td + 0.005f), v3(0, 0, 1), 0x2A2420);
  }
  for (float x = xm - 0.5f * tw; x < xm + 0.5f * tw - 0.2f; x += 0.4f)
    quad_f(mb, v3(x, y + 1.02f, zm + 0.5f * td + 0.065f), v3(x + 0.2f, y + 1.02f, zm + 0.5f * td + 0.065f), v3(x + 0.2f, y + 1.18f, zm + 0.5f * td + 0.065f),
           v3(x, y + 1.18f, zm + 0.5f * td + 0.065f), v3(0, 0, 1), S.wall_band2);
  unlit(mb, false);
}

// Tempietto maya sopra un blocco piccolo: due gradoni, il tempio con le colonne davanti (a sud) e la
// cresta sul tetto.
void maya_temple(MeshBuilder* mb, const Style& S, float x0, float z0, float x1, float z1, uint32_t h) {
  const Part p = part(mb);
  const float w = x1 - x0, d = z1 - z0;
  tier(mb, x0 + 0.08f, z0 + 0.08f, x1 - 0.08f, z1 - 0.08f, 0.06f, 0.7f, 1.25f, S.wall, S.wall_top);
  const float xm = 0.5f * (x0 + x1), zm = 0.5f * (z0 + z1), y = 1.25f;
  const float tw = w - 0.6f, td = d - 0.7f;
  obox(mb, v3(xm - 0.5f * tw, y, zm - 0.5f * td - 0.05f), v3(xm + 0.5f * tw, y + 0.9f, zm + 0.5f * td - 0.25f), 0.0f, S.wall, S.outline, 0.0f);
  box5(mb, v3(xm - 0.5f * tw - 0.08f, y + 0.9f, zm - 0.5f * td - 0.13f), v3(xm + 0.5f * tw + 0.08f, y + 1.18f, zm + 0.5f * td + 0.08f), S.wall_band);
  box5(mb, v3(xm - 0.5f * tw - 0.12f, y + 1.18f, zm - 0.5f * td - 0.17f), v3(xm + 0.5f * tw + 0.12f, y + 1.28f, zm + 0.5f * td + 0.12f), S.wall_trim);
  // colonne del portico (tra il corpo e il bordo sud)
  const int nc = w >= 3.5f ? 4 : 2;
  for (int k = 0; k < nc; ++k) {
    const float xc = xm - 0.5f * tw + 0.15f + (tw - 0.3f) * (float)k / (float)(nc - 1);
    mb_box(mb, v3(xc - 0.09f, y, zm + 0.5f * td - 0.05f), v3(xc + 0.09f, y + 0.9f, zm + 0.5f * td + 0.08f), S.rock);
  }
  if ((h & 3u) != 0) box5(mb, v3(xm - 0.25f * tw, y + 1.28f, zm - 0.06f), v3(xm + 0.25f * tw, y + 1.75f, zm + 0.06f), S.wall_top);   // cresta
  outline(mb, p, 0.045f, S.outline);
  unlit(mb, true);
  quad_f(mb, v3(xm - 0.2f, y + 0.02f, zm + 0.5f * td - 0.245f), v3(xm + 0.2f, y + 0.02f, zm + 0.5f * td - 0.245f),
         v3(xm + 0.2f, y + 0.7f, zm + 0.5f * td - 0.245f), v3(xm - 0.2f, y + 0.7f, zm + 0.5f * td - 0.245f), v3(0, 0, 1), 0x2A2420);
  unlit(mb, false);
  maya_stair(mb, S, v3(xm, 0.7f, z1 + 0.05f), v3(xm, y, z1 - 0.14f), v3(1, 0, 0), fminf(1.0f, 0.4f * w), false, 0.0f);
}

// Strutture sopra i blocchi di muro che hanno il centro nel blocco di caselle [cx0, cx1) x [cz0, cz1).
void build_blocks_in(MeshBuilder* mb, int cx0, int cz0, int cx1, int cz1) {
  const Style& S = *B.S;
  for (int b = 0; b < B.nblocks; ++b) {
    const int bx = B.blk[b][0], bz = B.blk[b][1], bw = B.blk[b][2], bh = B.blk[b][3];
    const int mx = bx + bw / 2, mz = bz + bh / 2;
    if (mx < cx0 || mx >= cx1 || mz < cz0 || mz >= cz1) continue;
    const Vec3 a = ccenter(bx, bz), c = ccenter(bx + bw - 1, bz + bh - 1);
    const float x0 = a.x - 0.5f, z0 = a.z - 0.5f, x1 = c.x + 0.5f, z1 = c.z + 0.5f;
    const Vec3 mid = v3(0.5f * (x0 + x1), 0.0f, 0.5f * (z0 + z1));
    const float shortest = (float)(bw < bh ? bw : bh);
    switch (B.style) {
      case STYLE_PIRAMIDI:
        if (bw >= 3 && bh >= 3) pyramid(mb, mid + v3(0, 0.55f, 0), 0.5f * (x1 - x0) - 0.18f, 0.5f * (z1 - z0) - 0.18f, 0.66f * shortest + 0.4f, S.wall, 0xF2C94C, 0.12f, S.outline, 0.05f);
        else obelisk(mb, S, mid, 0.55f, 2.6f + 0.6f * shortest, 0.42f, 0xC98B73);
        break;
      case STYLE_CHICHENITZA:
        if (bw >= 5 && bh >= 5) el_castillo(mb, S, x0, z0, x1, z1);
        else maya_temple(mb, S, x0, z0, x1, z1, hash2(bx, bz, 9));
        break;
      case STYLE_MURAGLIA: watch_tower(mb, S, x0, z0, x1, z1, bw >= 3 && bh >= 3); break;
      default: statue(mb, S, mid, 0.9f, 0.3f * hs(bx, bz, 3)); break;
    }
  }
}

// --- paesaggio lontano per stile ------------------------------------------------------------------

// Centro del rettangolo della mappa e mezze dimensioni (mondo).
void map_box(float* cx, float* cz, float* hx, float* hz) {
  *cx = 0.0f;
  *cz = -0.5f * (float)B.L->H;
  *hx = 0.5f * (float)B.L->W;
  *hz = 0.5f * (float)B.L->H;
}

// Le forme del paesaggio vanno nel blocco di caselle che le contiene (dentro la griglia estesa) o
// in tessere di 24 m oltre: il renderer scarta i pezzi fuori dall'inquadratura.
const int TILE_N = 9;
const float TILE = 24.0f;
MeshBuilder g_cmb[LV_MAX_CHUNKS];   // un costruttore per blocco
int g_ncx, g_ncz;
MeshBuilder g_tile[TILE_N * TILE_N];
MeshBuilder* scene_mb(float x, float z) {
  const float ex = x - B.ox, ez = z - B.oz;
  if (ex >= 0.0f && ez >= 0.0f && ex < (float)B.ew && ez < (float)B.eh) {
    const int kx = (int)ex / LV_CHUNK, kz = (int)ez / LV_CHUNK;
    return &g_cmb[kz * g_ncx + kx];
  }
  float cx, cz, hx, hz;
  map_box(&cx, &cz, &hx, &hz);
  int tx = (int)floorf((x - cx) / TILE + 0.5f * (float)TILE_N), tz = (int)floorf((z - cz) / TILE + 0.5f * (float)TILE_N);
  tx = tx < 0 ? 0 : (tx >= TILE_N ? TILE_N - 1 : tx);
  tz = tz < 0 ? 0 : (tz >= TILE_N ? TILE_N - 1 : tz);
  return &g_tile[tz * TILE_N + tx];
}

// Sulla striscia attorno alla mappa (quella che vede la telecamera di gioco)?
bool in_ring(float x, float z, float r) {
  const float ex = x - B.ox, ez = z - B.oz;
  return ex > -r && ez > -r && ex < (float)B.ew + r && ez < (float)B.eh + r;
}

// Oltre la striscia il paesaggio si vede solo dai menu, che guardano a sud dalla partenza della
// zona 1 con la telecamera bassa (la scelta dei personaggi vede fino all'orizzonte, 120 m), e dalla
// fine della mappa, che guarda a nord da dove sono i giocatori nell'ultima zona (inclinata di 0,5:
// oltre i 60 m dall'occhio non si vede piu' nulla); circa 32 gradi di lato (game.cpp,
// update_camera). Qui con margine: 40 gradi di lato (tan 0,84). Quello che non cade in questi
// spicchi non si fa.
bool far_seen(float x, float z, float r) {
  const Level* L = B.L;
  Vec3 st = v3(0, 0, 0);
  for (int k = 0; k < 4; ++k) st = st + L->zones[0].start[k] * 0.25f;
  const float mz = st.z - 6.5f;                           // occhio dei menu, poco a nord della partenza
  if (z + r > mz && fabsf(x - st.x) < 0.84f * (z - mz) + r + 1.0f && z - r - mz < 122.0f) return true;
  const ZoneDef& zl = L->zones[L->nzones - 1];
  const float ez = zl.wz1 + 6.5f, ex = 0.5f * (zl.wx0 + zl.wx1), ehx = 0.5f * (zl.wx1 - zl.wx0);
  if (z - r < ez && fabsf(x - ex) < ehx + 0.84f * (ez - z) + r && ez - (z + r) < 62.0f) return true;
  return false;
}
bool shown(float x, float z, float r) { return in_ring(x, z, r) || far_seen(x, z, r); }

// Distanza (mondo) dal rettangolo della mappa: 0 dentro.
float out_dist(float x, float z) {
  float cx, cz, hx, hz;
  map_box(&cx, &cz, &hx, &hz);
  return fmaxf(fabsf(x - cx) - hx, fabsf(z - cz) - hz);
}

void desert_far(void) {
  const Style& S = *B.S;
  float cx, cz, hx, hz;
  map_box(&cx, &cz, &hx, &hz);
  // Dune basse sulla striscia (lontano dai muri) e quelle che vedono i menu e la fine della mappa.
  for (int gz = -16; gz <= 16; ++gz)
    for (int gx = -12; gx <= 12; ++gx) {
      const float x = cx + (float)gx * 7.0f + 2.5f * hs(gx, gz, 501), z = cz + (float)gz * 7.0f + 2.5f * hs(gx, gz, 502);
      const float d = out_dist(x, z);
      const float rx = 3.0f + 2.5f * hf(gx, gz, 503), rz = 1.8f + 1.2f * hf(gx, gz, 504);
      if (d < rx + 2.5f || d > 70.0f || hf(gx, gz, 505) > 0.55f) continue;
      const float s = d > 14.0f ? 1.6f : 1.0f;
      if (!shown(x, z, rx * s)) continue;
      MeshBuilder* mb = scene_mb(x, z);
      dome(mb, v3(x, 0.0f, z), rx * s, (0.55f + 0.7f * hf(gx, gz, 506)) * s, rz * s, 9, 2, lighten(S.out, hs(gx, gz, 507) * 0.03f));
    }
  // Le tre grandi piramidi a sud (si vedono dalla scelta dei personaggi).
  const float zs = cz + hz;
  struct Pyr { float x, z, half, h; };
  const Pyr P[3] = { { cx - 22.0f, zs + 58.0f, 17.0f, 21.0f }, { cx + 14.0f, zs + 72.0f, 15.0f, 19.0f }, { cx + 40.0f, zs + 52.0f, 8.0f, 10.0f } };
  for (int k = 0; k < 3; ++k) {
    if (!far_seen(P[k].x, P[k].z, P[k].half)) continue;
    MeshBuilder* mb = scene_mb(P[k].x, P[k].z);
    pyramid(mb, v3(P[k].x, 0.0f, P[k].z), P[k].half, P[k].half, P[k].h, S.wall, k == 1 ? 0xF4E6C4 : S.wall_top, k == 1 ? 0.2f : 0.08f, S.outline, 0.12f);
  }
  // Palme rade sulla striscia e nei menu.
  for (int gz = -16; gz <= 16; ++gz)
    for (int gx = -10; gx <= 10; ++gx) {
      const float x = cx + (float)gx * 3.2f + 1.2f * hs(gx, gz, 511), z = cz + (float)gz * 3.2f + 1.2f * hs(gx, gz, 512);
      const float d = out_dist(x, z);
      if (d < 3.0f || d > 40.0f || hf(gx, gz, 513) > 0.035f || !shown(x, z, 2.0f)) continue;
      palm(scene_mb(x, z), S, v3(x, 0.0f, z), 1.1f + 0.4f * hf(gx, gz, 514), hash2(gx, gz, 515), false);
    }
}

// Giungla dello Yucatan (Chichen Itza): una fascia fitta ed economica di chiome tonde (cupole di tre
// verdi, senza contorni) sulla striscia attorno alla mappa, con qualche ceiba piu' alta che spunta.
// Oltre i 22 m non serve nulla: dai menu (telecamera bassa) la chioma vicina copre tutto il resto.
void jungle_far(void) {
  const Style& S = *B.S;
  float cx, cz, hx, hz;
  map_box(&cx, &cz, &hx, &hz);
  static const uint32_t CAN[3] = { 0x3E8E3C, 0x337E35, 0x4A9C42 };
  for (int gz = -20; gz <= 20; ++gz)
    for (int gx = -12; gx <= 12; ++gx) {
      const float x = cx + (float)gx * 4.4f + 1.6f * hs(gx, gz, 701), z = cz + (float)gz * 4.4f + 1.6f * hs(gx, gz, 702);
      const float d = out_dist(x, z);
      const float r = 2.6f + 1.5f * hf(gx, gz, 703);
      if (d < r * 0.55f + 1.4f || d > 22.0f || !shown(x, z, r)) continue;
      const float hgt = (d < 4.0f ? 1.6f : 2.4f) + 1.4f * hf(gx, gz, 704);
      MeshBuilder* mb = scene_mb(x, z);
      dome(mb, v3(x, 0.0f, z), r, hgt, r * (0.85f + 0.2f * hf(gx, gz, 705)), 7, 2, CAN[hash2(gx, gz, 706) % 3u]);
      if (hf(gx, gz, 707) < 0.12f && d > 5.0f) {   // ceiba che spunta sopra la chioma
        const Vec3 t = v3(x + 0.8f * hs(gx, gz, 708), hgt + 1.6f, z);
        pipe(mb, v3(t.x, hgt - 0.5f, t.z), t, 0.18f, 0.14f, 4, S.trunk);
        mb_ellipsoid(mb, t + v3(0, 0.25f, 0), v3(2.2f, 0.6f, 2.0f), 7, 2, CAN[(hash2(gx, gz, 709) + 1u) % 3u]);
      }
    }
}

// Quota delle colline lontane della Muraglia (anche per appoggiarci sopra il muro).
struct Hill { float x, z, rx, h, rz; };
Hill g_hill[24];
int g_nhill;
float hills_h(float x, float z) {
  float best = 0.0f;
  for (int k = 0; k < g_nhill; ++k) {
    const float h = dome_h(v3(g_hill[k].x, 0.0f, g_hill[k].z), g_hill[k].rx, g_hill[k].h, g_hill[k].rz, x, z);
    if (h > best) best = h;
  }
  return best;
}

void wall_far(void) {
  const Style& S = *B.S;
  float cx, cz, hx, hz;
  map_box(&cx, &cz, &hx, &hz);
  // Colline basse vicine (la Muraglia ci corre sopra) e grandi lontane: solo quelle che si vedono.
  g_nhill = 0;
  for (int k = 0; k < 14 && g_nhill < 24; ++k) {
    const float a = D_TWO_PI * (float)k / 14.0f + 0.2f * hs(k, 1, 531);
    Hill& h = g_hill[g_nhill++];
    h.x = cx + (hx + 25.0f) * d_cos(a);
    h.z = cz + (hz + 27.0f) * d_sin(a);
    h.rx = 13.0f + 6.0f * hf(k, 3, 533);
    h.rz = 9.0f + 4.0f * hf(k, 4, 534);
    h.h = 2.2f + 2.4f * hf(k, 5, 535);
  }
  for (int k = 0; k < 10 && g_nhill < 24; ++k) {
    const float a = D_TWO_PI * (float)k / 10.0f + 0.3f;
    Hill& h = g_hill[g_nhill++];
    h.x = cx + (hx + 62.0f) * d_cos(a);
    h.z = cz + (hz + 62.0f) * d_sin(a);
    h.rx = 22.0f + 8.0f * hf(k, 6, 536);
    h.rz = 16.0f + 6.0f * hf(k, 7, 537);
    h.h = 9.0f + 8.0f * hf(k, 8, 538);
  }
  for (int k = 0; k < g_nhill; ++k) {
    const Hill& h = g_hill[k];
    if (!shown(h.x, h.z, fmaxf(h.rx, h.rz) * 0.8f)) continue;
    MeshBuilder* mb = scene_mb(h.x, h.z);
    dome(mb, v3(h.x, 0.0f, h.z), h.rx, h.h, h.rz, 12, 3, k & 1 ? 0x7DB357 : 0x8BBE5E);
  }
  // La Muraglia che serpeggia sulle colline a sud, con le torri (si vede dai menu).
  Vec3 prev = v3(0, 0, 0);
  const int n = 16;
  for (int i = 0; i <= n; ++i) {
    const float t = (float)i / (float)n;
    const float x = cx - 70.0f + 140.0f * t, z = cz + hz + 27.0f + 6.0f * d_sin(t * 7.0f);
    const Vec3 cur = v3(x, hills_h(x, z), z);
    if (i > 0 && far_seen(0.5f * (prev.x + cur.x), 0.5f * (prev.z + cur.z), 6.0f)) {
      const Vec3 m = (prev + cur) * 0.5f;
      const Vec3 d = cur - prev;
      const float len = sqrtf(d.x * d.x + d.z * d.z);
      MeshBuilder* mb = scene_mb(m.x, m.z);
      const Part p = part(mb);
      box5(mb, v3(-0.5f * len - 0.1f, -1.2f, -0.9f), v3(0.5f * len + 0.1f, 2.5f, 0.9f), S.wall);
      outline(mb, p, 0.1f, S.outline);
      for (int q = 0; q < 2; ++q) box5(mb, v3(-0.5f * len + (float)q * len * 0.5f + 0.4f, 2.5f, 0.45f), v3(-0.5f * len + (float)q * len * 0.5f + 1.3f, 3.1f, 0.9f), S.wall_trim);
      const Xform xf = xf_mul(xf_translate(v3(m.x, fminf(prev.y, cur.y), m.z)), xf_rot_y(-d_atan2(d.z, d.x)));
      place_xf(mb, p, xf);
      if (i % 4 == 0) {
        const Part q2 = part(mb);
        box5(mb, v3(-1.8f, -1.2f, -1.8f), v3(1.8f, 4.6f, 1.8f), lighten(S.wall, 0.04f));
        pyramid(mb, v3(0, 4.6f, 0), 2.0f, 2.0f, 1.5f, S.gate_dark, S.gate_dark, 0.0f, 0, 0.0f);
        place(mb, q2, v3(cur.x, cur.y, cur.z), 0.0f, 1.0f);
        outline(mb, q2, 0.1f, S.outline);
      }
    }
    prev = cur;
  }
  // Nebbia tra le colline e pini radi.
  for (int k = 0; k < 10; ++k) {
    const float a = D_TWO_PI * (float)k / 10.0f + 0.4f;
    const float x = cx + (hx + 40.0f) * d_cos(a), z = cz + (hz + 36.0f) * d_sin(a);
    if (!far_seen(x, z, 14.0f)) continue;
    MeshBuilder* mb = scene_mb(x, z);
    unlit(mb, true);
    dome(mb, v3(x, 1.5f, z), 14.0f, 1.2f, 5.0f, 9, 2, 0xEEF3F4);
    unlit(mb, false);
  }
  for (int k = 0; k < 30; ++k) {
    const float a = D_TWO_PI * hf(k, 11, 541), r = 1.0f + 0.5f * hf(k, 12, 542);
    const float x = cx + (hx + 13.0f) * r * d_cos(a), z = cz + (hz + 13.0f) * r * d_sin(a);
    if (!shown(x, z, 1.5f)) continue;
    pine(scene_mb(x, z), S, v3(x, hills_h(x, z) - 0.2f, z), 1.3f + 0.5f * hf(k, 13, 543), hash2(k, 14, 544), false);
  }
}

// Gradinate e facciata del Colosseo attorno all'arena (ellisse dei muri dell'arena).
void colosseum_far(void) {
  const Style& S = *B.S;
  const Level* L = B.L;
  int x0 = L->W, x1 = -1, z0 = L->H, z1 = -1;
  for (int cz = 0; cz < L->H; ++cz)
    for (int cx = 0; cx < L->W; ++cx)
      if (L->cell[cz][cx] == C_WALL && B.blk_at[cz][cx] < 0) {
        if (cx < x0) x0 = cx;
        if (cx > x1) x1 = cx;
        if (cz < z0) z0 = cz;
        if (cz > z1) z1 = cz;
      }
  if (x1 < 0) return;
  const Vec3 a = ccenter(x0, z0), b = ccenter(x1, z1);
  const float ecx = 0.5f * (a.x + b.x), ecz = 0.5f * (a.z + b.z);
  float A = 0.5f * (b.x - a.x) + 0.5f, Bz = 0.5f * (b.z - a.z) + 0.5f;   // bordo esterno del muro
  // L'ellisse deve contenere tutti i muri dell'arena, qualunque forma abbia (anche rettangolare).
  float k2 = 1.0f;
  for (int cz = z0; cz <= z1; ++cz)
    for (int cx = x0; cx <= x1; ++cx) {
      if (L->cell[cz][cx] != C_WALL || B.blk_at[cz][cx] >= 0) continue;
      const Vec3 c = ccenter(cx, cz);
      for (int q = 0; q < 4; ++q) {
        const float u = (c.x + ((q & 1) ? 0.5f : -0.5f) - ecx) / A, w = (c.z + ((q & 2) ? 0.5f : -0.5f) - ecz) / Bz;
        if (u * u + w * w > k2) k2 = u * u + w * w;
      }
    }
  const float k = sqrtf(k2) > 1.06f ? sqrtf(k2) - 0.04f : 1.0f;
  A *= k;
  Bz *= k;
  const int N = 56;
  const int TIERS = 6;
  const float depth = 1.45f, rise = 0.85f, base = WALL_H + 0.3f;
  for (int i = 0; i < N; ++i) {
    const float t0 = D_TWO_PI * (float)i / (float)N, t1 = D_TWO_PI * (float)(i + 1) / (float)N;
    const float c0 = d_cos(t0), s0 = d_sin(t0), c1 = d_cos(t1), s1 = d_sin(t1);
    auto P = [&](float extra, float c, float sn, float y) { return v3(ecx + (A + extra) * c, y, ecz + (Bz + extra) * sn); };
    const float xm = ecx + A * d_cos(0.5f * (t0 + t1)), zm = ecz + Bz * d_sin(0.5f * (t0 + t1));
    MeshBuilder* mb = scene_mb(xm + 5.0f * d_cos(0.5f * (t0 + t1)), zm + 5.0f * d_sin(0.5f * (t0 + t1)));
    const Vec3 inward = v3(-(c0 + c1), 0, -(s0 + s1) * A / Bz);
    // podio: ripiano sopra il muro dell'arena, con il parapetto
    const float e0 = -0.75f;
    quad_f(mb, P(e0, c0, s0, base), P(e0, c1, s1, base), P(0.9f, c1, s1, base), P(0.9f, c0, s0, base), v3(0, 1, 0), S.wall_top);
    quad_f(mb, P(e0, c0, s0, base - 0.3f), P(e0, c1, s1, base - 0.3f), P(e0, c1, s1, base), P(e0, c0, s0, base), inward, S.wall_band);
    // gradoni
    for (int k = 0; k < TIERS; ++k) {
      const float r0 = 0.9f + depth * (float)k, r1 = r0 + depth, y = base + rise * (float)k, yt = y + rise;
      const uint32_t riser = (k & 1) ? S.wall : lighten(S.wall, 0.05f), tread = (k & 1) ? S.wall_top : lighten(S.wall_top, -0.04f);
      quad_f(mb, P(r0, c0, s0, y), P(r0, c1, s1, y), P(r0, c1, s1, yt), P(r0, c0, s0, yt), inward, riser);
      quad_f(mb, P(r0, c0, s0, yt), P(r0, c1, s1, yt), P(r1, c1, s1, yt), P(r1, c0, s0, yt), v3(0, 1, 0), tread);
      // ingressi (vomitori) ogni tanto, sulla terza fila
      if (k == 2 && (i % 4) == 1) {
        unlit(mb, true);
        const Vec3 q0 = P(r0 - 0.01f, c0, s0, y), q1 = P(r0 - 0.01f, c1, s1, y);
        const Vec3 m0 = q0 + (q1 - q0) * 0.28f, m1 = q0 + (q1 - q0) * 0.72f;
        quad_f(mb, m0 + v3(0, 0.05f, 0), m1 + v3(0, 0.05f, 0), m1 + v3(0, rise - 0.05f, 0), m0 + v3(0, rise - 0.05f, 0), inward, 0x3A3026);
        unlit(mb, false);
      }
    }
    // muro esterno con le arcate (lato interno a fasce, cornice in cima)
    const float rw = 0.9f + depth * (float)TIERS, yb = base + rise * (float)TIERS, yt = yb + 3.6f;
    quad_f(mb, P(rw, c0, s0, yb), P(rw, c1, s1, yb), P(rw, c1, s1, yt), P(rw, c0, s0, yt), inward, S.wall);
    unlit(mb, true);
    {
      const Vec3 q0 = P(rw - 0.01f, c0, s0, yb), q1 = P(rw - 0.01f, c1, s1, yb);
      const Vec3 m0 = q0 + (q1 - q0) * 0.25f, m1 = q0 + (q1 - q0) * 0.75f;
      quad_f(mb, m0 + v3(0, 0.3f, 0), m1 + v3(0, 0.3f, 0), m1 + v3(0, 1.9f, 0), m0 + v3(0, 1.9f, 0), inward, 0x4A3E32);
    }
    unlit(mb, false);
    quad_f(mb, P(rw - 0.25f, c0, s0, yt), P(rw - 0.25f, c1, s1, yt), P(rw + 0.8f, c1, s1, yt), P(rw + 0.8f, c0, s0, yt), v3(0, 1, 0), S.wall_trim);
    quad_f(mb, P(rw - 0.25f, c0, s0, yt - 0.25f), P(rw - 0.25f, c1, s1, yt - 0.25f), P(rw - 0.25f, c1, s1, yt), P(rw - 0.25f, c0, s0, yt), inward, S.wall_trim);
    // (la facciata esterna con le arcate non c'e' piu': tutte le telecamere stanno dentro l'arena)
  }
}

void build_landscape(Level* L) {
  switch (B.style) {
    case STYLE_PIRAMIDI: desert_far(); break;
    case STYLE_CHICHENITZA: jungle_far(); break;
    case STYLE_MURAGLIA: wall_far(); break;
    default: colosseum_far(); break;
  }
  for (int k = 0; k < TILE_N * TILE_N; ++k) {
    MeshBuilder* mb = &g_tile[k];
    if (mb->nt == 0 || L->nscenery >= LV_MAX_SCENERY) { free(mb->v); free(mb->t); mb_init(mb); continue; }
    L->scenery[L->nscenery++] = mb_finish(mb);
  }
}

// --- cancelli ------------------------------------------------------------------------------------

// Battenti (0.5.4: cancelli a battente, prima una saracinesca che scendeva sotto terra): ognuno con
// il cardine nell'origine e da chiuso, lungo +X (il battente al capo u0) o -X (quello al capo u1)
// per i cancelli da ovest a est, lungo Z per quelli da nord a sud. Li gira game_draw.cpp.
void build_gate_leaf(Level* L, int gi, int side) {
  const Style& S = *B.S;
  Gate& gt = L->gates[gi];
  Vec3 hinge, dir;
  float len;
  level_gate_leaf(L, gi, side, 0.0f, &hinge, &dir, &len);
  const float sg = side ? -1.0f : 1.0f;
  const bool horiz = gt.horiz;
  MeshBuilder mb;
  mb_init(&mb);
  // s: dal cardine verso il centro del cancello; w: mezzo spessore.
  auto P = [&](float s, float y, float w) { return horiz ? v3(sg * s, y, w) : v3(w, y, sg * s); };
  auto box = [&](float s0, float s1, float y0, float y1, float w, uint32_t col, float bev, float t) {
    const Vec3 a = P(s0, y0, -w), b = P(s1, y1, w);
    obox(&mb, v3(fminf(a.x, b.x), y0, fminf(a.z, b.z)), v3(fmaxf(a.x, b.x), y1, fmaxf(a.z, b.z)), bev, col, S.outline, t);
  };
  auto tip = [&](float s, float y, float hs_, float hw, float hgt, uint32_t col, float t) {
    pyramid(&mb, P(s, y, 0.0f), horiz ? hs_ : hw, horiz ? hw : hs_, hgt, col, col, 0.0f, S.outline, t);
  };
  switch (B.style) {
    case STYLE_PIRAMIDI: {
      // Battente di cedro con le assi, le fasce dipinte e le bandelle di bronzo dei cardini.
      box(-0.1f, 0.1f, -0.05f, 1.72f, 0.11f, S.gate_dark, 0.03f, 0.035f);
      box(-0.12f, 0.12f, 1.72f, 1.8f, 0.13f, 0xF2C94C, 0.02f, 0.03f);
      box(0.06f, len, -0.05f, 1.62f, 0.09f, S.gate, 0.04f, 0.035f);
      for (float s = 0.56f; s < len - 0.2f; s += 0.5f) box(s - 0.02f, s + 0.02f, 0.0f, 1.6f, 0.1f, S.gate_dark, 0.0f, 0.0f);
      box(0.08f, len - 0.04f, 1.25f, 1.42f, 0.11f, S.wall_band, 0.02f, 0.0f);
      box(0.08f, len - 0.04f, 0.25f, 0.38f, 0.11f, S.wall_band2, 0.02f, 0.0f);
      const float strap = fminf(0.6f, len - 0.08f);
      box(-0.12f, strap, 0.08f, 0.18f, 0.12f, 0xF2C94C, 0.01f, 0.0f);
      box(-0.12f, strap, 1.48f, 1.58f, 0.12f, 0xF2C94C, 0.01f, 0.0f);
      box(len - 0.05f, len, -0.05f, 1.63f, 0.1f, S.gate_dark, 0.01f, 0.0f);
      break;
    }
    case STYLE_CHICHENITZA: {
      // Palizzata di pali appuntiti legati da due traverse dipinte (rosso e blu maya), con le
      // borchie di giada; il palo del cardine e' un tronco.
      {
        const Part p = part(&mb);
        pipe(&mb, P(0.0f, -0.1f, 0.0f), P(0.0f, 1.66f, 0.0f), 0.12f, 0.12f, 8, S.gate_dark);
        pipe(&mb, P(0.0f, 1.66f, 0.0f), P(0.0f, 1.9f, 0.0f), 0.12f, 0.0f, 8, S.gate_dark);
        outline(&mb, p, 0.03f, S.outline);
      }
      int k = 0;
      for (float s = 0.3f; s < len - 0.1f; s += 0.33f, ++k) {
        const float top = 1.48f + 0.08f * hs(k, gi * 2 + side, 3);
        const uint32_t col = (k & 1) ? S.gate_dark : S.gate;
        box(s - 0.14f, s + 0.14f, -0.1f, top, 0.1f, col, 0.05f, 0.03f);
        tip(s, top, 0.14f, 0.1f, 0.18f, col, 0.03f);
      }
      box(-0.06f, len - 0.02f, 0.32f, 0.46f, 0.14f, S.wall_band2, 0.02f, 0.0f);
      box(-0.06f, len - 0.02f, 1.08f, 1.22f, 0.14f, S.wall_band, 0.02f, 0.0f);
      for (float s = 0.6f; s < len - 0.2f; s += 0.6f) box(s - 0.05f, s + 0.05f, 1.1f, 1.2f, 0.16f, S.deco1, 0.0f, 0.0f);
      break;
    }
    case STYLE_MURAGLIA: {
      // Battente del portone rosso: file di borchie d'oro, il battiporta con l'anello vicino al
      // bordo dove si chiude.
      box(-0.09f, 0.09f, -0.1f, 1.72f, 0.12f, S.gate_dark, 0.03f, 0.035f);
      box(0.05f, len, -0.1f, 1.65f, 0.1f, S.gate, 0.04f, 0.035f);
      for (int row = 0; row < 4; ++row)
        for (float s = 0.32f; s < len - 0.45f; s += 0.32f) {
          const float y = 0.32f + 0.34f * (float)row;
          box(s - 0.045f, s + 0.045f, y - 0.045f, y + 0.045f, 0.13f, S.deco1, 0.0f, 0.0f);
        }
      if (len > 0.7f) {
        box(len - 0.36f, len - 0.16f, 0.95f, 1.15f, 0.12f, S.deco1, 0.01f, 0.0f);
        box(len - 0.35f, len - 0.17f, 0.68f, 0.71f, 0.125f, S.deco1, 0.0f, 0.0f);
        box(len - 0.35f, len - 0.32f, 0.68f, 0.9f, 0.125f, S.deco1, 0.0f, 0.0f);
        box(len - 0.2f, len - 0.17f, 0.68f, 0.9f, 0.125f, S.deco1, 0.0f, 0.0f);
      }
      box(len - 0.035f, len, -0.1f, 1.66f, 0.105f, S.gate_dark, 0.0f, 0.0f);
      break;
    }
    default: {
      // Cancellata di ferro con le punte a lancia, due traverse scure.
      box(-0.08f, 0.08f, 0.0f, 1.72f, 0.08f, S.gate_metal, 0.02f, 0.03f);
      tip(0.0f, 1.72f, 0.08f, 0.08f, 0.2f, 0xF2C94C, 0.03f);
      for (float s = 0.32f; s < len - 0.3f; s += 0.48f) {
        box(s - 0.07f, s + 0.07f, 0.0f, 1.6f, 0.07f, S.gate_metal, 0.02f, 0.03f);
        tip(s, 1.6f, 0.07f, 0.06f, 0.17f, S.gate_metal, 0.03f);
      }
      box(len - 0.14f, len, 0.0f, 1.6f, 0.07f, S.gate_metal, 0.02f, 0.03f);
      tip(len - 0.07f, 1.6f, 0.07f, 0.06f, 0.17f, S.gate_metal, 0.03f);
      box(-0.06f, len, 0.30f, 0.44f, 0.1f, S.gate_dark, 0.02f, 0.03f);
      box(-0.06f, len, 1.12f, 1.26f, 0.1f, S.gate_dark, 0.02f, 0.03f);
      break;
    }
  }
  gt.leaf[side] = mb_finish(&mb);
}

// Parti fisse attorno ai cancelli (obelischi, stipiti, torri): restano anche col cancello aperto.
// Stanno sulle caselle di muro accanto al cancello; niente tetti sopra il passaggio, che
// nasconderebbero il cancello alla telecamera.
void build_gate_frame(MeshBuilder* mb, int gi) {
  const Level* L = B.L;
  const Style& S = *B.S;
  const Gate& gt = L->gates[gi];
  const Vec3 a = cell_center(L, gt.x0, gt.z0), b = cell_center(L, gt.x1, gt.z1);
  const bool horiz = gt.horiz;
  const float u0 = horiz ? a.x - 0.5f : a.z - 0.5f, u1 = horiz ? b.x + 0.5f : b.z + 0.5f;
  const float c = horiz ? a.z : a.x;
  auto P = [&](float u, float y, float w) { return horiz ? v3(u, y, c + w) : v3(c + w, y, u); };
  for (int side = 0; side < 2; ++side) {
    const float u = side ? u1 + 0.42f : u0 - 0.42f;
    const Vec3 base = P(u, 0.0f, 0.0f);
    switch (B.style) {
      case STYLE_PIRAMIDI: {
        // Obelisco di granito rosa sul muro accanto al cancello.
        const Part p = part(mb);
        mb_box(mb, v3(-0.46f, 0.0f, -0.52f), v3(0.46f, WALL_H + 0.25f, 0.52f), S.wall_trim);
        taper_box(mb, v3(0, 0, 0), 0.3f, 0.19f, WALL_H + 0.25f, WALL_H + 2.9f, 0xC98B73);
        pyramid(mb, v3(0, WALL_H + 2.9f, 0), 0.19f, 0.19f, 0.32f, 0xF2C94C, 0xF2C94C, 0.5f, 0, 0.0f);
        place(mb, p, base, 0.0f, 1.0f);
        outline(mb, p, 0.04f, S.outline);
        const Part q = part(mb);
        box5(mb, v3(-0.47f, WALL_H - 0.05f, -0.53f), v3(0.47f, WALL_H + 0.08f, 0.53f), S.wall_band);
        place(mb, q, base, 0.0f, 1.0f);
        break;
      }
      case STYLE_CHICHENITZA: {
        // Pilastro di pietra accanto al passaggio, con il fregio dipinto in alto.
        const Part p = part(mb);
        mb_box(mb, v3(-0.46f, 0.0f, -0.5f), v3(0.46f, 2.05f, 0.5f), S.wall);
        place(mb, p, base, 0.0f, 1.0f);
        outline(mb, p, 0.04f, S.outline);
        const Part q = part(mb);
        box5(mb, v3(-0.49f, 1.45f, -0.53f), v3(0.49f, 1.8f, 0.53f), S.wall_band);
        box5(mb, v3(-0.52f, 2.05f, -0.56f), v3(0.52f, 2.16f, 0.56f), S.wall_trim);
        place(mb, q, base, 0.0f, 1.0f);
        break;
      }
      case STYLE_MURAGLIA: {
        // Torretta accanto al portone, con il tetto di tegole a quattro falde.
        const Part p = part(mb);
        mb_box(mb, v3(-0.62f, 0.0f, -0.62f), v3(0.62f, 2.55f, 0.62f), lighten(S.wall, 0.04f));
        box5(mb, v3(-0.68f, 2.55f, -0.68f), v3(0.68f, 2.68f, 0.68f), S.wall_trim);
        for (int c4 = 0; c4 < 4; ++c4) {
          const float x = (c4 & 1) ? 0.45f : -0.45f, z = (c4 & 2) ? 0.45f : -0.45f;
          mb_box(mb, v3(x - 0.06f, 2.68f, z - 0.06f), v3(x + 0.06f, 3.15f, z + 0.06f), S.gate);
        }
        pyramid(mb, v3(0, 3.15f, 0), 0.95f, 0.95f, 0.75f, S.gate_dark, lighten(S.gate_dark, 0.1f), 0.25f, 0, 0.0f);
        place(mb, p, base, 0.0f, 1.0f);
        outline(mb, p, 0.04f, S.outline);
        break;
      }
      default: {
        obox(mb, base + v3(-0.32f, 0, -0.32f), base + v3(0.32f, 2.2f, 0.32f), 0.04f, S.wall_trim, S.outline, 0.04f);
        break;
      }
    }
  }
  const float um = 0.5f * (u0 + u1), span = u1 - u0;
  if (B.style == STYLE_CHICHENITZA) {
    // Arco maya a mensola (falsa volta): dai due pilastri le pietre sporgono a gradini verso il
    // centro e si chiudono a triangolo con la pietra di chiave. Sottile e sopra il cancello, che
    // sotto si vede.
    const Part p = part(mb);
    const float hw = 0.5f * span, y0 = 2.05f, n = 5.0f, ch = 0.36f;
    for (int k = 0; k < 5; ++k) {
      const float in = hw * (float)(k + 1) / (n + 0.6f), out = hw + 0.9f - 0.42f * (float)k;
      const float ya = y0 + ch * (float)k, yb = ya + ch;
      for (int sd = -1; sd <= 1; sd += 2) {
        const float a = (float)sd * out, b = (float)sd * (hw - in);
        mb_box(mb, v3(fminf(a, b), ya, -0.24f), v3(fmaxf(a, b), yb, 0.24f), (k & 1) ? S.wall : lighten(S.wall, 0.04f));
      }
    }
    const float yk = y0 + ch * 5.0f;
    mb_box(mb, v3(-0.42f * hw - 0.25f, yk - 0.02f, -0.27f), v3(0.42f * hw + 0.25f, yk + 0.3f, 0.27f), S.wall_trim);
    mb_box(mb, v3(-0.42f * hw - 0.15f, yk + 0.06f, -0.29f), v3(0.42f * hw + 0.15f, yk + 0.24f, 0.29f), S.wall_band2);
    place(mb, p, P(um, 0.0f, 0.0f), horiz ? 0.0f : D_HALF_PI, 1.0f);
    outline(mb, p, 0.04f, S.outline);
  } else if (B.style == STYLE_MURAGLIA) {
    // trave rossa con l'insegna tra le due torrette
    const Part p = part(mb);
    mb_box(mb, v3(-0.5f * span - 0.1f, 2.25f, -0.1f), v3(0.5f * span + 0.1f, 2.45f, 0.1f), S.gate);
    mb_box(mb, v3(-0.42f, 2.05f, -0.12f), v3(0.42f, 2.62f, 0.12f), S.gate_dark);
    place(mb, p, P(um, 0.0f, 0.0f), horiz ? 0.0f : D_HALF_PI, 1.0f);
    outline(mb, p, 0.035f, S.outline);
    const Part q = part(mb);
    mb_box(mb, v3(-0.3f, 2.17f, -0.13f), v3(0.3f, 2.5f, 0.13f), S.deco1);
    // due lanterne rosse appese alla trave
    for (int k = 0; k < 2; ++k) {
      const float x = (k ? 1.0f : -1.0f) * (0.5f * span - 0.45f);
      mb_cylinder(mb, v3(x, 1.95f, 0.0f), 0.015f, 0.015f, 0.3f, 3, 0x2A2622);
      mb_ellipsoid(mb, v3(x, 1.82f, 0.0f), v3(0.17f, 0.21f, 0.17f), 6, 4, S.deco0);
      mb_cylinder(mb, v3(x, 1.99f, 0.0f), 0.08f, 0.08f, 0.04f, 6, S.deco1);
      mb_cylinder(mb, v3(x, 1.6f, 0.0f), 0.08f, 0.08f, 0.04f, 6, S.deco1);
    }
    place(mb, q, P(um, 0.0f, 0.0f), horiz ? 0.0f : D_HALF_PI, 1.0f);
  }
}

// --- oggetti mobili nello stile della mappa -------------------------------------------------------

// Ciuffo di fili d'erba (due facce per filo), alti hg, larghi w, attorno all'origine.
void blades(MeshBuilder* mb, int n, float r, float hg, float hv, float w, uint32_t c0, uint32_t c1, int seed) {
  for (int k = 0; k < n; ++k) {
    const float a = 2.4f * (float)k + 0.3f * hs(k, seed, 1), rr = r * (0.25f + 0.75f * hf(k, seed, 2));
    const float x = rr * d_cos(a), z = rr * d_sin(a), h = hg + hv * hf(k, seed, 3);
    const uint32_t c = (k & 1) ? c0 : c1;
    const Vec3 b = v3(x, 0.03f, z), t = v3(x + 0.18f * d_cos(a), h, z + 0.18f * d_sin(a));
    const Vec3 sd = v3(-d_sin(a), 0, d_cos(a)) * w;
    tri_f(mb, b - sd, b + sd, t, v3(d_cos(a), 0.3f, d_sin(a)), c);
    tri_f(mb, b - sd, t, b + sd, v3(-d_cos(a), 0.3f, -d_sin(a)), mix(c, 0x000000, 0.18f));
  }
}

void build_dyn_meshes(Level* L) {
  const Style& S = *B.S;
  for (int d = 0; d < DM_COUNT; ++d) {
    MeshBuilder mb;
    mb_init(&mb);
    switch (d) {
      case DM_BUSH: {
        // cespuglio tondo a tre gobbe (nel deserto grigioverde, coi fiori gialli)
        const bool desert = B.style == STYLE_PIRAMIDI;
        const uint32_t c0 = desert ? 0x8DA25A : (B.style == STYLE_COLOSSEO ? 0x3F7A3A : 0x3E9441);
        const uint32_t c1 = desert ? 0xA5B86C : (B.style == STYLE_COLOSSEO ? 0x4E8C45 : 0x4DA84A);
        Part p = part(&mb);
        mb_ellipsoid(&mb, v3(0, 0.4f, 0), v3(0.48f, 0.4f, 0.45f), 7, 3, c0);
        mb_ellipsoid(&mb, v3(0.24f, 0.35f, 0.2f), v3(0.3f, 0.3f, 0.29f), 5, 3, c1);
        mb_ellipsoid(&mb, v3(-0.24f, 0.37f, -0.14f), v3(0.29f, 0.31f, 0.29f), 5, 3, c1);
        outline(&mb, p, 0.04f, desert ? 0x2E3818 : 0x16301A);
        if (desert || B.style == STYLE_CHICHENITZA) {
          const uint32_t fl = desert ? 0xEBD36A : 0xF2553A;
          for (int k = 0; k < 4; ++k) {
            const float a = 1.6f * (float)k + 0.3f;
            pebble(&mb, v3(0.33f * d_cos(a), 0.56f + 0.06f * (float)(k & 1), 0.31f * d_sin(a)), 0.55f, fl);
          }
        }
        break;
      }
      case DM_GRASS: {
        if (B.style == STYLE_PIRAMIDI) {
          // papiri: fusti lisci e il ciuffo a ombrello in cima
          Part p = part(&mb);
          dome(&mb, v3(0, 0, 0), 0.46f, 0.16f, 0.46f, 7, 1, 0x557F2E);
          outline(&mb, p, 0.03f, 0x24360F);
          for (int k = 0; k < 9; ++k) {
            const float a = 2.4f * (float)k, rr = 0.06f + 0.32f * (float)((k * 7) % 5) / 4.0f;
            const float x = rr * d_cos(a), z = rr * d_sin(a), hg = 1.05f + 0.3f * (float)((k * 3) % 4) / 3.0f;
            const Vec3 b = v3(x, 0.05f, z), t = v3(x + 0.1f * d_cos(a), hg, z + 0.1f * d_sin(a));
            const Vec3 sd = v3(-d_sin(a), 0, d_cos(a)) * 0.028f;
            tri_f(&mb, b - sd, b + sd, t, v3(d_cos(a), 0.1f, d_sin(a)), 0x5E9A33);
            tri_f(&mb, b - sd, t, b + sd, v3(-d_cos(a), 0.1f, -d_sin(a)), 0x4A7E28);
            for (int f = 0; f < 4; ++f) {
              const float fa = a + D_TWO_PI * (float)f / 4.0f + 0.4f;
              const Vec3 e = t + v3(0.2f * d_cos(fa), 0.07f, 0.2f * d_sin(fa));
              const Vec3 sd2 = v3(-d_sin(fa), 0, d_cos(fa)) * 0.07f;
              tri_f(&mb, t, e + sd2, e - sd2, v3(0, 1, 0), (f & 1) ? 0x9ACD52 : 0x86BC44);
            }
          }
        } else if (B.style == STYLE_MURAGLIA) {
          // bambu': canne con i nodi e le foglie in cima
          Part p = part(&mb);
          dome(&mb, v3(0, 0, 0), 0.45f, 0.14f, 0.45f, 7, 1, 0x4E7E30);
          outline(&mb, p, 0.03f, 0x1E3A12);
          for (int k = 0; k < 5; ++k) {
            const float a = 2.5f * (float)k + 0.4f, rr = 0.1f + 0.25f * (float)((k * 5) % 4) / 3.0f;
            const float x = rr * d_cos(a), z = rr * d_sin(a), hg = 1.3f + 0.35f * (float)((k * 3) % 4) / 3.0f;
            pipe(&mb, v3(x, 0.0f, z), v3(x + 0.05f * d_cos(a), hg, z + 0.05f * d_sin(a)), 0.05f, 0.04f, 4, (k & 1) ? 0x7CB84A : 0x6AA63E);
            pipe(&mb, v3(x, 0.55f, z), v3(x, 0.6f, z), 0.06f, 0.06f, 4, 0x4E8A30);
            for (int f = 0; f < 2; ++f) {
              const float fa = a + 2.2f * (float)f + 0.6f;
              frond(&mb, v3(x, hg - 0.08f, z), v3(x + 0.42f * d_cos(fa), hg - 0.3f, z + 0.42f * d_sin(fa)), 0.08f, 0.05f, 0x7AB84E, 0x62A23E);
            }
          }
        } else if (B.style == STYLE_CHICHENITZA) {
          // felci della giungla: fronde lunghe che si aprono dal centro e ricadono
          Part p = part(&mb);
          dome(&mb, v3(0, 0, 0), 0.4f, 0.16f, 0.4f, 7, 1, 0x2E7A32);
          outline(&mb, p, 0.03f, 0x123A16);
          for (int k = 0; k < 9; ++k) {
            const float a = 2.4f * (float)k + 0.3f, l = 0.62f + 0.18f * (float)((k * 5) % 3) / 2.0f;
            const Vec3 base = v3(0.05f * d_cos(a), 0.12f, 0.05f * d_sin(a));
            const Vec3 tip = v3(l * d_cos(a), 0.55f + 0.25f * (float)((k * 3) % 4) / 3.0f, l * d_sin(a));
            frond(&mb, base, tip, 0.12f, 0.32f, (k & 1) ? 0x3E9A3A : 0x4FAE44, (k & 1) ? 0x2E8430 : 0x3E9A3A);
          }
        } else {
          // erba alta dell'arena
          Part p = part(&mb);
          dome(&mb, v3(0, 0, 0), 0.46f, 0.14f, 0.46f, 7, 1, 0x9AA048);
          outline(&mb, p, 0.03f, 0x3A3A14);
          blades(&mb, 14, 0.42f, 0.75f, 0.3f, 0.07f, 0xB9C460, 0xA2AE4E, 7);
        }
        break;
      }
      case DM_CRATE: case DM_CRATE_TAL: {
        const bool tal = d == DM_CRATE_TAL;
        const uint32_t mark = 0x3CE07A;
        if (B.style == STYLE_PIRAMIDI || B.style == STYLE_COLOSSEO) {
          // anfora di terracotta (verde dipinta e con le gemme se c'e' il talismano)
          const uint32_t clay = S.deco0, dark = mix(S.deco0, 0x000000, 0.25f);
          Part p = part(&mb);
          mb_ellipsoid(&mb, v3(0, 0.44f, 0), v3(0.37f, 0.44f, 0.37f), 7, 4, clay);
          pipe(&mb, v3(0, 0.0f, 0), v3(0, 0.1f, 0), 0.16f, 0.2f, 7, dark);
          pipe(&mb, v3(0, 0.8f, 0), v3(0, 0.95f, 0), 0.15f, 0.12f, 7, clay);
          mb_cylinder(&mb, v3(0, 0.93f, 0), 0.2f, 0.2f, 0.07f, 7, lighten(clay, 0.1f));
          outline(&mb, p, 0.035f, 0x3A1E10);
          pipe(&mb, v3(0.13f, 0.88f, 0), v3(0.3f, 0.62f, 0), 0.04f, 0.04f, 4, dark);
          pipe(&mb, v3(-0.13f, 0.88f, 0), v3(-0.3f, 0.62f, 0), 0.04f, 0.04f, 4, dark);
          pipe(&mb, v3(0, 0.47f, 0), v3(0, 0.6f, 0), 0.382f, 0.37f, 7, tal ? mark : S.wall_band);
          if (tal) {
            unlit(&mb, true);
            for (int k = 0; k < 4; ++k) {
              const float a = D_HALF_PI * (float)k + 0.4f;
              const Vec3 c = v3(0.37f * d_cos(a), 0.33f, 0.37f * d_sin(a)), o = v3(d_cos(a), 0, d_sin(a));
              const Vec3 t = v3(-d_sin(a), 0, d_cos(a));
              quad_f(&mb, c + o * 0.02f + v3(0, 0.07f, 0), c + o * 0.02f + t * 0.06f, c + o * 0.02f + v3(0, -0.07f, 0), c + o * 0.02f - t * 0.06f, o, mark);
            }
            mb_disc(&mb, v3(0, 1.005f, 0), 0.13f, 7, mark);
            unlit(&mb, false);
          }
        } else if (B.style == STYLE_CHICHENITZA) {
          // vaso maya di terracotta dipinta: pancia larga, collo basso, fasce crema e nere (verde di
          // giada col talismano)
          const uint32_t clay = S.deco0, dark = mix(S.deco0, 0x000000, 0.3f);
          Part p = part(&mb);
          mb_ellipsoid(&mb, v3(0, 0.4f, 0), v3(0.42f, 0.4f, 0.42f), 8, 4, clay);
          pipe(&mb, v3(0, 0.0f, 0), v3(0, 0.08f, 0), 0.2f, 0.26f, 8, dark);
          pipe(&mb, v3(0, 0.74f, 0), v3(0, 0.9f, 0), 0.26f, 0.3f, 8, clay);
          outline(&mb, p, 0.035f, 0x3A1E10);
          pipe(&mb, v3(0, 0.46f, 0), v3(0, 0.6f, 0), 0.43f, 0.405f, 8, tal ? mark : 0xEAD9B0);
          pipe(&mb, v3(0, 0.6f, 0), v3(0, 0.65f, 0), 0.405f, 0.385f, 8, 0x2A1E16);
          pipe(&mb, v3(0, 0.86f, 0), v3(0, 0.91f, 0), 0.305f, 0.31f, 8, 0x2A1E16);
          if (tal) {
            unlit(&mb, true);
            mb_disc(&mb, v3(0, 0.88f, 0), 0.2f, 7, mark);
            unlit(&mb, false);
          }
        } else {
          // cassa di legno laccata di rosso, angoli scuri e una placca (verde di giada col talismano)
          Part p = part(&mb);
          mb_bevel_box(&mb, v3(-0.45f, 0.0f, -0.45f), v3(0.45f, 0.92f, 0.45f), 0.05f, 0xB8432F);
          outline(&mb, p, 0.035f, 0x2A0E08);
          for (int k = 0; k < 2; ++k) {
            const float y = k ? 0.72f : 0.12f;
            mb_box(&mb, v3(-0.465f, y, -0.465f), v3(0.465f, y + 0.08f, 0.465f), 0x3A2A22);
          }
          box5(&mb, v3(-0.13f, 0.34f, 0.455f), v3(0.13f, 0.58f, 0.478f), tal ? mark : 0xE8B53A);
          box5(&mb, v3(-0.13f, 0.34f, -0.478f), v3(0.13f, 0.58f, -0.455f), tal ? mark : 0xE8B53A);
          if (tal) {
            unlit(&mb, true);
            quad_f(&mb, v3(0, 0.925f, -0.17f), v3(-0.14f, 0.925f, 0.0f), v3(0, 0.925f, 0.17f), v3(0.14f, 0.925f, 0.0f), v3(0, 1, 0), mark);
            unlit(&mb, false);
          }
        }
        break;
      }
      case DM_TRAMP: {
        // trampolino: un tamburo (pelle tesa, fascia dipinta, pioli)
        const uint32_t skin = B.style == STYLE_CHICHENITZA ? 0xE8D9A8 : 0xF2E6C8;
        const uint32_t body = B.style == STYLE_MURAGLIA ? 0xC0392B : (B.style == STYLE_CHICHENITZA ? S.wall_band2 : S.deco0);
        Part p = part(&mb);
        mb_cylinder(&mb, v3(0, 0, 0), 0.44f, 0.46f, 0.22f, 10, body);
        outline(&mb, p, 0.035f, 0x3A1E10);
        mb_cylinder(&mb, v3(0, 0.215f, 0), 0.475f, 0.475f, 0.05f, 10, skin);
        for (int k = 0; k < 5; ++k) {
          const float a = D_TWO_PI * (float)k / 5.0f + 0.3f;
          box5(&mb, v3(0.455f * d_cos(a) - 0.035f, 0.03f, 0.455f * d_sin(a) - 0.035f), v3(0.455f * d_cos(a) + 0.035f, 0.2f, 0.455f * d_sin(a) + 0.035f), S.deco1);
        }
        unlit(&mb, true);
        mb_ring(&mb, v3(0, 0.27f, 0), 0.17f, 0.27f, 10, S.deco1);
        unlit(&mb, false);
        break;
      }
      default: break;
    }
    L->dyn_mesh[d] = mb_finish(&mb);
  }
}

// --- paesaggio lontano ---------------------------------------------------------------------------

// Terreno oltre la griglia estesa: quattro rettangoli grandi alla quota del bordo, senza
// sovrapposizioni, fin dove arrivano le telecamere dei menu (120 m); sotto tutto un piano dello
// stesso colore del terreno, che copre le eventuali fessure di un pixel tra i pezzi del terreno
// (con il cenote sta sotto la sua acqua).
void build_far_ground(Level* L) {
  const float x0 = B.ox, x1 = (float)B.ew + B.ox, z0 = B.oz, z1 = (float)B.eh + B.oz;
  const float F = 130.0f;
  const uint32_t c = B.krgb[keyat(0, 0)];
  const float rects[4][4] = { { -F, -F, F, z0 }, { -F, z1, F, F }, { -F, z0, x0, z1 }, { x1, z0, F, z1 } };
  for (int r = 0; r < 4; ++r) {
    if (L->nscenery >= LV_MAX_SCENERY) return;
    MeshBuilder mb;
    mb_init(&mb);
    quad_up(&mb, rects[r][0], rects[r][1], rects[r][2], rects[r][3], B.far_y, c);
    L->scenery[L->nscenery++] = mb_finish(&mb);
  }
  if (L->nscenery >= LV_MAX_SCENERY) return;
  MeshBuilder mb;
  mb_init(&mb);
  quad_up(&mb, x0, z0, x1, z1, B.pits ? PIT_WATER_Y - 0.6f : B.far_y - 0.45f, B.krgb[TK_FLOOR * 4 + 1]);
  L->scenery[L->nscenery++] = mb_finish(&mb);
}

}  // namespace

void level_build_mesh(Level* L) {
  for (int i = 0; i < L->nchunks; ++i) mesh_free(&L->chunk[i]);
  L->nchunks = 0;
  for (int i = 0; i < L->nscenery; ++i) mesh_free(&L->scenery[i]);
  L->nscenery = 0;
  for (int i = 0; i < DM_COUNT; ++i) mesh_free(&L->dyn_mesh[i]);
  for (int i = 0; i < L->ngates; ++i) {
    mesh_free(&L->gates[i].leaf[0]);
    mesh_free(&L->gates[i].leaf[1]);
  }

  B.L = L;
  B.style = L->style >= 0 && L->style < NSTYLES ? L->style : STYLE_PIRAMIDI;
  B.S = &STYLES[B.style];
  B.MW = LV_MARGIN_X;
  B.MN = LV_MARGIN_N;
  B.ew = L->W + 2 * LV_MARGIN_X;
  B.eh = L->H + LV_MARGIN_N + LV_MARGIN_S;
  B.ox = -(float)B.MW - (float)L->W * 0.5f;
  B.oz = -(float)B.MN - (float)L->H;
  B.far_y = 0.0f;
  B.pits = false;
  for (int cz = 0; cz < L->H; ++cz)
    for (int cx = 0; cx < L->W; ++cx) B.pits |= L->cell[cz][cx] == C_PIT;
  setup_keys();
  compute_keys();
  g_nst = 0;

  g_ncx = (B.ew + LV_CHUNK - 1) / LV_CHUNK;
  g_ncz = (B.eh + LV_CHUNK - 1) / LV_CHUNK;
  for (int k = 0; k < g_ncx * g_ncz && k < LV_MAX_CHUNKS; ++k) mb_init(&g_cmb[k]);
  for (int k = 0; k < TILE_N * TILE_N; ++k) mb_init(&g_tile[k]);
  for (int kz = 0; kz < g_ncz; ++kz)
    for (int kx = 0; kx < g_ncx; ++kx) {
      if (kz * g_ncx + kx >= LV_MAX_CHUNKS) break;
      MeshBuilder& mb = g_cmb[kz * g_ncx + kx];
      const int i0 = kx * LV_CHUNK, j0 = kz * LV_CHUNK;
      const int i1 = i0 + LV_CHUNK < B.ew ? i0 + LV_CHUNK : B.ew, j1 = j0 + LV_CHUNK < B.eh ? j0 + LV_CHUNK : B.eh;
      // spigoli: l'ultimo blocco di ogni fila prende anche lo spigolo del bordo esterno
      build_ground(&mb, i0, j0, i1 == B.ew ? B.ew + 1 : i1, j1 == B.eh ? B.eh + 1 : j1);
      build_cells(&mb, i0 - B.MW, j0 - B.MN, i1 - B.MW, j1 - B.MN);
      build_blocks_in(&mb, i0 - B.MW, j0 - B.MN, i1 - B.MW, j1 - B.MN);
      build_margin(&mb, i0, j0, i1, j1);
      water_glints(&mb, i0, j0, i1, j1);
      for (int gi = 0; gi < L->ngates; ++gi) {
        const Gate& gt = L->gates[gi];
        const int gx = (gt.x0 + gt.x1) / 2 + B.MW, gz = (gt.z0 + gt.z1) / 2 + B.MN;
        if (gx >= i0 && gx < i1 && gz >= j0 && gz < j1) build_gate_frame(&mb, gi);
      }
    }
  build_landscape(L);
  for (int k = 0; k < g_ncx * g_ncz && k < LV_MAX_CHUNKS; ++k) {
    MeshBuilder& mb = g_cmb[k];
    if (mb.nt == 0) {
      free(mb.v);
      free(mb.t);
      mb_init(&mb);
      continue;
    }
    L->chunk[L->nchunks++] = mb_finish(&mb);
  }
  build_far_ground(L);
  build_dyn_meshes(L);
  for (int gi = 0; gi < L->ngates; ++gi) {
    build_gate_leaf(L, gi, 0);
    build_gate_leaf(L, gi, 1);
  }
}
