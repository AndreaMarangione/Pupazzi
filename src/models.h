// Modelli in stile cartone: forme arrotondate, colori pieni, contorno scuro.
// Vanno costruiti una volta sola all'avvio (il renderer GPU li riconosce dall'identificativo).
#pragma once
#include "game.h"

#define NSETS 5        // colori dei 4 giocatori + grigio

// Colori delle particelle (0..3 = colori dei giocatori). Dalla 0.5: sputo (bianco verdastro),
// corazza dello scorpione (ambra, dalla 0.5.1), argilla dei guerrieri e degli arcieri, scarabeo,
// lana (bianco panna), sabbia, pietra, bronzo, cristalli di luna, energia del fucilone, zolfo.
enum { PC_WHITE = 4, PC_GOLD, PC_GREEN, PC_GREY, PC_WOOD, PC_STRAW, PC_SNOW, PC_FIRE, PC_DARK, PC_LEAF,
       PC_RED, PC_EARTH, PC_SPIT, PC_SCORPION, PC_CLAY, PC_SCARAB,
       PC_WOOL, PC_SAND, PC_STONE, PC_BRONZE, PC_MOON, PC_ENERGY, PC_SULFUR, NPCOL };

// Personaggio: il corpo ha l'origine ai piedi e guarda verso +Z; braccia e gambe sono appese ai
// loro perni (si estendono verso -Y) e si animano ruotandole.
struct CharMesh {
  Mesh body, arm_l, arm_r, leg, accent[4];
  Mesh fx;               // parte che si muove da sola: fiamma di Zolfo, antenne di Marzio, orecchio di Mochi
  Vec3 fx_at;            // suo perno (coordinate del corpo)
  float sh_x, sh_y;      // spalle in (+-sh_x, sh_y)
  float hip_x, hip_y;    // anche in (+-hip_x, hip_y)
  float arm_len;         // mano a -arm_len dal perno
  float top;             // sopra la testa
  float rest_r, swing_r; // braccio destro: inclinazione a riposo e quanto oscilla camminando (fucilone di Astro)
  Vec3 tip;              // dove nasce il lampo del colpo (bocca del fucilone o mano), nel sistema del braccio destro
};

// Perni dei pezzi dei mostri e del boss (coordinate del corpo, origine ai piedi, davanti +Z).
// Scorpione: 4 zampe per lato in (+-SCORPION_LEG_X, SCORPION_LEG_Y, SCORPION_LEG_Z0 + k * SCORPION_LEG_DZ),
// la mesh della zampa va verso +X (a sinistra si gira di mezzo giro); chele sulle spalle
// (+-SCORPION_CLAW_X, ...), con il dito mobile sul perno (+-SCORPION_FINGER_X, ...) del sistema
// della chela; coda: SCORPION_SEGS segmenti lunghi SCORPION_SEG lungo +Z del loro sistema, a catena
// dalla base, poi il pungiglione (punta in SCORPION_STING_TIP del suo sistema).
const float SCORPION_LEG_X = 0.15f, SCORPION_LEG_Y = 0.27f, SCORPION_LEG_Z0 = 0.17f, SCORPION_LEG_DZ = -0.135f;
const float SCORPION_CLAW_X = 0.16f, SCORPION_CLAW_Y = 0.31f, SCORPION_CLAW_Z = 0.30f;
const float SCORPION_FINGER_X = 0.085f, SCORPION_FINGER_Y = 0.07f, SCORPION_FINGER_Z = 0.5f;
const float SCORPION_TAIL_Y = 0.38f, SCORPION_TAIL_Z = -0.58f, SCORPION_SEG = 0.17f;
const int SCORPION_SEGS = 4;
const Vec3 SCORPION_STING_TIP = { 0.0f, 0.13f, 0.3f };
// Arciere di terracotta: braccio dell'arco (sinistro, un pezzo) e braccio che tende la corda
// (destro: braccio e avambraccio), gambe a due pezzi (coscia e stinco) per inginocchiarsi.
// L'arco ha l'impugnatura nell'origine, le punte verso +-Y e la corda dietro (verso -Z) a
// ARCHER_STRING_Z; si tiene con la mano sinistra (a -ARCHER_ARM lungo il braccio).
const float ARCHER_SH_X = 0.26f, ARCHER_SH_Y = 1.2f, ARCHER_ARM = 0.5f, ARCHER_UPPER = 0.25f, ARCHER_FORE = 0.25f;
const float ARCHER_HIP_X = 0.11f, ARCHER_HIP_Y = 0.7f, ARCHER_THIGH = 0.33f;   // anche (e ginocchio a -ARCHER_THIGH)
const float ARCHER_STRING_Z = -0.14f, ARCHER_BOW_TIP = 0.58f;
// Da dove parte la freccia (coordinate dell'arciere inginocchiato: il centro della freccia incoccata
// a piena tensione, accanto alla mano dell'arco).
const float ARCHER_NOCK_Y = 1.3f, ARCHER_NOCK_Z = 0.25f;
// Fotogrammi per inginocchiarsi e tendere l'arco, e dopo il tiro per rialzarsi (regole e disegno).
const int ARCHER_DRAW_FRAMES = 36, ARCHER_REST_FRAMES = 24;
const float WARRIOR_SH_X = 0.5f, WARRIOR_SH_Y = 1.36f, WARRIOR_ARM = 0.62f, WARRIOR_HIP_X = 0.2f, WARRIOR_HIP_Y = 0.62f;
const float BOSS_SH_X = 1.02f, BOSS_SH_Y = 3.3f, BOSS_ARM = 1.45f, BOSS_HIP_X = 0.42f, BOSS_HIP_Y = 1.5f;
const float BOSS_NECK_Y = 3.55f;
const float BOSS_SWEEP_HALF = 1.22f;   // fendente del gladiatore: meta' dell'apertura (140 gradi), segno a terra e regole
// Rotazione che porta gli assi locali X, Y, Z su x, y, z (ortonormali, destrorsi).
static inline Xform xf_axes(Vec3 x, Vec3 y, Vec3 z) {
  Xform r = xf_identity();
  r.r[0] = x.x; r.r[1] = y.x; r.r[2] = z.x;
  r.r[3] = x.y; r.r[4] = y.y; r.r[5] = z.y;
  r.r[6] = x.z; r.r[7] = y.z; r.r[8] = z.z;
  return r;
}
// Fodero al fianco sinistro del gladiatore (coordinate del boss), nel sistema del gladio (lama
// verso +Z): li' sta il gladio mentre lancia i giavellotti.
static inline Xform boss_scabbard(void) {
  const Vec3 z = normalize(v3(-0.22f, -1.0f, -0.3f));
  const Vec3 y = normalize(v3(0, 0, 1) - z * z.z);
  Xform t = xf_axes(cross(y, z), y, z);
  t.p = v3(-0.8f, 2.25f, 0.28f);
  return t;
}

// Capsula dell'esploratore: occupa una casella (origine a terra al centro); l'esploratore sta sul
// pavimento alto CAPSULE_FLOOR, spostato di CAPSULE_EXPLORER_Z verso il fondo.
const float CAPSULE_FLOOR = 0.22f, CAPSULE_EXPLORER_Z = -0.02f;
const float CAPSULE_LIGHT_Y = 1.3f;    // lucina in cima all'antenna (lampeggia)
struct Models {
  CharMesh ch[NCHARS];
  // Mostri della Terra.
  Mesh scorpion_body, scorpion_leg, scorpion_claw_l, scorpion_claw_r, scorpion_finger_l, scorpion_finger_r;   // scorpione
  Mesh scorpion_tail[SCORPION_SEGS], scorpion_sting;             // ...coda a segmenti e pungiglione
  Mesh archer_body, archer_arm_l, archer_upper_r, archer_fore_r, archer_thigh, archer_shin;   // arciere di terracotta
  Mesh bow, bow_string;                                          // ...arco e mezza corda (da una punta alla mano)
  Mesh scarab_body, scarab_wing_l, scarab_wing_r;                // scarabeo
  Mesh warrior_body, warrior_arm_l, warrior_arm_r, warrior_leg, halberd;   // guerriero di terracotta
  // Boss: il Gladiatore gigante. Gladio e scudo hanno l'impugnatura nell'origine.
  Mesh boss_body, boss_head, boss_arm_l, boss_arm_r, boss_leg_l, boss_leg_r, boss_sword, boss_shield;
  // Colpi (guardano verso +Z).
  Mesh bullet, rocket, rocket_fire, shard, frost, frost_mist, lava, arrow, javelin;
  Mesh talisman, talisman_glow, bomb, bomb_spark;
  Mesh shadow[4];        // piccola, media, grande, boss (sul prato)
  Mesh shadow_tone[2][4];  // le stesse ombre su sabbia e su pietra (il disegno sceglie dal terreno della mappa)
  Mesh ring[4], ring_big[4];
  Mesh aim_dash[4], aim_ring[4];
  Mesh mark_lob, mark_fill, mark_crack, mark_crack_glow, rock_spikes, mark_rise, mark_blast;
  // Segni del gladiatore (0.5.1). Settore del fendente: raggio 1, verso +Z (bordo, riempimento, lampo
  // del colpo). Corsia della carica verso +Z: riempimento largo 2 e lungo 1 e frecce da scalare, bordi
  // lunghi 1 (si scala solo la lunghezza), fondo largo 2, segno d'urto dove sbatte.
  Mesh mark_sector, mark_sector_fill, mark_sector_hit, mark_swoosh;   // mark_swoosh: scia bianca della spada (arco)
  Mesh mark_lane_fill, mark_lane_edge, mark_lane_end, mark_chevron, mark_impact;
  Mesh blast, muzzle, alert, snow_ring, stars, smoke;   // (cespugli, erba alta, casse, trampolini: Level::dyn_mesh)
  Mesh capsule, capsule_open;  // capsula dell'esploratore, chiusa e aperta
  Mesh capsule_light;          // lucina dell'antenna (si disegna a intermittenza)
  Mesh explorer;               // esploratore della capsula, braccia in alto; origine ai piedi
  Mesh cube[NPCOL], ball[NPCOL], glow[NPCOL];
};

extern Models M;
extern const uint32_t PLAYER_RGB[NSETS];
extern const uint32_t PARTICLE_RGB[NPCOL];

void models_build(void);
void models_free(void);
uint32_t rgb_mix(uint32_t a, uint32_t b, float t);
