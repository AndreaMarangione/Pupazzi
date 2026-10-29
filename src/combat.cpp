// Colpi, danni, esplosioni, bombe, talismani, casse che si rompono (con i bonus dalla 0.5.4) ed
// effetti (particelle, numeri dei danni, segni a terra, scossone dello schermo).
#include "game_int.h"
#include "plat.h"
#include <stdio.h>
#include <string.h>

const char* const PW_NAME[NPOWERS] = { N_("VELOCITÀ!"), N_("FURIA!"), N_("INVINCIBILE!"), N_("CARICA!") };
static const char* const PW_LOG[NPOWERS] = { "velocita'", "furia", "invincibile", "carica" };
static const int PW_SND[NPOWERS] = { SND_POWER_SPEED, SND_POWER_FURY, SND_POWER_INVINCIBLE, SND_POWER_CHARGE };

namespace {

const float BOMB_RADIUS = 3.0f, BOMB_DMG = 110.0f, BOMB_KNOCK = 9.0f;
const int BOMB_FUSE = 180;
const float PICK_DIST = 0.75f;
const float MAGNET_DIST = 3.0f;       // CALAMITA

float shot_knock(int kind) {
  switch (kind) {
    case SK_BULLET: return 0.6f;
    case SK_SHARD: return 1.2f;
    case SK_FROST: return 0.8f;
    case SK_FLAME: return 0.15f;
    default: return 0.0f;
  }
}

int shot_col(const Shot& s) {
  switch (s.kind) {
    case SK_BULLET: return PC_ENERGY;
    case SK_SHARD: return PC_MOON;
    case SK_FROST: return PC_SNOW;
    case SK_FLAME: case SK_LAVA: return PC_FIRE;
    case SK_ARROW: return PC_WOOD;
    case SK_JAVELIN: return PC_BRONZE;
    default: return PC_GREY;
  }
}

// Pezzi del colore del mostro sconfitto.
int monster_col(int type) {
  switch (type) {
    case MO_SCORPIONE: return PC_SCORPION;
    case MO_ARCIERE: return PC_CLAY;
    case MO_SCARABEO: return PC_SCARAB;
    default: return PC_CLAY;
  }
}

Bomb* bomb_new(void) {
  for (int i = 0; i < MAX_BOMBS; ++i)
    if (!g.bo[i].alive) {
      memset(&g.bo[i], 0, sizeof(Bomb));
      return &g.bo[i];
    }
  return 0;
}

// Una bomba della mappa colpita da qualcuno: si accende e scoppia dopo 3 secondi.
void light_item_bomb(int it, int owner, int fuse) {
  if (!g.item_on[it]) return;
  Bomb* b = bomb_new();
  if (!b) return;
  g.item_on[it] = false;
  const MapItem& mi = g.L.items[it];
  b->alive = true;
  b->lit = true;
  b->owner = (int8_t)owner;
  b->item = (int8_t)it;
  b->pos = cell_center(&g.L, mi.cx, mi.cz);
  b->pos.y = mi.y;
  b->fuse = fuse;
}

}  // namespace

namespace {

void kill_monster(int k, int owner) {
  Monster& m = g.mo[k];
  m.alive = false;
  g.kills++;
  // Sconfitto: pezzi del suo colore, scintille e uno sbuffo.
  const Vec3 c = monster_center(m);
  burst(c + v3(0, 0.4f, 0), 6, PC_GOLD, 3.5f);
  burst(c, 8, monster_col(m.type), 3.5f);
  burst(c, 4, PC_WHITE, 3.0f);
  for (int i = 0; i < 3; ++i) puff(c, PK_PUFF, PC_WHITE, rng_vec(1.5f, 1.0f, 1.0f, 1.5f), 0.8f, 20);
  if (m.type == MO_GUERRIERO) add_shake(0.12f);
  snd_at(SND_DEFEAT, m.pos);
  if (owner >= 0 && owner < MAX_PLAYERS) {
    Player& p = g.pl[owner];
    p.kills++;
    if (p.alive) heal_player(owner, has_card(p, CA_MERENDA) ? HEAL_KILL * 2.0f : HEAL_KILL);
  }
}

// RIMBALZO: muri, massi e alberi rimandano indietro i colpi.
bool bounces_on(int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= g.L.W || cz >= g.L.H) return false;
  const int c = g.L.cell[cz][cx];
  return c == C_WALL || c == C_ROCK || c == C_TREE;
}

// Riflette il colpo sulla faccia della casella (cx, cz) da cui e' entrato in hp: quella piu' vicina
// al punto tra le facce verso cui va il colpo.
void bounce_shot(Shot& s, Vec3 hp, int cx, int cz) {
  const float gx = hp.x + (float)g.L.W * 0.5f, gz = hp.z + (float)g.L.H;
  const float fx = s.vel.x > 0.0f ? fabsf(gx - (float)cx) : (s.vel.x < 0.0f ? fabsf(gx - (float)(cx + 1)) : 1e30f);
  const float fz = s.vel.z > 0.0f ? fabsf(gz - (float)cz) : (s.vel.z < 0.0f ? fabsf(gz - (float)(cz + 1)) : 1e30f);
  if (fx <= fz) s.vel.x = -s.vel.x;
  else s.vel.z = -s.vel.z;
  const Vec3 d = normalize(v3(s.vel.x, 0.0f, s.vel.z));
  s.pos = v3(hp.x + d.x * 0.03f, hp.y, hp.z + d.z * 0.03f);
  s.yaw = yaw_of(s.vel.x, s.vel.z);
  s.bounce--;
}

void update_straight(Shot& s) {
  const Vec3 old = s.pos;
  const float speed = length(s.vel);
  s.pos = s.pos + s.vel * DT;
  s.left -= speed * DT;
  if (s.kind == SK_FLAME) {
    s.r += 0.012f;
    s.vel = s.vel * 0.985f;
  }
  float t;
  int hx, hz;
  if (level_trace(&g.L, old, s.pos, &t, &hx, &hz)) {
    const Vec3 hp = old + (s.pos - old) * t;
    if (s.bounce > 0 && bounces_on(hx, hz)) {
      bounce_shot(s, hp, hx, hz);
      burst(hp, 2, shot_col(s), 1.2f);
    } else {
      if (s.owner >= 0 && hx >= 0 && hz >= 0 && hx < g.L.W && hz < g.L.H) {
        const int d = g.L.dyn_at[hz][hx];
        if (d >= 0 && g.L.dyn[d].type == C_BREAK) crate_damage(d, s.kind == SK_FLAME ? s.dmg * 0.6f : s.dmg, old);
      }
      s.alive = false;
      if (s.kind == SK_FLAME) puff(hp, PK_FLAME, PC_FIRE, v3(0, 1.0f, 0), 0.8f, 14);
      else burst(hp, 3, shot_col(s), 1.5f);
      return;
    }
  }
  if (s.left <= 0.0f || s.pos.y < 0.02f) {
    s.alive = false;
    if (s.kind == SK_FROST) puff(s.pos, PK_SNOW, PC_SNOW, v3(0, 0.5f, 0), 0.9f, 16);
    else if (s.kind != SK_FLAME) burst(s.pos, 2, shot_col(s), 1.0f);
    return;
  }
  // Bombe della mappa: un colpo qualunque le accende.
  for (int it = 0; it < g.L.nitems; ++it) {
    const MapItem& mi = g.L.items[it];
    if (!g.item_on[it] || mi.kind != IT_BOMB) continue;
    const Vec3 bp = cell_center(&g.L, mi.cx, mi.cz);
    if (dist2d(bp, s.pos) > 0.3f + s.r || s.pos.y > mi.y + 0.9f) continue;
    light_item_bomb(it, s.owner >= 0 ? s.owner : -1, BOMB_FUSE);
    if (s.kind != SK_FLAME) { s.alive = false; return; }
  }
  if (s.owner >= 0) {
    const Vec3 dir = normalize(v3(s.vel.x, 0.0f, s.vel.z));
    for (int k = 0; k < MAX_MONSTERS; ++k) {
      Monster& m = g.mo[k];
      if (!m.alive || m.rise > 0 || ((s.hit >> k) & 1u)) continue;
      if (dist2d(s.pos, m.pos) > monster_r(m) + s.r) continue;
      const bool fl = MONS[m.type].flying;
      const float base = fl ? m.fly - 0.6f : -0.3f, top = fl ? m.fly + 0.5f : monster_h(m) + 0.8f;
      if (s.pos.y < base || s.pos.y > top) continue;
      damage_monster(k, s.dmg, s.owner, dir, shot_knock(s.kind), !s.special);
      if (s.kind == SK_FROST && m.alive) {
        m.slow_t = 90;
        m.slow_k = 0.6f;
        puff(s.pos, PK_SNOW, PC_SNOW, v3(0, 0.6f, 0), 1.0f, 18);
      }
      if (m.alive) card_shot_hit(k, s);        // combo: bruciature, rallentamento, vapore
      if (s.kind == SK_FLAME) {
        s.hit |= 1ull << k;
        continue;
      }
      if (s.pierce > 0) {                       // PERFORANTE: passa oltre questo mostro
        s.pierce--;
        s.hit |= 1ull << k;
        burst(s.pos, 2, shot_col(s), 1.5f);
        continue;
      }
      burst(s.pos, 3, shot_col(s), 2.0f);
      s.alive = false;
      return;
    }
    if (g.boss.alive && !s.hit_boss && dist2d(s.pos, g.boss.pos) < boss_r() + s.r && s.pos.y < 4.5f) {
      damage_boss(s.dmg, s.owner, !s.special);
      if (s.kind == SK_FLAME) s.hit_boss = true;
      else if (s.pierce > 0) {
        s.pierce--;
        s.hit_boss = true;
      } else {
        burst(s.pos, 3, shot_col(s), 2.0f);
        s.alive = false;
      }
    }
    return;
  }
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& p = g.pl[j];
    if (!p.active || !p.alive) continue;
    if (dist2d(s.pos, p.pos) > PL_R + s.r) continue;
    if (s.pos.y < p.pos.y - 0.1f || s.pos.y > p.pos.y + PL_H + 0.1f) continue;
    hurt_player(j, s.dmg, old, 2.5f);
    burst(s.pos, 4, shot_col(s), 2.0f);
    s.alive = false;
    return;
  }
}

// Razzi di Astro: si allargano, poi inseguono ognuno un mostro diverso.
void update_crow(Shot& s, int idx) {
  if (s.life > 10) {
    bool ok = false;
    if (s.target >= 0) ok = g.mo[s.target].alive && g.mo[s.target].rise == 0;
    else if (s.target == -2) ok = g.boss.alive;
    if (!ok) {
      int best = -1;
      float bd = 16.0f;
      for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int k = 0; k < MAX_MONSTERS; ++k) {
          const Monster& m = g.mo[k];
          if (!m.alive || m.rise > 0) continue;
          bool taken = false;
          if (pass == 0)
            for (int o = 0; o < MAX_SHOTS; ++o)
              if (o != idx && g.sh[o].alive && g.sh[o].kind == SK_ROCKET && g.sh[o].target == k) taken = true;
          if (taken) continue;
          const float d = dist2d(m.pos, s.pos);
          if (d < bd) { bd = d; best = k; }
        }
      if (best >= 0) s.target = best;
      else s.target = g.boss.alive ? -2 : -1;
    }
    if (s.target != -1) {
      const Vec3 tp = s.target >= 0 ? monster_center(g.mo[s.target]) : g.boss.pos + v3(0, 2.0f, 0);
      const float want = yaw_of(tp.x - s.pos.x, tp.z - s.pos.z);
      const float diff = d_wrap_pi(want - s.yaw);
      s.yaw = d_wrap_pi(s.yaw + clampf(diff, -6.0f * DT, 6.0f * DT));
      s.pos.y += clampf(tp.y - s.pos.y, -2.0f * DT, 2.0f * DT);
    }
  }
  const float speed = fminf(12.0f, 6.5f + 0.12f * (float)s.life);
  s.vel = dir_yaw(s.yaw) * speed;
  s.pos = s.pos + s.vel * DT;
  if ((s.life & 3) == 0) puff(s.pos, PK_PUFF, PC_WHITE, v3(0, 0.3f, 0), 0.5f, 18);   // scia di fumo del razzo
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    Monster& m = g.mo[k];
    if (!m.alive || m.rise > 0) continue;
    if (dist2d(s.pos, m.pos) > monster_r(m) + 0.35f) continue;
    damage_monster(k, s.dmg, s.owner, dir_yaw(s.yaw), 2.0f, false);
    burst(s.pos, 8, PC_FIRE, 3.0f);
    s.alive = false;
    return;
  }
  if (g.boss.alive && dist2d(s.pos, g.boss.pos) < boss_r() + 0.3f) {
    damage_boss(s.dmg, s.owner, false);
    burst(s.pos, 8, PC_FIRE, 3.0f);
    s.alive = false;
    return;
  }
  if (--s.left <= 0.0f) {
    s.alive = false;
    burst(s.pos, 6, PC_GREY, 2.0f);
  }
}

// Colpi a parabola: bolle di lava di Zolfo e giavellotti del gladiatore.
void update_lob(Shot& s) {
  s.vel.y -= GRAVITY * DT;
  s.pos = s.pos + s.vel * DT;
  int cx, cz;
  float top = 0.0f;
  if (level_cell(&g.L, s.pos.x, s.pos.z, &cx, &cz)) top = fmaxf(0.0f, level_solid_top(&g.L, cx, cz));
  bool hit = s.pos.y <= top;
  if (!hit && s.kind == SK_LAVA && g.boss.alive && dist2d(s.pos, g.boss.pos) < boss_r() && s.pos.y < 4.5f) hit = true;
  if (!hit) {
    if ((s.life & 1) == 0) puff(s.pos, s.kind == SK_LAVA ? PK_FLAME : PK_PUFF, s.kind == SK_LAVA ? PC_FIRE : PC_WHITE, v3(0, 0, 0), s.kind == SK_LAVA ? 0.5f : 0.35f, 12);
    return;
  }
  s.alive = false;
  const Vec3 p = v3(s.pos.x, top, s.pos.z);
  if (s.kind == SK_LAVA) {
    explode(p, 1.6f, s.dmg, s.owner, 3.5f, false, EL_FIRE);
    return;
  }
  // Giavellotto: fa male ai giocatori vicini.
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (!q.active || !q.alive) continue;
    if (dist2d(q.pos, p) > 1.4f + PL_R || q.pos.y > p.y + 1.6f) continue;
    hurt_player(j, s.dmg, p, 5.0f);
  }
  burst(p + v3(0, 0.2f, 0), 10, PC_SAND, 4.0f);       // giavellotto piantato: sabbia e polvere
  for (int k = 0; k < 5; ++k) puff(p + v3(0, 0.3f, 0), PK_PUFF, PC_SAND, rng_vec(2.0f, 1.0f, 0.0f, 2.0f), 1.1f, 24);
  Marker* mk = marker_new(MK_BLAST, p, 1.4f, 12);
  (void)mk;
  add_shake(0.08f);
  snd_at(SND_JAVELIN, p);
}

// Frecce degli arcieri di terracotta: a parabola come i giavellotti (passano sopra muretti e casse),
// con una scia leggera. Dove cadono fanno male a chi sta nel cerchio rosso (raggio s.r), alzano un
// po' di polvere e restano piantate per ARROW_STUCK fotogrammi: life negativa, conta fino a 0
// (shots_update la fa salire di uno a ogni fotogramma; il disegno la affonda alla fine).
const int ARROW_STUCK = 40;

void update_arrow(Shot& s) {
  if (s.life <= 0) {
    if (s.life == 0) s.alive = false;
    return;
  }
  s.vel.y -= GRAVITY * DT;
  s.pos = s.pos + s.vel * DT;
  int cx, cz;
  float top = 0.0f;
  if (level_cell(&g.L, s.pos.x, s.pos.z, &cx, &cz)) top = fmaxf(0.0f, level_solid_top(&g.L, cx, cz));
  if (s.pos.y > top) {
    if ((s.life & 1) == 0) puff(s.pos, PK_PUFF, PC_WHITE, v3(0, 0, 0), 0.32f, 12);
    return;
  }
  if (level_pit_at(&g.L, s.pos.x, s.pos.z)) {   // finita nel cenote: sparisce giu' nel pozzo
    s.alive = false;
    return;
  }
  const Vec3 p = v3(s.pos.x, top, s.pos.z);
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (!q.active || !q.alive) continue;
    if (dist2d(q.pos, p) > s.r + PL_R || q.pos.y > p.y + 1.2f) continue;
    hurt_player(j, s.dmg, p, 2.5f);
  }
  burst(p + v3(0, 0.1f, 0), 3, PC_SAND, 1.6f);
  for (int k = 0; k < 3; ++k) puff(p + v3(0, 0.15f, 0), PK_PUFF, PC_SAND, rng_vec(0.9f, 0.5f, 0.0f, 0.9f), 0.55f, 18);
  snd_at(SND_ARROW_LAND, p);
  s.pos = p;
  s.life = -ARROW_STUCK;
}

}  // namespace

// --- effetti --------------------------------------------------------------------

void add_shake(float a) {
  if (a > g.shake) g.shake = a > 0.6f ? 0.6f : a;
}

void puff(Vec3 p, int kind, int col, Vec3 vel, float size, int life) {
  int slot = -1;
  for (int i = 0; i < MAX_PARTICLES; ++i)
    if (!g.pa[i].alive) { slot = i; break; }
  if (slot < 0) return;
  Particle& q = g.pa[slot];
  q.alive = true;
  q.kind = (uint8_t)kind;
  q.col = (uint8_t)col;
  q.life = q.life0 = life;
  q.pos = p;
  q.vel = vel;
  q.size = size;
  q.spin = rng_f() * D_TWO_PI;
}

void burst(Vec3 p, int n, int col, float speed) {
  for (int k = 0; k < n; ++k) {
    const float a = D_TWO_PI * rng_f(), s = speed * (0.4f + 0.6f * rng_f());
    const float vy = 2.0f + 3.0f * rng_f();
    const float size = 0.7f + 0.6f * rng_f();
    const int life = 22 + (int)(rng_next() % 24u);
    puff(p, PK_CUBE, col, v3(d_cos(a) * s, vy, d_sin(a) * s), size, life);
  }
}

// key identifica il bersaglio: un numero comparso da poco sullo stesso bersaglio si aggiorna invece
// di aggiungerne un altro sopra (sign: '-', '+' o 0 per nessun segno).
void add_float(Vec3 p, uint32_t rgb, int key, int value, char sign) {
  int slot = -1, oldest = 1 << 30;
  for (int i = 0; i < MAX_FLOATS; ++i) {
    FloatText& f = g.ft[i];
    if (f.alive && f.key == key && f.life > 30 && f.rgb == rgb) { slot = i; value += f.value; break; }
  }
  if (slot < 0)
    for (int i = 0; i < MAX_FLOATS; ++i) {
      if (!g.ft[i].alive) { slot = i; break; }
      if (g.ft[i].life < oldest) { oldest = g.ft[i].life; slot = i; }
    }
  FloatText& f = g.ft[slot];
  f.alive = true;
  f.life = 48;
  f.key = key;
  f.value = value;
  f.rgb = rgb;
  f.pos = p;
  if (sign) snprintf(f.txt, sizeof(f.txt), "%c%d", sign, value);
  else snprintf(f.txt, sizeof(f.txt), "%d", value);
}

void add_float_text(Vec3 p, uint32_t rgb, const char* txt) {
  int slot = 0, oldest = 1 << 30;
  for (int i = 0; i < MAX_FLOATS; ++i) {
    if (!g.ft[i].alive) { slot = i; break; }
    if (g.ft[i].life < oldest) { oldest = g.ft[i].life; slot = i; }
  }
  FloatText& f = g.ft[slot];
  f.alive = true;
  f.life = 70;
  f.key = -1;          // non si somma con nessun numero
  f.value = 0;
  f.rgb = rgb;
  f.pos = p;
  snprintf(f.txt, sizeof(f.txt), "%s", txt);
}

namespace {
const int HELP_PERIOD = 170;        // ogni quanto chiama aiuto (fotogrammi)
const float HELP_DIST = 10.0f;      // ...se qualcuno e' entro questa distanza
}

void explorer_rescue(Vec3 c) {
  if (g.explorer_free) return;
  g.explorer_free = true;
  g.expl_t = 0;
  g.expl_pos = v3(c.x, 0.0f, c.z);
  g.expl_yaw = 0.0f;
  burst(v3(c.x, 0.8f, c.z), 18, PC_GOLD, 4.5f);
  burst(v3(c.x, 0.6f, c.z), 10, PC_GREY, 3.5f);
  add_float_text(v3(c.x, 1.9f, c.z), 0xFFE14D, N_("GRAZIE!"));
  show_msg(tr("ESPLORATORE SALVATO!"), tr("una stella in più per questa mappa"), 0xFFE14D, 2, 150);
  snd_at(SND_RESCUE, c);
  if (g.phase == PH_MAP_DONE) stars_late_explorer();     // la mappa era gia' finita: la stella arriva lo stesso
  plat_log("esploratore salvato (mappa %d, zona %d)", g.map_idx + 1, g.zone + 1);
}

void explorer_update(void) {
  if (g.L.capsule_dyn < 0) return;
  const DynCell& dc = g.L.dyn[g.L.capsule_dyn];
  const Vec3 c = cell_center(&g.L, dc.cx, dc.cz);
  if (!g.explorer_free) {
    // Nella capsula: chiama aiuto quando qualcuno passa abbastanza vicino.
    if (g.help_t > 0) { g.help_t--; return; }
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& p = g.pl[j];
      if (!p.active || !p.alive || dist2d(p.pos, c) > HELP_DIST) continue;
      add_float_text(v3(c.x, 1.7f, c.z), 0xFFFFFF, N_("AIUTO!"));
      snd_at(SND_HELP, c);
      g.help_t = HELP_PERIOD;
      break;
    }
    return;
  }
  if (g.expl_t < 0) return;
  // Libero: salta fuori, festeggia, poi se ne va saltellando verso la partenza della mappa.
  g.expl_t++;
  if (g.expl_t > 70) {
    const Vec3 home = g.L.zones[0].start[0];
    const Vec3 d = v3(home.x - g.expl_pos.x, 0.0f, home.z - g.expl_pos.z);
    const float l = length(d);
    if (l > 0.5f) {
      float nx = g.expl_pos.x + d.x / l * 2.6f * DT, nz = g.expl_pos.z + d.z / l * 2.6f * DT;
      level_push_out(&g.L, &nx, &nz, 0.0f, 0.25f, PL_STEP, BODY_WALK | BODY_PIT);
      level_body_guard(&g.L, g.expl_pos.x, g.expl_pos.z, &nx, &nz, 0.0f, 0.25f, PL_STEP, BODY_WALK | BODY_PIT);
      g.expl_pos.x = nx;
      g.expl_pos.z = nz;
      g.expl_yaw = yaw_of(d.x, d.z);
    }
  } else {
    g.expl_yaw = 0.0f;     // verso la telecamera
  }
  if (g.expl_t >= 220) {
    puff(v3(g.expl_pos.x, 0.5f, g.expl_pos.z), PK_PUFF, PC_WHITE, v3(0, 0.8f, 0), 1.0f, 20);
    burst(v3(g.expl_pos.x, 0.6f, g.expl_pos.z), 8, PC_GOLD, 3.0f);
    g.expl_t = -1;
  }
}

Marker* marker_new(int kind, Vec3 pos, float r, int life) {
  for (int i = 0; i < MAX_MARKERS; ++i)
    if (!g.mk[i].alive) {
      Marker& m = g.mk[i];
      memset(&m, 0, sizeof(m));
      m.alive = true;
      m.kind = (uint8_t)kind;
      m.pos = pos;
      m.r = r;
      m.life = life;
      return &m;
    }
  return 0;
}

void effects_update(void) {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    Particle& q = g.pa[i];
    if (!q.alive) continue;
    if (--q.life <= 0) { q.alive = false; continue; }
    q.spin += 0.25f;
    switch (q.kind) {
      case PK_CUBE: case PK_LEAF: case PK_SPARK:
        q.vel.y -= (q.kind == PK_LEAF ? 4.0f : GRAVITY) * DT;
        if (q.kind == PK_LEAF) { q.vel.x *= 0.96f; q.vel.z *= 0.96f; }
        q.pos = q.pos + q.vel * DT;
        if (q.pos.y < 0.05f && q.kind != PK_SPARK) {
          q.pos.y = 0.05f;
          q.vel.y = -q.vel.y * 0.35f;
          q.vel.x *= 0.6f;
          q.vel.z *= 0.6f;
        }
        break;
      default:
        q.vel = q.vel * 0.93f;
        q.vel.y += (q.kind == PK_SNOW ? -0.6f : 0.9f) * DT;
        q.pos = q.pos + q.vel * DT;
        break;
    }
  }
  for (int i = 0; i < MAX_FLOATS; ++i) {
    FloatText& f = g.ft[i];
    if (!f.alive) continue;
    if (--f.life <= 0) { f.alive = false; continue; }
    f.pos.y += 1.3f * DT;
  }
  for (int i = 0; i < MAX_MARKERS; ++i) {
    Marker& m = g.mk[i];
    if (m.alive && ++m.t >= m.life) m.alive = false;
  }
  if (g.msg_t > 0) g.msg_t--;
  g.shake *= 0.86f;
  if (g.shake < 0.004f) g.shake = 0.0f;
  // Cespugli ed erba alta si schiacciano quando qualcuno ci passa in mezzo.
  for (int d = 0; d < g.L.ndyn; ++d) {
    const DynCell& dc = g.L.dyn[d];
    if (g.crate_hit[d] > 0) g.crate_hit[d]--;
    if (g.tramp_t[d] > 0) g.tramp_t[d]--;
    if (dc.type != C_BUSH && dc.type != C_GRASS) continue;
    const Vec3 c = cell_center(&g.L, dc.cx, dc.cz);
    float want = 0.0f;
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& p = g.pl[j];
      if (p.active && p.alive && fabsf(p.pos.x - c.x) < 0.85f && fabsf(p.pos.z - c.z) < 0.85f && p.pos.y < 0.6f) want = 1.0f;
    }
    g.squash[d] += (want - g.squash[d]) * 0.18f;
  }
}

// --- danni ------------------------------------------------------------------------

// FURIA (bonus delle casse): i danni di quel giocatore sono doppi, qualunque cosa li faccia.
static float fury(float dmg, int owner) {
  return owner >= 0 && owner < MAX_PLAYERS && g.pl[owner].pw_t[PW_FURY] > 0 ? dmg * PW_FURY_K : dmg;
}

void damage_monster(int k, float dmg, int owner, Vec3 dir, float knock, bool charges) {
  Monster& m = g.mo[k];
  if (!m.alive) return;
  dmg = fury(dmg, owner);
  m.hp -= dmg;
  const int hurt = dmg >= 4.0f ? 8 : 2;      // danni piccoli e continui (bufera): appena un guizzo
  if (m.hurt < hurt) m.hurt = hurt;
  m.shown = 150;
  m.reveal = 60;
  m.sq_v -= dmg >= 4.0f ? 2.0f : 0.3f;
  const float kk = m.type == MO_GUERRIERO ? 0.35f : (m.type == MO_SCARABEO ? 1.6f : 1.0f);
  m.kx += dir.x * knock * kk;
  m.kz += dir.z * knock * kk;
  if (knock > 0.0f) m.push_by = (int8_t)(owner >= 0 && owner < MAX_PLAYERS ? owner + 1 : 0);   // chi l'ha spinto (cenote, monsters.cpp)
  if (dmg > 0.0f)
    add_float(v3(m.pos.x, m.pos.y + (MONS[m.type].flying ? m.fly + 0.5f : monster_h(m) + 0.4f), m.pos.z), charges ? 0xFFFFFF : 0xFFE14D,
              1 + k, (int)(dmg + 0.5f), 0);
  if (owner >= 0 && dmg >= 4.0f && m.hp > 0.0f) snd_at(SND_HIT, m.pos);   // i danni piccoli e continui non fanno rumore
  if (owner >= 0 && owner < MAX_PLAYERS) {
    Player& p = g.pl[owner];
    p.dmg_done += dmg;
    if (charges) {
      const float cost = pl_special_cost(p);     // SUPER CARICA
      const bool was = p.charge >= cost;
      p.charge = fminf(cost, p.charge + dmg);
      if (!was && p.charge >= cost && p.alive) snd_at(SND_SPECIAL_READY, p.pos);
    }
  }
  if (m.hp <= 0.0f) kill_monster(k, owner);
}

void damage_boss(float dmg, int owner, bool charges) {
  Boss& b = g.boss;
  if (!b.alive || g.phase == PH_WIPE) return;   // tutti a terra: si ricomincia, i colpi in volo non contano
  dmg = fury(dmg, owner);
  if (b.roar > 0) dmg *= 0.25f;      // mentre ruggisce tra una fase e l'altra prende poco
  else if (b.act == BA_STUN) dmg *= BOSS_STUN_DMG;   // stordito dopo una carica contro un ostacolo: prende di piu'
  b.hp -= dmg;
  if (owner >= 0 && owner < MAX_PLAYERS) b.aggro[owner] += dmg;   // chi lo picchia forte se lo tira dietro
  if (b.hurt == 0 && dmg >= 4.0f) snd_at(SND_HIT, b.pos);
  b.hurt = 6;
  b.shown = 150;
  b.sq_v -= 0.6f;
  add_float(v3(b.pos.x, 4.8f, b.pos.z + 1.0f), charges ? 0xFFFFFF : 0xFFE14D, 90, (int)(dmg + 0.5f), 0);
  if (owner >= 0 && owner < MAX_PLAYERS) {
    Player& p = g.pl[owner];
    p.dmg_done += dmg;
    if (charges) {
      const bool was = p.charge >= pl_special_cost(p);
      p.charge = fminf(pl_special_cost(p), p.charge + dmg);
      if (!was && p.charge >= pl_special_cost(p) && p.alive) snd_at(SND_SPECIAL_READY, p.pos);
    }
  }
}

void explode(Vec3 p, float radius, float dmg, int owner, float knock, bool big, int elem) {
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    Monster& m = g.mo[k];
    if (!m.alive || m.rise > 0) continue;
    const float d = dist2d(m.pos, p) - monster_r(m);
    if (d > radius) continue;
    const float f = 1.0f - 0.5f * clampf(d / radius, 0.0f, 1.0f);
    Vec3 dir = v3(m.pos.x - p.x, 0.0f, m.pos.z - p.z);
    dir = length(dir) > 1e-3f ? normalize(dir) : dir_yaw(rng_f() * D_TWO_PI);
    damage_monster(k, dmg * f, owner, dir, knock * f, false);
    if (elem != EL_NONE && m.alive) card_elem_hit(k, owner, elem);   // VAPORE
  }
  if (g.boss.alive && dist2d(g.boss.pos, p) - boss_r() < radius) damage_boss(dmg * (big ? 0.7f : 1.0f), owner, false);
  if (big)
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& q = g.pl[j];
      if (!q.active || !q.alive) continue;
      const float d = dist2d(q.pos, p);
      if (d > radius) continue;
      Vec3 dir = v3(q.pos.x - p.x, 0.0f, q.pos.z - p.z);
      dir = length(dir) > 1e-3f ? normalize(dir) : dir_yaw(rng_f() * D_TWO_PI);
      const float f = 1.0f - 0.6f * clampf(d / radius, 0.0f, 1.0f);
      push_player(j, dir.x * knock * f, dir.z * knock * f, 0);
    }
  // Le altre bombe vicine si accendono e scoppiano subito dopo.
  for (int it = 0; it < g.L.nitems; ++it) {
    const MapItem& mi = g.L.items[it];
    if (g.item_on[it] && mi.kind == IT_BOMB && dist2d(cell_center(&g.L, mi.cx, mi.cz), p) < radius + 0.4f) light_item_bomb(it, owner, 15);
  }
  for (int i = 0; i < MAX_BOMBS; ++i) {
    Bomb& b = g.bo[i];
    if (b.alive && b.fuse > 15 && dist2d(b.pos, p) < radius + 0.4f) b.fuse = 15;
  }
  // Casse.
  const int r = (int)ceilf(radius);
  int pcx, pcz;
  if (level_cell(&g.L, p.x, p.z, &pcx, &pcz))
    for (int z = pcz - r; z <= pcz + r; ++z)
      for (int x = pcx - r; x <= pcx + r; ++x) {
        if (x < 0 || z < 0 || x >= g.L.W || z >= g.L.H) continue;
        const int d = g.L.dyn_at[z][x];
        if (d < 0 || g.L.dyn[d].type != C_BREAK) continue;
        if (dist2d(cell_center(&g.L, x, z), p) < radius + 0.5f) crate_damage(d, big ? 250.0f : dmg, p);
      }
  marker_new(MK_BLAST, v3(p.x, p.y, p.z), radius, big ? 16 : 12);
  const int n = big ? 16 : 8;
  for (int k = 0; k < n; ++k) {
    const float a = D_TWO_PI * (float)k / (float)n, s = radius * (2.0f + 2.0f * rng_f());
    const float vy = 1.5f + 2.0f * rng_f();
    const int life = 18 + (int)(rng_next() % 10u);
    puff(p + v3(0, 0.4f, 0), PK_FLAME, PC_FIRE, v3(d_cos(a) * s, vy, d_sin(a) * s), big ? 1.6f : 1.0f, life);
  }
  for (int k = 0; k < n / 2; ++k)
  {
    const Vec3 at = p + rng_vec(0.5f, 0.5f, 0.0f, 0.5f);
    puff(at, PK_PUFF, PC_GREY, rng_vec(2.0f, 2.0f, 1.0f, 2.0f), big ? 1.8f : 1.1f, 34);
  }
  burst(p + v3(0, 0.3f, 0), big ? 10 : 5, PC_DARK, 6.0f);
  add_shake(big ? 0.45f : 0.15f);
  snd_at(big ? SND_EXPLODE : SND_EXPLODE_SMALL, p);
}

// --- bombe, talismani, casse ------------------------------------------------------

bool bomb_drop(int i) {
  Player& p = g.pl[i];
  if (p.bombs <= 0) return false;
  Bomb* b = bomb_new();
  if (!b) return false;
  p.bombs--;
  b->alive = true;
  b->lit = true;
  b->owner = (int8_t)i;
  b->item = -1;
  b->pos = p.pos;
  b->vy = 0.0f;
  b->fuse = BOMB_FUSE;
  snd_at(SND_BOMB_DROP, p.pos);
  plat_log("giocatore %d lascia una bomba (ne restano %d)", i + 1, p.bombs);
  return true;
}

void bombs_update(void) {
  for (int i = 0; i < MAX_BOMBS; ++i) {
    Bomb& b = g.bo[i];
    if (!b.alive) continue;
    const float gnd = level_ground(&g.L, b.pos.x, b.pos.z, b.pos.y, 0.05f);
    if (b.pos.y > gnd) {
      b.vy -= GRAVITY * DT;
      b.pos.y = fmaxf(gnd, b.pos.y + b.vy * DT);
    } else {
      b.vy = 0.0f;
    }
    if ((b.fuse & 3) == 0) puff(b.pos + v3(0.06f, 0.55f, 0), PK_SPARK, PC_GOLD, rng_vec(1.0f, 2.0f, 0.0f, 1.0f), 0.5f, 8);
    if (--b.fuse > 0) continue;
    b.alive = false;
    const int before = g.kills;
    float radius = BOMB_RADIUS, dmg = BOMB_DMG;
    if (b.item < 0 && b.owner >= 0 && b.owner < MAX_PLAYERS && has_card(g.pl[b.owner], CA_BOMBE)) {
      radius *= 1.35f;                                // BOMBE GIGANTI: solo quelle lasciate da chi ha la carta
      dmg *= 1.5f;
    }
    explode(b.pos + v3(0, 0.3f, 0), radius, dmg, b.owner, BOMB_KNOCK, true);
    plat_log("bomba esplosa (accesa da %d): %d mostri sconfitti", b.owner + 1, g.kills - before);
  }
}

void items_reset(int zone) {
  for (int it = 0; it < g.L.nitems; ++it) {
    const MapItem& mi = g.L.items[it];
    if (zone >= 0 && mi.zone != zone) continue;
    g.item_on[it] = mi.crate < 0;
    g.item_t[it] = 0;
  }
  for (int i = 0; i < MAX_BOMBS; ++i) g.bo[i].alive = false;
}

void items_update(void) {
  for (int it = 0; it < g.L.nitems; ++it) {
    if (!g.item_on[it]) continue;
    g.item_t[it]++;
    const MapItem& mi = g.L.items[it];
    const Vec3 c = cell_center(&g.L, mi.cx, mi.cz);
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      Player& p = g.pl[j];
      if (!p.active || !p.alive) continue;
      const float d = dist2d(p.pos, c);
      // CALAMITA: anche da 3 metri, se niente si mette in mezzo.
      if (d > PICK_DIST && !(has_card(p, CA_CALAMITA) && d <= MAGNET_DIST &&
                             !level_blocked(&g.L, player_chest(p), v3(c.x, mi.y + 0.5f, c.z)))) continue;
      if (fabsf(p.pos.y - mi.y) > 0.8f) continue;
      if (mi.kind == IT_TALISMAN) {
        if (p.hp >= p.hp_max) continue;
        g.item_on[it] = false;
        heal_player(j, HEAL_TALISMAN);
        burst(v3(c.x, mi.y + 0.7f, c.z), 14, PC_GREEN, 3.0f);
        snd_at(SND_PICK_TALISMAN, c);
        plat_log("giocatore %d prende un talismano (vita %d)", j + 1, (int)p.hp);
      } else if (mi.kind == IT_POWER) {
        if (g.item_t[it] < 20) continue;             // prima salta fuori dalla cassa
        if (!power_take(j, g.item_pw[it])) continue;  // CARICA con lo speciale gia' pronto: resta a chi serve
        g.item_on[it] = false;
        burst(v3(c.x, mi.y + 0.7f, c.z), 14, PC_WHITE, 3.0f);
      } else {
        if (p.bombs >= pl_bombs_cap(p)) continue;     // BOMBAROLO: fino a 4
        g.item_on[it] = false;
        p.bombs++;
        snd_at(SND_PICK_BOMB, c);
        plat_log("giocatore %d raccoglie una bomba (ne ha %d)", j + 1, p.bombs);
        burst(v3(c.x, mi.y + 0.4f, c.z), 8, PC_GOLD, 2.5f);
        add_float(v3(p.pos.x, p.pos.y + PL_H + 1.2f, p.pos.z), 0xFFE14D, 300 + j, p.bombs, '+');
      }
      break;
    }
  }
}

void crate_damage(int d, float dmg, Vec3 from) {
  DynCell& dc = g.L.dyn[d];
  if (dc.type != C_BREAK || dc.hp <= 0.0f) return;
  dc.hp -= dmg;
  const bool sound = g.crate_hit[d] <= 6;      // i colpi a raffica fanno un rumore solo
  g.crate_hit[d] = 10;
  const Vec3 c = cell_center(&g.L, dc.cx, dc.cz);
  if (dc.hp > 0.0f) {
    (void)from;
    burst(v3(c.x, 0.7f, c.z), 2, dc.capsule ? PC_WHITE : PC_WOOD, 1.5f);
    if (sound) snd_at(dc.capsule ? SND_CAPSULE_HIT : SND_CRATE_HIT, c);
    return;
  }
  dc.hp = 0.0f;
  snd_at(SND_CRATE_BREAK, c);
  if (dc.capsule) {
    explorer_rescue(c);
    return;
  }
  burst(v3(c.x, 0.5f, c.z), 14, PC_WOOD, 4.0f);
  for (int k = 0; k < 4; ++k) puff(v3(c.x, 0.4f, c.z), PK_PUFF, PC_EARTH, rng_vec(1.0f, 1.2f, 0.0f, 1.0f), 1.2f, 26);
  int pw = -1;
  for (int it = 0; it < g.L.nitems; ++it)
    if (g.L.items[it].crate == d) {
      g.item_on[it] = true;
      g.item_t[it] = 0;
      if (g.L.items[it].kind == IT_POWER) {
        // Quale bonus: VELOCITA' 30%, FURIA 25%, CARICA 25%, INVINCIBILE 20%.
        const uint32_t r = rng_next() % 100u;
        pw = r < 30u ? PW_SPEED : (r < 55u ? PW_FURY : (r < 80u ? PW_CHARGE : PW_INVINCIBLE));
        g.item_pw[it] = (uint8_t)pw;
        burst(v3(c.x, 0.8f, c.z), 10, PC_GOLD, 3.0f);
      }
    }
  if (pw >= 0) plat_log("cassa rotta (riga %d, colonna %d): bonus %s", dc.cz + 1, dc.cx + 1, PW_LOG[pw]);
  else plat_log("cassa rotta (riga %d, colonna %d)", dc.cz + 1, dc.cx + 1);
}

// Il giocatore j prende il bonus pw: VELOCITA', FURIA e INVINCIBILE ripartono da pieni, CARICA
// riempie lo speciale. false se non gli serve (CARICA con lo speciale gia' pronto).
bool power_take(int j, int pw) {
  Player& p = g.pl[j];
  if (pw < 0 || pw >= NPOWERS) return false;
  if (pw == PW_CHARGE) {
    const float cost = pl_special_cost(p);
    if (p.charge >= cost) return false;
    p.charge = cost;
  } else {
    p.pw_t[pw] = PW_FRAMES[pw];
  }
  snd_at(PW_SND[pw], p.pos);
  add_float_text(v3(p.pos.x, p.pos.y + PL_H + 1.0f, p.pos.z), PW_RGB[pw], PW_NAME[pw]);
  plat_log("giocatore %d prende il bonus %s", j + 1, PW_LOG[pw]);
  return true;
}

// --- colpi --------------------------------------------------------------------------

Shot* shot_new(int kind, int owner, Vec3 pos, Vec3 vel) {
  for (int i = 0; i < MAX_SHOTS; ++i)
    if (!g.sh[i].alive) {
      Shot& s = g.sh[i];
      memset(&s, 0, sizeof(s));
      s.alive = true;
      s.kind = (uint8_t)kind;
      s.owner = (int8_t)owner;
      s.pos = pos;
      s.vel = vel;
      s.yaw = yaw_of(vel.x, vel.z);
      s.target = -1;
      return &s;
    }
  return 0;
}

void shots_update(void) {
  for (int i = 0; i < MAX_SHOTS; ++i) {
    Shot& s = g.sh[i];
    if (!s.alive) continue;
    s.life++;
    if (s.kind == SK_ROCKET) update_crow(s, i);
    else if (s.kind == SK_LAVA || s.kind == SK_JAVELIN) update_lob(s);
    else if (s.kind == SK_ARROW) update_arrow(s);
    else update_straight(s);
  }
}
