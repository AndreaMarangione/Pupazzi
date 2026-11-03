// Measures, timings and colors that the rules share with the drawings: the colors of the players,
// of the power-ups and of the particles, where an arrow starts and how long an archer takes to draw
// the bow, where the gladiator's feet are and how wide its sweep is. The game uses them for its
// rules and effects; the models (game/view/models.h) are built on the same numbers.
#pragma once
#include <stdint.h>

#define NSETS 5        // colors of the 4 players + gray
extern const uint32_t PLAYER_RGB[NSETS];
extern const uint32_t PW_RGB[4];       // power-up colors: light blue (VELOCITA'), red (FURIA), gold (INVINCIBILE), purple (CARICA)

// Particle colors (0..3 = player colors). Since 0.5: spit (greenish white), scorpion armor (amber,
// since 0.5.1), clay of the warriors and of the archers, scarab, wool (cream white), sand, stone,
// bronze, moon crystals, shotgun energy, sulfur.
enum { PC_WHITE = 4, PC_GOLD, PC_GREEN, PC_GREY, PC_WOOD, PC_STRAW, PC_SNOW, PC_FIRE, PC_DARK, PC_LEAF,
       PC_RED, PC_EARTH, PC_SPIT, PC_SCORPION, PC_CLAY, PC_SCARAB,
       PC_WOOL, PC_SAND, PC_STONE, PC_BRONZE, PC_MOON, PC_ENERGY, PC_SULFUR, NPCOL };

// Terracotta archer. Where the arrow starts from (coordinates of the kneeling archer: the center
// of the nocked arrow at full draw, next to the bow hand).
const float ARCHER_NOCK_Y = 1.3f, ARCHER_NOCK_Z = 0.25f;
// Frames to kneel and draw the bow, and after the shot to stand up again (rules and drawing).
const int ARCHER_DRAW_FRAMES = 36, ARCHER_REST_FRAMES = 24;

// Giant gladiator: hips (the feet stamp the ground there) and the sweep, half of its span (140
// degrees): ground mark and rules.
const float BOSS_HIP_X = 0.42f;
const float BOSS_SWEEP_HALF = 1.22f;
