// Matematica deterministica: stessi risultati bit per bit su x86, ARM a 32 bit e ARM a 64 bit.
// Niente funzioni trigonometriche di libm (possono differire tra piattaforme); sqrtf e' esatta
// ovunque. Tutto il progetto va compilato con -ffp-contract=off.
#pragma once
#include <math.h>
#include <stdint.h>

#define D_PI      3.14159265358979323846f
#define D_TWO_PI  6.28318530717958647692f
#define D_HALF_PI 1.57079632679489661923f

// Riporta un angolo in [-pi, pi].
static inline float d_wrap_pi(float x) {
  float k = x * (1.0f / D_TWO_PI);
  k = (float)(int)(k + (k >= 0.0f ? 0.5f : -0.5f));
  return x - k * D_TWO_PI;
}

// Errore massimo circa 4e-6.
static inline float d_sin(float x) {
  x = d_wrap_pi(x);
  if (x > D_HALF_PI) x = D_PI - x;
  else if (x < -D_HALF_PI) x = -D_PI - x;
  const float x2 = x * x;
  return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333333e-3f + x2 * (-1.9841270e-4f + x2 * 2.7557319e-6f))));
}

static inline float d_cos(float x) { return d_sin(x + D_HALF_PI); }

// atan su [-1, 1], errore massimo circa 1e-5 rad.
static inline float d_atan_unit(float z) {
  const float z2 = z * z;
  return z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f)))));
}

static inline float d_atan2(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  if (ax == 0.0f && ay == 0.0f) return 0.0f;
  float a = ax > ay ? d_atan_unit(ay / ax) : D_HALF_PI - d_atan_unit(ax / ay);
  if (x < 0.0f) a = D_PI - a;
  return y < 0.0f ? -a : a;
}
