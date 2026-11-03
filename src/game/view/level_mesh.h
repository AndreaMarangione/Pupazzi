// Geometry of a map (level_mesh.cpp), in the style of the map: ground, walls and fixed obstacles
// in chunks of 8x8 cells, the distant scenery, the meshes of the dynamic objects and the gate
// leaves. Owned by the view, which builds it again when the game loads another map.
#pragma once
#include "game/sim/level.h"
#include "engine/mesh.h"

struct LevelMeshes {
  int nchunks;
  Mesh chunk[LV_MAX_CHUNKS];           // ground, walls, fixed obstacles, nearby scenery (blocks of 8x8 cells)
  int nscenery;
  Mesh scenery[LV_MAX_SCENERY];        // distant scenery: first the large shapes, from scenery_ground the ground beyond the border
  int scenery_ground;                  // (0.5.6) with low details only the ground is drawn
  Mesh dyn[DM_COUNT];                  // bush, tall grass, crate, crate with the talisman, trampoline
  Mesh gate_leaf[LV_MAX_GATES][2];     // closed leaves of each gate, with the hinge at the origin
};

// Builds the geometry of map L in out (freeing first what was there).
void level_build_mesh(const Level& L, LevelMeshes* out);
void level_meshes_free(LevelMeshes* m);
