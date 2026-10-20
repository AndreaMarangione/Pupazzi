// Prove del Gladiatore gigante (0.5.1): insegue camminando e gira attorno agli ostacoli, fendente
// davanti e non dietro, carica evitata uscendo dalla corsia, carica contro un ostacolo (stordito e
// piu' danni), fasi con ruggito e rinforzi, boss rimesso a posto quando si ricomincia, mai bloccato
// con gli eroi che gli corrono attorno, la stessa partita due volte. Usa il gioco senza frontend
// (come test_rules) e un salvataggio in una cartella temporanea.
//   make build/native/test_boss && ./build/native/test_boss [--immagini CARTELLA] [--lotte [N]] [--ritardo F]
//                                                          [--solo PERSONAGGI] [--traccia N]
// --immagini: anche le schermate delle mosse (PPM 640x480, renderer software) in CARTELLA (e in
//   boss.txt dove sta il boss in ogni immagine, per ritagliarle).
// --lotte: anche partite intere contro il boss con 4, 2 e 1 giocatori automatici (N per gruppo,
//   di serie 1): quanto ci mettono, cadute, mappe da capo, attacchi. --ritardo F: nelle lotte i
//   giocatori automatici vedono i segni a terra F fotogrammi dopo che compaiono (come una persona).
//   --solo PERSONAGGI (per esempio 01): solo quella lotta, senza le prove. --traccia N: una riga
//   ogni N fotogrammi con boss e giocatori.
#include "game_int.h"
#include "maps.h"
#include "plat.h"
#include "save.h"
#include "swr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
static Pad g_pads[MAX_PLAYERS];
static const char* g_shots;             // --immagini
static char g_base[64] = "/tmp/pupazzi_test_boss_XXXXXX";
static char buf[512];
static int g_trace;                     // --traccia N: nelle lotte una riga ogni N fotogrammi
static const char* g_only;              // --solo PERSONAGGI: solo le lotte con questi personaggi (e salta le prove)

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static void step(int n = 1) {
  for (int k = 0; k < n; ++k) game_update(g_pads);
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

// Niente mostri, colpi e segni: restano gli eroi e il boss.
static void quiet(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  monsters_clear();
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
}

// L'arena con n giocatori (personaggi chars), senza carte; il boss non attacca da solo (pausa
// lunghissima) finche' la prova non lo vuole.
static void arena(int n, const char* chars) {
  game_init();
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(boss_map(), n, chars);
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

// Il cerchio di raggio r sta in (x, z) senza toccare ostacoli per chi cammina?
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

// Schermata del gioco come la vedrebbe il giocatore (640x480, renderer software).
static void shot(const char* name) {
  if (!g_shots) return;
  static Hud hud;
  FrameDesc fd{};
  game_build_frame(&fd, 640, 480, &hud);
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
  // Dove sta il boss nell'immagine (per ritagliarla): punti dello schermo virtuale 320x240.
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

// --- prove -----------------------------------------------------------------------------------

static void test_walk(void) {
  printf("1) cammina verso il bersaglio, piu' piano degli eroi, e gira attorno alle statue\n");
  arena(1, "0");
  const Vec3 spawn = g.L.boss_pos;
  put_boss(spawn.x, spawn.z, 0.0f);
  put(0, spawn.x, spawn.z + 12.5f);
  const float d0 = dist2d(g.boss.pos, g.pl[0].pos);
  step(90);
  const Vec3 a = g.boss.pos;
  step(30);
  const float v = dist2d(a, g.boss.pos) / (30.0f * DT), d1 = dist2d(g.boss.pos, g.pl[0].pos);
  const float face = fabsf(d_wrap_pi(g.boss.face - yaw_to(g.boss.pos, g.pl[0].pos)));
  snprintf(buf, sizeof(buf), "in 2 secondi da %.1f a %.1f m, a %.2f m/s (fase 1: %.1f), guardandolo (%.2f rad)", (double)d0, (double)d1,
           (double)v, (double)boss_speed(), (double)face);
  check(d0 - d1 > 3.6f && fabsf(v - 2.4f) < 0.15f && face < 0.15f && g.boss.act == BA_NONE, buf);
  g.boss.stage = 1;
  const float v1 = boss_speed();
  g.boss.stage = 2;
  const float v2 = boss_speed();
  g.boss.stage = 0;
  snprintf(buf, sizeof(buf), "piu' svelto a ogni fase: %.1f e %.1f m/s (gli eroi da 4,7 a 5)", (double)v1, (double)v2);
  check(v1 > 2.65f && v1 < 2.75f && v2 > 2.95f && v2 < 3.05f && v2 < CHARS[CH_MOCHI].speed, buf);
  // Il bersaglio si sposta di lato: il gladiatore si gira e lo segue.
  put(0, spawn.x + 9.0f, g.boss.pos.z + 1.0f);
  step(60);
  const float f2 = fabsf(d_wrap_pi(g.boss.face - yaw_to(g.boss.pos, g.pl[0].pos)));
  snprintf(buf, sizeof(buf), "il bersaglio va di lato: un secondo dopo lo guarda di nuovo (%.2f rad)", (double)f2);
  check(f2 < 0.3f, buf);
  // Dietro una statua (blocco di muro 2x2 staccato): la strada dritta e' chiusa, ci gira attorno.
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
  check(frames < 400 && still == 0, buf);
}

static void test_sweep(void) {
  printf("2) fendente: colpisce chi gli sta davanti, non chi sta dietro\n");
  arena(2, "01");
  const Vec3 c = v3(g.L.boss_pos.x - 4.5f, 0, g.L.boss_pos.z + 2.5f);
  check(fits(c.x, c.z, boss_r()) && fits(c.x + 0.3f, c.z + 2.6f, PL_R) && fits(c.x, c.z - 2.6f, PL_R), "posto libero per la prova");
  put_boss(c.x, c.z, 0.0f);
  put(0, c.x + 0.3f, c.z + 2.6f);     // davanti (verso sud)
  put(1, c.x, c.z - 2.6f);            // dietro
  g.boss.target = 0;
  g.boss.target_t = 0;
  const float h0 = g.pl[0].hp, h1 = g.pl[1].hp;
  check(boss_force_act(BA_SWEEP), "comincia il fendente");
  const int wind = g.boss.wind;
  const Marker* m = find_marker(MK_SWEEP);
  snprintf(buf, sizeof(buf), "settore rosso a terra verso il bersaglio (%d fotogrammi di preparazione)", wind);
  check(m && fabsf(d_wrap_pi(m->yaw - yaw_to(c, g.pl[0].pos))) < 0.01f && m->fire_at == wind && wind == 33 &&
            fabsf(m->r - BOSS_SWEEP_R) < 0.01f,
        buf);
  step(wind - 1);
  const bool before = g.pl[0].hp == h0 && g.boss.act == BA_SWEEP && dist2d(g.boss.pos, c) < 0.01f;
  const float z0 = g.pl[0].pos.z;
  step(1);
  snprintf(buf, sizeof(buf), "fermo durante la preparazione; al colpo: davanti %.0f -> %.0f, dietro %.0f -> %.0f", (double)h0, (double)g.pl[0].hp,
           (double)h1, (double)g.pl[1].hp);
  check(before && fabsf(h0 - g.pl[0].hp - 28.0f) < 0.01f && g.pl[1].hp == h1, buf);
  step(10);
  snprintf(buf, sizeof(buf), "chi e' colpito vola via (%.2f m)", (double)(g.pl[0].pos.z - z0));
  check(g.pl[0].pos.z - z0 > 0.5f, buf);
  // Di lato al limite del settore (75 gradi) si prende, a 110 gradi no; nell'ultima fase si prepara piu' in fretta.
  arena(2, "01");
  put_boss(c.x, c.z, 0.0f);
  put(0, c.x + 3.0f * d_sin(1.3f), c.z + 3.0f * d_cos(1.3f));
  put(1, c.x - 3.0f * d_sin(1.9f), c.z + 3.0f * d_cos(1.9f));
  g.boss.stage = 2;
  g.boss.target = 0;
  boss_force_act(BA_SWEEP);
  g.boss.act_yaw = 0.0f;              // il settore verso sud: uno a 75 gradi, l'altro a 110
  const int w2 = g.boss.wind;
  const float a0 = g.pl[0].hp, a1 = g.pl[1].hp;
  step(w2 + 1);
  snprintf(buf, sizeof(buf), "fase 3: preparazione di %d fotogrammi; a 75 gradi colpito (%.0f), a 110 no (%.0f)", w2, (double)(a0 - g.pl[0].hp),
           (double)(a1 - g.pl[1].hp));
  check(w2 == 24 && g.pl[0].hp < a0 && g.pl[1].hp == a1, buf);
}

static void test_rush(void) {
  printf("3) carica: chi resta nella corsia la prende e vola di lato, chi ne esce no\n");
  for (int dodge = 0; dodge < 2; ++dodge) {
    arena(1, "1");
    const Vec3 s = g.L.boss_pos;
    put_boss(s.x, s.z, 0.0f);
    put(0, s.x, s.z + 9.0f);
    g.boss.target = 0;
    const float h0 = g.pl[0].hp;
    boss_force_act(BA_RUSH);
    const Marker* m = find_marker(MK_LANE);
    const float lane = g.boss.lane;
    const int wind = g.boss.wind;
    if (!dodge) {
      snprintf(buf, sizeof(buf), "corsia rossa larga %.1f m e lunga %.1f m verso il bersaglio (preparazione %d fotogrammi, contro un ostacolo: %s)",
               m ? (double)(2.0f * m->r) : 0.0, m ? (double)m->len : 0.0, wind, g.boss.crash ? "si" : "no");
      check(m && fabsf(m->yaw) < 0.01f && m->r > 1.15f && m->r < 1.3f && lane > 9.0f && lane <= 15.0f && wind == 48, buf);
    }
    if (dodge) g_pads[0].lx = 1.0f;     // appena vede la corsia esce di lato
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
      check(fabsf(h0 - g.pl[0].hp - 25.0f) < 0.01f && fabsf(g.pl[0].pos.x - s.x) > 1.0f && fabsf(ran - lane) < 0.05f, buf);
      // Si e' fermato senza sbattere: poco dopo i giavellotti.
      int t = 0;
      for (; t < 120 && g.boss.act != BA_JAVELIN; ++t) step();
      snprintf(buf, sizeof(buf), "fermo senza sbattere: dopo %d fotogrammi lancia i giavellotti", t);
      check(g.boss.act == BA_JAVELIN && !g.boss.crash, buf);
    } else {
      snprintf(buf, sizeof(buf), "uscito di lato (a %.1f m dalla linea): nessun danno; lui corre %.1f m", (double)fabsf(g.pl[0].pos.x - s.x), (double)ran);
      check(g.pl[0].hp == h0 && fabsf(ran - lane) < 0.05f, buf);
    }
  }
}

static void test_crash(void) {
  printf("4) carica contro un ostacolo: stordito per 1,5 secondi, intanto i colpi fanno il 50%% in piu'\n");
  arena(1, "1");
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z, D_HALF_PI);
  put(0, s.x + 8.0f, s.z);            // verso est: la colonna o il muro dell'arena
  g.pl[0].invuln = 100000;
  g.boss.target = 0;
  boss_force_act(BA_RUSH);
  const Marker* m = find_marker(MK_LANE);
  const bool crash = g.boss.crash && m && m->crash;
  const float lane = g.boss.lane;
  int f = 0;
  for (; f < 300 && g.boss.act == BA_RUSH; ++f) step();
  const Vec3 ahead = g.boss.pos + dir_yaw(g.boss.face) * 0.3f;   // ancora un passo e toccherebbe l'ostacolo
  snprintf(buf, sizeof(buf), "la corsia finisce contro un ostacolo dopo %.1f m: dopo %d fotogrammi e' stordito (\"STORDITO!\")", (double)lane, f);
  check(crash && g.boss.act == BA_STUN && has_float("STORDITO!") && !fits(ahead.x, ahead.z, boss_r()), buf);
  const float hp0 = g.boss.hp;
  damage_boss(100.0f, 0, false);
  const float stun_dmg = hp0 - g.boss.hp;
  int t = 1;
  for (; t < 300 && g.boss.act == BA_STUN; ++t) step();
  const float hp1 = g.boss.hp;
  damage_boss(100.0f, 0, false);
  snprintf(buf, sizeof(buf), "stordito per %d fotogrammi: 100 di colpo ne tolgono %.0f, dopo %.0f", t, (double)stun_dmg, (double)(hp1 - g.boss.hp));
  check(t >= BOSS_STUN_FRAMES && t <= BOSS_STUN_FRAMES + 2 && fabsf(stun_dmg - 150.0f) < 0.01f && fabsf(hp1 - g.boss.hp - 100.0f) < 0.01f, buf);
}

static void test_stages(void) {
  printf("5) a 2/3 e a 1/3 della vita ruggisce (prende poco) e chiama i rinforzi, senza riempire l'arena\n");
  arena(2, "01");
  Boss& b = g.boss;
  b.hp = b.hp_max * 0.66f;
  step();
  snprintf(buf, sizeof(buf), "a 2/3 della vita: fase %d, ruggisce, \"%s\"", b.stage + 1, g.msg);
  check(b.stage == 1 && b.roar > 0 && !strcmp(g.msg, "SI ARRABBIA!"), buf);
  const float hp0 = b.hp;
  damage_boss(100.0f, 0, false);
  snprintf(buf, sizeof(buf), "mentre ruggisce 100 di colpo ne tolgono %.0f", (double)(hp0 - b.hp));
  check(fabsf(hp0 - b.hp - 25.0f) < 0.01f, buf);
  step(70);
  const bool calls = b.act == BA_SUMMON;
  step(40);
  const int n1 = count_monsters();
  snprintf(buf, sizeof(buf), "poi chiama i rinforzi: %d mostri dalle botole", n1);
  check(calls && n1 >= 3 && n1 <= 10, buf);
  // Altri mostri in campo: il massimo resta 10.
  for (int k = 0; k < 6; ++k) monster_spawn(MO_SCARABEO, false, 0);
  for (int k = 0; k < 400 && b.act != BA_NONE; ++k) step();
  b.cd = 1000000;
  b.hp = b.hp_max * 0.32f;
  step();
  const bool roar2 = b.stage == 2 && b.roar > 0 && !strcmp(g.msg, "È FURIOSO!");
  step(115);
  snprintf(buf, sizeof(buf), "a 1/3: fase %d (\"È FURIOSO!\"), mostri in campo %d (al massimo 10)", b.stage + 1, count_monsters());
  check(roar2 && count_monsters() <= 10, buf);
  // Un attacco a meta' non si interrompe: la fase cambia quando e' finito.
  arena(1, "0");
  put(0, g.boss.pos.x, g.boss.pos.z + 3.0f);
  g.boss.target = 0;
  boss_force_act(BA_SWEEP);
  g.boss.hp = g.boss.hp_max * 0.6f;
  step(5);
  const bool waits = g.boss.stage == 0 && g.boss.act == BA_SWEEP;
  for (int k = 0; k < 200 && g.boss.roar == 0; ++k) step();
  check(waits && g.boss.stage == 1 && g.boss.roar > 0, "con il fendente in corso ruggisce dopo averlo finito");
}

static void test_target(void) {
  printf("6) bersaglio: il piu' vicino, tenuto almeno 3 secondi; passa a chi lo picchia forte; giavellotti 1, 2, 3\n");
  arena(2, "01");
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z, 0.0f);
  put(0, s.x - 2.0f, s.z + 7.0f);
  put(1, s.x + 3.0f, s.z + 9.5f);
  g.boss.target = -1;                 // come appena comparso
  step(2);
  const bool first = g.boss.target == 0;
  put(1, s.x + 1.5f, g.boss.pos.z + 6.5f);      // un po' piu' vicino di G1 (che sta a circa 7 m)
  put(0, g.boss.pos.x - 2.0f, g.boss.pos.z + 7.0f);
  step(30);
  const bool kept = g.boss.target == 0;
  for (int k = 0; k < 200 && g.boss.target == 0; ++k) {
    put(0, g.boss.pos.x - 2.0f, g.boss.pos.z + 7.5f);
    put(1, g.boss.pos.x + 1.5f, g.boss.pos.z + 5.0f);
    step();
  }
  snprintf(buf, sizeof(buf), "prima G1 (il piu' vicino), tiene G1 con G2 appena piu' vicino, dopo 3 secondi passa a G2 (bersaglio G%d)", g.boss.target + 1);
  check(first && kept && g.boss.target == 1 && g.boss.target_t < 5, buf);
  // G1 lontano lo picchia forte: il gladiatore va da lui.
  step(70);
  for (int k = 0; k < 4; ++k) damage_boss(60.0f, 0, false);
  step(2);
  snprintf(buf, sizeof(buf), "G1 da lontano gli toglie 240 in un attimo: insegue G1 (bersaglio G%d)", g.boss.target + 1);
  check(g.boss.target == 0, buf);
  // Giavellotti: 1, 2 e 3 secondo la fase (con due eroi: uno a testa, il terzo di lato al bersaglio).
  int n[3];
  for (int st = 0; st < 3; ++st) {
    arena(2, "01");
    put_boss(s.x, s.z, 0.0f);
    put(0, s.x, s.z + 11.0f);
    put(1, s.x + 6.0f, s.z + 9.0f);
    g.boss.stage = st;
    g.boss.target = 0;
    boss_force_act(BA_JAVELIN);
    step(g.boss.wind + 1);
    n[st] = 0;
    for (int i = 0; i < MAX_SHOTS; ++i) n[st] += g.sh[i].alive && g.sh[i].kind == SK_JAVELIN;
  }
  snprintf(buf, sizeof(buf), "giavellotti per fase: %d, %d, %d (con il cerchio rosso dove cadono)", n[0], n[1], n[2]);
  check(n[0] == 1 && n[1] == 2 && n[2] == 3 && count_markers(MK_LOB) == 3, buf);
}

static void test_restart(void) {
  printf("7) si ricomincia: il gladiatore torna al suo posto con la vita piena\n");
  arena(1, "0");
  const Vec3 spawn = g.L.boss_pos;
  const ZoneDef& z = g.L.zones[g.zone];
  float dmin = 1e30f;
  for (int i = 0; i < MAX_PLAYERS; ++i) dmin = fminf(dmin, dist2d(spawn, z.start[i]));
  snprintf(buf, sizeof(buf), "parte dalla casella B, a %.1f m dalla partenza piu' vicina, dove ci sta", (double)dmin);
  check(dmin >= 4.0f && fits(spawn.x, spawn.z, boss_r()), buf);
  put_boss(spawn.x + 4.0f, spawn.z + 6.0f, 1.0f);
  g.boss.hp = g.boss.hp_max * 0.5f;
  g.boss.stage = 1;
  put(0, spawn.x + 4.0f, spawn.z + 9.0f);
  g.boss.target = 0;
  boss_force_act(BA_SWEEP);
  hurt_player(0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  step();
  const bool wipe = g.phase == PH_WIPE && !g.wipe_map;
  step(WIPE_ZONE_FRAMES);
  const Boss& b = g.boss;
  snprintf(buf, sizeof(buf), "da soli, zona rifatta: al suo posto (%.1f m), vita %.0f su %.0f, fase %d, nessun segno a terra", (double)dist2d(b.pos, spawn),
           (double)b.hp, (double)b.hp_max, b.stage + 1);
  check(wipe && g.phase == PH_FIGHT && b.alive && dist2d(b.pos, spawn) < 0.01f && b.hp == b.hp_max && b.stage == 0 && b.act == BA_NONE &&
            count_markers(MK_SWEEP) == 0 && count_markers(MK_LANE) == 0,
        buf);
  // In due: chi cade rientra dopo 5 secondi, il boss resta com'era.
  arena(2, "01");
  g.boss.hp = g.boss.hp_max * 0.8f;
  const float hp = g.boss.hp;
  put_boss(spawn.x - 3.0f, spawn.z + 5.0f, 0.0f);
  const Vec3 at = g.boss.pos;
  hurt_player(1, 1e6f, g.pl[1].pos + v3(1, 0, 0), 0.0f);
  step(RESPAWN_FRAMES + 2);
  snprintf(buf, sizeof(buf), "in due, G2 cade e rientra: il boss non torna al suo posto (%.1f m) e tiene la sua vita", (double)dist2d(g.boss.pos, spawn));
  check(g.pl[1].alive && g.boss.hp == hp && dist2d(g.boss.pos, spawn) > 2.0f && dist2d(g.boss.pos, at) < 8.0f, buf);
  // Tutti a terra: mappa da capo, boss da capo.
  g.pl[0].invuln = g.pl[1].invuln = 0;
  hurt_player(0, 1e6f, g.pl[0].pos + v3(1, 0, 0), 0.0f);
  hurt_player(1, 1e6f, g.pl[1].pos + v3(1, 0, 0), 0.0f);
  step();
  const bool map = g.phase == PH_WIPE && g.wipe_map;
  step(WIPE_MAP_FRAMES);
  snprintf(buf, sizeof(buf), "tutti a terra: mappa da capo, boss al suo posto con la vita piena (%.0f)", (double)g.boss.hp);
  check(map && g.phase == PH_FIGHT && g.boss.hp == g.boss.hp_max && dist2d(g.boss.pos, spawn) < 0.01f && g.map_wipes == 1, buf);
}

// Due eroi che corrono in cerchio attorno al centro dell'arena (invulnerabili, senza sparare): il
// gladiatore li insegue e attacca per n fotogrammi. Mai fermo a lungo mentre vuole camminare, mai
// dentro un ostacolo o fuori dalla zona.
static void test_kite(int n) {
  printf("8) %d fotogrammi con due eroi che gli corrono attorno: mai bloccato, mai dentro un ostacolo\n", n);
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
    if (f == n * 3 / 10) g.boss.hp = g.boss.hp_max * 0.6f;      // fasi e rinforzi lungo la prova
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
        if (dist2d(p.pos, last_p[i]) < 0.3f) jump[i] = 12;      // fermo contro qualcosa: salta
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
  check(stuck == 0 && inside == 0 && outside == 0, buf);
  snprintf(buf, sizeof(buf), "attacchi: fendenti %d, cariche %d (stordito %d), spaccaterra %d, giavellotti %d, rinforzi %d", acts[BA_SWEEP], acts[BA_RUSH],
           acts[BA_STUN], acts[BA_SLAM], acts[BA_JAVELIN], acts[BA_SUMMON]);
  check(acts[BA_SWEEP] > 0 && acts[BA_RUSH] > 0 && acts[BA_SLAM] > 0 && acts[BA_JAVELIN] > 0 && acts[BA_SUMMON] == 2, buf);
}

// Partita con n giocatori automatici dalla mappa del boss; restituisce l'impronta di tutti i fotogrammi.
static uint64_t bot_run(int n, const char* chars, int frames, int pre, uint64_t* last) {
  game_init();
  for (int k = 0; k < pre; ++k) {          // qualche fotogramma nel titolo: generatore casuale diverso
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_update(pads);
  }
  game_quick_start(boss_map(), n, chars);
  uint64_t h = 1469598103934665603ull;
  for (int f = 0; f < frames; ++f) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_bot_pads(pads, n);
    game_update(pads);
    h = (h ^ game_state_hash()) * 1099511628211ull;
  }
  if (last) *last = game_state_hash();
  return h;
}

static void test_determinism(void) {
  printf("9) la stessa lotta due volte: la stessa partita fotogramma per fotogramma\n");
  uint64_t h[2], e[2];
  for (int run = 0; run < 2; ++run) {
    save_open(save_dir(run ? "det2" : "det1"));
    game_set_cards_enabled(true);
    h[run] = bot_run(3, "023", 5000, 0, &e[run]);
  }
  game_set_cards_enabled(false);
  snprintf(buf, sizeof(buf), "5000 fotogrammi con 3 giocatori automatici: impronte %016llx e %016llx (ultima %016llx)", (unsigned long long)h[0],
           (unsigned long long)h[1], (unsigned long long)e[0]);
  check(h[0] == h[1] && e[0] == e[1], buf);
}

// --- lotte: partite intere con i giocatori automatici ---------------------------------------------

struct Fight {
  bool won;
  int frames, deaths, wipes, restarts, hits;
  int acts[BA_COUNT];
  float boss_hp;            // vita del boss alla fine (0 se battuto)
};

static Fight fight(int n, const char* chars, int max_frames, int pre) {
  Fight r;
  memset(&r, 0, sizeof(r));
  game_init();
  for (int k = 0; k < pre; ++k) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_update(pads);
  }
  game_quick_start(boss_map(), n, chars);
  int prev = BA_NONE;
  float hp[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; ++i) hp[i] = g.pl[i].hp;
  for (int f = 0; f < max_frames; ++f) {
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_bot_pads(pads, n);
    game_update(pads);
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
      printf(" mostri %d\n", count_monsters());
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
  bot_set_lag(lag);
  game_set_cards_enabled(true);
  for (const Group& gr : G) {
    if (g_only && strcmp(g_only, gr.chars)) continue;
    for (int k = 0; k < per; ++k) {
      save_open(save_dir("lotte"));
      const Fight r = fight(gr.n, gr.chars, gr.limit, 17 * k);
      printf("  %d giocatori (%s), prova %d: %s in %d fotogrammi (%.1f minuti), cadute %d, zone rifatte %d, mappe da capo %d, colpi presi %d;"
             " fendenti %d, cariche %d (stordito %d), spaccaterra %d, giavellotti %d%s\n",
             gr.n, gr.chars, k + 1, r.won ? "battuto" : "NON battuto", r.frames, (double)r.frames / 3600.0, r.deaths, r.wipes, r.restarts, r.hits,
             r.acts[BA_SWEEP], r.acts[BA_RUSH], r.acts[BA_STUN], r.acts[BA_SLAM], r.acts[BA_JAVELIN], r.won ? "" : " (limite)");
      fflush(stdout);
    }
  }
  bot_set_lag(0);
  game_set_cards_enabled(false);
}

// --- schermate --------------------------------------------------------------------------------

// Tre eroi attorno al boss in mezzo all'arena.
static void scene(const char* chars) {
  arena((int)strlen(chars), chars);
  const Vec3 s = g.L.boss_pos;
  put_boss(s.x, s.z + 3.0f, 0.0f);
  static const float P[4][2] = { { 0.0f, 6.5f }, { -5.5f, 4.5f }, { 5.0f, 5.5f }, { 4.5f, -2.5f } };
  for (int i = 0; i < (int)strlen(chars); ++i) put(i, s.x + P[i][0], s.z + 3.0f + P[i][1]);
  for (int i = 0; i < MAX_PLAYERS; ++i) g.pl[i].invuln = 100000;
  g.boss.target = 0;
  g.msg_t = 0;                       // niente scritta della mappa sopra
  update_camera(true);
}

static void pictures(void) {
  printf("schermate in %s\n", g_shots);
  // Camminata: quattro momenti del passo.
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
  // Fendente: preparazione e colpo.
  scene("012");
  put(0, g.boss.pos.x + 0.5f, g.boss.pos.z + 3.0f);
  boss_force_act(BA_SWEEP);
  step(g.boss.wind * 2 / 3);
  shot("02_fendente_preparazione");
  step(g.boss.wind - g.boss.act_t - 1);
  shot("03_fendente_quasi");
  step(2);
  shot("04_fendente_colpo");
  step(4);
  shot("05_fendente_dopo");
  // Carica: preparazione, corsa, contro il muro e stordito.
  scene("012");
  put(0, g.boss.pos.x - 1.0f, g.boss.pos.z + 9.5f);
  boss_force_act(BA_RUSH);
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
  boss_force_act(BA_RUSH);
  step(g.boss.wind / 2);
  shot("08_carica_verso_la_colonna");
  for (int k = 0; k < 200 && g.boss.act == BA_RUSH; ++k) step();
  step(2);
  shot("09_carica_urto");
  step(30);
  shot("10_stordito");
  step(30);
  shot("11_stordito_dopo");
  // Giavellotti (ultima fase: tre), spaccaterra, ruggito, rinforzi, sconfitto.
  scene("012");
  g.boss.stage = 2;
  put(0, g.boss.pos.x + 2.0f, g.boss.pos.z + 10.0f);
  boss_force_act(BA_JAVELIN);
  step(g.boss.wind - 6);
  shot("12_giavellotti_carica");
  step(20);
  shot("13_giavellotti_in_volo");
  scene("012");
  put(0, g.boss.pos.x + 1.0f, g.boss.pos.z + 7.0f);
  boss_force_act(BA_SLAM);
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
  save_open(save_dir("prove"));
  game_set_cards_enabled(false);
  game_init();
  if (boss_map() < 0) {
    printf("manca l'arena del boss\n");
    return 1;
  }
  if (!g_only) {
    test_walk();
    test_sweep();
    test_rush();
    test_crash();
    test_stages();
    test_target();
    test_restart();
    test_kite(20000);
    test_determinism();
  }
  if (g_shots) pictures();
  if (per > 0) fights(per, lag);
  game_shutdown();
  if (g_shots) swr_shutdown();
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd) != 0) printf("  (non riesco a cancellare %s)\n", g_base);
  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
