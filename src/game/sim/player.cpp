// Heroes: movement, jump, trampolines, aiming with the right stick, normal shot (R1),
// special (L1), bombs (L2), damage, falls, lives and respawns, joining and leaving the game.
// Astro, Mochi, Marzio and Zolfo (since 0.5) have the weapons and the values of Paglia, Spina,
// Fiocco and Brace.
#include "game/sim/game_int.h"
#include "platform/plat.h"
#include <stdio.h>
#include <string.h>

const CharDef CHARS[NCHARS] = {
  // name, who, role, shot, special, color, health, speed,
  // bar, cost, fire rate, wait, reload, range, aim, width, special cost, special aim, range, width
  { N_("ASTRO"), N_("astronauta della Terra"), N_("equilibrato"), N_("fucilone: raffica di 3 colpi a ventaglio"), N_("3 razzi a ricerca"), 0xE9EEF5,
    100.0f, 5.0f, 5.0f, 1.0f, 22, 30, 100, 8.0f, AIM_FAN, 0.44f, 160.0f, AIM_LINE, 10.0f, 0.6f },
  { N_("MOCHI"), N_("coniglio della Luna"), N_("resistente"), N_("ventaglio di 5 schegge di luna"), N_("salto lunare con le schegge"), 0xD9D6F0,
    140.0f, 4.7f, 4.0f, 1.0f, 28, 30, 88, 5.2f, AIM_FAN, 0.70f, 150.0f, AIM_LOB, 6.5f, 2.3f },
  { N_("MARZIO"), N_("marziano del polo"), N_("controllo"), N_("3 palle di ghiaccio secco che rallentano"), N_("tempesta di sabbia gelata"), 0xE8673C,
    95.0f, 5.0f, 8.0f, 1.0f, 13, 30, 96, 8.5f, AIM_FAN, 0.35f, 140.0f, AIM_AREA, 4.0f, 0.0f },
  { N_("ZOLFO"), N_("fiamma di Venere"), N_("lanciafiamme"), N_("fiamma che attraversa i mostri"), N_("eruzione: 5 bolle di lava"), 0xFFC93C,
    120.0f, 4.8f, 100.0f, 4.2f, 3, 25, 78, 4.2f, AIM_FAN, 0.32f, 180.0f, AIM_LOB, 8.0f, 1.6f },
};

// Name, name in the pause menu, threshold, minimum filter cutoff (Hz), beta (Hz per rad/s).
const AimTune AIM_TUNE[NAIMLEVELS] = {
  { "extra morbida", N_("EXTRA MORBIDA"), 0.60f, 0.5f, 0.12f },
  { "morbida", N_("MORBIDA"), 0.55f, 0.8f, 0.20f },
  { "media", N_("MEDIA"), 0.50f, 1.5f, 0.30f },
  { "rapida", N_("RAPIDA"), 0.45f, 3.0f, 0.50f },
};

namespace {

const int LEAP_FRAMES = 33;
const float TRAMP_VY = 10.0f, TRAMP_SPEED = 5.6f;

Vec3 muzzle(const Player& p, float yaw) { return p.pos + dir_yaw(yaw) * 0.42f + v3(0, 0.85f, 0); }

void reset_body(Player& p, Vec3 pos) {
  p.pos = pos;
  p.vx = p.vz = p.vy = p.kx = p.kz = 0.0f;
  p.grounded = true;
  p.alive = true;
  p.ammo = CHARS[p.ch].ammo_max;
  p.locked = false;
  p.fire_cd = p.reload_wait = 0;
  p.burst_left = 0;
  p.aim_hold = 0;
  p.aiming = false;
  p.aim_m = 0.0f;
  p.aim_on_n = p.aim_nh = 0;
  p.respawn = 0;
  p.hurt = p.recoil = p.down = p.reveal = p.land = 0;
  p.leap_t = p.leap_n = 0;
  p.blizzard_t = p.flame_t = p.tramp_t = 0;
  p.amp = 0.0f;
  p.sq = p.sq_v = 0.0f;
}

// Does the player's circle fit at (x, z) without touching obstacles, in the right zone (and not
// over the cenote)?
bool spot_ok(Game& g, Vec3 c, int zone, float feet) {
  int cx, cz;
  if (!level_cell(&g.L, c.x, c.z, &cx, &cz)) return false;
  if (zone >= 0 && g.L.zone[cz][cx] != zone) return false;
  if (g.L.cell[cz][cx] == C_WATER || g.L.cell[cz][cx] == C_PIT) return false;
  const float top = level_stand_top(&g.L, cx, cz);
  if (top > feet + PL_STEP + 0.01f) return false;
  if (!level_body_ok(&g.L, c.x, c.z, top, PL_R, PL_STEP, BODY_WALK)) return false;
  if (g.boss.alive && dist2d(c, g.boss.pos) < boss_r() + PL_R + 0.2f) return false;
  return true;
}

// A free spot near ref: for a player coming back into play (never on the cenote nor next to it).
Vec3 free_spot_near(Game& g, Vec3 ref) {
  static const float OFF[8][2] = { { 1.1f, 0 }, { -1.1f, 0 }, { 0, 1.1f }, { 0, -1.1f },
                                   { 0.85f, 0.85f }, { -0.85f, 0.85f }, { 0.85f, -0.85f }, { -0.85f, -0.85f } };
  const int zone = level_zone_at(&g.L, ref.x, ref.z);
  for (int k = 0; k < 8; ++k) {
    const Vec3 c = v3(ref.x + OFF[k][0], 0.0f, ref.z + OFF[k][1]);
    if (!spot_ok(g, c, zone, 0.0f) || level_pit_near(&g.L, c.x, c.z, PL_R + 0.6f)) continue;
    if (level_ground(&g.L, c.x, c.z, 0.0f, PL_STEP) != 0.0f) continue;
    if (!walk_clear(g, v3(ref.x, 0, ref.z), c, PL_R, false)) continue;
    return c;
  }
  return v3(ref.x, 0.0f, ref.z);
}

// Puts the player next to the nearest standing teammate; false if there is nobody.
bool place_near_group(Game& g, int i) {
  Player& p = g.pl[i];
  int best = -1;
  float bd = 1e30f;
  // During the fight you come back only next to someone who is in the zone (never past a gate).
  const bool in_zone = g.phase == PH_FIGHT;
  Vec3 pos = p.pos;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (j == i || !q.active || !q.alive || q.leap_n > 0) continue;
    if (in_zone && level_zone_at(&g.L, q.pos.x, q.pos.z) != g.zone) continue;
    const float d = dist2d(q.pos, p.pos);
    if (d >= bd) continue;
    const Vec3 c = free_spot_near(g, q.pos);
    if (level_pit_near(&g.L, c.x, c.z, PL_R + 0.6f)) continue;   // teammate at the cenote's edge: another one, or retry
    bd = d;
    best = j;
    pos = c;
  }
  if (best < 0) return false;
  const Player& ref = g.pl[best];
  pos.y = level_ground(&g.L, pos.x, pos.z, ref.pos.y, PL_STEP);
  reset_body(p, pos);
  p.yaw = ref.yaw;
  burst(g, v3(pos.x, pos.y + 0.6f, pos.z), 12, i, 3.0f);
  return true;
}

void respawn_player(Game& g, int i) {
  Player& p = g.pl[i];
  if (!place_near_group(g, i)) {
    p.respawn = 1;     // no free teammate right now (in the air, out of the zone): try again
    return;
  }
  p.hp = p.hp_max * RESPAWN_HP;
  p.invuln = INVULN_FRAMES;
  ev_sound(g, SND_REVIVE, p.pos);
  plat_log("giocatore %d di nuovo in gioco", i + 1);
}

// Nobody moves away from the group further than the camera can frame.
void span_clamp(Game& g, int i, float* nx, float* nz) {
  if (g.net) return;      // in a LAN game (0.5.4) each has their own camera: no limit (the gates keep all in the zone)
  float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
  int n = 0;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (j == i || !q.active || !q.alive) continue;
    mnx = fminf(mnx, q.pos.x); mxx = fmaxf(mxx, q.pos.x);
    mnz = fminf(mnz, q.pos.z); mxz = fmaxf(mxz, q.pos.z);
    n++;
  }
  if (!n) return;
  const Player& p = g.pl[i];
  const float lox = mxx - SPAN_X, hix = mnx + SPAN_X, loz = mxz - SPAN_Z, hiz = mnz + SPAN_Z;
  if (*nx > hix) *nx = p.pos.x > hix ? fminf(*nx, p.pos.x) : hix;
  if (*nx < lox) *nx = p.pos.x < lox ? fmaxf(*nx, p.pos.x) : lox;
  if (*nz > hiz) *nz = p.pos.z > hiz ? fminf(*nz, p.pos.z) : hiz;
  if (*nz < loz) *nz = p.pos.z < loz ? fmaxf(*nz, p.pos.z) : loz;
}

void boss_push(Game& g, float* x, float* z, float r) {
  if (!g.boss.alive) return;
  const float dx = *x - g.boss.pos.x, dz = *z - g.boss.pos.z, d = sqrtf(dx * dx + dz * dz), min = boss_r() + r;
  if (d >= min) return;
  if (d < 1e-4f) { *z += min; return; }
  *x = g.boss.pos.x + dx / d * min;
  *z = g.boss.pos.z + dz / d * min;
}

Shot* fire_shot(Game& g, int i, int kind, float yaw, float speed, float range, float dmg, float r, bool special) {
  Player& p = g.pl[i];
  const Vec3 m = muzzle(p, yaw);
  if (level_blocked(&g.L, p.pos + v3(0, 0.85f, 0), m)) {     // barrel inside an obstacle: shot fizzles out
    burst(g, m, 2, PC_WHITE, 1.2f);
    return 0;
  }
  Shot* s = shot_new(g, kind, i, m, dir_yaw(yaw) * speed);
  if (!s) return 0;
  s->left = range;
  s->dmg = dmg;
  s->r = r;
  s->special = special;
  if (!special) card_shot_mods(g, i, s);          // cards: damage, PERFORANTE, RIMBALZO, combos
  return s;
}

void attack_normal(Game& g, int i, float yaw) {
  Player& p = g.pl[i];
  const CharDef& cd = CHARS[p.ch];
  const float range = pl_range(p);
  p.ammo -= cd.ammo_cost;
  if (p.ammo < cd.ammo_cost) p.locked = true;    // empty bar: wait until it is full again
  p.fire_cd = cd.fire_frames;
  p.reload_wait = pl_reload_wait(p);
  p.yaw = yaw;
  p.aim_hold = 18;
  p.recoil = 6;
  p.reveal = 60;
  switch (p.ch) {
    case CH_ASTRO:
      p.burst_left = 3;
      p.burst_cd = 0;
      p.burst_yaw = yaw;
      p.sq_v -= 0.6f;
      break;
    case CH_MOCHI:
      for (int k = 0; k < 5; ++k) fire_shot(g, i, SK_SHARD, yaw + cd.aim_w * ((float)k / 4.0f - 0.5f), 15.0f, range, 9.0f, 0.12f, false);
      p.sq_v -= 1.2f;
      ev_sound(g, SND_SHARD, p.pos);
      break;
    case CH_MARZIO:
      // Three snowballs together, spread over the fan: left, center, right.
      for (int k = 1; k >= -1; --k) fire_shot(g, i, SK_FROST, yaw + cd.aim_w * 0.5f * (float)k, 14.0f, range, 6.0f, 0.16f, false);
      p.sq_v -= 0.8f;
      ev_sound(g, SND_FROST, p.pos);
      break;
    case CH_ZOLFO: {
      Shot* s = fire_shot(g, i, SK_FLAME, yaw + rng_s(g) * 0.12f, 9.5f, range, 4.5f, 0.18f, false);
      (void)s;
      ev_sound(g, SND_FLAME, p.pos);
      p.flame_t = 8;
      p.recoil = 2;
      break;
    }
  }
}

// Mochi: where the special's leap lands (never inside an obstacle, in water or past a gate).
Vec3 leap_target(Game& g, const Player& p, float yaw) {
  const Vec3 d = dir_yaw(yaw);
  const float want = p.aiming ? fmaxf(1.5f, p.aim_k * CHARS[CH_MOCHI].sp_range) : 4.5f;
  const int zone = level_zone_at(&g.L, p.pos.x, p.pos.z) >= 0 ? level_zone_at(&g.L, p.pos.x, p.pos.z) : g.zone;
  for (float k = want; k > 0.0f; k -= 0.25f) {
    Vec3 c = p.pos + d * k;
    float nx = c.x, nz = c.z;
    span_clamp(g, (int)(&p - g.pl), &nx, &nz);
    if (nx != c.x || nz != c.z) continue;
    const float top = level_ground(&g.L, c.x, c.z, 9.0f, 0.0f);
    if (top > CRATE_H + 0.01f) continue;
    if (!spot_ok(g, c, zone, top)) continue;
    c.y = top;
    return c;
  }
  return p.pos;
}

void special(Game& g, int i, float yaw) {
  Player& p = g.pl[i];
  const CharDef& cd = CHARS[p.ch];
  p.charge = 0.0f;
  p.yaw = yaw;
  p.aim_hold = 24;
  p.reveal = 60;
  p.recoil = 8;
  p.sq_v += 2.0f;
  plat_log("giocatore %d: speciale di %s", i + 1, cd.name);
  switch (p.ch) {
    case CH_ASTRO:
      for (int k = -1; k <= 1; ++k) {
        const float a = yaw + 0.55f * (float)k;
        Shot* s = shot_new(g, SK_ROCKET, i, p.pos + v3(0, 1.1f, 0) + dir_yaw(a) * 0.3f, dir_yaw(a) * 6.5f);
        if (!s) break;
        s->left = 190.0f;
        s->dmg = 40.0f;
        s->r = 0.3f;
        s->special = true;
        s->yaw = a;
      }
      burst(g, p.pos + v3(0, 1.2f, 0), 10, PC_GREY, 3.0f);   // rocket smoke
      ev_sound(g, SND_ROCKET, p.pos);
      break;
    case CH_MOCHI:
      p.leap_a = p.pos;
      p.leap_b = leap_target(g, p, yaw);
      p.leap_t = 0;
      p.leap_n = LEAP_FRAMES;
      p.grounded = false;
      p.tramp_t = 0;
      for (int k = 0; k < 6; ++k) puff(g, p.pos + v3(0, 0.2f, 0), PK_PUFF, PC_EARTH, rng_vec(g, 2.0f, 0.8f, 0.0f, 2.0f), 1.0f, 20);
      ev_sound(g, SND_LEAP, p.pos);
      break;
    case CH_MARZIO:
      p.blizzard_t = 240;
      ev_sound(g, SND_BLIZZARD, p.pos);
      for (int k = 0; k < 16; ++k) {
        const float a = D_TWO_PI * (float)k / 16.0f;
        puff(g, p.pos + v3(0, 0.8f, 0), PK_SNOW, PC_SNOW, v3(d_cos(a) * 6.0f, 1.0f, d_sin(a) * 6.0f), 1.2f, 30);
      }
      break;
    case CH_ZOLFO: {
      const float dist = p.aiming ? fmaxf(2.0f, p.aim_k * cd.sp_range) : 6.0f;
      const Vec3 start = p.pos + v3(0, 1.1f, 0) + dir_yaw(yaw) * 0.3f;
      for (int k = -2; k <= 2; ++k) {
        const float a = yaw + 0.2f * (float)k, d = dist * (1.0f - 0.08f * fabsf((float)k));
        Vec3 dest = p.pos + dir_yaw(a) * d;
        dest.y = level_ground(&g.L, dest.x, dest.z, 9.0f, 0.0f);
        const float T = 0.62f + 0.05f * fabsf((float)k);
        const Vec3 vel = v3((dest.x - start.x) / T, (dest.y - start.y + 0.5f * GRAVITY * T * T) / T, (dest.z - start.z) / T);
        Shot* s = shot_new(g, SK_LAVA, i, start, vel);
        if (!s) break;
        s->dmg = 50.0f;
        s->r = 0.22f;
        s->special = true;
        s->dest = dest;
      }
      p.flame_t = 14;
      for (int k = 0; k < 8; ++k) puff(g, start, PK_FLAME, PC_FIRE, rng_vec(g, 2.0f, 3.0f, 0.0f, 2.0f), 1.0f, 16);
      ev_sound(g, SND_ERUPT, p.pos);
      ev_sound(g, SND_LAVA, p.pos);
      break;
    }
  }
  card_special_used(g, i);         // SPECIALE GENEROSO
}

// Mochi in the air: arc up to the chosen point, then the ground slam with the ring of shards.
void update_leap(Game& g, int i) {
  Player& p = g.pl[i];
  p.leap_t++;
  const float f = (float)p.leap_t / (float)p.leap_n;
  p.pos.x = lerpf(p.leap_a.x, p.leap_b.x, f);
  p.pos.z = lerpf(p.leap_a.z, p.leap_b.z, f);
  p.pos.y = lerpf(p.leap_a.y, p.leap_b.y, f) + 4.0f * 2.2f * f * (1.0f - f);
  if (p.leap_t < p.leap_n) return;
  p.pos = p.leap_b;
  p.leap_n = p.leap_t = 0;
  p.grounded = true;
  p.vy = 0.0f;
  p.land = 10;
  p.sq_v -= 6.0f;
  ev_sound(g, SND_SLAM, p.pos);
  explode(g, p.pos + v3(0, 0.2f, 0), 2.3f, 60.0f, i, 5.5f, false);
  for (int k = 0; k < 12; ++k) {
    const float a = D_TWO_PI * (float)k / 12.0f;
    Shot* s = shot_new(g, SK_SHARD, i, p.pos + v3(0, 0.6f, 0) + dir_yaw(a) * 0.3f, dir_yaw(a) * 14.0f);
    if (!s) break;
    s->left = 4.0f;
    s->dmg = 9.0f;
    s->r = 0.12f;
    s->special = true;
    s->fx = card_shot_fx(g, i, SK_SHARD);       // combo: these thorns also set on fire or slow down
  }
  for (int k = 0; k < 10; ++k) {
    const float a = D_TWO_PI * (float)k / 10.0f;
    puff(g, p.pos + v3(0, 0.2f, 0), PK_PUFF, PC_EARTH, v3(d_cos(a) * 4.0f, 0.6f, d_sin(a) * 4.0f), 1.2f, 22);
  }
  add_shake(g, 0.25f);
}

void update_blizzard(Game& g, int i) {
  Player& p = g.pl[i];
  p.blizzard_t--;
  const float R = CHARS[CH_MARZIO].sp_range;
  if (p.blizzard_t % 6 == 0) {
    for (int k = 0; k < MAX_MONSTERS; ++k) {
      Monster& m = g.mo[k];
      if (!m.alive || m.rise > 0 || dist2d(m.pos, p.pos) > R + monster_r(m)) continue;
      damage_monster(g, k, 1.5f, i, v3(0, 0, 0), 0.0f, false);
      if (m.alive) {
        m.slow_t = 20;
        m.slow_k = 0.3f;
        card_elem_hit(g, k, i, EL_SNOW);       // VAPORE on burning monsters
      }
    }
    if (g.boss.alive && dist2d(g.boss.pos, p.pos) < R + boss_r()) damage_boss(g, 1.5f, i, false);
  }
  for (int k = 0; k < 2; ++k) {
    const float a = rng_f(g) * D_TWO_PI, r = 1.0f + rng_f(g) * (R - 1.0f);
    const Vec3 tang = v3(-d_sin(a), 0.0f, d_cos(a)) * 4.0f;
    const float y = 0.3f + rng_f(g) * 1.4f;
    const float size = 0.6f + 0.5f * rng_f(g);
    puff(g, p.pos + v3(d_cos(a) * r, y, d_sin(a) * r), PK_SNOW, PC_SNOW, tang, size, 24);
  }
}

// Coefficient of a low-pass filter at cutoff Hz with steps of DT ("1 euro" filter).
float lowpass_a(float cutoff) {
  const float r = D_TWO_PI * cutoff * DT;
  return r / (r + 1.0f);
}

// Right stick: circular dead zone, filtered direction, aim that stays where it was when the
// stick is released (the spring return does not turn it) and that does not restart on a bounce.
void update_aim(Game& g, int i, const Pad& pad) {
  Player& p = g.pl[i];
  const AimTune& t = aim_tune(g, i);
  // Frames in which the stick was not read (pause, Mochi's leap, down, just joined): start over
  // from scratch, without comparing with the previous stick.
  if (p.aim_tick + 1u != g.ui_tick) {
    p.aiming = false;
    p.aim_m = 0.0f;
    p.aim_nh = p.aim_on_n = 0;
    p.aim_off_t = AIM_REARM;
  }
  p.aim_tick = g.ui_tick;
  const float m = fminf(1.0f, sqrtf(pad.rx * pad.rx + pad.ry * pad.ry));
  const bool falling = m < p.aim_m - AIM_DROP;
  p.aim_m = m;
  if (p.aim_off_t < 100000) p.aim_off_t++;
  if (m > t.on) {
    if (p.aim_on_n < 100000) p.aim_on_n++;
  } else {
    p.aim_on_n = 0;
  }
  if (p.aim_on_n < (p.aim_off_t < AIM_REARM ? 2 : 1)) {
    if (p.aiming) {
      // Stops aiming. If the tilt collapsed within a few frames, the stick was released: the aim
      // goes back to how it was in the frame where the fast drop began (walk back the history as
      // long as each earlier frame was clearly tilted more).
      int best = p.aim_nh > 0 ? 0 : -1;
      while (best >= 0 && best + 1 < p.aim_nh && p.aim_hm[best + 1] > p.aim_hm[best] + 0.05f) best++;
      if (best >= 0 && p.aim_hm[best] - m > AIM_RELEASE) p.aim_yaw = p.yaw = p.aim_hy[best];
      p.aiming = false;
      p.aim_off_t = 0;
      p.aim_nh = 0;
    }
    return;
  }
  const float raw = yaw_of(pad.rx, pad.ry / AIM_FORESHORTEN);
  if (!p.aiming) {
    // Starts aiming: right away where the stick points.
    p.aiming = true;
    p.aim_f = p.aim_yaw = raw;
    p.aim_dx = 0.0f;
    p.aim_k = aim_k_of(m, t.on);
  } else {
    const float d = d_wrap_pi(raw - p.aim_f);
    p.aim_dx += (d / DT - p.aim_dx) * lowpass_a(AIM_D_CUT);
    p.aim_f = d_wrap_pi(p.aim_f + d * lowpass_a(t.min_cut + t.beta * fabsf(p.aim_dx)));
    // While the tilt drops fast (maybe the stick was released) the aim used to turn and shoot
    // stays still; meanwhile the filter keeps following the stick.
    if (!falling) {
      p.aim_yaw = p.aim_f;
      p.aim_k += (aim_k_of(m, t.on) - p.aim_k) * 0.5f;
    }
  }
  for (int k = AIM_HIST - 1; k > 0; --k) {
    p.aim_hy[k] = p.aim_hy[k - 1];
    p.aim_hm[k] = p.aim_hm[k - 1];
  }
  p.aim_hy[0] = p.aim_yaw;
  p.aim_hm[0] = m;
  if (p.aim_nh < AIM_HIST) p.aim_nh++;
}

}  // namespace

void game_set_aim_level(Game& g, int player, int level) {
  if (player < 0 || player >= MAX_PLAYERS || level < 0 || level >= NAIMLEVELS) return;
  g.aim_level[player] = (uint8_t)level;
}

const char* game_aim_name(Game& g, int player) {
  if (player < 0 || player >= MAX_PLAYERS) return "?";
  return aim_tune(g, player).name;
}

Vec3 player_chest(const Player& p) { return v3(p.pos.x, p.pos.y + 0.85f, p.pos.z); }

bool player_visible_from(Game& g, int i, Vec3 from) {
  const Player& p = g.pl[i];
  return !p.hidden || dist2d(p.pos, from) < 3.0f;
}

void push_player(Game& g, int i, float vx, float vz, int frames) {
  (void)frames;
  Player& p = g.pl[i];
  p.kx += vx;
  p.kz += vz;
}

void heal_player(Game& g, int i, float amount) {
  Player& p = g.pl[i];
  if (p.hp >= p.hp_max) return;
  const float before = p.hp;
  p.hp = fminf(p.hp_max, p.hp + amount);
  add_float(g, v3(p.pos.x - 0.5f, p.pos.y + PL_H + 1.4f, p.pos.z), 0x5CF08A, 200 + i, (int)(p.hp - before + 0.5f), '+');
}

static void player_down(Game& g, int i, bool pit);

void hurt_player(Game& g, int i, float dmg, Vec3 from, float knock) {
  Player& p = g.pl[i];
  if (!p.active || !p.alive || p.invuln > 0 || p.leap_n > 0) return;
  if (p.pw_t[PW_INVINCIBLE] > 0) {     // INVINCIBILE (crate power-up): the hit bounces off, no damage nor knockback
    if (((g.tick + (unsigned)i) & 7u) == 0u) burst(g, player_chest(p), 5, PC_GOLD, 2.5f);   // sparks, not on every hit
    return;
  }
  if (card_shield_hit(g, i)) return;      // SCUDO: this hit is stopped completely
  p.hp -= dmg;
  p.hurt = 10;
  p.reveal = 60;
  p.sq_v -= 2.5f;
  Vec3 d = v3(p.pos.x - from.x, 0.0f, p.pos.z - from.z);
  if (length(d) > 1e-3f) {
    d = normalize(d);
    p.kx += d.x * knock;
    p.kz += d.z * knock;
  }
  add_float(g, v3(p.pos.x + 0.5f, p.pos.y + PL_H + 1.4f, p.pos.z), 0xFF5A4A, 100 + i, (int)(dmg + 0.5f), '-');
  if (p.hp > 0.0f) {
    ev_sound(g, SND_HURT, p.pos);
    return;
  }
  player_down(g, i, false);
}

// Fell into the cenote (0.5.1): water splashes and then the same as going down (lives, respawn
// next to a teammate, everyone down, messages), even if they were invulnerable or had the SCUDO.
// While down they sink slowly (player_update) and disappear under the water of the pit.
static void player_fall(Game& g, int i) {
  Player& p = g.pl[i];
  for (int k = 0; k < 10; ++k) {
    const float a = D_TWO_PI * (float)k / 10.0f;
    puff(g, p.pos + v3(0.0f, -0.6f, 0.0f), PK_PUFF, PC_WHITE, v3(d_cos(a) * 1.6f, 3.2f + 0.3f * (float)(k & 1), d_sin(a) * 1.6f), 0.7f, 26);
  }
  burst(g, p.pos + v3(0.0f, -0.4f, 0.0f), 8, PC_SNOW, 3.0f);
  add_float_text(g, v3(p.pos.x, 1.6f, p.pos.z), 0xBFEFFF, N_("SPLASH!"));
  ev_sound(g, SND_SPLASH, p.pos);   // the plop in the water comes after 0.75 s (no SND_DOWN on top)
  plat_log("giocatore %d caduto nel cenote", i + 1);
  p.hp = 0.0f;
  p.kx = p.kz = p.vx = p.vz = 0.0f;
  player_down(g, i, true);
}

// Down (health gone or fell into the cenote): lives, respawn, messages.
static void player_down(Game& g, int i, bool pit) {
  Player& p = g.pl[i];
  if (!pit) ev_sound(g, SND_DOWN, p.pos);
  p.hp = 0.0f;
  p.alive = false;
  p.pos.y = level_ground(&g.L, p.pos.x, p.pos.z, p.pos.y, PL_STEP);   // hit mid-jump: down to the ground
  p.vy = 0.0f;
  p.down = 0;
  p.blizzard_t = p.burst_left = p.tramp_t = 0;
  for (int k = 0; k < NPW_TIMED; ++k) p.pw_t[k] = 0;   // power-ups end with the fall
  p.deaths++;
  g.deaths++;
  burst(g, v3(p.pos.x, p.pos.y + 0.7f, p.pos.z), 16, i, 4.0f);
  // Every fall costs a life. With no lives left they stay out: alone, the map is restarted from the
  // start; with more players they don't come back (all down together: game.cpp restarts the map).
  if (p.lives > 0) p.lives--;
  p.out = p.lives == 0;
  p.respawn = p.out ? 0 : RESPAWN_FRAMES;
  plat_log("giocatore %d a terra (mappa %d, zona %d): vite rimaste %d", i + 1, g.map_idx + 1, g.zone + 1, p.lives);
  if (count_active(g) < 2) return;       // alone: zone replayed or map restarted (game.cpp)
  char big[160], small[224];
  if (p.out) {
    snprintf(big, sizeof(big), pit ? tr("G%d È CADUTO NEL CENOTE!") : tr("G%d SENZA VITE!"), i + 1);
    snprintf(small, sizeof(small), "%s", pit ? tr("senza vite: resta fuori fino alla fine") : tr("resta fuori fino alla fine della mappa"));
    plat_log("giocatore %d senza vite: resta fuori fino alla fine della mappa (o finche' non ricomincia)", i + 1);
  } else {
    snprintf(big, sizeof(big), pit ? tr("G%d È CADUTO NEL CENOTE!") : tr("G%d A TERRA!"), i + 1);
    if (p.lives == 1) snprintf(small, sizeof(small), "%s", tr("resta 1 vita: rientra tra 5 secondi"));
    else snprintf(small, sizeof(small), tr("restano %d vite: rientra tra 5 secondi"), p.lives);
  }
  show_msg(g, big, small, PLAYER_RGB[i], 2, p.out ? 150 : 100);
}

void player_join(Game& g, int i, int ch, bool mid_game) {
  Player& p = g.pl[i];
  memset(&p, 0, sizeof(p));
  p.active = true;
  p.ch = (uint8_t)ch;
  p.hp_max = p.hp = CHARS[ch].hp;
  p.speed = CHARS[ch].speed;
  p.ammo = CHARS[ch].ammo_max;
  p.yaw = D_PI;
  p.lives = START_LIVES;
  if (!mid_game) return;
  p.pos = g.L.zones[g.zone].start[i];
  // A player who had already joined in this attempt at the map gets back the lives they had
  // (leaving and joining again doesn't refill them).
  if (g.lives_kept[i] >= 0) {
    p.lives = g.lives_kept[i];
    if (g.vita_kept[i]) p.cards |= 1u << CA_VITA;    // already taken: not offered again
  }
  if (g.stats_kept[i]) {
    p.kills = g.kills_kept[i];
    p.deaths = g.deaths_kept[i];
    p.dmg_done = g.dmg_kept[i];
  }
  plat_log("giocatore %d entra in partita con %s (mappa %d, zona %d, vite %d)", i + 1, CHARS[ch].name, g.map_idx + 1, g.zone + 1,
           p.lives);
  if (p.lives <= 0) {
    // Out of lives: stays out until the end of the map, as if they had gone down.
    p.out = true;
    p.alive = false;
    p.down = OUT_VANISH;
    plat_log("giocatore %d e' senza vite: entra quando la mappa ricomincia o alla prossima", i + 1);
    return;
  }
  if (place_near_group(g, i)) {
    p.invuln = INVULN_FRAMES;
  } else {
    // Nobody standing: joins at the next restart of the zone.
    p.alive = false;
    p.respawn = 60;
  }
}

void player_leave(Game& g, int i) {
  Player& p = g.pl[i];
  if (!p.active) return;
  if (p.alive) burst(g, v3(p.pos.x, p.pos.y + 0.6f, p.pos.z), 14, i, 3.0f);
  p.active = false;
  p.alive = false;
  if (g.screen == SC_PLAY) {
    // If they join again: in this attempt with these lives (and VITA EXTRA already taken), in this
    // map with their statistics.
    g.lives_kept[i] = p.lives;
    g.vita_kept[i] = has_card(p, CA_VITA);
    g.stats_kept[i] = true;
    g.kills_kept[i] = p.kills;
    g.deaths_kept[i] = p.deaths;
    g.dmg_kept[i] = p.dmg_done;
  }
  card_lose_all(g, i);
  plat_log("giocatore %d lascia la partita (vite %d)", i + 1, p.lives);
}

void players_gather(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active || p.alive) continue;
    int best = -1;
    float bd = 1e30f;
    for (int j = 0; j < MAX_PLAYERS; ++j) {
      const Player& q = g.pl[j];
      if (j == i || !q.active || !q.alive) continue;
      const float d = dist2d(q.pos, p.pos);
      if (d < bd) { bd = d; best = j; }
    }
    if (best < 0) {
      // Nobody standing (alone, or everyone down when the map ends): to the start of the zone, so
      // nobody stays at the bottom of a cenote for the celebration and the statistics.
      Vec3 pos = g.L.zones[g.zone].start[i];
      pos.y = level_ground(&g.L, pos.x, pos.z, 0.0f, PL_STEP);
      p.pos = pos;
      continue;
    }
    Vec3 pos = free_spot_near(g, g.pl[best].pos);
    pos.y = level_ground(&g.L, pos.x, pos.z, g.pl[best].pos.y, PL_STEP);
    p.pos = pos;
    p.yaw = g.pl[best].yaw;
  }
}

void place_players(Game& g, int z) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    if (!p.active || p.out) continue;   // a player out of lives stays out
    reset_body(p, g.L.zones[z].start[i]);
    p.hp = p.hp_max;
    p.yaw = D_PI;                // everyone faces forward (toward north)
    p.invuln = 60;
  }
}

void player_update(Game& g, int i, const Pad& raw) {
  Player& p = g.pl[i];
  if (!p.active) return;

  // Controller that stays stuck in the same state (not at rest) for 30 seconds: it is ignored
  // until new input arrives (for example a button left "pressed" in a LAN game).
  const bool same = raw.btn == p.last_pad.btn && raw.lx == p.last_pad.lx && raw.ly == p.last_pad.ly &&
                    raw.rx == p.last_pad.rx && raw.ry == p.last_pad.ry;
  // At rest: no button, left stick centered, right stick far from the aim threshold (a right
  // stick that stays a bit off-center at rest doesn't count as stuck input).
  const bool idle = raw.btn == 0 && raw.lx == 0.0f && raw.ly == 0.0f && raw.rx * raw.rx + raw.ry * raw.ry < 0.25f * 0.25f;
  if (same && !idle && i >= g.bots) {
    if (p.same_t < FROZEN_FRAMES) p.same_t++;
    if (p.same_t >= FROZEN_FRAMES && !p.frozen) {
      p.frozen = true;
      plat_log("giocatore %d: comandi fermi da 30 secondi, il personaggio si ferma", i + 1);
    }
  } else {
    if (p.frozen) plat_log("giocatore %d: comandi di nuovo attivi", i + 1);
    p.frozen = false;
    p.same_t = 0;
  }
  p.last_pad = raw;
  Pad pad = raw;
  if (p.frozen) memset(&pad, 0, sizeof(pad));
  const uint16_t pressed = (uint16_t)(pad.btn & ~p.prev_btn);
  p.prev_btn = pad.btn;

  if (!p.alive) {
    if (p.down < 100000) p.down++;
    // Fell into the cenote: sinks down to below the water of the pit.
    if (p.pos.y > PIT_WATER_Y - 1.2f && level_pit_at(&g.L, p.pos.x, p.pos.z)) p.pos.y -= p.pos.y > PIT_WATER_Y ? 0.07f : 0.02f;
    // Out of lives: after a while vanishes in a puff (the icon at the top stays, grey with the X).
    if (p.out && p.down == OUT_VANISH) {
      puff(g, p.pos + v3(0, 0.5f, 0), PK_PUFF, PC_WHITE, v3(0, 0.8f, 0), 1.3f, 24);
      burst(g, p.pos + v3(0, 0.6f, 0), 10, PC_GREY, 2.5f);
    }
    if (!p.out && p.respawn > 0 && g.phase != PH_WIPE && --p.respawn == 0) respawn_player(g, i);
    return;
  }
  if (p.invuln > 0) p.invuln--;
  // Crate power-ups (0.5.4): time runs only during play (it stops in the pause and with the cards).
  for (int k = 0; k < NPW_TIMED; ++k)
    if (p.pw_t[k] > 0 && --p.pw_t[k] == 0) ev_sound(g, SND_POWER_END, p.pos);
  if (p.hurt > 0) p.hurt--;
  if (p.recoil > 0) p.recoil--;
  if (p.aim_hold > 0) p.aim_hold--;
  if (p.reveal > 0) p.reveal--;
  if (p.land > 0) p.land--;
  if (p.flame_t > 0) p.flame_t--;
  if (p.tramp_t < 0) p.tramp_t++;
  // Squash & stretch: a damped spring around the normal shape.
  p.sq_v += (-320.0f * p.sq - 16.0f * p.sq_v) * DT;
  p.sq += p.sq_v * DT;
  p.sq = clampf(p.sq, -0.4f, 0.4f);
  if (p.blizzard_t > 0) update_blizzard(g, i);

  if (p.leap_n > 0) {
    update_leap(g, i);
    return;
  }

  // Left stick or d-pad: movement. Right stick: aim.
  float ix = pad.lx, iy = pad.ly;
  if (pad.btn & (1u << PAD_LEFT)) ix -= 1.0f;
  if (pad.btn & (1u << PAD_RIGHT)) ix += 1.0f;
  if (pad.btn & (1u << PAD_UP)) iy -= 1.0f;
  if (pad.btn & (1u << PAD_DOWN)) iy += 1.0f;
  float mag = sqrtf(ix * ix + iy * iy);
  if (mag > 1.0f) { ix /= mag; iy /= mag; mag = 1.0f; }
  update_aim(g, i, pad);
  const bool jump = (pad.btn & (1u << PAD_R2)) != 0;   // jump: R2 (held, higher)
  const bool fire = (pad.btn & (1u << PAD_R)) != 0;
  const CharDef& cd = CHARS[p.ch];
  const float run = pl_speed(p);        // PASSO SVELTO

  // Movement: speed that reaches the requested one almost immediately (no sliding).
  if (p.tramp_t > 0) {
    p.vx = p.tvx;
    p.vz = p.tvz;
  } else {
    p.vx += (ix * run - p.vx) * PL_ACCEL;
    p.vz += (iy * run - p.vz) * PL_ACCEL;
    if (fabsf(p.vx) < 0.01f) p.vx = 0.0f;
    if (fabsf(p.vz) < 0.01f) p.vz = 0.0f;
  }
  float mvx = (p.vx + p.kx) * DT, mvz = (p.vz + p.kz) * DT;
  const float mv = sqrtf(mvx * mvx + mvz * mvz);
  if (mv > 0.22f) { mvx *= 0.22f / mv; mvz *= 0.22f / mv; }
  p.kx *= 0.82f;
  p.kz *= 0.82f;
  if (fabsf(p.kx) < 0.01f) p.kx = 0.0f;
  if (fabsf(p.kz) < 0.01f) p.kz = 0.0f;
  float nx = p.pos.x + mvx, nz = p.pos.z + mvz;
  span_clamp(g, i, &nx, &nz);
  // Map finished: celebration, and the cenotes stop you like walls (no more falling in).
  const int body = g.phase == PH_MAP_DONE ? (BODY_WALK | BODY_PIT) : BODY_WALK;
  level_push_out(&g.L, &nx, &nz, p.pos.y, PL_R, PL_STEP, body);
  boss_push(g, &nx, &nz, PL_R);
  level_body_guard(&g.L, p.pos.x, p.pos.z, &nx, &nz, p.pos.y, PL_R, PL_STEP, body);   // 0.5.1: never inside an obstacle
  const float moved = sqrtf((nx - p.pos.x) * (nx - p.pos.x) + (nz - p.pos.z) * (nz - p.pos.z));
  const float speed = moved / DT;
  p.phase = d_wrap_pi(p.phase + moved * 3.4f);
  p.amp = lerpf(p.amp, speed > 0.3f ? 0.75f * clampf(speed / run, 0.0f, 1.0f) : 0.0f, 0.25f);

  // Where they look: where they aim, or where they just attacked, or where they go.
  if (p.aiming) {
    p.yaw = p.aim_yaw;
  } else if (p.aim_hold == 0 && mag > 0.15f && p.tramp_t <= 0) {
    const float diff = d_wrap_pi(yaw_of(ix, iy) - p.yaw);
    p.yaw = d_wrap_pi(p.yaw + clampf(diff, -PL_TURN * DT, PL_TURN * DT));
  }

  // Jump: holding B down goes higher.
  if ((pressed & (1u << PAD_R2)) && p.grounded) {
    p.vy = PL_JUMP_V;
    p.grounded = false;
    p.sq_v += 3.5f;
    ev_sound(g, SND_JUMP, p.pos);
  }
  if (!jump && !p.grounded && p.tramp_t <= 0 && p.vy > 2.5f) p.vy = 2.5f;
  if (!p.grounded) p.vy -= GRAVITY * DT;
  float ny = p.pos.y + p.vy * DT;
  const float gnd = level_ground(&g.L, nx, nz, p.pos.y, PL_STEP);
  if (ny <= gnd) {
    if (!p.grounded && p.vy < -4.0f) {
      p.sq_v -= clampf(-p.vy * 0.45f, 1.5f, 5.0f);
      p.land = 8;
      puff(g, v3(nx, gnd + 0.05f, nz), PK_PUFF, PC_WHITE, v3(0, 0.3f, 0), 0.6f, 12);
      ev_sound(g, SND_LAND, p.pos);
    }
    ny = gnd;
    p.vy = 0.0f;
    p.grounded = true;
    if (p.tramp_t > 0) p.tramp_t = -20;
  } else if (p.grounded) {
    if (p.pos.y - gnd <= 0.18f) ny = gnd;          // small height difference: stays on the ground
    else { p.grounded = false; p.vy = 0.0f; }       // walked off an edge: falls
  }
  p.pos = v3(nx, ny, nz);
  // Cenote: on the ground with the center over the pit you fall in (jumping over it, no).
  if (g.phase != PH_MAP_DONE && p.grounded && p.pos.y < 0.05f && level_pit_at(&g.L, p.pos.x, p.pos.z)) {
    player_fall(g, i);
    return;
  }

  // Trampoline: launches far in the direction you are going.
  if (p.grounded && p.tramp_t == 0 && p.pos.y <= TRAMP_H + 0.02f && level_type_at(&g.L, p.pos.x, p.pos.z) == C_TRAMP) {
    const Vec3 d = mag > 0.15f ? normalize(v3(ix, 0.0f, iy)) : dir_yaw(p.yaw);
    p.tvx = d.x * TRAMP_SPEED;
    p.tvz = d.z * TRAMP_SPEED;
    p.vy = TRAMP_VY;
    p.grounded = false;
    p.tramp_t = 1;
    p.sq_v += 5.0f;
    ev_sound(g, SND_TRAMP, p.pos);
    int cx, cz;
    if (level_cell(&g.L, p.pos.x, p.pos.z, &cx, &cz) && g.L.dyn_at[cz][cx] >= 0) g.tramp_t[g.L.dyn_at[cz][cx]] = 20;
    plat_log("giocatore %d sul trampolino", i + 1);
  }

  // Normal shot: a bar that refills by itself shortly after the last attack. If it empties
  // completely, you can attack again only once it is full again.
  if (p.fire_cd > 0) p.fire_cd--;
  if (p.reload_wait > 0) {
    p.reload_wait--;
  } else if (p.ammo < cd.ammo_max) {
    p.ammo += cd.ammo_max / pl_reload_frames(p);     // RICARICA LAMPO
    if (p.ammo >= cd.ammo_max) { p.ammo = cd.ammo_max; p.locked = false; }
  }
  const float attack_yaw = p.aiming ? p.aim_yaw : p.yaw;
  if (fire && !p.locked && p.fire_cd == 0 && p.ammo >= cd.ammo_cost) attack_normal(g, i, attack_yaw);
  if (p.burst_left > 0 && --p.burst_cd <= 0) {
    // Astro: the three shots of the burst sweep the fan (left, center, right).
    const float side = 0.5f * (float)(p.burst_left - 2);
    fire_shot(g, i, SK_BULLET, p.burst_yaw + cd.aim_w * side, 18.0f, pl_range(p), 12.0f, 0.12f, false);
    ev_sound(g, SND_BULLET, p.pos);
    p.burst_left--;
    p.burst_cd = 4;
  }
  if ((pressed & (1u << PAD_L)) && p.charge >= pl_special_cost(p)) special(g, i, attack_yaw);
  if (pressed & (1u << PAD_L2)) {   // bomb: L2
    if (bomb_drop(g, i)) p.sq_v -= 1.5f;
  }

  const int t = level_type_at(&g.L, p.pos.x, p.pos.z);
  p.hidden = (t == C_BUSH || t == C_GRASS) && p.reveal == 0 && p.pos.y < 0.5f;
}
