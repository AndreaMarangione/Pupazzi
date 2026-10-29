// Giocatori automatici, solo per le prove (PUPAZZI_BOT=n): giocano da soli con i comandi veri
// (levetta sinistra, mira con la levetta destra, R1, L1, L2, R2, A) per verificare regole,
// prestazioni e determinismo senza nessuno ai tasti. Decidono solo in base allo stato del
// gioco (mai con g.rng), quindi la partita resta identica su ogni processore. A terra o senza
// vite non fanno nulla; quando la zona o la mappa ricominciano ripartono da dove sono messi.
// Contro il gladiatore (0.5.1) escono dal settore del fendente e dalla corsia della carica e per il
// resto gli girano attorno da 5 a 8 m, sparando.
#include "game_int.h"
#include <string.h>

namespace {

struct Bot {
  Vec3 last, goal;
  int check_t, side_t, jump_t, strafe_t, field_t, menu_t;
  float side, strafe;
  int orbit_t;                        // gladiatore: fotogrammi prima di poter cambiare verso del giro attorno
  float orbit;                        // ...e il verso (+1 / -1)
  bool l1_prev, a_prev, field_ok;
  unsigned card_seq;                  // scelta delle carte gia' vista
  int card_t, card_want, card_wait;
  int cage_t;                         // da quanto cercano di aprire la capsula dell'esploratore
  int heal_t, heal_off;               // da quanto inseguono un talismano / per quanto lasciarli stare
  uint16_t field[FLOW_W * FLOW_H];
};

Bot B[MAX_PLAYERS];
int g_lag;                            // prove: i segni a terra si vedono dopo cosi' tanti fotogrammi (bot_set_lag)

// Carte: ognuno preme A sul banner per vedere le sue carte (dalla 0.5.4), aspetta un po' (30-60
// fotogrammi, diverso per giocatore), a volte sposta il cursore e conferma con A. Le scelte vengono
// da un miscuglio di numeri della partita: g.rng non si tocca.
void bot_cards(int i, Pad& pad) {
  Bot& b = B[i];
  const CardOffer& o = g.offer[i];
  if (!o.on || o.done || o.n <= 0) return;
  if (b.card_seq != g.cards_seq) {
    uint32_t h = g.tick * 2654435761u ^ (uint32_t)(i + 1) * 40503u ^ g.cards_seq * 2246822519u ^ (uint32_t)g.map_idx * 97u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    b.card_seq = g.cards_seq;
    b.card_t = 0;
    b.card_want = (int)(h % (uint32_t)o.n);
    b.card_wait = 30 + (int)((h >> 8) % 31u);
  }
  b.card_t++;
  if (!o.shown) {                       // il banner: A (premuto a fotogrammi alterni, ogni volta di nuovo)
    if (b.card_t >= 20 + (b.card_wait - 30) / 2 && (b.card_t & 1)) pad.btn = (uint16_t)(1u << PAD_A);
    return;
  }
  if (o.sel != b.card_want) {
    if (b.card_t % 8 == 0) pad.btn = (uint16_t)(1u << (b.card_want > o.sel ? PAD_DOWN : PAD_UP));
    return;
  }
  if (b.card_t >= b.card_wait && (b.card_t & 1)) pad.btn = (uint16_t)(1u << PAD_A);
}

// Bersaglio: il mostro piu' vicino (o il boss), con la distanza.
bool nearest_target(const Player& p, Vec3* where, float* dist, int* type) {
  float bd = 1e30f;
  bool found = false;
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    const Monster& m = g.mo[k];
    if (!m.alive || m.rise > 0) continue;
    const float d = dist2d(m.pos, p.pos);
    if (d < bd) { bd = d; *where = monster_center(m); *type = m.type; found = true; }
  }
  if (g.boss.alive) {
    const float d = dist2d(g.boss.pos, p.pos) - boss_r();
    if (d < bd) { bd = d; *where = g.boss.pos + v3(0, 1.5f, 0); *type = -1; found = true; }
  }
  *dist = bd;
  return found;
}

int monsters_near(Vec3 p, float r) {
  int n = 0;
  for (int k = 0; k < MAX_MONSTERS; ++k)
    if (g.mo[k].alive && g.mo[k].rise == 0 && dist2d(g.mo[k].pos, p) < r) n += g.mo[k].type == MO_GUERRIERO ? 2 : 1;
  if (g.boss.alive && dist2d(g.boss.pos, p) < r + boss_r()) n += 3;
  return n;
}

bool nearest_item(const Player& p, int kind, float maxd, Vec3* out) {
  float bd = maxd;
  bool found = false;
  for (int it = 0; it < g.L.nitems; ++it) {
    if (!g.item_on[it] || g.L.items[it].kind != kind) continue;
    const MapItem& mi = g.L.items[it];
    Vec3 c = cell_center(&g.L, mi.cx, mi.cz);
    c.y = mi.y;
    const float d = dist2d(c, p.pos);
    if (d < bd) { bd = d; *out = c; found = true; }
  }
  return found;
}

// Zona appena ripulita con l'esploratore ancora nella capsula: si va a salvarlo (per al massimo
// 20 secondi, poi avanti), cosi' le prove coprono anche la capsula e le stelle. goal: la casella
// libera accanto alla capsula piu' vicina; capsule: il centro della capsula.
bool capsule_goal(const Player& p, Bot& b, Vec3* goal, Vec3* capsule) {
  if (g.phase != PH_CLEAR || g.L.capsule_dyn < 0 || g.explorer_free) { b.cage_t = 0; return false; }
  const DynCell& dc = g.L.dyn[g.L.capsule_dyn];
  if (dc.hp <= 0.0f || dc.zone != g.zone || ++b.cage_t > 1200) return false;
  *capsule = cell_center(&g.L, dc.cx, dc.cz);
  static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { -1, 0 }, { 0, -1 } };
  float bd = 1e30f;
  bool ok = false;
  for (int k = 0; k < 4; ++k) {
    const int x = dc.cx + D[k][0], z = dc.cz + D[k][1];
    if (x < 0 || z < 0 || x >= g.L.W || z >= g.L.H || g.L.zone[z][x] != g.zone) continue;
    if (level_walk_top(&g.L, x, z) > PL_STEP) continue;
    const Vec3 c = cell_center(&g.L, x, z);
    const float d = dist2d(c, p.pos);
    if (d < bd) { bd = d; *goal = c; ok = true; }
  }
  return ok;
}

// Si puo' fare un passo da 1,6 m in quella direzione (niente muri, rocce, casse alte)?
bool step_ok(const Player& p, float dx, float dz) {
  return walk_clear(p.pos, v3(p.pos.x + dx * 1.6f, 0.0f, p.pos.z + dz * 1.6f), PL_R, false);
}

// Gladiatore: dentro il settore del fendente o la corsia della carica mentre si preparano (o mentre
// corre)? In (ox, oz) la via d'uscita piu' corta: dal settore all'indietro o dal lato piu' vicino,
// dalla corsia di lato; se da quella parte c'e' un ostacolo, l'altra.
bool boss_escape(const Player& p, int i, float* ox, float* oz) {
  for (int k = 0; k < MAX_MARKERS; ++k) {
    const Marker& m = g.mk[k];
    if (!m.alive || (m.kind != MK_SWEEP && m.kind != MK_LANE) || m.t < g_lag) continue;
    const float vx = p.pos.x - m.pos.x, vz = p.pos.z - m.pos.z;
    float ax, az, bx, bz;                      // la via migliore e l'altra
    if (m.kind == MK_SWEEP) {
      if (m.t > m.fire_at) continue;
      const float d = sqrtf(vx * vx + vz * vz), half = BOSS_SWEEP_HALF + 0.3f;
      const float off = d > 0.3f ? d_wrap_pi(yaw_of(vx, vz) - m.yaw) : 0.0f;
      if (d > m.r + PL_R + 0.6f || fabsf(off) > half) continue;
      const float ux = d > 0.3f ? vx / d : d_sin(m.yaw), uz = d > 0.3f ? vz / d : d_cos(m.yaw);
      const float s = off >= 0.0f ? 1.0f : -1.0f;
      const float tx = uz * s, tz = -ux * s;   // di lato, verso il bordo piu' vicino del settore
      const float out_r = m.r + PL_R + 0.6f - d, out_s = d * d_sin(fminf(half - fabsf(off), D_HALF_PI));
      if (out_r <= out_s) { ax = ux; az = uz; bx = tx; bz = tz; }
      else { ax = tx + ux * 0.3f; az = tz + uz * 0.3f; bx = ux; bz = uz; }
    } else {
      const float dx = d_sin(m.yaw), dz = d_cos(m.yaw);
      const float along = vx * dx + vz * dz, lat = vx * dz - vz * dx;
      if (along < -1.5f || along > m.len + 1.2f || fabsf(lat) > m.r + PL_R + 0.7f) continue;
      // Gia' corso oltre: dietro di lui non si rischia piu'.
      if (g.boss.alive && g.boss.act == BA_RUSH && (g.boss.pos.x - m.pos.x) * dx + (g.boss.pos.z - m.pos.z) * dz > along + 1.8f) continue;
      const float s = lat > 0.05f ? 1.0f : (lat < -0.05f ? -1.0f : ((i & 1) ? 1.0f : -1.0f));
      ax = dz * s; az = -dx * s;
      bx = -ax; bz = -az;
    }
    if (!step_ok(p, ax, az) && step_ok(p, bx, bz)) { ax = bx; az = bz; }
    *ox = ax;
    *oz = az;
    return true;
  }
  return false;
}

// Gladiatore vicino: gli si gira attorno alla distanza giusta per colpirlo (da 5 a 8 m dal centro).
// Il verso del giro si sceglie dallo spazio libero davanti e da quanto porta verso il centro
// dell'arena (negli angoli si resta chiusi e lui arriva col fendente); se dietro c'e' un muro non
// ci si allontana: si scivola di lato attorno a lui.
void boss_orbit(const Player& p, Bot& b, int i, float range, float* mx, float* mz) {
  const Boss& bs = g.boss;
  const float dx = p.pos.x - bs.pos.x, dz = p.pos.z - bs.pos.z, d = fmaxf(sqrtf(dx * dx + dz * dz), 1e-3f);
  const float ux = dx / d, uz = dz / d;
  const float keep = clampf(range + boss_r() + 0.4f - 1.0f, 5.0f, 8.0f);
  float rad = clampf((keep - d) * 0.7f, -1.0f, 1.0f);
  if (rad > 0.0f && !step_ok(p, ux, uz)) rad = 0.0f;
  const ZoneDef& z = g.L.zones[g.zone];
  float ex = 0.5f * (z.wx0 + z.wx1) - p.pos.x, ez = 0.5f * (z.wz0 + z.wz1) - p.pos.z;
  const float el = sqrtf(ex * ex + ez * ez);
  if (el > 1e-3f) {
    ex /= el;
    ez /= el;
  }
  if (b.orbit == 0.0f) b.orbit = (i & 1) ? 1.0f : -1.0f;
  if (b.orbit_t > 0) b.orbit_t--;
  float best = -1e30f, pick = b.orbit;
  for (int s = -1; s <= 1; s += 2) {
    const float sx = ux * rad - uz * (float)s * 0.9f, sz = uz * rad + ux * (float)s * 0.9f, sl = sqrtf(sx * sx + sz * sz);
    const float nx = sx / sl, nz = sz / sl;
    float score = (nx * ex + nz * ez) * 0.8f * fminf(el / 8.0f, 1.5f);
    if (step_ok(p, nx, nz)) score += 2.0f;
    if (walk_clear(p.pos, v3(p.pos.x + nx * 3.2f, 0.0f, p.pos.z + nz * 3.2f), PL_R, false)) score += 1.0f;
    if ((float)s == b.orbit) score += 0.6f;      // senza cambiare a ogni passo
    if (score > best) {
      best = score;
      pick = (float)s;
    }
  }
  if (pick != b.orbit && b.orbit_t == 0) {
    b.orbit = pick;
    b.orbit_t = 45;
  }
  *mx = ux * rad - uz * b.orbit * 0.9f;
  *mz = uz * rad + ux * b.orbit * 0.9f;
  if (el > 8.0f) {                              // lontano dal centro: un po' verso il centro
    const float k = fminf((el - 8.0f) * 0.15f, 0.6f);
    *mx += ex * k;
    *mz += ez * k;
  }
}

// I comandi del giocatore automatico i in questo fotogramma.
void bot_one(int i, Pad& pad) {
  {
    memset(&pad, 0, sizeof(pad));
    Bot& b = B[i];
    if (g.screen != SC_PLAY) {
      // Menu: A ogni tanto (avanti dalle statistiche, pronto nella scelta dei personaggi).
      if (++b.menu_t >= 40) {
        b.menu_t = 0;
        pad.btn = 1u << PAD_A;
      }
      return;
    }
    if (g.cards_open) {               // scelta delle carte: il gioco e' fermo
      bot_cards(i, pad);
      return;
    }
    const Player& p = g.pl[i];
    if (!p.active) {
      if (g.tick > 30u + 20u * (unsigned)i) pad.btn = (g.tick & 8u) ? (uint16_t)(1u << PAD_A) : 0;   // entra in partita
      return;
    }
    if (!p.alive || g.paused || p.leap_n > 0) return;
    const CharDef& cd = CHARS[p.ch];

    Vec3 tp = p.pos, it;
    float td = 1e30f;
    int ttype = 0;
    const bool have = nearest_target(p, &tp, &td, &ttype);
    bool have_goal = false, jumpy = false, fire = false, to_capsule = false;
    Vec3 goal = p.pos, capsule = p.pos;
    float mx = 0.0f, mz = 0.0f;
    const float range = pl_range(p), cost = pl_special_cost(p);   // con le carte (LUNGA GITTATA, SUPER CARICA)
    const float want = range * 0.75f;
    // Talismano per curarsi; se in 10 secondi non lo prendono (uno su una piattaforma da un lato
    // stretto) lo lasciano stare per 15 secondi.
    if (b.heal_off > 0) b.heal_off--;
    const bool heal = b.heal_off == 0 && p.hp < p.hp_max * 0.45f && nearest_item(p, IT_TALISMAN, 14.0f, &it);
    if (!heal) b.heal_t = 0;
    else if (++b.heal_t > 600) { b.heal_t = 0; b.heal_off = 900; }
    if (heal && b.heal_off == 0) {
      goal = it;
      have_goal = true;
      jumpy = it.y > 0.1f && dist2d(it, p.pos) < 3.5f;
    } else if (p.bombs < pl_bombs_cap(p) && nearest_item(p, IT_BOMB, 5.0f, &it) && (!have || td > 4.0f)) {
      goal = it;
      have_goal = true;
    } else if (g.boss.alive && dist2d(g.boss.pos, p.pos) < 16.0f) {
      boss_orbit(p, b, i, range, &mx, &mz);   // gladiatore: gli si gira attorno sparando (i mostri si colpiscono lo stesso)
    } else if (have) {
      const float ux = (tp.x - p.pos.x) / (td > 1e-3f ? td : 1.0f), uz = (tp.z - p.pos.z) / (td > 1e-3f ? td : 1.0f);
      if (--b.strafe_t <= 0) { b.strafe_t = 70; b.strafe = b.strafe > 0.0f ? -1.0f : 1.0f; }
      if (td > want + 1.5f) {
        goal = v3(tp.x, 0.0f, tp.z);
        have_goal = true;
      } else if (td < want * 0.5f || (ttype == MO_SCORPIONE && td < 3.5f) || ttype == -1) {
        mx = -ux - uz * b.strafe * 0.7f;
        mz = -uz + ux * b.strafe * 0.7f;
      } else {
        mx = -uz * b.strafe;
        mz = ux * b.strafe;
      }
    } else if (capsule_goal(p, b, &goal, &capsule)) {
      have_goal = true;
      to_capsule = true;
    } else if (g.phase == PH_CLEAR && g.zone + 1 < g.L.nzones) {
      goal = g.L.zones[g.zone + 1].start[i];
      have_goal = true;
    } else if (g.phase == PH_FIGHT || g.phase == PH_ENTER) {
      const ZoneDef& z = g.L.zones[g.zone];
      goal = v3(0.5f * (z.wx0 + z.wx1), 0.0f, 0.5f * (z.wz0 + z.wz1) + 2.0f);
      have_goal = dist2d(goal, p.pos) > 2.5f;
    }

    if (have_goal && dist2d(goal, p.pos) > 0.5f) {
      if (walk_clear(p.pos, goal, PL_R, false)) {
        mx = goal.x - p.pos.x;
        mz = goal.z - p.pos.z;
      } else {
        if (!b.field_ok || --b.field_t <= 0 || dist2d(goal, b.goal) > 1.5f) {
          flow_build(b.field, &goal, 1, -1);
          b.field_ok = true;
          b.field_t = 30;
          b.goal = goal;
        }
        if (!flow_dir(b.field, p.pos.x, p.pos.z, &mx, &mz)) {
          mx = goal.x - p.pos.x;
          mz = goal.z - p.pos.z;
        }
      }
    }
    // Pericoli segnati a terra: fendente e carica del gladiatore prima di tutto, poi giavellotti in
    // arrivo e crepe che stanno per spaccarsi.
    float ex = 0.0f, ez = 0.0f;
    const bool escape = boss_escape(p, i, &ex, &ez);
    if (escape) {
      mx = ex;
      mz = ez;
    } else {
      float ddx = 0.0f, ddz = 0.0f;
      for (int k = 0; k < MAX_MARKERS; ++k) {
        const Marker& m = g.mk[k];
        if (!m.alive || m.t < g_lag) continue;
        const bool lob = m.kind == MK_LOB, root = m.kind == MK_ROOT && m.t < m.fire_at;
        if (!lob && !root) continue;
        const float d = dist2d(p.pos, m.pos);
        if (d > m.r + (lob ? 0.7f : 0.6f) || d < 1e-3f) continue;
        ddx += (p.pos.x - m.pos.x) / d;
        ddz += (p.pos.z - m.pos.z) / d;
      }
      if (ddx != 0.0f || ddz != 0.0f) {
        mx = ddx;
        mz = ddz;
      }
    }
    const float l = sqrtf(mx * mx + mz * mz);
    if (l > 1e-4f) { mx /= l; mz /= l; }

    // Bloccato contro qualcosa: salta e scansa di lato.
    if (++b.check_t >= 30) {
      b.check_t = 0;
      if (l > 1e-4f && dist2d(p.pos, b.last) < 0.35f) {
        b.jump_t = 14;
        b.side_t = 25;
        b.side = (g.tick & 32u) ? 1.0f : -1.0f;
      }
      b.last = p.pos;
    }
    if (b.side_t > 0) {
      b.side_t--;
      const float k = escape ? 0.4f : 1.0f;     // scappando dal gladiatore si scansa poco
      const float sx = mx - mz * b.side * k, sz = mz + mx * b.side * k;
      const float sl = sqrtf(sx * sx + sz * sz);
      if (sl > 1e-4f) { mx = sx / sl; mz = sz / sl; }
    }
    if (jumpy && p.grounded && b.jump_t == 0) b.jump_t = 14;
    if (b.jump_t > 0) {
      b.jump_t--;
      if (b.jump_t > 0) pad.btn |= 1u << PAD_R2;
    }
    // Cenote (0.5.1): schivando, indietreggiando o scansando di lato non ci si butta dentro. Si
    // guarda il tratto davanti fino a 1,5 m con il proprio raggio; se c'e' il pozzo si gira di 90
    // gradi da una parte o dall'altra (o si torna indietro); vicino al pozzo niente salti.
    if (level_pit_near(&g.L, p.pos.x, p.pos.z, 2.2f)) {
      pad.btn &= (uint16_t)~(1u << PAD_R2);
      if (mx != 0.0f || mz != 0.0f) {
        const float dirs[4][2] = { { mx, mz }, { -mz, mx }, { mz, -mx }, { -mx, -mz } };
        for (int t = 0; t < 4; ++t) {
          bool ok = true;
          for (int s = 1; s <= 3 && ok; ++s)
            ok = !level_pit_near(&g.L, p.pos.x + dirs[t][0] * 0.5f * (float)s, p.pos.z + dirs[t][1] * 0.5f * (float)s, PL_R);
          if (ok) {
            mx = dirs[t][0];
            mz = dirs[t][1];
            break;
          }
        }
      }
    }
    pad.lx = mx;
    pad.ly = mz;

    // Mira con la levetta destra e attacchi.
    if (have && td < fmaxf(range, cd.sp_range) + 1.5f) {
      const float dx = tp.x - p.pos.x, dz = tp.z - p.pos.z, dl = sqrtf(dx * dx + dz * dz);
      float k = 1.0f;
      // Speciale a parabola quasi carico: la levetta si porta in anticipo all'inclinazione giusta,
      // cosi' quando parte la gittata ha gia' seguito la levetta (come farebbe un giocatore).
      if (cd.aim_sp == AIM_LOB && p.charge >= cost * 0.85f) k = clampf(td / cd.sp_range, 0.2f, 1.0f);
      if (dl > 1e-3f) {
        const float on = aim_tune(i).on;
        const float mag = fminf(1.0f, on + k * (AIM_FULL - on) + 0.02f);
        float ax = dx / dl, az = dz / dl * AIM_FORESHORTEN;
        const float al = sqrtf(ax * ax + az * az);
        pad.rx = ax / al * mag;
        pad.ry = az / al * mag;
      }
      const bool los = !level_blocked(&g.L, player_chest(p), tp);
      fire = los && td < range + 0.4f;
      bool sp = false;
      if (p.charge >= cost) {
        switch (p.ch) {
          case CH_MARZIO: sp = monsters_near(p.pos, 3.5f) >= 2; break;
          case CH_MOCHI: sp = td > 1.5f && td < 6.0f; break;
          default: sp = td < cd.sp_range; break;
        }
      }
      // A parabola si lancia solo quando la gittata mostrata a terra e' quella voluta.
      if (cd.aim_sp == AIM_LOB && (!p.aiming || fabsf(p.aim_k - k) > 0.1f)) sp = false;
      if (sp && !b.l1_prev) pad.btn |= 1u << PAD_L;
      b.l1_prev = sp && !b.l1_prev;
    }
    // La capsula dell'esploratore: da vicino la si mira e le si spara.
    if (to_capsule && !have && dist2d(capsule, p.pos) < range) {
      const float dx = capsule.x - p.pos.x, dz = capsule.z - p.pos.z, dl = sqrtf(dx * dx + dz * dz);
      if (dl > 1e-3f) {
        const float on = aim_tune(i).on, mag = fminf(1.0f, on + 0.5f * (AIM_FULL - on) + 0.02f);
        const float ax = dx / dl, az = dz / dl * AIM_FORESHORTEN, al = sqrtf(ax * ax + az * az);
        pad.rx = ax / al * mag;
        pad.ry = az / al * mag;
        fire = p.aiming;
      }
    }
    if (fire) pad.btn |= 1u << PAD_R;
    const bool bomb = p.bombs > 0 && monsters_near(p.pos, 2.6f) >= 2;
    if (bomb && !b.a_prev) pad.btn |= 1u << PAD_L2;
    b.a_prev = bomb && !b.a_prev;
  }
}

}  // namespace

void bot_reset(void) { memset(B, 0, sizeof(B)); }
void bot_set_lag(int frames) { g_lag = frames > 0 ? frames : 0; }

void game_bot_pads(Pad pads[MAX_PLAYERS], int n) {
  if (n > MAX_PLAYERS) n = MAX_PLAYERS;
  g.bots = n;
  for (int i = 0; i < n; ++i) bot_one(i, pads[i]);
}

void game_bot_pad(int player, Pad* out) {
  if (player >= 0 && player < MAX_PLAYERS) bot_one(player, *out);
}
