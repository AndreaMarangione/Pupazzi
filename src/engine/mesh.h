// Triangle mesh with per-face color and normal (lighting computed per face, cartoon style).
#pragma once
#include "engine/vmath.h"

enum { TRI_UNLIT = 1 };          // full color, no lighting (outlines, shadows, marks on the ground)

struct MeshTri {
  uint16_t a, b, c;      // vertex indices, counterclockwise seen from outside
  uint8_t r, g, bl;      // base color
  uint8_t flags;         // TRI_*
  Vec3 n;                // unit normal in local space
};

struct Mesh {
  Vec3* v; int nv;
  MeshTri* t; int nt;
  Vec3 center; float radius;   // sphere that contains the mesh
  uint32_t id;                 // different for every mesh built (that is how the GPU renderer recognizes it)
};

struct MeshBuilder {
  Vec3* v; int nv, capv;
  MeshTri* t; int nt, capt;
  uint8_t flags;               // flags given to the triangles added from here on
};

void mb_init(MeshBuilder* b);
int  mb_vert(MeshBuilder* b, Vec3 p);
void mb_tri(MeshBuilder* b, int i0, int i1, int i2, uint32_t rgb);
void mb_quad(MeshBuilder* b, int i0, int i1, int i2, int i3, uint32_t rgb);
void mb_box(MeshBuilder* b, Vec3 mn, Vec3 mx, uint32_t rgb);
Mesh mb_finish(MeshBuilder* b);
void mesh_free(Mesh* m);
int mesh_overflow_count(void);   // vertices rejected because beyond the limit of 65535 (must stay 0)

// --- rounded shapes and utilities (shapes in mesh.cpp) ------------------------
// Ellipsoid centered at c with semi-axes r; seg_u wedges around Y, seg_v bands from the pole.
void mb_ellipsoid(MeshBuilder* b, Vec3 c, Vec3 r, int seg_u, int seg_v, uint32_t rgb);
// Vertical cylinder (Y axis) from y0 to y1, radius r0 at the bottom and r1 at the top (r1 = 0: cone).
void mb_cylinder(MeshBuilder* b, Vec3 base, float r0, float r1, float h, int seg, uint32_t rgb);
// Cuboid with the edges beveled by "bev".
void mb_bevel_box(MeshBuilder* b, Vec3 mn, Vec3 mx, float bev, uint32_t rgb);
// Horizontal disc and ring (facing up) at height y.
void mb_disc(MeshBuilder* b, Vec3 c, float r, int seg, uint32_t rgb);
void mb_ring(MeshBuilder* b, Vec3 c, float r0, float r1, int seg, uint32_t rgb);
// Applies a transform to the vertices from first_v on and recomputes the triangle normals from first_t.
void mb_apply(MeshBuilder* b, int first_v, int first_t, const Xform& xf);
// Cartoon outline: adds an inverted "hull", inflated by t along the vertex normals, for the
// triangles from first_t on. Seen from outside, only the dark border remains.
void mb_outline(MeshBuilder* b, int first_t, float t, uint32_t rgb);
