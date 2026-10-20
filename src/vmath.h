// Vettori, trasformazioni rigide e matrici 4x4.
// Convenzioni: sistema destrorso, Y verso l'alto; Mat4 in ordine per colonne (come OpenGL).
#pragma once
#include "dmath.h"

struct Vec3 { float x, y, z; };

static inline Vec3 v3(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static inline Vec3 operator+(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 operator-(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 operator*(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(Vec3 a, Vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float length(Vec3 a) { return sqrtf(dot(a, a)); }
static inline Vec3 normalize(Vec3 a) {
  const float l = length(a);
  return l > 1e-20f ? a * (1.0f / l) : v3(0, 0, 0);
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Trasformazione rigida: p_mondo = R * p_locale + p. R e' 3x3 per righe.
struct Xform { float r[9]; Vec3 p; };

static inline Xform xf_identity(void) {
  Xform x;
  x.r[0] = 1; x.r[1] = 0; x.r[2] = 0;
  x.r[3] = 0; x.r[4] = 1; x.r[5] = 0;
  x.r[6] = 0; x.r[7] = 0; x.r[8] = 1;
  x.p = v3(0, 0, 0);
  return x;
}
static inline Xform xf_translate(Vec3 p) { Xform x = xf_identity(); x.p = p; return x; }
// Rotazione attorno a Y: porta (0,0,1) in (sin a, 0, cos a).
static inline Xform xf_rot_y(float a) {
  const float s = d_sin(a), c = d_cos(a);
  Xform x = xf_identity();
  x.r[0] = c; x.r[2] = s; x.r[6] = -s; x.r[8] = c;
  return x;
}
// Rotazione attorno a X: porta (0,1,0) in (0, cos a, sin a).
static inline Xform xf_rot_x(float a) {
  const float s = d_sin(a), c = d_cos(a);
  Xform x = xf_identity();
  x.r[4] = c; x.r[5] = -s; x.r[7] = s; x.r[8] = c;
  return x;
}
static inline Vec3 xf_dir(const Xform& x, Vec3 v) {
  return v3(x.r[0] * v.x + x.r[1] * v.y + x.r[2] * v.z,
            x.r[3] * v.x + x.r[4] * v.y + x.r[5] * v.z,
            x.r[6] * v.x + x.r[7] * v.y + x.r[8] * v.z);
}
static inline Vec3 xf_point(const Xform& x, Vec3 v) { return xf_dir(x, v) + x.p; }
static inline Vec3 xf_inv_dir(const Xform& x, Vec3 v) {
  return v3(x.r[0] * v.x + x.r[3] * v.y + x.r[6] * v.z,
            x.r[1] * v.x + x.r[4] * v.y + x.r[7] * v.z,
            x.r[2] * v.x + x.r[5] * v.y + x.r[8] * v.z);
}
static inline Vec3 xf_inv_point(const Xform& x, Vec3 v) { return xf_inv_dir(x, v - x.p); }
// Composizione: prima b, poi a.
static inline Xform xf_mul(const Xform& a, const Xform& b) {
  Xform o;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      o.r[i * 3 + j] = a.r[i * 3] * b.r[j] + a.r[i * 3 + 1] * b.r[3 + j] + a.r[i * 3 + 2] * b.r[6 + j];
  o.p = xf_point(a, b.p);
  return o;
}

// Rotazione attorno a Z: porta (1,0,0) in (cos a, sin a, 0).
static inline Xform xf_rot_z(float a) {
  const float s = d_sin(a), c = d_cos(a);
  Xform x = xf_identity();
  x.r[0] = c; x.r[1] = -s; x.r[3] = s; x.r[4] = c;
  return x;
}
// Rotazione che porta l'asse Y locale lungo dir (unitario).
static inline Xform xf_align_y(Vec3 dir) {
  const Vec3 y = dir;
  const Vec3 h = fabsf(y.z) < 0.9f ? v3(0, 0, 1) : v3(1, 0, 0);
  const Vec3 xa = normalize(cross(y, h));
  const Vec3 za = cross(xa, y);
  Xform x = xf_identity();
  x.r[0] = xa.x; x.r[3] = xa.y; x.r[6] = xa.z;
  x.r[1] = y.x;  x.r[4] = y.y;  x.r[7] = y.z;
  x.r[2] = za.x; x.r[5] = za.y; x.r[8] = za.z;
  return x;
}
// Fattore di scala massimo della parte 3x3 (per allargare le sfere di scarto con squash & stretch).
static inline float xf_max_scale(const Xform& x) {
  float m = 0.0f;
  for (int c = 0; c < 3; ++c) {
    const float l = x.r[c] * x.r[c] + x.r[3 + c] * x.r[3 + c] + x.r[6 + c] * x.r[6 + c];
    if (l > m) m = l;
  }
  return sqrtf(m);
}
// Scala lungo gli assi.
static inline Xform xf_scale(float sx, float sy, float sz) {
  Xform x = xf_identity();
  x.r[0] = sx; x.r[4] = sy; x.r[8] = sz;
  return x;
}

struct Mat4 { float m[16]; };   // m[colonna * 4 + riga]

static inline Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
  Mat4 o;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      o.m[c * 4 + r] = a.m[r] * b.m[c * 4] + a.m[4 + r] * b.m[c * 4 + 1] +
                       a.m[8 + r] * b.m[c * 4 + 2] + a.m[12 + r] * b.m[c * 4 + 3];
  return o;
}

// Proiezione prospettica standard OpenGL (clip z in [-w, w]); w di clip = profondita' di vista.
static inline Mat4 mat4_perspective(float fovy, float aspect, float zn, float zf) {
  const float f = d_cos(fovy * 0.5f) / d_sin(fovy * 0.5f);
  Mat4 o;
  for (int i = 0; i < 16; ++i) o.m[i] = 0.0f;
  o.m[0] = f / aspect;
  o.m[5] = f;
  o.m[10] = (zf + zn) / (zn - zf);
  o.m[11] = -1.0f;
  o.m[14] = 2.0f * zf * zn / (zn - zf);
  return o;
}

static inline Mat4 mat4_look_at(Vec3 eye, Vec3 at, Vec3 up) {
  const Vec3 f = normalize(at - eye);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 o;
  o.m[0] = s.x;  o.m[4] = s.y;  o.m[8] = s.z;   o.m[12] = -dot(s, eye);
  o.m[1] = u.x;  o.m[5] = u.y;  o.m[9] = u.z;   o.m[13] = -dot(u, eye);
  o.m[2] = -f.x; o.m[6] = -f.y; o.m[10] = -f.z; o.m[14] = dot(f, eye);
  o.m[3] = 0;    o.m[7] = 0;    o.m[11] = 0;    o.m[15] = 1;
  return o;
}

// Righe 0, 1 e 3 di (VP * M) con M = [R p; 0 1]: bastano per x, y e w di clip.
static inline void mvp_rows013(const Mat4& vp, const Xform& x, float out[3][4]) {
  static const int rows[3] = { 0, 1, 3 };
  for (int k = 0; k < 3; ++k) {
    const int r = rows[k];
    const float a = vp.m[r], b = vp.m[4 + r], c = vp.m[8 + r], d = vp.m[12 + r];
    out[k][0] = a * x.r[0] + b * x.r[3] + c * x.r[6];
    out[k][1] = a * x.r[1] + b * x.r[4] + c * x.r[7];
    out[k][2] = a * x.r[2] + b * x.r[5] + c * x.r[8];
    out[k][3] = a * x.p.x + b * x.p.y + c * x.p.z + d;
  }
}

struct Plane { Vec3 n; float d; };   // dentro se dot(n, p) + d >= 0

// Piani sinistro, destro, basso, alto e vicino del tronco di vista, in spazio mondo.
static inline void frustum_planes(const Mat4& vp, float zn, Plane out[5]) {
  const float* m = vp.m;
  const float r0[4] = { m[0], m[4], m[8], m[12] };
  const float r1[4] = { m[1], m[5], m[9], m[13] };
  const float r3[4] = { m[3], m[7], m[11], m[15] };
  float p[5][4];
  for (int i = 0; i < 4; ++i) {
    p[0][i] = r3[i] + r0[i];
    p[1][i] = r3[i] - r0[i];
    p[2][i] = r3[i] + r1[i];
    p[3][i] = r3[i] - r1[i];
    p[4][i] = r3[i];
  }
  p[4][3] -= zn;
  for (int k = 0; k < 5; ++k) {
    const float l = sqrtf(p[k][0] * p[k][0] + p[k][1] * p[k][1] + p[k][2] * p[k][2]);
    const float inv = l > 0.0f ? 1.0f / l : 0.0f;
    out[k].n = v3(p[k][0] * inv, p[k][1] * inv, p[k][2] * inv);
    out[k].d = p[k][3] * inv;
  }
}

static inline bool sphere_outside(const Plane fr[5], Vec3 c, float radius) {
  for (int i = 0; i < 5; ++i)
    if (dot(fr[i].n, c) + fr[i].d < -radius) return true;
  return false;
}
