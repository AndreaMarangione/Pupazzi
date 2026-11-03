// Interface since 0.5.2: "Orbita" style with the "Tramonto" colors (chosen by Andrea on 19/10/2026
// from the mockups, see SPECIFICA.md). Panels with two cut corners and the border shading from
// amber to pink, selected item with the same gradient and a warm glow, white titles with the glow,
// Chakra Petch glyphs. Everything in pixels at 640x480, with the values of the mockups.
#pragma once
#include "engine/hud.h"

namespace ui {

// Colors (0xRRGGBB; those with alpha 0xAARRGGBB).
const uint32_t L1 = 0xFFB547, L2 = 0xFF4F8B;            // amber and pink: borders and selected item
const uint32_t ON = 0x2A0820;                            // text on the selected item
const uint32_t TX = 0xFFF4EC, MU = 0xD9BCD3;             // text and secondary text
const uint32_t DIM = 0x4E2C5A, DIM2 = 0x7A5486, OFF = 0x3C2148, DIS = 0x8E6C96;
const uint32_t A2 = 0x6FE0FF;                            // cyan: "boss", "in arrivo" labels, exit items
const uint32_t STAR = 0xFFD54A;
const uint32_t SCRIM = 0x18081E;                         // dark plum of the scrims
const uint32_t FTX = 0xEBD3E0, BTX = 0xE8D2E2;           // text over the game, button text
const uint32_t AVBG = 0x2E1440, CARD_A = 0x4A1C52, CARD_B = 0x1E0E2E, TDROP = 0x4A1C52;
const uint32_t BASE = 0xE61E0E2Eu, SOLID = 0xF7200F30u;  // background of the panels (90%) and of the popups (97%)
const uint32_t GLOW = 0x8CFF785Au, TGLOW = 0x73FF965Au;  // glow of the selected item and of the titles
const uint32_t RING = 0xBFFFB547u, SEPC = 0x59FFB547u, KCFILL = 0x40FFB547u;
extern const uint32_t PLAYER[4];                         // player colors (G1 red ... G4 yellow)

// Safe margin for TVs, which crop the edges of the image.
const float SAFE_X = 32.0f, SAFE_Y = 24.0f;

// Scrims: top (for the titles), bottom (for the footer), over everything (pause, popup).
void scrim_top(Hud* h, float hh = 160.0f);
void scrim_bottom(Hud* h, float hh = 120.0f);
void overlay(Hud* h, float a);

// Text that fits in w pixels: cut at the end if needed, with ell ("…"; for the title faces, which
// lack that glyph, "..."). Does not split UTF-8 characters or Hindi words. Returns dst.
const char* fit(char* dst, int n, const char* s, int face, float track, float w, const char* ell = "\xE2\x80\xA6");
// Lines at most w pixels wide, breaking at spaces and (Chinese, Japanese) between one character
// and the next; if max lines are not enough the last one ends with "…" and cut (if given) becomes
// true. A word wider than the line is split (a Hindi word is not: the dots) and then too cut is
// true. Returns how many lines.
const int WRAP_LEN = 256;
int wrap(const char* s, float w, int face, float track, char (*out)[WRAP_LEN], int max, bool* cut = 0);

// Text centered vertically on cy.
void text(Hud* h, float x, float cy, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
// Title with glow and shadow; deco: the dashes and dots at the sides. top = top of the line.
void title(Hud* h, float cx, float top, int face, const char* s, float track, bool deco = true, float alpha = 1.0f);

// Panel: glow (if glow), gradient border bw wide, background fill.
void panel(Hud* h, float x, float y, float w, float hh, float cut = 16.0f, uint32_t fill = BASE, bool glow = false, float bw = 1.5f);
// Inactive panel: solid-color border.
void panel_off(Hud* h, float x, float y, float w, float hh, float cut = 16.0f, uint32_t border = DIM, uint32_t fill = BASE);
// Selected card (Insieme): glow, 2-pixel gradient border, gradient background.
void card_sel(Hud* h, float x, float y, float w, float hh, float cut = 16.0f);
// Band of the selected item in a list (glow and amber-pink gradient).
void bar_sel(Hud* h, float x, float y, float w, float hh, float cut = 8.0f, bool glow = true);
// Button centered on cx; returns nothing.
void button(Hud* h, float cx, float y, float w, float hh, const char* s, int face, float track, bool sel);
// Thin line that fades out at both ends.
void sep(Hud* h, float x, float y, float w);

// Round button with the letter (A, B...); returns the width.
float key(Hud* h, float x, float cy, const char* s, uint32_t color = L1);
// Menu footer at the bottom left: button-text pairs ("A", "conferma", "B", "indietro"...).
void footer(Hud* h, const char* const* items, int n);
// Version number v at the bottom right, above the footer.
void version(Hud* h, const char* v);
// Label with the outline (GIRO 2, BOSS, IN ARRIVO); align as in hud_text; returns the width.
float tag(Hud* h, float x, float cy, const char* s, uint32_t color, int face, float track, float padx, float hh, int align = -1);
// Player token (G1...): player color, white lettering; returns the width.
float gp(Hud* h, float x, float cy, int player, int face = HF_B11);
float gp_w(int player, int face = HF_B11);   // width of gp, without drawing
// Console battery (0.5.4): "87%" and the battery icon, right-aligned on right; tick for the
// blinking below 15%. Returns the width.
float battery(Hud* h, float right, float cy, int percent, bool charging, unsigned tick);
// Buttons in the controls popup: rectangular button (R1, START...) and round stick (L, R).
float keycap(Hud* h, float x, float cy, const char* s, bool small);
float stick(Hud* h, float x, float cy, const char* s);

// Face of a hero (px: 20, 24, 30 or 40); look as FACE_NORMAL/FACE_DOWN/FACE_OUT; a = opacity.
void face(Hud* h, float x, float y, int px, int ch, int look, float a = 1.0f);
// Face in its circle (32 pixels), as in the Insieme card.
void avatar(Hud* h, float x, float y, int ch, bool gray = false);
// Three stars of 16 (gap 3) from x, centered on cy: got = bits of those taken; on_sel: on the selected band.
float stars3(Hud* h, float x, float cy, unsigned got, bool on_sel);
void star(Hud* h, int icon, float x, float y, bool on, uint32_t off_color = DIM2);
// Slanted notches (volume, progress).
void segs(Hud* h, float x, float cy, int n, int filled, float w, float hh, uint32_t on, uint32_t off, bool glow);
void toggle(Hud* h, float x, float cy, bool on);
// Small planet (15 pixels) for planet w (WORLD_NAMES), from the top-left corner; the Earth (26).
void planet(Hud* h, float x, float y, int w, float a = 1.0f);
void earth(Hud* h, float x, float y);
// Globe (15 pixels) centered on cx, cy: the Lingua item.
void globe(Hud* h, float cx, float cy, uint32_t color);
// Small arrow > (8 x 12) and < .
void chev(Hud* h, float x, float y, bool right, uint32_t color);

}  // namespace ui
