// Map tests (0.5.1): no escapes from the zones. Andrea, in the Piramidi map, jumped into the Nile
// and from there the pushes took him out through the other side of the walls, into the next zones
// without opening the gates. Here:
//   1) fixed checks of the built-in maps (edge, outside, zones that touch, cells outside the
//      zones, everything reachable on foot) redone separately, and the wrong maps the game must
//      reject with their message;
//   2) random players: in every zone of every map 4 invulnerable players, with no monsters, go
//      back and forth with short and long jumps, against walls, water, gates, blocks, platforms,
//      low walls, trampolines and cenotes, with a few strong pushes like those of bombs; at every
//      frame each one must stay in their zone (or in the passage of the open gate) and never
//      touch a cell that stops them; whoever falls into the cenote is counted separately;
//   3) targeted cases: jumps from platforms and blocks against the zone walls, against closed
//      gates, towards water; the cenote (falling in, jumping over it, monsters pushed in, whoever
//      comes back does not reappear on the pit); crates that come back on whoever stands there.
// Random numbers from a hash function of the test: never the game's generator.
//   make build/native/test_maps && ./build/native/test_maps [FRAMES_PER_ZONE [SEED]]
#include "game/sim/game_int.h"
#include "game/sim/maps.h"
#include "platform/plat.h"
#include "game/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "test.h"

static Game& g = *game_create();   // the game under test (the game keeps no global state: the test owns it)

static Pad g_pads[MAX_PLAYERS];
static char g_base[64] = "/tmp/pupazzi_test_maps_XXXXXX";
static int g_per_zone = 3000;        // first argument: frames per zone
static uint32_t g_seed = 0;          // second argument: other random games

// As in the game: the settings, then game 1 (loaded or, if there is none, new).
static void open_game(const char* dir) {
  save_open(dir);
  if (!save_slot_load(0)) save_slot_new(0);
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

// No monsters, no waves, no boss: the zone stays to be cleared until the test decides otherwise.
static void quiet(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  monsters_clear(g);
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
  g.boss.alive = false;
}

static void clear_zone(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.wave = g.L.zones[g.zone].nwaves - 1;
  g.spawn_wait = 0;
  monsters_clear(g);
  g.boss.alive = false;
  for (int k = 0; k < 10 && g.phase != PH_CLEAR && g.phase != PH_MAP_DONE; ++k) game_update(g, g_pads);
}

static int cell_type(const Level* L, int cx, int cz) { return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? (int)C_VOID : (int)L->cell[cz][cx]; }
static int cell_zone(const Level* L, int cx, int cz) { return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? -1 : L->zone[cz][cx]; }

// --- 1) fixed checks ---------------------------------------------------------------------------

// Redone here without level.cpp: the edge is wall or outside, the outside is closed off by walls,
// zones touch (even diagonally) only with a gate in between, every cell that is not wall, outside,
// tree, rock or gate is in a zone; inside every zone everything can be reached on foot.
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
    // with a single zone the "zona 2" line is one too many: try again without it
    snprintf(text, sizeof(text), "nome: prova\nmondo: terra\nzona 1: scorpioni 1\n---\n%s", grid);
    ok = level_parse(&g_lv, text, err, sizeof(err));
  } else if (strstr(err, "zona 2\" ma la griglia")) {
    snprintf(text, sizeof(text), "nome: prova\nmondo: terra\nzona 1: scorpioni 1\n---\n%s", grid);
    ok = level_parse(&g_lv, text, err, sizeof(err));
  }
  char buf[320];
  snprintf(buf, sizeof(buf), "rifiutata: %s (\"%s\")", name, ok ? "accettata!" : err);
  EXPECT(!ok && strstr(err, want) != NULL, buf);
}

TEST_CASE("1) mappe di serie e mappe sbagliate") {
  char buf[400], why[160];
  for (int i = 0; i < MAP_TEXT_COUNT; ++i) {
    char err[200];
    const bool ok = level_parse(&g_lv, MAP_TEXTS[i].text, err, sizeof(err));
    snprintf(buf, sizeof(buf), "%s: si carica (%s)", MAP_TEXTS[i].id, ok ? g_lv.name : err);
    EXPECT(ok, buf);
    if (!ok) continue;
    const bool s = static_ok(&g_lv, why, sizeof(why));
    snprintf(buf, sizeof(buf), "%s: bordo e fuori chiusi, zone separate, tutto nelle zone e raggiungibile a piedi%s%s", MAP_TEXTS[i].id,
             s ? "" : ": ", s ? "" : why);
    EXPECT(s, buf);
    // Zones as big as the built-in ones (31 x 21) and no water or cenote on zone edges outside the walls.
    if (g_lv.boss == BOSS_NONE) {
      bool sizes = true;
      for (int z = 0; z < g_lv.nzones; ++z) {
        const ZoneDef& zd = g_lv.zones[z];
        if (zd.x1 - zd.x0 + 1 != 31 || zd.z1 - zd.z0 + 1 != 21) sizes = false;
      }
      snprintf(buf, sizeof(buf), "%s: %d zone da 31 x 21", MAP_TEXTS[i].id, g_lv.nzones);
      EXPECT(sizes && g_lv.nzones == 3, buf);
    }
  }
  // Maps the game must reject (with their message in the maps menu).
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
    // and a correct map with an outside corner (spaces) closed off by walls loads
    char err[200];
    const bool ok = level_parse(&g_lv,
                                "nome: prova\nmondo: terra\nzona 1: scorpioni 1\nzona 2: scorpioni 1\n---\n"
                                "    ######\n    #....#\n    #....#\n#####GG###\n#........#\n#........#\n#..,12,..#\n##########\n",
                                err, sizeof(err));
    snprintf(buf, sizeof(buf), "accettata: mappa con un angolo fuori chiuso dai muri (%s)", ok ? "ok" : err);
    EXPECT(ok, buf);
  }
  // Swing gates (0.5.4): the leaves open towards the zone that opens the gate, also in north-south
  // gates and in one-cell ones; a rock in front stops the leaf that would bang into it (the other
  // one opens fully).
  {
    char err[200];
    const char* HEAD = "nome: prova\nmondo: terra\nzona 1: scorpioni 1\nzona 2: scorpioni 1\n---\n";
    char text[1024];
    snprintf(text, sizeof(text), "%s%s", HEAD, "##########\n#........#\n#........#\n#........#\n###GGGG###\n#...o....#\n#........#\n#..,12,..#\n##########\n");
    bool ok = level_parse(&g_lv, text, err, sizeof(err));
    const Gate& a = g_lv.gates[0];
    bool clear = true;      // the stopped leaf does not touch the rock
    if (ok) {
      Vec3 h, d;
      float len;
      level_gate_leaf(&g_lv, 0, 0, gate_theta(a, 0), &h, &d, &len);
      for (float r = 0.0f; r <= len; r += 0.05f)
        for (int w = -1; w <= 1; w += 2) {
          const Vec3 q = h + d * r + v3(-d.z, 0.0f, d.x) * ((float)w * GATE_LEAF_HALF);
          if (level_type_at(&g_lv, q.x, q.z) == C_ROCK) clear = false;
        }
    }
    snprintf(buf, sizeof(buf), "masso davanti al cancello: il battente sinistro si ferma a %.0f gradi senza toccarlo, il destro si apre del tutto (%s)",
             ok ? (double)(a.stop[0] * 90.0f) : 0.0, ok ? "ok" : err);
    EXPECT(ok && a.horiz && a.mz > 0.0f && a.stop[0] > 0.0f && a.stop[0] < 0.5f && a.stop[1] == 1.0f && clear, buf);
    snprintf(text, sizeof(text), "%s%s", HEAD, "##########\n#...#....#\n#...#....#\n#...G....#\n#...G....#\n#...#....#\n#.12#....#\n##########\n");
    ok = level_parse(&g_lv, text, err, sizeof(err));
    const Gate& b = g_lv.gates[0];
    snprintf(buf, sizeof(buf), "cancello da nord a sud: i battenti si aprono verso ovest, del tutto (%s)", ok ? "ok" : err);
    EXPECT(ok && !b.horiz && b.mx < 0.0f && b.mz == 0.0f && b.stop[0] == 1.0f && b.stop[1] == 1.0f, buf);
    snprintf(text, sizeof(text), "%s%s", HEAD, "########\n#......#\n#......#\n###G####\n#......#\n#......#\n#.12...#\n########\n");
    ok = level_parse(&g_lv, text, err, sizeof(err));
    const Gate& c = g_lv.gates[0];
    snprintf(buf, sizeof(buf), "cancello di una casella: da ovest a est, si apre verso sud (%s)", ok ? "ok" : err);
    EXPECT(ok && c.horiz && c.mz > 0.0f && c.stop[0] == 1.0f && c.stop[1] == 1.0f, buf);
  }
}

// --- 2) random players -----------------------------------------------------------------------------

struct Walker {
  float tx, tz;       // where it goes
  int t;              // frames with this target
  int mode;           // jumps: 0 none, 1 taps, 2 held, 3 random, 4 continuous
  int idle;           // standing still for a while
};

struct Fuzz {
  int map;
  long frames, overlaps, leaks, jumps_zone, falls, falls_bad, respawn_bad, wipes;
  char first[200];
};

// Is the cell a "hard" obstacle for someone walking (whatever the height of the feet)?
static bool hard_block(const Level* L, int cx, int cz) {
  const int c = cell_type(L, cx, cz);
  if (c == C_VOID || c == C_WALL || c == C_TREE || c == C_ROCK || c == C_WATER) return true;
  if (c == C_GATE) {
    const int gi = L->gate_at[cz][cx];
    return gi < 0 || L->gates[gi].open <= 0.0f;     // fully closed (while it moves, the pushes resolve it)
  }
  return false;
}

// Does the player's circle touch something it should not? feet: height of the feet it moved with.
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

// Interesting target in zone z (or near it): walls on the zone's edge, water, gates, blocks,
// platforms, low walls, trampolines, cenotes, any ground.
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
    // a bit of zigzag and now and then sideways, so it rubs against the corners
    const float wob = 0.6f * (hfloat(seed, (uint32_t)(frame / 7), (uint32_t)i + 50u) - 0.5f);
    g_pads[i].lx = clampf(dx - dz * wob, -1.0f, 1.0f);
    g_pads[i].ly = clampf(dz + dx * wob, -1.0f, 1.0f);
    bool jump = false;
    switch (k.mode) {
      case 1: jump = (frame + i * 7) % 23 < 2; break;                 // taps
      case 2: jump = (frame + i * 11) % 47 < 30; break;               // held (high)
      case 3: jump = hfloat(seed, (uint32_t)frame, (uint32_t)i + 99u) < 0.3f; break;
      case 4: jump = (frame & 1) != 0 || (frame % 61) < 40; break;    // almost always
      default: break;
    }
    if (jump) g_pads[i].btn |= (uint16_t)(1u << PAD_R2);
    // now and then a strong push like a bomb's (towards walls, water, gates, cenotes)
    if (hmix(seed, (uint32_t)frame, (uint32_t)i + 200u) % 97u == 0u) {
      const float a = D_TWO_PI * hfloat(seed, (uint32_t)frame, (uint32_t)i + 300u), k = 3.0f + 7.0f * hfloat(seed, (uint32_t)frame, (uint32_t)i + 400u);
      push_player(g, i, d_cos(a) * k, d_sin(a) * k, 0);
    }
  }
}

// One step of the game with the checks of every player.
static void fuzz_step(Fuzz& F, const Walker* w, Vec3* prev, float* prev_y, bool* prev_alive) {
  (void)w;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    prev[i] = g.pl[i].pos;
    prev_y[i] = g.pl[i].pos.y;
    prev_alive[i] = g.pl[i].alive;
  }
  const int zone0 = g.zone, phase0 = g.phase;
  game_update(g, g_pads);
  F.frames++;
  if (g.phase == PH_WIPE && phase0 != PH_WIPE) F.wipes++;
  if (getenv("TEST_MAPS_DEBUG") && (g.zone != zone0 || g.phase != phase0))
    printf("    fotogramma %ld: zona %d fase %d -> zona %d fase %d\n", F.frames, zone0 + 1, phase0, g.zone + 1, g.phase);
  // no monsters, lives that never run out, invulnerable
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  g.boss.alive = false;
  monsters_clear(g);
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
      // invulnerable: you go down only by falling into the cenote, with the center over the pit
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
    // in the right zone (or in the passage of an open gate)
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
    // from one zone to the other only through the gate (which has no zone)
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
  game_init(g);
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(g, map, 4, "0123");
  game_update(g, g_pads);
  quiet();
  Walker w[MAX_PLAYERS];
  memset(w, 0, sizeof(w));
  Vec3 prev[MAX_PLAYERS];
  float prev_y[MAX_PLAYERS];
  bool prev_alive[MAX_PLAYERS];
  const uint32_t seed = 0xC0FFEEu + (uint32_t)map * 7919u + g_seed * 104729u;
  int frame = 0;
  for (int z = 0; z < g.L.nzones; ++z) {
    // the zone with the gates closed
    for (int f = 0; f < per_zone; ++f, ++frame) {
      walker_pads(w, seed, frame, g.zone, false);
      fuzz_step(F, w, prev, prev_y, prev_alive);
    }
    // (if everyone fell together the map restarted: go on from the game's zone)
    if (g.zone + 1 >= g.L.nzones) continue;
    const int next = g.zone + 1;
    // cleared: on through the gate (with whoever stays behind jumping around)
    clear_zone();
    for (int i = 0; i < MAX_PLAYERS; ++i) w[i].t = 0;
    int f = 0;
    for (; f < 4000 && !(g.zone == next && g.phase == PH_FIGHT); ++f, ++frame) {
      const int tz = g.phase == PH_CLEAR ? g.zone + 1 : g.zone;
      walker_pads(w, seed, frame, tz, true);
      fuzz_step(F, w, prev, prev_y, prev_alive);
    }
    if (!(g.zone == next && g.phase == PH_FIGHT) && g.phase == PH_CLEAR && g.zone + 1 == next) {
      // they did not get there on their own (the leash, an obstacle): the test takes them there
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active && g.pl[i].alive) g.pl[i].pos = g.L.zones[next].start[i];
      for (int k = 0; k < 400 && !(g.phase == PH_FIGHT && g.zone == next); ++k) game_update(g, g_pads);
    }
    quiet();
  }
}

TEST_CASE("2) giocatori a caso in ogni zona, 4 giocatori") {
  const int per_zone = g_per_zone;
  printf("  (%d fotogrammi per zona)\n", per_zone);
  for (int m = 0; m < maps_count(); ++m) {
    Fuzz F;
    fuzz_map(m, per_zone, F);
    char buf[400];
    snprintf(buf, sizeof(buf), "%s: %ld fotogrammi, cadute nel cenote %ld (tutti a terra %ld), fughe %ld, salti di zona %ld, dentro gli ostacoli %ld%s%s",
             maps_name(m), F.frames, F.falls, F.wipes, F.leaks, F.jumps_zone, F.overlaps, F.first[0] ? " - " : "", F.first);
    EXPECT(F.leaks == 0 && F.jumps_zone == 0 && F.overlaps == 0 && F.falls_bad == 0 && F.respawn_bad == 0, buf);
  }
}

// --- 3) targeted cases -------------------------------------------------------------------------------

// Keeps player i jumping and makes them run towards (tx, tz) for n frames; false if they leave
// the zone or enter an obstacle.
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
    game_update(g, g_pads);
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
  game_init(g);
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(g, map, players, "0123");
  game_update(g, g_pads);
  quiet();
}

// Puts player 0 at the center of the cell, on top of whatever is there.
static void put(int i, int cx, int cz) {
  Player& p = g.pl[i];
  p.pos = cell_center(&g.L, cx, cz);
  p.pos.y = level_stand_top(&g.L, cx, cz);
  p.grounded = true;
  p.vy = p.vx = p.vz = p.kx = p.kz = 0.0f;
  p.leap_n = 0;
  p.tramp_t = 0;
}

TEST_CASE("3) casi mirati") {
  char buf[400], why[200];
  for (int m = 0; m < maps_count(); ++m) {
    // a) from the blocks and platforms next to the walls on the zone's edge, jumping against the wall
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
    EXPECT(bad == 0, buf);
    // b) against the closed gates, jumping and running sideways
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
        // the zone's cell in front of the gate
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
    EXPECT(bad == 0, buf);
    // c) towards the water from every bank, jumping
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
          if ((cx * 7 + cz * 3 + d) % 3) continue;     // one bank in three is enough
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
    EXPECT(bad == 0, buf);
  }
}

// --- hand-made test maps (PUPAZZI_MAPS directory: they replace terra1) -----------------------------

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

TEST_CASE("4) mappe di prova: acqua contro il muro tra due zone, cenote, casse, cancelli") {
  char buf[400], why[200];
  {
    // The 0.5 bug: water against the wall on both sides. Jump towards the water of zone 1 from
    // every side: you do not get into it and do not pass into zone 2.
    use_custom(MAP_ACQUA);
    fresh(0, 1);
    EXPECT(!strcmp(g.L.name, "Prova acqua") && g.L.nzones == 2, "la mappa di prova con l'acqua si carica");
    bool ok = true;
    why[0] = 0;
    for (int k = 0; k < 12 && ok; ++k) {
      put(0, 9 + (k % 5), 7);
      const Vec3 t = cell_center(&g.L, 10 + (k % 3), 3);
      ok = run_at(0, t.x, t.z, 240, k % 3, why, sizeof(why));
    }
    snprintf(buf, sizeof(buf), "salti continui verso l'acqua contro il muro: si resta nella zona 1%s%s", ok ? "" : ": ", why);
    EXPECT(ok, buf);
    // The trampoline launches towards the water: the flight stops at the bank as against a wall.
    {
      put(0, 12, 8);
      bool okt = true, launched = false;
      for (int k = 0; k < 90 && okt; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        g_pads[0].ly = -1.0f;
        const float y0 = g.pl[0].pos.y;
        game_update(g, g_pads);
        quiet();
        if (g.pl[0].tramp_t > 0) launched = true;
        char what[200];
        if (overlaps(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, y0, what, sizeof(what)) || level_zone_at(&g.L, g.pl[0].pos.x, g.pl[0].pos.z) != 0) okt = false;
      }
      snprintf(buf, sizeof(buf), "trampolino verso l'acqua: lanciato (%s), il volo si ferma alla riva", launched ? "si" : "no");
      EXPECT(okt && launched, buf);
    }
    // Body forced inside the water (as in 0.5) surrounded by water and wall: the push never sends
    // it inside the wall.
    float x = cell_center(&g.L, 11, 5).x, z = cell_center(&g.L, 11, 5).z - 0.42f;   // near the wall
    const float x0 = x, z0 = z;
    level_push_out(&g.L, &x, &z, 0.0f, PL_R, PL_STEP, BODY_WALK);
    int cx, cz;
    level_cell(&g.L, x, z, &cx, &cz);
    snprintf(buf, sizeof(buf), "spinta da dentro l'acqua: il centro resta fuori dai muri (casella %d,%d tipo %d)", cx, cz, g.L.cell[cz][cx]);
    EXPECT(g.L.cell[cz][cx] != C_WALL && g.L.zone[cz][cx] == 0, buf);
    (void)x0;
    (void)z0;
    // The guard: you do not get inside an obstacle, you go back where you were.
    float gx = cell_center(&g.L, 9, 5).x, gz = cell_center(&g.L, 9, 5).z;
    const float ox = gx, oz = gz;
    gx += 1.0f;   // inside the water
    const bool took = level_body_guard(&g.L, ox, oz, &gx, &gz, 0.0f, PL_R, PL_STEP, BODY_WALK);
    EXPECT(!took && level_body_ok(&g.L, gx, gz, 0.0f, PL_R, PL_STEP, BODY_WALK), "level_body_guard rifiuta lo spostamento dentro l'acqua");
    // Whoever flies passes over the water.
    EXPECT(level_body_ok(&g.L, cell_center(&g.L, 11, 5).x, cell_center(&g.L, 11, 5).z, 0.0f, 0.26f, PL_STEP, BODY_FLY),
           "chi vola passa sopra l'acqua");
    // Gate: someone left in the passage when zone 2 starts is brought back inside.
    clear_zone();
    put(0, 7, 3);
    for (int k = 0; k < 400 && !(g.phase == PH_FIGHT && g.zone == 1); ++k) game_update(g, g_pads);
    quiet();
    for (int k = 0; k < 60; ++k) game_update(g, g_pads);
    const int zz = level_zone_at(&g.L, g.pl[0].pos.x, g.pl[0].pos.z);
    snprintf(buf, sizeof(buf), "zona 2 cominciata e cancello chiuso: G1 nella zona 2 (zona %d)", zz + 1);
    EXPECT(g.zone == 1 && zz == 1 && level_body_ok(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK), buf);
  }
  {
    use_custom(MAP_CENOTE);
    fresh(0, 1);
    EXPECT(!strcmp(g.L.name, "Prova cenote"), "la mappa di prova con il cenote si carica");
    // down inside the cenote: a life is lost (alone: the zone is replayed)
    Player& p = g.pl[0];
    put(0, 5, 6);
    const int lives = p.lives;
    int f = 0;
    for (; f < 200 && p.alive; ++f) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[0].ly = -1.0f;
      game_update(g, g_pads);
      quiet();
    }
    int cx = -1, cz = -1;
    level_cell(&g.L, p.pos.x, p.pos.z, &cx, &cz);
    snprintf(buf, sizeof(buf), "camminando nel cenote si cade: vite %d -> %d, casella %d,%d, \"%s\"", lives, p.lives, cx, cz, g.msg);
    EXPECT(!p.alive && p.lives == lives - 1 && g.L.cell[cz][cx] == C_PIT && g.phase == PH_WIPE, buf);
    for (int k = 0; k < 30; ++k) game_update(g, g_pads);
    snprintf(buf, sizeof(buf), "affonda sotto l'acqua del pozzo (quota %.2f)", p.pos.y);
    EXPECT(p.pos.y < -1.0f, buf);
    for (int k = 0; k < WIPE_ZONE_FRAMES + 5; ++k) game_update(g, g_pads);
    quiet();
    EXPECT(p.alive && !level_pit_near(&g.L, p.pos.x, p.pos.z, PL_R + 0.5f), "si riparte dalla partenza della zona, lontano dal pozzo");
    // over the narrow cenote (one cell) you jump without falling
    put(0, 10, 4);
    p.yaw = D_HALF_PI;
    bool fell = false, over = false;
    for (int k = 0; k < 60; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[0].lx = 1.0f;
      if (k < 30) g_pads[0].btn = (uint16_t)(1u << PAD_R2);
      game_update(g, g_pads);
      quiet();
      if (!p.alive) fell = true;
      if (level_pit_at(&g.L, p.pos.x, p.pos.z)) over = true;
      if (p.pos.x > cell_center(&g.L, 12, 4).x + 0.3f) break;
    }
    level_cell(&g.L, p.pos.x, p.pos.z, &cx, &cz);
    snprintf(buf, sizeof(buf), "salto sopra il cenote largo una casella: passato (casella %d,%d), mai caduto", cx, cz);
    EXPECT(over && !fell && p.alive && cx >= 12, buf);
    // on the rim (center outside the pit's cell) you do not fall: the rim is the cell's edge
    put(0, 7, 3);
    p.pos.x = cell_center(&g.L, 7, 3).x - 0.45f;     // 5 cm from the pit
    for (int k = 0; k < 30; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      game_update(g, g_pads);
      quiet();
    }
    EXPECT(p.alive, "sull'orlo del cenote, col centro fuori, si resta in piedi");
    // the monster pushed in falls and is defeated (the kill goes to whoever pushed it)
    put(0, 9, 8);
    const int kills = p.kills;
    Vec3 at = cell_center(&g.L, 7, 3);
    monster_spawn(g, MO_SCORPIONE, false, &at);
    int mk = -1;
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) mk = k;
    bool dead = false;
    if (mk >= 0) {
      g.mo[mk].rise = 0;
      g.mo[mk].hp = g.mo[mk].hp_max = 1000.0f;
      g.spawn_wait = 1000000;
      damage_monster(g, mk, 1.0f, 0, v3(-1, 0, 0), 9.0f, false);
      for (int k = 0; k < 60 && g.mo[mk].alive; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        game_update(g, g_pads);
        g.spawn_wait = 1000000;
        memset(g.spawn_left, 0, sizeof(g.spawn_left));
      }
      dead = !g.mo[mk].alive;
    }
    snprintf(buf, sizeof(buf), "mostro spinto nel cenote: sconfitto (%s), la vittoria va a G1 (%d -> %d)", dead ? "si" : "no", kills, p.kills);
    EXPECT(dead && p.kills == kills + 1, buf);
    // and one walking on its own does not go in: it chases a player on the other side of the pit
    quiet();
    put(0, 2, 3);
    at = cell_center(&g.L, 8, 3);
    monster_spawn(g, MO_SCORPIONE, false, &at);
    mk = -1;
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) mk = k;
    bool alive = mk >= 0;
    if (alive) {
      g.mo[mk].rise = 0;
      for (int k = 0; k < 300; ++k) {
        memset(g_pads, 0, sizeof(g_pads));
        g.pl[0].invuln = 1000;
        game_update(g, g_pads);
        g.spawn_wait = 1000000;
        memset(g.spawn_left, 0, sizeof(g.spawn_left));
        if (!g.mo[mk].alive) { alive = false; break; }
      }
    }
    EXPECT(alive, "un mostro che cammina non entra nel cenote da solo (lo gira)");
    // two players: whoever falls comes back next to the teammate, never on the pit or next to it
    quiet();
    fresh(0, 2);
    put(0, 7, 3);
    g.pl[0].pos.x -= 0.4f;                       // G1 on the rim
    put(1, 5, 6);
    for (int k = 0; k < 200 && g.pl[1].alive; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      g_pads[1].ly = -1.0f;
      game_update(g, g_pads);
      quiet();
      g.pl[0].invuln = 1000;
    }
    snprintf(buf, sizeof(buf), "in due: G2 cade nel cenote (\"%s\")", g.msg);
    EXPECT(!g.pl[1].alive && strstr(g.msg, "CENOTE") != NULL, buf);
    bool back = false, safe = true;
    for (int k = 0; k < RESPAWN_FRAMES + 200; ++k) {
      memset(g_pads, 0, sizeof(g_pads));
      if (k == RESPAWN_FRAMES + 60) g.pl[0].pos = cell_center(&g.L, 13, 8);   // G1 leaves the rim
      game_update(g, g_pads);
      quiet();
      g.pl[0].invuln = 1000;
      if (g.pl[1].alive) {
        back = true;
        if (level_pit_near(&g.L, g.pl[1].pos.x, g.pl[1].pos.z, PL_R + 0.5f)) safe = false;
      }
    }
    snprintf(buf, sizeof(buf), "G2 rientra lontano dal cenote (a %.2f,%.2f)", g.pl[1].pos.x, g.pl[1].pos.z);
    EXPECT(back && safe, buf);
    // crates that come back on whoever stands there when the zone restarts: restart from the start
    fresh(0, 1);
    const int d = g.L.dyn_at[6][3];
    if (d >= 0) g.L.dyn[d].hp = 0.0f;
    put(0, 3, 6);
    g.pl[0].invuln = 0;
    g.pl[0].shield = false;
    hurt_player(g, 0, 100000.0f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
    for (int k = 0; k < WIPE_ZONE_FRAMES + 5; ++k) game_update(g, g_pads);
    quiet();
    snprintf(buf, sizeof(buf), "cassa rotta, rimessa a posto alla ripartenza: G1 fuori dalla cassa (%.2f,%.2f)", g.pl[0].pos.x, g.pl[0].pos.z);
    EXPECT(d >= 0 && g.L.dyn[d].hp > 0.0f && g.pl[0].alive && level_body_ok(&g.L, g.pl[0].pos.x, g.pl[0].pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK), buf);
  }
  no_custom();
}

int main(int argc, char** argv) {
  plat_log_open("/dev/null");
  if (argc >= 2 && argv[1][0] != '-') g_per_zone = atoi(argv[1]);
  if (argc >= 3 && argv[2][0] != '-') g_seed = (uint32_t)atoi(argv[2]);
  if (!mkdtemp(g_base)) {
    printf("mkdtemp non riuscita\n");
    return 1;
  }
  open_game(g_base);
  game_set_cards_enabled(g, false);
  const int failed = run_tests(argc, argv);
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd)) {}
  return failed;
}
