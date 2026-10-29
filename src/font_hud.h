// Generato da tools/make_font.py: non modificare a mano (i dati sono in font_hud_data.h).
// Caratteri: Chakra Petch SemiBold e Bold, Poppins Medium e Bold (hindi), Noto Sans CJK SC e JP
// Medium e Bold (cinese e giapponese), tutti SIL OFL 1.1: vedi third_party/fonts/LEGGIMI.txt.
// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.
#pragma once
#include <stdint.h>
#define HUD_ATLAS_W 2048
#define HUD_ATLAS_H 2048
#define HUD_ATLAS_USED 1602
#define HUD_GLOW_PAD 12
#define HUD_SOLID_X 832
#define HUD_SOLID_Y 1574
#define HUD_NGLYPHS 7665
#define HUD_NFACES 18
#define HUD_NSFACES 27
#define HUD_NCPS 3533
#define HUD_NWORDS 1399
#define HUD_NRUNS 5507
#define HUD_NKERN 22863
#define HUD_NNIB 759456
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
  HI_HEART = 7592,
  HI_HEART_EMPTY = 7593,
  HI_CROSS30 = 7594,
  HI_STAR15 = 7595,
  HI_STAR16 = 7596,
  HI_STAR16O = 7597,
  HI_STAR22 = 7598,
  HI_STAR26 = 7599,
  HI_STAR30 = 7600,
  HI_STAR34 = 7601,
  HI_CHEV_R = 7602,
  HI_CHEV_L = 7603,
  HI_TRI_R = 7604,
  HI_TRI_U = 7605,
  HI_TRI_D = 7606,
  HI_EARTH_SEA = 7607,
  HI_EARTH_LAND = 7608,
  HI_EARTH_RING = 7609,
  HI_PL_BODY = 7610,
  HI_PL_CRATERS = 7611,
  HI_PL_SPOT = 7612,
  HI_PL_STREAK = 7613,
  HI_PL_BANDS = 7614,
  HI_SAT_BODY = 7615,
  HI_SAT_RING = 7616,
  HI_FACE20_0 = 7617,
  HI_FACE20_1 = 7618,
  HI_FACE20_2 = 7619,
  HI_FACE20_3 = 7620,
  HI_FACE20_4 = 7621,
  HI_FACE20_5 = 7622,
  HI_FACE20_6 = 7623,
  HI_FACE20_7 = 7624,
  HI_FACE20_8 = 7625,
  HI_FACE20_9 = 7626,
  HI_FACE20_10 = 7627,
  HI_FACE20_11 = 7628,
  HI_FACE24_0 = 7629,
  HI_FACE24_1 = 7630,
  HI_FACE24_2 = 7631,
  HI_FACE24_3 = 7632,
  HI_FACE24_4 = 7633,
  HI_FACE24_5 = 7634,
  HI_FACE24_6 = 7635,
  HI_FACE24_7 = 7636,
  HI_FACE24_8 = 7637,
  HI_FACE24_9 = 7638,
  HI_FACE24_10 = 7639,
  HI_FACE24_11 = 7640,
  HI_FACE30_0 = 7641,
  HI_FACE30_1 = 7642,
  HI_FACE30_2 = 7643,
  HI_FACE30_3 = 7644,
  HI_FACE30_4 = 7645,
  HI_FACE30_5 = 7646,
  HI_FACE30_6 = 7647,
  HI_FACE30_7 = 7648,
  HI_FACE30_8 = 7649,
  HI_FACE30_9 = 7650,
  HI_FACE30_10 = 7651,
  HI_FACE30_11 = 7652,
  HI_FACE40_0 = 7653,
  HI_FACE40_1 = 7654,
  HI_FACE40_2 = 7655,
  HI_FACE40_3 = 7656,
  HI_FACE40_4 = 7657,
  HI_FACE40_5 = 7658,
  HI_FACE40_6 = 7659,
  HI_FACE40_7 = 7660,
  HI_FACE40_8 = 7661,
  HI_FACE40_9 = 7662,
  HI_FACE40_10 = 7663,
  HI_FACE40_11 = 7664,
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
