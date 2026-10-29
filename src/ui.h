// Interfaccia dalla 0.5.2: stile "Orbita" con i colori "Tramonto" (scelti da Andrea il 19/10/2026 sui
// mockup, vedi SPECIFICA.md). Pannelli con due angoli tagliati e il bordo che sfuma dall'ambra al
// rosa, voce scelta con la stessa sfumatura e un alone caldo, titoli bianchi con l'alone, caratteri
// Chakra Petch. Tutto in pixel a 640x480, con i valori dei mockup.
#pragma once
#include "hud.h"

namespace ui {

// Colori (0xRRGGBB; quelli con l'alfa 0xAARRGGBB).
const uint32_t L1 = 0xFFB547, L2 = 0xFF4F8B;            // ambra e rosa: bordi e voce scelta
const uint32_t ON = 0x2A0820;                            // testo sulla voce scelta
const uint32_t TX = 0xFFF4EC, MU = 0xD9BCD3;             // testo e testo secondario
const uint32_t DIM = 0x4E2C5A, DIM2 = 0x7A5486, OFF = 0x3C2148, DIS = 0x8E6C96;
const uint32_t A2 = 0x6FE0FF;                            // ciano: etichette "boss", "in arrivo", voci d'uscita
const uint32_t STAR = 0xFFD54A;
const uint32_t SCRIM = 0x18081E;                         // prugna scura delle velature
const uint32_t FTX = 0xEBD3E0, BTX = 0xE8D2E2;           // testo sopra il gioco, testo dei bottoni
const uint32_t AVBG = 0x2E1440, CARD_A = 0x4A1C52, CARD_B = 0x1E0E2E, TDROP = 0x4A1C52;
const uint32_t BASE = 0xE61E0E2Eu, SOLID = 0xF7200F30u;  // fondo dei pannelli (90%) e dei popup (97%)
const uint32_t GLOW = 0x8CFF785Au, TGLOW = 0x73FF965Au;  // alone della voce scelta e dei titoli
const uint32_t RING = 0xBFFFB547u, SEPC = 0x59FFB547u, KCFILL = 0x40FFB547u;
extern const uint32_t PLAYER[4];                         // colori dei giocatori (G1 rosso ... G4 giallo)

// Margine sicuro per i televisori, che tagliano i bordi dell'immagine.
const float SAFE_X = 32.0f, SAFE_Y = 24.0f;

// Velature: sopra (per i titoli), sotto (per il piede), su tutto (pausa, popup).
void scrim_top(Hud* h, float hh = 160.0f);
void scrim_bottom(Hud* h, float hh = 120.0f);
void overlay(Hud* h, float a);

// Testo che sta in w pixel: se serve tagliato alla fine, con ell ("…"; per le facce dei titoli, che
// non hanno quel carattere, "..."). Non spezza i caratteri UTF-8 ne' le parole hindi. Restituisce dst.
const char* fit(char* dst, int n, const char* s, int face, float track, float w, const char* ell = "\xE2\x80\xA6");
// Righe larghe al massimo w pixel, a capo sugli spazi e (cinese, giapponese) tra un carattere e
// l'altro; se non bastano max righe l'ultima finisce con "…" e cut (se c'e') diventa true. Una
// parola piu' larga della riga si spezza (una parola hindi no: i puntini) e anche allora cut e'
// true. Restituisce quante righe.
const int WRAP_LEN = 256;
int wrap(const char* s, float w, int face, float track, char (*out)[WRAP_LEN], int max, bool* cut = 0);

// Testo centrato in altezza su cy.
void text(Hud* h, float x, float cy, int face, uint32_t argb, const char* s, float track = 0.0f, int align = -1);
// Titolo con l'alone e l'ombra; deco: le lineette e i puntini ai lati. top = cima della riga.
void title(Hud* h, float cx, float top, int face, const char* s, float track, bool deco = true, float alpha = 1.0f);

// Pannello: alone (se glow), bordo sfumato largo bw, fondo fill.
void panel(Hud* h, float x, float y, float w, float hh, float cut = 16.0f, uint32_t fill = BASE, bool glow = false, float bw = 1.5f);
// Pannello spento: bordo a tinta unita.
void panel_off(Hud* h, float x, float y, float w, float hh, float cut = 16.0f, uint32_t border = DIM, uint32_t fill = BASE);
// Scheda scelta (Insieme): alone, bordo sfumato di 2 pixel, fondo sfumato.
void card_sel(Hud* h, float x, float y, float w, float hh, float cut = 16.0f);
// Fascia della voce scelta in un elenco (alone e sfumatura ambra-rosa).
void bar_sel(Hud* h, float x, float y, float w, float hh, float cut = 8.0f, bool glow = true);
// Bottone centrato su cx; restituisce nulla.
void button(Hud* h, float cx, float y, float w, float hh, const char* s, int face, float track, bool sel);
// Linea sottile che sfuma ai due capi.
void sep(Hud* h, float x, float y, float w);

// Tasto tondo con la lettera (A, B...); restituisce la larghezza.
float key(Hud* h, float x, float cy, const char* s, uint32_t color = L1);
// Piede dei menu in basso a sinistra: coppie tasto-testo ("A", "conferma", "B", "indietro"...).
void footer(Hud* h, const char* const* items, int n);
void version(Hud* h);
// Etichetta con il contorno (GIRO 2, BOSS, IN ARRIVO); align come hud_text; restituisce la larghezza.
float tag(Hud* h, float x, float cy, const char* s, uint32_t color, int face, float track, float padx, float hh, int align = -1);
// Gettone del giocatore (G1...): colore del giocatore, scritta bianca; restituisce la larghezza.
float gp(Hud* h, float x, float cy, int player, int face = HF_B11);
float gp_w(int player, int face = HF_B11);   // larghezza di gp, senza disegnare
// Batteria della console (0.5.4): "87%" e la pila, allineate a destra su right; tick per il
// lampeggio sotto il 15%. Restituisce la larghezza.
float battery(Hud* h, float right, float cy, int percent, bool charging, unsigned tick);
// Tasti nel popup dei comandi: tasto rettangolare (R1, START...) e levetta tonda (L, R).
float keycap(Hud* h, float x, float cy, const char* s, bool small);
float stick(Hud* h, float x, float cy, const char* s);

// Faccia di un eroe (px: 20, 24, 30 o 40); look come FACE_NORMAL/FACE_DOWN/FACE_OUT; a = opacita'.
void face(Hud* h, float x, float y, int px, int ch, int look, float a = 1.0f);
// Faccia nel suo tondo (32 pixel), come nella scheda Insieme.
void avatar(Hud* h, float x, float y, int ch, bool gray = false);
// Tre stelle da 16 (gap 3) a partire da x, centrate su cy: got = bit delle prese; on_sel: sulla fascia scelta.
float stars3(Hud* h, float x, float cy, unsigned got, bool on_sel);
void star(Hud* h, int icon, float x, float y, bool on, uint32_t off_color = DIM2);
// Tacche inclinate (volume, avanzamento).
void segs(Hud* h, float x, float cy, int n, int filled, float w, float hh, uint32_t on, uint32_t off, bool glow);
void toggle(Hud* h, float x, float cy, bool on);
// Pianeta piccolo (15 pixel) del pianeta w (WORLD_NAMES), dall'angolo in alto a sinistra; la Terra (26).
void planet(Hud* h, float x, float y, int w, float a = 1.0f);
void earth(Hud* h, float x, float y);
// Mappamondo (15 pixel) centrato su cx, cy: la voce Lingua.
void globe(Hud* h, float cx, float cy, uint32_t color);
// Freccetta > (8 x 12) e < .
void chev(Hud* h, float x, float y, bool right, uint32_t color);

}  // namespace ui
