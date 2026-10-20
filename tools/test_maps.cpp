// Prove delle mappe (0.5.1): niente fughe dalle zone. Andrea nelle Piramidi saltava nel Nilo e da
// li' le spinte lo facevano uscire dall'altra parte dei muri, fino alle zone dopo senza aprire i
// cancelli. Qui:
//   1) controlli fissi delle mappe di serie (bordo, fuori, zone che si toccano, caselle fuori dalle
//      zone, tutto raggiungibile a piedi) rifatti a parte, e le mappe sbagliate che il gioco deve
//      rifiutare con il loro messaggio;
//   2) giocatori a caso: in ogni zona di ogni mappa 4 giocatori invulnerabili, senza mostri, vanno
//      di qua e di la' con salti corti e lunghi, contro muri, acqua, cancelli, blocchi, piattaforme,
//      muretti, trampolini e cenote, con qualche spinta forte come quella delle bombe; a ogni
//      fotogramma ognuno deve stare nella sua zona (o nel passaggio del cancello aperto) e non
//      toccare mai una casella che lo ferma; chi cade nel cenote si conta a parte;
//   3) casi mirati: salti da piattaforme e blocchi contro i muri delle zone, contro i cancelli
//      chiusi, verso l'acqua; il cenote (ci si cade, ci si salta sopra, i mostri spinti dentro, chi
//      rientra non riappare sul pozzo); casse che tornano su chi ci sta sopra.
// Numeri a caso da una funzione hash della prova: mai il generatore del gioco.
//   make build/native/test_maps && ./build/native/test_maps [FOTOGRAMMI_PER_ZONA [SEME]]
#include "game_int.h"
#include "maps.h"
#include "plat.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
static Pad g_pads[MAX_PLAYERS];
static char g_base[64] = "/tmp/pupazzi_test_maps_XXXXXX";
static uint32_t g_seed = 0;          // secondo argomento: altre partite a caso

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static uint32_t hmix(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}
static float hfloat(uint32_t a, uint32_t b, uint32_t c) { return (float)(hmix(a, b, c) >> 8) * (1.0f / 16777216.0f); }

// Niente mostri, niente ondate, niente boss: la zona resta da ripulire finche' non lo decide la prova.
static void quiet(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  monsters_clear();
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
  g.boss.alive = false;
}

static void clear_zone(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.wave = g.L.zones[g.zone].nwaves - 1;
  g.spawn_wait = 0;
  monsters_clear();
  g.boss.alive = false;
  for (int k = 0; k < 10 && g.phase != PH_CLEAR && g.phase != PH_MAP_DONE; ++k) game_update(g_pads);
}

static int cell_type(const Level* L, int cx, int cz) { return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? (int)C_VOID : (int)L->cell[cz][cx]; }
static int cell_zone(const Level* L, int cx, int cz) { return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? -1 : L->zone[cz][cx]; }

// --- 1) controlli fissi ------------------------------------------------------------------------

// Rifatti qui senza level.cpp: il bordo e' muro o fuori, il fuori e' chiuso dai muri, le zone si
// toccano (anche in diagonale) solo con un cancello in mezzo, ogni casella che non e' muro, fuori,
// albero, masso o cancello sta in una zona; dentro ogni zona si arriva a piedi dappertutto.
static bool static_ok(const Level* L, char* why, int n) {
  for (int cz = 0; cz < L->H; ++cz)
    for (int cx = 0; cx < L->W; ++cx) {
      const int c = L->cell[cz][cx];
      const bool edge = cx == 0 || cz == 0 || cx == L->W - 1 || cz == L->H - 1;
      if (edge && c != C_WALL && c != C_VOID) { snprintf(why, n, "casella %d,%d sul bordo non e' muro", cx, cz); return false; }
      if (c == C_WALL || c == C_VOID) continue;
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          const int o = cell_type(L, cx + dx, cz + dz);
          if (o == C_VOID) { snprintf(why, n, "casella %d,%d accanto al fuori", cx, cz); return false; }
          const int za = L->zone[cz][cx], zb = cell_zone(L, cx + dx, cz + dz);
          if (za >= 0 && zb >= 0 && za != zb) { snprintf(why, n, "zone %d e %d si toccano in %d,%d", za + 1, zb + 1, cx, cz); return false; }
        }
      if (c != C_TREE && c != C_ROCK && c != C_GATE && L->zone[cz][cx] < 0) { snprintf(why, n, "casella %d,%d fuori dalle zone", cx, cz); return false; }
    }
  static uint8_t seen[LV_MAX_H][LV_MAX_W];
  static int16_t q[LV_MAX_W * LV_MAX_H][2];
  for (int k = 0; k < L->nzones; ++k) {
    memset(seen, 0, sizeof(seen));
    int qn = 0, qh = 0;
    int sx, sz;
    level_cell(L, L->zones[k].start[0].x, L->zones[k].start[0].z, &sx, &sz);
    q[qn][0] = (int16_t)sx;
    q[qn][1] = (int16_t)sz;
    qn++;
    seen[sz][sx] = 1;
    while (qh < qn) {
      const int x = q[qh][0], z = q[qh][1];
      qh++;
      static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
      for (int d = 0; d < 4; ++d) {
        const int nx = x + D[d][0], nz = z + D[d][1];
        if (cell_zone(L, nx, nz) != k || seen[nz][nx]) continue;
        const int c = L->cell[nz][nx];
        if (c == C_WATER || c == C_PIT) continue;
        seen[nz][nx] = 1;
        q[qn][0] = (int16_t)nx;
        q[qn][1] = (int16_t)nz;
        qn++;
      }
    }
    for (int cz = 0; cz < L->H; ++cz)
      for (int cx = 0; cx < L->W; ++cx) {
        const int c = L->cell[cz][cx];
        if (L->zone[cz][cx] == k && c != C_WATER && c != C_PIT && !seen[cz][cx]) {
          snprintf(why, n, "zona %d: casella %d,%d non si raggiunge a piedi", k + 1, cx, cz);
          return false;
        }
      }
  }
  return true;
}

static Level g_lv;

static void parse_rejects(const char* name, const char* grid, const char* want) {
  char text[4096], err[200];
  snprintf(text, sizeof(text), "nome: prova\nmondo: terra\nzona 1: scorpioni 1\nzona 2: scorpioni 1\n---\n%s", grid);
  bool ok = level_parse(&g_lv, text, err, sizeof(err));
  if (ok) {
    // con una zona sola la riga "zona 2" e' di troppo: si riprova senza
    snprintf(text, sizeof(text), "nome: prova\nmondo: terra\nzona 1: scorpioni 1\n---\n%s", grid);
    ok = level_parse(&g_lv, text, err, sizeof(err));
  } else if (strstr(err, "zona 2\" ma la griglia")) {
    snprintf(text, sizeof(text), "nome: prova\nmondo: terra\nzona 1: scorpioni 1\n---\n%s", grid);
    ok = level_parse(&g_lv, text, err, sizeof(err));
  }
  char buf[320];
  snprintf(buf, sizeof(buf), "rifiutata: %s (\"%s\")", name, ok ? "accettata!" : err);
  check(!ok && strstr(err, want) != NULL, buf);
}

static void test_static(void) {
  printf("1) mappe di serie e mappe sbagliate\n");
  char buf[256], why[160];
  for (int i = 0; i < MAP_TEXT_COUNT; ++i) {
    char err[200];
    const bool ok = level_parse(&g_lv, MAP_TEXTS[i].text, err, sizeof(err));
    snprintf(buf, sizeof(buf), "%s: si carica (%s)", MAP_TEXTS[i].id, ok ? g_lv.name : err);
    check(ok, buf);
    if (!ok) continue;
    const bool s = static_ok(&g_lv, why, sizeof(why));
    snprintf(buf, sizeof(buf), "%s: bordo e fuori chiusi, zone separate, tutto nelle zone e raggiungibile a piedi%s%s", MAP_TEXTS[i].id,
             s ? "" : ": ", s ? "" : why);
    check(s, buf);
    // Zone grandi come quelle di serie (31 x 21) e niente acqua o cenote sul bordo delle zone fuori dai muri.
    if (g_lv.boss == BOSS_NONE) {
      bool sizes = true;
      for (int z = 0; z < g_lv.nzones; ++z) {
        const ZoneDef& zd = g_lv.zones[z];
        if (zd.x1 - zd.x0 + 1 != 31 || zd.z1 - zd.z0 + 1 != 21) sizes = false;
      }
      snprintf(buf, sizeof(buf), "%s: %d zone da 31 x 21", MAP_TEXTS[i].id, g_lv.nzones);
      check(sizes && g_lv.nzones == 3, buf);
    }
  }
  // Mappe che il gioco deve rifiutare (con il loro messaggio nel menu delle mappe).
  parse_rejects("acqua sul bordo (il Nilo della 0.5)",
                "#########\n#......~~\n#......~~\n#......~~\n#......~~\n#......~~\n#..,12,~~\n#########\n", "sul bordo");
  parse_rejects("terreno contro il fuori", "########\n#......#\n#......#\n#......\n#......#\n#......#\n#..12..#\n########\n", "manca il muro");
  parse_rejects("zone che si toccano in diagonale",
                "##########\n#........#\n#.....####\n#GG###...#\n#........#\n#..,12,..#\n#..,34,..#\n##########\n", "(serve G)");
  parse_rejects("terreno chiuso fuori dalle zone",
                "##########\n#........#\n#..####..#\n#..#..#..#\n#..####..#\n#........#\n#..,12,..#\n##########\n", "fuori dalle zone");
  parse_rejects("zona tagliata dall'acqua",
                "##########\n#........#\n#........#\n#~~~~~~~~#\n#........#\n#........#\n#..,12,..#\n##########\n", "a piedi");
  parse_rejects("zona tagliata dal cenote",
                "##########\n#........#\n#........#\n#OOOOOOOO#\n#........#\n#........#\n#..,12,..#\n##########\n", "a piedi");
  parse_rejects("cenote accanto alla partenza",
                "##########\n#........#\n#........#\n#........#\n#..O.....#\n#........#\n#..,12,..#\n##########\n", "cenote");
  parse_rejects("cenote accanto a un cancello",
                "##########\n#........#\n#........#\n####GG####\n#...O....#\n#........#\n#........#\n#..,12,..#\n##########\n", "cenote");
  parse_rejects("cenote accanto alla capsula", "##########\n#........#\n#..Op....#\n#........#\n#........#\n#........#\n#..,12,..#\n##########\n", "capsula");
  parse_rejects("muretto senza un lato libero", "########\n#......#\n#.===..#\n#.===..#\n#.===..#\n#......#\n#.12...#\n########\n", "muretto");
  parse_rejects("capsula chiusa dai blocchi", "########\n#......#\n#.ccc..#\n#.cpc..#\n#.ccc..#\n#......#\n#.12...#\n########\n", "capsula");
  parse_rejects("cancello che da' solo sull'acqua",
                "##########\n#........#\n#........#\n#~~~~~~~~#\n####GG####\n#........#\n#..,12,..#\n##########\n", "a piedi");
  {
    // e una mappa giusta con un angolo fuori (spazi) chiuso dai muri si carica
    char err[200];
    const bool ok = level_parse(&g_lv,
                                "nome: prova\nmondo: terra\nzona 1: scorpioni 1\nzona 2: scorpioni 1\n---\n"
                                "    ######\n    #....#\n    #....#\n#####GG###\n#........#\n#........#\n#..,12,..#\n##########\n",
                                err, sizeof(err));
    snprintf(buf, sizeof(buf), "accettata: mappa con un angolo fuori chiuso dai muri (%s)", ok ? "ok" : err);
    check(ok, buf);
  }
}

// --- 2) giocatori a caso ---------------------------------------------------------------------------

struct Walker {
  float tx, tz;       // dove va
  int t;              // fotogrammi con questo obiettivo
  int mode;           // salti: 0 niente, 1 colpetti, 2 tenuti, 3 a caso, 4 continui
  int idle;           // fermo per un po'
};

struct Fuzz {
  int map;
  long frames, overlaps, leaks, jumps_zone, falls, falls_bad, respawn_bad, wipes;
  char first[200];
};

// La casella e' un ostacolo "duro" per chi cammina (qualunque sia la quota dei piedi)?
static bool hard_block(const Level* L, int cx, int cz) {
  const int c = cell_type(L, cx, cz);
  if (c == C_VOID || c == C_WALL || c == C_TREE || c == C_ROCK || c == C_WATER) return true;
  if (c == C_GATE) {
    const int gi = L->gate_at[cz][cx];
    return gi < 0 || L->gates[gi].open <= 0.0f;     // chiuso del tutto (mentre si muove le spinte lo risolvono)
  }
  return false;
}

// Il cerchio del giocatore tocca qualcosa che non dovrebbe? feet: quota dei piedi con cui si e' mosso.
static bool overlaps(const Level* L, float x, float z, float feet, char* what, int n) {
  const float r = PL_R - 0.004f;
  const float fx = x + (float)L->W * 0.5f, fz = z + (float)L->H;
  for (int cz = (int)floorf(fz - r); cz <= (int)floorf(fz + r); ++cz)
    for (int cx = (int)floorf(fx - r); cx <= (int)floorf(fx + r); ++cx) {
      const float qx = clampf(fx, (float)cx, (float)cx + 1.0f), qz = clampf(fz, (float)cz, (float)cz + 1.0f);
      if ((fx - qx) * (fx - qx) + (fz - qz) * (fz - qz) >= r * r) continue;
      bool bad = hard_block(L, cx, cz);
      if (!bad && cell_type(L, cx, cz) != C_GATE && cell_type(L, cx, cz) != C_PIT) bad = level_walk_top(L, cx, cz) > feet + PL_STEP + 0.001f;
      if (bad) {
        snprintf(what, n, "casella %d,%d (tipo %d) a %.2f,%.2f piedi %.2f", cx, cz, cell_type(L, cx, cz), x, z, feet);
        return true;
      }
    }
  return false;
}

// Obiettivo interessante nella zona z (o vicino): muri del bordo della zona, acqua, cancelli,
// blocchi, piattaforme, muretti, trampolini, cenote, terreno qualsiasi.
static void pick_target(const Level* L, int z, uint32_t seed, float* tx, float* tz) {
  const int want = (int)(hmix(seed, 1, 2) % 9u);
  const ZoneDef& zd = L->zones[z];
  for (int tries = 0; tries < 400; ++tries) {
    const int cx = zd.x0 - 1 + (int)(hmix(seed, tries, 3) % (uint32_t)(zd.x1 - zd.x0 + 3));
    const int cz = zd.z0 - 1 + (int)(hmix(seed, tries, 4) % (uint32_t)(zd.z1 - zd.z0 + 3));
    const int c = cell_type(L, cx, cz);
    bool near = cell_zone(L, cx, cz) == z;
    for (int d = 0; d < 4 && !near; ++d) {
      static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
      near = cell_zone(L, cx + D[d][0], cz + D[d][1]) == z;
    }
    if (!near) continue;
    bool ok = false;
    switch (want) {
      case 0: ok = c == C_WALL; break;
      case 1: ok = c == C_WATER; break;
      case 2: ok = c == C_GATE; break;
      case 3: ok = c == C_CRATE || c == C_BREAK; break;
      case 4: ok = c == C_PLATFORM; break;
      case 5: ok = c == C_LOW; break;
      case 6: ok = c == C_TRAMP; break;
      case 7: ok = c == C_PIT || c == C_TREE || c == C_ROCK; break;
      default: ok = true; break;
    }
    if (!ok && tries < 300) continue;
    const Vec3 cc = cell_center(L, cx, cz);
    *tx = cc.x + 0.8f * (hfloat(seed, tries, 5) - 0.5f);
    *tz = cc.z + 0.8f * (hfloat(seed, tries, 6) - 0.5f);
    return;
  }
  const Vec3 cc = zd.start[0];
  *tx = cc.x;
  *tz = cc.z;
}

static void walker_pads(Walker* w, uint32_t seed, int frame, int target_zone, bool to_gate) {
  memset(g_pads, 0, sizeof(g_pads));
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    Walker& k = w[i];
    if (--k.t <= 0) {
      const uint32_t s = hmix(seed, (uint32_t)frame, (uint32_t)i);
      k.t = 40 + (int)(s % 200u);
      k.mode = (int)((s >> 9) % 5u);
      k.idle = (s >> 14) % 9u == 0 ? 10 + (int)((s >> 18) % 30u) : 0;
      if (to_gate && (s >> 20) % 3u != 0) {
        const ZoneDef& zd = g.L.zones[target_zone];
        k.tx = zd.start[i].x;
        k.tz = zd.start[i].z - 3.0f;
      } else {
        pick_target(&g.L, target_zone, s, &k.tx, &k.tz);
      }
    }
    if (!p.active || !p.alive) continue;
    if (k.idle > 0) { k.idle--; continue; }
    float dx = k.tx - p.pos.x, dz = k.tz - p.pos.z;
    const float l = sqrtf(dx * dx + dz * dz);
    if (l > 1e-3f) { dx /= l; dz /= l; }
    // un po' di zigzag e ogni tanto di traverso, cosi' si struscia contro gli spigoli
    const float wob = 0.6f * (hfloat(seed, (uint32_t)(frame / 7), (uint32_t)i + 50u) - 0.5f);
    g_pads[i].lx = clampf(dx - dz * wob, -1.0f, 1.0f);
    g_pads[i].ly = clampf(dz + dx * wob, -1.0f, 1.0f);
    bool jump = false;
    switch (k.mode) {
      case 1: jump = (frame + i * 7) % 23 < 2; break;                 // colpetti
      case 2: jump = (frame + i * 11) % 47 < 30; break;               // tenuti (in alto)
      case 3: jump = hfloat(seed, (uint32_t)frame, (uint32_t)i + 99u) < 0.3f; break;
      case 4: jump = (frame & 1) != 0 || (frame % 61) < 40; break;    // quasi sempre
      default: break;
    }
    if (jump) g_pads[i].btn |= (uint16_t)(1u << PAD_R2);
    // ogni tanto una spinta forte come quella di una bomba (verso muri, acqua, cancelli, cenote)
    if (hmix(seed, (uint32_t)frame, (uint32_t)i + 200u) % 97u == 0u) {
      const float a = D_TWO_PI * hfloat(seed, (uint32_t)frame, (uint32_t)i + 300u), k = 3.0f + 7.0f * hfloat(seed, (uint32_t)frame, (uint32_t)i + 400u);
      push_player(i, d_cos(a) * k, d_sin(a) * k, 0);
    }
  }
}

// Un passo della partita con i controlli di ogni giocatore.
static void fuzz_step(Fuzz& F, const Walker* w, Vec3* prev, float* prev_y, bool* prev_alive) {
  (void)w;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    prev[i] = g.pl[i].pos;
    prev_y[i] = g.pl[i].pos.y;
    prev_alive[i] = g.pl[i].alive;
  }
  const int zone0 = g.zone, phase0 = g.phase;
  game_update(g_pads);
  F.frames++;
  if (g.phase == PH_WIPE && phase0 != PH_WIPE) F.wipes++;
  if (getenv("TEST_MAPS_DEBUG") && (g.zone != zone0 || g.phase != phase0))
    printf("    fotogramma %ld: zona %d fase %d -> zona %d fase %d\n", F.frames, zone0 + 1, phase0, g.zone + 1, g.phase);
  // niente mostri, vite che non finiscono, invulnerabili
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  g.boss.alive = false;
  monsters_clear();
  const Level* L = &g.L;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active) continue;
    if (p.alive) {
      p.invuln = 1000;
      p.hp = p.hp_max;
    }
    if (p.lives < 2) p.lives = 3;
    char what[200];
    const float moved = dist2d(p.pos, prev[i]);
    const bool teleport = moved > 0.5f;
    if (prev_alive[i] && !p.alive) {
      // invulnerabili: si va a terra solo cadendo nel cenote, con il centro sopra il pozzo
      F.falls++;
      if (!level_pit_at(L, p.pos.x, p.pos.z) || teleport) {
        F.falls_bad++;
        if (!F.first[0]) snprintf(F.first, sizeof(F.first), "G%d a terra fuori dal cenote a %.2f,%.2f", i + 1, p.pos.x, p.pos.z);
      }
      continue;
    }
    if (!p.alive) continue;
    if (!prev_alive[i] && p.alive && level_pit_near(L, p.pos.x, p.pos.z, PL_R + 0.5f)) {
      F.respawn_bad++;
      if (!F.first[0]) snprintf(F.first, sizeof(F.first), "G%d rientra accanto al cenote a %.2f,%.2f", i + 1, p.pos.x, p.pos.z);
    }
    if (p.leap_n > 0) continue;
    // nella zona giusta (o nel passaggio di un cancello aperto)
    int cx, cz;
    const bool in = level_cell(L, p.pos.x, p.pos.z, &cx, &cz);
    const int z = in ? L->zone[cz][cx] : -1;
    bool zone_ok = z == g.zone || (g.phase == PH_ENTER && z == g.zone - 1) || (g.phase == PH_CLEAR && z == g.zone + 1);
    if (in && z < 0 && L->cell[cz][cx] == C_GATE) zone_ok = L->gates[L->gate_at[cz][cx]].open > 0.0f;
    if (!zone_ok) {
      F.leaks++;
      if (!F.first[0]) {
        snprintf(F.first, sizeof(F.first), "G%d fuori zona: casella %d,%d zona %d (in corso %d, fase %d, fotogramma %ld) a %.2f,%.2f", i + 1, cx, cz, z + 1,
                 g.zone + 1, g.phase, F.frames, p.pos.x, p.pos.z);
        if (getenv("TEST_MAPS_DEBUG"))
          for (int j = 0; j < MAX_PLAYERS; ++j)
            printf("    G%d vivo %d vite %d a %.2f,%.2f zona %d (prima %.2f,%.2f)\n", j + 1, g.pl[j].alive, g.pl[j].lives, g.pl[j].pos.x, g.pl[j].pos.z,
                   level_zone_at(L, g.pl[j].pos.x, g.pl[j].pos.z) + 1, prev[j].x, prev[j].z);
      }
    }
    // da una zona all'altra solo passando dal cancello (che non ha zona)
    if (!teleport) {
      const int z0 = level_zone_at(L, prev[i].x, prev[i].z);
      if (z0 >= 0 && z >= 0 && z0 != z) {
        F.jumps_zone++;
        if (!F.first[0]) snprintf(F.first, sizeof(F.first), "G%d passa dalla zona %d alla %d senza cancello a %.2f,%.2f", i + 1, z0 + 1, z + 1, p.pos.x, p.pos.z);
      }
    }
    if (!teleport && overlaps(L, p.pos.x, p.pos.z, prev_y[i], what, sizeof(what))) {
      F.overlaps++;
      if (!F.first[0]) snprintf(F.first, sizeof(F.first), "G%d dentro un ostacolo: %.150s", i + 1, what);
    }
  }
}

static void fuzz_map(int map, int per_zone, Fuzz& F) {
  memset(&F, 0, sizeof(F));
  F.map = map;
  game_init();
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(map, 4, "0123");
  game_update(g_pads);
  quiet();
  Walker w[MAX_PLAYERS];
  memset(w, 0, sizeof(w));
  Vec3 prev[MAX_PLAYERS];
  float prev_y[MAX_PLAYERS];
  bool prev_alive[MAX_PLAYERS];
  const uint32_t seed = 0xC0FFEEu + (uint32_t)map * 7919u + g_seed * 104729u;
  int frame = 0;
  for (int z = 0; z < g.L.nzones; ++z) {
    // la zona con i cancelli chiusi
    for (int f = 0; f < per_zone; ++f, ++frame) {
      walker_pads(w, seed, frame, g.zone, false);
      fuzz_step(F, w, prev, prev_y, prev_alive);
    }
    // (se tutti sono caduti insieme la mappa e' ricominciata: si va avanti dalla zona del gioco)
    if (g.zone + 1 >= g.L.nzones) continue;
    const int next = g.zone + 1;
    // ripulita: avanti attraverso il cancello (con chi resta indietro a saltare in giro)
    clear_zone();
    for (int i = 0; i < MAX_PLAYERS; ++i) w[i].t = 0;
    int f = 0;
    for (; f < 4000 && !(g.zone == next && g.phase == PH_FIGHT); ++f, ++frame) {
      const int tz = g.phase == PH_CLEAR ? g.zone + 1 : g.zone;
      walker_pads(w, seed, frame, tz, true);
      fuzz_step(F, w, prev, prev_y, prev_alive);
    }
    if (!(g.zone == next && g.phase == PH_FIGHT) && g.phase == PH_CLEAR && g.zone + 1 == next) {
      // non ci sono arrivati da soli (il guinzaglio, un ostacolo): li porta la prova
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active && g.pl[i].alive) g.pl[i].pos = g.L.zones[next].start[i];
      for (int k = 0; k < 400 && !(g.phase == PH_FIGHT && g.zone == next); ++k) game_update(g_pads);
    }
    quiet();
  }
}

static void test_fuzz(int per_zone) {
  printf("2) giocatori a caso in ogni zona (%d fotogrammi per zona, 4 giocatori)\n", per_zone);
  for (int m = 0; m < maps_count(); ++m) {
    Fuzz F;
    fuzz_map(m, per_zone, F);
    char buf[400];
    snprintf(buf, sizeof(buf), "%s: %ld fotogrammi, cadute nel cenote %ld (tutti a terra %ld), fughe %ld, salti di zona %ld, dentro gli ostacoli %ld%s%s",
             maps_name(m), F.frames, F.falls, F.wipes, F.leaks, F.jumps_zone, F.overlaps, F.first[0] ? " - " : "", F.first);
    check(F.leaks == 0 && F.jumps_zone == 0 && F.overlaps == 0 && F.falls_bad == 0 && F.respawn_bad == 0, buf);
  }
}

// --- 3) casi mirati ----------------------------------------------------------------------------------

// Tiene il giocatore i in salto e lo fa correre verso (tx, tz) per n fotogrammi; false se esce
// dalla zona o entra in un ostacolo.
static bool run_at(int i, float tx, float tz, int n, int jump_mode, char* why, int wn) {
  const int zone = level_zone_at(&g.L, g.pl[i].pos.x, g.pl[i].pos.z);
  for (int f = 0; f < n; ++f) {
    Player& p = g.pl[i];
    memset(g_pads, 0, sizeof(g_pads));
    float dx = tx - p.pos.x, dz = tz - p.pos.z;
    const float l = sqrtf(dx * dx + dz * dz);
    if (l > 1e-3f) { dx /= l; dz /= l; }
    g_pads[i].lx = dx;
    g_pads[i].ly = dz;
    const bool jump = jump_mode == 0 ? (f % 50) < 32 : (jump_mode == 1 ? (f % 9) < 2 : (f % 40) < 36);
    if (jump) g_pads[i].btn = (uint16_t)(1u << PAD_R2);
    const float y0 = p.pos.y;
    game_update(g_pads);
    quiet();
    p.invuln = 1000;
    p.hp = p.hp_max;
    if (!p.alive) { snprintf(why, wn, "a terra"); return false; }
    char what[200];
    if (overlaps(&g.L, p.pos.x, p.pos.z, y0, what, sizeof(what))) { snprintf(why, wn, "dentro un ostacolo: %.150s", what); return false; }
    const int z = level_zone_at(&g.L, p.pos.x, p.pos.z);
    if (z != zone) { snprintf(why, wn, "dalla zona %d alla %d a %.2f,%.2f", zone + 1, z + 1, p.pos.x, p.pos.z); return false; }
  }
  return true;
}

static void fresh(int map, int players) {
  game_init();
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(map, players, "0123");
  game_update(g_pads);
  quiet();
}

// Mette il giocatore 0 al centro della casella, sopra quello che c'e'.
static void put(int i, int cx, int cz) {
  Player& p = g.pl[i];
  p.pos = cell_center(&g.L, cx, cz);
  p.pos.y = level_stand_top(&g.L, cx, cz);
  p.grounded = true;
  p.vy = p.vx = p.vz = p.kx = p.kz = 0.0f;
  p.leap_n = 0;
  p.tramp_t = 0;
}

static void test_targeted(void) {
  printf("3) casi mirati\n");
  char buf[400], why[200];
  for (int m = 0; m < maps_count(); ++m) {
    // a) dai blocchi e dalle piattaforme accanto ai muri del bordo della zona, saltando contro il muro
    fresh(m, 1);
    int tried = 0, bad = 0;
    why[0] = 0;
    for (int cz = 1; cz + 1 < g.L.H && tried < 40; ++cz)
      for (int cx = 1; cx + 1 < g.L.W && tried < 40; ++cx) {
        const int c = g.L.cell[cz][cx];
        if (c != C_PLATFORM && c != C_CRATE) continue;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int d = 0; d < 4; ++d) {
          const bool next = cell_type(&g.L, cx + D[d][0], cz + D[d][1]) == C_WALL;
          const bool two = cell_type(&g.L, cx + 2 * D[d][0], cz + 2 * D[d][1]) == C_WALL && level_walk_top(&g.L, cx + D[d][0], cz + D[d][1]) <= PL_STEP;
          if (!next && !two) continue;
          const int z = g.L.zone[cz][cx];
          if (z < 0) continue;
          g.zone = z;
          g.phase = PH_FIGHT;
          put(0, cx, cz);
          const Vec3 wc = cell_center(&g.L, cx + 2 * D[d][0], cz + 2 * D[d][1]);
          char w2[200];
          for (int k = 0; k < 3; ++k) {
            const float side = (float)(k - 1) * 0.7f;
            if (!run_at(0, wc.x + side * (float)D[d][1], wc.z + side * (float)D[d][0], 120, k, w2, sizeof(w2))) {
              bad++;
              if (!why[0]) snprintf(why, sizeof(why), "da %d,%d: %.160s", cx, cz, w2);
            }
            put(0, cx, cz);
          }
          tried++;
        }
      }
    snprintf(buf, sizeof(buf), "%s: salti da blocchi e piattaforme contro i muri (%d casi)%s%s", maps_name(m), tried, bad ? ": " : "", why);
    check(bad == 0, buf);
    // b) contro i cancelli chiusi, saltando e correndo di traverso
    fresh(m, 1);
    bad = 0;
    tried = 0;
    why[0] = 0;
    for (int gi = 0; gi < g.L.ngates; ++gi) {
      const Gate& gt = g.L.gates[gi];
      const int z = gt.from_zone;
      g.zone = z;
      g.phase = PH_FIGHT;
      for (int x = gt.x0; x <= gt.x1; ++x) {
        // la casella della zona davanti al cancello
        int fz = -1;
        if (cell_zone(&g.L, x, gt.z1 + 1) == z) fz = gt.z1 + 1;
        else if (cell_zone(&g.L, x, gt.z0 - 1) == z) fz = gt.z0 - 1;
        if (fz < 0 || g.L.cell[fz][x] == C_WATER || g.L.cell[fz][x] == C_PIT) continue;
        for (int k = 0; k < 3; ++k) {
          put(0, x, fz);
          const Vec3 t = cell_center(&g.L, x + (k - 1) * 2, fz + (fz > gt.z1 ? -3 : 3));
          char w2[200];
          if (!run_at(0, t.x, t.z, 150, k, w2, sizeof(w2))) {
            bad++;
            if (!why[0]) snprintf(why, sizeof(why), "cancello %d: %.160s", gi, w2);
          }
          tried++;
        }
      }
    }
    snprintf(buf, sizeof(buf), "%s: salti contro i cancelli chiusi (%d casi)%s%s", maps_name(m), tried, bad ? ": " : "", why);
    check(bad == 0, buf);
    // c) verso l'acqua da ogni riva, saltando
    fresh(m, 1);
    bad = 0;
    tried = 0;
    why[0] = 0;
    for (int cz = 1; cz + 1 < g.L.H && tried < 60; ++cz)
      for (int cx = 1; cx + 1 < g.L.W && tried < 60; ++cx) {
        if (g.L.cell[cz][cx] != C_WATER) continue;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int d = 0; d < 4; ++d) {
          const int bx = cx + D[d][0], bz = cz + D[d][1];
          const int c = cell_type(&g.L, bx, bz);
          if (c != C_FLOOR && c != C_PATH && c != C_GRASS && c != C_BUSH) continue;
          if ((cx * 7 + cz * 3 + d) % 3) continue;     // un riva su tre basta
          const int z = g.L.zone[bz][bx];
          g.zone = z;
          g.phase = PH_FIGHT;
          put(0, bx, bz);
          const Vec3 t = cell_center(&g.L, cx - 2 * D[d][0], cz - 2 * D[d][1]);
          char w2[200];
          if (!run_at(0, t.x, t.z, 100, (cx + cz) % 3, w2, sizeof(w2))) {
            bad++;
            if (!why[0]) snprintf(why, sizeof(why), "riva %d,%d: %.160s", bx, bz, w2);
          }
          tried++;
        }
      }
    snprintf(buf, sizeof(buf), "%s: salti verso l'acqua dalle rive (%d casi)%s%s", maps_name(m), tried, bad ? ": " : "", why);
    check(bad == 0, buf);
  }
}

// --- mappe di prova fatte a mano (cartella PUPAZZI_MAPS: sostituiscono terra1) ---------------------

static const char* const MAP_ACQUA =
  "nome: Prova acqua\nmondo: terra\nstile: piramidi\nzona 1: scorpioni 1\nzona 2: scorpioni 1\n---\n"
  "###############\n"
  "#.............#\n"
  "#.............#\n"
  "#.........~~~.#\n"
  "######GGG######\n"
  "#.........~~~.#\n"
  "#.........~~~.#\n"
  "#....,12,.....#\n"
  "#....,34,...T.#\n"
  "###############\n";

static const char* const MAP_CENOTE =
  "nome: Prova cenote\nmondo: terra\nstile: chichenitza\nzona 1: scorpioni 1\n---\n"
  "#################\n"
  "#...............#\n"
  "#...OOO.........#\n"
  "#...OOO....O....#\n"
  "#...OOO....O....#\n"
  "#..........O....#\n"
  "#..x............#\n"
  "#...............#\n"
  "#......,,,......#\n"
  "#......,12,.....#\n"
  "#......,34,.....#\n"
  "#......,,,......#\n"
  "#################\n";

static void use_custom(const char* text) {
  char path[160];
  snprintf(path, sizeof(path), "%s/terra1.txt", g_base);
  FILE* f = fopen(path, "wb");
  if (f) {
    fputs(text, f);
    fclose(f);
  }
  setenv("PUPAZZI_MAPS", g_base, 1);
}
static void no_custom(void) {
  char path[160];
  snprintf(path, sizeof(path), "%s/terra1.txt", g_base);
  unlink(path);
  unsetenv("PUPAZZI_MAPS");
}

static void test_custom(void) {
  printf("4) mappe di prova: acqua contro il muro tra due zone, cenote, casse, cancelli\n");
  char buf[400], why[200];
  {
    // L'errore della 0.5: acqua contro il muro da tutte e due le parti. Si salta verso l'acqua
    // della zona 1 da ogni lato: non ci si entra e non si passa nella zona 2.
    use_custom(MAP_ACQUA);
    fresh(0, 1);
    check(!strcmp(g.L.name, "Prova acqua") && g.L.nzones == 2, "la mappa di prova con l'acqua si carica");
    bool ok = true;
    why[0] = 0;
    for (int k = 0; k < 12 && ok; ++k) {
      put(0, 9 + (k % 5), 7);
      const Vec3 t = cell_center(&g.L, 10 + (k % 3), 3);
      ok = run_at(0, t.x, t.z, 240, k % 3, why, sizeof(why));
    }
    snprintf(buf, sizeof(buf), "salti continui verso l'acqua contro il muro: si resta nella zona 1%s%s", ok ? "" : ": ", why);
    check(ok, buf);
    // Il trampolino lancia verso l'acqua: il volo si ferma alla riva come contro un muro.
    {
      put(0, 12, 8);
      bool okt = true, launched = false;
      for (int k = 0; k < 90 && okt; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        g_pads[0].ly = -1.0f;
        const float y0 = g.pl[0].pos.y;
        game_update(g_pads);
        quiet();
        if (g.pl[0].tramp_t > 0) launched = true;
        char what[200];
        if (overlaps(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, y0, what, sizeof(what)) || level_zone_at(&g.L, g.pl[0].pos.x, g.pl[0].pos.z) != 0) okt = false;
      }
      snprintf(buf, sizeof(buf), "trampolino verso l'acqua: lanciato (%s), il volo si ferma alla riva", launched ? "si" : "no");
      check(okt && launched, buf);
    }
    // Corpo messo a forza dentro l'acqua (come nella 0.5) circondato da acqua e muro: la spinta
    // non lo manda mai dentro il muro.
    float x = cell_center(&g.L, 11, 5).x, z = cell_center(&g.L, 11, 5).z - 0.42f;   // vicino al muro
    const float x0 = x, z0 = z;
    level_push_out(&g.L, &x, &z, 0.0f, PL_R, PL_STEP, BODY_WALK);
    int cx, cz;
    level_cell(&g.L, x, z, &cx, &cz);
    snprintf(buf, sizeof(buf), "spinta da dentro l'acqua: il centro resta fuori dai muri (casella %d,%d tipo %d)", cx, cz, g.L.cell[cz][cx]);
    check(g.L.cell[cz][cx] != C_WALL && g.L.zone[cz][cx] == 0, buf);
    (void)x0;
    (void)z0;
    // La guardia: dentro un ostacolo non si entra, si torna dove si era.
    float gx = cell_center(&g.L, 9, 5).x, gz = cell_center(&g.L, 9, 5).z;
    const float ox = gx, oz = gz;
    gx += 1.0f;   // dentro l'acqua
    const bool took = level_body_guard(&g.L, ox, oz, &gx, &gz, 0.0f, PL_R, PL_STEP, BODY_WALK);
    check(!took && level_body_ok(&g.L, gx, gz, 0.0f, PL_R, PL_STEP, BODY_WALK), "level_body_guard rifiuta lo spostamento dentro l'acqua");
    // Chi vola passa sopra l'acqua.
    check(level_body_ok(&g.L, cell_center(&g.L, 11, 5).x, cell_center(&g.L, 11, 5).z, 0.0f, 0.26f, PL_STEP, BODY_FLY),
          "chi vola passa sopra l'acqua");
    // Cancello: uno che resta nel passaggio quando la zona 2 comincia viene riportato dentro.
    clear_zone();
    put(0, 7, 3);
    for (int k = 0; k < 400 && !(g.phase == PH_FIGHT && g.zone == 1); ++k) game_update(g_pads);
    quiet();
    for (int k = 0; k < 60; ++k) game_update(g_pads);
    const int zz = level_zone_at(&g.L, g.pl[0].pos.x, g.pl[0].pos.z);
    snprintf(buf, sizeof(buf), "zona 2 cominciata e cancello chiuso: G1 nella zona 2 (zona %d)", zz + 1);
    check(g.zone == 1 && zz == 1 && level_body_ok(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK), buf);
  }
  {
    use_custom(MAP_CENOTE);
    fresh(0, 1);
    check(!strcmp(g.L.name, "Prova cenote"), "la mappa di prova con il cenote si carica");
    // a terra dentro il cenote: si perde una vita (da soli: si rifa' la zona)
    Player& p = g.pl[0];
    put(0, 5, 6);
    const int lives = p.lives;
    int f = 0;
    for (; f < 200 && p.alive; ++f) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[0].ly = -1.0f;
      game_update(g_pads);
      quiet();
    }
    int cx = -1, cz = -1;
    level_cell(&g.L, p.pos.x, p.pos.z, &cx, &cz);
    snprintf(buf, sizeof(buf), "camminando nel cenote si cade: vite %d -> %d, casella %d,%d, \"%s\"", lives, p.lives, cx, cz, g.msg);
    check(!p.alive && p.lives == lives - 1 && g.L.cell[cz][cx] == C_PIT && g.phase == PH_WIPE, buf);
    for (int k = 0; k < 30; ++k) game_update(g_pads);
    snprintf(buf, sizeof(buf), "affonda sotto l'acqua del pozzo (quota %.2f)", p.pos.y);
    check(p.pos.y < -1.0f, buf);
    for (int k = 0; k < WIPE_ZONE_FRAMES + 5; ++k) game_update(g_pads);
    quiet();
    check(p.alive && !level_pit_near(&g.L, p.pos.x, p.pos.z, PL_R + 0.5f), "si riparte dalla partenza della zona, lontano dal pozzo");
    // sopra il cenote stretto (una casella) si salta senza cadere
    put(0, 10, 4);
    p.yaw = D_HALF_PI;
    bool fell = false, over = false;
    for (int k = 0; k < 60; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[0].lx = 1.0f;
      if (k < 30) g_pads[0].btn = (uint16_t)(1u << PAD_R2);
      game_update(g_pads);
      quiet();
      if (!p.alive) fell = true;
      if (level_pit_at(&g.L, p.pos.x, p.pos.z)) over = true;
      if (p.pos.x > cell_center(&g.L, 12, 4).x + 0.3f) break;
    }
    level_cell(&g.L, p.pos.x, p.pos.z, &cx, &cz);
    snprintf(buf, sizeof(buf), "salto sopra il cenote largo una casella: passato (casella %d,%d), mai caduto", cx, cz);
    check(over && !fell && p.alive && cx >= 12, buf);
    // sull'orlo (centro fuori dalla casella del pozzo) non si cade: l'orlo e' il bordo della casella
    put(0, 7, 3);
    p.pos.x = cell_center(&g.L, 7, 3).x - 0.45f;     // a 5 cm dal pozzo
    for (int k = 0; k < 30; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      game_update(g_pads);
      quiet();
    }
    check(p.alive, "sull'orlo del cenote, col centro fuori, si resta in piedi");
    // il mostro spinto dentro cade ed e' sconfitto (la vittoria a chi l'ha spinto)
    put(0, 9, 8);
    const int kills = p.kills;
    Vec3 at = cell_center(&g.L, 7, 3);
    monster_spawn(MO_SCORPIONE, false, &at);
    int mk = -1;
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) mk = k;
    bool dead = false;
    if (mk >= 0) {
      g.mo[mk].rise = 0;
      g.mo[mk].hp = g.mo[mk].hp_max = 1000.0f;
      g.spawn_wait = 1000000;
      damage_monster(mk, 1.0f, 0, v3(-1, 0, 0), 9.0f, false);
      for (int k = 0; k < 60 && g.mo[mk].alive; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        game_update(g_pads);
        g.spawn_wait = 1000000;
        memset(g.spawn_left, 0, sizeof(g.spawn_left));
      }
      dead = !g.mo[mk].alive;
    }
    snprintf(buf, sizeof(buf), "mostro spinto nel cenote: sconfitto (%s), la vittoria va a G1 (%d -> %d)", dead ? "si" : "no", kills, p.kills);
    check(dead && p.kills == kills + 1, buf);
    // e quello che cammina da solo non ci entra: insegue un giocatore dall'altra parte del pozzo
    quiet();
    put(0, 2, 3);
    at = cell_center(&g.L, 8, 3);
    monster_spawn(MO_SCORPIONE, false, &at);
    mk = -1;
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) mk = k;
    bool alive = mk >= 0;
    if (alive) {
      g.mo[mk].rise = 0;
      for (int k = 0; k < 300; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        g.pl[0].invuln = 1000;
        game_update(g_pads);
        g.spawn_wait = 1000000;
        memset(g.spawn_left, 0, sizeof(g.spawn_left));
        if (!g.mo[mk].alive) { alive = false; break; }
      }
    }
    check(alive, "un mostro che cammina non entra nel cenote da solo (lo gira)");
    // in due: chi cade rientra accanto al compagno, mai sul pozzo ne' accanto
    quiet();
    fresh(0, 2);
    put(0, 7, 3);
    g.pl[0].pos.x -= 0.4f;                       // G1 sull'orlo
    put(1, 5, 6);
    for (int k = 0; k < 200 && g.pl[1].alive; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[1].ly = -1.0f;
      game_update(g_pads);
      quiet();
      g.pl[0].invuln = 1000;
    }
    snprintf(buf, sizeof(buf), "in due: G2 cade nel cenote (\"%s\")", g.msg);
    check(!g.pl[1].alive && strstr(g.msg, "CENOTE") != NULL, buf);
    bool back = false, safe = true;
    for (int k = 0; k < RESPAWN_FRAMES + 200; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      if (k == RESPAWN_FRAMES + 60) g.pl[0].pos = cell_center(&g.L, 13, 8);   // G1 si allontana dall'orlo
      game_update(g_pads);
      quiet();
      g.pl[0].invuln = 1000;
      if (g.pl[1].alive) {
        back = true;
        if (level_pit_near(&g.L, g.pl[1].pos.x, g.pl[1].pos.z, PL_R + 0.5f)) safe = false;
      }
    }
    snprintf(buf, sizeof(buf), "G2 rientra lontano dal cenote (a %.2f,%.2f)", g.pl[1].pos.x, g.pl[1].pos.z);
    check(back && safe, buf);
    // casse che tornano su chi ci sta sopra quando la zona ricomincia: si riparte dalla partenza
    fresh(0, 1);
    const int d = g.L.dyn_at[6][3];
    if (d >= 0) g.L.dyn[d].hp = 0.0f;
    put(0, 3, 6);
    g.pl[0].invuln = 0;
    g.pl[0].shield = false;
    hurt_player(0, 100000.0f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
    for (int k = 0; k < WIPE_ZONE_FRAMES + 5; ++k) game_update(g_pads);
    quiet();
    snprintf(buf, sizeof(buf), "cassa rotta, rimessa a posto alla ripartenza: G1 fuori dalla cassa (%.2f,%.2f)", g.pl[0].pos.x, g.pl[0].pos.z);
    check(d >= 0 && g.L.dyn[d].hp > 0.0f && g.pl[0].alive && level_body_ok(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK), buf);
  }
  no_custom();
}

int main(int argc, char** argv) {
  plat_log_open("/dev/null");
  const int per_zone = argc >= 2 ? atoi(argv[1]) : 3000;
  if (argc >= 3) g_seed = (uint32_t)atoi(argv[2]);
  if (!mkdtemp(g_base)) {
    printf("mkdtemp non riuscita\n");
    return 1;
  }
  save_open(g_base);
  game_set_cards_enabled(false);
  test_static();
  test_fuzz(per_zone);
  test_targeted();
  test_custom();
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd)) {}
  printf("\n%s\n", g_fail ? "CI SONO ERRORI" : "tutto bene");
  return g_fail ? 1 : 0;
}
