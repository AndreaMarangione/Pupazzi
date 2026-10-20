// Prove delle regole (0.5): vite (da soli: zona rifatta, finite le vite mappa da capo; in piu'
// giocatori rientro dopo 5 secondi, senza vite fuori, tutti a terra insieme mappa da capo), chi
// esce e rientra, VITA EXTRA, terza stella, giri dei pianeti aperti con le stelle, difficolta' del
// giro, mappe aperte per pianeta e salvataggio della 0.4. Usa il gioco senza frontend (come
// test_aim) e un salvataggio in una cartella temporanea.
//   make build/native/test_rules && ./build/native/test_rules [--immagini CARTELLA]
// Con --immagini scrive anche le schermate dell'HUD (PPM a 640x480, renderer software): icone
// dei giocatori con uno a terra e uno senza vite, arena, menu delle mappe, fine dell'arena.
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
static char g_base[64] = "/tmp/pupazzi_test_rules_XXXXXX";
static char g_lastmsg[160];

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static void step(int n = 1) {
  for (int k = 0; k < n; ++k) {
    game_update(g_pads);
    if (g.msg_t > 0) snprintf(g_lastmsg, sizeof(g_lastmsg), "%s / %s", g.msg, g.msg2);
  }
}

static void press(int i, int b) {
  memset(g_pads, 0, sizeof(g_pads));
  step();
  g_pads[i].btn = (uint16_t)(1u << b);
  step();
  memset(g_pads, 0, sizeof(g_pads));
}

// Niente mostri e niente ondate: le cadute le decide la prova.
static void quiet(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.spawn_wait = 1000000;
  monsters_clear();
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
}

// Un colpo che lo manda a terra, poi un fotogramma: il gioco vede chi e' a terra (e se lo sono
// tutti comincia la ripartenza).
static void kill_player(int i) {
  Player& p = g.pl[i];
  p.invuln = 0;
  p.shield = false;
  p.leap_n = 0;
  hurt_player(i, 100000.0f, p.pos + v3(1, 0, 0), 0.0f);
  step();
}

// La zona in corso ripulita (resta da andare avanti, o la mappa e' finita).
static void clear_zone(void) {
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.wave = g.L.zones[g.zone].nwaves - 1;
  g.spawn_wait = 0;
  monsters_clear();
  g.boss.alive = false;
  for (int k = 0; k < 10 && g.phase != PH_CLEAR && g.phase != PH_MAP_DONE; ++k) step();
}

// Avanti fino al combattimento della zona dopo (i giocatori in piedi portati alla sua partenza).
static bool enter_next(void) {
  const int z = g.zone + 1;
  clear_zone();
  if (g.phase != PH_CLEAR) return false;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.pl[i].active && g.pl[i].alive) g.pl[i].pos = g.L.zones[z].start[i];
  for (int k = 0; k < 400 && !(g.phase == PH_FIGHT && g.zone == z); ++k) step();
  quiet();
  return g.phase == PH_FIGHT && g.zone == z;
}

// Tutte le zone, fino alla festa di fine mappa.
static bool complete_map(void) {
  for (int k = 0; k < 8 && g.phase != PH_MAP_DONE; ++k) {
    if (g.zone + 1 < g.L.nzones) {
      if (!enter_next()) return false;
    } else {
      clear_zone();
    }
  }
  return g.phase == PH_MAP_DONE;
}

static int boss_map(void) {
  for (int i = 0; i < maps_count(); ++i)
    if (maps_world(i) == WORLD_TERRA && maps_is_boss(i)) return i;
  return -1;
}

// Come nel gioco: le impostazioni, poi la partita 1 (caricata o, se non c'e', nuova).
static void open_game(const char* dir) {
  save_open(dir);
  if (!save_slot_load(0)) save_slot_new(0);
}

static bool file_has(const char* path, const char* what) {
  static char buf[32768];
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = 0;
  return strstr(buf, what) != NULL;
}

static const char* save_dir(const char* name) {
  static char path[4][160];
  static int k = 0;
  char* p = path[k++ & 3];
  snprintf(p, 160, "%s/%s", g_base, name);
  mkdir(p, 0755);
  return p;
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
  printf("  (immagine %s)\n", path);
}

static void fresh(int map, int players, const char* chars) {
  game_init();
  memset(g_pads, 0, sizeof(g_pads));
  game_quick_start(map, players, chars);
  step(3);
  quiet();
}

int main(int argc, char** argv) {
  plat_log_open("/dev/null");
  for (int i = 1; i + 1 < argc; ++i)
    if (!strcmp(argv[i], "--immagini")) g_shots = argv[i + 1];
  if (g_shots) {
    mkdir(g_shots, 0755);
    swr_init(640, 480);
  }
  if (!mkdtemp(g_base)) {
    printf("mkdtemp non riuscita\n");
    return 1;
  }
  game_set_cards_enabled(false);
  char buf[256];

  printf("1) da soli: una caduta costa una vita e si rifa' la zona\n");
  {
    open_game(save_dir("uno"));
    fresh(0, 1, "0");
    Player& p = g.pl[0];
    check(p.lives == START_LIVES && !p.out, "si parte con 3 vite");
    const bool in2 = enter_next();
    const int z = g.zone;
    p.pos = g.L.zones[z].start[0] + v3(1.5f, 0, -3.0f);
    p.hp = 10.0f;
    kill_player(0);
    snprintf(buf, sizeof(buf), "caduta nella zona %d: vite %d, schermata \"%s\"", z + 1, p.lives, g.msg);
    check(in2 && z == 1 && p.lives == 2 && !p.alive && !p.out && g.phase == PH_WIPE && !g.wipe_map && !strcmp(g.msg, "VITA PERSA!"), buf);
    step(WIPE_ZONE_FRAMES - 1);
    const bool waiting = g.phase == PH_WIPE;
    step();
    int pending = 0;
    for (int k = 0; k < MO_NTYPES; ++k) pending += g.spawn_left[k];
    snprintf(buf, sizeof(buf), "dopo %d fotogrammi si riparte dalla partenza della zona %d con la vita piena (%s)", WIPE_ZONE_FRAMES, g.zone + 1,
             g_lastmsg);
    check(waiting && g.phase == PH_FIGHT && g.zone == z && p.alive && p.hp == p.hp_max && dist2d(p.pos, g.L.zones[z].start[0]) < 0.3f &&
              pending > 0 && count_monsters() == 0 && !strcmp(g.msg, "SI RIPARTE!"),
          buf);
    check(g.wipes == 1 && g.map_wipes == 0 && g.restarts == 0 && p.deaths == 1, "zona rifatta: non conta come mappa da capo");

    printf("2) da soli: finite le vite la mappa ricomincia da capo\n");
    quiet();
    kill_player(0);
    step(WIPE_ZONE_FRAMES);
    quiet();
    check(p.lives == 1 && p.alive && g.zone == z, "seconda caduta: ultima vita, stessa zona");
    g.explorer_free = true;
    if (g.L.capsule_dyn >= 0) g.L.dyn[g.L.capsule_dyn].hp = 0.0f;
    kill_player(0);
    snprintf(buf, sizeof(buf), "terza caduta: senza vite, \"%s / %s\"", g.msg, g.msg2);
    check(p.lives == 0 && p.out && g.phase == PH_WIPE && g.wipe_map && !strcmp(g.msg, "VITE FINITE!"), buf);
    step(WIPE_MAP_FRAMES);
    const bool capsule = g.L.capsule_dyn < 0 || g.L.dyn[g.L.capsule_dyn].hp == CAPSULE_HP;
    snprintf(buf, sizeof(buf), "mappa da capo: zona %d, vite %d, \"%s / %s\"", g.zone + 1, p.lives, g.msg, g.msg2);
    check(g.zone == 0 && g.phase == PH_FIGHT && p.alive && !p.out && p.lives == START_LIVES && !g.explorer_free && capsule &&
              !strcmp(g.msg, "SI RICOMINCIA!"),
          buf);
    check(g.map_wipes == 1 && g.restarts == 1 && p.deaths == 3, "conta per la terza stella; le cadute della mappa restano");
    // Finita dopo la mappa da capo: niente terza stella.
    quiet();
    const bool done = complete_map();
    snprintf(buf, sizeof(buf), "finita dopo averla ricominciata: stelle %d (senza la terza)", g.stars_got);
    check(done && (g.stars_got & SAVE_STAR_DONE) && !(g.stars_got & SAVE_STAR_NO_WIPE), buf);
  }

  printf("3) terza stella: da soli si puo' cadere, basta non finire le vite\n");
  {
    open_game(save_dir("tre"));
    fresh(0, 1, "0");
    kill_player(0);
    step(WIPE_ZONE_FRAMES);
    quiet();
    kill_player(0);
    step(WIPE_ZONE_FRAMES);
    quiet();
    const bool done = complete_map();
    snprintf(buf, sizeof(buf), "due cadute (vite %d), mappa finita: stelle %d", g.pl[0].lives, g.stars_got);
    check(done && g.pl[0].lives == 1 && (g.stars_got & SAVE_STAR_NO_WIPE) && g.map_wipes == 0 && g.wipes == 2, buf);
    if (g_shots) {
      step(330);
      shot("fine_mappa");
    }
  }

  printf("4) VITA EXTRA: una vita in piu' subito\n");
  {
    open_game(save_dir("quattro"));
    game_set_cards_enabled(true);
    game_init();
    memset(g_pads, 0, sizeof(g_pads));
    game_quick_start(0, 1, "0");
    const bool open = g.cards_open;
    g.offer[0].card[g.offer[0].sel] = CA_VITA;
    press(0, PAD_A);
    step(CARDS_CLOSE_FRAMES + 2);
    quiet();
    Player& p = g.pl[0];
    check(open && !g.cards_open && has_card(p, CA_VITA) && p.lives == START_LIVES + 1, "presa all'inizio: 4 vite");
    int zone_restarts = 0;
    for (int k = 0; k < 3; ++k) {
      kill_player(0);
      zone_restarts += g.phase == PH_WIPE && !g.wipe_map;
      step(WIPE_ZONE_FRAMES);
      quiet();
    }
    kill_player(0);
    const bool map_again = g.phase == PH_WIPE && g.wipe_map;
    step(WIPE_MAP_FRAMES);
    snprintf(buf, sizeof(buf), "tre zone rifatte (%d), alla quarta caduta mappa da capo; poi vite %d e carte di nuovo da scegliere",
             zone_restarts, p.lives);
    check(zone_restarts == 3 && map_again && p.lives == START_LIVES && !has_card(p, CA_VITA) && g.cards_open, buf);
    game_set_cards_enabled(false);
  }

  printf("5) in piu' giocatori: si rientra dopo 5 secondi accanto a un compagno\n");
  {
    open_game(save_dir("cinque"));
    fresh(0, 2, "01");
    Player& a = g.pl[0];
    Player& b = g.pl[1];
    kill_player(1);
    snprintf(buf, sizeof(buf), "G2 a terra: vite %d, \"%s / %s\"", b.lives, g.msg, g.msg2);
    check(b.lives == 2 && !b.alive && !b.out && b.respawn == RESPAWN_FRAMES - 1 && g.phase == PH_FIGHT && !strcmp(g.msg, "G2 A TERRA!"), buf);
    step(RESPAWN_FRAMES - 2);
    const bool still = !b.alive;
    step();
    snprintf(buf, sizeof(buf), "rientra dopo %d fotogrammi a %.1f m da G1 con %.0f di vita", RESPAWN_FRAMES, (double)dist2d(a.pos, b.pos),
             (double)b.hp);
    check(still && b.alive && dist2d(a.pos, b.pos) < 2.0f && b.hp == b.hp_max * RESPAWN_HP && b.lives == 2, buf);

    printf("6) senza vite si resta fuori, ignorati da telecamera, guinzaglio, mostri e carte\n");
    for (int k = 0; k < 2; ++k) {
      quiet();
      kill_player(1);
      if (!b.out) step(RESPAWN_FRAMES);
    }
    snprintf(buf, sizeof(buf), "terza caduta di G2: \"%s / %s\"", g.msg, g.msg2);
    check(b.out && b.lives == 0 && b.respawn == 0 && !strcmp(g.msg, "G2 SENZA VITE!") && g.phase == PH_FIGHT, buf);
    step(RESPAWN_FRAMES + 60);
    check(!b.alive && b.out && b.down > OUT_VANISH && a.alive && g.phase == PH_FIGHT, "non rientra piu' (e sparisce)");
    // Il corpo lontano non trattiene G1 e non sposta la telecamera; un mostro accanto lo ignora.
    const ZoneDef& zd = g.L.zones[g.zone];
    const Vec3 mid = v3(0.5f * (zd.wx0 + zd.wx1), 0.0f, 0.5f * (zd.wz0 + zd.wz1));
    b.pos = v3(a.pos.x < mid.x ? a.pos.x - SPAN_X - 4.0f : a.pos.x + SPAN_X + 4.0f, 0.0f, a.pos.z);
    const Vec3 a0 = a.pos;
    g_pads[0].lx = a.pos.x < mid.x ? 1.0f : -1.0f;
    step(60);
    memset(g_pads, 0, sizeof(g_pads));
    const float moved = dist2d(a.pos, a0);
    step(120);
    const float cam_off = fabsf(g.cam_target.x - a.pos.x);
    quiet();
    Vec3 near_b = g.L.zones[g.zone].start[1];
    b.pos = near_b;
    const int k = monster_spawn(MO_SCORPIONE, false, &near_b) ? 0 : -1;
    int mk = -1;
    for (int j = 0; j < MAX_MONSTERS; ++j)
      if (g.mo[j].alive) mk = j;
    bool targeted_b = false;
    for (int f = 0; f < 90 && mk >= 0; ++f) {
      step();
      a.invuln = 100;
      targeted_b |= g.mo[mk].alive && g.mo[mk].target == 1;
    }
    quiet();
    snprintf(buf, sizeof(buf), "G1 si allontana dal corpo di G2 (%.1f m), telecamera su G1 (%.1f m), il mostro non lo cerca", (double)moved,
             (double)cam_off);
    check(k == 0 && moved > 3.0f && cam_off < 8.0f && !targeted_b, buf);
    game_set_cards_enabled(true);
    cards_offer();
    check(g.cards_open && g.offer[0].on && !g.offer[1].on, "carte: solo per chi ha ancora vite");
    g.cards_open = false;
    game_set_cards_enabled(false);
    check(count_in_play() == 1 && count_active() == 2, "conta come giocatore attivo (statistiche) ma non in gioco");

    printf("7) senza vite e l'ultimo in piedi cade: la mappa ricomincia da capo\n");
    a.hp = 1.0f;
    kill_player(0);
    snprintf(buf, sizeof(buf), "\"%s / %s\"", g.msg, g.msg2);
    check(g.phase == PH_WIPE && g.wipe_map && !strcmp(g.msg, "TUTTI A TERRA!"), buf);
    step(WIPE_MAP_FRAMES);
    check(g.zone == 0 && a.alive && b.alive && !b.out && a.lives == START_LIVES && b.lives == START_LIVES && g.map_wipes == 1,
          "tutti di nuovo in gioco con 3 vite");
  }

  printf("8) in piu' giocatori, a terra tutti insieme anche con vite: mappa da capo\n");
  {
    open_game(save_dir("otto"));
    fresh(0, 3, "012");
    enter_next();
    kill_player(0);
    kill_player(2);
    const bool on = g.phase == PH_FIGHT;
    kill_player(1);
    snprintf(buf, sizeof(buf), "vite %d %d %d, \"%s / %s\"", g.pl[0].lives, g.pl[1].lives, g.pl[2].lives, g.msg, g.msg2);
    check(on && g.phase == PH_WIPE && g.wipe_map && g.pl[1].lives == 2 && !strcmp(g.msg, "TUTTI A TERRA!"), buf);
    step(WIPE_MAP_FRAMES);
    check(g.zone == 0 && count_alive() == 3 && g.map_wipes == 1 && g.pl[0].deaths == 1, "mappa da capo dalla zona 1, tutti in piedi");
  }

  printf("9) chi esce e rientra nella stessa mappa ha le vite che aveva\n");
  {
    open_game(save_dir("nove"));
    fresh(0, 2, "01");
    Player& b = g.pl[1];
    kill_player(1);
    step(RESPAWN_FRAMES);
    quiet();
    player_leave(1);
    player_join(1, 1, true);
    check(b.active && b.alive && b.lives == 2, "uscito con 2 vite, rientra con 2");
    kill_player(1);
    step(RESPAWN_FRAMES);
    quiet();
    kill_player(1);
    player_leave(1);
    player_join(1, 1, true);
    check(b.active && !b.alive && b.out && b.lives == 0 && g.phase == PH_FIGHT, "senza vite: rientra ancora fuori");
    player_join(2, 2, true);
    check(g.pl[2].lives == START_LIVES && g.pl[2].alive, "un giocatore nuovo entra con 3 vite");
    player_leave(2);
    // G2 esce: G1 resta da solo e quando cade valgono le regole di chi gioca da solo.
    player_leave(1);
    kill_player(0);
    step();
    snprintf(buf, sizeof(buf), "rimasto da solo: zona rifatta (vite %d)", g.pl[0].lives);
    check(g.phase == PH_WIPE && !g.wipe_map && g.pl[0].lives == 2, buf);
    step(WIPE_ZONE_FRAMES);
    quiet();
    check(g.pl[0].alive && g.pl[0].lives == 2, "e si riparte con le vite che aveva");
    // G1 a terra che aspetta di rientrare, G3 (l'unico in piedi) esce: G1 resta da solo e si rifa'
    // la zona senza perdere un'altra vita.
    player_join(2, 2, true);
    kill_player(0);
    const bool waits = g.phase == PH_FIGHT && !g.pl[0].alive && g.pl[0].respawn > 0 && g.pl[0].lives == 1;
    player_leave(2);
    step();
    snprintf(buf, sizeof(buf), "G1 a terra (1 vita) e G3 esce: zona rifatta, vite %d", g.pl[0].lives);
    check(waits && g.phase == PH_WIPE && !g.wipe_map && g.pl[0].lives == 1, buf);
    step(WIPE_ZONE_FRAMES);
    quiet();
    // Mappa da capo (vite finite di G1): chi rientra dopo ha di nuovo 3 vite.
    kill_player(0);
    step(WIPE_MAP_FRAMES + 1);
    player_join(1, 1, true);
    check(g.pl[0].lives == START_LIVES && b.lives == START_LIVES && b.alive && !b.out, "dopo la mappa da capo si rientra con 3 vite");
  }

  printf("9b) chi esce e rientra: VITA EXTRA non si riprende, le statistiche restano\n");
  {
    open_game(save_dir("noveb"));
    fresh(0, 2, "01");
    Player& b = g.pl[1];
    b.cards |= 1u << CA_VITA;          // presa all'inizio della mappa
    b.lives = START_LIVES + 1;
    b.kills = 7;
    b.deaths = 1;
    b.dmg_done = 250.0f;
    player_leave(1);
    player_join(1, 1, true);
    check(b.lives == START_LIVES + 1 && has_card(b, CA_VITA), "rientra con 4 vite e VITA EXTRA gia' presa (non torna tra le carte)");
    check(b.kills == 7 && b.deaths == 1 && b.dmg_done == 250.0f, "e con le sue statistiche della mappa");
    // Mappa da capo mentre e' fuori: rientra con 3 vite, VITA EXTRA di nuovo possibile, statistiche
    // della visita tenute.
    player_leave(1);
    g.pl[0].lives = 1;
    kill_player(0);
    step(WIPE_MAP_FRAMES + 1);
    player_join(1, 1, true);
    check(b.lives == START_LIVES && !has_card(b, CA_VITA) && b.kills == 7, "dopo la mappa da capo: 3 vite, VITA EXTRA di nuovo possibile");
  }

  printf("9c) boss battuto mentre cade l'ultimo giocatore: la mappa e' vinta\n");
  {
    open_game(save_dir("novec"));
    const int bm = boss_map();
    fresh(bm, 1, "3");
    g.spawn_wait = 0;
    g.boss.hp = 30.0f;
    // La bolla di lava di Zolfo arriva sul boss nello stesso fotogramma in cui Zolfo cade (i colpi
    // si muovono dopo il turno del boss: il boss senza vita cade al fotogramma dopo).
    Shot* s = shot_new(SK_LAVA, 0, g.boss.pos + v3(0, 1.0f, 0), v3(0, -1.0f, 0));
    check(s != NULL, "bolla di lava sul boss");
    if (s) {
      s->dmg = 500.0f;
      s->r = 0.22f;
      s->special = true;
      s->dest = g.boss.pos;
    }
    Player& p = g.pl[0];
    p.invuln = 0;
    p.shield = false;
    hurt_player(0, 1e6f, p.pos + v3(1, 0, 0), 0.0f);
    step(3);
    snprintf(buf, sizeof(buf), "il gladiatore cade, mappa finita, niente ripartenza (fase %d, boss %d)", g.phase, (int)g.boss.alive);
    check(!g.boss.alive && g.phase == PH_MAP_DONE && g.map_wipes == 0, buf);
    // Tutti a terra (in due): un colpo che arriva durante il conto alla rovescia non batte il boss,
    // che poi tornerebbe in piedi con la mappa da capo.
    fresh(bm, 2, "01");
    g.boss.hp = 5.0f;
    kill_player(0);
    kill_player(1);
    damage_boss(100.0f, 0, false);
    step();
    check(g.phase == PH_WIPE && g.boss.alive && g.boss.hp > 0.0f, "tutti a terra: un colpo arrivato dopo non batte il boss");
  }

  printf("10) giri: dopo il boss con almeno 10 stelle si apre il giro dopo\n");
  {
    const char* dir = save_dir("dieci");
    open_game(dir);
    const int bm = boss_map();
    check(bm >= 0 && world_maps(WORLD_TERRA) == 4 && world_giro(WORLD_TERRA) == 1, "la Terra ha 4 mappe, giro 1");
    save_set_stars(maps_id(0), 7);
    save_set_stars(maps_id(1), 7);
    save_set_stars(maps_id(2), 3);
    save_data()->maps_open[WORLD_TERRA] = 4;
    fresh(bm, 2, "23");
    const bool done = complete_map();
    snprintf(buf, sizeof(buf), "8 stelle + 2 del boss: giro %d, stelle della Terra %d, giro aperto %d", world_giro(WORLD_TERRA),
             world_stars(WORLD_TERRA), (int)g.giro_up);
    check(done && g.giro_done && g.giro_up && g.giro_was == 1 && g.giro_stars == 10 && world_giro(WORLD_TERRA) == 2 &&
              world_stars(WORLD_TERRA) == 0,
          buf);
    char path[256], key[64];
    snprintf(path, sizeof(path), "%s/partita1.sav", dir);
    snprintf(key, sizeof(key), "\nstelle.%s=0\n", maps_id(0));
    check(file_has(path, "\ngiro.terra=2\n") && file_has(path, key) && file_has(path, "\nmappe.luna=1\n"),
          "salvato: giro.terra=2, stelle da zero, la Luna aperta");
    check(g.stars_got == (SAVE_STAR_DONE | SAVE_STAR_NO_WIPE), "le stelle di questa volta restano da mostrare");
    step(210);
    check(g.screen == SC_STATS, "schermata di fine mappa");
    step(120);
    shot("fine_arena_giro_aperto");
    press(0, PAD_A);
    check(g.screen == SC_CHARS && g.map_sel == world_first_map(WORLD_TERRA), "A: si torna alla prima mappa della Terra");
    // Difficolta': la mappa dopo comincia al giro 2.
    fresh(0, 1, "0");
    Vec3 at = g.L.zones[0].start[0] + v3(0, 0, -5.0f);
    monster_spawn(MO_SCORPIONE, false, &at);
    int mk = -1;
    for (int j = 0; j < MAX_MONSTERS; ++j)
      if (g.mo[j].alive) mk = j;
    snprintf(buf, sizeof(buf), "giro 2: mostri con %.2f volte la vita (sciacallo %.0f)", (double)diff_hp(), mk >= 0 ? (double)g.mo[mk].hp_max : 0.0);
    check(g.loop == 1 && mk >= 0 && g.mo[mk].hp_max == MONS[MO_SCORPIONE].hp * 1.6f && diff_dmg() == 1.25f, buf);
    if (g_shots) {
      fresh(bm, 4, "0123");
      kill_player(1);
      for (int k = 0; k < 3; ++k) {
        kill_player(2);
        if (!g.pl[2].out) step(RESPAWN_FRAMES);
        quiet();
      }
      kill_player(1);
      step(90);
      printf("  arena: telecamera a %.1f m\n", (double)g.cam_dist);
      shot("arena_4_giocatori");
    }
  }

  printf("11) giri: con meno di 10 stelle no (e l'esploratore salvato alla fine puo' fare la decima)\n");
  {
    const char* dir = save_dir("undici");
    open_game(dir);
    const int bm = boss_map();
    save_set_stars(maps_id(0), 7);
    save_set_stars(maps_id(1), 3);
    save_set_stars(maps_id(2), 1);
    fresh(bm, 1, "1");
    const bool done = complete_map();
    snprintf(buf, sizeof(buf), "6 stelle + 2 del boss: giro %d, stelle %d", world_giro(WORLD_TERRA), world_stars(WORLD_TERRA));
    check(done && g.giro_done && !g.giro_up && g.giro_stars == 8 && world_giro(WORLD_TERRA) == 1 && world_stars(WORLD_TERRA) == 8, buf);
    step(330);
    shot("fine_arena_mancano_stelle");
    save_set_stars(maps_id(1), 7);
    fresh(bm, 1, "1");
    complete_map();
    const bool nine = !g.giro_up && g.giro_stars == 9;
    if (g.L.capsule_dyn >= 0) {
      const DynCell& dc = g.L.dyn[g.L.capsule_dyn];
      explorer_rescue(cell_center(&g.L, dc.cx, dc.cz));
    }
    snprintf(buf, sizeof(buf), "9 stelle, poi l'esploratore durante la festa: giro %d", world_giro(WORLD_TERRA));
    check(g.L.capsule_dyn < 0 || (nine && g.giro_up && world_giro(WORLD_TERRA) == 2 && (g.stars_got & SAVE_STAR_EXPLORER) &&
                                   world_stars(WORLD_TERRA) == 0),
          buf);
  }

  printf("12) mappe aperte per pianeta; il salvataggio della 0.4 ricomincia dalla prima mappa\n");
  {
    const char* dir = save_dir("dodici");
    char path[256], old[256];
    snprintf(path, sizeof(path), "%s/partita1.sav", dir);
    snprintf(old, sizeof(old), "%s/pupazzi.sav", dir);       // il file della 0.4: diventa la partita 1
    FILE* f = fopen(old, "wb");
    if (f) {
      fputs("versione=1\nmappe_sbloccate=4\nstoria_vista=1\nstelle.prateria1=7\nstelle.prateria2=7\nstelle.prateria3=7\n"
            "stelle.prateria_boss=7\n", f);
      fclose(f);
    }
    open_game(dir);
    game_init();
    bool ok = map_unlocked(0) && world_giro(WORLD_TERRA) == 1;
    for (int i = 1; i < maps_count(); ++i) ok &= !map_unlocked(i);
    check(ok, "solo la prima mappa della Terra aperta, giro 1");
    if (strcmp(maps_id(0), "prateria1")) check(world_stars(WORLD_TERRA) == 0, "le stelle della prateria non contano per la Terra");
    else printf("  (le mappe sono ancora quelle della 0.4: le loro stelle contano)\n");
    fresh(0, 1, "0");
    complete_map();
    check(map_unlocked(1) && !map_unlocked(2) && save_data()->maps_open[WORLD_TERRA] == 2 && file_has(path, "\nmappe.terra=2\n") &&
              file_has(path, "\nmappe_sbloccate=4\n"),
          "finita la prima si apre la seconda (mappe.terra=2; mappe_sbloccate resta com'era)");
    check(!map_unlocked(-1) && !map_unlocked(maps_count()), "fuori dai limiti: chiusa");
    // Giro 3 dal salvataggio: si gioca al giro 3.
    save_data()->giro[WORLD_TERRA] = 3;
    fresh(0, 1, "0");
    check(g.loop == 2 && diff_hp() == 1.0f + 0.6f * 2.0f, "giro 3 nel salvataggio: mostri del giro 3");
    if (g_shots) {
      save_set_stars(maps_id(0), 7);
      save_set_stars(maps_id(1), 5);
      g.map_sel = 1;
      go_screen(SC_MAPS);
      step(30);
      shot("menu_mappe");
    }
  }

  printf("13) l'impronta della partita comprende vite e giocatori fuori\n");
  {
    fresh(0, 2, "01");
    const uint64_t h0 = game_state_hash();
    g.pl[1].lives--;
    const uint64_t h1 = game_state_hash();
    g.pl[1].lives++;
    g.pl[1].out = true;
    const uint64_t h2 = game_state_hash();
    g.pl[1].out = false;
    const int loop = g.loop;
    g.loop = loop + 1;
    const uint64_t h3 = game_state_hash();
    g.loop = loop;
    const uint64_t h4 = game_state_hash();
    snprintf(buf, sizeof(buf), "vite, senza vite e giro cambiano l'impronta (%d %d %d %d)", h1 != h0, h2 != h0, h3 != h0, h4 == h0);
    check(h1 != h0 && h2 != h0 && h3 != h0 && h4 == h0, buf);
  }

  printf("14) partita con due giocatori automatici: si ripete uguale\n");
  {
    uint64_t h[2] = { 0, 0 };
    int deaths[2] = { 0, 0 };
    for (int run = 0; run < 2; ++run) {
      open_game(save_dir(run ? "q2" : "q1"));
      game_set_cards_enabled(true);
      game_init();
      game_quick_start(0, 2, "03");
      for (int f = 0; f < 3000; ++f) {
        Pad pads[MAX_PLAYERS];
        memset(pads, 0, sizeof(pads));
        game_bot_pads(pads, 2);
        game_update(pads);
        if (f == 1500) {
          // Una caduta a meta': rientro e vite dentro la partita ripetuta.
          g.pl[1].invuln = 0;
          g.pl[1].shield = false;
          if (g.pl[1].alive && g.pl[1].leap_n == 0) hurt_player(1, 100000.0f, g.pl[1].pos, 0.0f);
        }
      }
      h[run] = game_state_hash();
      deaths[run] = g.deaths;
    }
    game_set_cards_enabled(false);
    snprintf(buf, sizeof(buf), "3000 fotogrammi, cadute %d: stessa impronta %016llx", deaths[0], (unsigned long long)h[0]);
    check(h[0] == h[1] && deaths[0] == deaths[1] && deaths[0] >= 1, buf);
  }

  if (g_shots) {
    printf("15) schermate dell'HUD\n");
    open_game(save_dir("immagini"));
    fresh(0, 1, "0");
    step(30);
    shot("hud_1_giocatore");
    fresh(0, 4, "0123");
    kill_player(1);
    for (int k = 0; k < 3; ++k) {
      kill_player(2);
      if (!g.pl[2].out) step(RESPAWN_FRAMES);
      quiet();
    }
    kill_player(1);
    step(110);
    shot("hud_4_giocatori");

    // Telecamera con il gruppo sparso nella zona 1 (ai bordi deve vedersi poco oltre la zona).
    printf("16) telecamera\n");
    fresh(0, 4, "0123");
    const ZoneDef& z = g.L.zones[0];
    const float x0 = z.wx0 + 1.0f, x1 = z.wx1 - 1.0f, zn = z.wz0 + 1.0f, zs = z.wz1 - 1.0f;
    const float xm = 0.5f * (x0 + x1), zm = 0.5f * (zn + zs);
    printf("  zona 1: %.0f x %.0f m\n", (double)(z.wx1 - z.wx0), (double)(z.wz1 - z.wz0));
    struct Spot { const char* name; float p[4][2]; } SPOTS[] = {
      { "cam_angolo_nordovest", { { x0, zn }, { x0 + 1.5f, zn }, { x0, zn + 1.5f }, { x0 + 1.5f, zn + 1.5f } } },
      { "cam_angolo_sudest", { { x1, zs }, { x1 - 1.5f, zs }, { x1, zs - 1.5f }, { x1 - 1.5f, zs - 1.5f } } },
      { "cam_largo_ovest_est", { { x0, zm }, { x1, zm }, { xm, zm }, { xm, zm + 1.0f } } },
      { "cam_lungo_nord_sud", { { xm, zn }, { xm, zs }, { xm + 1.0f, zm }, { xm - 1.0f, zm } } },
      { "cam_quattro_angoli", { { x0, zn }, { x1, zn }, { x0, zs }, { x1, zs } } },
    };
    for (const Spot& s : SPOTS) {
      for (int i = 0; i < 4; ++i) g.pl[i].pos = v3(s.p[i][0], 0.0f, s.p[i][1]);
      update_camera(true);
      printf("  %s: distanza %.1f m, punto inquadrato %.1f %.1f\n", s.name, (double)g.cam_dist, (double)g.cam_target.x,
             (double)g.cam_target.z);
      shot(s.name);
    }
  }

  game_shutdown();
  if (g_shots) swr_shutdown();
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd) != 0) printf("  (non riesco a cancellare %s)\n", g_base);
  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
