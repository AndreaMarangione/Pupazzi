// Generato da tools/make_font.py: non modificare a mano (i dati sono in font_hud_data.h).
// Caratteri: Chakra Petch SemiBold e Bold, Poppins Medium e Bold (hindi), Noto Sans CJK SC e JP
// Medium e Bold (cinese e giapponese), tutti SIL OFL 1.1: vedi third_party/fonts/LEGGIMI.txt.
// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.
#pragma once
#include <stdint.h>
#define HUD_ATLAS_W 2048
#define HUD_ATLAS_H 2048
#define HUD_ATLAS_USED 1604
#define HUD_GLOW_PAD 12
#define HUD_SOLID_X 1458
#define HUD_SOLID_Y 1573
#define HUD_NGLYPHS 7726
#define HUD_NFACES 18
#define HUD_NSFACES 27
#define HUD_NCPS 3580
#define HUD_NWORDS 1411
#define HUD_NRUNS 5589
#define HUD_NKERN 22863
#define HUD_NNIB 764635
struct HudGlyph { uint16_t x, y; uint8_t w, h; int8_t ox, oy; uint16_t adv16; uint32_t data; uint16_t glow; };
struct HudFace { uint8_t size, asc, desc, glow; uint16_t kern_first, kern_n; };
struct HudKern { uint8_t a, b; int8_t k16; };
// Facce di scrittura (cinese, giapponese, hindi): punti di codice (in ordine) e parole composte
// (in ordine di hash FNV-1a dei byte UTF-8, poi di lunghezza), ognuna una fila di pezzi.
struct HudSFace { uint32_t cp_first, cp_n, word_first, word_n; };
struct HudCp { uint32_t cp; uint16_t glyph; };
struct HudWord { uint32_t hash; uint16_t len, run_n; uint32_t run_first; uint16_t adv16; };
struct HudRun { uint16_t glyph; int16_t dx; int8_t dy; };
enum {
  HF_LOGO = 0,
  HF_T44 = 1,
  HF_T38 = 2,
  HF_T32 = 3,
  HF_T25 = 4,
  HF_B24 = 5,
  HF_B22 = 6,
  HF_S17 = 7,
  HF_B16 = 8,
  HF_S16 = 9,
  HF_S15 = 10,
  HF_S14 = 11,
  HF_B13 = 12,
  HF_S13 = 13,
  HF_B12 = 14,
  HF_S12 = 15,
  HF_B11 = 16,
  HF_B10 = 17,
};
enum {
  HI_HEART = 7653,
  HI_HEART_EMPTY = 7654,
  HI_CROSS30 = 7655,
  HI_STAR15 = 7656,
  HI_STAR16 = 7657,
  HI_STAR16O = 7658,
  HI_STAR22 = 7659,
  HI_STAR26 = 7660,
  HI_STAR30 = 7661,
  HI_STAR34 = 7662,
  HI_CHEV_R = 7663,
  HI_CHEV_L = 7664,
  HI_TRI_R = 7665,
  HI_TRI_U = 7666,
  HI_TRI_D = 7667,
  HI_EARTH_SEA = 7668,
  HI_EARTH_LAND = 7669,
  HI_EARTH_RING = 7670,
  HI_PL_BODY = 7671,
  HI_PL_CRATERS = 7672,
  HI_PL_SPOT = 7673,
  HI_PL_STREAK = 7674,
  HI_PL_BANDS = 7675,
  HI_SAT_BODY = 7676,
  HI_SAT_RING = 7677,
  HI_FACE20_0 = 7678,
  HI_FACE20_1 = 7679,
  HI_FACE20_2 = 7680,
  HI_FACE20_3 = 7681,
  HI_FACE20_4 = 7682,
  HI_FACE20_5 = 7683,
  HI_FACE20_6 = 7684,
  HI_FACE20_7 = 7685,
  HI_FACE20_8 = 7686,
  HI_FACE20_9 = 7687,
  HI_FACE20_10 = 7688,
  HI_FACE20_11 = 7689,
  HI_FACE24_0 = 7690,
  HI_FACE24_1 = 7691,
  HI_FACE24_2 = 7692,
  HI_FACE24_3 = 7693,
  HI_FACE24_4 = 7694,
  HI_FACE24_5 = 7695,
  HI_FACE24_6 = 7696,
  HI_FACE24_7 = 7697,
  HI_FACE24_8 = 7698,
  HI_FACE24_9 = 7699,
  HI_FACE24_10 = 7700,
  HI_FACE24_11 = 7701,
  HI_FACE30_0 = 7702,
  HI_FACE30_1 = 7703,
  HI_FACE30_2 = 7704,
  HI_FACE30_3 = 7705,
  HI_FACE30_4 = 7706,
  HI_FACE30_5 = 7707,
  HI_FACE30_6 = 7708,
  HI_FACE30_7 = 7709,
  HI_FACE30_8 = 7710,
  HI_FACE30_9 = 7711,
  HI_FACE30_10 = 7712,
  HI_FACE30_11 = 7713,
  HI_FACE40_0 = 7714,
  HI_FACE40_1 = 7715,
  HI_FACE40_2 = 7716,
  HI_FACE40_3 = 7717,
  HI_FACE40_4 = 7718,
  HI_FACE40_5 = 7719,
  HI_FACE40_6 = 7720,
  HI_FACE40_7 = 7721,
  HI_FACE40_8 = 7722,
  HI_FACE40_9 = 7723,
  HI_FACE40_10 = 7724,
  HI_FACE40_11 = 7725,
};
extern const HudGlyph HUD_GLYPHS[HUD_NGLYPHS];
extern const HudFace HUD_FACES[HUD_NFACES];
extern const uint16_t HUD_FACE_MAP[HUD_NFACES][256];
extern const uint8_t HUD_FACE_SF[HUD_NFACES][3];   // faccia di scrittura per cinese, giapponese, hindi (255: nessuna)
extern const HudKern HUD_KERN[HUD_NKERN];
extern const HudSFace HUD_SFACES[HUD_NSFACES];
extern const HudCp HUD_CPS[HUD_NCPS];
extern const HudWord HUD_WORDS[HUD_NWORDS];
extern const HudRun HUD_RUNS[HUD_NRUNS];
extern const uint8_t HUD_NIB[HUD_NNIB];
