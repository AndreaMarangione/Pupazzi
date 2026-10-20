// Mesh a triangoli con colore e normale per faccia (luce calcolata per faccia, stile cartone).
#pragma once
#include "vmath.h"

enum { TRI_UNLIT = 1 };          // colore pieno, senza luce (contorni, ombre, segni a terra)

struct MeshTri {
  uint16_t a, b, c;      // indici dei vertici, antiorari visti da fuori
  uint8_t r, g, bl;      // colore di base
  uint8_t flags;         // TRI_*
  Vec3 n;                // normale unitaria in spazio locale
};

struct Mesh {
  Vec3* v; int nv;
  MeshTri* t; int nt;
  Vec3 center; float radius;   // sfera che contiene la mesh
  uint32_t id;                 // diverso per ogni mesh costruita (il renderer GPU la riconosce cosi')
};

struct MeshBuilder {
  Vec3* v; int nv, capv;
  MeshTri* t; int nt, capt;
  uint8_t flags;               // flag dati ai triangoli aggiunti da qui in poi
};

void mb_init(MeshBuilder* b);
int  mb_vert(MeshBuilder* b, Vec3 p);
void mb_tri(MeshBuilder* b, int i0, int i1, int i2, uint32_t rgb);
void mb_quad(MeshBuilder* b, int i0, int i1, int i2, int i3, uint32_t rgb);
void mb_box(MeshBuilder* b, Vec3 mn, Vec3 mx, uint32_t rgb);
Mesh mb_finish(MeshBuilder* b);
void mesh_free(Mesh* m);
int mesh_overflow_count(void);   // vertici rifiutati perche' oltre il limite di 65535 (deve restare 0)

// --- forme arrotondate e utilita' (shapes in mesh.cpp) ------------------------
// Ellissoide centrato in c con semiassi r; seg_u spicchi attorno a Y, seg_v fasce dal polo.
void mb_ellipsoid(MeshBuilder* b, Vec3 c, Vec3 r, int seg_u, int seg_v, uint32_t rgb);
// Cilindro verticale (asse Y) da y0 a y1, raggio in basso r0 e in alto r1 (r1 = 0: cono).
void mb_cylinder(MeshBuilder* b, Vec3 base, float r0, float r1, float h, int seg, uint32_t rgb);
// Parallelepipedo con gli spigoli smussati di "bev".
void mb_bevel_box(MeshBuilder* b, Vec3 mn, Vec3 mx, float bev, uint32_t rgb);
// Disco e anello orizzontali (rivolti verso l'alto) alla quota y.
void mb_disc(MeshBuilder* b, Vec3 c, float r, int seg, uint32_t rgb);
void mb_ring(MeshBuilder* b, Vec3 c, float r0, float r1, int seg, uint32_t rgb);
// Applica una trasformazione ai vertici da first_v in poi e ricalcola le normali dei triangoli da first_t.
void mb_apply(MeshBuilder* b, int first_v, int first_t, const Xform& xf);
// Contorno da cartone: aggiunge uno "scafo" rovesciato, gonfiato di t lungo le normali dei
// vertici, per i triangoli da first_t in poi. Visto da fuori resta solo il bordo scuro.
void mb_outline(MeshBuilder* b, int first_t, float t, uint32_t rgb);
