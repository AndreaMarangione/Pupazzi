// Disegno: oggetti della scena con le loro animazioni, telecamera e HUD della partita.
#include "game_int.h"
#include "maps.h"
#include "plat.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

namespace {

enum { MAX_ITEMS = 3072 };
const int HELP_FLASH = 150;    // la capsula ha appena chiamato aiuto: lucina accesa fissa
DrawItem g_items[MAX_ITEMS];
int g_n;
Vec3 g_view;          // centro dell'inquadratura: gli oggetti lontani non si mandano nemmeno al renderer

const float CAM_FOVY = 0.87f;

void push(const Mesh* m, const Xform& xf, uint8_t flash = 0, uint8_t dim = 0, uint32_t flags = 0) {
  if (g_n >= MAX_ITEMS || !m->nt) return;
  DrawItem& it = g_items[g_n++];
  it.mesh = m;
  it.xf = xf;
  it.flags = flags;
  it.flash = flash;
  it.dim = dim;
}

Xform at(float x, float y, float z, float yaw) { return xf_mul(xf_translate(v3(x, y, z)), xf_rot_y(yaw)); }
Xform at(Vec3 p, float yaw) { return at(p.x, p.y, p.z, yaw); }
Xform part(const Xform& root, float x, float y, float z, float pitch) {
  return xf_mul(root, xf_mul(xf_translate(v3(x, y, z)), xf_rot_x(pitch)));
}
Xform scaled(const Xform& x, float sx, float sy, float sz) { return xf_mul(x, xf_scale(sx, sy, sz)); }
Xform ground_at(Vec3 p, float s) { return xf_mul(xf_translate(p), xf_scale(s, 1.0f, s)); }

// Scarto grossolano degli oggetti mobili lontani dall'inquadratura (poi i renderer scartano
// quelli fuori dal campo visivo): un rettangolo attorno al punto inquadrato che contiene il
// terreno visibile con la telecamera di gioco a distanza d (inclinazione 0,98, campo 0,87):
// di lato circa 0,93 d, verso la telecamera 0,45 d, in fondo 0,86 d (a 21 m: 19,5 - 9,2 - 18,1).
bool near_view(Vec3 p, float margin) {
  const float d = fmaxf(g.cam_dist, 17.0f);
  return fabsf(p.x - g_view.x) < 0.95f * d + margin && p.z - g_view.z < 0.55f * d + margin && g_view.z - p.z < d + margin;
}

float ground_under(Vec3 p) { return level_ground(&g.L, p.x, p.z, p.y, PL_STEP); }

// Ombra a disco della misura k (piccola, media, grande, boss) nel tono del terreno della mappa:
// prato, sabbia o pietra, scelto una volta per mappa dal colore medio del pavimento.
const Mesh* shadow(int k) {
  static uint32_t seen = 0;
  static int tone = -1;
  if (g.L.nchunks > 0 && g.L.chunk[0].id != seen) {
    seen = g.L.chunk[0].id;
    float r = 0.0f, gr = 0.0f, bl = 0.0f, n = 0.0f;
    for (int c = 0; c < g.L.nchunks; ++c) {
      const Mesh& m = g.L.chunk[c];
      for (int i = 0; i < m.nt; ++i) {
        const MeshTri& t = m.t[i];
        if (t.n.y < 0.95f || fabsf(m.v[t.a].y) > 0.05f) continue;
        r += (float)t.r; gr += (float)t.g; bl += (float)t.bl; n += 1.0f;
      }
    }
    tone = -1;
    if (n > 0.0f && gr < r * 1.05f) tone = r > bl * 1.35f ? 0 : 1;   // non verde: sabbia (calda) o pietra
  }
  return tone < 0 ? &M.shadow[k] : &M.shadow_tone[tone][k];
}

// --- personaggi -------------------------------------------------------------------------

struct Anim {
  float sq;           // squash & stretch
  float swing;        // camminata (-1..1)
  float recoil;       // attacco col braccio destro (0..1)
  bool air;           // in aria
  bool both;          // tutte e due le braccia avanti (Zolfo che sputa fuoco)
  bool aim;           // mira o raffica in corso: chi ha l'arma nel braccio (Astro) la tiene puntata
  float cheer;        // braccia in alto (vittoria, 0..1)
  float down;         // a terra (0..1)
  float t;            // tempo delle parti che si muovono da sole (fiamma, antenne, orecchio)
  uint8_t flash, dim;
};

// Parte che si muove da sola (CharMesh.fx), attorno al suo perno: la fiamma di Zolfo tremola, le
// antenne di Marzio molleggiano, l'orecchio piegato di Mochi ballonzola.
Xform fx_motion(int ch, const CharMesh& c, const Anim& a) {
  float rz, rx, sy = 1.0f, sxz = 1.0f;
  switch (ch) {
    case CH_ZOLFO: {
      const float f = d_sin(a.t * 2.3f) + 0.5f * d_sin(a.t * 5.3f + 1.0f);
      sy = 1.0f + 0.06f * f;
      sxz = 1.0f - 0.03f * f;
      rz = 0.07f * d_sin(a.t * 1.3f);
      rx = -0.05f * d_sin(a.t * 0.9f + 0.5f) - 0.12f * fabsf(a.swing);
      break;
    }
    case CH_MARZIO:
      rz = 0.1f * d_sin(a.t * 1.9f);
      rx = 0.1f * d_sin(a.t * 1.3f) + 0.22f * a.swing;
      break;
    default:
      rz = 0.16f * d_sin(a.t * 1.4f) + 0.25f * a.swing;
      rx = 0.12f * d_sin(a.t * 0.9f);
      break;
  }
  if (a.air) rx -= 0.35f;
  const Vec3 p = c.fx_at;
  return xf_mul(xf_translate(p), xf_mul(xf_rot_z(rz), xf_mul(xf_rot_x(rx), xf_mul(xf_scale(sxz, sy, sxz), xf_translate(p * -1.0f)))));
}

// Disegna un personaggio; in tip[0] e tip[1] mette dove nascono i colpi delle due braccia (bocca
// del fucilone di Astro o mano), per i lampi.
void draw_char(int ch, int col, Vec3 pos, float yaw, const Anim& a, Vec3* tip = 0) {
  const CharMesh& c = M.ch[ch];
  const float sy = 1.0f + a.sq, sxz = 1.0f - 0.5f * a.sq;
  Xform root;
  if (a.down > 0.0f) {
    root = xf_mul(xf_mul(xf_translate(pos + v3(0, 0.28f * a.down, 0)), xf_rot_y(yaw)), xf_rot_x(-D_HALF_PI * a.down));
  } else {
    root = scaled(at(pos, yaw), sxz, sy, sxz);
  }
  push(&c.body, root, a.flash, a.dim);
  push(&c.accent[col], root, a.flash, a.dim);
  if (c.fx.nt) push(&c.fx, xf_mul(root, fx_motion(ch, c, a)), a.flash, a.dim);
  float arm_r, arm_l, leg_l, leg_r;
  if (a.down > 0.0f) {
    arm_r = -2.4f; arm_l = -2.6f; leg_l = 0.25f; leg_r = -0.2f;
  } else if (a.air) {
    arm_r = arm_l = -2.5f; leg_l = 0.45f; leg_r = -0.3f;
  } else {
    arm_l = a.swing * 0.9f;
    arm_r = c.rest_r - a.swing * c.swing_r;
    leg_l = -a.swing;
    leg_r = a.swing;
  }
  if (a.aim && c.rest_r < 0.0f && a.down <= 0.0f) arm_r = -D_HALF_PI;   // fucilone puntato
  if (a.recoil > 0.0f) arm_r = -D_HALF_PI - 0.5f * a.recoil;
  if (a.both) arm_r = arm_l = -D_HALF_PI;
  if (a.cheer > 0.0f) {
    arm_r = lerpf(arm_r, -2.9f, a.cheer);
    arm_l = lerpf(arm_l, -2.9f, a.cheer);
  }
  const Xform xr = part(root, c.sh_x, c.sh_y, 0.0f, arm_r), xl = part(root, -c.sh_x, c.sh_y, 0.0f, arm_l);
  push(&c.arm_r, xr, a.flash, a.dim);
  push(&c.arm_l, xl, a.flash, a.dim);
  push(&c.leg, part(root, -c.hip_x, c.hip_y, 0.0f, leg_l), a.flash, a.dim);
  push(&c.leg, part(root, c.hip_x, c.hip_y, 0.0f, leg_r), a.flash, a.dim);
  if (tip) {
    tip[0] = xf_point(xr, c.tip);
    tip[1] = xf_point(xl, v3(-c.tip.x, c.tip.y, c.tip.z));
  }
  // Ghiaccio secco che fuma ai piedi di Marzio, scintille che salgono dalla fiamma di Zolfo.
  if (a.down <= 0.0f && (ch == CH_MARZIO || ch == CH_ZOLFO)) {
    for (int k = 0; k < 2; ++k) {
      const float u = a.t * 0.06f + 0.5f * (float)k, f = u - floorf(u);
      if (ch == CH_MARZIO) {
        const Vec3 q = xf_point(root, v3((k ? 0.2f : -0.22f) + 0.08f * f, 0.12f + 0.45f * f, (k ? -0.1f : 0.12f)));
        const float s = 1.3f * (1.0f - f) + 0.3f;
        push(&M.glow[PC_SNOW], scaled(at(q, 0.0f), s, s * 0.8f, s));
      } else {
        const Vec3 q = xf_point(root, v3((k ? 0.12f : -0.1f) + 0.06f * d_sin(f * 9.0f), 1.35f + 0.55f * f, -0.05f));
        const float s = 0.45f * (1.0f - f) + 0.1f;
        push(&M.glow[f < 0.5f ? PC_SULFUR : PC_FIRE], scaled(at(q, 0.0f), s, s, s));
      }
    }
  }
}

void push_aim(int i) {
  const Player& p = g.pl[i];
  const CharDef& cd = CHARS[p.ch];
  const float y = p.pos.y + 0.02f, range = pl_range(p);   // LUNGA GITTATA: la striscia e' lunga quanto il colpo
  auto dashes = [&](float yaw, float from, float to) {
    const Vec3 d = dir_yaw(yaw);
    for (float s = from; s < to - 0.15f; s += 0.6f) push(&M.aim_dash[i], at(p.pos.x + d.x * s, y, p.pos.z + d.z * s, yaw));
  };
  if (cd.aim == AIM_FAN) {
    dashes(p.aim_yaw - cd.aim_w * 0.5f, 0.5f, range);
    dashes(p.aim_yaw + cd.aim_w * 0.5f, 0.5f, range);
  } else {
    dashes(p.aim_yaw, 0.6f, range);
  }
  if (p.charge < pl_special_cost(p)) return;
  // Speciale carico: dove arriva.
  if (cd.aim_sp == AIM_LOB) {
    const float dist = fmaxf(p.ch == CH_MOCHI ? 1.5f : 2.0f, p.aim_k * cd.sp_range);
    const Vec3 t = p.pos + dir_yaw(p.aim_yaw) * dist;
    push(&M.aim_ring[3], xf_mul(xf_translate(v3(t.x, ground_under(v3(t.x, 9.0f, t.z)) , t.z)), xf_scale(cd.sp_w, 1.0f, cd.sp_w)));
  } else if (cd.aim_sp == AIM_AREA) {
    push(&M.aim_ring[3], xf_mul(xf_translate(v3(p.pos.x, y, p.pos.z)), xf_scale(cd.sp_range, 1.0f, cd.sp_range)));
  }
}

void push_player(int i) {
  const Player& p = g.pl[i];
  if (!p.active || (p.out && p.down >= OUT_VANISH)) return;   // senza vite: sparito in uno sbuffo
  Anim a;
  memset(&a, 0, sizeof(a));
  if (!p.alive) {
    a.down = clampf((float)p.down / 12.0f, 0.0f, 1.0f);
    a.dim = 150;
    a.t = (float)g.ui_tick * 0.1f + (float)i * 1.7f;
    draw_char(p.ch, i, p.pos, p.yaw, a);
    return;
  }
  a.t = (float)g.ui_tick * 0.1f + (float)i * 1.7f;
  if (p.hurt > 0) a.flash = 170;
  else if (p.invuln > 0 && ((g.ui_tick >> 2) & 1u)) a.flash = 110;
  a.sq = p.sq + 0.015f * d_sin((float)g.ui_tick * 0.1f + (float)i);
  a.swing = d_sin(p.phase) * p.amp;
  a.air = !p.grounded;
  a.recoil = (float)p.recoil / 8.0f;
  a.both = p.flame_t > 0;
  a.aim = (p.aiming && !p.frozen) || p.burst_left > 0 || p.aim_hold > 0;
  if (g.screen == SC_PLAY && g.phase == PH_MAP_DONE) a.cheer = 1.0f;
  const float bob = p.grounded ? 0.03f * fabsf(a.swing) : 0.0f;
  Vec3 tip[2];
  draw_char(p.ch, i, p.pos + v3(0, bob, 0), p.yaw, a, tip);
  const float gy = ground_under(p.pos);
  push(shadow(0), xf_translate(v3(p.pos.x, gy, p.pos.z)));
  push(&M.ring[i], xf_translate(v3(p.pos.x, gy, p.pos.z)));
  if (p.shield) {
    // SCUDO: una bolla del colore del giocatore che pulsa, cioe' un anello rivolto verso la
    // telecamera attorno al corpo e uno di traverso all'altezza della pancia.
    const float k = 1.1f + 0.06f * d_sin((float)g.ui_tick * 0.16f + (float)i * 1.3f);
    const Vec3 c = v3(p.pos.x, p.pos.y + 0.7f, p.pos.z);
    push(&M.ring_big[i], scaled(xf_mul(xf_translate(c), xf_rot_x(D_HALF_PI - g.cam_pitch)), k, k, k));
    push(&M.ring_big[i], scaled(xf_mul(at(c + v3(0, -0.15f, 0), (float)g.ui_tick * 0.04f), xf_rot_x(0.35f)), k * 1.05f, k, k * 1.05f));
  }
  if (p.aiming && !p.frozen) push_aim(i);
  if (p.blizzard_t > 0) {
    const float r = CHARS[CH_MARZIO].sp_range;
    const float k = p.blizzard_t > 20 ? 1.0f : (float)p.blizzard_t / 20.0f;
    push(&M.snow_ring, xf_mul(at(p.pos.x, gy, p.pos.z, (float)g.tick * 0.05f), xf_scale(r * k, 1.0f, r * k)));
  }
  if (p.leap_n > 0) {
    push(&M.aim_ring[i], xf_mul(xf_translate(v3(p.leap_b.x, p.leap_b.y + 0.02f, p.leap_b.z)), xf_scale(1.0f, 1.0f, 1.0f)));
  }
  // Lampi dei colpi: alla bocca del fucilone per ogni colpo della raffica di Astro (colpi appena
  // partiti), alla mano per le schegge di Mochi e il ghiaccio di Marzio, fiammelle sulle mani di Zolfo.
  switch (p.ch) {
    case CH_ASTRO:
      for (int k = 0; k < MAX_SHOTS; ++k) {
        const Shot& s = g.sh[k];
        if (s.alive && s.owner == i && s.kind == SK_BULLET && s.life <= 2) {
          const float f = s.life <= 1 ? 1.0f : 0.7f;
          push(&M.muzzle, scaled(at(tip[0], s.yaw), f, f, f));
        }
      }
      break;
    case CH_MOCHI:
    case CH_MARZIO:
      if (p.recoil > 4) {
        const float k = 1.4f + 0.3f * (float)(p.recoil - 4);
        push(&M.glow[p.ch == CH_MOCHI ? PC_MOON : PC_SNOW], scaled(at(tip[0], 0.0f), k, k, k));
      }
      break;
    default:
      if (p.flame_t > 0)
        for (int h = 0; h < 2; ++h) {
          const float k = 1.0f + 0.35f * d_sin((float)g.ui_tick * 1.7f + (float)h * 2.0f);
          push(&M.glow[PC_SULFUR], scaled(at(tip[h], 0.0f), k, k, k));
        }
      break;
  }
}

// --- mostri e boss -------------------------------------------------------------------

// Braccio appeso al perno (x, y, z): prima inclinato di lato di roll (attorno a Z, positivo verso +X:
// in fuori per il braccio a destra, verso il corpo per quello a sinistra), poi alzato di pitch.
Xform limb(const Xform& root, float x, float y, float z, float pitch, float roll) {
  return xf_mul(root, xf_mul(xf_translate(v3(x, y, z)), xf_mul(xf_rot_x(pitch), xf_rot_z(roll))));
}
// Rotazione attorno a un punto (coordinate del pezzo).
Xform about(Vec3 p, const Xform& r) { return xf_mul(xf_translate(p), xf_mul(r, xf_translate(p * -1.0f))); }
float smooth01(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// Cima del disegno di un mostro a terra (non l'altezza delle regole, MONS: la coda dello scorpione,
// l'arciere che e' alto quasi come il guerriero).
static float monster_draw_top(const Monster& m) {
  static const float TOP[MO_NTYPES] = { 1.1f, 1.7f, 0.6f, 2.15f };
  return m.pos.y + TOP[m.type];
}

// Braccio a due pezzi lunghi a e b dalla spalla s alla mano h (avvicinata se e' troppo lontana): il
// gomito, dalla parte di hint.
Vec3 elbow_ik(Vec3 s, Vec3* h, float a, float b, Vec3 hint) {
  Vec3 d = *h - s;
  float l = length(d);
  const float lmax = (a + b) * 0.995f;
  if (l > lmax) {
    d = d * (lmax / l);
    l = lmax;
    *h = s + d;
  }
  if (l < 1e-3f) return s + hint * a;
  const Vec3 u = d * (1.0f / l);
  const float x = (a * a - b * b + l * l) / (2.0f * l);
  Vec3 w = hint - u * dot(hint, u);
  w = length(w) > 1e-4f ? normalize(w) : v3(u.y, -u.x, 0.0f);
  return s + u * x + w * sqrtf(fmaxf(0.0f, a * a - x * x));
}
// Pezzo appeso al perno p che pende (lungo -Y) verso q.
Xform hang(Vec3 p, Vec3 q) { return xf_mul(xf_translate(p), xf_align_y(normalize(p - q))); }

// Scorpione: zampette che zampettano a coppie alterne, chele che pizzicano, coda inarcata.
void draw_scorpion(const Monster& m, int k, uint8_t flash, float rise, float sy, float sxz) {
  const float tk = (float)g.tick;
  float yaw = m.yaw, pitch = 0.0f, y = m.pos.y - 1.1f * rise;
  float ph = m.phase * 1.2f, step = 0.3f, lift = 0.4f;
  // Coda: inclinazione della base (0 indietro in piano, pi/2 dritta in su) e piega di ogni giunto.
  float tail0 = 0.95f, bend = 0.62f, sting = 0.62f, sway = 0.08f * d_sin(tk * 0.05f + (float)k * 1.7f);
  // Chele: in fuori (spread), in giu' (droop), dita aperte (open).
  float spread = 0.06f * d_sin(ph), droop = 0.0f, open = 0.1f + 0.1f * d_sin(tk * 0.09f + (float)k * 2.0f);
  bool glow = false, stunned = false;
  if (m.state == 1) {                         // si prepara: coda alta che freme, chele spalancate, "!"
    const float u = smooth01((1.0f - (float)m.state_t / 32.0f) * 3.0f);
    const float tw = 0.07f * d_sin(tk * 1.9f);
    tail0 = lerpf(tail0, 1.3f, u);
    bend = lerpf(bend, 0.42f, u) + tw;
    sting = lerpf(sting, 0.3f, u) - tw;
    spread = 0.5f * u;
    droop = -0.35f * u;
    open = lerpf(open, 0.75f, u);
    pitch = -0.1f * u;
    y += 0.03f * u;
    flash = ((m.state_t >> 2) & 1) ? 120 : flash;
    glow = true;
    push(&M.alert, at(m.pos.x, 1.55f + 0.05f * fabsf(d_sin(tk * 0.35f)), m.pos.z, 0.0f));
  } else if (m.state == 2) {                  // carica: basso, chele avanti, pungiglione puntato avanti
    ph = m.phase * 0.8f;
    step = 0.45f;
    tail0 = 1.4f;
    bend = 0.55f;
    sting = 0.6f;
    spread = -0.12f;
    droop = 0.1f;
    open = 0.3f;
    pitch = 0.07f;
    y -= 0.06f;
    glow = true;
  } else if (m.state == 3 && m.state_t > 24) {   // stordito contro un ostacolo: coda a terra, chele molli
    stunned = true;
    yaw += 0.12f * d_sin(tk * 0.3f);
    tail0 = -0.22f + 0.05f * d_sin(tk * 0.21f);
    bend = 0.07f;
    sting = 0.25f;
    spread = 0.25f;
    droop = 0.3f;
    open = 0.45f;
    y -= 0.04f;
  }
  const Xform root = scaled(xf_mul(at(m.pos.x, y, m.pos.z, yaw), xf_rot_x(pitch)), sxz, sy, sxz);
  push(&M.scorpion_body, root, flash);
  // Zampe: verso l'esterno, le davanti piegate in avanti e le dietro indietro; a coppie alterne
  // (destra 1 con sinistra 2...) si spostano avanti alzandosi e indietro poggiate.
  static const float LEG_A[4] = { 0.8f, 0.28f, -0.22f, -0.7f };
  for (int s = -1; s <= 1; s += 2)
    for (int l = 0; l < 4; ++l) {
      const float off = ((l + (s > 0)) & 1) ? D_PI : 0.0f;
      const float a = LEG_A[l] + step * d_sin(ph + off), up = lift * fmaxf(0.0f, d_cos(ph + off)) * fminf(1.0f, step * 3.0f);
      const Xform hip = xf_translate(v3((float)s * SCORPION_LEG_X, SCORPION_LEG_Y, SCORPION_LEG_Z0 + SCORPION_LEG_DZ * (float)l));
      const Xform r = xf_mul(xf_rot_y(s > 0 ? -a : D_PI + a), xf_rot_z(up * 0.5f));
      push(&M.scorpion_leg, xf_mul(root, xf_mul(hip, r)), flash);
    }
  // Chele e dita mobili.
  for (int s = -1; s <= 1; s += 2) {
    const float sx = (float)s;
    const Xform c = xf_mul(root, xf_mul(xf_translate(v3(sx * SCORPION_CLAW_X, SCORPION_CLAW_Y, SCORPION_CLAW_Z)),
                                        xf_mul(xf_rot_y(sx * spread), xf_rot_x(droop))));
    push(s > 0 ? &M.scorpion_claw_r : &M.scorpion_claw_l, c, flash);
    push(s > 0 ? &M.scorpion_finger_r : &M.scorpion_finger_l,
         xf_mul(c, xf_mul(xf_translate(v3(sx * SCORPION_FINGER_X, SCORPION_FINGER_Y, SCORPION_FINGER_Z)), xf_rot_y(-sx * open))), flash);
  }
  // Coda: segmenti a catena dalla base, ognuno piegato verso il dentro della curva.
  Xform t = xf_mul(root, xf_mul(xf_translate(v3(0, SCORPION_TAIL_Y, SCORPION_TAIL_Z)), xf_mul(xf_rot_y(D_PI + sway), xf_rot_x(-tail0))));
  for (int s = 0; s < SCORPION_SEGS; ++s) {
    push(&M.scorpion_tail[s], t, flash);
    t = xf_mul(t, xf_mul(xf_translate(v3(0, 0, SCORPION_SEG)), xf_rot_x(-(s + 1 < SCORPION_SEGS ? bend : sting))));
  }
  push(&M.scorpion_sting, t, flash);
  const Vec3 tip = xf_point(t, SCORPION_STING_TIP);
  if (glow) {
    const float f = 0.55f + 0.2f * d_sin(tk * 0.8f);
    push(&M.glow[PC_FIRE], scaled(at(tip, 0.0f), f, f, f));
  }
  if (stunned) {
    const Vec3 head = xf_point(root, v3(0, 0.62f, 0.25f));
    for (int q = 0; q < 3; ++q) {
      const float a = tk * 0.2f + (float)q * 2.1f;
      push(&M.stars, at(head.x + 0.3f * d_cos(a), head.y + 0.05f * d_sin(a * 2.0f), head.z + 0.3f * d_sin(a), a * 1.5f));
    }
  }
  if (m.rise == 0) push(shadow(2), at(m.pos.x, 0.0f, m.pos.z, m.yaw));
}

// Arciere di terracotta. Cammina con l'arco nella sinistra; per tirare si inginocchia (ginocchio
// destro a terra), si gira di fianco, alza l'arco verso il cielo e tende la corda fino al mento con
// la freccia incoccata; scoccata, la corda vibra e la mano va indietro, poi abbassa l'arco e si rialza.
void draw_archer(const Monster& m, uint8_t flash, float rise, float sy, float sxz) {
  float kneel = 0.0f, raise = 0.0f, pull = 0.0f, rel = 0.0f;
  if (m.windup > 0) {
    const float u = 1.0f - (float)m.windup / (float)ARCHER_DRAW_FRAMES;
    kneel = smooth01(u / 0.3f);
    raise = smooth01((u - 0.1f) / 0.5f);
    pull = smooth01((u - 0.3f) / 0.55f);
    if (u > 0.3f) flash = ((m.windup >> 2) & 1) ? 90 : flash;
  } else if (m.state == 3) {
    const float r = (float)m.state_t / (float)ARCHER_REST_FRAMES;
    kneel = smooth01(r / 0.55f);
    raise = smooth01((r - 0.3f) / 0.45f);
    rel = smooth01((r - 0.5f) / 0.5f);
  }
  const float sw = d_sin(m.phase) * (1.0f - kneel);      // passo
  const float twist = 1.0f * raise, lean = -0.12f * raise, elev = 0.7f;
  const Xform root = scaled(at(m.pos.x, m.pos.y - 1.7f * rise - 0.3f * kneel, m.pos.z, m.yaw + twist), sxz, sy, sxz);
  const Xform ub = about(v3(0, ARCHER_HIP_Y, 0), xf_rot_x(lean));       // busto (nel sistema di root)
  push(&M.archer_body, xf_mul(root, ub), flash);
  // Gambe: camminando oscillano piegando il ginocchio; in ginocchio la sinistra va avanti con il
  // piede a terra, la destra ha il ginocchio a terra e lo stinco disteso dietro.
  for (int s = -1; s <= 1; s += 2) {
    const float leg = (float)s * sw;
    float thigh = 0.5f * leg, knee = 0.12f + 0.45f * fmaxf(0.0f, -d_cos(m.phase + (s > 0 ? D_PI : 0.0f))) * (1.0f - kneel);
    thigh = lerpf(thigh, s < 0 ? -1.45f : -0.15f, kneel);
    knee = lerpf(knee, s < 0 ? 1.45f : 1.62f, kneel);
    const Xform th = part(root, (float)s * ARCHER_HIP_X, ARCHER_HIP_Y, 0.0f, thigh);
    push(&M.archer_thigh, th, flash);
    push(&M.archer_shin, xf_mul(th, xf_mul(xf_translate(v3(0, -ARCHER_THIGH, 0)), xf_rot_x(knee))), flash);
  }
  // Tutto nel sistema di root (che e' gia' girato di twist). Mirando, la freccia va dal mento (dove
  // la mano destra tende la corda) verso il bersaglio e in alto, e passa per la mano dell'arco: il
  // braccio sinistro punta dove questa linea incontra la sfera della sua lunghezza.
  const float L = ARCHER_ARM - 0.04f;                    // dalla spalla all'impugnatura
  const Vec3 sl = xf_point(ub, v3(-ARCHER_SH_X, ARCHER_SH_Y, 0.02f)), sr = xf_point(ub, v3(ARCHER_SH_X, ARCHER_SH_Y, 0.0f));
  const Vec3 dir = v3(-d_sin(twist) * d_cos(elev), d_sin(elev), d_cos(twist) * d_cos(elev));
  const Vec3 chin = xf_point(ub, v3(0.05f, 1.36f, 0.1f));
  Vec3 aim;
  {
    const Vec3 w = chin - sl;
    const float b = dot(dir, w), disc = b * b - dot(w, w) + L * L;
    aim = chin + dir * (-b + sqrtf(fmaxf(0.0f, disc))) - sl;
  }
  const Vec3 down = v3(0.0f, -d_cos(0.35f * sw), -d_sin(0.35f * sw));   // a riposo: penzola e oscilla
  const Vec3 adir = normalize(down + (normalize(aim) - down) * raise);
  const Vec3 hand_l = sl + adir * L;
  push(&M.archer_arm_l, xf_mul(root, hang(sl, hand_l)), flash);
  // Arco nel pugno: a riposo lungo il braccio, mirando dritto sulla linea della freccia.
  Vec3 bz = normalize(v3(0, 0, 1) + (dir - v3(0, 0, 1)) * raise);
  Vec3 by = normalize(adir * -1.0f + (normalize(v3(0, 1, 0) - dir * dir.y) + adir) * raise);
  by = normalize(by - bz * dot(by, bz));
  Xform bow = xf_axes(cross(by, bz), by, bz);
  bow.p = hand_l;
  push(&M.bow, xf_mul(root, bow), flash);
  // Corda tesa fino al mento (dopo il tiro vibra), freccia incoccata: tre a ventaglio se il tiro e'
  // una raffica (m.hitmask, bit 0, vedi shoot_arrow).
  const float full = -length(chin - hand_l);
  float nz = lerpf(ARCHER_STRING_Z, full, pull);
  if (rel > 0.0f) nz += 0.05f * rel * d_sin((float)g.ui_tick * 2.6f);
  const Vec3 nock = xf_point(bow, v3(0, 0, nz));
  for (int s = -1; s <= 1; s += 2) {
    const Vec3 a = xf_point(bow, v3(0, (float)s * ARCHER_BOW_TIP, ARCHER_STRING_Z)), d = nock - a;
    push(&M.bow_string, xf_mul(root, xf_mul(xf_translate(a), xf_mul(xf_align_y(normalize(d)), xf_scale(1.0f, length(d), 1.0f)))), flash);
  }
  if (pull > 0.05f) {
    const Xform nk = xf_mul(bow, xf_translate(v3(0, 0, nz)));
    const int n = (m.hitmask & 1u) ? 3 : 1;
    for (int a = 0; a < n; ++a)
      push(&M.arrow, xf_mul(root, xf_mul(nk, xf_mul(xf_rot_y(0.16f * (float)(a - n / 2)), xf_translate(v3(0, 0, 0.55f))))), flash);
  }
  // Braccio destro: la mano va alla corda (dopo il tiro resta li' e va un po' indietro), il gomito
  // alto e indietro; camminando penzola. A due pezzi dalla spalla alla mano.
  const Vec3 rest = sr + v3(0.03f, -0.46f, 0.08f + 0.18f * sw);
  const Vec3 grip = xf_point(bow, v3(0.02f, 0.0f, (rel > 0.0f ? full - 0.1f * rel : nz) - 0.02f));
  Vec3 hand = rest + (grip - rest) * smooth01(raise * 1.4f);
  const Vec3 hint = normalize(v3(0.25f, 0.0f, -1.0f) + (v3(0.45f, 0.5f, 0.0f) - dir) * raise);
  const Vec3 el = elbow_ik(sr, &hand, ARCHER_UPPER, ARCHER_FORE, hint);
  push(&M.archer_upper_r, xf_mul(root, hang(sr, el)), flash);
  push(&M.archer_fore_r, xf_mul(root, hang(el, hand)), flash);
  if (m.rise == 0) push(shadow(1), xf_translate(v3(m.pos.x, 0.0f, m.pos.z)));
}

// Corpo del mostro con le sue animazioni.
void draw_monster(const Monster& m, int k) {
  uint8_t flash = m.hurt > 0 ? (uint8_t)(m.hurt >= 4 ? 200 : 70) : 0;
  const float rise = m.rise > 0 ? (float)m.rise / 36.0f : 0.0f;
  if (m.rise > 0) {
    push(&M.mark_rise, xf_translate(v3(m.pos.x, 0.0f, m.pos.z)));
    flash = 0;
  }
  const float sy = 1.0f + m.sq, sxz = 1.0f - 0.5f * m.sq;
  const float tk = (float)g.tick;
  switch (m.type) {
    case MO_SCORPIONE: draw_scorpion(m, k, flash, rise, sy, sxz); break;
    case MO_ARCIERE: draw_archer(m, flash, rise, sy, sxz); break;
    case MO_SCARABEO: {
      const float y = m.rise > 0 ? m.fly * (1.0f - rise) : m.fly;
      const Xform root = scaled(xf_mul(at(m.pos.x, y, m.pos.z, m.yaw), xf_rot_x(0.15f)), sxz, sy, sxz);
      push(&M.scarab_body, root, flash);
      const float w = 0.55f * d_sin(tk * 1.9f + (float)k);
      push(&M.scarab_wing_r, xf_mul(root, xf_rot_z(w)), flash);
      push(&M.scarab_wing_l, xf_mul(root, xf_rot_z(-w)), flash);
      push(shadow(0), xf_mul(xf_translate(v3(m.pos.x, 0.0f, m.pos.z)), xf_scale(0.7f, 1.0f, 0.7f)));
      break;
    }
    default: {
      // Guerriero di terracotta: tiene l'alabarda dritta; per colpire la alza sopra la testa con
      // tutte e due le mani e la cala davanti a se', poi la rialza piano.
      float tall = 1.0f, arm_r = 0.1f + d_sin(m.phase) * 0.12f, arm_l = -d_sin(m.phase) * 0.45f, roll = 0.0f;
      float halb = 0.12f, lean = 0.0f;
      if (m.state == 1) {
        const float k2 = 1.0f - (float)m.state_t / 30.0f;
        flash = ((m.state_t >> 2) & 1) ? 110 : flash;
        if (k2 < 0.8f) {
          const float u = smooth01(k2 / 0.8f);
          tall = 1.0f + 0.07f * u;
          arm_r = arm_l = -2.75f * u;
          roll = 0.72f * u;
          halb = 0.12f - 1.1f * u;
          lean = -0.1f * u;
        } else {
          const float u = (k2 - 0.8f) / 0.2f;
          tall = 1.07f - 0.1f * u;
          arm_r = arm_l = -2.75f + 1.45f * u;
          roll = 0.72f;
          halb = -0.98f + 3.0f * u;
          lean = -0.1f + 0.32f * u;
        }
      } else if (m.state == 3) {
        const float e = smooth01((float)m.state_t / 30.0f * 1.4f);   // 1 subito dopo il colpo, poi 0
        tall = 1.0f - 0.03f * e;
        arm_r = arm_l = -1.3f * e;
        roll = 0.72f * e;
        halb = 0.12f + 1.9f * e;
        lean = 0.22f * e;
      }
      const Xform root = scaled(xf_mul(at(m.pos.x, m.pos.y - 1.8f * rise, m.pos.z, m.yaw), xf_rot_x(lean)), sxz, sy * tall, sxz);
      push(&M.warrior_body, root, flash);
      const Xform ar = limb(xf_identity(), WARRIOR_SH_X, WARRIOR_SH_Y, 0.05f, arm_r, -roll);
      push(&M.warrior_arm_r, xf_mul(root, ar), flash);
      push(&M.warrior_arm_l, limb(root, -WARRIOR_SH_X, WARRIOR_SH_Y, 0.05f, arm_l, roll), flash);
      push(&M.halberd, xf_mul(root, xf_mul(xf_translate(xf_point(ar, v3(0, -WARRIOR_ARM, 0.04f))), xf_rot_x(halb))), flash);
      push(&M.warrior_leg, part(root, -WARRIOR_HIP_X, WARRIOR_HIP_Y, 0.0f, d_sin(m.phase) * 0.6f), flash);
      push(&M.warrior_leg, part(root, WARRIOR_HIP_X, WARRIOR_HIP_Y, 0.0f, -d_sin(m.phase) * 0.6f), flash);
      if (m.rise == 0) push(shadow(2), xf_translate(v3(m.pos.x, 0.0f, m.pos.z)));
      break;
    }
  }
  if (m.slow_t > 0 && m.rise == 0) {
    const float r = monster_r(m) * 1.6f;
    push(&M.snow_ring, xf_mul(at(m.pos.x, MONS[m.type].flying ? 0.02f : 0.04f, m.pos.z, (float)g.tick * 0.1f), xf_scale(r, 1.0f, r)));
  }
  if (m.burn_t > 0 && m.rise == 0) {
    // Carte combo: chi brucia ha una fiammella che tremola sulla testa (oltre alle scintille).
    const float fl = 1.0f + 0.25f * d_sin((float)g.ui_tick * 0.9f + (float)k * 1.7f);
    const float top = MONS[m.type].flying ? m.fly + 0.3f : monster_draw_top(m) - 0.15f;
    push(&M.glow[PC_FIRE], scaled(at(m.pos.x, top, m.pos.z, (float)g.ui_tick * 0.3f), 2.0f * fl, 2.6f * fl, 2.0f * fl));
    push(&M.glow[PC_GOLD], scaled(at(m.pos.x, top + 0.2f * fl, m.pos.z, 0.0f), 1.1f, 1.5f * fl, 1.1f));
  }
}

void push_monster(int k) {
  const Monster& m = g.mo[k];
  if (!m.alive || !near_view(m.pos, 2.0f)) return;
  if (!monster_drawn(m)) return;
  draw_monster(m, k);
}

// Posa del Gladiatore gigante, calcolata solo dal suo stato (camminata, azione in corso, ruggito,
// sconfitta).
struct BossPose {
  float lean, twist, drop;      // busto avanti/indietro (sui piedi), girato, abbassato (m)
  float arm_r, arm_l;           // braccio della spada e dello scudo (come i personaggi: -pi/2 avanti)
  float roll_r, roll_l;         // braccia in fuori (+) o verso il corpo (-); con il braccio avanti
                                // (arm -pi/2) il roll lo porta di lato in orizzontale (il fendente)
  float head;                   // testa: su (-) o giu' (+)
  float wrist;                  // gladio rispetto al braccio (pi/2: lungo il braccio)
  float leg_l, leg_r;
  float shield;                 // scudo girato in avanti, davanti al petto (0..1, la carica)
  bool sword;                   // gladio in mano (altrimenti nel fodero, mentre lancia)
  bool javelin;                 // giavellotto in mano
  bool stars;                   // stordito: stelline attorno all'elmo
};

const float BOSS_ACT_THROW = 30.0f;            // fotogramma "di serie" del lancio dei giavellotti
const float BOSS_REST_R = -0.35f, BOSS_REST_L = -0.8f, BOSS_REST_ROLL_L = -0.3f, BOSS_REST_WRIST = 0.55f;

float span(float t, float t0, float t1) { return smooth01((t - t0) / (t1 - t0)); }

// Tempo dell'azione "di serie": nelle fasi avanzate la preparazione (b.wind fotogrammi) e' piu'
// corta, quindi la posa la percorre piu' in fretta fino al colpo (nom); dopo il colpo va uguale.
float act_time(const Boss& b, float nom) {
  const float t = (float)b.act_t, w = (float)(b.wind > 0 ? b.wind : 1);
  return t < w ? t * nom / w : nom + (t - w);
}

// Posa della corsa con lo scudo davanti (u: quanto e' abbassato, s: passo della corsa).
void rush_pose(BossPose* p, float u, float s, float run) {
  p->drop = 0.42f * u + 0.06f * run * fabsf(s);
  p->lean = 0.3f * u + 0.12f * run;
  p->head = 0.2f * u;
  p->arm_l = lerpf(BOSS_REST_L, -1.45f, u);
  p->roll_l = lerpf(BOSS_REST_ROLL_L, -0.78f, u);
  p->shield = u;
  p->arm_r = lerpf(BOSS_REST_R, 0.35f, u) + 0.15f * run * s;
  p->roll_r = 0.28f * u;
  p->wrist = lerpf(BOSS_REST_WRIST, 1.25f, u);
  p->leg_l = lerpf(-0.3f * u, 0.85f * s, run);
  p->leg_r = lerpf(0.45f * u, -0.85f * s, run);
}

void boss_pose(const Boss& b, BossPose* p) {
  const float br = d_sin((float)g.tick * 0.05f);           // respiro
  p->lean = 0.0f; p->twist = 0.0f; p->drop = 0.03f * br;
  p->arm_r = BOSS_REST_R + 0.05f * br; p->arm_l = BOSS_REST_L; p->roll_r = 0.0f; p->roll_l = BOSS_REST_ROLL_L;
  p->head = 0.0f; p->wrist = BOSS_REST_WRIST; p->leg_l = p->leg_r = 0.0f;
  p->shield = 0.0f;
  p->sword = true; p->javelin = false; p->stars = false;
  if (!b.alive) {
    // Sconfitto: cede sulle ginocchia, crolla in avanti e sprofonda nella polvere.
    const float t = (float)b.dead_t;
    const float kneel = span(t, 0.0f, 20.0f), fall = span(t, 20.0f, 44.0f), sink = span(t, 40.0f, 72.0f);
    p->drop = 0.9f * kneel + 2.2f * sink;
    p->lean = 0.22f * kneel + 1.2f * fall * fall;
    p->leg_l = p->leg_r = -1.1f * kneel;
    p->arm_r = lerpf(BOSS_REST_R, 0.25f, kneel);
    p->arm_l = lerpf(BOSS_REST_L, 0.1f, kneel);
    p->roll_r = 0.3f * kneel;
    p->roll_l = 0.2f * kneel;
    p->head = 0.4f * kneel;
    p->wrist = lerpf(BOSS_REST_WRIST, 1.5f, kneel);
    return;
  }
  if (b.roar > 0) {
    // Ruggito tra una fase e l'altra: braccia larghe in alto, testa all'indietro.
    const float u = span((float)(70 - b.roar), 0.0f, 10.0f) * span((float)b.roar, 0.0f, 10.0f);
    p->arm_r = lerpf(p->arm_r, -2.2f, u);
    p->arm_l = lerpf(p->arm_l, -2.2f, u);
    p->roll_r = 0.8f * u;
    p->roll_l = 0.8f * u;
    p->head = -0.45f * u;
    p->lean = -0.1f * u;
    return;
  }
  switch (b.act) {
    case BA_NONE: {
      // Camminata pesante: gambe e braccia a passo alterno (spada con la gamba sinistra, scudo con
      // la destra), il busto che va su e giu' e si gira un po', la cresta che dondola.
      const float s = d_sin(b.phase), a = clampf(b.amp, 0.0f, 1.2f);
      p->leg_l = 0.6f * s * a;
      p->leg_r = -0.6f * s * a;
      p->arm_r += 0.3f * s * a;
      p->arm_l -= 0.2f * s * a;
      p->roll_l += 0.06f * fabsf(s) * a;            // lo scudo ballonzola
      p->drop += 0.1f * a * fabsf(s);
      p->twist = -0.09f * s * a;
      p->lean = 0.06f * a;
      p->head = 0.05f * a * d_sin(b.phase * 2.0f + 0.6f);
      break;
    }
    case BA_JAVELIN: {
      // Giavellotti: prende un giavellotto dalla faretra, lo carica dietro la testa e lo lancia.
      const float t = act_time(b, BOSS_ACT_THROW);
      p->sword = false;
      const float reach = span(t, 0.0f, 8.0f), cock = span(t, 24.0f, 30.0f), follow = span(t, 30.0f, 35.0f), back = span(t, 35.0f, 44.0f);
      float a = lerpf(BOSS_REST_R, -3.55f, reach);
      a = lerpf(a, -2.3f, cock);
      a = lerpf(a, -1.1f, follow);
      p->arm_r = lerpf(a, BOSS_REST_R, back);
      p->twist = lerpf(-0.35f * reach, 0.25f, cock) * (1.0f - back);
      p->lean = (lerpf(-0.08f * reach, 0.14f, cock)) * (1.0f - back);
      p->head = -0.1f * reach * (1.0f - cock);
      p->arm_l = lerpf(BOSS_REST_L, -1.3f, reach * (1.0f - back));
      p->javelin = t >= 5.0f && t < BOSS_ACT_THROW;
      break;
    }
    case BA_SLAM: {
      // Spaccaterra: spada in alto in un attimo e giu' fino a terra lungo la linea della crepa.
      const float t = act_time(b, 14.0f);
      const float up = span(t, 0.0f, 10.0f), slam = span(t, 10.0f, 16.0f), rec = span(t, 40.0f, 60.0f);
      float a = lerpf(BOSS_REST_R, -3.0f, up);
      a = lerpf(a, -0.5f, slam);
      p->arm_r = lerpf(a, BOSS_REST_R, rec);
      p->roll_r = -0.62f * slam * (1.0f - rec);
      p->wrist = lerpf(BOSS_REST_WRIST, 1.2f, up) * (1.0f - rec) + BOSS_REST_WRIST * rec;
      p->lean = (lerpf(-0.12f * up, 0.36f, slam)) * (1.0f - rec);
      p->drop = (0.32f * slam) * (1.0f - rec);
      p->head = (-0.15f * up + 0.25f * slam) * (1.0f - rec);
      p->twist = 0.12f * slam * (1.0f - rec);
      p->arm_l = lerpf(BOSS_REST_L, -0.4f, up * (1.0f - rec));
      p->roll_l = lerpf(BOSS_REST_ROLL_L, 0.5f, up * (1.0f - rec));
      if (t > 16.0f && t < 40.0f) p->arm_r += 0.03f * d_sin(t * 2.5f);
      break;
    }
    case BA_SUMMON: {
      // Chiama i rinforzi: spada e scudo in alto, testa all'indietro.
      const float t = (float)b.act_t;
      const float u = span(t, 0.0f, 12.0f) * (1.0f - span(t, 30.0f, 40.0f));
      p->arm_r = lerpf(BOSS_REST_R, -2.9f, u);
      p->arm_l = lerpf(BOSS_REST_L, -2.6f, u);
      p->roll_r = 0.35f * u;
      p->roll_l = 0.35f * u;
      p->head = -0.4f * u;
      p->lean = -0.12f * u;
      p->wrist = lerpf(BOSS_REST_WRIST, 1.4f, u);
      break;
    }
    case BA_SWEEP: {
      // Fendente: braccio quasi orizzontale con la spada portata indietro di lato (con il braccio
      // in avanti, roll lo gira in orizzontale; il busto si carica e si abbassa), poi la spadata
      // larga e bassa da destra a sinistra davanti a se', e la ripresa.
      const float t = act_time(b, 33.0f);
      const float load = span(t, 0.0f, 24.0f), cut = span(t, 31.0f, 37.0f), rec = span(t, 46.0f, 71.0f);
      const float k = (1.0f - rec), shake = t > 24.0f && t < 31.0f ? 0.03f * d_sin(t * 2.7f) : 0.0f;
      p->arm_r = lerpf(lerpf(BOSS_REST_R, -1.45f, load), -1.25f, cut) * k + BOSS_REST_R * rec;
      p->roll_r = (lerpf(1.9f * load, -1.15f, cut) + shake) * k;
      p->wrist = lerpf(BOSS_REST_WRIST, 1.45f, load) * k + BOSS_REST_WRIST * rec;
      p->twist = lerpf(0.45f * load, -0.5f, cut) * k;
      p->lean = lerpf(-0.06f * load, 0.24f, cut) * k;
      p->drop += (0.24f * load + 0.14f * cut) * k;
      p->head = lerpf(-0.08f * load, 0.15f, cut) * k;
      p->arm_l = lerpf(BOSS_REST_L, -0.45f, load * k);
      p->roll_l = lerpf(BOSS_REST_ROLL_L, 0.45f, load * k);
      p->leg_l = -0.25f * load * k;
      p->leg_r = 0.2f * load * k;
      break;
    }
    case BA_RUSH: {
      if (b.act_t < b.wind) {
        // Si abbassa dietro lo scudo, una gamba indietro per la spinta che raspa la sabbia.
        const float u = span((float)b.act_t, 0.0f, (float)b.wind * 0.45f);
        rush_pose(p, u, 0.0f, 0.0f);
        p->leg_r += 0.22f * u * d_sin((float)b.act_t * 0.75f);
        p->head = 0.2f * u + 0.04f * d_sin((float)b.act_t * 0.75f);
      } else if (b.run < b.lane) {
        rush_pose(p, 1.0f, d_sin(b.phase), 1.0f);      // corre a testa bassa dietro lo scudo
      } else {
        // Corsa finita senza sbattere: frena puntando i piedi, poi si rialza.
        const float u = 1.0f - span((float)(b.act_t - b.end_t), 6.0f, 28.0f);
        rush_pose(p, u, 0.0f, 0.0f);
        p->lean = lerpf(0.0f, -0.1f, u);
        p->leg_l = -0.35f * u;
        p->leg_r = -0.1f * u;
      }
      break;
    }
    case BA_STUN: {
      // Stordito: le ginocchia cedono, il busto si accascia e ciondola, braccia giu', le stelline.
      const float t = (float)b.act_t, w = (float)b.wind;
      const float u = span(t, 0.0f, 8.0f) * (1.0f - span(t, w - 16.0f, w));
      const float wob = d_sin(t * 0.21f);
      p->drop = 0.3f * u + 0.05f * u * wob;
      p->lean = 0.2f * u;
      p->head = 0.42f * u;
      p->twist = 0.12f * u * wob;
      p->leg_l = -0.55f * u;
      p->leg_r = -0.4f * u;
      p->arm_r = lerpf(p->arm_r, 0.15f, u);
      p->roll_r = 0.15f * u;
      p->wrist = lerpf(BOSS_REST_WRIST, 1.45f, u);
      p->arm_l = lerpf(BOSS_REST_L, 0.05f, u);
      p->roll_l = lerpf(BOSS_REST_ROLL_L, 0.3f, u);
      p->stars = t < w - 10.0f;
      break;
    }
    default: break;
  }
}

void push_boss(void) {
  const Boss& b = g.boss;
  const bool dying = !b.alive && b.dead_t > 0 && b.dead_t < 72;
  if (!b.alive && !dying) return;
  // Colpito: lampo forte solo nei primi fotogrammi (in quattro lo colpiscono di continuo: deve
  // restare dei suoi colori), lampeggia mentre ruggisce.
  uint8_t flash = b.hurt >= 5 ? 70 : (b.hurt > 0 ? 25 : 0);
  if (b.roar > 0 && ((b.roar >> 3) & 1)) flash = 90;
  uint8_t dim = 0;
  if (dying) {
    flash = b.dead_t < 8 ? 200 : 0;
    dim = (uint8_t)clampf((float)(b.dead_t - 8) * 4.0f, 0.0f, 150.0f);
  }
  BossPose p;
  boss_pose(b, &p);
  const float sy = 1.0f + b.sq * 0.5f, sxz = 1.0f - 0.25f * b.sq;
  const Xform base = scaled(xf_mul(at(b.pos + v3(0, -p.drop, 0), b.face), xf_rot_x(p.lean)), sxz, sy, sxz);
  // Gambe (non girano col busto), poi il busto girato attorno ai fianchi con testa, braccia e armi.
  push(&M.boss_leg_l, part(base, -BOSS_HIP_X, BOSS_HIP_Y, 0.0f, p.leg_l), flash, dim);
  push(&M.boss_leg_r, part(base, BOSS_HIP_X, BOSS_HIP_Y, 0.0f, p.leg_r), flash, dim);
  const Xform up = xf_mul(base, about(v3(0, BOSS_HIP_Y, 0), xf_mul(xf_rot_y(p.twist), xf_rot_z(b.sway))));
  push(&M.boss_body, up, flash, dim);
  push(&M.boss_head, xf_mul(up, about(v3(0, BOSS_NECK_Y, 0), xf_mul(xf_rot_y(-0.6f * p.twist), xf_rot_x(p.head)))), flash, dim);
  const Xform ar = limb(xf_identity(), BOSS_SH_X, BOSS_SH_Y, 0.0f, p.arm_r, p.roll_r);
  const Xform al = limb(xf_identity(), -BOSS_SH_X, BOSS_SH_Y, 0.0f, p.arm_l, -p.roll_l);
  push(&M.boss_arm_r, xf_mul(up, ar), flash, dim);
  push(&M.boss_arm_l, xf_mul(up, al), flash, dim);
  const Vec3 hand_r = xf_point(ar, v3(0, -BOSS_ARM, 0.05f)), hand_l = xf_point(al, v3(0, -BOSS_ARM, 0.05f));
  if (p.sword) {
    push(&M.boss_sword, xf_mul(up, xf_mul(ar, xf_mul(xf_translate(v3(0, -BOSS_ARM, 0.05f)), xf_rot_x(p.wrist)))), flash, dim);
  } else {
    push(&M.boss_sword, xf_mul(up, boss_scabbard()), flash, dim);      // nel fodero: si vede l'impugnatura
  }
  if (p.javelin) push(&M.javelin, xf_mul(up, xf_mul(xf_translate(hand_r), xf_rot_x(-0.2f))), flash, dim);   // quasi orizzontale: si vede dall'alto
  // Scudo: un po' girato verso l'esterno, dritto davanti nella carica.
  const float sh_yaw = -0.25f * (1.0f - p.shield), sh_tilt = -0.25f * (p.arm_l - BOSS_REST_L) * (1.0f - p.shield);
  push(&M.boss_shield, xf_mul(up, xf_mul(xf_translate(hand_l), xf_mul(xf_rot_y(sh_yaw), xf_rot_x(sh_tilt)))), flash, dim);
  if (p.stars) {
    // Stordito: quattro stelline grandi che girano attorno all'elmo.
    const Vec3 c = xf_point(up, v3(0, BOSS_NECK_Y + 1.15f, 0.1f));
    for (int q = 0; q < 4; ++q) {
      const float a = (float)b.act_t * 0.16f + (float)q * D_HALF_PI;
      push(&M.stars, scaled(at(c.x + 1.0f * d_cos(a), c.y + 0.15f * d_sin(a * 2.0f), c.z + 1.0f * d_sin(a), a * 1.5f), 4.6f, 4.6f, 4.6f));
    }
  }
  if (b.alive) push(shadow(3), xf_translate(v3(b.pos.x, 0.0f, b.pos.z)));
}

// Mano del gladiatore quando lascia i giavellotti (coordinate del boss): da li' partono quelli disegnati.
Vec3 boss_throw_hand(const Boss& b) {
  Boss t = b;
  t.alive = true;
  t.roar = 0;
  t.act = BA_JAVELIN;
  t.act_t = t.wind = b.act == BA_JAVELIN && b.wind > 0 ? b.wind : (int)BOSS_ACT_THROW;
  BossPose p;
  boss_pose(t, &p);
  const Xform base = xf_mul(at(b.pos + v3(0, -p.drop, 0), b.face), xf_rot_x(p.lean));
  const Xform up = xf_mul(base, about(v3(0, BOSS_HIP_Y, 0), xf_rot_y(p.twist)));
  return xf_point(xf_mul(up, limb(xf_identity(), BOSS_SH_X, BOSS_SH_Y, 0.0f, p.arm_r, p.roll_r)), v3(0, -BOSS_ARM, 0.05f));
}

// --- mondo ------------------------------------------------------------------------------

// Capsula dell'esploratore: chiusa con lui dentro che saltella e la lucina dell'antenna che
// lampeggia; poi aperta, vuota, con un filo di fumo.
void push_capsule(const DynCell& dc, int d, Vec3 c) {
  if (dc.hp > 0.0f) {
    const float hit = (float)g.crate_hit[d] / 10.0f;
    const float wob = 0.05f * hit * d_sin((float)g.crate_hit[d] * 2.0f);
    const Xform cx = at(c, wob);
    push(&M.capsule, cx, hit > 0.0f ? 120 : 0);
    if ((((g.ui_tick + (unsigned)d * 7u) / 18u) & 1u) || g.help_t > HELP_FLASH) push(&M.capsule_light, cx);
    const float t = (float)g.ui_tick * 0.12f + (float)d;
    push(&M.explorer, scaled(at(c.x, c.y + CAPSULE_FLOOR + 0.05f * fabsf(d_sin(t)), c.z + CAPSULE_EXPLORER_Z, 0.3f * d_sin(t * 0.4f)),
                             1.0f, 1.0f + 0.04f * d_sin(t * 2.0f), 1.0f));
  } else {
    push(&M.capsule_open, at(c, 0.0f));
    for (int k = 0; k < 3; ++k) {
      const float u = fmodf((float)g.ui_tick * 0.006f + (float)k / 3.0f, 1.0f);
      const float s = 1.0f + 2.6f * u * (1.0f - u) + 0.6f * u;
      push(&M.smoke, scaled(at(c.x + 0.12f * d_sin(u * 5.0f + (float)k), c.y + 0.55f + 1.4f * u, c.z - 0.05f, 0.0f), s, s, s));
    }
  }
}

void push_world(void) {
  for (int i = 0; i < g.L.nchunks; ++i) push(&g.L.chunk[i], xf_identity(), 0, 0, ITEM_STATIC);
  for (int i = 0; i < g.L.nscenery; ++i) push(&g.L.scenery[i], xf_identity(), 0, 0, ITEM_STATIC);   // paesaggio lontano
  for (int i = 0; i < g.L.ngates; ++i)
    if (g.L.gates[i].open < 1.0f) push(&g.L.gates[i].mesh, xf_translate(v3(0.0f, -1.95f * g.L.gates[i].open, 0.0f)));
  // Cespugli, erba alta, casse e trampolini con le mesh dello stile della mappa (level_mesh.cpp):
  // girati e un po' piu' grandi o piu' piccoli secondo la casella, cosi' non sembrano in fila.
  for (int d = 0; d < g.L.ndyn; ++d) {
    const DynCell& dc = g.L.dyn[d];
    const Vec3 c = cell_center(&g.L, dc.cx, dc.cz);
    if (!near_view(c, 1.0f)) continue;
    uint32_t hh = (uint32_t)dc.cx * 374761393u ^ (uint32_t)dc.cz * 668265263u;
    hh = (hh ^ (hh >> 13)) * 1274126177u;
    hh ^= hh >> 16;
    const float yaw = (float)(hh % 628u) * 0.01f, k = 0.92f + 0.16f * (float)((hh >> 10) % 100u) / 100.0f;
    switch (dc.type) {
      case C_BUSH: {
        const float s = g.squash[d];
        push(&g.L.dyn_mesh[DM_BUSH], scaled(at(c, yaw), k * (1.0f + 0.15f * s), k * (1.0f - 0.55f * s), k * (1.0f + 0.15f * s)));
        break;
      }
      case C_GRASS: {
        const float s = g.squash[d];
        const float w = 0.05f * d_sin((float)g.ui_tick * 0.05f + (float)(dc.cx + dc.cz));
        push(&g.L.dyn_mesh[DM_GRASS], scaled(xf_mul(at(c, yaw), xf_rot_x(w)), 1.0f + 0.1f * s, k * (1.0f - 0.6f * s), 1.0f + 0.1f * s));
        break;
      }
      case C_BREAK: {
        if (dc.capsule) {
          push_capsule(dc, d, c);
          break;
        }
        if (dc.hp <= 0.0f) break;
        bool tal = false;
        for (int it = 0; it < g.L.nitems; ++it) tal |= g.L.items[it].crate == d;
        const float hit = (float)g.crate_hit[d] / 10.0f;
        const float wob = 0.06f * hit * d_sin((float)g.crate_hit[d] * 2.0f);
        const float tilt = 0.004f * (float)((int)((hh >> 4) % 100u) - 50);   // poco: le casse quadrate restano nella casella
        push(&g.L.dyn_mesh[tal ? DM_CRATE_TAL : DM_CRATE], scaled(at(c, tilt + wob), 1.0f + 0.05f * hit, 1.0f - 0.05f * hit, 1.0f + 0.05f * hit),
             hit > 0.0f ? 120 : 0);
        break;
      }
      case C_TRAMP: {
        const float t = (float)g.tramp_t[d] / 20.0f;
        const float s = t > 0.0f ? 0.35f * d_sin(t * D_PI * 2.0f) * t : 0.0f;
        push(&g.L.dyn_mesh[DM_TRAMP], scaled(at(c, yaw), 1.0f - 0.5f * s, 1.0f + s, 1.0f - 0.5f * s));
        break;
      }
      default: break;
    }
  }
  // Talismani e bombe della mappa.
  for (int it = 0; it < g.L.nitems; ++it) {
    if (!g.item_on[it]) continue;
    const MapItem& mi = g.L.items[it];
    const Vec3 c = cell_center(&g.L, mi.cx, mi.cz);
    if (!near_view(c, 1.0f)) continue;
    const float pop = clampf((float)g.item_t[it] / 15.0f, 0.0f, 1.0f);
    if (mi.kind == IT_TALISMAN) {
      const float ph = (float)g.ui_tick * 0.06f + (float)it * 2.0f;
      const float s = pop * (1.0f + 0.25f * (1.0f - pop));
      push(&M.talisman, scaled(at(c.x, mi.y + 0.75f + 0.12f * d_sin(ph) + 0.6f * (1.0f - pop), c.z, (float)g.ui_tick * 0.05f), s, s, s));
      push(&M.talisman_glow, xf_mul(xf_translate(v3(c.x, mi.y, c.z)), xf_scale(1.0f + 0.15f * d_sin(ph * 2.0f), 1.0f, 1.0f + 0.15f * d_sin(ph * 2.0f))));
    } else {
      push(&M.bomb, at(c.x, mi.y, c.z, 0.6f));
      push(shadow(0), xf_translate(v3(c.x, mi.y, c.z)));
    }
  }
  for (int i = 0; i < MAX_BOMBS; ++i) {
    const Bomb& b = g.bo[i];
    if (!b.alive) continue;
    // BOMBE GIGANTI: le bombe lasciate da chi ha la carta si vedono piu' grandi.
    const float big = b.item < 0 && b.owner >= 0 && b.owner < MAX_PLAYERS && has_card(g.pl[b.owner], CA_BOMBE) ? 1.3f : 1.0f;
    const float k = big * (b.fuse < 60 ? 1.0f + 0.18f * fabsf(d_sin((float)b.fuse * 0.5f))
                                       : 1.0f + 0.08f * fabsf(d_sin((float)b.fuse * 0.2f)));
    const uint8_t fl = (b.fuse < 60 ? ((b.fuse >> 2) & 1) : ((b.fuse >> 4) & 1)) ? 160 : 0;
    push(&M.bomb, scaled(at(b.pos, 0.6f), k, k, k), fl);
    push(&M.bomb_spark, at(b.pos + v3(0.09f * k, 0.55f * k, 0.0f), 0.0f));
    push(shadow(0), xf_translate(v3(b.pos.x, ground_under(b.pos), b.pos.z)));
  }
}

// L'esploratore salvato: salta di gioia davanti alla capsula, poi se ne va saltellando.
void push_explorer(void) {
  if (g.expl_t < 0 || !near_view(g.expl_pos, 2.0f)) return;
  const float t = (float)g.expl_t;
  const float hop = g.expl_t < 70 ? 0.45f * fabsf(d_sin(t * 0.16f)) : 0.22f * fabsf(d_sin(t * 0.38f));
  const float shrink = g.expl_t > 200 ? clampf((float)(220 - g.expl_t) / 20.0f, 0.0f, 1.0f) : 1.0f;
  const float sq = g.expl_t < 70 ? 0.08f * d_sin(t * 0.32f) : 0.0f;
  push(&M.explorer, scaled(at(g.expl_pos.x, ground_under(g.expl_pos) + hop, g.expl_pos.z, g.expl_yaw), shrink * (1.0f - 0.5f * sq),
                           shrink * (1.0f + sq), shrink * (1.0f - 0.5f * sq)));
  push(shadow(0), xf_translate(v3(g.expl_pos.x, ground_under(g.expl_pos), g.expl_pos.z)));
  if (g.expl_t < 70 && (g.expl_t / 5) % 2 == 0)
    push(&M.stars, at(g.expl_pos.x, ground_under(g.expl_pos) + hop + 1.05f, g.expl_pos.z, t * 0.2f));
}

// Colpi. I giavellotti appena lanciati dal gladiatore partono dalla mano disegnata: per i primi
// fotogrammi si spostano un po' verso li' (solo nel disegno; il gioco li fa partire da
// boss_javelin_start, launch_javelins). Le frecce degli arcieri partono gia' dall'arco disegnato
// (ARCHER_NOCK_*, shoot_arrow).
void push_shots(void) {
  const Boss& b = g.boss;
  bool hand_ok = false;
  Vec3 hand = v3(0, 0, 0);
  for (int i = 0; i < MAX_SHOTS; ++i) {
    const Shot& s = g.sh[i];
    if (!s.alive || !near_view(s.pos, 1.0f)) continue;
    switch (s.kind) {
      case SK_BULLET: push(&M.bullet, at(s.pos, s.yaw)); break;
      case SK_SHARD: push(&M.shard, xf_mul(at(s.pos, s.yaw), xf_rot_z((float)s.life * 0.35f))); break;
      case SK_FROST:
        push(&M.frost, at(s.pos, (float)s.life * 0.3f));
        push(&M.frost_mist, xf_mul(at(s.pos, -(float)s.life * 0.22f), xf_rot_x(0.35f)));
        break;
      case SK_ARROW: {
        // Freccia dell'arciere: in volo inclinata lungo la parabola; piantata a terra (life <= 0,
        // vedi update_arrow) resta con la punta nel terreno e alla fine affonda.
        const float hv = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        const Xform r = xf_mul(at(s.pos, s.yaw), xf_rot_x(-d_atan2(s.vel.y, hv)));
        if (s.life > 0) {
          push(&M.arrow, r);
          push(shadow(0), xf_mul(xf_translate(v3(s.pos.x, ground_under(v3(s.pos.x, 9.0f, s.pos.z)) + 0.01f, s.pos.z)),
                                 xf_scale(0.55f, 1.0f, 0.55f)));
        } else {
          const float sink = 0.32f + 0.3f * clampf(1.0f + (float)s.life / 8.0f, 0.0f, 1.0f);
          push(&M.arrow, xf_mul(r, xf_translate(v3(0, 0, sink - 0.55f))));
        }
        break;
      }
      case SK_FLAME: {
        const float f = clampf((float)s.life / 26.0f, 0.0f, 1.0f);
        const int col = f < 0.22f ? PC_SULFUR : (f < 0.45f ? PC_GOLD : (f < 0.75f ? PC_FIRE : PC_RED));
        const float k = s.r / 0.12f;
        push(&M.glow[col], scaled(at(s.pos, (float)s.life * 0.4f), k, k, k));
        break;
      }
      case SK_LAVA: push(&M.lava, xf_mul(at(s.pos, s.yaw), xf_rot_x((float)s.life * 0.25f))); break;
      case SK_JAVELIN: {
        // Inclinato lungo la traiettoria, punta in avanti.
        Vec3 p = s.pos;
        if (s.owner == -2 && s.life < 12) {
          if (!hand_ok) { hand = boss_throw_hand(b); hand_ok = true; }
          const float u = 1.0f - (float)s.life / 12.0f;
          p = p + (hand - boss_javelin_start(b)) * (u * u);
        }
        const float hv = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        push(&M.javelin, xf_mul(at(p, s.yaw), xf_rot_x(-d_atan2(s.vel.y, hv))));
        break;
      }
      case SK_ROCKET: {
        const Xform root = xf_mul(at(s.pos, s.yaw), xf_rot_z((float)s.life * 0.3f));
        push(&M.rocket, root);
        const float fl = 1.0f + 0.3f * d_sin((float)s.life * 2.3f);
        push(&M.rocket_fire, xf_mul(root, about(v3(0, 0, -0.26f), xf_scale(1.0f, 1.0f, fl))));
        break;
      }
      default: break;
    }
    if (s.kind == SK_LAVA || s.kind == SK_JAVELIN || s.kind == SK_ROCKET)
      push(shadow(0), xf_mul(xf_translate(v3(s.pos.x, ground_under(v3(s.pos.x, 9.0f, s.pos.z)) + 0.01f, s.pos.z)), xf_scale(0.6f, 1.0f, 0.6f)));
  }
}

void push_markers(void) {
  for (int i = 0; i < MAX_MARKERS; ++i) {
    const Marker& m = g.mk[i];
    if (!m.alive) continue;
    const float f = m.life > 0 ? (float)m.t / (float)m.life : 0.0f;
    switch (m.kind) {
      case MK_LOB:
        push(&M.mark_lob, ground_at(m.pos, m.r));
        push(&M.mark_fill, ground_at(m.pos - v3(0, 0.005f, 0), m.r * f));
        break;
      case MK_ROOT: {
        // Spaccaterra del gladiatore: la crepa corre dalla spada verso fuori (ogni segno si apre a
        // open_at, prima quelli vicini a lui), la riga dentro si accende sempre di piu', poi spuntano
        // le pietre a punta.
        const float yaw = m.yaw;
        if (m.t < m.fire_at) {
          if (m.t < m.open_at) break;
          push(&M.mark_crack, at(m.pos, yaw));
          const int left = m.fire_at - m.t;
          const float w = left < 16 ? 1.3f + 0.7f * fabsf(d_sin((float)m.t * 0.8f)) : 0.7f + 0.6f * (1.0f - (float)left / 70.0f);
          push(&M.mark_crack_glow, scaled(at(m.pos, yaw), w, 1.0f, 1.0f));
        } else {
          const int dt = m.t - m.fire_at, left = m.life - m.t;
          const float h = clampf((float)dt / 4.0f, 0.0f, 1.0f) * clampf((float)left / 8.0f, 0.0f, 1.0f);
          push(&M.mark_crack, at(m.pos, yaw));
          push(&M.rock_spikes, scaled(at(m.pos, yaw + (float)i * 1.3f), 1.0f, h, 1.0f));
        }
        break;
      }
      case MK_SWEEP: {
        // Fendente: il settore davanti al gladiatore si riempie dal centro verso il bordo; quando e'
        // pieno arriva la spadata e per qualche fotogramma resta chiaro dove ha colpito.
        const Xform base = at(m.pos.x, m.pos.y, m.pos.z, m.yaw);
        if (m.t < m.fire_at) {
          const float k = m.fire_at > 0 ? (float)m.t / (float)m.fire_at : 1.0f;
          push(&M.mark_sector, scaled(base, m.r, 1.0f, m.r));
          push(&M.mark_sector_fill, scaled(base, m.r * k, 1.0f, m.r * k));
        } else if (m.t < m.fire_at + 6) {
          push(&M.mark_sector_hit, scaled(base, m.r, 1.0f, m.r));
          // La scia della spada all'altezza della lama, che si allarga e scende mentre svanisce.
          if (m.t < m.fire_at + 4) {
            const float u = (float)(m.t - m.fire_at) / 4.0f, r = m.r * (0.8f + 0.12f * u);
            push(&M.mark_swoosh, scaled(at(m.pos.x, m.pos.y + 1.7f - 0.5f * u, m.pos.z, m.yaw), r, 1.0f, r));
          }
        }
        break;
      }
      case MK_LANE: {
        // Carica: la corsia dal gladiatore in avanti, bordi e frecce, si riempie mentre si prepara e
        // resta finche' corre; dove c'e' un ostacolo, alla fine, il segno dell'urto.
        const Xform base = at(m.pos.x, m.pos.y, m.pos.z, m.yaw);
        const float k = m.fire_at > 0 ? clampf((float)m.t / (float)m.fire_at, 0.0f, 1.0f) : 1.0f;
        push(&M.mark_lane_fill, xf_mul(base, xf_scale(m.r, 1.0f, m.len * k)));
        for (int s = -1; s <= 1; s += 2) push(&M.mark_lane_edge, xf_mul(base, xf_mul(xf_translate(v3((float)s * m.r, 0, 0)), xf_scale(1.0f, 1.0f, m.len))));
        push(&M.mark_lane_end, xf_mul(base, xf_mul(xf_translate(v3(0, 0, m.len)), xf_scale(m.r, 1.0f, 1.0f))));
        // Frecce che scorrono verso la fine (piu' svelte mentre corre).
        const float step = 2.2f, run = fmodf((float)m.t * (m.t < m.fire_at ? 0.04f : 0.12f), 1.0f) * step;
        for (float z = 1.6f + run; z < m.len - 0.6f; z += step) push(&M.mark_chevron, xf_mul(base, xf_mul(xf_translate(v3(0, 0, z)), xf_scale(m.r, 1.0f, 1.0f))));
        if (m.crash) push(&M.mark_impact, xf_mul(base, xf_translate(v3(0, 0, m.len))));
        break;
      }
      case MK_BLAST: {
        if (m.t < 5) {
          const float k = m.r * (0.35f + 0.13f * (float)m.t);
          push(&M.blast, scaled(at(m.pos + v3(0, 0.3f, 0), 0.0f), k, k * 0.8f, k));
        }
        push(&M.mark_blast, ground_at(v3(m.pos.x, 0.0f, m.pos.z) + v3(0, ground_under(m.pos), 0), m.r * (0.4f + 0.6f * f)));
        break;
      }
      default:
        push(&M.aim_ring[0], ground_at(m.pos, m.r));
        break;
    }
  }
}

void push_particles(void) {
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    const Particle& q = g.pa[i];
    if (!q.alive) continue;
    const float f = (float)q.life / (float)(q.life0 > 0 ? q.life0 : 1);
    switch (q.kind) {
      case PK_CUBE: {
        const float k = q.size * (f < 0.3f ? f / 0.3f : 1.0f);
        push(&M.cube[q.col], scaled(xf_mul(at(q.pos, q.spin), xf_rot_x(q.spin * 0.7f)), k, k, k));
        break;
      }
      case PK_LEAF:
        push(&M.cube[q.col], scaled(xf_mul(at(q.pos, q.spin), xf_rot_x(0.4f * d_sin(q.spin))), q.size * 1.2f, q.size * 0.25f, q.size * 0.8f));
        break;
      case PK_SPARK: {
        const float k = 0.5f * q.size;
        push(&M.glow[q.col], scaled(at(q.pos, 0.0f), k, k, k));
        break;
      }
      case PK_FLAME: {
        const int col = f > 0.66f ? PC_GOLD : (f > 0.33f ? PC_FIRE : PC_RED);
        const float k = q.size * (0.4f + 0.8f * f);
        push(&M.glow[col], scaled(at(q.pos, q.spin), k, k, k));
        break;
      }
      case PK_SNOW: {
        const float k = 0.45f * q.size;
        push(&M.glow[PC_SNOW], scaled(at(q.pos, 0.0f), k, k, k));
        break;
      }
      default: {
        const float k = q.size * (0.5f + 0.7f * (1.0f - f)) * (f < 0.25f ? f / 0.25f : 1.0f);
        push(&M.ball[q.col], scaled(at(q.pos, q.spin), k, k, k));
        break;
      }
    }
  }
}

// Menu: i personaggi alla partenza della mappa, rivolti verso la telecamera.
void push_menu_chars(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    int ch = i;
    bool show = true, ready = false;
    if (g.screen == SC_CHARS) {
      show = g.joined[i];
      ch = g.pick[i];
      ready = g.ready[i];
    }
    if (!show) continue;
    const Vec3 base = menu_spot(i);
    Anim a;
    memset(&a, 0, sizeof(a));
    const float t = (float)g.ui_tick * 0.08f + (float)i * 1.7f;
    a.t = (float)g.ui_tick * 0.1f + (float)i * 1.7f;
    float hop = 0.0f;
    if (ready) {
      hop = 0.35f * fabsf(d_sin(t * 1.2f));
      a.cheer = 1.0f;
    } else if (g.screen == SC_TITLE && ((g.ui_tick / 90u + (unsigned)i) % 4u) == 0u) {
      hop = 0.3f * fabsf(d_sin((float)(g.ui_tick % 90u) / 90.0f * D_PI));
      a.air = hop > 0.05f;
    }
    a.sq = 0.03f * d_sin(t * 1.5f);
    a.swing = 0.15f * d_sin(t);
    draw_char(ch, i, base + v3(0, hop, 0), D_PI + 0.25f * d_sin(t * 0.3f), a);
    push(shadow(0), xf_translate(base));
    if (g.screen == SC_CHARS) push(&M.ring_big[i], xf_translate(base));
  }
}

void push_stats_chars(void) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    Anim a;
    memset(&a, 0, sizeof(a));
    const float t = (float)g.ui_tick * 0.1f + (float)i * 1.3f;
    a.t = t;
    a.cheer = 1.0f;
    const float hop = (i == g.mvp ? 0.45f : 0.25f) * fabsf(d_sin(t));
    a.sq = 0.06f * d_sin(t * 2.0f);
    const Vec3 base = v3(p.pos.x, ground_under(p.pos), p.pos.z);
    draw_char(p.ch, i, base + v3(0, hop, 0), 0.0f, a);
    push(shadow(0), xf_translate(base));
    push(i == g.mvp ? &M.ring_big[i] : &M.ring[i], xf_translate(base));
  }
  if (g.explorer_free) {
    float n = 0.0f, mx = -1e30f;
    Vec3 s = v3(0, 0, 0);
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (g.pl[i].active) { s = s + g.pl[i].pos; n += 1.0f; mx = fmaxf(mx, g.pl[i].pos.x); }
    if (n > 0.0f) {
      const Vec3 base = v3(mx + 1.1f, 0.0f, s.z / n + 0.3f);
      const float t = (float)g.ui_tick * 0.13f;
      const Vec3 pos = v3(base.x, ground_under(v3(base.x, s.y / n + 0.5f, base.z)), base.z);
      push(&M.explorer, at(pos + v3(0, 0.3f * fabsf(d_sin(t)), 0), 0.25f * d_sin(t * 0.5f)));
      push(shadow(0), xf_translate(pos));
    }
  }
}

// --- HUD (0.5.2: stile Orbita, in pixel a 640x480) ---------------------------------------
// Dentro il margine dei televisori: 24 pixel ai lati, 16 sopra e sotto.

const float HX = 24.0f, HY = 16.0f;

bool blink(unsigned period) { return ((g.ui_tick / period) & 1u) != 0; }
inline uint32_t O(uint32_t rgb) { return HUD_OPAQUE(rgb); }
inline uint32_t A(uint32_t rgb, float a) { return hud_a(rgb, a); }

// Colore a meta' strada tra a e b (t da 0 ad a, 1 a b).
uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int s = 0; s <= 16; s += 8) {
    const float ca = (float)((a >> s) & 255u), cb = (float)((b >> s) & 255u);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  }
  return out;
}

// Testo sopra il gioco: con l'ombra scura, per staccarsi da qualunque sfondo.
void stext(Hud* h, float x, float cy, int face, uint32_t rgb, const char* s, float track = 0.0f, int align = -1) {
  const float o = hud_face_size(face) >= 25 ? 2.0f : 1.0f;
  ui::text(h, x + o, cy + o, face, A(ui::SCRIM, 0.85f), s, track, align);
  ui::text(h, x, cy, face, O(rgb), s, track, align);
}

// Punto del mondo -> pixel a 640x480.
bool to_px(const Camera& cam, Vec3 p, float* x, float* y) {
  int sx, sy;
  if (!to_screen(cam, p, &sx, &sy)) return false;
  *x = (float)sx * 2.0f;
  *y = (float)sy * 2.0f;
  return true;
}

void player_bars(Hud* h, const Camera& cam, int i) {
  const Player& p = g.pl[i];
  const CharDef& cd = CHARS[p.ch];
  float sx, sy;
  if (!to_px(cam, v3(p.pos.x, p.pos.y + M.ch[p.ch].top + 0.15f, p.pos.z), &sx, &sy)) return;
  // Barre sottili sopra la testa: vita, colpo (tacche, o carburante per Zolfo), speciale.
  const float W = 48.0f, x0 = sx - W * 0.5f, y0 = sy - 22.0f;
  hud_box(h, x0 - 2.0f, y0 - 2.0f, W + 4.0f, 17.0f, 3.0f, A(ui::SCRIM, 0.8f));
  hud_box(h, x0 - 19.0f, y0 - 2.0f, 15.0f, 17.0f, 3.0f, O(ui::PLAYER[i]));
  const char num[2] = { (char)('1' + i), 0 };
  ui::text(h, x0 - 11.5f, y0 + 6.5f, HF_B11, O(i == 3 ? 0x06102E : 0xFFFFFF), num, 0.0f, 0);
  const float hw = clampf(p.hp / p.hp_max, 0.0f, 1.0f) * W;
  hud_rect(h, x0, y0, hw, 6.0f, p.hurt > 0 ? 0xFFFFFF : (p.hp < p.hp_max * 0.3f && blink(8) ? 0xFF5A4A : 0x5CE07A));
  hud_rect(h, x0 + hw, y0, W - hw, 6.0f, 0x2E1E3A);
  const uint32_t ac = p.locked ? 0xC04A3A : ui::L1;
  if (cd.ammo_max <= 10.0f) {
    const int n = (int)cd.ammo_max;
    const float gap = 1.0f, seg = (W - gap * (float)(n - 1)) / (float)n;
    for (int k = 0; k < n; ++k) {
      const float fill = clampf(p.ammo - (float)k, 0.0f, 1.0f);
      const float x = x0 + (float)k * (seg + gap);
      hud_rect(h, x, y0 + 7.0f, seg, 3.0f, ui::OFF);
      if (fill > 0.0f) hud_rect(h, x, y0 + 7.0f, seg * fill, 3.0f, fill >= 1.0f ? ac : 0x9A6A3A);
    }
  } else {
    hud_rect(h, x0, y0 + 7.0f, W, 3.0f, ui::OFF);
    hud_rect(h, x0, y0 + 7.0f, clampf(p.ammo / cd.ammo_max, 0.0f, 1.0f) * W, 3.0f, ac);
  }
  const float cost = pl_special_cost(p);   // SUPER CARICA: la barra si riempie prima
  const bool ready = p.charge >= cost;
  hud_rect(h, x0, y0 + 11.0f, clampf(p.charge / cost, 0.0f, 1.0f) * W, 2.0f, ready && blink(6) ? 0xFFFFFF : ui::STAR);
  if (ready) {
    hud_box(h, x0 + W + 4.0f, y0 - 2.0f, 21.0f, 17.0f, 3.0f, O(blink(6) ? 0xFFFFFF : ui::STAR));
    ui::text(h, x0 + W + 14.5f, y0 + 6.5f, HF_B10, O(ui::ON), "L1", 0.0f, 0);
  }
  for (int k = 0; k < p.bombs; ++k) {
    hud_box(h, x0 + 2.0f + (float)k * 9.0f, y0 - 11.0f, 7.0f, 7.0f, 2.0f, O(0x1E1C24));
    hud_rect(h, x0 + 5.0f + (float)k * 9.0f, y0 - 12.5f, 2.0f, 2.0f, 0xFF8A2A);
  }
  if (p.frozen) stext(h, sx, y0 - 22.0f, HF_B12, ui::A2, tr("COMANDI FERMI"), 1.0f, 0);
}

// Schede dei giocatori in alto a sinistra: la faccia dell'eroe nella cornice del colore del
// giocatore e sotto i cuori delle vite. A terra la faccia si spegne e una barra rossa si svuota nei
// 5 secondi del rientro; senza vite la faccia e' grigia con una X. Ci sono anche da soli.
// Restituisce dove finiscono a destra.
const float CARD = 40.0f, CARD_STEP = 46.0f;

float player_icons(Hud* h) {
  float x = HX;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    const bool down = !p.alive && !p.out, out = p.out;
    const uint32_t c = out ? 0x6A6470 : (down ? mix_rgb(ui::PLAYER[i], 0x101418, 0.45f) : ui::PLAYER[i]);
    hud_box_grad(h, x, HY, CARD, CARD, 7.0f, O(c), O(mix_rgb(c, 0x101418, 0.4f)), 135.0f);
    hud_box(h, x, HY, CARD, CARD, 7.0f, ui::SOLID, 2.0f);
    ui::face(h, x + 5.0f, HY + 5.0f, 30, p.ch, out ? 2 : (down ? 1 : 0));
    if (out) {
      hud_icon(h, HI_CROSS30, x + 6.0f, HY + 6.0f, O(0x101418));
      hud_icon(h, HI_CROSS30, x + 5.0f, HY + 5.0f, O(0xFF4A3A));
    } else if (down) {
      // Barra rossa del rientro: piena quando cade, vuota quando rientra (ferma se si rifa' la zona).
      const float k = clampf((float)p.respawn / (float)RESPAWN_FRAMES, 0.0f, 1.0f);
      hud_rect(h, x + 4.0f, HY + CARD - 10.0f, CARD - 8.0f, 6.0f, 0x101418);
      hud_rect(h, x + 5.0f, HY + CARD - 9.0f, (CARD - 10.0f) * k, 4.0f, 0xFF2E2E);
    }
    // Vite: cuori pieni per quelle rimaste, vuoti per quelle perse (3, o 4 con VITA EXTRA).
    int total = START_LIVES + (has_card(p, CA_VITA) ? 1 : 0);
    if (p.lives > total) total = p.lives;
    const int shown = total < 5 ? total : 5;
    const float step = total > 3 ? 10.0f : 13.0f;
    const float hx = x + CARD * 0.5f - step * (float)(shown - 1) * 0.5f - 6.0f, hy = HY + CARD + 3.0f;
    for (int k = 0; k < shown; ++k) {
      const bool full = k < p.lives;
      const float cx = hx + step * (float)k;
      hud_icon(h, HI_HEART, cx + 1.0f, hy + 1.0f, A(ui::SCRIM, 0.85f));
      hud_icon(h, HI_HEART, cx, hy, O(full ? (out ? 0x9AA2AA : 0xFF4A5A) : 0x2E2238));
      if (!full) hud_icon(h, HI_HEART_EMPTY, cx, hy, O(0x8A7E96));
    }
    x += CARD_STEP;
  }
  return x - (CARD_STEP - CARD);
}

// Riga in alto, a destra delle schede: pianeta e mappa, giro, zona, mostri che mancano; nell'arena
// la vita del boss al posto della zona.
void top_line(Hud* h, float x) {
  char buf[128];
  const float cy = HY + 14.0f;
  const int w = g.L.world >= 0 && g.L.world < NWORLDS ? g.L.world : 0;
  if (maps_is_boss(g.map_idx)) snprintf(buf, sizeof(buf), tr("%s · BOSS"), tr(WORLD_NAMES[w]));
  else snprintf(buf, sizeof(buf), "%s %d", tr(WORLD_NAMES[w]), map_in_world(g.map_idx) + 1);
  stext(h, x, cy, HF_B13, ui::L1, buf, 1.5f);
  x += hud_text_w(HF_B13, buf, 1.5f) + 10.0f;
  snprintf(buf, sizeof(buf), tr("GIRO %d"), g.loop + 1);
  hud_frame(h, x + 1.0f, cy - 8.5f, hud_text_w(HF_B11, buf, 1.5f) + 15.0f, 19.0f, 0, 1.5f, A(ui::SCRIM, 0.6f));
  x += ui::tag(h, x, cy, buf, ui::FTX, HF_B11, 1.5f, 6.0f, 19.0f) + 12.0f;
  float xr = 640.0f - HX;
  if (g.phase == PH_CLEAR) {
    const char* go = tr("VAI AVANTI");
    const float tw = hud_text_w(HF_B13, go, 1.5f);
    if (blink(20)) {
      stext(h, xr - 14.0f, cy, HF_B13, ui::L1, go, 1.5f, 1);
      ui::chev(h, xr - 8.0f, cy - 6.0f, true, ui::L1);
    }
    xr -= tw + 26.0f;
  } else if (g.phase == PH_FIGHT && !g.boss.alive) {
    snprintf(buf, sizeof(buf), tr("MOSTRI %d"), g.enemies_left);
    stext(h, xr, cy, HF_B13, ui::A2, buf, 1.5f, 1);
    xr -= hud_text_w(HF_B13, buf, 1.5f) + 12.0f;
  } else if (g.boss.alive && count_monsters() > 0) {
    snprintf(buf, sizeof(buf), "+%d", count_monsters());
    stext(h, xr, cy, HF_B13, ui::A2, buf, 1.0f, 1);
    xr -= hud_text_w(HF_B13, buf, 1.0f) + 10.0f;
  }
  if (g.boss.alive || (g.L.boss != BOSS_NONE && g.phase != PH_MAP_DONE)) {
    const Boss& b = g.boss;
    const float x0 = x, W = fmaxf(xr - x0, 80.0f), y0 = cy - 7.0f;
    hud_box(h, x0 - 2.0f, y0 - 2.0f, W + 4.0f, 18.0f, 5.0f, A(ui::SCRIM, 0.85f));
    const float k = clampf(b.hp / (b.hp_max > 0.0f ? b.hp_max : 1.0f), 0.0f, 1.0f), fw = floorf(k * W + 0.5f);
    if (fw > 0.0f) {
      if (b.hurt > 0) hud_box(h, x0, y0, fw, 14.0f, 4.0f, 0xFFFFFFFFu);
      else hud_box_grad(h, x0, y0, fw, 14.0f, 4.0f, O(ui::L1), O(ui::L2), 90.0f);
    }
    hud_rect(h, x0 + floorf(W / 3.0f), y0, 2.0f, 14.0f, 0x18081E);
    hud_rect(h, x0 + floorf(2.0f * W / 3.0f), y0, 2.0f, 14.0f, 0x18081E);
  } else {
    snprintf(buf, sizeof(buf), tr("ZONA %d/%d"), g.zone + 1, g.L.nzones);
    stext(h, x, cy, HF_B13, ui::TX, buf, 1.5f);
  }
}

// Messaggio grande al centro, su una fascia scura che sfuma ai lati.
void center_msg(Hud* h) {
  const float cy = 140.0f, has2 = g.msg2[0] ? 1.0f : 0.0f;
  const float bh = 54.0f + 26.0f * has2, by = cy - 30.0f;
  hud_box_grad(h, 0, by, 320, bh, 0, A(ui::SCRIM, 0.0f), A(ui::SCRIM, 0.6f), 90.0f);
  hud_box_grad(h, 320, by, 320, bh, 0, A(ui::SCRIM, 0.6f), A(ui::SCRIM, 0.0f), 90.0f);
  const float base = hud_mid(HF_T32, cy);
  char big[192], sub[256];
  ui::fit(big, sizeof(big), g.msg, HF_T32, 3.0f, 600.0f, "...");   // i nomi delle mappe personalizzate
  hud_text(h, 320, base + 2.0f, HF_T32, O(ui::TDROP), big, 3.0f, 0);
  hud_text_glow(h, 320, base, HF_T32, ui::TGLOW, big, 3.0f, 0);
  hud_text(h, 320, base, HF_T32, O(g.msg_rgb), big, 3.0f, 0);
  if (g.msg2[0]) stext(h, 320, cy + 32.0f, HF_S15, ui::TX, ui::fit(sub, sizeof(sub), g.msg2, HF_S15, 0.5f, 600.0f), 0.5f, 0);
}

void build_play_hud(Hud* h, const Camera& cam) {
  char buf[64];
  // Sopra i mostri colpiti: quanta vita resta.
  for (int k = 0; k < MAX_MONSTERS; ++k) {
    const Monster& m = g.mo[k];
    if (!m.alive || m.rise > 0 || m.shown <= 0 || !monster_drawn(m)) continue;
    float sx, sy;
    const float top = MONS[m.type].flying ? m.fly + 0.45f : monster_draw_top(m) + 0.1f;
    if (!to_px(cam, v3(m.pos.x, top, m.pos.z), &sx, &sy)) continue;
    const float W = m.type == MO_GUERRIERO ? 40.0f : 24.0f, w = clampf(m.hp / m.hp_max, 0.0f, 1.0f) * W;
    hud_rect(h, sx - W * 0.5f - 1.0f, sy - 5.0f, W + 2.0f, 5.0f, 0x18081E);
    hud_rect(h, sx - W * 0.5f, sy - 4.0f, w, 3.0f, 0xFF5040);
  }
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = g.pl[i];
    if (!p.active) continue;
    if (p.alive) {
      player_bars(h, cam, i);
    } else if (p.respawn > 0 && g.phase != PH_WIPE) {
      float sx, sy;
      if (to_px(cam, v3(p.pos.x, p.pos.y + 1.0f, p.pos.z), &sx, &sy)) {
        snprintf(buf, sizeof(buf), "%d", (p.respawn + 59) / 60);
        stext(h, sx, sy - 24.0f, HF_T32, ui::PLAYER[i], buf, 0.0f, 0);
      }
    }
  }
  for (int i = 0; i < MAX_FLOATS; ++i) {
    const FloatText& f = g.ft[i];
    if (!f.alive) continue;
    float sx, sy;
    if (to_px(cam, f.pos, &sx, &sy)) stext(h, sx, sy + 8.0f, HF_B13, f.rgb, tr(f.txt), 1.0f, 0);   // numeri: restano come sono
  }

  // In alto: le schede dei giocatori a sinistra, poi la riga con mappa, giro, zona e mostri.
  int n = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) n += g.pl[i].active;
  // Sotto la pausa e le carte no: si scontrerebbe con i titoli (e il gioco e' fermo).
  if (!g.paused && !g.cards_open) {
    ui::scrim_top(h, 96.0f);
    const float right = n > 0 ? player_icons(h) : HX - 12.0f;
    top_line(h, right + 12.0f);
  }

  // Messaggi grandi al centro (non sotto le carte: si vedono dopo, il gioco e' fermo).
  if (g.msg_t > 0 && !g.paused && !g.cards_open) center_msg(h);
  bool waiting = false, joining = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    waiting |= !g.pl[i].active && !g.pl[i].joining;
    joining |= g.pl[i].joining;
  }
  if (!g.paused && !g.cards_open && !joining && waiting && g.zone == 0 && g.tick < 900 && g.map_idx == 0) {
    const char* msg = tr("altri giocatori: premete un tasto");
    const float cy = 480.0f - HY - 11.0f;
    float ww = 4.0f + hud_text_w(HF_S13, msg, 1.0f);
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active) ww += 30.0f;
    ui::scrim_bottom(h, 70.0f);
    float x = 320.0f - ww * 0.5f;
    for (int i = 0; i < MAX_PLAYERS; ++i)
      if (!g.pl[i].active) x += ui::gp(h, x, cy, i) + 6.0f;
    stext(h, x + 4.0f, cy, HF_S13, ui::FTX, msg, 1.0f);
  }
  if (g.cards_open) cards_hud(h);      // scelta delle carte al posto di pausa e finestre per entrare
  else play_menus_hud(h);
}

}  // namespace

// --- funzioni condivise ----------------------------------------------------------------

// Punto del mondo -> griglia virtuale 320x240 dell'HUD.
bool to_screen(const Camera& c, Vec3 p, int* sx, int* sy) {
  const float* m = c.vp.m;
  const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
  const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
  const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
  if (w < 0.1f) return false;
  const float fx = (x / w * 0.5f + 0.5f) * 320.0f, fy = (0.5f - y / w * 0.5f) * 240.0f;
  if (fx < -40.0f || fx > 360.0f || fy < -40.0f || fy > 280.0f) return false;
  *sx = (int)fx;
  *sy = (int)fy;
  return true;
}

void game_build_frame(FrameDesc* fd, int w, int h, Hud* hud) {
  g_n = 0;
  g_view = g.cam_target;
  if (g.level_ok) {
    push_world();
    if (g.screen == SC_PLAY) {
      for (int i = 0; i < MAX_PLAYERS; ++i) push_player(i);
      for (int k = 0; k < MAX_MONSTERS; ++k) push_monster(k);
      push_explorer();
      push_boss();
      push_shots();
      push_markers();
    } else if (g.screen == SC_STATS) {
      push_stats_chars();
    } else {
      push_menu_chars();
    }
    push_particles();
  }
  Vec3 target = g.cam_target;
  if (g.shake > 0.0f) {
    const float t = (float)g.ui_tick;
    target = target + v3(d_sin(t * 1.7f) * g.shake, d_sin(t * 2.3f + 1.0f) * g.shake * 0.6f, d_sin(t * 1.3f + 2.0f) * g.shake * 0.5f);
  }
  const Vec3 eye = target + v3(0.0f, d_sin(g.cam_pitch), g.cam_back ? -d_cos(g.cam_pitch) : d_cos(g.cam_pitch)) * g.cam_dist;
  camera_setup(&fd->cam, eye, target, CAM_FOVY, 4.0f / 3.0f, 0.5f, 120.0f);
  fd->w = w;
  fd->h = h;
  fd->items = g_items;
  fd->nitems = g_n;
  fd->light = normalize(v3(0.35f, 0.85f, 0.40f));
  fd->sky_top = 0x6FB8F0;
  fd->sky_bot = 0xDCF0FF;
  fd->hud = hud;
  fd->hud_scale = h / 240 > 0 ? h / 240 : 1;
  if (!hud) return;
  hud_reset(hud);
  if (g.screen == SC_PLAY) build_play_hud(hud, fd->cam);
  else menu_build_hud(hud, fd->cam);
  // Caratteri che il gioco non sa disegnare (si vedono come '?'; nomi di mappe personalizzate, o
  // testi cambiati senza rigenerare i caratteri): una volta nel registro.
  static bool missing_logged = false;
  const int miss = hud_missing(true);
  if (miss > 0 && !missing_logged) {
    missing_logged = true;
    plat_log("testo con caratteri che il gioco non sa disegnare (%d, lingua %s): si vedono come ?", miss, save_lang_id(lang_get()));
  }
}
