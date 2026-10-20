// Flusso del gioco: schermate, campagna, mappe, zone, ondate, cancelli, vite e mappe da capo,
// stelle e giri dei pianeti, telecamera.
#include "game_int.h"
#include "lang.h"
#include "maps.h"
#include "plat.h"
#include "save.h"
#include <stdio.h>
#include <string.h>

Game g;
const char* const WORLD_NAMES[NWORLDS] = { N_("TERRA"), N_("LUNA"), N_("MARTE"), N_("VENERE"), N_("MERCURIO"), N_("GIOVE"),
                                            N_("SATURNO"), N_("URANO"), N_("NETTUNO") };
static_assert(SAVE_WORLDS == NWORLDS, "save.h e level.h devono avere gli stessi pianeti");

namespace {

bool g_inited, g_quit;
bool g_settings_dirty;        // impostazioni della pausa da salvare
const float PITCH_PLAY = 0.98f, PITCH_MENU = 0.5f;
// In partita: da CAM_DIST_MIN (gruppo vicino) a CAM_DIST_MAX (gruppo largo); senza HDMI, sullo schermo
// piccolo della console, si parte piu' vicino (0.5.3). Prima: 0.5.1-0.5.2 da 22 a 30 per tutti; 0.5 da 15
// a 30; 0.3.2-0.4 da 15 a 21; fino alla 0.3.1 da 11 a 17.
const float CAM_DIST_MIN = 22.0f, CAM_DIST_MIN_LCD = 15.0f, CAM_DIST_MAX = 30.0f;
bool g_hdmi = true;
// Terreno inquadrato dalla telecamera di gioco a distanza d (inclinazione 0,98, campo 0,87 in
// game_draw.cpp, schermo 4:3), in metri per metro di distanza: verso nord (in alto) fino a 0,81 d
// dal punto inquadrato, verso sud 0,425 d; di lato, s metri a sud del punto inquadrato (s
// negativo: a nord), fino a 0,62 (d - 0,557 s) dal centro: 0,62 d all'altezza del punto
// inquadrato, 0,47 d in basso e 0,90 d in alto (l'inquadratura a terra e' un trapezio).
const float VIEW_N = 0.81f, VIEW_S = 0.425f, VIEW_W = 0.62f, VIEW_K = 0.557f;
// Chi gioca si deve vedere con le sue barre: di lato entro CAM_SAFE_X della mezza larghezza della
// sua riga (le barre sono larghe una ventina di punti su 160) meno CAM_MARGIN_X metri, i piedi
// CAM_MARGIN_S sopra il bordo in basso e in alto non oltre VIEW_NU d - CAM_MARGIN_N (testa e barre
// sotto la riga in alto).
const float CAM_SAFE_X = 0.875f, CAM_MARGIN_X = 0.4f, CAM_MARGIN_S = 1.0f, CAM_MARGIN_N = 2.5f, VIEW_NU = 0.6f;
// Ai bordi della zona la telecamera si ferma mostrando al massimo cosi' tanto oltre (in metri: di
// lato all'altezza del punto inquadrato, a nord, a sud), purche' si veda ancora chi gioca; quando la
// zona sta tutta nell'inquadratura a sud si mostra CAM_OVER_S e quello che avanza va a nord (in
// alto il terreno e' piu' schiacciato e c'e' la zona dopo), di lato si centra la zona.
const float CAM_OVER_X = 1.8f, CAM_OVER_N = 6.0f, CAM_OVER_S = 3.5f;

// Distanza minima con cui i punti p[] si vedono tutti, inquadrando (cx, cz).
float cam_need(const Vec3* p, int n, float cx, float cz) {
  float d = 0.0f;
  for (int k = 0; k < n; ++k) {
    const float a = fabsf(p[k].x - cx), s = p[k].z - cz;     // di lato; verso sud (negativo: nord)
    d = fmaxf(d, (a + CAM_MARGIN_X) / (CAM_SAFE_X * VIEW_W) + VIEW_K * s);
    d = fmaxf(d, (s + CAM_MARGIN_S) / VIEW_S);
    d = fmaxf(d, (CAM_MARGIN_N - s) / VIEW_NU);
  }
  return d;
}

// Mostri di un gruppo: piu' giocatori in gioco (chi e' senza vite non conta), piu' mostri; e
// di piu' a ogni giro.
int wave_size(int base) {
  if (base <= 0) return 0;
  const int np = count_in_play() > 0 ? count_in_play() : 1;
  const int n = (int)((float)base * (0.7f + 0.3f * (float)np) * (1.0f + 0.4f * (float)g.loop) + 0.5f);
  return n < 1 ? 1 : n;
}

void begin_wave(int w) {
  g.wave = w;
  const ZoneDef& zd = g.L.zones[g.zone];
  if (w >= zd.nwaves) return;
  const WaveDef& wd = zd.wave[w];
  for (int k = 0; k < wd.nkinds; ++k) g.spawn_left[wd.kind[k]] += wave_size(wd.count[k]);
  g.spawn_cd = 0;
}

void begin_zone(int z, bool entering) {
  g.zone = z;
  g.phase = entering ? PH_ENTER : PH_FIGHT;
  g.phase_t = 0;
  g.spawn_wait = 90;
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  begin_wave(0);
  boss_start();      // solo nell'ultima zona di un'arena; altrimenti nessun boss
  cards_zone_start();
  int n = 0;
  for (int k = 0; k < MO_NTYPES; ++k) n += g.spawn_left[k];
  plat_log("mappa %d (%s), zona %d: %d mostri nella prima ondata", g.map_idx + 1, g.L.name, z + 1, n);
}

int zone_total_left(void) {
  int n = count_monsters();
  for (int k = 0; k < MO_NTYPES; ++k) n += g.spawn_left[k];
  const ZoneDef& zd = g.L.zones[g.zone];
  for (int w = g.wave + 1; w < zd.nwaves; ++w)
    for (int k = 0; k < zd.wave[w].nkinds; ++k) n += wave_size(zd.wave[w].count[k]);
  return n;
}

void clear_actors(void) {
  monsters_clear();
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
}

// Da soli, caduti con vite rimaste: la zona ricomincia (mostri e oggetti della zona da capo,
// vita piena alla partenza della zona).
void restart_zone(void) {
  clear_actors();
  items_reset(g.zone);
  level_reset_dynamic(&g.L, g.zone);
  if (g.explorer_free && g.L.capsule_dyn >= 0) g.L.dyn[g.L.capsule_dyn].hp = 0.0f;   // l'esploratore resta salvo
  for (int i = 0; i < g.L.ngates; ++i) g.gate_want[i] = g.L.gates[i].open;
  place_players(g.zone);
  cards_zone_start();
  int lives = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.pl[i].active) lives += g.pl[i].lives;
  char sub[224];
  if (g.wipe_cleared) {
    // La zona era gia' stata ripulita (l'ha preso un colpo arrivato dopo): resta libera.
    g.phase = PH_CLEAR;
    g.phase_t = 0;
    const int gi = g.L.zones[g.zone].exit_gate;
    if (gi >= 0) g.gate_want[gi] = 1.0f;
    if (lives == 1) snprintf(sub, sizeof(sub), "%s", tr("la zona resta libera - ultima vita!"));
    else snprintf(sub, sizeof(sub), tr("la zona resta libera - ancora %d vite"), lives);
    show_msg(tr("SI RIPARTE!"), sub, 0xFFFFFF, 2, 120);
  } else {
    begin_zone(g.zone, false);
    if (lives == 1) snprintf(sub, sizeof(sub), tr("zona %d - ultima vita!"), g.zone + 1);
    else snprintf(sub, sizeof(sub), tr("zona %d - ancora %d vite"), g.zone + 1, lives);
    show_msg(tr("SI RIPARTE!"), sub, 0xFFFFFF, 2, 110);
  }
  plat_log("si riparte dalla zona %d della mappa %d (vite %d)", g.zone + 1, g.map_idx + 1, lives);
  update_camera(true);
}

// Boss del pianeta battuto: si apre il pianeta dopo e, con almeno GIRO_STARS stelle nel giro in
// corso, il giro dopo (mostri piu' forti) con le stelle del pianeta da zero. Si guarda a fine
// mappa e di nuovo se l'esploratore viene salvato durante la festa (la sua puo' essere la decima).
// Solo in memoria: chi la chiama poi scrive il salvataggio.
void giro_check(void) {
  if (!maps_is_boss(g.map_idx) || g.giro_up) return;
  SaveData* sd = save_data();
  const int w = maps_world(g.map_idx);
  if (w < 0 || w >= NWORLDS) return;
  const bool again = g.giro_done;
  g.giro_done = true;
  g.giro_was = world_giro(w);
  g.giro_stars = world_stars(w);
  if (w + 1 < NWORLDS && sd->maps_open[w + 1] < 1) {
    sd->maps_open[w + 1] = 1;
    plat_log("si apre il pianeta %s", WORLD_NAMES[w + 1]);
  }
  if (g.giro_stars >= giro_need(w) && g.giro_was < SAVE_GIRO_MAX) {
    g.giro_up = true;
    sd->giro[w] = (uint8_t)(g.giro_was + 1);
    for (int i = 0; i < maps_count(); ++i)
      if (maps_world(i) == w) save_set_stars(maps_id(i), 0);
    plat_log("%s: %d stelle nel giro %d, si apre il giro %d (le stelle del pianeta ripartono da zero)", WORLD_NAMES[w],
             g.giro_stars, g.giro_was, g.giro_was + 1);
  } else if (!again) {
    plat_log("%s: %d stelle nel giro %d, per il giro %d ne servono %d", WORLD_NAMES[w], g.giro_stars, g.giro_was,
             g.giro_was + 1, giro_need(w));
  }
}

// Fine mappa: stelle (finita; esploratore salvato; senza ricominciare la mappa da capo), mappa
// dopo del pianeta aperta, giro (dopo il boss), salvataggio. Una mappa personalizzata senza
// capsula da' comunque la stella dell'esploratore.
void award_stars(void) {
  uint8_t got = SAVE_STAR_DONE;
  if (g.explorer_free || g.L.capsule_dyn < 0) got |= SAVE_STAR_EXPLORER;
  if (g.map_wipes == 0) got |= SAVE_STAR_NO_WIPE;
  const char* id = maps_id(g.map_idx);
  const uint8_t before = save_stars(id);
  save_add_stars(id, got);
  g.stars_got = got;
  g.stars_had = before;
  g.stars_new = (uint8_t)(got & ~before);
  SaveData* sd = save_data();
  const int w = maps_world(g.map_idx), pos = map_in_world(g.map_idx);
  if (w >= 0 && w < NWORLDS && pos + 1 < world_maps(w) && sd->maps_open[w] < pos + 2) {
    sd->maps_open[w] = (uint8_t)(pos + 2);
    plat_log("si apre la mappa %d di %s", pos + 2, WORLD_NAMES[w]);
  }
  plat_log("stelle della mappa %d: %s%s%s (nuove %d; %s, giro %d: %d stelle su %d)", g.map_idx + 1,
           (got & SAVE_STAR_DONE) ? "finita " : "", (got & SAVE_STAR_EXPLORER) ? "esploratore " : "",
           (got & SAVE_STAR_NO_WIPE) ? "senza-ricominciare" : "", star_count(g.stars_new), WORLD_NAMES[w >= 0 && w < NWORLDS ? w : 0],
           world_giro(w), world_stars(w), 3 * world_maps(w));
  giro_check();
  save_write();
}

void update_spawning(void) {
  if (g.spawn_wait > 0) { g.spawn_wait--; return; }
  int pending = 0;
  for (int k = 0; k < MO_NTYPES; ++k) pending += g.spawn_left[k];
  if (pending > 0) {
    if (g.spawn_cd > 0) { g.spawn_cd--; return; }
    int r = (int)(rng_next() % (uint32_t)pending), type = 0;
    for (int k = 0; k < MO_NTYPES; ++k) {
      if (r < g.spawn_left[k]) { type = k; break; }
      r -= g.spawn_left[k];
    }
    if (monster_spawn(type, false, 0)) {
      g.spawn_left[type]--;
      g.spawn_cd = type == MO_SCARABEO ? 6 : 14;
    }
    return;
  }
  const int alive = count_monsters();
  if (g.wave + 1 < g.L.zones[g.zone].nwaves) {
    if (alive <= 1) {
      begin_wave(g.wave + 1);
      g.spawn_wait = 45;
      show_msg(tr("ARRIVANO ALTRI!"), "", 0xFF8A5C, 2, 90);
      snd_ui(SND_WAVE);
    }
    return;
  }
  if (alive > 0 || g.boss.alive) return;
  if (g.zone + 1 < g.L.nzones) {
    g.phase = PH_CLEAR;
    g.phase_t = 0;
    const int gi = g.L.zones[g.zone].exit_gate;
    if (gi >= 0) {
      g.gate_want[gi] = 1.0f;
      const Gate& gt = g.L.gates[gi];
      snd_at(SND_GATE, cell_center(&g.L, (gt.x0 + gt.x1) / 2, (gt.z0 + gt.z1) / 2));
    }
    show_msg(tr("ZONA LIBERA!"), tr("avanti: il cancello è aperto"), 0x7CF29A, 2, 180);
    snd_ui(SND_ZONE_CLEAR);
    plat_log("mappa %d, zona %d libera", g.map_idx + 1, g.zone + 1);
    cards_zone_cleared();
  } else {
    g.phase = PH_MAP_DONE;
    g.phase_t = 0;
    show_msg(tr("MAPPA COMPLETATA!"), "", 0xFFE14D, 2, 200);
    plat_log("mappa %d (%s) completata: mostri sconfitti in totale %d, cadute %d", g.map_idx + 1, g.L.name, g.kills, g.deaths);
    award_stars();
  }
}

// Chi e' rimasto indietro quando la zona comincia viene portato alla sua partenza nella zona.
void regroup(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active || !p.alive) continue;
    if (p.leap_n == 0 && level_zone_at(&g.L, p.pos.x, p.pos.z) == g.zone) continue;
    if (p.leap_n > 0 && level_zone_at(&g.L, p.leap_b.x, p.leap_b.z) == g.zone) continue;   // atterra dentro
    p.leap_n = p.leap_t = 0;
    burst(p.pos + v3(0, 0.6f, 0), 8, i, 2.5f);
    p.pos = g.L.zones[g.zone].start[i];
    level_push_out(&g.L, &p.pos.x, &p.pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK);
    level_body_guard(&g.L, g.L.zones[g.zone].start[i].x, g.L.zones[g.zone].start[i].z, &p.pos.x, &p.pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK);
    p.pos.y = level_ground(&g.L, p.pos.x, p.pos.z, 0.0f, PL_STEP);
    p.vy = 0.0f;
    p.grounded = true;
    p.tramp_t = 0;
    burst(p.pos + v3(0, 0.6f, 0), 8, i, 2.5f);
  }
}

void compute_mvp(void) {
  g.mvp = -1;
  float best = -1e30f;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    const float s = (float)p.kills * 100.0f + p.dmg_done - (float)p.deaths * 150.0f;
    if (s > best) { best = s; g.mvp = i; }
  }
}

void update_gates(void) {
  for (int i = 0; i < g.L.ngates; ++i) {
    float& o = g.L.gates[i].open;
    const float w = g.gate_want[i];
    if (o < w) o = fminf(w, o + 1.0f / 50.0f);
    else if (o > w) o = fmaxf(w, o - 1.0f / 30.0f);
  }
}

// Comincia la mappa gia' caricata (g.map_idx) dalla zona 1, come appena scelta: 3 vite a testa,
// carte da scegliere, oggetti, casse e capsula come all'inizio. again: la stessa mappa da capo
// nella stessa visita (statistiche dei giocatori e ripartenze restano).
static void map_start_msg(bool again, bool log = true);

void begin_map(bool again) {
  clear_actors();
  memset(g.bo, 0, sizeof(g.bo));
  memset(g.pa, 0, sizeof(g.pa));
  memset(g.ft, 0, sizeof(g.ft));
  memset(g.squash, 0, sizeof(g.squash));
  memset(g.crate_hit, 0, sizeof(g.crate_hit));
  memset(g.tramp_t, 0, sizeof(g.tramp_t));
  items_reset(-1);
  level_reset_dynamic(&g.L, -1);
  for (int i = 0; i < LV_MAX_GATES; ++i) g.gate_want[i] = 0.0f;
  g.loop = world_giro(maps_world(g.map_idx)) - 1;   // difficolta': il giro del pianeta
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    g.lives_kept[i] = -1;
    g.vita_kept[i] = false;
    if (!again) {
      g.stats_kept[i] = false;
      g.kills_kept[i] = g.deaths_kept[i] = 0;
      g.dmg_kept[i] = 0.0f;
    }
    Player& p = g.pl[i];
    if (!p.active) continue;
    if (!again) {
      p.kills = p.deaths = 0;
      p.dmg_done = 0.0f;
    }
    p.charge = 0.0f;
    p.bombs = 0;
    p.hp_max = CHARS[p.ch].hp;
    p.joining = false;
    p.lives = START_LIVES;
    p.out = false;
  }
  cards_map_start();
  g.explorer_free = false;
  g.expl_t = -1;
  g.help_t = 0;
  g.stars_got = g.stars_new = g.stars_had = 0;
  g.giro_done = g.giro_up = false;
  g.giro_stars = g.giro_was = 0;
  g.zone = 0;
  place_players(0);
  begin_zone(0, false);
  g.wipe_cleared = g.wipe_map = false;
  g.screen = SC_PLAY;
  g.screen_t = 0;
  g.paused = false;
  g.cam_pitch = PITCH_PLAY;
  map_start_msg(again);
  update_camera(true);
  cards_offer();     // prima della prima ondata; il messaggio della mappa si vede dopo le carte
}

// Messaggio grande all'inizio della mappa (nome e "pronti... via!", oppure "si ricomincia").
static void map_start_msg(bool again, bool log) {
  char big[160], sub[224];
  const int w = maps_world(g.map_idx);
  if (again) {
    snprintf(big, sizeof(big), "%s", tr("SI RICOMINCIA!"));
    if (count_active() > 1) snprintf(sub, sizeof(sub), tr("la mappa da capo: %d vite a testa"), START_LIVES);
    else snprintf(sub, sizeof(sub), tr("la mappa da capo: %d vite"), START_LIVES);
    if (log)
      plat_log("=== mappa %d: %s da capo (ripartenza %d in questa visita, %s giro %d) ===", g.map_idx + 1, g.L.name, g.map_wipes,
               WORLD_NAMES[w >= 0 && w < NWORLDS ? w : 0], g.loop + 1);
  } else {
    hud_upper(big, tr(g.L.name), sizeof(big));
    const char* what = g.L.boss != BOSS_NONE ? tr("arena del boss") : tr("pronti... via!");
    if (g.loop > 0) snprintf(sub, sizeof(sub), tr("%s - giro %d, mostri più forti"), what, g.loop + 1);
    else snprintf(sub, sizeof(sub), "%s", what);
    if (log) plat_log("=== mappa %d: %s (%s, giro %d) ===", g.map_idx + 1, g.L.name, WORLD_NAMES[w >= 0 && w < NWORLDS ? w : 0], g.loop + 1);
  }
  show_msg(big, sub, 0xFFE14D, 2, 150);
}

void restart_map(void) { begin_map(true); }

void play_update(const Pad pads[MAX_PLAYERS]) {
  // Scelta delle carte aperta: conta solo quella (niente pausa, niente ingressi), gioco fermo.
  if (g.cards_open) cards_update();
  else play_menus_update();
  if (g.screen != SC_PLAY) return;
  if (g.paused || g.cards_open) return;
  g.tick++;
  for (int i = 0; i < MAX_PLAYERS; ++i) player_update(i, pads[i]);
  monsters_update();
  boss_update();
  shots_update();
  bombs_update();
  items_update();
  effects_update();
  explorer_update();
  update_gates();

  switch (g.phase) {
    case PH_FIGHT:
      update_spawning();
      break;
    case PH_ENTER: {
      g.phase_t++;
      bool all_in = true;
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g.pl[i];
        if (p.active && p.alive && level_zone_at(&g.L, p.pos.x, p.pos.z) != g.zone) all_in = false;
      }
      if (all_in || g.phase_t >= 240) {
        regroup();
        const int gi = g.L.zones[g.zone].entry_gate;
        if (gi >= 0) g.gate_want[gi] = 0.0f;
        g.phase = PH_FIGHT;
        g.phase_t = 0;
        g.spawn_wait = 50;
      }
      break;
    }
    case PH_CLEAR: {
      if (g.zone + 1 >= g.L.nzones) break;
      // Prima le carte per la zona che segue: si aprono poco dopo "ZONA LIBERA!".
      if (g.cards_due > 0) {
        if (--g.cards_due == 0) cards_offer();
        break;
      }
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active && g.pl[i].alive && level_zone_at(&g.L, g.pl[i].pos.x, g.pl[i].pos.z) == g.zone + 1) {
          begin_zone(g.zone + 1, true);
          break;
        }
      break;
    }
    case PH_MAP_DONE:
      if (++g.phase_t >= 210) {
        compute_mvp();
        g.maps_done++;
        players_gather();      // chi e' a terra (o senza vite) festeggia con gli altri
        go_screen(SC_STATS);
        return;
      }
      break;
    case PH_WIPE:
      if (++g.phase_t >= (g.wipe_map ? WIPE_MAP_FRAMES : WIPE_ZONE_FRAMES)) {
        if (g.wipe_map) restart_map();
        else restart_zone();
      }
      break;
  }
  // Boss senza vita ma ancora in piedi (cade al prossimo boss_update): ha vinto la squadra, anche se
  // l'ultimo giocatore e' caduto nello stesso fotogramma (bolla di lava, razzo o bomba arrivati
  // dopo il turno del boss).
  const bool boss_beaten = g.boss.alive && g.boss.hp <= 0.0f;
  if (!boss_beaten && (g.phase == PH_FIGHT || g.phase == PH_CLEAR || g.phase == PH_ENTER) && count_active() > 0 &&
      count_alive() == 0) {
    // Tutti a terra. Da soli (un giocatore attivo) la caduta ha gia' tolto una vita: con vite
    // rimaste si rifa' la zona, senza vite la mappa ricomincia da capo. In piu' giocatori, a
    // terra tutti nello stesso momento, la mappa ricomincia da capo anche con vite rimaste.
    const bool solo = count_active() == 1;
    int lives = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (g.pl[i].active) lives += g.pl[i].lives;
    g.wipe_cleared = g.phase == PH_CLEAR;
    g.wipe_map = !solo || lives == 0;
    g.phase = PH_WIPE;
    g.phase_t = 0;
    snd_ui(SND_WIPE);
    if (g.wipe_map) {
      g.map_wipes++;
      g.restarts++;
      show_msg(solo ? tr("VITE FINITE!") : tr("TUTTI A TERRA!"), tr("si ricomincia la mappa da capo"), 0xFF6A5A, 2, WIPE_MAP_FRAMES);
      if (solo) plat_log("vite finite: la mappa %d (%s) ricomincia da capo", g.map_idx + 1, g.L.name);
      else plat_log("tutti a terra insieme (vite rimaste in tutto: %d): la mappa %d (%s) ricomincia da capo", lives, g.map_idx + 1, g.L.name);
    } else {
      g.wipes++;
      char sub[224];
      if (lives == 1) snprintf(sub, sizeof(sub), "%s", tr("resta 1 vita: si rifà questa zona"));
      else snprintf(sub, sizeof(sub), tr("restano %d vite: si rifà questa zona"), lives);
      show_msg(tr("VITA PERSA!"), sub, 0xFF6A5A, 2, WIPE_ZONE_FRAMES);
      plat_log("vita persa (restano %d): mappa %d, zona %d da rifare", lives, g.map_idx + 1, g.zone + 1);
    }
  }
  g.enemies_left = g.phase == PH_FIGHT ? zone_total_left() : count_monsters();
  update_camera(false);
}

void update_pad_states(const Pad pads[MAX_PLAYERS]) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    PadState& s = g.in[i];
    s.prev = s.btn;
    s.btn = pads[i].btn;
    s.lx = pads[i].lx;
    s.ly = pads[i].ly;
    int dir = NAV_NONE;
    if ((s.btn & (1u << PAD_UP)) || s.ly < -0.6f) dir = NAV_UP;
    else if ((s.btn & (1u << PAD_DOWN)) || s.ly > 0.6f) dir = NAV_DOWN;
    else if ((s.btn & (1u << PAD_LEFT)) || s.lx < -0.6f) dir = NAV_LEFT;
    else if ((s.btn & (1u << PAD_RIGHT)) || s.lx > 0.6f) dir = NAV_RIGHT;
    if (dir == NAV_NONE) { s.nav = NAV_NONE; s.nav_t = 0; s.navdir = NAV_NONE; }
    else if (dir != s.navdir) { s.nav = dir; s.nav_t = 18; s.navdir = dir; }
    else if (--s.nav_t <= 0) { s.nav = dir; s.nav_t = 6; }
    else s.nav = NAV_NONE;
  }
}

}  // namespace

// --- funzioni condivise -------------------------------------------------------------

int count_active(void) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active;
  return n;
}
int count_alive(void) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active && g.pl[i].alive;
  return n;
}
int count_in_play(void) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active && !g.pl[i].out;
  return n;
}
int count_monsters(void) {
  int n = 0;
  for (int i = 0; i < MAX_MONSTERS; ++i) n += g.mo[i].alive;
  return n;
}

// Pianeti e giri: giro e mappe aperte stanno nel salvataggio, le mappe in maps.h (in ordine di
// campagna, un pianeta dopo l'altro).
int world_giro(int w) { return w >= 0 && w < NWORLDS ? save_data()->giro[w] : 1; }

int world_maps(int w) {
  int n = 0;
  for (int i = 0; i < maps_count(); ++i) n += maps_world(i) == w;
  return n;
}

int world_first_map(int w) {
  for (int i = 0; i < maps_count(); ++i)
    if (maps_world(i) == w) return i;
  return -1;
}

int star_count(uint8_t s) { return (s & 1) + ((s >> 1) & 1) + ((s >> 2) & 1); }

int world_stars(int w) {
  int n = 0;
  for (int i = 0; i < maps_count(); ++i)
    if (maps_world(i) == w) n += star_count(save_stars(maps_id(i)));
  return n;
}

int giro_need(int w) {
  const int n = 3 * world_maps(w) - 2;      // 4 mappe: 10 su 12; con meno mappe tutte le stelle meno due
  return n >= GIRO_STARS ? GIRO_STARS : (n > 1 ? n : 1);
}

int map_in_world(int i) {
  int n = 0;
  for (int k = 0; k < i && k < maps_count(); ++k) n += maps_world(k) == maps_world(i);
  return n;
}

bool map_unlocked(int i) {
  if (i < 0 || i >= maps_count()) return false;
  const int w = maps_world(i);
  if (w < 0 || w >= NWORLDS) return false;
  const int open = save_data()->maps_open[w];
  return map_in_world(i) < (w == 0 && open < 1 ? 1 : open);   // la prima mappa della Terra e' sempre aperta
}

// Mostri piu' forti a ogni giro del pianeta (g.loop: giro - 1).
float diff_hp(void) { return 1.0f + 0.6f * (float)g.loop; }
float diff_dmg(void) { return 1.0f + 0.25f * (float)g.loop; }
float diff_speed(void) { return fminf(1.0f + 0.05f * (float)g.loop, 1.25f); }

void show_msg(const char* big, const char* small, uint32_t rgb, int size, int frames) {
  snprintf(g.msg, sizeof(g.msg), "%s", big);
  snprintf(g.msg2, sizeof(g.msg2), "%s", small);
  g.msg_rgb = rgb;
  g.msg_size = size;
  g.msg_t = frames;
}

bool load_map_preview(int idx) {
  if (idx < 0 || idx >= maps_count()) return false;
  if (g.level_ok && g.map_shown == idx) return true;
  level_free(&g.L);
  g.level_ok = maps_load(idx, &g.L, g.map_note, sizeof(g.map_note));
  if (!g.level_ok) return false;
  level_build_mesh(&g.L);
  g.map_shown = idx;
  clear_actors();
  memset(g.bo, 0, sizeof(g.bo));
  memset(g.pa, 0, sizeof(g.pa));
  memset(g.ft, 0, sizeof(g.ft));
  memset(g.squash, 0, sizeof(g.squash));
  memset(g.crate_hit, 0, sizeof(g.crate_hit));
  memset(g.tramp_t, 0, sizeof(g.tramp_t));
  items_reset(-1);
  level_reset_dynamic(&g.L, -1);
  for (int i = 0; i < LV_MAX_GATES; ++i) g.gate_want[i] = 0.0f;
  memset(&g.boss, 0, sizeof(g.boss));
  g.zone = 0;
  return true;
}

void start_map(int idx) {
  if (!load_map_preview(idx)) return;
  g.map_idx = idx;
  // Il generatore casuale riparte mescolato con i fotogrammi passati dall'avvio (tempo nei menu
  // compreso): carte e ondate cambiano da una partita all'altra, ma con gli stessi comandi dal
  // primo fotogramma la partita resta la stessa su ogni processore.
  g.rng ^= 0x9E3779B9u * (g.ui_tick + 1u);
  if (g.rng == 0u) g.rng = 0x9E3779B9u;
  g.map_wipes = 0;
  begin_map(false);
}

// Nei menu i personaggi stanno in fila alla partenza della zona 1; la telecamera li guarda da
// nord, quindi il giocatore 1 sta a est (a sinistra sullo schermo).
Vec3 menu_spot(int i) {
  Vec3 c = v3(0, 0, 0);
  for (int k = 0; k < 4; ++k) c = c + g.L.zones[0].start[k] * 0.25f;
  return v3(c.x + (1.5f - (float)i) * 1.25f, 0.0f, c.z);
}

void go_screen(int s) {
  g.screen = s;
  g.screen_t = 0;
  g.menu_sel = 0;
  g.paused = false;
  if (s != SC_PLAY && s != SC_STATS) {
    if (!g.level_ok) load_map_preview(s == SC_MAPS || s == SC_CHARS ? g.map_sel : 0);
  }
  if (s == SC_STORY) g.story_t = 0;
  if (s == SC_CHARS) {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      g.ready[i] = false;
      g.joined[i] = g.pl[i].active;
      if (g.pl[i].active) g.pick[i] = g.pl[i].ch;
    }
    g.chars_t = 0;
  }
  update_camera(true);
}

void update_camera(bool snap) {
  Vec3 want;
  float dist, pitch;
  bool back = false;
  if (g.screen != SC_PLAY) {
    // Menu: inquadratura bassa sui personaggi alla partenza della mappa mostrata, da nord
    // (dietro di loro la siepe di confine e gli alberi).
    Vec3 c = v3(0, 0, 0);
    for (int i = 0; i < 4; ++i) c = c + g.L.zones[0].start[i] * 0.25f;
    want = v3(c.x, 0.85f, c.z + 0.2f);
    if (g.screen == SC_TITLE) want.x -= 1.8f;       // 0.5.3: gli eroi a sinistra, le voci a destra
    dist = g.screen == SC_CHARS ? 5.6f : (g.screen == SC_TITLE ? 7.4f : 6.6f);
    pitch = g.screen == SC_CHARS ? 0.36f : PITCH_MENU;
    back = true;
    if (g.screen == SC_STATS) {
      // Fine mappa: i personaggi dove sono, che festeggiano rivolti verso la telecamera.
      float n = 0.0f;
      Vec3 s = v3(0, 0, 0);
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active) { s = s + g.pl[i].pos; n += 1.0f; }
      if (n > 0.0f) want = v3(s.x / n, 0.85f + s.y / n, s.z / n - 0.2f);
      dist = 7.0f;
      back = false;
    }
  } else {
    // Si inquadra chi e' in piedi; se sono tutti a terra chi ha ancora vite, se no tutti.
    Vec3 pts[MAX_PLAYERS + 4];
    int np = 0;
    float y = 0.0f;
    for (int pass = 0; pass < 3 && np == 0; ++pass)
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g.pl[i];
        if (!p.active || (pass == 0 && !p.alive) || (pass == 1 && p.out)) continue;
        pts[np++] = p.pos;
        y += p.pos.y;
      }
    if (np == 0) return;
    const int n = np;
    float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
    for (int k = 0; k < np; ++k) {
      mnx = fminf(mnx, pts[k].x); mxx = fmaxf(mxx, pts[k].x);
      mnz = fminf(mnz, pts[k].z); mxz = fmaxf(mxz, pts[k].z);
    }
    const float gx = 0.5f * (mnx + mxx), gz = 0.5f * (mnz + mxz);
    if (g.boss.alive && dist2d(g.boss.pos, v3(gx, 0, gz)) < 16.0f) {
      // Arena: il boss sempre tutto nell'inquadratura insieme ai giocatori.
      const Vec3 b = g.boss.pos;
      static const float BOX[4][2] = { { -1.6f, -0.5f }, { 1.6f, -0.5f }, { -1.6f, 1.5f }, { 1.6f, 1.5f } };
      for (int k = 0; k < 4; ++k) {
        pts[np++] = v3(b.x + BOX[k][0], 0.0f, b.z + BOX[k][1]);
        mnx = fminf(mnx, b.x + BOX[k][0]); mxx = fmaxf(mxx, b.x + BOX[k][0]);
        mnz = fminf(mnz, b.z + BOX[k][1]); mxz = fmaxf(mxz, b.z + BOX[k][1]);
      }
    }
    const ZoneDef& z = g.L.zones[g.zone];
    float zx0 = z.wx0, zx1 = z.wx1, zz0 = z.wz0, zz1 = z.wz1;
    auto join = [&](int k) {
      if (k < 0 || k >= g.L.nzones) return;
      const ZoneDef& o = g.L.zones[k];
      zx0 = fminf(zx0, o.wx0); zx1 = fmaxf(zx1, o.wx1);
      zz0 = fminf(zz0, o.wz0); zz1 = fmaxf(zz1, o.wz1);
    };
    if (g.phase == PH_CLEAR) join(g.zone + 1);
    if (g.phase == PH_ENTER) join(g.zone - 1);
    // Il punto inquadrato sta un po' a nord del gruppo (si vede di piu' davanti). Distanza: da
    // 15 m (si vedono circa 19 x 19 m) fino a 30 m quando il gruppo si allarga; se a 30 m non si
    // vedono ancora tutti, il punto inquadrato scende verso sud quanto basta (in alto
    // l'inquadratura e' piu' larga: una zona di 31 x 21 m con un giocatore per angolo ci sta).
    const float cx = 0.5f * (mnx + mxx), cz0 = 0.5f * (mnz + mxz) - 1.2f;
    float cz = cz0;
    dist = cam_need(pts, np, cx, cz0);
    for (int k = 1; k <= 24 && dist > CAM_DIST_MAX; ++k) {
      const float d = cam_need(pts, np, cx, cz0 + 0.5f * (float)k);
      if (d < dist) {
        dist = d;
        cz = cz0 + 0.5f * (float)k;
      }
    }
    dist = clampf(dist, g_hdmi ? CAM_DIST_MIN : CAM_DIST_MIN_LCD, CAM_DIST_MAX);
    // Ai bordi della zona ci si ferma mostrando poco oltre, a ogni distanza, ma chi gioca resta
    // nell'inquadratura: prima nord-sud (se il punto fermato al bordo lascerebbe fuori qualcuno,
    // si torna verso quello voluto quanto basta), poi di lato con la riga in cui si trova ognuno.
    const float loz = zz0 - CAM_OVER_N + VIEW_N * dist, hiz = zz1 + CAM_OVER_S - VIEW_S * dist;
    const float czc = loz <= hiz ? clampf(cz, loz, hiz) : hiz;
    want.z = czc;
    for (int k = 1; k <= 10 && cam_need(pts, np, cx, want.z) > dist + 0.01f; ++k) want.z = czc + (cz - czc) * 0.1f * (float)k;
    const float lox = zx0 - CAM_OVER_X + VIEW_W * dist, hix = zx1 + CAM_OVER_X - VIEW_W * dist;
    const float tx = lox <= hix ? clampf(cx, lox, hix) : 0.5f * (zx0 + zx1);
    float vlo = -1e30f, vhi = 1e30f;
    for (int k = 0; k < np; ++k) {
      const float hw = CAM_SAFE_X * VIEW_W * (dist - VIEW_K * (pts[k].z - want.z)) - CAM_MARGIN_X;
      vlo = fmaxf(vlo, pts[k].x - hw);
      vhi = fminf(vhi, pts[k].x + hw);
    }
    want.x = vlo <= vhi ? clampf(tx, vlo, vhi) : 0.5f * (vlo + vhi);
    want.y = 0.6f + y / (float)n * 0.5f;
    pitch = PITCH_PLAY;
  }
  if (snap || back != g.cam_back) {
    g.cam_target = want;
    g.cam_dist = dist;
    g.cam_pitch = pitch;
    g.cam_back = back;
  } else {
    g.cam_target = g.cam_target + (want - g.cam_target) * 0.12f;
    g.cam_dist += (dist - g.cam_dist) * 0.05f;
    g.cam_pitch += (pitch - g.cam_pitch) * 0.08f;
  }
}

// --- interfaccia ------------------------------------------------------------------------

void game_init(void) {
  if (g_inited) game_shutdown();
  memset(&g, 0, sizeof(g));
  for (int i = 0; i < MAX_PLAYERS; ++i) g.aim_level[i] = AIM_DEFAULT;   // resta per tutta la sessione
  g.aa_on = true;                                                       // antialias acceso se il renderer lo sa fare
  models_build();
  maps_init();
  g_inited = true;
  game_reset();
}

void game_shutdown(void) {
  if (!g_inited) return;
  level_free(&g.L);
  models_free();
  g_inited = false;
}

void game_reset(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    memset(&g.pl[i], 0, sizeof(Player));
    g.joined[i] = g.ready[i] = false;
    g.pick[i] = i % NCHARS;
  }
  g.rng = 0x9E3779B9u;
  g.tick = 0;
  g.loop = 0;
  g.map_idx = 0;
  g.map_sel = 0;
  g.kills = g.deaths = g.wipes = g.maps_done = g.restarts = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    g.lives_kept[i] = -1;
    g.vita_kept[i] = g.stats_kept[i] = false;
  }
  g.cards_open = false;
  g_quit = false;
  bot_reset();
  load_map_preview(0);
  go_screen(SC_TITLE);
  g.cam_pitch = PITCH_MENU;
}

void game_quick_start(int map, int players, const char* chars) {
  if (map < 0 || map >= maps_count()) map = 0;
  if (players < 1) players = 1;
  if (players > MAX_PLAYERS) players = MAX_PLAYERS;
  const int nc = chars ? (int)strlen(chars) : 0;
  for (int i = 0; i < players; ++i) {
    int ch = i % NCHARS;
    if (i < nc && chars[i] >= '0' && chars[i] < '0' + NCHARS) ch = chars[i] - '0';
    player_join(i, ch, false);
    g.joined[i] = true;
    g.pick[i] = ch;
  }
  g.map_sel = map;
  start_map(map);
}

// Musica secondo la schermata (lo stesso brano continua senza ripartire) e musica piu' bassa
// mentre la partita e' ferma (pausa, carte).
static void music_update(void) {
  int want = MUS_TITLE;
  static const int STYLE_MUSIC[NSTYLES] = { MUS_PIRAMIDI, MUS_CHICHENITZA, MUS_MURAGLIA, MUS_COLOSSEO };
  if (g.screen == SC_PLAY)
    want = g.phase == PH_MAP_DONE ? MUS_VICTORY : STYLE_MUSIC[g.L.style >= 0 && g.L.style < NSTYLES ? g.L.style : 0];
  else if (g.screen == SC_STATS) want = MUS_VICTORY;     // la fanfara una volta, poi silenzio
  audio_music(want);
  audio_duck(g.screen == SC_PLAY && (g.paused || g.cards_open));
}

// Impostazioni cambiate nella pausa (mira, antialias, volumi; la pausa le mette subito in
// save_data): il file si scrive quando la pausa si chiude.
static void settings_save(void) {
  if (!g_settings_dirty || (g.screen == SC_PLAY && g.paused)) return;
  g_settings_dirty = false;
  save_write();
}

void game_update(const Pad pads[MAX_PLAYERS]) {
  g.ui_tick++;
  g.screen_t++;
  // Tempo di gioco della partita attiva (fuori dalla prima schermata e dalle sue pagine).
  static int s_frames;
  if (save_slot() >= 0 && g.screen != SC_TITLE && g.screen != SC_SLOTS && g.screen != SC_LANG && ++s_frames >= 60) {
    s_frames = 0;
    save_data()->seconds++;
  }
  update_pad_states(pads);
  if (g.screen == SC_PLAY) {
    play_update(pads);
  } else {
    menu_update();
    effects_update();
    update_camera(false);
  }
  for (int i = 0; i < MAX_PLAYERS; ++i) g.start_tap[i] = false;
  music_update();
  settings_save();
}

// --- sonoro ---------------------------------------------------------------------------------

void snd_at(int id, Vec3 pos) {
  // Inquadratura: circa 0,62 volte la distanza della telecamera per lato. Dentro si sente pieno,
  // fuori cala fino a zero a una volta e mezza; sinistra/destra dalla posizione sullo schermo.
  const float dist = fmaxf(g.cam_dist, 8.0f);
  const float dx = pos.x - g.cam_target.x, dz = pos.z - g.cam_target.z;
  const float pan = clampf(dx / (0.62f * dist * 1.3f), -0.75f, 0.75f);
  const float d = sqrtf(dx * dx + dz * dz);
  const float vol = d < dist ? 1.0f : clampf(1.0f - (d - dist) / (0.5f * dist), 0.0f, 1.0f);
  if (vol > 0.02f) audio_play(id, pan, vol);
}

void snd_ui(int id) { audio_play(id, 0.0f, 1.0f); }

void game_start_tap(int player) {
  if (player >= 0 && player < MAX_PLAYERS) g.start_tap[player] = true;
}

bool game_paused(void) { return g.screen == SC_PLAY && g.paused; }

void game_set_hdmi(bool on) {
  if (on == g_hdmi) return;
  g_hdmi = on;
  plat_log("schermo: %s, visuale da %.0f a %.0f m", on ? "HDMI (TV)" : "della console", (double)(on ? CAM_DIST_MIN : CAM_DIST_MIN_LCD),
           (double)CAM_DIST_MAX);
}
void game_set_aa_available(bool avail) { g.aa_avail = avail; }
void game_set_aa_on(bool on) { g.aa_on = on; }
bool game_aa_wanted(void) { return g.aa_avail && g.aa_on; }
void game_save_loaded(void) {
  // Impostazioni salvate: mira di ogni posto, antialias, volumi, lingua.
  const SaveData* sd = save_data();
  for (int i = 0; i < MAX_PLAYERS && i < SAVE_PLAYERS; ++i)
    if (sd->aim[i] < NAIMLEVELS) g.aim_level[i] = sd->aim[i];
  g.aa_on = sd->antialias;
  audio_set_volume(sd->music_vol, sd->sfx_vol);
  lang_set(sd->lang);
  g_settings_dirty = false;
  // Prove: la partita veloce (giocatori automatici, PUPAZZI_MAPPA) comincia prima che il core legga
  // il salvataggio e gioca nella partita 1 (se non c'e' la crea); se il pianeta e' a un altro giro
  // la mappa riparte con quello.
  if (g.screen == SC_PLAY && g.tick == 0 && save_slot() < 0 && !save_slot_load(0)) save_slot_new(0);
  if (g.screen == SC_PLAY && g.tick == 0) map_start_msg(false, false);      // nella lingua salvata
  if (g.screen == SC_PLAY && g.tick == 0 && g.loop != world_giro(maps_world(g.map_idx)) - 1) {
    plat_log("salvataggio letto: %s e' al giro %d, la mappa di prova riparte", WORLD_NAMES[maps_world(g.map_idx)],
             world_giro(maps_world(g.map_idx)));
    start_map(g.map_idx);
  }
}

void settings_changed(void) { g_settings_dirty = true; }

void stars_late_explorer(void) {
  g.stars_got |= SAVE_STAR_EXPLORER;
  if (!(g.stars_had & SAVE_STAR_EXPLORER)) g.stars_new |= SAVE_STAR_EXPLORER;
  if (g.giro_up) {
    // Il giro dopo e' gia' aperto (stelle da zero): questa stella era del giro finito.
    plat_log("stella dell'esploratore a mappa finita (mappa %d): il giro %d e' gia' aperto, vale solo per questa volta",
             g.map_idx + 1, g.giro_was + 1);
    return;
  }
  save_add_stars(maps_id(g.map_idx), SAVE_STAR_EXPLORER);
  plat_log("stella dell'esploratore aggiunta a mappa finita (mappa %d)", g.map_idx + 1);
  giro_check();      // dopo il boss puo' essere la decima stella
  save_write();
}
bool game_wants_quit(void) { return g_quit; }
void game_request_quit(void) { g_quit = true; }

bool game_player_active(int i) { return i >= 0 && i < MAX_PLAYERS && g.pl[i].active; }
uint32_t game_player_rgb(int i) { return PLAYER_RGB[i >= 0 && i < MAX_PLAYERS ? i : MAX_PLAYERS]; }

uint64_t game_state_hash(void) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  };
  mix(&g.rng, sizeof(g.rng));
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    mix(&p.pos, sizeof(p.pos));
    mix(&p.hp, sizeof(p.hp));
    mix(&p.ammo, sizeof(p.ammo));
    mix(&p.charge, sizeof(p.charge));
    mix(&p.yaw, sizeof(p.yaw));
    mix(&p.aim_yaw, sizeof(p.aim_yaw));
    // Vite (0.5): quante ne restano, a terra, senza vite.
    const int8_t lv[4] = { (int8_t)p.lives, (int8_t)p.active, (int8_t)p.alive, (int8_t)p.out };
    mix(lv, sizeof(lv));
  }
  // Giro del pianeta, mappa e zona in corso, mappa ricominciata da capo, vite di chi e' uscito.
  const int st[6] = { g.loop, g.map_idx, g.zone, g.phase, g.map_wipes, (int)g.wipe_map };
  mix(st, sizeof(st));
  mix(g.lives_kept, sizeof(g.lives_kept));
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const int kp[4] = { (int)g.vita_kept[i], (int)g.stats_kept[i], g.kills_kept[i], g.deaths_kept[i] };
    mix(kp, sizeof(kp));
  }
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    const Monster& m = g.mo[k];
    if (!m.alive) continue;
    mix(&k, sizeof(k));
    mix(&m.pos, sizeof(m.pos));
    mix(&m.hp, sizeof(m.hp));
  }
  for (int i = 0; i < MAX_SHOTS; ++i)
    if (g.sh[i].alive) mix(&g.sh[i].pos, sizeof(Vec3));
  // Gladiatore (0.5.1): dove sta e dove guarda, vita, azione in corso, bersaglio.
  if (g.boss.alive) { const float bf[4] = { g.boss.pos.x, g.boss.pos.z, g.boss.hp, g.boss.face }; const int bi[3] = { g.boss.act, g.boss.act_t, g.boss.target }; mix(bf, sizeof(bf)); mix(bi, sizeof(bi)); }
  // Carte: solo se attive, cosi' a carte spente l'impronta resta quella di prima.
  if (cards_enabled()) {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      const Player& p = g.pl[i];
      const uint8_t f[2] = { (uint8_t)p.shield, 0 };
      mix(&p.cards, sizeof(p.cards));
      mix(f, sizeof(f));
      mix(g.offer[i].card, sizeof(g.offer[i].card));
    }
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) {
        mix(&g.mo[k].burn_t, sizeof(int));
        mix(&g.mo[k].vapor_t, sizeof(int));
      }
    const uint8_t open = g.cards_open;
    mix(&open, 1);
  }
  // Esploratore della capsula.
  const uint8_t pf = g.explorer_free;
  mix(&pf, 1);
  if (g.expl_t >= 0) mix(&g.expl_pos, sizeof(Vec3));
  return h;
}

// Prove: tutti i campi dei mostri vivi, in esadecimale esatto, per trovare dove due processori
// cominciano a dare risultati diversi.
void game_trace_monsters(void (*out)(const char* line)) {
  char buf[512];
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    const Monster& m = g.mo[k];
    if (!m.alive) continue;
    snprintf(buf, sizeof(buf), "m%d t%d p%a,%a,%a v%a,%a k%a,%a y%a f%a ph%a sq%a,%a hp%a st%d,%d sl%a,%d",
             k, m.type, (double)m.pos.x, (double)m.pos.y, (double)m.pos.z, (double)m.vx, (double)m.vz, (double)m.kx, (double)m.kz,
             (double)m.yaw, (double)m.fly, (double)m.phase, (double)m.sq, (double)m.sq_v, (double)m.hp, m.state, m.state_t,
             (double)m.slow_k, m.slow_t);
    out(buf);
  }
}

void game_info(GameInfo* gi) {
  memset(gi, 0, sizeof(*gi));
  gi->screen = g.screen;
  gi->map = g.map_idx + 1;
  gi->loop = g.loop + 1;
  gi->zone = g.zone + 1;
  gi->nzones = g.L.nzones;
  gi->enemies = count_monsters();
  gi->players = count_active();
  gi->alive = count_alive();
  gi->phase = g.phase;
  gi->kills = g.kills;
  gi->deaths = g.deaths;
  gi->wipes = g.wipes;
  gi->maps_done = g.maps_done;
  gi->restarts = g.restarts;
  for (int i = 0; i < MAX_PLAYERS; ++i) gi->lives[i] = g.pl[i].active ? g.pl[i].lives : -1;
  snprintf(gi->map_name, sizeof(gi->map_name), "%s", g.L.name);
}
