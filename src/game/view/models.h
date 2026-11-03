// Cartoon-style models: rounded shapes, solid colors, dark outline.
// They must be built only once at start-up (the GPU renderer recognizes them by their id).
#pragma once
#include "game/game.h"

#define NSETS 5        // colors of the 4 players + gray

// Particle colors (0..3 = player colors). Since 0.5: spit (greenish white), scorpion armor (amber,
// since 0.5.1), clay of the warriors and of the archers, scarab, wool (cream white), sand, stone,
// bronze, moon crystals, shotgun energy, sulfur.
enum { PC_WHITE = 4, PC_GOLD, PC_GREEN, PC_GREY, PC_WOOD, PC_STRAW, PC_SNOW, PC_FIRE, PC_DARK, PC_LEAF,
       PC_RED, PC_EARTH, PC_SPIT, PC_SCORPION, PC_CLAY, PC_SCARAB,
       PC_WOOL, PC_SAND, PC_STONE, PC_BRONZE, PC_MOON, PC_ENERGY, PC_SULFUR, NPCOL };

// Character: the body has its origin at the feet and faces +Z; arms and legs hang from their
// pivots (they extend towards -Y) and are animated by rotating them.
struct CharMesh {
  Mesh body, arm_l, arm_r, leg, accent[4];
  Mesh fx;               // part that moves by itself: Zolfo's flame, Marzio's antennae, Mochi's ear
  Vec3 fx_at;            // its pivot (body coordinates)
  float sh_x, sh_y;      // shoulders at (+-sh_x, sh_y)
  float hip_x, hip_y;    // hips at (+-hip_x, hip_y)
  float arm_len;         // hand at -arm_len from the pivot
  float top;             // above the head
  float rest_r, swing_r; // right arm: tilt at rest and how much it swings when walking (Astro's shotgun)
  Vec3 tip;              // where the shot's flash starts (shotgun muzzle or hand), in the right arm's coordinate system
};

// Pivots of the monsters' and the boss's parts (body coordinates, origin at the feet, front +Z).
// Scorpion: 4 legs per side at (+-SCORPION_LEG_X, SCORPION_LEG_Y, SCORPION_LEG_Z0 + k * SCORPION_LEG_DZ),
// the leg mesh points towards +X (on the left it is turned by half a turn); claws on the shoulders
// (+-SCORPION_CLAW_X, ...), with the movable finger on the pivot (+-SCORPION_FINGER_X, ...) of the
// claw's coordinate system; tail: SCORPION_SEGS segments SCORPION_SEG long along +Z of their own
// coordinate system, chained from the base, then the sting (tip at SCORPION_STING_TIP of its own
// coordinate system).
const float SCORPION_LEG_X = 0.15f, SCORPION_LEG_Y = 0.27f, SCORPION_LEG_Z0 = 0.17f, SCORPION_LEG_DZ = -0.135f;
const float SCORPION_CLAW_X = 0.16f, SCORPION_CLAW_Y = 0.31f, SCORPION_CLAW_Z = 0.30f;
const float SCORPION_FINGER_X = 0.085f, SCORPION_FINGER_Y = 0.07f, SCORPION_FINGER_Z = 0.5f;
const float SCORPION_TAIL_Y = 0.38f, SCORPION_TAIL_Z = -0.58f, SCORPION_SEG = 0.17f;
const int SCORPION_SEGS = 4;
const Vec3 SCORPION_STING_TIP = { 0.0f, 0.13f, 0.3f };
// Terracotta archer: bow arm (left, one piece) and arm that draws the string (right: upper arm and
// forearm), two-piece legs (thigh and shin) to kneel.
// The bow has the grip at the origin, the tips towards +-Y and the string behind (towards -Z) at
// ARCHER_STRING_Z; it is held with the left hand (at -ARCHER_ARM along the arm).
const float ARCHER_SH_X = 0.26f, ARCHER_SH_Y = 1.2f, ARCHER_ARM = 0.5f, ARCHER_UPPER = 0.25f, ARCHER_FORE = 0.25f;
const float ARCHER_HIP_X = 0.11f, ARCHER_HIP_Y = 0.7f, ARCHER_THIGH = 0.33f;   // hips (and knee at -ARCHER_THIGH)
const float ARCHER_STRING_Z = -0.14f, ARCHER_BOW_TIP = 0.58f;
// Where the arrow starts from (coordinates of the kneeling archer: the center of the nocked arrow
// at full draw, next to the bow hand).
const float ARCHER_NOCK_Y = 1.3f, ARCHER_NOCK_Z = 0.25f;
// Frames to kneel and draw the bow, and after the shot to stand up again (rules and drawing).
const int ARCHER_DRAW_FRAMES = 36, ARCHER_REST_FRAMES = 24;
const float WARRIOR_SH_X = 0.5f, WARRIOR_SH_Y = 1.36f, WARRIOR_ARM = 0.62f, WARRIOR_HIP_X = 0.2f, WARRIOR_HIP_Y = 0.62f;
const float BOSS_SH_X = 1.02f, BOSS_SH_Y = 3.3f, BOSS_ARM = 1.45f, BOSS_HIP_X = 0.42f, BOSS_HIP_Y = 1.5f;
const float BOSS_NECK_Y = 3.55f;
const float BOSS_SWEEP_HALF = 1.22f;   // gladiator's sweep: half of the span (140 degrees), ground mark and rules
// Rotation that takes the local axes X, Y, Z onto x, y, z (orthonormal, right-handed).
static inline Xform xf_axes(Vec3 x, Vec3 y, Vec3 z) {
  Xform r = xf_identity();
  r.r[0] = x.x; r.r[1] = y.x; r.r[2] = z.x;
  r.r[3] = x.y; r.r[4] = y.y; r.r[5] = z.y;
  r.r[6] = x.z; r.r[7] = y.z; r.r[8] = z.z;
  return r;
}
// Scabbard at the gladiator's left hip (boss coordinates), in the gladius's coordinate system
// (blade towards +Z): the gladius sits there while he throws the javelins.
static inline Xform boss_scabbard(void) {
  const Vec3 z = normalize(v3(-0.22f, -1.0f, -0.3f));
  const Vec3 y = normalize(v3(0, 0, 1) - z * z.z);
  Xform t = xf_axes(cross(y, z), y, z);
  t.p = v3(-0.8f, 2.25f, 0.28f);
  return t;
}

// Explorer's capsule: it takes up one cell (origin on the ground at the center); the explorer
// stands on the floor, CAPSULE_FLOOR high, moved by CAPSULE_EXPLORER_Z towards the back.
const float CAPSULE_FLOOR = 0.22f, CAPSULE_EXPLORER_Z = -0.02f;
const float CAPSULE_LIGHT_Y = 1.3f;    // small light on top of the antenna (blinks)
struct Models {
  CharMesh ch[NCHARS];
  // Monsters of Earth.
  Mesh scorpion_body, scorpion_leg, scorpion_claw_l, scorpion_claw_r, scorpion_finger_l, scorpion_finger_r;   // scorpion
  Mesh scorpion_tail[SCORPION_SEGS], scorpion_sting;             // ...segmented tail and sting
  Mesh archer_body, archer_arm_l, archer_upper_r, archer_fore_r, archer_thigh, archer_shin;   // terracotta archer
  Mesh bow, bow_string;                                          // ...bow and half string (from one tip to the hand)
  Mesh scarab_body, scarab_wing_l, scarab_wing_r;                // scarab
  Mesh warrior_body, warrior_arm_l, warrior_arm_r, warrior_leg, halberd;   // terracotta warrior
  // Boss: the giant Gladiator. Gladius and shield have the grip at the origin.
  Mesh boss_body, boss_head, boss_arm_l, boss_arm_r, boss_leg_l, boss_leg_r, boss_sword, boss_shield;
  // Shots (facing +Z).
  Mesh bullet, rocket, rocket_fire, shard, frost, frost_mist, lava, arrow, javelin;
  Mesh talisman, talisman_glow, bomb, bomb_spark;
  // Crate power-ups (0.5.4, PW_* in game_int.h): spinning coin with the power-up's picture on both
  // faces (center at the origin, faces towards +-Z) and ring on the ground (from 0.62 to 0.74 m: it
  // is scaled).
  Mesh pw_coin[4], pw_ring[4];
  Mesh shadow[4];        // small, medium, large, boss (on grass)
  Mesh shadow_tone[2][4];  // the same shadows on sand and on stone (the drawing chooses based on the map's ground)
  Mesh ring[4], ring_big[4];
  Mesh aim_dash[4], aim_ring[4];
  Mesh mark_lob, mark_fill, mark_crack, mark_crack_glow, rock_spikes, mark_rise, mark_blast;
  // Gladiator's marks (0.5.1). Sweep sector: radius 1, towards +Z (edge, fill, flash of the hit).
  // Charge lane towards +Z: fill 2 wide and 1 long and arrows to be scaled, edges 1 long (only the
  // length is scaled), end 2 wide, impact mark where he crashes.
  Mesh mark_sector, mark_sector_fill, mark_sector_hit, mark_swoosh;   // mark_swoosh: white trail of the sword (arc)
  Mesh mark_lane_fill, mark_lane_edge, mark_lane_end, mark_chevron, mark_impact;
  Mesh blast, muzzle, alert, snow_ring, stars, smoke;   // (bushes, tall grass, crates, trampolines: Level::dyn_mesh)
  Mesh capsule, capsule_open;  // explorer's capsule, closed and open
  Mesh capsule_light;          // antenna light (drawn intermittently)
  Mesh explorer;               // the capsule's explorer, arms up; origin at the feet
  Mesh cube[NPCOL], ball[NPCOL], glow[NPCOL];
};

extern Models M;
extern const uint32_t PLAYER_RGB[NSETS];
extern const uint32_t PARTICLE_RGB[NPCOL];
extern const uint32_t PW_RGB[4];       // power-up colors: light blue (VELOCITA'), red (FURIA), gold (INVINCIBILE), purple (CARICA)

void models_build(void);
void models_free(void);
uint32_t rgb_mix(uint32_t a, uint32_t b, float t);
