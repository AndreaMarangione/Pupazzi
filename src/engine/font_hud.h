// Generato da tools/make_font.py: non modificare a mano (i dati sono in font_hud_data.h).
// Caratteri: Chakra Petch SemiBold e Bold, Poppins Medium e Bold (hindi), Noto Sans CJK SC e JP
// Medium e Bold (cinese e giapponese), tutti SIL OFL 1.1: vedi third_party/fonts/LEGGIMI.txt.
// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.
#pragma once
#include <stdint.h>
#define HUD_ATLAS_W 2048
#define HUD_ATLAS_H 2048
#define HUD_ATLAS_USED 1716
#define HUD_GLOW_PAD 12
#define HUD_SOLID_X 1426
#define HUD_SOLID_Y 1685
#define HUD_NGLYPHS 8092
#define HUD_NFACES 18
#define HUD_NSFACES 27
#define HUD_NCPS 3842
#define HUD_NWORDS 1664
#define HUD_NRUNS 6429
#define HUD_NKERN 22863
#define HUD_NNIB 814147
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
  HI_HEART = 8006,
  HI_HEART_EMPTY = 8007,
  HI_CROSS30 = 8008,
  HI_STAR15 = 8009,
  HI_STAR16 = 8010,
  HI_STAR16O = 8011,
  HI_STAR22 = 8012,
  HI_STAR26 = 8013,
  HI_STAR30 = 8014,
  HI_STAR34 = 8015,
  HI_CHEV_R = 8016,
  HI_CHEV_L = 8017,
  HI_TRI_R = 8018,
  HI_TRI_U = 8019,
  HI_TRI_D = 8020,
  HI_EARTH_SEA = 8021,
  HI_EARTH_LAND = 8022,
  HI_EARTH_RING = 8023,
  HI_PL_BODY = 8024,
  HI_PL_CRATERS = 8025,
  HI_PL_SPOT = 8026,
  HI_PL_STREAK = 8027,
  HI_PL_BANDS = 8028,
  HI_SAT_BODY = 8029,
  HI_SAT_RING = 8030,
  HI_WIFI = 8031,
  HI_ARW0 = 8032,
  HI_ARW1 = 8033,
  HI_ARW2 = 8034,
  HI_ARW3 = 8035,
  HI_ARW4 = 8036,
  HI_ARW5 = 8037,
  HI_ARW6 = 8038,
  HI_ARW7 = 8039,
  HI_PW0 = 8040,
  HI_PW1 = 8041,
  HI_PW2 = 8042,
  HI_PW3 = 8043,
  HI_FACE20_0 = 8044,
  HI_FACE20_1 = 8045,
  HI_FACE20_2 = 8046,
  HI_FACE20_3 = 8047,
  HI_FACE20_4 = 8048,
  HI_FACE20_5 = 8049,
  HI_FACE20_6 = 8050,
  HI_FACE20_7 = 8051,
  HI_FACE20_8 = 8052,
  HI_FACE20_9 = 8053,
  HI_FACE20_10 = 8054,
  HI_FACE20_11 = 8055,
  HI_FACE24_0 = 8056,
  HI_FACE24_1 = 8057,
  HI_FACE24_2 = 8058,
  HI_FACE24_3 = 8059,
  HI_FACE24_4 = 8060,
  HI_FACE24_5 = 8061,
  HI_FACE24_6 = 8062,
  HI_FACE24_7 = 8063,
  HI_FACE24_8 = 8064,
  HI_FACE24_9 = 8065,
  HI_FACE24_10 = 8066,
  HI_FACE24_11 = 8067,
  HI_FACE30_0 = 8068,
  HI_FACE30_1 = 8069,
  HI_FACE30_2 = 8070,
  HI_FACE30_3 = 8071,
  HI_FACE30_4 = 8072,
  HI_FACE30_5 = 8073,
  HI_FACE30_6 = 8074,
  HI_FACE30_7 = 8075,
  HI_FACE30_8 = 8076,
  HI_FACE30_9 = 8077,
  HI_FACE30_10 = 8078,
  HI_FACE30_11 = 8079,
  HI_FACE40_0 = 8080,
  HI_FACE40_1 = 8081,
  HI_FACE40_2 = 8082,
  HI_FACE40_3 = 8083,
  HI_FACE40_4 = 8084,
  HI_FACE40_5 = 8085,
  HI_FACE40_6 = 8086,
  HI_FACE40_7 = 8087,
  HI_FACE40_8 = 8088,
  HI_FACE40_9 = 8089,
  HI_FACE40_10 = 8090,
  HI_FACE40_11 = 8091,
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
