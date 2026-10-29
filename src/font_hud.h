// Generato da tools/make_font.py: non modificare a mano (i dati sono in font_hud_data.h).
// Caratteri: Chakra Petch SemiBold e Bold, Poppins Medium e Bold (hindi), Noto Sans CJK SC e JP
// Medium e Bold (cinese e giapponese), tutti SIL OFL 1.1: vedi third_party/fonts/LEGGIMI.txt.
// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.
#pragma once
#include <stdint.h>
#define HUD_ATLAS_W 2048
#define HUD_ATLAS_H 2048
#define HUD_ATLAS_USED 1715
#define HUD_GLOW_PAD 12
#define HUD_SOLID_X 1181
#define HUD_SOLID_Y 1684
#define HUD_NGLYPHS 8075
#define HUD_NFACES 18
#define HUD_NSFACES 27
#define HUD_NCPS 3830
#define HUD_NWORDS 1656
#define HUD_NRUNS 6397
#define HUD_NKERN 22863
#define HUD_NNIB 812360
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
  HI_HEART = 7993,
  HI_HEART_EMPTY = 7994,
  HI_CROSS30 = 7995,
  HI_STAR15 = 7996,
  HI_STAR16 = 7997,
  HI_STAR16O = 7998,
  HI_STAR22 = 7999,
  HI_STAR26 = 8000,
  HI_STAR30 = 8001,
  HI_STAR34 = 8002,
  HI_CHEV_R = 8003,
  HI_CHEV_L = 8004,
  HI_TRI_R = 8005,
  HI_TRI_U = 8006,
  HI_TRI_D = 8007,
  HI_EARTH_SEA = 8008,
  HI_EARTH_LAND = 8009,
  HI_EARTH_RING = 8010,
  HI_PL_BODY = 8011,
  HI_PL_CRATERS = 8012,
  HI_PL_SPOT = 8013,
  HI_PL_STREAK = 8014,
  HI_PL_BANDS = 8015,
  HI_SAT_BODY = 8016,
  HI_SAT_RING = 8017,
  HI_WIFI = 8018,
  HI_ARW0 = 8019,
  HI_ARW1 = 8020,
  HI_ARW2 = 8021,
  HI_ARW3 = 8022,
  HI_ARW4 = 8023,
  HI_ARW5 = 8024,
  HI_ARW6 = 8025,
  HI_ARW7 = 8026,
  HI_FACE20_0 = 8027,
  HI_FACE20_1 = 8028,
  HI_FACE20_2 = 8029,
  HI_FACE20_3 = 8030,
  HI_FACE20_4 = 8031,
  HI_FACE20_5 = 8032,
  HI_FACE20_6 = 8033,
  HI_FACE20_7 = 8034,
  HI_FACE20_8 = 8035,
  HI_FACE20_9 = 8036,
  HI_FACE20_10 = 8037,
  HI_FACE20_11 = 8038,
  HI_FACE24_0 = 8039,
  HI_FACE24_1 = 8040,
  HI_FACE24_2 = 8041,
  HI_FACE24_3 = 8042,
  HI_FACE24_4 = 8043,
  HI_FACE24_5 = 8044,
  HI_FACE24_6 = 8045,
  HI_FACE24_7 = 8046,
  HI_FACE24_8 = 8047,
  HI_FACE24_9 = 8048,
  HI_FACE24_10 = 8049,
  HI_FACE24_11 = 8050,
  HI_FACE30_0 = 8051,
  HI_FACE30_1 = 8052,
  HI_FACE30_2 = 8053,
  HI_FACE30_3 = 8054,
  HI_FACE30_4 = 8055,
  HI_FACE30_5 = 8056,
  HI_FACE30_6 = 8057,
  HI_FACE30_7 = 8058,
  HI_FACE30_8 = 8059,
  HI_FACE30_9 = 8060,
  HI_FACE30_10 = 8061,
  HI_FACE30_11 = 8062,
  HI_FACE40_0 = 8063,
  HI_FACE40_1 = 8064,
  HI_FACE40_2 = 8065,
  HI_FACE40_3 = 8066,
  HI_FACE40_4 = 8067,
  HI_FACE40_5 = 8068,
  HI_FACE40_6 = 8069,
  HI_FACE40_7 = 8070,
  HI_FACE40_8 = 8071,
  HI_FACE40_9 = 8072,
  HI_FACE40_10 = 8073,
  HI_FACE40_11 = 8074,
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
