// Game flow: campaign, maps, zones, waves, gates, lives and map restarts, stars and rounds of the
// planets; the game API of game.h.
#include "game/sim/game_int.h"
#include "engine/lang.h"
#include "game/sim/maps.h"
#include "platform/plat.h"
#include "game/save.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

// The state is plain data (game_init clears it with memset): calloc, so the core needs no C++ runtime
// (operator new would pull in libstdc++).
static_assert(std::is_trivially_default_constructible<Game>::value && std::is_trivially_copyable<Game>::value &&
                  std::is_standard_layout<Game>::value,
              "Game is plain data: created with calloc, cleared with memset (game_init)");
Game* game_create(void) {
  Game* g = (Game*)calloc(1, sizeof(Game));
  g->session.net_local = -1;
  g->session.cards_on = true;
  return g;
}
void game_destroy(Game* g) { free(g); }

const char* const WORLD_NAMES[NWORLDS] = { N_("TERRA"), N_("LUNA"), N_("MARTE"), N_("VENERE"), N_("MERCURIO"), N_("GIOVE"),
                                            N_("SATURNO"), N_("URANO"), N_("NETTUNO") };
static_assert(SAVE_WORLDS == NWORLDS, "save.h e level.h devono avere gli stessi pianeti");
const uint32_t PLAYER_RGB[NSETS] = { 0xE8463C, 0x3C82E8, 0x3FC04A, 0xF2C230, 0x8A8E94 };
const uint32_t PW_RGB[4] = { 0x3FD8FF, 0xFF4A3A, 0xFFC83C, 0xB06CFF };
static_assert(NET_NAME_LEN == SAVE_NAME_LEN, "game.h e save.h: stessa lunghezza dei nomi delle partite");

namespace {

bool g_inited;                 // maps loaded (shared by every game in the process)
// Monsters of a group: more players in play (whoever is out of lives does not count), more
// monsters; and more at every round.
int wave_size(Game& g, int base) {
  if (base <= 0) return 0;
  const int np = count_in_play(g) > 0 ? count_in_play(g) : 1;
  const int n = (int)((float)base * (0.7f + 0.3f * (float)np) * (1.0f + 0.4f * (float)g.loop) + 0.5f);
  return n < 1 ? 1 : n;
}

void begin_wave(Game& g, int w) {
  g.wave = w;
  const ZoneDef& zd = g.L.zones[g.zone];
  if (w >= zd.nwaves) return;
  const WaveDef& wd = zd.wave[w];
  for (int k = 0; k < wd.nkinds; ++k) g.spawn_left[wd.kind[k]] += wave_size(g, wd.count[k]);
  g.spawn_cd = 0;
}

void begin_zone(Game& g, int z, bool entering) {
  g.zone = z;
  g.phase = entering ? PH_ENTER : PH_FIGHT;
  g.phase_t = 0;
  g.spawn_wait = 90;
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  begin_wave(g, 0);
  boss_start(g);      // only in the last zone of an arena; otherwise no boss
  cards_zone_start(g);
  int n = 0;
  for (int k = 0; k < MO_NTYPES; ++k) n += g.spawn_left[k];
  plat_log("mappa %d (%s), zona %d: %d mostri nella prima ondata", g.map_idx + 1, g.L.name, z + 1, n);
}

int zone_total_left(Game& g) {
  int n = count_monsters(g);
  for (int k = 0; k < MO_NTYPES; ++k) n += g.spawn_left[k];
  const ZoneDef& zd = g.L.zones[g.zone];
  for (int w = g.wave + 1; w < zd.nwaves; ++w)
    for (int k = 0; k < zd.wave[w].nkinds; ++k) n += wave_size(g, zd.wave[w].count[k]);
  return n;
}

void clear_actors(Game& g) {
  monsters_clear(g);
  memset(g.sh, 0, sizeof(g.sh));
  memset(g.mk, 0, sizeof(g.mk));
}

// Playing alone, fallen with lives left: the zone starts again (the zone's monsters and items from
// the start, full health at the zone's start).
void restart_zone(Game& g) {
  clear_actors(g);
  items_reset(g, g.zone);
  level_reset_dynamic(&g.L, g.zone);
  if (g.explorer_free && g.L.capsule_dyn >= 0) g.L.dyn[g.L.capsule_dyn].hp = 0.0f;   // the explorer stays rescued
  for (int i = 0; i < g.L.ngates; ++i) g.gate_want[i] = g.L.gates[i].open;
  place_players(g, g.zone);
  cards_zone_start(g);
  int lives = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.pl[i].active) lives += g.pl[i].lives;
  char sub[224];
  if (g.wipe_cleared) {
    // The zone had already been cleared (a shot that arrived later got them): it stays clear.
    g.phase = PH_CLEAR;
    g.phase_t = 0;
    const int gi = g.L.zones[g.zone].exit_gate;
    if (gi >= 0) g.gate_want[gi] = 1.0f;
    if (lives == 1) snprintf(sub, sizeof(sub), "%s", tr("la zona resta libera - ultima vita!"));
    else snprintf(sub, sizeof(sub), tr("la zona resta libera - ancora %d vite"), lives);
    show_msg(g, tr("SI RIPARTE!"), sub, 0xFFFFFF, 2, 120);
  } else {
    begin_zone(g, g.zone, false);
    if (lives == 1) snprintf(sub, sizeof(sub), tr("zona %d - ultima vita!"), g.zone + 1);
    else snprintf(sub, sizeof(sub), tr("zona %d - ancora %d vite"), g.zone + 1, lives);
    show_msg(g, tr("SI RIPARTE!"), sub, 0xFFFFFF, 2, 110);
  }
  plat_log("si riparte dalla zona %d della mappa %d (vite %d)", g.zone + 1, g.map_idx + 1, lives);
  ev_camera(g, CAM_CUT);
}

// Planet boss beaten: the next planet opens and, with at least GIRO_STARS stars in the current
// round, the next round (stronger monsters) with the planet's stars from zero. Checked at the end
// of the map and again if the explorer is rescued during the party (their star may be the tenth).
// Only in memory: the caller then writes the save.
void giro_check(Game& g) {
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

// End of map: stars (finished; explorer rescued; without restarting the map from the start), next
// map of the planet opened, round (after the boss), save. A custom map without a capsule gives
// the explorer's star anyway.
void award_stars(Game& g) {
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
  giro_check(g);
  save_write();
}

void update_spawning(Game& g) {
  if (g.spawn_wait > 0) { g.spawn_wait--; return; }
  int pending = 0;
  for (int k = 0; k < MO_NTYPES; ++k) pending += g.spawn_left[k];
  if (pending > 0) {
    if (g.spawn_cd > 0) { g.spawn_cd--; return; }
    int r = (int)(rng_next(g) % (uint32_t)pending), type = 0;
    for (int k = 0; k < MO_NTYPES; ++k) {
      if (r < g.spawn_left[k]) { type = k; break; }
      r -= g.spawn_left[k];
    }
    if (monster_spawn(g, type, false, 0)) {
      g.spawn_left[type]--;
      g.spawn_cd = type == MO_SCARABEO ? 6 : 14;
    }
    return;
  }
  const int alive = count_monsters(g);
  if (g.wave + 1 < g.L.zones[g.zone].nwaves) {
    if (alive <= 1) {
      begin_wave(g, g.wave + 1);
      g.spawn_wait = 45;
      show_msg(g, tr("ARRIVANO ALTRI!"), "", 0xFF8A5C, 2, 90);
      ev_sound_ui(g, SND_WAVE);
    }
    return;
  }
  if (alive > 0 || g.boss.alive) return;
  if (g.zone + 1 < g.L.nzones) {
    g.phase = PH_CLEAR;
    g.phase_t = 0;
    const int gi = g.L.zones[g.zone].exit_gate;
    if (gi >= 0) g.gate_want[gi] = 1.0f;          // it opens (and creaks: update_gates)
    show_msg(g, tr("ZONA LIBERA!"), tr("avanti: il cancello è aperto"), 0x7CF29A, 2, 180);
    ev_sound_ui(g, SND_ZONE_CLEAR);
    plat_log("mappa %d, zona %d libera", g.map_idx + 1, g.zone + 1);
    cards_zone_cleared(g);
  } else {
    g.phase = PH_MAP_DONE;
    g.phase_t = 0;
    show_msg(g, tr("MAPPA COMPLETATA!"), "", 0xFFE14D, 2, 200);
    plat_log("mappa %d (%s) completata: mostri sconfitti in totale %d, cadute %d", g.map_idx + 1, g.L.name, g.kills, g.deaths);
    award_stars(g);
  }
}

// Player i to their start in the current zone (a puff where they were and one where they arrive).
void to_zone_start(Game& g, int i) {
  Player& p = g.pl[i];
  p.leap_n = p.leap_t = 0;
  burst(g, p.pos + v3(0, 0.6f, 0), 8, i, 2.5f);
  p.pos = g.L.zones[g.zone].start[i];
  level_push_out(&g.L, &p.pos.x, &p.pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK);
  level_body_guard(&g.L, g.L.zones[g.zone].start[i].x, g.L.zones[g.zone].start[i].z, &p.pos.x, &p.pos.z, 0.0f, PL_R, PL_STEP, BODY_WALK);
  p.pos.y = level_ground(&g.L, p.pos.x, p.pos.z, 0.0f, PL_STEP);
  p.vy = 0.0f;
  p.grounded = true;
  p.tramp_t = 0;
  burst(g, p.pos + v3(0, 0.6f, 0), 8, i, 2.5f);
}

// Whoever stayed behind when the zone starts is taken to their start in the zone.
void regroup(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active || !p.alive) continue;
    if (p.leap_n == 0 && level_zone_at(&g.L, p.pos.x, p.pos.z) == g.zone) continue;
    if (p.leap_n > 0 && level_zone_at(&g.L, p.leap_b.x, p.leap_b.z) == g.zone) continue;   // lands inside
    to_zone_start(g, i);
  }
}

// 0.5.4 (review): whoever, with the gate closed, ends up in an earlier zone (went back just as the
// gate was closing, pushed...) goes back to their start in the current zone: from there they could
// no longer get out, and playing alone the zone would never end.
void strays_back(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active || !p.alive || p.leap_n > 0 || p.tramp_t > 0) continue;
    const int z = level_zone_at(&g.L, p.pos.x, p.pos.z);
    if (z < 0 || z >= g.zone) continue;
    plat_log("G%d nella zona %d a cancello chiuso: torna nella zona %d", i + 1, z + 1, g.zone + 1);
    to_zone_start(g, i);
  }
}

void compute_mvp(Game& g) {
  g.mvp = -1;
  float best = -1e30f;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    const float s = (float)p.kills * 100.0f + p.dmg_done - (float)p.deaths * 150.0f;
    if (s > best) { best = s; g.mvp = i; }
  }
}

// Swing gates (0.5.4). They open like a heavy door being pulled (they speed up, then brake and stop
// open: 50 frames) and they close by slamming: they speed up all the way (41 frames), hit with a
// thud and a bit of dust, bounce back a few degrees and stop. While turning they push away whoever
// they find in front of them (they open towards whoever is waiting for them).
namespace {
const float GATE_OPEN_ACC = 0.0016f;
const float GATE_SHUT_ACC = 0.0012f;
const float GATE_BOUNCE = 0.28f;          // of the speed of the hit
const float GATE_REST = 0.004f;           // slower bounces: at rest
const float GATE_SLAM = 0.02f;            // from this speed the hit can be heard (closed from fully open: 0.049)
const float GATE_TOP = 1.75f;             // whoever is higher up (jump, trampoline) passes over the leaves
const int GATE_DUST[NSTYLES] = { PC_SAND, PC_EARTH, PC_STONE, PC_SAND };

// A leaf in its current position (turned by th) and which way it turns (turn: +1 opens).
struct Leaf {
  Vec3 h, d, n;                                          // hinge, towards the tip, the way it turns when opening
  float len, turn;
};

Leaf gate_leaf_now(Game& g, int gi, int side, float th, float turn) {
  Leaf lf;
  Vec3 c;
  level_gate_leaf(&g.L, gi, side, th, &lf.h, &lf.d, &lf.len, &c);
  const Gate& gt = g.L.gates[gi];
  lf.n = c * -d_sin(th) + v3(gt.mx, 0.0f, gt.mz) * d_cos(th);
  lf.turn = turn;
  return lf;
}

// The player touched by the leaf is pushed out, on the side where their centre is (at the tip: away
// from the tip). true if it moved them.
bool leaf_push(Game& g, Player& p, const Leaf& lf) {
  const float rr = PL_R + GATE_LEAF_HALF;
  const Vec3 rel = v3(p.pos.x - lf.h.x, 0.0f, p.pos.z - lf.h.z);
  const float along = dot(rel, lf.d), across = dot(rel, lf.n);
  float nx, nz;
  if (along >= 0.0f && along <= lf.len) {
    if (fabsf(across) >= rr) return false;
    const float to = (across > 0.0f || (across == 0.0f && lf.turn > 0.0f) ? rr : -rr) - across;
    nx = p.pos.x + lf.n.x * to;
    nz = p.pos.z + lf.n.z * to;
  } else if (along > lf.len) {
    const Vec3 tip = lf.h + lf.d * lf.len;
    float dx = p.pos.x - tip.x, dz = p.pos.z - tip.z;
    float dd = dx * dx + dz * dz;
    if (dd >= rr * rr) return false;
    if (dd < 1e-8f) {
      dx = lf.n.x * lf.turn;
      dz = lf.n.z * lf.turn;
      dd = 1.0f;
    }
    const float l = sqrtf(dd);
    nx = tip.x + dx / l * rr;
    nz = tip.z + dz / l * rr;
  } else {
    return false;                                        // behind the hinge: inside the pillar
  }
  level_push_out(&g.L, &nx, &nz, p.pos.y, PL_R, PL_STEP, BODY_WALK | BODY_PIT);             // never into a cenote
  level_body_guard(&g.L, p.pos.x, p.pos.z, &nx, &nz, p.pos.y, PL_R, PL_STEP, BODY_WALK | BODY_PIT);
  const bool moved = nx != p.pos.x || nz != p.pos.z;
  p.pos.x = nx;
  p.pos.z = nz;
  return moved;
}

// Gate gi has moved from open0: whoever the leaves find in front of them is pushed away (several
// times if, in the middle, both of them push them, with the tips close together).
void gate_sweep(Game& g, int gi, float open0) {
  const Gate& gt = g.L.gates[gi];
  const float turn = gt.open > open0 ? 1.0f : -1.0f;
  Leaf lf[2];
  for (int side = 0; side < 2; ++side) lf[side] = gate_leaf_now(g, gi, side, gate_theta(gt, side), turn);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active || !p.alive || p.leap_n > 0 || p.pos.y > GATE_TOP) continue;
    for (int pass = 0; pass < 8; ++pass) {
      const bool a = leaf_push(g, p, lf[0]);
      const bool b = leaf_push(g, p, lf[1]);
      if (!a && !b) break;
    }
  }
}

Vec3 gate_mid(Game& g, const Gate& gt) {
  return (cell_center(&g.L, gt.x0, gt.z0) + cell_center(&g.L, gt.x1, gt.z1)) * 0.5f;
}

// The slam of the closing gate: a thud, a cloud of dust on the side of the leaves, a small shake.
void gate_slam(Game& g, int gi) {
  const Gate& gt = g.L.gates[gi];
  const Vec3 mid = gate_mid(g, gt), m = v3(gt.mx, 0.0f, gt.mz);
  const Vec3 eu = gt.horiz ? v3(1, 0, 0) : v3(0, 0, 1);
  const float half = 0.5f * (float)(gt.horiz ? gt.x1 - gt.x0 + 1 : gt.z1 - gt.z0 + 1);
  const int col = GATE_DUST[g.L.style >= 0 && g.L.style < NSTYLES ? g.L.style : 0];
  for (int k = 0; k < 14; ++k) {
    // One call to the generator per statement: the order stays the same with every compiler.
    const float r0 = rng_f(g);
    const float r1 = rng_f(g);
    const float r2 = rng_f(g);
    const float r3 = rng_f(g);
    const float r4 = rng_f(g);
    const int life = 32 + (int)(rng_next(g) % 14u);
    const float u = ((float)k - 6.5f) / 6.5f * half;
    const Vec3 at = mid + eu * u + m * (0.35f + 0.25f * r0) + v3(0.0f, 0.25f, 0.0f);
    const Vec3 vel = m * (0.5f + 0.9f * r1) + eu * (0.8f * (r2 - 0.5f)) + v3(0.0f, 0.35f + 0.35f * r3, 0.0f);
    puff(g, at, PK_PUFF, col, vel, 2.4f + 0.8f * r4, life);
  }
  ev_sound(g, SND_GATE_SHUT, mid);
  add_shake(g, 0.1f);
}
}  // namespace

void update_gates(Game& g) {
  for (int i = 0; i < g.L.ngates; ++i) {
    Gate& gt = g.L.gates[i];
    const float w = g.gate_want[i], o0 = gt.open;
    const bool rest = gt.vel == 0.0f;
    if (w >= 0.1f) {
      // Towards w (usually fully open): it speeds up and brakes in time to stop there. Any more
      // closed than this (even a gate stopped mid-bounce by a zone replayed) and it closes.
      const float dist = w - gt.open, dir = dist > 0.0f ? 1.0f : -1.0f;
      float sp = gt.vel * dir > 0.0f ? fabsf(gt.vel) : 0.0f;
      sp = fminf(sp + GATE_OPEN_ACC, sqrtf(2.0f * GATE_OPEN_ACC * fabsf(dist)));
      if (sp >= fabsf(dist)) {
        gt.open = w;
        gt.vel = 0.0f;
      } else {
        gt.vel = dir * sp;
        gt.open += gt.vel;
      }
    } else if (gt.open > 0.0f || gt.vel != 0.0f) {
      // It closes: it speeds up until the hit, bounces and falls back until it stops.
      gt.vel -= GATE_SHUT_ACC;
      gt.open += gt.vel;
      if (gt.open <= 0.0f) {
        const float hit = -gt.vel;
        gt.open = 0.0f;
        gt.vel = hit * GATE_BOUNCE;
        if (gt.vel < GATE_REST) gt.vel = 0.0f;
        if (hit >= GATE_SLAM) gate_slam(g, i);
      }
    }
    if (gt.open == o0) continue;
    if (rest && gt.vel != 0.0f) ev_sound(g, SND_GATE, gate_mid(g, gt));   // it starts turning: it creaks
    gate_sweep(g, i, o0);
  }
}

// Starts the already loaded map (g.map_idx) from zone 1, as if just chosen: 3 lives each, cards
// to choose, items, crates and capsule as at the beginning. again: the same map from the start in
// the same visit (the players' stats and restarts are kept).
static void map_start_msg(Game& g, bool again, bool log = true);

void begin_map(Game& g, bool again) {
  clear_actors(g);
  memset(g.bo, 0, sizeof(g.bo));
  memset(g.pa, 0, sizeof(g.pa));
  memset(g.ft, 0, sizeof(g.ft));
  memset(g.squash, 0, sizeof(g.squash));
  memset(g.crate_hit, 0, sizeof(g.crate_hit));
  memset(g.tramp_t, 0, sizeof(g.tramp_t));
  items_reset(g, -1);
  level_reset_dynamic(&g.L, -1);
  for (int i = 0; i < LV_MAX_GATES; ++i) g.gate_want[i] = 0.0f;
  g.loop = world_giro(maps_world(g.map_idx)) - 1;   // difficulty: the planet's round
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
    for (int k = 0; k < NPW_TIMED; ++k) p.pw_t[k] = 0;   // crate power-ups: everyone starts again without them
    p.hp_max = CHARS[p.ch].hp;
    p.joining = false;
    p.lives = START_LIVES;
    p.out = false;
  }
  cards_map_start(g);
  g.explorer_free = false;
  g.expl_t = -1;
  g.help_t = 0;
  g.stars_got = g.stars_new = g.stars_had = 0;
  g.giro_done = g.giro_up = false;
  g.giro_stars = g.giro_was = 0;
  g.zone = 0;
  place_players(g, 0);
  begin_zone(g, 0, false);
  g.wipe_cleared = g.wipe_map = false;
  g.screen = SC_PLAY;
  g.screen_t = 0;
  g.paused = false;
  map_start_msg(g, again);
  ev_camera(g, CAM_CUT_PLAY);
  cards_offer(g);     // before the first wave; the map's message is shown after the cards
}

// Big message at the start of the map (name and "pronti... via!", or "si ricomincia").
static void map_start_msg(Game& g, bool again, bool log) {
  char big[160], sub[224];
  const int w = maps_world(g.map_idx);
  if (again) {
    snprintf(big, sizeof(big), "%s", tr("SI RICOMINCIA!"));
    if (count_active(g) > 1) snprintf(sub, sizeof(sub), tr("la mappa da capo: %d vite a testa"), START_LIVES);
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
  show_msg(g, big, sub, 0xFFE14D, 2, 150);
}

void restart_map(Game& g) { begin_map(g, true); }

void play_update(Game& g, const Pad pads[MAX_PLAYERS]) {
  // Card choice open: only that counts (no pause, no joining), game stopped.
  if (g.cards_open) cards_update(g);
  else play_menus_update(g);
  if (g.screen != SC_PLAY) return;
  if (g.paused || g.cards_open) return;
  g.tick++;
  for (int i = 0; i < MAX_PLAYERS; ++i) player_update(g, i, pads[i]);
  monsters_update(g);
  boss_update(g);
  shots_update(g);
  bombs_update(g);
  items_update(g);
  effects_update(g);
  explorer_update(g);
  update_gates(g);

  if (g.phase == PH_FIGHT || g.phase == PH_CLEAR) strays_back(g);
  switch (g.phase) {
    case PH_FIGHT:
      update_spawning(g);
      break;
    case PH_ENTER: {
      g.phase_t++;
      bool all_in = true;
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g.pl[i];
        if (p.active && p.alive && level_zone_at(&g.L, p.pos.x, p.pos.z) != g.zone) all_in = false;
      }
      if (all_in || g.phase_t >= 240) {
        regroup(g);
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
      // First the cards for the next zone: they open shortly after "ZONA LIBERA!".
      if (g.cards_due > 0) {
        if (--g.cards_due == 0) cards_offer(g);
        break;
      }
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active && g.pl[i].alive && level_zone_at(&g.L, g.pl[i].pos.x, g.pl[i].pos.z) == g.zone + 1) {
          begin_zone(g, g.zone + 1, true);
          break;
        }
      break;
    }
    case PH_MAP_DONE:
      if (++g.phase_t >= 210) {
        compute_mvp(g);
        g.maps_done++;
        players_gather(g);      // whoever is down (or out of lives) celebrates with the others
        go_screen(g, SC_STATS);
        return;
      }
      break;
    case PH_WIPE:
      if (++g.phase_t >= (g.wipe_map ? WIPE_MAP_FRAMES : WIPE_ZONE_FRAMES)) {
        if (g.wipe_map) restart_map(g);
        else restart_zone(g);
      }
      break;
  }
  // Boss with no health left but still standing (it falls at the next boss_update): the team has
  // won, even if the last player fell in the same frame (lava bubble, rocket or bomb arriving after
  // the boss's turn).
  const bool boss_beaten = g.boss.alive && g.boss.hp <= 0.0f;
  if (!boss_beaten && (g.phase == PH_FIGHT || g.phase == PH_CLEAR || g.phase == PH_ENTER) && count_active(g) > 0 &&
      count_alive(g) == 0) {
    // Everyone down. Playing alone (one active player) the fall has already taken a life: with
    // lives left the zone is replayed, out of lives the map restarts from the start. With more
    // players, all down at the same moment, the map restarts from the start even with lives left.
    const bool solo = count_active(g) == 1;
    int lives = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (g.pl[i].active) lives += g.pl[i].lives;
    g.wipe_cleared = g.phase == PH_CLEAR;
    g.wipe_map = !solo || lives == 0;
    g.phase = PH_WIPE;
    g.phase_t = 0;
    ev_sound_ui(g, SND_WIPE);
    if (g.wipe_map) {
      g.map_wipes++;
      g.restarts++;
      show_msg(g, solo ? tr("VITE FINITE!") : tr("TUTTI A TERRA!"), tr("si ricomincia la mappa da capo"), 0xFF6A5A, 2, WIPE_MAP_FRAMES);
      if (solo) plat_log("vite finite: la mappa %d (%s) ricomincia da capo", g.map_idx + 1, g.L.name);
      else plat_log("tutti a terra insieme (vite rimaste in tutto: %d): la mappa %d (%s) ricomincia da capo", lives, g.map_idx + 1, g.L.name);
    } else {
      g.wipes++;
      char sub[224];
      if (lives == 1) snprintf(sub, sizeof(sub), "%s", tr("resta 1 vita: si rifà questa zona"));
      else snprintf(sub, sizeof(sub), tr("restano %d vite: si rifà questa zona"), lives);
      show_msg(g, tr("VITA PERSA!"), sub, 0xFF6A5A, 2, WIPE_ZONE_FRAMES);
      plat_log("vita persa (restano %d): mappa %d, zona %d da rifare", lives, g.map_idx + 1, g.zone + 1);
    }
  }
  g.enemies_left = g.phase == PH_FIGHT ? zone_total_left(g) : count_monsters(g);
  ev_camera(g, CAM_FOLLOW);
}

void update_pad_states(Game& g, const Pad pads[MAX_PLAYERS]) {
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

// --- shared functions ---------------------------------------------------------------

int count_active(Game& g) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active;
  return n;
}
int count_alive(Game& g) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active && g.pl[i].alive;
  return n;
}
int count_in_play(Game& g) {
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active && !g.pl[i].out;
  return n;
}
int count_monsters(const Game& g) {
  int n = 0;
  for (int i = 0; i < MAX_MONSTERS; ++i) n += g.mo[i].alive;
  return n;
}

// Planets and rounds: round and open maps are in the save, the maps in maps.h (in campaign
// order, one planet after the other).
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
  const int n = 3 * world_maps(w) - 2;      // 4 maps: 10 out of 12; with fewer maps all the stars but two
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
  return map_in_world(i) < (w == 0 && open < 1 ? 1 : open);   // the first map of Terra is always open
}

// Stronger monsters at every round of the planet (g.loop: round - 1).
float diff_hp(Game& g) { return 1.0f + 0.6f * (float)g.loop; }
float diff_dmg(Game& g) { return 1.0f + 0.25f * (float)g.loop; }
float diff_speed(Game& g) { return fminf(1.0f + 0.05f * (float)g.loop, 1.25f); }

void show_msg(Game& g, const char* big, const char* small, uint32_t rgb, int size, int frames) {
  snprintf(g.msg, sizeof(g.msg), "%s", big);
  snprintf(g.msg2, sizeof(g.msg2), "%s", small);
  g.msg_rgb = rgb;
  g.msg_size = size;
  g.msg_t = frames;
}

bool load_map_preview(Game& g, int idx) {
  if (idx < 0 || idx >= maps_count()) return false;
  if (g.level_ok && g.map_shown == idx) return true;
  g.session.level_serial++;      // another map: the views build its geometry again
  g.level_ok = maps_load(idx, &g.L, g.map_note, sizeof(g.map_note));
  if (!g.level_ok) return false;
  g.map_shown = idx;
  clear_actors(g);
  memset(g.bo, 0, sizeof(g.bo));
  memset(g.pa, 0, sizeof(g.pa));
  memset(g.ft, 0, sizeof(g.ft));
  memset(g.squash, 0, sizeof(g.squash));
  memset(g.crate_hit, 0, sizeof(g.crate_hit));
  memset(g.tramp_t, 0, sizeof(g.tramp_t));
  items_reset(g, -1);
  level_reset_dynamic(&g.L, -1);
  for (int i = 0; i < LV_MAX_GATES; ++i) g.gate_want[i] = 0.0f;
  memset(&g.boss, 0, sizeof(g.boss));
  g.zone = 0;
  return true;
}

void start_map(Game& g, int idx) {
  if (!load_map_preview(g, idx)) return;
  g.map_idx = idx;
  // The random generator restarts mixed with the frames elapsed since start-up (time in the menus
  // included): cards and waves change from one game to the next, but with the same inputs from
  // the first frame the game stays the same on every processor.
  g.rng ^= 0x9E3779B9u * (g.ui_tick + 1u);
  if (g.rng == 0u) g.rng = 0x9E3779B9u;
  g.map_wipes = 0;
  begin_map(g, false);
}

// In the menus the characters stand in a row at the start of zone 1; the camera looks at them
// from the north, so player 1 is to the east (on the left of the screen).
Vec3 menu_spot(const Game& g, int i) {
  Vec3 c = v3(0, 0, 0);
  for (int k = 0; k < 4; ++k) c = c + g.L.zones[0].start[k] * 0.25f;
  return v3(c.x + (1.5f - (float)i) * 1.25f, 0.0f, c.z);
}

// --- interface --------------------------------------------------------------------------

void game_init(Game& g) {
  if (g_inited) game_shutdown(g);
  memset(&g, 0, offsetof(Game, session));   // everything but the session, which outlives game_init (the last member)
  for (int i = 0; i < MAX_PLAYERS; ++i) g.aim_level[i] = AIM_DEFAULT;   // it stays for the whole session
  g.aa_level = 1;                                                       // antialiasing: the best the renderer can do
  g.aa_cap = 8;
  maps_init();
  g_inited = true;
  game_reset(g);
}

void game_shutdown(Game& g) {
  if (!g_inited) return;
  g_inited = false;
}

void game_reset(Game& g) {
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
  g.session.quit = false;
  bot_reset(g);
  load_map_preview(g, 0);
  go_screen(g, SC_TITLE);
}

void game_quick_start(Game& g, int map, int players, const char* chars) {
  if (map < 0 || map >= maps_count()) map = 0;
  if (players < 1) players = 1;
  if (players > MAX_PLAYERS) players = MAX_PLAYERS;
  const int nc = chars ? (int)strlen(chars) : 0;
  for (int i = 0; i < players; ++i) {
    int ch = i % NCHARS;
    if (i < nc && chars[i] >= '0' && chars[i] < '0' + NCHARS) ch = chars[i] - '0';
    player_join(g, i, ch, false);
    g.joined[i] = true;
    g.pick[i] = ch;
  }
  g.map_sel = map;
  start_map(g, map);
}

// Settings changed in the pause (aim, antialiasing, volumes; the pause puts them in save_data
// right away): the file is written when the pause closes.
static void settings_save(Game& g) {
  if (!g.session.settings_dirty || (g.screen == SC_PLAY && g.paused)) return;
  g.session.settings_dirty = false;
  save_write();
}

void game_update(Game& g, const Pad pads[MAX_PLAYERS]) {
  g.ui_tick++;
  g.screen_t++;
  // Play time of the active game (outside the first screen and its pages: saves, language, name,
  // LAN play).
  if (save_slot() >= 0 && g.screen != SC_TITLE && g.screen != SC_SLOTS && g.screen != SC_LANG && g.screen != SC_NAME &&
      g.screen != SC_NET && g.screen != SC_LOBBY && ++g.time_frames >= 60) {
    g.time_frames = 0;
    save_data()->seconds++;
  }
  update_pad_states(g, pads);
  if (g.screen == SC_PLAY) {
    play_update(g, pads);
  } else {
    screens_update(g);
    effects_update(g);
    ev_camera(g, CAM_FOLLOW);
  }
  for (int i = 0; i < MAX_PLAYERS; ++i) g.start_tap[i] = false;
  settings_save(g);
}


void game_set_events(Game& g, const GameEvents& ev) { g.session.events = ev; }

void game_start_tap(Game& g, int player) {
  if (player >= 0 && player < MAX_PLAYERS) g.start_tap[player] = true;
}

bool game_paused(Game& g) { return g.screen == SC_PLAY && g.paused; }

bool game_playing(Game& g) { return g.screen == SC_PLAY && !g.paused && !g.cards_open; }
void game_set_aa_available(Game& g, int max_samples) { g.aa_max = max_samples > 0 ? max_samples : 0; }
void game_set_aa_level(Game& g, int level) { g.aa_level = level; }
int game_aa_level(Game& g) { return g.aa_level; }
void game_aa_cap(Game& g, int samples) { g.aa_cap = samples; }
int game_aa_wanted(const Game& g) {
  if (g.aa_max <= 0 || g.aa_level <= 0) return 0;
  int want = g.aa_level == 1 ? g.aa_cap : g.aa_level;     // the best: as far as it goes, except for the cap
  if (want > g.aa_max) want = g.aa_max;
  return want;
}
void game_save_loaded(Game& g) {
  // Saved settings: aim of every slot, antialiasing, volumes, language.
  const SaveData* sd = save_data();
  for (int i = 0; i < MAX_PLAYERS && i < SAVE_PLAYERS; ++i)
    if (sd->aim[i] < NAIMLEVELS) g.aim_level[i] = sd->aim[i];
  g.aa_level = sd->antialias;
  audio_set_volume(sd->music_vol, sd->sfx_vol);
  lang_set(sd->lang);
  g.session.settings_dirty = false;
  // Tests: the quick game (bots, PUPAZZI_MAPPA) starts before the core reads the save and plays
  // in game 1 (it creates it if it does not exist); if the planet is at another round the map
  // restarts with that one.
  if (g.screen == SC_PLAY && g.tick == 0 && save_slot() < 0 && !save_slot_load(0)) save_slot_new(0);
  if (g.screen == SC_PLAY && g.tick == 0) map_start_msg(g, false, false);      // in the saved language
  if (g.screen == SC_PLAY && g.tick == 0 && g.loop != world_giro(maps_world(g.map_idx)) - 1) {
    plat_log("salvataggio letto: %s e' al giro %d, la mappa di prova riparte", WORLD_NAMES[maps_world(g.map_idx)],
             world_giro(maps_world(g.map_idx)));
    start_map(g, g.map_idx);
  }
}

void settings_changed(Game& g) { g.session.settings_dirty = true; }

void stars_late_explorer(Game& g) {
  g.stars_got |= SAVE_STAR_EXPLORER;
  if (!(g.stars_had & SAVE_STAR_EXPLORER)) g.stars_new |= SAVE_STAR_EXPLORER;
  if (g.giro_up) {
    // The next round is already open (stars from zero): this star belonged to the finished round.
    plat_log("stella dell'esploratore a mappa finita (mappa %d): il giro %d e' gia' aperto, vale solo per questa volta",
             g.map_idx + 1, g.giro_was + 1);
    return;
  }
  save_add_stars(maps_id(g.map_idx), SAVE_STAR_EXPLORER);
  plat_log("stella dell'esploratore aggiunta a mappa finita (mappa %d)", g.map_idx + 1);
  giro_check(g);      // after the boss it may be the tenth star
  save_write();
}
bool game_wants_quit(Game& g) { return g.session.quit; }
void game_request_quit(Game& g) { g.session.quit = true; }

bool game_player_active(Game& g, int i) { return i >= 0 && i < MAX_PLAYERS && g.pl[i].active; }
uint32_t game_player_rgb(int i) { return PLAYER_RGB[i >= 0 && i < MAX_PLAYERS ? i : MAX_PLAYERS]; }

uint64_t game_state_hash(Game& g) {
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
    // Lives (0.5): how many are left, down, out of lives.
    const int8_t lv[4] = { (int8_t)p.lives, (int8_t)p.active, (int8_t)p.alive, (int8_t)p.out };
    mix(lv, sizeof(lv));
    mix(p.pw_t, sizeof(p.pw_t));    // crate power-ups (0.5.4)
  }
  for (int it = 0; it < g.L.nitems; ++it)
    if (g.L.items[it].kind == IT_POWER && g.item_on[it]) {
      const uint8_t pw[2] = { (uint8_t)it, g.item_pw[it] };
      mix(pw, sizeof(pw));
    }
  // Round of the planet, current map and zone, map restarted from the start, lives of whoever left.
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
  // Gladiator (0.5.1): where it stands and where it faces, health, current action, target.
  if (g.boss.alive) { const float bf[4] = { g.boss.pos.x, g.boss.pos.z, g.boss.hp, g.boss.face }; const int bi[3] = { g.boss.act, g.boss.act_t, g.boss.target }; mix(bf, sizeof(bf)); mix(bi, sizeof(bi)); }
  // Cards: only if enabled, so with the cards off the hash stays the same as before.
  if (cards_enabled(g)) {
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
  // Swing gates (0.5.4): how open they are and how they move.
  for (int i = 0; i < g.L.ngates; ++i) {
    const float gf[2] = { g.L.gates[i].open, g.L.gates[i].vel };
    mix(gf, sizeof(gf));
  }
  // The explorer of the capsule.
  const uint8_t pf = g.explorer_free;
  mix(&pf, 1);
  if (g.expl_t >= 0) mix(&g.expl_pos, sizeof(Vec3));
  return h;
}

// Tests: all the fields of the living monsters, in exact hexadecimal, to find where two
// processors start giving different results.
void game_trace_monsters(Game& g, void (*out)(const char* line)) {
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

void game_info(Game& g, GameInfo* gi) {
  memset(gi, 0, sizeof(*gi));
  gi->screen = g.screen;
  gi->map = g.map_idx + 1;
  gi->loop = g.loop + 1;
  gi->zone = g.zone + 1;
  gi->nzones = g.L.nzones;
  gi->enemies = count_monsters(g);
  gi->players = count_active(g);
  gi->alive = count_alive(g);
  gi->phase = g.phase;
  gi->kills = g.kills;
  gi->deaths = g.deaths;
  gi->wipes = g.wipes;
  gi->maps_done = g.maps_done;
  gi->restarts = g.restarts;
  for (int i = 0; i < MAX_PLAYERS; ++i) gi->lives[i] = g.pl[i].active ? g.pl[i].lives : -1;
  snprintf(gi->map_name, sizeof(gi->map_name), "%s", g.L.name);
}

// --- LAN play (0.5.4) -----------------------------------------------------------------------------

const NetView& net_view(const Game& g) { return g.session.net_view; }
void net_req(Game& g, int req, int arg) {
  g.session.net_req = req;
  g.session.net_req_arg = arg;
}
int net_local(const Game& g) { return g.net ? g.session.net_local : -1; }
int net_wait_frames(const Game& g) { return g.net ? g.session.net_wait : 0; }
uint8_t net_silent(const Game& g) { return g.net ? g.session.net_silent : 0; }

void game_net_view(Game& g, const NetView* v) { g.session.net_view = *v; }

int game_net_request(Game& g, int* arg) {
  const int r = g.session.net_req;
  *arg = g.session.net_req_arg;
  g.session.net_req = NR_NONE;
  g.session.net_req_arg = 0;
  return r;
}

void game_net_summary(NetGame* out) {
  memset(out, 0, sizeof(*out));
  const SaveData* sd = save_data();
  memcpy(out->name, sd->name, NET_NAME_LEN);
  out->name[NET_NAME_LEN - 1] = 0;
  out->slot = save_slot() >= 0 ? save_slot() : 0;
  int w = 0;      // the farthest planet reached (as in the saves page)
  for (int k = 1; k < NWORLDS; ++k)
    if (world_maps(k) > 0 && (sd->maps_open[k] > 0 || sd->giro[k] > 1)) w = k;
  out->world = w;
  out->giro = sd->giro[w];
  out->players = 1;
  out->state = NG_OPEN;
  snprintf(out->version, sizeof(out->version), "%s", PUPAZZI_VERSION);
}

void game_net_lobby(Game& g) { go_screen(g, SC_LOBBY); }

void game_net_prepare(Game& g, NetStart* s) {
  uint32_t seed = g.rng ^ (g.ui_tick * 0x9E3779B9u) ^ 0x5A17C0DEu;
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  s->seed = seed ? seed : 1u;
  s->map_sel = g.map_sel;
  for (int i = 0; i < MAX_PLAYERS; ++i) s->aim[i] = (uint8_t)aim_level_of(g, i);
}

// The game as if the console had just been switched on, the same on all of them: everything
// that matters for the game starts again from zero and the starting map is read again from
// scratch (items, crates, gates, monsters, shots, particles). Only the things of this console
// remain (camera, antialiasing, language).
static void net_reset_sim(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    memset(&g.pl[i], 0, sizeof(Player));
    g.joined[i] = g.ready[i] = false;
    g.pick[i] = i % NCHARS;
    g.lives_kept[i] = -1;
    g.vita_kept[i] = g.stats_kept[i] = false;
    g.kills_kept[i] = g.deaths_kept[i] = 0;
    g.dmg_kept[i] = 0.0f;
    g.start_tap[i] = false;
  }
  memset(g.in, 0, sizeof(g.in));
  // Buttons held from before (the A with which the host starts the game) count only after being
  // released: the same on all the consoles.
  for (int i = 0; i < MAX_PLAYERS; ++i) g.in[i].btn = 0xFFFFu;
  memset(g.offer, 0, sizeof(g.offer));
  memset(g.last_vis, 0, sizeof(g.last_vis));
  memset(g.spawn_left, 0, sizeof(g.spawn_left));
  g.tick = g.ui_tick = 0;
  g.screen_t = 0;
  g.menu_sel = g.menu_owner = 0;
  g.chars_t = g.map_load_t = 0;
  g.slots_mode = g.slots_sel = g.slots_pop = g.slots_pop_sel = 0;
  g.lang_sel = 0;
  g.name_mode = g.name_slot = g.name_row = g.name_col = 0;
  g.name_overwrite = false;
  g.name_buf[0] = 0;
  g.pause_owner = g.pause_sel = 0;
  g.paused = g.pause_help = false;
  g.loop = g.map_idx = g.map_sel = 0;
  g.kills = g.deaths = g.wipes = g.maps_done = g.restarts = 0;
  g.boss_seq = 0;
  g.zone = g.phase = g.phase_t = 0;
  g.wave = g.spawn_cd = g.spawn_wait = g.enemies_left = 0;
  g.msg[0] = g.msg2[0] = 0;
  g.msg_t = 0;
  g.wipe_cleared = g.wipe_map = false;
  g.mvp = 0;
  g.cards_open = false;
  g.cards_t = g.cards_age = g.cards_due = 0;
  g.cards_seq = 0;
  g.explorer_free = false;
  g.expl_t = -1;
  g.expl_pos = v3(0, 0, 0);
  g.expl_yaw = 0.0f;
  g.help_t = 0;
  g.map_wipes = 0;
  g.stars_got = g.stars_new = g.stars_had = 0;
  g.giro_done = g.giro_up = false;
  g.giro_stars = g.giro_was = 0;
  g.shake = 0.0f;
  g.bots = 0;
  g.net_pop = false;
  g.lobby_sel = 0;
  g.session.quit = false;
  bot_reset(g);
  g.level_ok = false;
  g.map_shown = -1;
  load_map_preview(g, 0);
}

void game_net_begin(Game& g, const NetStart* s) {
  net_reset_sim(g);
  g.net = true;
  g.net_consoles = s->consoles;
  g.session.net_local = s->local >= 0 && s->local < MAX_PLAYERS ? s->local : 0;
  g.session.net_wait = 0;
  g.session.net_silent = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) g.aim_level[i] = s->aim[i] < NAIMLEVELS ? s->aim[i] : (uint8_t)AIM_DEFAULT;
  g.rng = s->seed ? s->seed : 0x9E3779B9u;
  g.map_sel = s->map_sel >= 0 && s->map_sel < maps_count() ? s->map_sel : 0;
  go_screen(g, SC_MODE);
  plat_log("partita in rete: questa console e' il giocatore %d (console: %s%s%s%s)", g.session.net_local + 1, (s->consoles & 1) ? "1" : "",
           (s->consoles & 2) ? " 2" : "", (s->consoles & 4) ? " 3" : "", (s->consoles & 8) ? " 4" : "");
}

void game_net_frame(Game& g, uint8_t consoles, uint8_t silent, const bool taps[MAX_PLAYERS]) {
  g.session.net_silent = silent;
  if (!g.net) return;
  const uint8_t gone = (uint8_t)(g.net_consoles & ~consoles);
  g.net_consoles = (uint8_t)(g.net_consoles & consoles);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!((gone >> i) & 1u)) continue;
    // Whoever disconnects leaves as from the pause (LASCIA LA PARTITA): their slot is freed.
    plat_log("rete: la console del giocatore %d non c'e' piu': il suo eroe esce (fotogramma %u)", i + 1, g.ui_tick);
    if (g.pl[i].active) player_leave(g, i);
    g.pl[i].joining = false;
    g.joined[i] = g.ready[i] = false;
    char big[160];
    snprintf(big, sizeof(big), tr("GIOCATORE %d È USCITO"), i + 1);
    if (g.screen == SC_PLAY) show_msg(g, big, tr("la sua console si è scollegata"), PLAYER_RGB[i], 2, 150);
    else menu_info(g, big);
  }
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (taps[i]) g.start_tap[i] = true;
}

bool game_net_over(Game& g) { return g.net && g.screen == SC_TITLE; }

void game_net_end(Game& g, int reason, bool host) {
  if (!g.net) return;
  g.net = false;
  g.net_consoles = 0;
  g.net_pop = false;
  g.session.net_local = -1;
  g.session.net_wait = 0;
  g.session.net_silent = 0;
  // The aims saved on this console again (on the network each slot had the one of its console).
  for (int i = 0; i < MAX_PLAYERS && i < SAVE_PLAYERS; ++i)
    if (save_data()->aim[i] < NAIMLEVELS) g.aim_level[i] = save_data()->aim[i];
  char buf[256];
  if (!host) {
    // This console's saves again, from the title.
    save_guest_end();
    game_reset(g);
    menu_info(g, net_error_text(g, reason, buf, sizeof(buf)));
    plat_log("partita in rete finita su questa console: si torna al titolo");
    return;
  }
  if (reason == NE_ALONE) {
    if (g.screen == SC_PLAY) show_msg(g, tr("SEI RIMASTO SOLO"), net_error_text(g, reason, buf, sizeof(buf)), 0xFFE14D, 2, 180);
    else menu_info(g, net_error_text(g, reason, buf, sizeof(buf)));
  }
  plat_log("partita in rete finita: si continua su questa console");
}

int game_net_local(Game& g) { return net_local(g); }
void game_net_wait(Game& g, int frames) { g.session.net_wait = frames; }
