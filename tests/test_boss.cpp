// Tests of the giant Gladiator (0.5.1): chases by walking and goes around obstacles, slash in
// front and not behind, charge dodged by leaving the lane, charge into an obstacle (stunned and
// more damage), phases with roar and reinforcements, the cards at every third of its health
// (0.5.6), boss reset when restarting, never stuck with the heroes running around it, the same
// game twice. Uses the game without a frontend (like
// test_rules) and a save in a temporary directory.
//   make build/native/test_boss && ./build/native/test_boss [--immagini DIR] [--lotte [N]] [--ritardo F]
//                                                          [--solo HEROES] [--traccia N]
// --immagini: also the screens of the moves (PPM 640x480, software renderer) in DIR (and in
//   boss.txt where the boss is in each image, to crop them).
// --lotte: also whole games against the boss with 4, 2 and 1 bots (N per group, default 1): how
//   long they take, falls, map restarts, attacks. --ritardo F: in the fights the bots see the
//   ground markers F frames after they appear (like a person).
//   --solo HEROES (for example 01): only that fight, without the tests. --traccia N: one line
//   every N frames with boss and players.
#include "game/sim/game_int.h"
#include "game/view/view.h"
#include "game/sim/maps.h"
#include "platform/plat.h"
#include "game/save.h"
#include "engine/swr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "test.h"

static Game& g = *game_create();   // the game under test (the game keeps no global state: the test owns it)
static View& v = *view_create();   // its view: camera and frames for the images

static Pad g_pads[MAX_PLAYERS];
static const char* g_shots;             // --immagini
static char g_base[64] = "/tmp/pupazzi_test_boss_XXXXXX";
static char buf[512];
static int g_trace;                     // --traccia N: in the fights one line every N frames
static const char* g_only;              // --solo HEROES: only fights with these characters (skipping the tests)

// As in the game: the settings, then game 1 (loaded or, if there is none, new).
static void open_game(const char* dir) {
  save_open(dir);
  if (!save_slot_load(0)) save_slot_new(0);
}

static void step(int n = 1) {
  for (int k = 0; k < n; ++k) game_update(g, g_pads);
}

static int boss_map(void) {
  for (int i = 0; i < maps_count(); ++i)
    if (maps_world(i) == WORLD_TERRA && maps_is_boss(i)) return i;
  return -1;
}

static const char* save_dir(const char* name) {
  static char path[4][160];
  static int k = 0;
  char* p = path[k++ & 3];
  snprintf(p, 160, "%s/%s", g_base, name);
  mkdir(p, 0755);
  return p;
}

// No monsters, shots or markers: the heroes and the boss remain.
static void quiet(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  monsters_clear(g);
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
}

// The arena with n players (characters chars), without cards; the boss does not attack on its own
// (very long pause) until the test wants it to.
static void arena(int n, const char* chars) {
  game_init(g);
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(g, boss_map(), n, chars);
  step(3);
  quiet();
  for (int i = 0; i < MAX_PLAYERS; ++i) g.pl[i].invuln = 0;
  g.boss.cd = 1000000;
}

static void put(int i, float x, float z) {
  Player& p = g.pl[i];
  p.pos = v3(x, 0.0f, z);
  p.vx = p.vz = p.kx = p.kz = p.vy = 0.0f;
  p.grounded = true;
}

static void put_boss(float x, float z, float face) {
  Boss& b = g.boss;
  b.pos = v3(x, 0.0f, z);
  b.face = face;
  b.vx = b.vz = 0.0f;
  b.act = BA_NONE;
  b.roar = 0;
  b.cd = 1000000;
}

// Does the circle of radius r fit at (x, z) without touching obstacles for walkers?
static bool fits(float x, float z, float r) {
  const float fx = x + (float)g.L.W * 0.5f, fz = z + (float)g.L.H;
  for (int cz = (int)floorf(fz - r); cz <= (int)floorf(fz + r); ++cz)
    for (int cx = (int)floorf(fx - r); cx <= (int)floorf(fx + r); ++cx) {
      if (level_walk_top(&g.L, cx, cz) <= PL_STEP) continue;
      const float qx = clampf(fx, (float)cx, (float)cx + 1.0f), qz = clampf(fz, (float)cz, (float)cz + 1.0f);
      if ((fx - qx) * (fx - qx) + (fz - qz) * (fz - qz) < r * r) return false;
    }
  return true;
}

static const Marker* find_marker(int kind) {
  for (int i = 0; i < MAX_MARKERS; ++i)
    if (g.mk[i].alive && g.mk[i].kind == kind) return &g.mk[i];
  return 0;
}

static int count_markers(int kind) {
  int n = 0;
  for (int i = 0; i < MAX_MARKERS; ++i) n += g.mk[i].alive && g.mk[i].kind == kind;
  return n;
}

static float yaw_to(Vec3 a, Vec3 b) { return yaw_of(b.x - a.x, b.z - a.z); }

static bool has_float(const char* txt) {
  for (int i = 0; i < MAX_FLOATS; ++i)
    if (g.ft[i].alive && !strcmp(g.ft[i].txt, txt)) return true;
  return false;
}

// Game screen as the player would see it (640x480, software renderer).
static void shot(const char* name) {
  if (!g_shots) return;
  static Hud hud;
  FrameDesc fd{};
  view_build_frame(v, g, &fd, 640, 480, &hud);
  RenderStats st;
  const uint32_t* fb = swr_render(&fd, &st);
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", g_shots, name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n640 480\n255\n");
  for (int i = 0; i < 640 * 480; ++i) {
    const unsigned char px[3] = { (unsigned char)(fb[i] >> 16), (unsigned char)(fb[i] >> 8), (unsigned char)fb[i] };
    fwrite(px, 1, 3, f);
  }
  fclose(f);
  // Where the boss is in the image (to crop it): points of the 320x240 virtual screen.
  int sx = 0, sy = 0;
  if (to_screen(fd.cam, g.boss.pos + v3(0, 2.4f, 0), &sx, &sy)) {
    snprintf(path, sizeof(path), "%s/boss.txt", g_shots);
    FILE* t = fopen(path, "a");
    if (t) {
      fprintf(t, "%s %d %d\n", name, sx * 2, sy * 2);
      fclose(t);
    }
  }
}

// --- tests -----------------------------------------------------------------------------------

TEST_CASE("1) cammina verso il bersaglio, piu' piano degli eroi, e gira attorno alle statue") {
  arena(1, "0");
  const Vec3 spawn = g.L.boss_pos;
  put_boss(spawn.x, spawn.z, 0.0f);
  put(0, spawn.x, spawn.z + 12.5f);
  const float d0 = dist2d(g.boss.pos, g.pl[0].pos);
  step(90);
  const Vec3 a = g.boss.pos;
  step(30);
  const float speed = dist2d(a, g.boss.pos) / (30.0f * DT), d1 = dist2d(g.boss.pos, g.pl[0].pos);
  const float face = fabsf(d_wrap_pi(g.boss.face - yaw_to(g.boss.pos, g.pl[0].pos)));
  snprintf(buf, sizeof(buf), "in 2 secondi da %.1f a %.1f m, a %.2f m/s (fase 1: %.1f), guardandolo (%.2f rad)", (double)d0, (double)d1,
           (double)speed, (double)boss_speed(g), (double)face);
  EXPECT(d0 - d1 > 3.6f && fabsf(speed - 2.4f) < 0.15f && face < 0.15f && g.boss.act == BA_NONE, buf);
  g.boss.stage = 1;
  const float v1 = boss_speed(g);
  g.boss.stage = 2;
  const float v2 = boss_speed(g);
  g.boss.stage = 0;
  snprintf(buf, sizeof(buf), "piu' svelto a ogni fase: %.1f e %.1f m/s (gli eroi da 4,7 a 5)", (double)v1, (double)v2);
  EXPECT(v1 > 2.65f && v1 < 2.75f && v2 > 2.95f && v2 < 3.05f && v2 < CHARS[CH_MOCHI].speed, buf);
  // The target moves sideways: the gladiator turns and follows it.
  put(0, spawn.x + 9.0f, g.boss.pos.z + 1.0f);
  step(60);
  const float f2 = fabsf(d_wrap_pi(g.boss.face - yaw_to(g.boss.pos, g.pl[0].pos)));
  snprintf(buf, sizeof(buf), "il bersaglio va di lato: un secondo dopo lo guarda di nuovo (%.2f rad)", (double)f2);
  EXPECT(f2 < 0.3f, buf);
  // Behind a statue (detached 2x2 wall block): the straight path is blocked, it goes around it.
  bool found = false;
  Vec3 from = v3(0, 0, 0), to = v3(0, 0, 0);
  for (int cz = 1; cz + 2 < g.L.H && !found; ++cz)
    for (int cx = 1; cx + 2 < g.L.W && !found; ++cx) {
      const uint8_t (*c)[LV_MAX_W] = g.L.cell;
      if (c[cz][cx] != C_WALL || c[cz][cx + 1] != C_WALL || c[cz + 1][cx] != C_WALL || c[cz + 1][cx + 1] != C_WALL) continue;
      if (c[cz - 1][cx] == C_WALL || c[cz][cx - 1] == C_WALL || c[cz][cx + 2] == C_WALL || c[cz + 2][cx] == C_WALL) continue;
      const Vec3 m = cell_center(&g.L, cx, cz) + v3(0.5f, 0, 0.5f);
      for (float up = 2.3f; up < 3.6f && !found; up += 0.1f)
        if (fits(m.x, m.z - up, boss_r()) && fits(m.x, m.z + 3.5f, PL_R + 0.1f)) {
          from = v3(m.x, 0, m.z - up);
          to = v3(m.x, 0, m.z + 3.5f);
          found = true;
        }
    }
  if (!found) {
    printf("  (nessuna statua con spazio dietro in questa arena: prova saltata)\n");
    return;
  }
  put_boss(from.x, from.z, 0.0f);
  put(0, to.x, to.z);
  int frames = 0, still = 0;
  Vec3 last = g.boss.pos;
  for (; frames < 600 && dist2d(g.boss.pos, g.pl[0].pos) > 3.0f; ++frames) {
    step();
    if (frames % 60 == 59) {
      if (dist2d(last, g.boss.pos) < 0.5f) still++;
      last = g.boss.pos;
    }
  }
  snprintf(buf, sizeof(buf), "da dietro la statua (%.1f %.1f) arriva a 3 m dal giocatore in %d fotogrammi (fermo %d volte)", (double)from.x,
           (double)from.z, frames, still);
  EXPECT(frames < 400 && still == 0, buf);
}

TEST_CASE("2) fendente: colpisce chi gli sta davanti, non chi sta dietro") {
  arena(2, "01");
  const Vec3 c = v3(g.L.boss_pos.x - 4.5f, 0, g.L.boss_pos.z + 2.5f);
  EXPECT(fits(c.x, c.z, boss_r()) && fits(c.x + 0.3f, c.z + 2.6f, PL_R) && fits(c.x, c.z - 2.6f, PL_R), "posto libero per la prova");
  put_boss(c.x, c.z, 0.0f);
  put(0, c.x + 0.3f, c.z + 2.6f);     // in front (towards the south)
  put(1, c.x, c.z - 2.6f);            // behind
  g.boss.target = 0;
  g.boss.target_t = 0;
  const float h0 = g.pl[0].hp, h1 = g.pl[1].hp;
  EXPECT(boss_force_act(g, BA_SWEEP), "comincia il fendente");
  const int wind = g.boss.wind;
  const Marker* m = find_marker(MK_SWEEP);
  snprintf(buf, sizeof(buf), "settore rosso a terra verso il bersaglio (%d fotogrammi di preparazione)", wind);
  EXPECT(m && fabsf(d_wrap_pi(m->yaw - yaw_to(c, g.pl[0].pos))) < 0.01f && m->fire_at == wind && wind == 33 &&
             fabsf(m->r - BOSS_SWEEP_R) < 0.01f,
         buf);
  step(wind - 1);
  const bool before = g.pl[0].hp == h0 && g.boss.act == BA_SWEEP && dist2d(g.boss.pos, c) < 0.01f;
  const float z0 = g.pl[0].pos.z;
  step(1);
  snprintf(buf, sizeof(buf), "fermo durante la preparazione; al colpo: davanti %.0f -> %.0f, dietro %.0f -> %.0f", (double)h0, (double)g.pl[0].hp,
           (double)h1, (double)g.pl[1].hp);
  EXPECT(before && fabsf(h0 - g.pl[0].hp - 28.0f) < 0.01f && g.pl[1].hp == h1, buf);
  step(10);
  snprintf(buf, sizeof(buf), "chi e' colpito vola via (%.2f m)", (double)(g.pl[0].pos.z - z0));
  EXPECT(g.pl[0].pos.z - z0 > 0.5f, buf);
  // Sideways at the sector's edge (75 degrees) you get hit, at 110 degrees not; in the last phase
  // it winds up faster.
  arena(2, "01");
  put_boss(c.x, c.z, 0.0f);
  put(0, c.x + 3.0f * d_sin(1.3f), c.z + 3.0f * d_cos(1.3f));
  put(1, c.x - 3.0f * d_sin(1.9f), c.z + 3.0f * d_cos(1.9f));
  g.boss.stage = 2;
  g.boss.target = 0;
  boss_force_act(g, BA_SWEEP);
  g.boss.act_yaw = 0.0f;              // sector facing south: one at 75 degrees, the other at 110
  const int w2 = g.boss.wind;
  const float a0 = g.pl[0].hp, a1 = g.pl[1].hp;
  step(w2 + 1);
  snprintf(buf, sizeof(buf), "fase 3: preparazione di %d fotogrammi; a 75 gradi colpito (%.0f), a 110 no (%.0f)", w2, (double)(a0 - g.pl[0].hp),
           (double)(a1 - g.pl[1].hp));
  EXPECT(w2 == 24 && g.pl[0].hp < a0 && g.pl[1].hp == a1, buf);
}

TEST_CASE("3) carica: chi resta nella corsia la prende e vola di lato, chi ne esce no") {
  for (int dodge = 0; dodge < 2; ++dodge) {
    arena(1, "1");
    const Vec3 s = g.L.boss_pos;
    put_boss(s.x, s.z, 0.0f);
    put(0, s.x, s.z + 9.0f);
    g.boss.target = 0;
    const float h0 = g.pl[0].hp;
    boss_force_act(g, BA_RUSH);
    const Marker* m = find_marker(MK_LANE);
    const float lane = g.boss.lane;
    const int wind = g.boss.wind;
    if (!dodge) {
      snprintf(buf, sizeof(buf), "corsia rossa larga %.1f m e lunga %.1f m verso il bersaglio (preparazione %d fotogrammi, contro un ostacolo: %s)",
               m ? (double)(2.0f * m->r) : 0.0, m ? (double)m->len : 0.0, wind, g.boss.crash ? "si" : "no");
      EXPECT(m && fabsf(m->yaw) < 0.01f && m->r > 1.15f && m->r < 1.3f && lane > 9.0f && lane <= 15.0f && wind == 48, buf);
    }
    if (dodge) g_pads[0].lx = 1.0f;     // as soon as they see the lane they step out sideways
    int f = 0;
    for (; f < 400 && g.boss.act == BA_RUSH && g.boss.run < g.boss.lane; ++f) {
      step();
      if (f == 30) memset(g_pads, 0, sizeof(g_pads));
    }
    memset(g_pads, 0, sizeof(g_pads));
    const float ran = dist2d(g.boss.pos, s);
    if (!dodge) {
      snprintf(buf, sizeof(buf), "restando nella corsia: %.0f di danni, spinto di lato di %.1f m; lui corre %.1f m in %d fotogrammi", (double)(h0 - g.pl[0].hp),
               (double)fabsf(g.pl[0].pos.x - s.x), (double)ran, f);
      EXPECT(fabsf(h0 - g.pl[0].hp - 25.0f) < 0.01f && fabsf(g.pl[0].pos.x - s.x) > 1.0f && fabsf(ran - lane) < 0.05f, buf);
      // It stopped without crashing: shortly after, the javelins.
      int t = 0;
      for (; t < 120 && g.boss.act != BA_JAVELIN; ++t) step();
      snprintf(buf, sizeof(buf), "fermo senza sbattere: dopo %d fotogrammi lancia i giavellotti", t);
      EXPECT(g.boss.act == BA_JAVELIN && !g.boss.crash, buf);
    } else {
      snprintf(buf, sizeof(buf), "uscito di lato (a %.1f m dalla linea): nessun danno; lui corre %.1f m", (double)fabsf(g.pl[0].pos.x - s.x), (double)ran);
      EXPECT(g.pl[0].hp == h0 && fabsf(ran - lane) < 0.05f, buf);
    }
  }
}

TEST_CASE("4) carica contro un ostacolo: stordito per 1,5 secondi, intanto i colpi fanno il 50% in piu'") {
  arena(1, "1");
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z, D_HALF_PI);
  put(0, s.x + 8.0f, s.z);            // towards the east: the column or the arena wall
  g.pl[0].invuln = 100000;
  g.boss.target = 0;
  boss_force_act(g, BA_RUSH);
  const Marker* m = find_marker(MK_LANE);
  const bool crash = g.boss.crash && m && m->crash;
  const float lane = g.boss.lane;
  int f = 0;
  for (; f < 300 && g.boss.act == BA_RUSH; ++f) step();
  const Vec3 ahead = g.boss.pos + dir_yaw(g.boss.face) * 0.3f;   // one more step would touch the obstacle
  snprintf(buf, sizeof(buf), "la corsia finisce contro un ostacolo dopo %.1f m: dopo %d fotogrammi e' stordito (\"STORDITO!\")", (double)lane, f);
  EXPECT(crash && g.boss.act == BA_STUN && has_float("STORDITO!") && !fits(ahead.x, ahead.z, boss_r()), buf);
  const float hp0 = g.boss.hp;
  damage_boss(g, 100.0f, 0, false);
  const float stun_dmg = hp0 - g.boss.hp;
  int t = 1;
  for (; t < 300 && g.boss.act == BA_STUN; ++t) step();
  const float hp1 = g.boss.hp;
  damage_boss(g, 100.0f, 0, false);
  snprintf(buf, sizeof(buf), "stordito per %d fotogrammi: 100 di colpo ne tolgono %.0f, dopo %.0f", t, (double)stun_dmg, (double)(hp1 - g.boss.hp));
  EXPECT(t >= BOSS_STUN_FRAMES && t <= BOSS_STUN_FRAMES + 2 && fabsf(stun_dmg - 150.0f) < 0.01f && fabsf(hp1 - g.boss.hp - 100.0f) < 0.01f, buf);
}

TEST_CASE("5) a 2/3 e a 1/3 della vita ruggisce (prende poco) e chiama i rinforzi, senza riempire l'arena") {
  arena(2, "01");
  Boss& b = g.boss;
  b.hp = b.hp_max * 0.66f;
  step();
  snprintf(buf, sizeof(buf), "a 2/3 della vita: fase %d, ruggisce, \"%s\"", b.stage + 1, g.msg);
  EXPECT(b.stage == 1 && b.roar > 0 && !strcmp(g.msg, "SI ARRABBIA!"), buf);
  const float hp0 = b.hp;
  damage_boss(g, 100.0f, 0, false);
  snprintf(buf, sizeof(buf), "mentre ruggisce 100 di colpo ne tolgono %.0f", (double)(hp0 - b.hp));
  EXPECT(fabsf(hp0 - b.hp - 25.0f) < 0.01f, buf);
  step(70);
  const bool calls = b.act == BA_SUMMON;
  step(40);
  const int n1 = count_monsters(g);
  snprintf(buf, sizeof(buf), "poi chiama i rinforzi: %d mostri dalle botole", n1);
  EXPECT(calls && n1 >= 3 && n1 <= 10, buf);
  // More monsters on the field: the maximum stays 10.
  for (int k = 0; k < 6; ++k) monster_spawn(g, MO_SCARABEO, false, 0);
  for (int k = 0; k < 400 && b.act != BA_NONE; ++k) step();
  b.cd = 1000000;
  b.hp = b.hp_max * 0.32f;
  step();
  const bool roar2 = b.stage == 2 && b.roar > 0 && !strcmp(g.msg, "È FURIOSO!");
  step(115);
  snprintf(buf, sizeof(buf), "a 1/3: fase %d (\"È FURIOSO!\"), mostri in campo %d (al massimo 10)", b.stage + 1, count_monsters(g));
  EXPECT(roar2 && count_monsters(g) <= 10, buf);
  // An attack halfway through is not interrupted: the phase changes when it is over.
  arena(1, "0");
  put(0, g.boss.pos.x, g.boss.pos.z + 3.0f);
  g.boss.target = 0;
  boss_force_act(g, BA_SWEEP);
  g.boss.hp = g.boss.hp_max * 0.6f;
  step(5);
  const bool waits = g.boss.stage == 0 && g.boss.act == BA_SWEEP;
  for (int k = 0; k < 200 && g.boss.roar == 0; ++k) step();
  EXPECT(waits && g.boss.stage == 1 && g.boss.roar > 0, "con il fendente in corso ruggisce dopo averlo finito");
}

// Card choice open: everyone takes the card under the cursor (A on alternate frames, as a person
// presses it: the first A shows the cards, the next one takes), until the game starts again.
static bool choose_cards(void) {
  for (int k = 0; k < 2000 && g.cards_open; ++k) {
    for (int i = 0; i < MAX_PLAYERS; ++i) g_pads[i].btn = (k & 1) ? (uint16_t)(1u << PAD_A) : (uint16_t)0;
    step();
  }
  memset(g_pads, 0, sizeof(g_pads));
  return !g.cards_open;
}

static int ncards(int i) { return __builtin_popcount(g.pl[i].cards); }

// The arena as above (no choice at the start of the map), then with the cards on.
static void arena_cards(int n, const char* chars) {
  game_set_cards_enabled(g, false);
  arena(n, chars);
  game_set_cards_enabled(g, true);
}

TEST_CASE("5b) carte al boss (0.5.6): a 2/3 e a 1/3 della vita, una volta per terzo nella mappa") {
  arena_cards(2, "01");
  Boss& b = g.boss;
  b.hp = b.hp_max * 0.66f;
  step();
  bool ok = g.cards_open && g.offer[0].on && g.offer[1].on && b.stage == 0 && b.roar == 0 && g.boss_thirds == 1;
  EXPECT(ok, "a 2/3 della vita: prima la scelta delle carte per tutti, il gladiatore aspetta");
  ok = choose_cards() && ncards(0) == 1 && ncards(1) == 1;
  step();
  snprintf(buf, sizeof(buf), "scelte le carte (G1 %d, G2 %d): ruggisce adesso, fase %d, \"%s\"", ncards(0), ncards(1), b.stage + 1, g.msg);
  EXPECT(ok && b.stage == 1 && b.roar > 0 && !strcmp(g.msg, "SI ARRABBIA!"), buf);
  for (int k = 0; k < 400 && (b.roar > 0 || b.act != BA_NONE); ++k) step();
  b.cd = 1000000;
  b.hp = b.hp_max * 0.32f;
  step();
  ok = g.cards_open && b.stage == 1 && g.boss_thirds == 2;
  ok &= choose_cards();
  step();
  snprintf(buf, sizeof(buf), "a 1/3: di nuovo le carte (G1 %d, G2 %d), poi \"%s\"", ncards(0), ncards(1), g.msg);
  EXPECT(ok && ncards(0) == 2 && ncards(1) == 2 && b.stage == 2 && b.roar > 0 && !strcmp(g.msg, "È FURIOSO!"), buf);
  // Two thirds lost before the phase changes (during an attack): two choices in a row, one roar.
  arena_cards(1, "0");
  const unsigned seq = g.cards_seq;
  g.boss.hp = g.boss.hp_max * 0.3f;
  step();
  ok = g.cards_open && choose_cards();
  step();
  snprintf(buf, sizeof(buf), "da 1 a meno di 1/3 in una volta: due scelte di fila (%u, carte %d), poi un ruggito solo (fase %d)", g.cards_seq - seq,
           ncards(0), g.boss.stage + 1);
  EXPECT(ok && g.cards_seq - seq == 2 && ncards(0) == 2 && g.boss.stage == 2 && g.boss.roar > 0 && !strcmp(g.msg, "È FURIOSO!"), buf);
  // Alone, fallen with lives left: the zone (and the gladiator) starts again, the cards stay and
  // the thirds already rewarded do not give them again.
  arena_cards(1, "0");
  g.boss.hp = g.boss.hp_max * 0.6f;
  step();
  ok = choose_cards();
  step();
  g.pl[0].invuln = 0;
  hurt_player(g, 0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  step();
  ok &= g.phase == PH_WIPE && !g.wipe_map;
  step(WIPE_ZONE_FRAMES);
  quiet();
  g.boss.cd = 1000000;
  ok &= g.boss.hp == g.boss.hp_max && g.boss.stage == 0 && ncards(0) == 1;
  g.boss.hp = g.boss.hp_max * 0.6f;
  step();
  snprintf(buf, sizeof(buf), "zona rifatta: la carta resta (%d), di nuovo a 2/3 ruggisce subito senza carte", ncards(0));
  EXPECT(ok && !g.cards_open && g.boss.stage == 1 && g.boss.roar > 0, buf);
  // The hero falls in the same step in which the gladiator loses the third: no choice (the zone is
  // replayed), and the third still gives it when it is lost again.
  arena_cards(1, "0");
  g.boss.hp = g.boss.hp_max * 0.6f;
  g.pl[0].invuln = 0;
  hurt_player(g, 0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  step();
  ok = !g.cards_open && g.boss_thirds == 0 && g.phase == PH_WIPE;
  step(WIPE_ZONE_FRAMES);
  quiet();
  g.boss.cd = 1000000;
  g.boss.hp = g.boss.hp_max * 0.6f;
  step();
  EXPECT(ok && g.cards_open && g.boss_thirds == 1, "l'eroe cade nello stesso passo: niente carte; rifatta la zona, a 2/3 arrivano");
  choose_cards();
  // Last life lost: the map starts again from the start, without cards, and the thirds count again.
  for (int k = 0; k < 400 && (g.boss.roar > 0 || g.boss.act != BA_NONE); ++k) step();
  g.pl[0].lives = 1;
  g.pl[0].invuln = 0;
  hurt_player(g, 0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  step();
  ok = g.phase == PH_WIPE && g.wipe_map;
  step(WIPE_MAP_FRAMES);
  ok &= g.cards_open && g.boss_thirds == 0 && ncards(0) == 0;
  ok &= choose_cards() && ncards(0) == 1;
  step(3);
  quiet();
  g.boss.cd = 1000000;
  g.boss.hp = g.boss.hp_max * 0.6f;
  step();
  EXPECT(ok && g.cards_open && g.boss_thirds == 1, "mappa da capo: le carte dell'inizio e di nuovo quelle a 2/3");
  choose_cards();
  game_set_cards_enabled(g, false);
}

TEST_CASE("6) bersaglio: il piu' vicino, tenuto almeno 3 secondi; passa a chi lo picchia forte; giavellotti 1, 2, 3") {
  arena(2, "01");
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z, 0.0f);
  put(0, s.x - 2.0f, s.z + 7.0f);
  put(1, s.x + 3.0f, s.z + 9.5f);
  g.boss.target = -1;                 // as if it just appeared
  step(2);
  const bool first = g.boss.target == 0;
  put(1, s.x + 1.5f, g.boss.pos.z + 6.5f);      // a bit closer than G1 (which is about 7 m away)
  put(0, g.boss.pos.x - 2.0f, g.boss.pos.z + 7.0f);
  step(30);
  const bool kept = g.boss.target == 0;
  for (int k = 0; k < 200 && g.boss.target == 0; ++k) {
    put(0, g.boss.pos.x - 2.0f, g.boss.pos.z + 7.5f);
    put(1, g.boss.pos.x + 1.5f, g.boss.pos.z + 5.0f);
    step();
  }
  snprintf(buf, sizeof(buf), "prima G1 (il piu' vicino), tiene G1 con G2 appena piu' vicino, dopo 3 secondi passa a G2 (bersaglio G%d)", g.boss.target + 1);
  EXPECT(first && kept && g.boss.target == 1 && g.boss.target_t < 5, buf);
  // G1, far away, hits it hard: the gladiator goes after G1.
  step(70);
  for (int k = 0; k < 4; ++k) damage_boss(g, 60.0f, 0, false);
  step(2);
  snprintf(buf, sizeof(buf), "G1 da lontano gli toglie 240 in un attimo: insegue G1 (bersaglio G%d)", g.boss.target + 1);
  EXPECT(g.boss.target == 0, buf);
  // Javelins: 1, 2 and 3 by phase (with two heroes: one each, the third to the side of the target).
  int n[3];
  for (int st = 0; st < 3; ++st) {
    arena(2, "01");
    put_boss(s.x, s.z, 0.0f);
    put(0, s.x, s.z + 11.0f);
    put(1, s.x + 6.0f, s.z + 9.0f);
    g.boss.stage = st;
    g.boss.target = 0;
    boss_force_act(g, BA_JAVELIN);
    step(g.boss.wind + 1);
    n[st] = 0;
    for (int i = 0; i < MAX_SHOTS; ++i) n[st] += g.sh[i].alive && g.sh[i].kind == SK_JAVELIN;
  }
  snprintf(buf, sizeof(buf), "giavellotti per fase: %d, %d, %d (con il cerchio rosso dove cadono)", n[0], n[1], n[2]);
  EXPECT(n[0] == 1 && n[1] == 2 && n[2] == 3 && count_markers(MK_LOB) == 3, buf);
}

TEST_CASE("7) si ricomincia: il gladiatore torna al suo posto con la vita piena") {
  arena(1, "0");
  const Vec3 spawn = g.L.boss_pos;
  const ZoneDef& z = g.L.zones[g.zone];
  float dmin = 1e30f;
  for (int i = 0; i < MAX_PLAYERS; ++i) dmin = fminf(dmin, dist2d(spawn, z.start[i]));
  snprintf(buf, sizeof(buf), "parte dalla casella B, a %.1f m dalla partenza piu' vicina, dove ci sta", (double)dmin);
  EXPECT(dmin >= 4.0f && fits(spawn.x, spawn.z, boss_r()), buf);
  put_boss(spawn.x + 4.0f, spawn.z + 6.0f, 1.0f);
  g.boss.hp = g.boss.hp_max * 0.5f;
  g.boss.stage = 1;
  put(0, spawn.x + 4.0f, spawn.z + 9.0f);
  g.boss.target = 0;
  boss_force_act(g, BA_SWEEP);
  hurt_player(g, 0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  step();
  const bool wipe = g.phase == PH_WIPE && !g.wipe_map;
  step(WIPE_ZONE_FRAMES);
  const Boss& b = g.boss;
  snprintf(buf, sizeof(buf), "da soli, zona rifatta: al suo posto (%.1f m), vita %.0f su %.0f, fase %d, nessun segno a terra", (double)dist2d(b.pos, spawn),
           (double)b.hp, (double)b.hp_max, b.stage + 1);
  EXPECT(wipe && g.phase == PH_FIGHT && b.alive && dist2d(b.pos, spawn) < 0.01f && b.hp == b.hp_max && b.stage == 0 && b.act == BA_NONE &&
             count_markers(MK_SWEEP) == 0 && count_markers(MK_LANE) == 0,
         buf);
  // Two players: whoever falls comes back after 5 seconds, the boss stays as it was.
  arena(2, "01");
  g.boss.hp = g.boss.hp_max * 0.8f;
  const float hp = g.boss.hp;
  put_boss(spawn.x - 3.0f, spawn.z + 5.0f, 0.0f);
  const Vec3 at = g.boss.pos;
  hurt_player(g, 1, 1e6f, g.pl[1].pos + v3(1, 0, 0), 0.0f);
  step(RESPAWN_FRAMES + 2);
  snprintf(buf, sizeof(buf), "in due, G2 cade e rientra: il boss non torna al suo posto (%.1f m) e tiene la sua vita", (double)dist2d(g.boss.pos, spawn));
  EXPECT(g.pl[1].alive && g.boss.hp == hp && dist2d(g.boss.pos, spawn) > 2.0f && dist2d(g.boss.pos, at) < 8.0f, buf);
  // All down: map restarted, boss restarted.
  g.pl[0].invuln = g.pl[1].invuln = 0;
  hurt_player(g, 0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  hurt_player(g, 1, 1e6f, g.pl[1].pos + v3(1, 0, 0), 0.0f);
  step();
  const bool map = g.phase == PH_WIPE && g.wipe_map;
  step(WIPE_MAP_FRAMES);
  snprintf(buf, sizeof(buf), "tutti a terra: mappa da capo, boss al suo posto con la vita piena (%.0f)", (double)g.boss.hp);
  EXPECT(map && g.phase == PH_FIGHT && g.boss.hp == g.boss.hp_max && dist2d(g.boss.pos, spawn) < 0.01f && g.map_wipes == 1, buf);
}

// Two heroes running in a circle around the center of the arena (invulnerable, not shooting): the
// gladiator chases and attacks them for n frames. Never still for long while it wants to walk,
// never inside an obstacle or outside the zone.
TEST_CASE("8) 20000 fotogrammi con due eroi che gli corrono attorno: mai bloccato, mai dentro un ostacolo") {
  const int n = 20000;
  arena(2, "01");
  g.boss.cd = 60;
  const ZoneDef& z = g.L.zones[g.zone];
  const Vec3 c = v3(0.5f * (z.wx0 + z.wx1), 0.0f, 0.5f * (z.wz0 + z.wz1));
  const float R = 7.5f, w = 4.0f / R;
  int acts[BA_COUNT] = { 0 }, inside = 0, outside = 0, streak = 0, worst = 0, stuck = 0, prev = BA_NONE;
  Vec3 worst_at = v3(0, 0, 0), last_p[MAX_PLAYERS];
  int jump[MAX_PLAYERS] = { 0 };
  for (int i = 0; i < MAX_PLAYERS; ++i) last_p[i] = g.pl[i].pos;
  for (int f = 0; f < n; ++f) {
    if (f == n * 3 / 10) g.boss.hp = g.boss.hp_max * 0.6f;      // phases, reinforcements mid-test
    if (f == n * 6 / 10) g.boss.hp = g.boss.hp_max * 0.3f;
    memset(g_pads, 0, sizeof(g_pads));
    for (int i = 0; i < 2; ++i) {
      Player& p = g.pl[i];
      p.invuln = 2;
      if (!p.alive) continue;
      const float a = w * (float)f * DT + D_PI * (float)i + 0.5f;
      const Vec3 t = v3(c.x + R * d_sin(a), 0.0f, c.z + R * d_cos(a));
      const float dx = t.x - p.pos.x, dz = t.z - p.pos.z, l = sqrtf(dx * dx + dz * dz);
      if (l > 0.2f) {
        g_pads[i].lx = dx / l;
        g_pads[i].ly = dz / l;
      }
      if (f % 30 == 0) {
        if (dist2d(p.pos, last_p[i]) < 0.3f) jump[i] = 12;      // stuck against something: jump
        last_p[i] = p.pos;
      }
      if (jump[i] > 0 && jump[i]-- > 1) g_pads[i].btn |= 1u << PAD_R2;
    }
    const Vec3 b0 = g.boss.pos;
    step();
    const Boss& b = g.boss;
    if (b.act != prev && b.act != BA_NONE) acts[b.act]++;
    prev = b.act;
    if (!fits(b.pos.x, b.pos.z, boss_r() - 0.05f)) inside++;
    if (level_zone_at(&g.L, b.pos.x, b.pos.z) != g.zone) outside++;
    const bool wants = b.act == BA_NONE && b.roar == 0 && b.target >= 0 && dist2d(b.pos, g.pl[b.target].pos) > 3.5f;
    if (wants && dist2d(b0, b.pos) < 0.3f * DT) {
      if (++streak > worst) { worst = streak; worst_at = b.pos; }
      if (streak == 180) stuck++;
    } else {
      streak = 0;
    }
  }
  snprintf(buf, sizeof(buf), "fermo mentre voleva camminare al massimo %d fotogrammi di fila (a %.1f %.1f), bloccato %d volte; dentro un ostacolo %d, fuori zona %d",
           worst, (double)worst_at.x, (double)worst_at.z, stuck, inside, outside);
  EXPECT(stuck == 0 && inside == 0 && outside == 0, buf);
  snprintf(buf, sizeof(buf), "attacchi: fendenti %d, cariche %d (stordito %d), spaccaterra %d, giavellotti %d, rinforzi %d", acts[BA_SWEEP], acts[BA_RUSH],
           acts[BA_STUN], acts[BA_SLAM], acts[BA_JAVELIN], acts[BA_SUMMON]);
  EXPECT(acts[BA_SWEEP] > 0 && acts[BA_RUSH] > 0 && acts[BA_SLAM] > 0 && acts[BA_JAVELIN] > 0 && acts[BA_SUMMON] == 2, buf);
}

// Game with n bots from the boss map; returns the hash of all the frames.
static uint64_t bot_run(int n, const char* chars, int frames, int pre, uint64_t* last) {
  game_init(g);
  for (int k = 0; k < pre; ++k) {          // a few frames in the title: a different random generator
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_update(g, pads);
  }
  game_quick_start(g, boss_map(), n, chars);
  uint64_t h = 1469598103934665603ull;
  for (int f = 0; f < frames; ++f) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_bot_pads(g, pads, n);
    game_update(g, pads);
    h = (h ^ game_state_hash(g)) * 1099511628211ull;
  }
  if (last) *last = game_state_hash(g);
  return h;
}

TEST_CASE("9) la stessa lotta due volte: la stessa partita fotogramma per fotogramma") {
  uint64_t h[2], e[2];
  for (int run = 0; run < 2; ++run) {
    open_game(save_dir(run ? "det2" : "det1"));
    game_set_cards_enabled(g, true);
    h[run] = bot_run(3, "023", 5000, 0, &e[run]);
  }
  game_set_cards_enabled(g, false);
  snprintf(buf, sizeof(buf), "5000 fotogrammi con 3 giocatori automatici: impronte %016llx e %016llx (ultima %016llx)", (unsigned long long)h[0],
           (unsigned long long)h[1], (unsigned long long)e[0]);
  EXPECT(h[0] == h[1] && e[0] == e[1], buf);
}

// --- fights: whole games with the bots ------------------------------------------------------------

struct Fight {
  bool won;
  int frames, deaths, wipes, restarts, hits;
  int acts[BA_COUNT];
  float boss_hp;            // boss health at the end (0 if beaten)
};

static Fight fight(int n, const char* chars, int max_frames, int pre) {
  Fight r;
  memset(&r, 0, sizeof(r));
  game_init(g);
  for (int k = 0; k < pre; ++k) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_update(g, pads);
  }
  game_quick_start(g, boss_map(), n, chars);
  int prev = BA_NONE;
  float hp[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; ++i) hp[i] = g.pl[i].hp;
  for (int f = 0; f < max_frames; ++f) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_bot_pads(g, pads, n);
    game_update(g, pads);
    r.frames = f + 1;
    if (g.boss.alive && g.boss.act != prev && g.boss.act != BA_NONE) r.acts[g.boss.act]++;
    prev = g.boss.alive ? g.boss.act : BA_NONE;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      if (g.pl[i].active && g.pl[i].alive && g.pl[i].hp < hp[i] - 0.5f) r.hits++;
      hp[i] = g.pl[i].hp;
    }
    if (g_trace > 0 && f % g_trace == 0 && g.boss.alive) {
      const Boss& b = g.boss;
      printf("    f%5d boss %5.1f %5.1f vita %4.0f az %d/%3d bers %d |", f, (double)b.pos.x, (double)b.pos.z, (double)b.hp, b.act, b.act_t, b.target);
      for (int i = 0; i < n; ++i)
        printf(" G%d %5.1f %5.1f d %4.1f v %3.0f%s", i + 1, (double)g.pl[i].pos.x, (double)g.pl[i].pos.z, (double)dist2d(g.pl[i].pos, b.pos),
               (double)g.pl[i].hp, g.pl[i].alive ? "" : "x");
      printf(" mostri %d\n", count_monsters(g));
    }
    if (g.screen == SC_PLAY && g.phase == PH_MAP_DONE && maps_is_boss(g.map_idx)) {
      r.won = true;
      break;
    }
    if (g.screen != SC_PLAY) break;
  }
  r.deaths = g.deaths;
  r.wipes = g.wipes;
  r.restarts = g.restarts;
  r.boss_hp = g.boss.alive ? g.boss.hp : 0.0f;
  return r;
}

static void fights(int per, int lag) {
  printf("lotte con i giocatori automatici (ritardo %d fotogrammi sui segni a terra)\n", lag);
  struct Group { int n; const char* chars; int limit; };
  static const Group G[] = { { 4, "0123", 20000 }, { 2, "01", 30000 }, { 2, "23", 30000 }, { 2, "03", 30000 },
                             { 1, "0", 40000 }, { 1, "1", 40000 }, { 1, "2", 40000 }, { 1, "3", 40000 } };
  bot_set_lag(g, lag);
  game_set_cards_enabled(g, true);
  for (const Group& gr : G) {
    if (g_only && strcmp(g_only, gr.chars)) continue;
    for (int k = 0; k < per; ++k) {
      open_game(save_dir("lotte"));
      const Fight r = fight(gr.n, gr.chars, gr.limit, 17 * k);
      printf("  %d giocatori (%s), prova %d: %s in %d fotogrammi (%.1f minuti), cadute %d, zone rifatte %d, mappe da capo %d, colpi presi %d;"
             " fendenti %d, cariche %d (stordito %d), spaccaterra %d, giavellotti %d%s\n",
             gr.n, gr.chars, k + 1, r.won ? "battuto" : "NON battuto", r.frames, (double)r.frames / 3600.0, r.deaths, r.wipes, r.restarts, r.hits,
             r.acts[BA_SWEEP], r.acts[BA_RUSH], r.acts[BA_STUN], r.acts[BA_SLAM], r.acts[BA_JAVELIN], r.won ? "" : " (limite)");
      fflush(stdout);
    }
  }
  bot_set_lag(g, 0);
  game_set_cards_enabled(g, false);
}

// --- screens ----------------------------------------------------------------------------------

// Three heroes around the boss in the middle of the arena.
static void scene(const char* chars) {
  arena((int)strlen(chars), chars);
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z + 3.0f, 0.0f);
  static const float P[4][2] = { { 0.0f, 6.5f }, { -5.5f, 4.5f }, { 5.0f, 5.5f }, { 4.5f, -2.5f } };
  for (int i = 0; i < (int)strlen(chars); ++i) put(i, s.x + P[i][0], s.z + 3.0f + P[i][1]);
  for (int i = 0; i < MAX_PLAYERS; ++i) g.pl[i].invuln = 100000;
  g.boss.target = 0;
  g.msg_t = 0;                       // no map caption over it
  view_camera(v, g, CAM_CUT);
}

static void pictures(void) {
  printf("schermate in %s\n", g_shots);
  // Walk: four moments of the stride.
  scene("012");
  put(0, g.boss.pos.x + 1.0f, g.boss.pos.z + 11.0f);
  g.boss.cd = 1000000;
  step(40);
  for (int k = 0, n = 0; k < 120 && n < 4; ++k) {
    const float ph = d_sin(g.boss.phase);
    step();
    if ((n % 2 == 0 && ph > 0.9f && d_sin(g.boss.phase) < ph) || (n % 2 == 1 && ph < -0.9f && d_sin(g.boss.phase) > ph)) {
      char name[32];
      snprintf(name, sizeof(name), "01_cammina_%d", n++);
      shot(name);
    }
  }
  // Slash: wind-up and hit.
  scene("012");
  put(0, g.boss.pos.x + 0.5f, g.boss.pos.z + 3.0f);
  boss_force_act(g, BA_SWEEP);
  step(g.boss.wind * 2 / 3);
  shot("02_fendente_preparazione");
  step(g.boss.wind - g.boss.act_t - 1);
  shot("03_fendente_quasi");
  step(2);
  shot("04_fendente_colpo");
  step(4);
  shot("05_fendente_dopo");
  // Charge: wind-up, run, into the wall and stunned.
  scene("012");
  put(0, g.boss.pos.x - 1.0f, g.boss.pos.z + 9.5f);
  boss_force_act(g, BA_RUSH);
  step(g.boss.wind * 3 / 4);
  shot("06_carica_preparazione");
  put(0, g.boss.pos.x - 4.0f, g.boss.pos.z + 8.0f);
  step(g.boss.wind - g.boss.act_t + 22);
  shot("07_carica_corsa");
  scene("012");
  put_boss(g.L.boss_pos.x, g.L.boss_pos.z + 2.0f, D_HALF_PI);
  put(0, g.boss.pos.x + 8.5f, g.boss.pos.z + 1.5f);
  put(1, g.boss.pos.x - 4.0f, g.boss.pos.z + 5.0f);
  put(2, g.boss.pos.x + 2.0f, g.boss.pos.z + 7.0f);
  boss_force_act(g, BA_RUSH);
  step(g.boss.wind / 2);
  shot("08_carica_verso_la_colonna");
  for (int k = 0; k < 200 && g.boss.act == BA_RUSH; ++k) step();
  step(2);
  shot("09_carica_urto");
  step(30);
  shot("10_stordito");
  step(30);
  shot("11_stordito_dopo");
  // Javelins (last phase: three), ground slam, roar, reinforcements, defeated.
  scene("012");
  g.boss.stage = 2;
  put(0, g.boss.pos.x + 2.0f, g.boss.pos.z + 10.0f);
  boss_force_act(g, BA_JAVELIN);
  step(g.boss.wind - 6);
  shot("12_giavellotti_carica");
  step(20);
  shot("13_giavellotti_in_volo");
  scene("012");
  put(0, g.boss.pos.x + 1.0f, g.boss.pos.z + 7.0f);
  boss_force_act(g, BA_SLAM);
  step(g.boss.wind + 4);
  shot("14_spaccaterra_colpo");
  step(26);
  shot("15_spaccaterra_crepa");
  step(30);
  shot("16_spaccaterra_pietre");
  scene("012");
  g.boss.hp = g.boss.hp_max * 0.6f;
  step(30);
  shot("17_ruggito");
  step(40 + 18);
  shot("18_rinforzi");
  scene("012");
  g.boss.hp = 0.0f;
  step(12);
  shot("19_sconfitto");
  step(24);
  shot("20_sconfitto_cade");
}

int main(int argc, char** argv) {
  game_set_events(g, view_events(&v));
  plat_log_open("/dev/null");
  int per = 0, lag = 0;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--immagini") && i + 1 < argc) g_shots = argv[++i];
    else if (!strcmp(argv[i], "--lotte")) per = (i + 1 < argc && argv[i + 1][0] != '-') ? atoi(argv[++i]) : 1;
    else if (!strcmp(argv[i], "--ritardo") && i + 1 < argc) lag = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--traccia") && i + 1 < argc) g_trace = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--solo") && i + 1 < argc) g_only = argv[++i];
  }
  if (g_shots) {
    mkdir(g_shots, 0755);
    swr_init(640, 480);
  }
  if (!mkdtemp(g_base)) {
    printf("mkdtemp non riuscita\n");
    return 1;
  }
  open_game(save_dir("prove"));
  game_set_cards_enabled(g, false);
  game_init(g);
  if (boss_map() < 0) {
    printf("manca l'arena del boss\n");
    return 1;
  }
  // --solo: only the fights with those heroes, no tests
  const int failed = g_only ? 0 : run_tests(argc, argv);
  if (g_shots) pictures();
  if (per > 0) fights(per, lag);
  game_shutdown(g);
  if (g_shots) swr_shutdown();
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd) != 0) printf("  (non riesco a cancellare %s)\n", g_base);
  return failed;
}
