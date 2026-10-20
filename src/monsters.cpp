// Mostri della Terra (scorpione, arciere di terracotta, scarabei, guerriero di terracotta) e il boss,
// il Gladiatore gigante del Colosseo. Comportamenti della 0.4: corridore, tiratore, sciame, grosso;
// il gladiatore dalla 0.5.1 cammina, insegue e combatte (fendente, carica, crepa, giavellotti).
#include "game_int.h"
#include "plat.h"
#include <string.h>

const MonDef MONS[MO_NTYPES] = {
  // nome, vita, velocita', danno, raggio, altezza, vola
  { "scorpione", 70.0f, 3.4f, 18.0f, 0.42f, 1.0f, false },
  { "arciere di terracotta", 50.0f, 2.3f, 14.0f, 0.42f, 0.8f, false },
  { "scarabeo", 18.0f, 4.4f, 6.0f, 0.26f, 0.5f, true },
  { "guerriero di terracotta", 280.0f, 2.1f, 28.0f, 0.48f, 1.8f, false },
};

namespace {

const int RISE_FRAMES = 36;
const float SPAWN_MIN_DIST = 4.5f;
enum { S_APPROACH = 0, S_WINDUP, S_CHARGE, S_RECOVER };

uint16_t g_flow[FLOW_W * FLOW_H];
bool g_flow_ok;
int g_flow_t;

float shoot_scale(void) { return 1.0f + 0.12f * (float)g.loop; }

void boss_push_monster(Monster& m) {
  if (!g.boss.alive) return;
  const float r = monster_r(m) + boss_r();
  const float dx = m.pos.x - g.boss.pos.x, dz = m.pos.z - g.boss.pos.z, d = sqrtf(dx * dx + dz * dz);
  if (d >= r) return;
  if (d < 1e-4f) { m.pos.z += r; return; }
  m.pos.x = g.boss.pos.x + dx / d * r;
  m.pos.z = g.boss.pos.z + dz / d * r;
}

// Bersaglio: il giocatore in piedi piu' vicino che il mostro riesce a vedere.
void pick_target(Monster& m, int k) {
  if (m.target >= 0 && g.pl[m.target].alive && (g.tick + (unsigned)k) % 20u != 0u && player_visible_from(m.target, m.pos)) return;
  m.target = -1;
  float bd = 1e30f;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& p = g.pl[j];
    if (!p.active || !p.alive || !player_visible_from(j, m.pos)) continue;
    const float d = dist2d(p.pos, m.pos);
    if (d < bd) { bd = d; m.target = (int8_t)j; }
  }
}

// Velocita' voluta verso (tx, tz): in linea retta se si puo', altrimenti lungo il campo dei percorsi.
void steer(Monster& m, Vec3 to, float spd, float* dvx, float* dvz) {
  const bool fl = MONS[m.type].flying;
  const float ex = to.x - m.pos.x, ez = to.z - m.pos.z, d = sqrtf(ex * ex + ez * ez);
  if (d < 1e-4f) { *dvx = *dvz = 0.0f; return; }
  if (--m.repath <= 0) {
    m.repath = 8;
    m.direct = walk_clear(m.pos, to, monster_r(m) * 0.9f, fl);
  }
  float dx = ex / d, dz = ez / d;
  if (!m.direct && g_flow_ok) flow_dir(g_flow, m.pos.x, m.pos.z, &dx, &dz);
  *dvx = dx * spd;
  *dvz = dz * spd;
}

// Corpo per le collisioni (0.5.1): chi cammina evita il cenote come un muro, a meno che una spinta
// forte (bombe, schianto di Mochi, colpi ravvicinati) non ce lo butti dentro.
int monster_body(const Monster& m) {
  if (MONS[m.type].flying) return BODY_FLY;
  return fabsf(m.kx) + fabsf(m.kz) > 2.0f ? BODY_WALK : BODY_WALK | BODY_PIT;
}

// Applica velocita' e spinte, rispettando ostacoli e boss; restituisce quanto si e' mosso.
float move(Monster& m, float dvx, float dvz, float accel, bool face) {
  m.vx += (dvx - m.vx) * accel;
  m.vz += (dvz - m.vz) * accel;
  // Mai piu' di 0,2 m per fotogramma, spinte comprese: niente salti attraverso gli ostacoli.
  float mvx = (m.vx + m.kx) * DT, mvz = (m.vz + m.kz) * DT;
  const float mv = sqrtf(mvx * mvx + mvz * mvz);
  if (mv > 0.2f) { mvx *= 0.2f / mv; mvz *= 0.2f / mv; }
  const int body = monster_body(m);
  m.kx *= 0.85f;
  m.kz *= 0.85f;
  float nx = m.pos.x + mvx, nz = m.pos.z + mvz;
  level_push_out(&g.L, &nx, &nz, 0.0f, monster_r(m), PL_STEP, body);
  level_body_guard(&g.L, m.pos.x, m.pos.z, &nx, &nz, 0.0f, monster_r(m), PL_STEP, body);   // mai dentro un ostacolo
  const float moved = sqrtf((nx - m.pos.x) * (nx - m.pos.x) + (nz - m.pos.z) * (nz - m.pos.z));
  if (face && moved > 1e-3f) {
    const float diff = d_wrap_pi(yaw_of(nx - m.pos.x, nz - m.pos.z) - m.yaw);
    m.yaw = d_wrap_pi(m.yaw + clampf(diff, -10.0f * DT, 10.0f * DT));
  }
  m.pos.x = nx;
  m.pos.z = nz;
  boss_push_monster(m);
  level_body_guard(&g.L, nx, nz, &m.pos.x, &m.pos.z, 0.0f, monster_r(m), PL_STEP, body);
  m.phase = d_wrap_pi(m.phase + moved * 5.0f);
  return moved;
}

void face_to(Monster& m, Vec3 p, float rate) {
  const float diff = d_wrap_pi(yaw_of(p.x - m.pos.x, p.z - m.pos.z) - m.yaw);
  m.yaw = d_wrap_pi(m.yaw + clampf(diff, -rate * DT, rate * DT));
}

Vec3 goal_of(const Monster& m) {
  if (m.target >= 0) return g.pl[m.target].pos;
  return m.last_seen;
}

void touch_damage(Monster& m, float dmg, float knock, int cd) {
  if (m.cd > 0) return;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& p = g.pl[j];
    if (!p.active || !p.alive || p.pos.y > (MONS[m.type].flying ? m.fly + 0.5f : 0.8f)) continue;
    const float d = dist2d(p.pos, m.pos);
    if (d > monster_r(m) + PL_R + 0.12f) continue;
    hurt_player(j, dmg, m.pos, knock);
    m.cd = cd;
    return;
  }
}

void update_scorpion(Monster& m, int k) {
  const float r = monster_r(m);
  pick_target(m, k);
  const float slow = m.slow_t > 0 ? m.slow_k : 1.0f;
  float dvx = 0.0f, dvz = 0.0f;
  switch (m.state) {
    case S_APPROACH: {
      const Vec3 to = goal_of(m);
      const float d = dist2d(to, m.pos);
      if (d > r + PL_R) steer(m, to, m.speed * slow, &dvx, &dvz);
      move(m, dvx, dvz, 0.2f, true);
      touch_damage(m, m.dmg * 0.55f, 4.0f, 48);
      if (m.target >= 0 && m.cd == 0 && d < 7.0f && d > 1.4f && walk_clear(m.pos, to, r * 0.9f, false)) {
        m.state = S_WINDUP;
        m.state_t = 32;
        snd_at(SND_SCORPION, m.pos);
      }
      break;
    }
    case S_WINDUP:
      if (m.target >= 0) face_to(m, g.pl[m.target].pos, 8.0f);
      move(m, 0.0f, 0.0f, 0.3f, false);
      if ((m.state_t & 7) == 0) puff(m.pos + v3(0, 0.1f, 0), PK_PUFF, PC_SAND, rng_vec(1.0f, 0.6f, 0.0f, 1.0f), 0.6f, 16);
      if (--m.state_t <= 0) {
        m.state = S_CHARGE;
        m.state_t = 66;
        m.cdx = d_sin(m.yaw);
        m.cdz = d_cos(m.yaw);
        m.hitmask = 0;
      }
      break;
    case S_CHARGE: {
      const float want = 9.5f * slow;
      m.vx = m.cdx * want;
      m.vz = m.cdz * want;
      const float moved = move(m, m.vx, m.vz, 1.0f, false);
      if ((m.state_t & 3) == 0) puff(m.pos + v3(0, 0.1f, 0), PK_PUFF, PC_SAND, v3(-m.cdx, 0.5f, -m.cdz), 0.7f, 14);
      for (int j = 0; j < MAX_PLAYERS; ++j) {
        const Player& p = g.pl[j];
        if (!p.active || !p.alive || ((m.hitmask >> j) & 1u) || p.pos.y > 0.9f) continue;
        if (dist2d(p.pos, m.pos) > r + PL_R + 0.15f) continue;
        m.hitmask |= (uint8_t)(1u << j);
        hurt_player(j, m.dmg, m.pos - v3(m.cdx, 0, m.cdz), 7.0f);
        add_shake(0.12f);
      }
      if (moved < want * DT * 0.4f) {          // contro un ostacolo: resta stordito
        m.state = S_RECOVER;
        m.state_t = 50;
        m.vx = m.vz = 0.0f;
        burst(m.pos + v3(m.cdx * 0.5f, 0.6f, m.cdz * 0.5f), 6, PC_EARTH, 2.5f);
        add_shake(0.06f);
      } else if (--m.state_t <= 0) {
        m.state = S_RECOVER;
        m.state_t = 24;
      }
      break;
    }
    default:
      move(m, 0.0f, 0.0f, 0.25f, false);
      if (--m.state_t <= 0) {
        m.state = S_APPROACH;
        m.cd = (int)(90.0f / shoot_scale());
      }
      break;
  }
}

// Arciere di terracotta (tiratore, dalla 0.5.1 al posto del lama): tiene le distanze camminando di
// lato; con il bersaglio a tiro (lo vede anche sopra muretti, casse e massi) si inginocchia e tende
// l'arco per ARCHER_DRAW_FRAMES seguendolo con la mira (m.cdx, m.cdz: l'ultimo punto visto), poi
// scocca frecce lente a parabola verso dove il bersaglio sta andando: la sua posizione piu' la
// velocita' per ARCHER_LEAD volte il tempo di volo, entro il tiro. Un cerchio rosso segna dove
// cadra' ogni freccia. Un tiro su due (m.hitmask, bit 0) e' una raffica di 3 frecce: le altre due a
// ARCHER_SPREAD metri di lato, di traverso alla linea arciere-bersaglio. Dopo il tiro resta in
// ginocchio per ARCHER_REST_FRAMES.
const float ARCHER_RANGE = 9.5f, ARCHER_NEAR = 4.5f, ARROW_R = 0.8f, ARCHER_LEAD = 0.7f, ARCHER_SPREAD = 1.5f;

float arrow_time(float d) { return clampf(0.85f + 0.025f * d, 0.9f, 1.1f); }   // in volo da 0,9 a 1,1 s

void launch_arrow(const Monster& m, Vec3 from, Vec3 dest) {
  dest.y = level_ground(&g.L, dest.x, dest.z, 9.0f, 0.0f);
  const float T = arrow_time(dist2d(dest, from));
  const Vec3 vel = v3((dest.x - from.x) / T, (dest.y - from.y + 0.5f * GRAVITY * T * T) / T, (dest.z - from.z) / T);
  Shot* s = shot_new(SK_ARROW, -1, from, vel);
  if (!s) return;
  s->dmg = m.dmg;
  s->r = ARROW_R;
  s->dest = dest;
  marker_new(MK_LOB, dest, ARROW_R, (int)(T * 60.0f) + 1);
}

void shoot_arrow(Monster& m) {
  const Vec3 from = m.pos + dir_yaw(m.yaw) * ARCHER_NOCK_Z + v3(0, ARCHER_NOCK_Y, 0);
  Vec3 aim = v3(m.cdx, 0.0f, m.cdz), side = dir_yaw(m.yaw);
  if (m.target >= 0 && g.pl[m.target].alive) {           // in anticipo su dove va
    const Player& t = g.pl[m.target];
    const float lead = arrow_time(dist2d(t.pos, from)) * ARCHER_LEAD;
    aim = v3(t.pos.x + t.vx * lead, 0.0f, t.pos.z + t.vz * lead);
    const float d = dist2d(t.pos, m.pos);
    if (d > 1e-3f) side = v3((t.pos.x - m.pos.x) / d, 0.0f, (t.pos.z - m.pos.z) / d);
  }
  const float ax = aim.x - m.pos.x, az = aim.z - m.pos.z, da = sqrtf(ax * ax + az * az);
  if (da > ARCHER_RANGE) aim = v3(m.pos.x + ax * (ARCHER_RANGE / da), 0.0f, m.pos.z + az * (ARCHER_RANGE / da));
  launch_arrow(m, from, aim);
  if (m.hitmask & 1u) {                                  // raffica: altre due frecce ai lati
    side = v3(-side.z * ARCHER_SPREAD, 0.0f, side.x * ARCHER_SPREAD);
    launch_arrow(m, from, aim + side);
    launch_arrow(m, from, aim - side);
  }
  m.hitmask ^= 1u;
  snd_at(SND_BOW, m.pos);
  m.cd = (int)(110.0f * (0.8f + 0.4f * rng_f()) / shoot_scale());
  m.state = S_RECOVER;
  m.state_t = ARCHER_REST_FRAMES;
  m.sq_v -= 1.5f;
}

void update_archer(Monster& m, int k) {
  pick_target(m, k);
  const float slow = m.slow_t > 0 ? m.slow_k : 1.0f;
  bool los = false;
  float d = 99.0f;
  if (m.target >= 0) {
    const Player& t = g.pl[m.target];
    d = dist2d(t.pos, m.pos);
    if (--m.los_t <= 0) {
      m.los_t = 6;
      m.los = !level_blocked(&g.L, m.pos + v3(0, 1.35f, 0), t.pos + v3(0, 1.2f, 0));
    }
    los = m.los;
  }
  if (m.windup > 0) {                  // in ginocchio, tende l'arco seguendo il bersaglio
    move(m, 0.0f, 0.0f, 0.3f, false);
    if (m.target >= 0) {
      m.cdx = g.pl[m.target].pos.x;
      m.cdz = g.pl[m.target].pos.z;
    }
    face_to(m, v3(m.cdx, 0.0f, m.cdz), 8.0f);
    if (--m.windup == 0) shoot_arrow(m);
    return;
  }
  if (m.state == S_RECOVER) {          // si rialza
    move(m, 0.0f, 0.0f, 0.3f, false);
    if (--m.state_t <= 0) m.state = S_APPROACH;
    return;
  }
  float dvx = 0.0f, dvz = 0.0f;
  const float spd = m.speed * slow;
  if (m.target >= 0 && los && d <= ARCHER_RANGE + 1.0f) {
    const Player& t = g.pl[m.target];
    const float ux = (t.pos.x - m.pos.x) / fmaxf(d, 1e-3f), uz = (t.pos.z - m.pos.z) / fmaxf(d, 1e-3f);
    if (--m.strafe_t <= 0) { m.strafe = -m.strafe; m.strafe_t = 50 + (int)(rng_next() % 60u); }
    if (d < ARCHER_NEAR) {             // troppo vicino: indietro
      dvx = -ux * spd * 0.85f;
      dvz = -uz * spd * 0.85f;
    } else {                           // di lato, e se e' al limite del tiro un po' avanti
      const float in = d > ARCHER_RANGE - 1.5f ? 0.4f : 0.0f;
      dvx = (-uz * m.strafe * 0.7f + ux * in) * spd;
      dvz = (ux * m.strafe * 0.7f + uz * in) * spd;
    }
    move(m, dvx, dvz, 0.2f, false);
    face_to(m, t.pos, 6.0f);
    if (m.cd == 0 && d <= ARCHER_RANGE && d >= 2.0f) {
      m.windup = ARCHER_DRAW_FRAMES;
      m.cdx = t.pos.x;
      m.cdz = t.pos.z;
      m.reveal = ARCHER_DRAW_FRAMES + 40;      // nell'erba alta si fa vedere mentre mira
    }
  } else {
    steer(m, goal_of(m), spd * 0.85f, &dvx, &dvz);
    move(m, dvx, dvz, 0.2f, true);
  }
}

void update_wasp(Monster& m, int k) {
  pick_target(m, k);
  m.fly = 1.15f + 0.12f * d_sin((float)g.tick * 0.2f + (float)k);
  const float slow = m.slow_t > 0 ? m.slow_k : 1.0f;
  float dvx = 0.0f, dvz = 0.0f;
  const Vec3 to = goal_of(m);
  steer(m, to, m.speed * slow, &dvx, &dvz);
  // Zigzag laterale.
  const float z = 0.6f * d_sin((float)g.tick * 0.15f + (float)k * 1.3f);
  const float l = sqrtf(dvx * dvx + dvz * dvz);
  if (l > 1e-3f) {
    const float px = -dvz / l, pz = dvx / l;
    dvx += px * z * m.speed;
    dvz += pz * z * m.speed;
  }
  move(m, dvx, dvz, 0.15f, true);
  if (m.cd == 0)
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& p = g.pl[j];
      if (!p.active || !p.alive) continue;
      const float d = dist2d(p.pos, m.pos);
      if (d > monster_r(m) + PL_R + 0.12f || p.pos.y > m.fly + 0.4f) continue;
      hurt_player(j, m.dmg, m.pos, 2.0f);
      snd_at(SND_SCARAB, m.pos);
      m.cd = 50;
      if (d > 1e-3f) {
        m.kx -= (p.pos.x - m.pos.x) / d * 4.0f;
        m.kz -= (p.pos.z - m.pos.z) / d * 4.0f;
      }
      break;
    }
}

void update_bear(Monster& m, int k) {
  pick_target(m, k);
  const float slow = m.slow_t > 0 ? m.slow_k : 1.0f;
  float dvx = 0.0f, dvz = 0.0f;
  switch (m.state) {
    case S_APPROACH: {
      const Vec3 to = goal_of(m);
      const float d = dist2d(to, m.pos);
      if (d > monster_r(m) + PL_R + 0.1f) steer(m, to, m.speed * slow, &dvx, &dvz);
      move(m, dvx, dvz, 0.15f, true);
      if (m.target >= 0 && m.cd == 0 && d < 1.9f) {
        m.state = S_WINDUP;
        m.state_t = 30;
        snd_at(SND_WARRIOR, m.pos);
      }
      break;
    }
    case S_WINDUP:
      if (m.target >= 0) face_to(m, g.pl[m.target].pos, 5.0f);
      move(m, 0.0f, 0.0f, 0.3f, false);
      if (--m.state_t <= 0) {
        const Vec3 f = dir_yaw(m.yaw);
        for (int j = 0; j < MAX_PLAYERS; ++j) {
          const Player& p = g.pl[j];
          if (!p.active || !p.alive || p.pos.y > 1.4f) continue;
          const Vec3 d = v3(p.pos.x - m.pos.x, 0.0f, p.pos.z - m.pos.z);
          const float dl = length(d);
          if (dl > 2.3f + PL_R) continue;
          if (dl > 0.3f && dot(d * (1.0f / dl), f) < 0.34f) continue;     // circa 140 gradi davanti
          hurt_player(j, m.dmg, m.pos, 9.0f);
        }
        burst(m.pos + f * 1.2f + v3(0, 0.6f, 0), 6, PC_STONE, 3.0f);
        add_shake(0.2f);
        m.state = S_RECOVER;
        m.state_t = 30;
        m.cd = (int)(70.0f / shoot_scale());
        m.sq_v -= 3.0f;
      }
      break;
    default:
      move(m, 0.0f, 0.0f, 0.2f, false);
      if (--m.state_t <= 0) m.state = S_APPROACH;
      break;
  }
}

// Mostro che cammina buttato nel cenote da una spinta: schizzi e sconfitto; la vittoria va a chi
// l'ha spinto (push_by), se lo si sa.
void monster_fall(int k) {
  Monster& m = g.mo[k];
  for (int j = 0; j < 8; ++j) {
    const float a = D_TWO_PI * (float)j / 8.0f;
    puff(m.pos + v3(0.0f, -0.6f, 0.0f), PK_PUFF, PC_WHITE, v3(d_cos(a) * 1.4f, 3.0f, d_sin(a) * 1.4f), 0.6f, 24);
  }
  add_float_text(v3(m.pos.x, 1.4f, m.pos.z), 0xBFEFFF, N_("SPLASH!"));
  snd_at(SND_SPLASH, m.pos);
  plat_log("mostro %d (%s) caduto nel cenote", k, MONS[m.type].name);
  m.hp = 0.0f;
  damage_monster(k, 0.0f, m.push_by - 1, v3(0, 0, 0), 0.0f, false);
}

// Mostri che non si compenetrano tra loro e non entrano nei giocatori.
void separate(void) {
  Vec3 was[MAX_MONSTERS];
  for (int a = 0; a < MAX_MONSTERS; ++a) was[a] = g.mo[a].pos;
  for (int a = 0; a < MAX_MONSTERS; ++a) {
    Monster& ma = g.mo[a];
    if (!ma.alive || ma.rise > 0) continue;
    const bool fa = MONS[ma.type].flying;
    if (!fa && level_pit_at(&g.L, ma.pos.x, ma.pos.z)) {   // buttato nel cenote da una spinta
      monster_fall(a);
      continue;
    }
    for (int b = a + 1; b < MAX_MONSTERS; ++b) {
      Monster& mb = g.mo[b];
      if (!mb.alive || mb.rise > 0 || MONS[mb.type].flying != fa) continue;
      const float dx = mb.pos.x - ma.pos.x, dz = mb.pos.z - ma.pos.z, d2 = dx * dx + dz * dz;
      const float mind = monster_r(ma) + monster_r(mb);
      if (d2 >= mind * mind || d2 < 1e-8f) continue;
      const float d = sqrtf(d2), k = 0.5f * fminf(mind - d, 0.08f) / d;    // a poco a poco: niente salti
      ma.pos.x -= dx * k; ma.pos.z -= dz * k;
      mb.pos.x += dx * k; mb.pos.z += dz * k;
    }
    if (!fa)
      for (int j = 0; j < MAX_PLAYERS; ++j) {
        const Player& p = g.pl[j];
        if (!p.active || !p.alive || p.pos.y > monster_h(ma)) continue;
        const float dx = ma.pos.x - p.pos.x, dz = ma.pos.z - p.pos.z, d2 = dx * dx + dz * dz;
        const float mind = monster_r(ma) + PL_R;
        if (d2 >= mind * mind || d2 < 1e-8f) continue;
        const float d = sqrtf(d2), k = fminf(mind - d, 0.08f) / d;
        ma.pos.x += dx * k; ma.pos.z += dz * k;
      }
    const int body = monster_body(ma);
    level_push_out(&g.L, &ma.pos.x, &ma.pos.z, 0.0f, monster_r(ma), PL_STEP, body);
    level_body_guard(&g.L, was[a].x, was[a].z, &ma.pos.x, &ma.pos.z, 0.0f, monster_r(ma), PL_STEP, body);
    if (!fa && level_pit_at(&g.L, ma.pos.x, ma.pos.z)) monster_fall(a);
  }
}

// Un mostro finito fuori dalla zona mentre il cancello di uscita e' chiuso non potrebbe piu'
// essere raggiunto (e la zona non finirebbe mai): torna a un punto di comparsa.
void rescue(void) {
  const ZoneDef& z = g.L.zones[g.zone];
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    Monster& m = g.mo[k];
    if (!m.alive || m.rise > 0) continue;
    if (m.pos.x >= z.wx0 - 0.3f && m.pos.x <= z.wx1 + 0.3f && m.pos.z >= z.wz0 - 0.3f && m.pos.z <= z.wz1 + 0.3f) continue;
    const int s = k % z.nspawn;
    m.pos = cell_center(&g.L, z.spawn[s][0], z.spawn[s][1]);
    m.vx = m.vz = m.kx = m.kz = 0.0f;
    m.rise = RISE_FRAMES;
    plat_log("mostro %d riportato nella zona %d", k, g.zone + 1);
  }
}

}  // namespace

float monster_r(const Monster& m) { return MONS[m.type].r; }
float monster_h(const Monster& m) { return MONS[m.type].h; }
Vec3 monster_center(const Monster& m) {
  return MONS[m.type].flying ? v3(m.pos.x, m.fly, m.pos.z) : v3(m.pos.x, m.pos.y + monster_h(m) * 0.5f, m.pos.z);
}

bool monster_drawn(const Monster& m) {
  if (MONS[m.type].flying || m.reveal > 0 || m.hurt > 0 || m.rise > 0) return true;
  if (level_type_at(&g.L, m.pos.x, m.pos.z) != C_GRASS) return true;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (g.pl[j].active && g.pl[j].alive && dist2d(g.pl[j].pos, m.pos) < 2.6f) return true;
  return false;
}

bool monster_spawn(int type, bool summoned, const Vec3* where) {
  int slot = -1;
  for (int i = 0; i < MAX_MONSTERS; ++i)
    if (!g.mo[i].alive) { slot = i; break; }
  if (slot < 0) return false;
  const ZoneDef& z = g.L.zones[g.zone];
  Vec3 pos;
  if (where) {
    pos = *where;
  } else {
    if (z.nspawn <= 0) return false;
    const int start = (int)(rng_next() % (uint32_t)z.nspawn);
    int chosen = -1, best = 0;
    float best_score = -1e30f;
    for (int k = 0; k < z.nspawn; ++k) {
      const int idx = (start + k) % z.nspawn;
      const Vec3 sp = cell_center(&g.L, z.spawn[idx][0], z.spawn[idx][1]);
      float dmin = 99.0f;
      for (int j = 0; j < MAX_PLAYERS; ++j)
        if (g.pl[j].active && g.pl[j].alive) dmin = fminf(dmin, dist2d(g.pl[j].pos, sp));
      bool busy = false;
      for (int j = 0; j < MAX_MONSTERS; ++j)
        if (g.mo[j].alive && dist2d(g.mo[j].pos, sp) < 0.9f) busy = true;
      if (dmin >= SPAWN_MIN_DIST && !busy) { chosen = idx; break; }
      const float score = busy ? dmin - 100.0f : dmin;
      if (score > best_score) { best_score = score; best = idx; }
    }
    if (chosen < 0) chosen = best;
    pos = cell_center(&g.L, z.spawn[chosen][0], z.spawn[chosen][1]);
    pos.x += rng_s() * 0.3f;
    pos.z += rng_s() * 0.3f;
  }
  Monster& m = g.mo[slot];
  memset(&m, 0, sizeof(m));
  const MonDef& d = MONS[type];
  m.alive = true;
  m.type = (uint8_t)type;
  m.summoned = summoned;
  m.pos = v3(pos.x, 0.0f, pos.z);
  const float var = 0.92f + 0.16f * rng_f();
  m.hp_max = m.hp = d.hp * diff_hp();
  m.speed = d.speed * diff_speed() * var;
  m.dmg = d.dmg * diff_dmg();
  m.fly = d.flying ? 1.2f : 0.0f;
  m.rise = RISE_FRAMES;
  m.target = -1;
  m.strafe = (rng_next() & 1u) ? 1.0f : -1.0f;
  m.strafe_t = 3;
  m.repath = slot % 8;
  m.slow_k = 1.0f;
  m.state_t = 10 + (int)(rng_next() % 20u);
  m.cd = type == MO_ARCIERE ? 50 + (int)(rng_next() % 40u) : 30;
  m.last_seen = m.pos;
  float bd = 1e30f;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (g.pl[j].active && g.pl[j].alive) {
      const float dd = dist2d(g.pl[j].pos, m.pos);
      if (dd < bd) { bd = dd; m.yaw = yaw_of(g.pl[j].pos.x - m.pos.x, g.pl[j].pos.z - m.pos.z); m.last_seen = g.pl[j].pos; }
    }
  for (int k = 0; k < 5; ++k) puff(m.pos + v3(0, 0.1f, 0), PK_PUFF, PC_EARTH, rng_vec(1.5f, 0.8f, 0.0f, 1.5f), 0.8f, 24);
  return true;
}

void monsters_clear(void) {
  memset(g.mo, 0, sizeof(g.mo));
  g_flow_ok = false;
}

void monsters_update(void) {
  // Dove i mostri cercano i giocatori: dove li vedono, oppure dove li hanno visti l'ultima volta.
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (g.pl[j].active && g.pl[j].alive && !g.pl[j].hidden) g.last_vis[j] = g.pl[j].pos;
  if (++g_flow_t >= 12 || !g_flow_ok) {
    g_flow_t = 0;
    Vec3 goals[MAX_PLAYERS];
    int n = 0;
    for (int j = 0; j < MAX_PLAYERS; ++j)
      if (g.pl[j].active && g.pl[j].alive) goals[n++] = g.pl[j].hidden ? g.last_vis[j] : g.pl[j].pos;
    if (n > 0) {
      flow_build(g_flow, goals, n, g.zone);
      g_flow_ok = true;
    }
  }
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    Monster& m = g.mo[k];
    if (!m.alive) continue;
    if (m.rise > 0) { m.rise--; continue; }
    if (m.burn_t > 0) {                 // carte combo: brucia (puo' anche finire qui)
      card_burn_tick(k);
      if (!m.alive) continue;
    }
    if (m.vapor_t > 0) m.vapor_t--;
    if (m.hurt > 0) m.hurt--;
    if (m.shown > 0) m.shown--;
    if (m.cd > 0) m.cd--;
    if (m.reveal > 0) m.reveal--;
    if (m.slow_t > 0 && --m.slow_t == 0) m.slow_k = 1.0f;
    m.sq_v += (-300.0f * m.sq - 15.0f * m.sq_v) * DT;
    m.sq += m.sq_v * DT;
    m.sq = clampf(m.sq, -0.4f, 0.4f);
    if (m.target >= 0) m.last_seen = g.pl[m.target].pos;
    switch (m.type) {
      case MO_SCORPIONE: update_scorpion(m, k); break;
      case MO_ARCIERE: update_archer(m, k); break;
      case MO_SCARABEO: update_wasp(m, k); break;
      default: update_bear(m, k); break;
    }
  }
  separate();
  if (g.phase == PH_FIGHT) rescue();
}

// --- boss: il Gladiatore gigante ----------------------------------------------------------------
//
// Dalla 0.5.1 cammina e combatte. Insegue il giocatore in gioco piu' vicino (lo tiene almeno 3
// secondi, a meno che un altro sia molto piu' vicino o lo stia picchiando forte), piu' piano degli
// eroi; gira attorno a colonne, casse e statue con un campo dei percorsi tutto suo (celle da mezzo
// metro in cui ci sta con il suo raggio) e se resta fermo contro qualcosa fa un passo di lato. Tra
// un attacco e l'altro una pausa in cui cammina; l'attacco si sceglie dalla distanza, a turno (mai
// tre volte di fila lo stesso):
//  - fendente, fino a 3,6 m: il settore rosso davanti a lui si riempie, poi la spadata;
//  - spaccaterra, da 3,6 a 9 m: la crepa dai suoi piedi verso il bersaglio;
//  - carica con lo scudo, da 5 a 14 m con la strada libera: la corsia rossa, poi la corsa; contro
//    un ostacolo resta stordito e i colpi fanno di piu' (BOSS_STUN_DMG, in damage_boss);
//  - giavellotti a parabola da lontano (e da 3,6 a 5 m, a turno con lo spaccaterra) e subito dopo
//    una carica: 1, 2 o 3 secondo la fase.
// A 2/3 e a 1/3 della vita ruggisce (intanto prende poco) e chiama i rinforzi dalle botole; a ogni
// fase cammina piu' svelto e prepara gli attacchi piu' in fretta.

namespace {

// Vita: BOSS_HP da solo al giro 1, per (0,5 + 0,5 a giocatore) e per il giro (diff_hp). Nella 0.5 era
// 2600 per (0,6 + 0,4 a giocatore): fermo, finiva in meno di un minuto.
const float BOSS_HP = 4200.0f;
const float SPEED[3] = { 2.4f, 2.7f, 3.0f };      // camminata (gli eroi vanno a 4,7-5)
const float WIND_K[3] = { 1.0f, 0.85f, 0.72f };   // preparazioni piu' corte a ogni fase
const int PAUSE[3] = { 58, 46, 34 };              // pausa tra gli attacchi (piu' fino a 17 fotogrammi)
const int SWEEP_WIND = 33, SWEEP_HIT = 8, SWEEP_REST = 30;   // fendente: preparazione, colpo, ripresa
const int RUSH_WIND = 48, RUSH_REST = 30;         // carica: preparazione; ripresa se non sbatte
const int THROW_WIND = 30, THROW_REST = 14;       // giavellotti: lancio, ripresa
const int SLAM_WIND = 14, SLAM_REST = 46;         // spaccaterra: la spada a terra, ripresa
const int SUMMON_AT = 24, SUMMON_FRAMES = 40, ROAR_FRAMES = 70;
const float SWEEP_REACH = 3.6f;                   // bersaglio piu' vicino di cosi': fendente
const float SLAM_MAX = 9.0f;
const float RUSH_MIN = 5.0f, RUSH_MAX = 14.0f;    // bersaglio a questa distanza con la strada libera: carica
const float RUSH_SPEED = 11.0f;
const float RUSH_OVER = 3.0f, RUSH_LEN = 15.0f;   // la corsa va 3 m oltre il bersaglio, al massimo 15 m...
const float RUSH_WALL = 1.0f;                     // ...e con un ostacolo subito dopo arriva fino a sbatterci
const float STOP_D = 2.4f;                        // piu' vicino di cosi' al bersaglio non si avvicina
const int TARGET_KEEP = 180;                      // tiene il bersaglio almeno 3 secondi
const int MONSTER_CAP = 10;                       // rinforzi: mai piu' di cosi' mostri in campo
const float BCELL = 0.5f;

uint8_t g_bfree[FLOW_W * FLOW_H];                 // 1: il gladiatore ci sta (nessun ostacolo entro il suo raggio)
uint16_t g_bfield[FLOW_W * FLOW_H];               // passi (10 ogni mezzo metro) fino al bersaglio lungo le celle libere
int16_t g_hpos[FLOW_W * FLOW_H];                  // posto nel mucchio di Dijkstra (-1 fuori, -2 fatta)
uint16_t g_heap[FLOW_W * FLOW_H];
int g_hn, g_bw, g_bh, g_bfree_t, g_bfield_t, g_bcrates;
bool g_bfield_ok;
Vec3 g_bgoal;

int stage_of(void) { return g.boss.stage < 0 ? 0 : (g.boss.stage > 2 ? 2 : g.boss.stage); }
int wind_of(int frames) { return (int)((float)frames * WIND_K[stage_of()] + 0.5f); }
bool in_play(int j) { return g.pl[j].active && g.pl[j].alive; }
bool has_target(void) { return g.boss.target >= 0 && in_play(g.boss.target); }

void face_toward(float yaw, float rate) {
  Boss& b = g.boss;
  b.face = d_wrap_pi(b.face + clampf(d_wrap_pi(yaw - b.face), -rate * DT, rate * DT));
}

// --- dove ci sta e per dove passa ---

// Il cerchio di raggio r in (x, z) non tocca nessuna casella che ferma chi cammina, con il centro
// nella zona del boss (nelle mappe fatte a mano con piu' zone non esce dal cancello ancora aperto)?
bool fits(float x, float z, float r) {
  if (level_zone_at(&g.L, x, z) != g.zone) return false;
  const float fx = x + (float)g.L.W * 0.5f, fz = z + (float)g.L.H;
  const int x0 = (int)floorf(fx - r), x1 = (int)floorf(fx + r), z0 = (int)floorf(fz - r), z1 = (int)floorf(fz + r);
  for (int cz = z0; cz <= z1; ++cz)
    for (int cx = x0; cx <= x1; ++cx) {
      if (!level_blocks(&g.L, cx, cz, 0.0f, PL_STEP, BODY_WALK | BODY_PIT)) continue;
      const float qx = clampf(fx, (float)cx, (float)cx + 1.0f), qz = clampf(fz, (float)cz, (float)cz + 1.0f);
      const float dx = fx - qx, dz = fz - qz;
      if (dx * dx + dz * dz < r * r) return false;
    }
  return true;
}

// Da a a b in linea retta senza toccare nulla.
bool line_ok(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x, dz = b.z - a.z, l = sqrtf(dx * dx + dz * dz);
  const int n = (int)(l / 0.3f) + 1;
  for (int k = 1; k <= n; ++k) {
    const float f = (float)k / (float)n;
    if (!fits(a.x + dx * f, a.z + dz * f, boss_r())) return false;
  }
  return true;
}

// Corsia libera della carica da from verso yaw, al massimo RUSH_LEN metri; *wall: finisce contro
// un ostacolo (non solo perche' e' arrivata alla lunghezza massima).
float lane_free(Vec3 from, float yaw, bool* wall) {
  const Vec3 d = dir_yaw(yaw);
  float s = 0.0f;
  *wall = false;
  while (s < RUSH_LEN) {
    if (!fits(from.x + d.x * (s + 0.2f), from.z + d.z * (s + 0.2f), boss_r() - 0.02f)) {
      *wall = true;
      break;
    }
    s += 0.2f;
  }
  return fminf(s, RUSH_LEN);
}

int bcell(float x, float z) {
  const float fx = (x + (float)g.L.W * 0.5f) / BCELL, fz = (z + (float)g.L.H) / BCELL;
  if (fx < 0.0f || fz < 0.0f) return -1;
  const int cx = (int)fx, cz = (int)fz;
  if (cx >= g_bw || cz >= g_bh) return -1;
  return cz * g_bw + cx;
}

Vec3 bcenter(int c) {
  return v3(-(float)g.L.W * 0.5f + ((float)(c % g_bw) + 0.5f) * BCELL, 0.0f, -(float)g.L.H + ((float)(c / g_bw) + 0.5f) * BCELL);
}

// Casse e anfore ancora intere (quando se ne rompe una le celle libere si rifanno).
int crates_whole(void) {
  int n = 0;
  for (int i = 0; i < g.L.ndyn; ++i) n += g.L.dyn[i].type == C_BREAK && g.L.dyn[i].hp > 0.0f;
  return n;
}

void bfree_build(void) {
  g_bw = g.L.W * 2;
  g_bh = g.L.H * 2;
  g_bcrates = crates_whole();
  for (int c = 0; c < g_bw * g_bh; ++c) {
    const Vec3 p = bcenter(c);
    g_bfree[c] = fits(p.x, p.z, boss_r()) ? 1 : 0;
  }
}

// Campo dei percorsi (Dijkstra con un mucchio): partono le celle libere entro 2,5 m dal bersaglio
// (5 m se non ce ne sono), ognuna con la sua distanza; si passa solo dalle celle in cui ci sta.
bool hless(int a, int b) { return g_bfield[a] < g_bfield[b] || (g_bfield[a] == g_bfield[b] && a < b); }
void hswap(int i, int j) {
  const uint16_t t = g_heap[i];
  g_heap[i] = g_heap[j];
  g_heap[j] = t;
  g_hpos[g_heap[i]] = (int16_t)i;
  g_hpos[g_heap[j]] = (int16_t)j;
}
void hup(int i) {
  while (i > 0) {
    const int p = (i - 1) / 2;
    if (!hless(g_heap[i], g_heap[p])) break;
    hswap(i, p);
    i = p;
  }
}
void hdown(int i) {
  for (;;) {
    const int l = 2 * i + 1, r = l + 1;
    int m = i;
    if (l < g_hn && hless(g_heap[l], g_heap[m])) m = l;
    if (r < g_hn && hless(g_heap[r], g_heap[m])) m = r;
    if (m == i) return;
    hswap(i, m);
    i = m;
  }
}
void hset(int c, int d) {        // la cella c scende alla distanza d
  g_bfield[c] = (uint16_t)d;
  if (g_hpos[c] == -1) {
    g_heap[g_hn] = (uint16_t)c;
    g_hpos[c] = (int16_t)g_hn;
    hup(g_hn++);
  } else if (g_hpos[c] >= 0) {
    hup(g_hpos[c]);
  }
}
int hpop(void) {
  const int c = g_heap[0];
  g_hpos[c] = -2;
  if (--g_hn > 0) {
    g_heap[0] = g_heap[g_hn];
    g_hpos[g_heap[0]] = 0;
    hdown(0);
  }
  return c;
}

const int DX8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DZ8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// Vicina j della cella c percorribile (dentro, libera, senza tagliare gli angoli)? -1 se no.
int bstep(int c, int j) {
  const int cx = c % g_bw, cz = c / g_bw, x = cx + DX8[j], z = cz + DZ8[j];
  if (x < 0 || z < 0 || x >= g_bw || z >= g_bh) return -1;
  const int n = z * g_bw + x;
  if (!g_bfree[n]) return -1;
  if (j >= 4 && (!g_bfree[cz * g_bw + x] || !g_bfree[z * g_bw + cx])) return -1;
  return n;
}

void bfield_build(Vec3 goal) {
  const int n = g_bw * g_bh;
  for (int i = 0; i < n; ++i) {
    g_bfield[i] = FLOW_FAR;
    g_hpos[i] = -1;
  }
  g_hn = 0;
  g_bfield_ok = false;
  g_bgoal = goal;
  const int gc = bcell(goal.x, goal.z);
  if (gc < 0) return;
  const int gx = gc % g_bw, gz = gc / g_bw;
  for (int r = 5; r <= 10 && g_hn == 0; r += 5)
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        const int x = gx + dx, z = gz + dz;
        if (x < 0 || z < 0 || x >= g_bw || z >= g_bh || !g_bfree[z * g_bw + x]) continue;
        const int c = z * g_bw + x;
        const float d = dist2d(bcenter(c), goal);
        if (d > (float)r * BCELL) continue;
        const int cost = (int)(d * 20.0f + 0.5f);
        if (cost < g_bfield[c]) hset(c, cost);
      }
  if (g_hn == 0) return;
  while (g_hn > 0) {
    const int c = hpop();
    for (int j = 0; j < 8; ++j) {
      const int nb = bstep(c, j);
      if (nb < 0 || g_hpos[nb] == -2) continue;
      const int nd = g_bfield[c] + (j < 4 ? 10 : 14);
      if (nd < g_bfield[nb]) hset(nb, nd);
    }
  }
  g_bfield_ok = true;
}

// Dove andare lungo il campo: la cella piu' avanti sul percorso in discesa (fino a 6 passi) che si
// raggiunge in linea retta. WAY_NONE se il campo non arriva fin qui, WAY_THERE se e' gia' nella
// cella piu' vicina al bersaglio che riesce a raggiungere (il bersaglio sta dove lui non passa).
enum { WAY_NONE = 0, WAY_GO, WAY_THERE };
int bfield_way(Vec3 p, Vec3* way) {
  if (!g_bfield_ok) return WAY_NONE;
  const int c = bcell(p.x, p.z);
  if (c < 0) return WAY_NONE;
  if (!g_bfree[c] || g_bfield[c] == FLOW_FAR) {
    // Spinto vicino a un ostacolo, fuori dalle celle buone: la cella buona vicina che conviene di piu'.
    const int cx = c % g_bw, cz = c / g_bw;
    int best = -1, bv = 1 << 30;
    for (int dz = -4; dz <= 4; ++dz)
      for (int dx = -4; dx <= 4; ++dx) {
        const int x = cx + dx, z = cz + dz;
        if (x < 0 || z < 0 || x >= g_bw || z >= g_bh) continue;
        const int nb = z * g_bw + x;
        if (!g_bfree[nb] || g_bfield[nb] == FLOW_FAR) continue;
        const int v = g_bfield[nb] + 10 * ((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz));
        if (v < bv) { bv = v; best = nb; }
      }
    if (best < 0) return WAY_NONE;
    *way = bcenter(best);
    return WAY_GO;
  }
  int cur = c, pick = -1;
  for (int k = 0; k < 6; ++k) {
    int bn = -1, bv = g_bfield[cur];
    for (int j = 0; j < 8; ++j) {
      const int nb = bstep(cur, j);
      if (nb >= 0 && g_bfield[nb] < bv) { bv = g_bfield[nb]; bn = nb; }
    }
    if (bn < 0 || (pick >= 0 && !line_ok(p, bcenter(bn)))) break;
    pick = cur = bn;
  }
  if (pick < 0) return WAY_THERE;
  *way = bcenter(pick);
  return WAY_GO;
}

// Dove comincia: la casella B, spostata quanto basta se li' non ci sta o se e' a meno di 4 m dalle
// partenze dei giocatori (mappe fatte a mano).
Vec3 spawn_spot(void) {
  const ZoneDef& z = g.L.zones[g.zone];
  auto ok = [&](Vec3 p) {
    if (!fits(p.x, p.z, boss_r())) return false;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (dist2d(p, z.start[i]) < 4.0f) return false;
    return true;
  };
  const Vec3 B = g.L.boss_pos;
  if (ok(B)) return B;
  Vec3 best = B;
  float bd = 1e30f;
  for (int c = 0; c < g_bw * g_bh; ++c) {
    const Vec3 p = bcenter(c);
    if (!g_bfree[c] || level_zone_at(&g.L, p.x, p.z) != g.zone || !ok(p)) continue;
    const float d = dist2d(p, B);
    if (d < bd) { bd = d; best = p; }
  }
  plat_log("boss: la casella B non va bene (ostacolo o partenze vicine), parte da %.1f %.1f", (double)best.x, (double)best.z);
  return best;
}

// --- bersaglio ---

void update_target(void) {
  Boss& b = g.boss;
  for (int j = 0; j < MAX_PLAYERS; ++j) b.aggro[j] *= 0.985f;     // circa 60% in meno ogni secondo
  int near = -1;
  float nd = 1e30f;
  for (int pass = 0; pass < 2 && near < 0; ++pass)                 // prima chi si vede (non nascosto)
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      if (!in_play(j) || (pass == 0 && !player_visible_from(j, b.pos))) continue;
      const float d = dist2d(g.pl[j].pos, b.pos);
      if (d < nd) { nd = d; near = j; }
    }
  if (b.target_t < 100000) b.target_t++;
  if (!has_target()) {
    b.target = near;
    b.target_t = 0;
    return;
  }
  // Chi lo picchia forte (almeno 120 di recente e due volte e mezza il bersaglio) se lo tira dietro.
  int to = -1;
  float ha = 120.0f;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (j != b.target && in_play(j) && b.aggro[j] > ha && b.aggro[j] > 2.5f * b.aggro[b.target]) {
      ha = b.aggro[j];
      to = j;
    }
  if (to < 0 || b.target_t < 60) {
    to = -1;
    if (near < 0 || near == b.target) return;
    const float td = dist2d(g.pl[b.target].pos, b.pos);
    const bool lost = !player_visible_from(b.target, b.pos) && b.target_t >= 30;   // si e' nascosto
    const bool much = b.target_t >= 60 && nd < td * 0.5f && td - nd > 3.0f;       // un altro molto piu' vicino (dopo 1 s)
    const bool kept = b.target_t >= TARGET_KEEP && nd < td - 1.5f;                 // dopo 3 secondi: piu' vicino
    if (lost || much || kept) to = near;
  }
  if (to >= 0 && to != b.target) {
    b.target = to;
    b.target_t = 0;
  }
}

// --- attacchi ---

void act_begin(int act);

void act_end(void) {
  Boss& b = g.boss;
  b.act = BA_NONE;
  b.act_t = 0;
  b.cd = PAUSE[stage_of()] + (int)(rng_next() % 18u);
  if (b.queued != BA_NONE) {             // giavellotti subito dopo una carica
    const int q = b.queued;
    b.queued = BA_NONE;
    if (has_target()) act_begin(q);
  }
}

// Giavellotti a parabola: il primo al bersaglio, gli altri agli altri eroi (o ai suoi lati), dove
// saranno tra poco; un cerchio rosso dove cadono.
void launch_javelins(void) {
  Boss& b = g.boss;
  const int n = 1 + stage_of();
  int who[MAX_PLAYERS], na = 0;
  if (has_target()) who[na++] = b.target;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (j != b.target && in_play(j)) who[na++] = j;
  if (na == 0) return;
  const Vec3 start = boss_javelin_start(b);
  for (int k = 0; k < n; ++k) {
    const Player& p = g.pl[who[k % na]];
    Vec3 dest = p.pos + v3(p.vx * 0.6f, 0.0f, p.vz * 0.6f);
    if (k >= na) {
      const float ux = p.pos.x - b.pos.x, uz = p.pos.z - b.pos.z, ul = fmaxf(sqrtf(ux * ux + uz * uz), 0.1f);
      const float side = ((k - na) & 1) ? -2.4f : 2.4f;
      dest.x -= uz / ul * side;
      dest.z += ux / ul * side;
    }
    dest = dest + rng_vec(0.8f, 0.0f, 0.0f, 0.8f);
    dest.y = level_ground(&g.L, dest.x, dest.z, 9.0f, 0.0f);
    const float T = 1.0f + 0.12f * rng_f();
    const Vec3 vel = v3((dest.x - start.x) / T, (dest.y - start.y + 0.5f * GRAVITY * T * T) / T, (dest.z - start.z) / T);
    Shot* s = shot_new(SK_JAVELIN, -2, start, vel);
    if (!s) break;
    s->dmg = 18.0f * diff_dmg();
    s->r = 0.22f;
    s->dest = dest;
    marker_new(MK_LOB, dest, 1.4f, (int)(T * 60.0f) + 1);
  }
  b.sq_v -= 1.5f;
}

// Spaccaterra: la spada a terra e la crepa (una, tre nell'ultima fase) dai suoi piedi verso il
// bersaglio; le pietre a punta spuntano prima vicino a lui e poi via via piu' lontano.
void start_slam(void) {
  Boss& b = g.boss;
  const int lines = b.stage >= 2 ? 3 : 1;
  const float k = WIND_K[stage_of()];
  for (int l = 0; l < lines; ++l) {
    const float a = b.act_yaw + (lines > 1 ? 0.38f * (float)(l - 1) : 0.0f);
    const Vec3 d = dir_yaw(a);
    for (float s = boss_r() + 0.6f; s < 15.0f; s += 0.8f) {
      const Vec3 p = b.pos + d * s;
      int cx, cz;
      if (!level_cell(&g.L, p.x, p.z, &cx, &cz) || level_walk_top(&g.L, cx, cz) >= BLOCK_ALL) break;
      Marker* m = marker_new(MK_ROOT, v3(p.x, 0.0f, p.z), 0.75f, 0);
      if (!m) break;
      m->yaw = a;
      m->open_at = b.wind + (int)(s * 0.8f);
      m->fire_at = m->open_at + (int)((34.0f + 1.7f * s) * k);
      m->life = m->fire_at + 28;
    }
  }
  b.sq_v -= 2.0f;
  snd_at(SND_SLASH, b.pos);
}

// Fendente: chi sta nel settore davanti a lui (anche solo con un pezzo) prende la spadata e vola via.
void sweep_strike(void) {
  Boss& b = g.boss;
  b.face = b.act_yaw;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& p = g.pl[j];
    if (!in_play(j) || ((b.hitmask >> j) & 1u) || p.pos.y > 2.4f) continue;
    const float dx = p.pos.x - b.pos.x, dz = p.pos.z - b.pos.z, dl = sqrtf(dx * dx + dz * dz);
    if (dl > BOSS_SWEEP_R + PL_R) continue;
    if (dl > 0.5f && fabsf(d_wrap_pi(yaw_of(dx, dz) - b.act_yaw)) > BOSS_SWEEP_HALF + PL_R / dl) continue;
    if (level_blocked(&g.L, b.pos + v3(0, 1.2f, 0), player_chest(p))) continue;   // dietro un muro o una statua
    b.hitmask |= (uint8_t)(1u << j);
    hurt_player(j, 28.0f * diff_dmg(), b.pos, 10.0f);
  }
  for (int k = 0; k < 7; ++k) {                 // sabbia lungo l'arco della spada
    const float a = b.act_yaw + BOSS_SWEEP_HALF * ((float)k / 3.0f - 1.0f);
    const Vec3 d = dir_yaw(a);
    puff(b.pos + d * 3.3f + v3(0, 0.3f, 0), PK_PUFF, PC_SAND, d * 2.5f + v3(0, 0.6f, 0), 1.0f, 22);
  }
  add_shake(0.15f);
  snd_at(SND_BOSS_SWEEP, b.pos);
  b.sq_v -= 1.5f;
}

// Anfore e casse davanti a lui quando sbatte: si rompono (la capsula dell'esploratore no).
void smash(Vec3 at) {
  int cx, cz;
  if (!level_cell(&g.L, at.x, at.z, &cx, &cz)) return;
  for (int z = cz - 1; z <= cz + 1; ++z)
    for (int x = cx - 1; x <= cx + 1; ++x) {
      if (x < 0 || z < 0 || x >= g.L.W || z >= g.L.H) continue;
      const int k = g.L.dyn_at[z][x];
      if (k < 0 || g.L.dyn[k].type != C_BREAK || g.L.dyn[k].capsule || g.L.dyn[k].hp <= 0.0f) continue;
      if (dist2d(cell_center(&g.L, x, z), at) > 1.3f) continue;
      crate_damage(k, 1000.0f, g.boss.pos);
    }
}

// La carica finisce contro un ostacolo: botta, polvere, stelle; resta stordito.
void rush_crash(void) {
  Boss& b = g.boss;
  const Vec3 d = dir_yaw(b.act_yaw);
  const Vec3 hit = b.pos + d * (boss_r() + 0.25f);
  smash(hit + d * 0.3f);
  burst(hit + v3(0, 1.2f, 0), 12, PC_STONE, 4.0f);
  burst(hit + v3(0, 2.2f, 0), 6, PC_GOLD, 3.5f);
  for (int k = 0; k < 6; ++k) puff(hit + v3(0, 0.5f, 0), PK_PUFF, PC_SAND, rng_vec(2.5f, 0.6f, 1.0f, 2.5f), 1.4f, 30);
  add_shake(0.45f);
  snd_at(SND_BOSS_IMPACT, hit);
  add_float_text(v3(b.pos.x, 5.6f, b.pos.z), 0xFFE14D, N_("STORDITO!"));
  b.sq_v -= 3.0f;
  plat_log("boss: carica contro un ostacolo, stordito (fotogramma %u, vita %d)", g.tick, (int)b.hp);
  act_begin(BA_STUN);
}

void lane_marker_off(void) {
  for (int i = 0; i < MAX_MARKERS; ++i)
    if (g.mk[i].alive && g.mk[i].kind == MK_LANE && g.mk[i].life > g.mk[i].t + 4) g.mk[i].life = g.mk[i].t + 4;
}

void rush_update(void) {
  Boss& b = g.boss;
  const Vec3 d = dir_yaw(b.act_yaw);
  if (b.act_t < b.wind) {                        // si abbassa dietro lo scudo e punta la corsia, raspando
    face_toward(b.act_yaw, 9.0f);
    if (b.act_t % 8 == 4) puff(b.pos + d * 0.5f + v3(0, 0.1f, 0), PK_PUFF, PC_SAND, v3(-d.x * 1.5f, 0.5f, -d.z * 1.5f), 0.8f, 18);
    return;
  }
  if (b.run < b.lane) {
    if (b.act_t == b.wind) b.face = b.act_yaw;
    const float step = fminf(RUSH_SPEED * DT, b.lane - b.run);
    float nx = b.pos.x + d.x * step, nz = b.pos.z + d.z * step;
    level_push_out(&g.L, &nx, &nz, 0.0f, boss_r(), PL_STEP, BODY_WALK | BODY_PIT);
    level_body_guard(&g.L, b.pos.x, b.pos.z, &nx, &nz, 0.0f, boss_r(), PL_STEP, BODY_WALK | BODY_PIT);
    const float moved = sqrtf((nx - b.pos.x) * (nx - b.pos.x) + (nz - b.pos.z) * (nz - b.pos.z));
    b.pos.x = nx;
    b.pos.z = nz;
    b.run += step;
    b.phase = d_wrap_pi(b.phase + moved * 1.7f);
    b.amp = 1.0f;
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& p = g.pl[j];
      if (!in_play(j) || ((b.hitmask >> j) & 1u) || p.pos.y > 2.0f) continue;
      if (dist2d(p.pos, b.pos) > boss_r() + PL_R + 0.1f) continue;
      b.hitmask |= (uint8_t)(1u << j);
      // Spinto forte di lato, fuori dalla corsia, dalla parte in cui stava (e un po' in avanti).
      const float side = (p.pos.x - b.pos.x) * d.z - (p.pos.z - b.pos.z) * d.x;
      const Vec3 n = side >= 0.0f ? v3(d.z, 0.0f, -d.x) : v3(-d.z, 0.0f, d.x);
      hurt_player(j, 25.0f * diff_dmg(), p.pos - n - d * 0.3f, 16.0f);
      add_shake(0.15f);
    }
    if ((b.act_t & 3) == 0) puff(b.pos - d * 0.9f + v3(0, 0.15f, 0), PK_PUFF, PC_SAND, v3(-d.x * 2.0f, 0.8f, -d.z * 2.0f), 1.1f, 20);
    if (b.act_t % 9 == 0) snd_at(SND_BOSS_STEP, b.pos);
    const bool blocked = moved < step * 0.5f;     // qualcosa che la corsia non aveva (non dovrebbe succedere)
    if (b.run >= b.lane - 1e-4f || blocked) {
      b.run = b.lane;
      b.end_t = b.act_t;
      lane_marker_off();
      if (b.crash || blocked) rush_crash();
      else puff(b.pos + d * 1.0f + v3(0, 0.1f, 0), PK_PUFF, PC_SAND, v3(d.x * 2.0f, 0.6f, d.z * 2.0f), 1.5f, 26);
    }
    return;
  }
  if (b.act_t >= b.end_t + RUSH_REST) {          // si e' fermato senza sbattere: giavellotti subito
    b.queued = BA_JAVELIN;
    act_end();
  }
}

void summon(void) {
  const ZoneDef& z = g.L.zones[g.zone];
  if (z.nspawn <= 0) return;
  int room = MONSTER_CAP - count_monsters();
  const int np = count_in_play() > 0 ? count_in_play() : 1;
  const int scarabs = 2 + np / 2, scorpions = 1 + (g.boss.stage >= 2 ? 1 : 0) + (np >= 3 ? 1 : 0);
  int n = 0;
  for (int k = 0; k < scarabs && room > 0; ++k, --room) n += monster_spawn(MO_SCARABEO, true, 0) ? 1 : 0;
  for (int k = 0; k < scorpions && room > 0; ++k, --room) n += monster_spawn(MO_SCORPIONE, true, 0) ? 1 : 0;
  plat_log("il gladiatore chiama rinforzi: %d mostri (fotogramma %u)", n, g.tick);
}

void act_begin(int act) {
  Boss& b = g.boss;
  b.act = act;
  b.act_t = 0;
  b.hitmask = 0;
  b.vx = b.vz = 0.0f;                    // per prepararsi si ferma
  b.idle_t = 0;
  b.side_t = 0;
  b.check = b.pos;
  b.check_t = 0;
  if (act != BA_SUMMON && act != BA_STUN) {
    b.last[1] = b.last[0];
    b.last[0] = act;
    g.boss_seq++;
  }
  const Vec3 tp = has_target() ? g.pl[b.target].pos : b.pos + dir_yaw(b.face);
  b.act_yaw = yaw_of(tp.x - b.pos.x, tp.z - b.pos.z);
  const float d = dist2d(tp, b.pos);
  switch (act) {
    case BA_SWEEP: {
      b.wind = wind_of(SWEEP_WIND);
      Marker* m = marker_new(MK_SWEEP, v3(b.pos.x, 0.0f, b.pos.z), BOSS_SWEEP_R, b.wind + 10);
      if (m) {
        m->yaw = b.act_yaw;
        m->fire_at = b.wind;
      }
      snd_at(SND_BOSS_SWEEP_WARN, b.pos);
      break;
    }
    case BA_RUSH: {
      b.wind = wind_of(RUSH_WIND);
      bool wall;
      const float free = lane_free(b.pos, b.act_yaw, &wall);
      const float want = clampf(d + RUSH_OVER, RUSH_MIN + 2.0f, RUSH_LEN);
      b.crash = wall && free < want + RUSH_WALL;
      b.lane = b.crash ? free : fminf(want, free);
      b.run = 0.0f;
      b.end_t = 0;
      Marker* m = marker_new(MK_LANE, v3(b.pos.x, 0.0f, b.pos.z), BOSS_LANE_HW, b.wind + (int)(b.lane / RUSH_SPEED * 60.0f) + 30);
      if (m) {
        m->yaw = b.act_yaw;
        m->len = b.lane + boss_r();
        m->fire_at = b.wind;
        m->crash = b.crash;
      }
      snd_at(SND_BOSS_RUSH, b.pos);
      break;
    }
    case BA_JAVELIN: b.wind = wind_of(THROW_WIND); break;
    case BA_SLAM: b.wind = wind_of(SLAM_WIND); break;
    case BA_STUN: b.wind = BOSS_STUN_FRAMES; break;
    default: b.wind = SUMMON_AT; break;
  }
}

void act_update(void) {
  Boss& b = g.boss;
  b.act_t++;
  switch (b.act) {
    case BA_SWEEP:
      if (b.act_t < b.wind) face_toward(b.act_yaw, 9.0f);
      if (b.act_t == b.wind) sweep_strike();
      if (b.act_t >= b.wind + SWEEP_HIT + SWEEP_REST) act_end();
      break;
    case BA_RUSH:
      rush_update();
      break;
    case BA_JAVELIN:
      if (b.act_t < b.wind && has_target()) {
        const Vec3 tp = g.pl[b.target].pos;
        face_toward(yaw_of(tp.x - b.pos.x, tp.z - b.pos.z), 5.0f);
      }
      if (b.act_t < b.wind) b.sway += 0.06f * d_sin((float)b.act_t * 0.9f);
      if (b.act_t == b.wind) launch_javelins();
      if (b.act_t >= b.wind + THROW_REST) act_end();
      break;
    case BA_SLAM:
      if (b.act_t < b.wind) face_toward(b.act_yaw, 9.0f);
      if (b.act_t == 1) start_slam();
      if (b.act_t == b.wind) {
        b.face = b.act_yaw;
        add_shake(0.12f);
      }
      if (b.act_t >= b.wind + SLAM_REST) act_end();
      break;
    case BA_STUN:
      b.sway = 0.1f * d_sin((float)b.act_t * 0.21f);
      if (b.act_t >= b.wind) act_end();
      break;
    default:                                    // rinforzi
      if (b.act_t == 1) b.sq_v += 2.0f;
      if (b.act_t == b.wind) summon();
      if (b.act_t >= SUMMON_FRAMES) act_end();
      break;
  }
}

// Attacco adatto alla distanza d dal bersaglio in tp (BA_NONE: per ora nessuno, si cammina).
int choose_attack(float d, Vec3 tp) {
  Boss& b = g.boss;
  const float yaw = yaw_of(tp.x - b.pos.x, tp.z - b.pos.z);
  int a = BA_NONE, alt = BA_NONE;
  if (d <= SWEEP_REACH) {
    if (fabsf(d_wrap_pi(yaw - b.face)) < 2.0f) {     // non proprio alle spalle: prima si gira
      a = BA_SWEEP;
      alt = BA_SLAM;
    }
  } else if (d < RUSH_MIN) {
    a = (b.alt++ & 1) == 0 ? BA_SLAM : BA_JAVELIN;   // troppo vicino per caricare, troppo lontano per la spada
    alt = a == BA_SLAM ? BA_JAVELIN : BA_SLAM;
  } else {
    bool wall;
    const bool lane = d <= RUSH_MAX && lane_free(b.pos, yaw, &wall) >= d - (boss_r() + PL_R + 0.3f);
    const bool turn = (b.alt & 1) == 0;              // a turno: la carica e l'altro attacco della distanza
    const int other = d <= SLAM_MAX ? BA_SLAM : BA_JAVELIN;
    b.alt++;
    a = lane && turn ? BA_RUSH : other;
    alt = a == BA_RUSH ? other : (lane ? BA_RUSH : (other == BA_SLAM ? BA_JAVELIN : BA_NONE));
  }
  if (a != BA_NONE && a == b.last[0] && a == b.last[1]) a = alt;
  return a;
}

// Un passo della camminata verso (dvx, dvz): gli ostacoli lo spingono fuori e non entra negli eroi
// (li schiaccerebbe contro i muri): si ferma davanti. A ogni piede a terra un tonfo e un po' di sabbia.
void boss_move(float dvx, float dvz, float accel) {
  Boss& b = g.boss;
  b.vx += (dvx - b.vx) * accel;
  b.vz += (dvz - b.vz) * accel;
  const float ox = b.pos.x, oz = b.pos.z;
  float nx = ox + b.vx * DT, nz = oz + b.vz * DT;
  level_push_out(&g.L, &nx, &nz, 0.0f, boss_r(), PL_STEP, BODY_WALK | BODY_PIT);
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    if (!in_play(j)) continue;
    const Player& p = g.pl[j];
    const float dx = nx - p.pos.x, dz = nz - p.pos.z, dd = sqrtf(dx * dx + dz * dz), min = boss_r() + PL_R;
    if (dd >= min || dd < 1e-4f) continue;
    nx = p.pos.x + dx / dd * min;
    nz = p.pos.z + dz / dd * min;
  }
  level_push_out(&g.L, &nx, &nz, 0.0f, boss_r(), PL_STEP, BODY_WALK | BODY_PIT);
  level_body_guard(&g.L, ox, oz, &nx, &nz, 0.0f, boss_r(), PL_STEP, BODY_WALK | BODY_PIT);
  const float moved = sqrtf((nx - ox) * (nx - ox) + (nz - oz) * (nz - oz));
  b.pos.x = nx;
  b.pos.z = nz;
  const float s0 = d_sin(b.phase);
  b.phase = d_wrap_pi(b.phase + moved * 2.0f);
  const float speed = moved / DT;
  b.amp += ((speed > 0.3f ? clampf(speed / SPEED[0], 0.0f, 1.0f) : 0.0f) - b.amp) * 0.15f;
  const float s1 = d_sin(b.phase);
  if ((s0 < 0.0f) != (s1 < 0.0f) && b.amp > 0.4f) {
    const float side = s1 < 0.0f ? 1.0f : -1.0f;       // il piede che va giu'
    const Vec3 foot = b.pos + v3(d_cos(b.face) * BOSS_HIP_X * side, 0.1f, -d_sin(b.face) * BOSS_HIP_X * side);
    puff(foot, PK_PUFF, PC_SAND, v3(0, 0.4f, 0), 0.8f, 16);
    snd_at(SND_BOSS_STEP, b.pos);
  }
}

// Tra un attacco e l'altro: cammina verso il bersaglio (dritto se si puo', altrimenti lungo il suo
// campo dei percorsi), si gira verso di lui e, finita la pausa, sceglie l'attacco.
void walk_update(void) {
  Boss& b = g.boss;
  const bool have = has_target();
  const Vec3 tp = have ? g.pl[b.target].pos : b.pos;
  const float d = have ? dist2d(tp, b.pos) : 0.0f;
  const float spd = boss_speed();
  float mx = 0.0f, mz = 0.0f;
  if (have && d > STOP_D) {
    if (--b.repath <= 0 || (!b.direct && dist2d(b.pos, b.way) < 0.4f)) {
      b.repath = 8;
      const float k = (d - STOP_D) / d;
      b.direct = line_ok(b.pos, v3(b.pos.x + (tp.x - b.pos.x) * k, 0.0f, b.pos.z + (tp.z - b.pos.z) * k));
      if (!b.direct) {
        if (!g_bfield_ok || dist2d(tp, g_bgoal) > 1.0f || ++g_bfield_t >= 4) {
          g_bfield_t = 0;
          bfield_build(tp);
        }
        const int w = bfield_way(b.pos, &b.way);
        if (w == WAY_NONE) b.direct = level_zone_at(&g.L, tp.x, tp.z) == g.zone;   // fuori dalla sua zona non lo segue
        if (w == WAY_THERE || (w == WAY_NONE && !b.direct)) b.way = b.pos;   // piu' vicino di cosi' non arriva: resta li'
      }
    }
    const float ex = b.direct ? tp.x - b.pos.x : b.way.x - b.pos.x, ez = b.direct ? tp.z - b.pos.z : b.way.z - b.pos.z;
    const float el = sqrtf(ex * ex + ez * ez);
    if (el > 0.05f) {
      mx = ex / el;
      mz = ez / el;
    }
    if (b.side_t > 0) {                           // passo di lato per uscire da dove si era bloccato
      b.side_t--;
      const float sx = -mz * b.side + mx * 0.25f, sz = mx * b.side + mz * 0.25f, sl = sqrtf(sx * sx + sz * sz);
      if (b.stuck_n >= 3) {                       // bloccato piu' volte di fila: indietro
        mx = -mx;
        mz = -mz;
      } else if (sl > 1e-3f) {
        mx = sx / sl;
        mz = sz / sl;
      }
    }
  }
  boss_move(mx * spd, mz * spd, 0.1f);
  // Dove guarda: il bersaglio da vicino (o se e' fermo), altrimenti dove va.
  const float turn = 2.6f + 0.3f * (float)stage_of();
  if (have && (d < 10.0f || b.amp < 0.3f)) face_toward(yaw_of(tp.x - b.pos.x, tp.z - b.pos.z), turn);
  else if (b.vx * b.vx + b.vz * b.vz > 0.25f) face_toward(yaw_of(b.vx, b.vz), turn);
  // Bloccato? Fermo da mezzo secondo mentre vuole camminare, senza un eroe davanti che lo ferma.
  if (++b.check_t >= 30) {
    bool hero = false;
    for (int j = 0; j < MAX_PLAYERS; ++j) hero |= in_play(j) && dist2d(g.pl[j].pos, b.pos) < boss_r() + PL_R + 0.5f;
    const bool wants = mx != 0.0f || mz != 0.0f;
    if (wants && !hero && b.side_t == 0 && dist2d(b.pos, b.check) < spd * 0.15f) {
      const float lx = -mz, lz = mx;
      const bool l_ok = fits(b.pos.x + lx * 1.0f, b.pos.z + lz * 1.0f, boss_r());
      const bool r_ok = fits(b.pos.x - lx * 1.0f, b.pos.z - lz * 1.0f, boss_r());
      b.side = l_ok && !r_ok ? 1.0f : (r_ok && !l_ok ? -1.0f : (b.side > 0.0f ? -1.0f : 1.0f));
      b.side_t = 45;
      b.stuck_n++;
      b.repath = 0;
      g_bfield_ok = false;
      if (b.stuck_n == 3) plat_log("boss: bloccato piu' volte a %.1f %.1f, torna indietro", (double)b.pos.x, (double)b.pos.z);
    } else if (dist2d(b.pos, b.check) > spd * 0.3f) {
      b.stuck_n = 0;
    }
    b.check = b.pos;
    b.check_t = 0;
  }
  if (b.cd > 0) {
    b.cd--;
    return;
  }
  if (!have) return;
  const int a = choose_attack(d, tp);
  if (a != BA_NONE) act_begin(a);
  else if (++b.idle_t > 100) act_begin(BA_JAVELIN);   // niente di adatto da un po': giavellotti
}

}  // namespace

float boss_r(void) { return 1.25f; }
float boss_speed(void) { return SPEED[stage_of()] * diff_speed(); }
Vec3 boss_javelin_start(const Boss& b) { return b.pos + dir_yaw(b.face) * 0.4f + v3(0, 4.0f, 0); }

bool boss_force_act(int act) {
  Boss& b = g.boss;
  if (!b.alive || act < 0 || act >= BA_COUNT) return false;
  if (!has_target()) update_target();
  b.roar = 0;
  b.queued = BA_NONE;
  act_begin(act);
  return true;
}

void boss_start(void) {
  Boss& b = g.boss;
  memset(&b, 0, sizeof(b));
  g_bfield_ok = false;
  g_bfree_t = g_bfield_t = 0;
  if (g.L.boss == BOSS_NONE || g.zone != g.L.nzones - 1) return;
  bfree_build();
  const int np = count_active() > 0 ? count_active() : 1;
  b.alive = true;
  b.type = g.L.boss;
  b.pos = spawn_spot();
  b.hp_max = b.hp = BOSS_HP * (0.5f + 0.5f * (float)np) * diff_hp();
  b.cd = 120;
  b.act = b.queued = BA_NONE;
  b.last[0] = b.last[1] = BA_NONE;
  b.target = -1;
  b.check = b.pos;
  // Guarda verso le partenze dei giocatori.
  Vec3 c = v3(0, 0, 0);
  for (int i = 0; i < MAX_PLAYERS; ++i) c = c + g.L.zones[g.zone].start[i] * 0.25f;
  b.face = yaw_of(c.x - b.pos.x, c.z - b.pos.z);
  g.boss_seq = 0;
  plat_log("boss: gladiatore gigante con %d di vita", (int)b.hp_max);
}

void boss_update(void) {
  Boss& b = g.boss;
  // Crepe gia' segnate: le pietre spuntano al loro momento anche se intanto lui fa altro. Chi sta
  // tra due segni (o dove le tre crepe dell'ultima fase partono insieme) le prende una volta sola.
  for (int i = 0; i < MAX_MARKERS; ++i) {
    Marker& m = g.mk[i];
    if (!m.alive || m.kind != MK_ROOT || m.done || m.t < m.fire_at) continue;
    m.done = true;
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& p = g.pl[j];
      if (!p.active || !p.alive || p.pos.y > 1.0f) continue;
      if (dist2d(p.pos, m.pos) > m.r + PL_R * 0.5f) continue;
      if (b.crack_t[j] != 0u && g.tick - b.crack_t[j] < 40u) continue;
      b.crack_t[j] = g.tick;
      hurt_player(j, 22.0f * diff_dmg(), m.pos, 4.0f);
      g.pl[j].kx *= 0.5f;
    }
    burst(m.pos + v3(0, 0.3f, 0), 4, PC_STONE, 2.5f);
    add_shake(0.03f);
  }
  if (!b.alive) {
    if (b.dead_t > 0 && b.dead_t < 1000) b.dead_t++;     // disegno: la caduta dopo la sconfitta
    return;
  }
  if (b.hurt > 0) b.hurt--;
  if (b.shown > 0) b.shown--;
  b.sq_v += (-200.0f * b.sq - 10.0f * b.sq_v) * DT;
  b.sq += b.sq_v * DT;
  b.sq = clampf(b.sq, -0.25f, 0.25f);
  b.sway = 0.04f * d_sin((float)g.tick * 0.035f);
  update_target();
  if (++g_bfree_t >= 30) {                 // casse rotte: si passa di piu'
    g_bfree_t = 0;
    if (crates_whole() != g_bcrates) {
      bfree_build();
      g_bfield_ok = false;
    }
  }

  if (b.hp <= 0.0f) {
    b.alive = false;
    b.hp = 0.0f;
    b.dead_t = 1;
    b.act = BA_NONE;
    for (int k = 0; k < MAX_MONSTERS; ++k)
      if (g.mo[k].alive) {                 // i rinforzi scappano con lui
        burst(monster_center(g.mo[k]), 8, PC_WHITE, 3.0f);
        g.mo[k].alive = false;
      }
    // Spariscono anche i suoi attacchi ancora in arrivo: giavellotti in volo, segni a terra.
    for (int i = 0; i < MAX_SHOTS; ++i)
      if (g.sh[i].alive && g.sh[i].owner == -2) {
        g.sh[i].alive = false;
        burst(g.sh[i].pos, 3, PC_BRONZE, 1.5f);
      }
    for (int i = 0; i < MAX_MARKERS; ++i) {
      Marker& m = g.mk[i];
      if (m.alive && (m.kind == MK_LOB || m.kind == MK_SWEEP || m.kind == MK_LANE || (m.kind == MK_ROOT && !m.done))) m.alive = false;
    }
    snd_at(SND_BOSS_ROAR, b.pos);
    for (int k = 0; k < 40; ++k)
    {
      const Vec3 at = b.pos + rng_vec(1.6f, 3.0f, 2.5f, 1.6f);
      const Vec3 vel = rng_vec(5.0f, 1.0f, 3.0f, 5.0f);
      puff(at, PK_PUFF, (k & 1) ? PC_SAND : PC_STONE, vel, 1.6f, 70 + (int)(rng_next() % 40u));   // polvere
    }
    burst(b.pos + v3(0, 1.5f, 0), 30, PC_BRONZE, 6.0f);      // pezzi dell'armatura
    add_shake(0.6f);
    show_msg(tr("GLADIATORE SCONFITTO!"), tr("il Colosseo è libero"), 0x7CF29A, 2, 180);
    plat_log("boss sconfitto: gladiatore (fotogramma %u)", g.tick);
    return;
  }
  // Fase: a 2/3 e a 1/3 della vita, appena finito l'attacco in corso, ruggisce e chiama i rinforzi.
  const int stage = b.hp < b.hp_max / 3.0f ? 2 : (b.hp < b.hp_max * 2.0f / 3.0f ? 1 : 0);
  if (stage > b.stage && b.act == BA_NONE && b.roar == 0) {
    b.stage = stage;
    b.roar = ROAR_FRAMES;
    b.vx = b.vz = 0.0f;
    add_shake(0.35f);
    for (int k = 0; k < 24; ++k)
    {
      const Vec3 at = b.pos + rng_vec(1.8f, 3.5f, 2.0f, 1.8f);
      puff(at, PK_LEAF, (k % 3) ? PC_RED : PC_GOLD, rng_vec(3.0f, 0.5f, 0.0f, 3.0f), 1.0f, 80);   // piume della cresta
    }
    show_msg(stage == 1 ? tr("SI ARRABBIA!") : tr("È FURIOSO!"), tr("il gladiatore va più veloce"), 0xFF8A5C, 2, 120);
    snd_at(SND_BOSS_ROAR, b.pos);
    plat_log("boss: fase %d (fotogramma %u)", stage + 1, g.tick);
  }
  if (b.roar > 0) {
    b.roar--;
    b.sway = 0.08f * d_sin((float)b.roar * 0.8f);
    if (b.roar == 0) {
      if (count_monsters() < MONSTER_CAP) act_begin(BA_SUMMON);
      else b.cd = 30;
    }
    return;
  }
  if (b.act != BA_NONE) act_update();
  else walk_update();
}
