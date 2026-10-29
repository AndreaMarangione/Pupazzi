#!/usr/bin/env python3
# Genera src/font_hud.h (dichiarazioni) e src/font_hud_data.h (dati): caratteri e disegni dell'HUD
# (dalla 0.5.2, stile Orbita), gia' alla misura esatta in pixel a 640x480, con i bordi sfumati.
#   python3 tools/make_font.py                         (scrive i due file in src/)
#   python3 tools/make_font.py --anteprima FILE.png    (l'atlante come lo costruisce il gioco, alone escluso)
# Servono Pillow con raqm (le coppie di crenatura), uharfbuzz e freetype-py (hindi, cinese e
# giapponese): pip install uharfbuzz freetype-py.
#
# Caratteri latini: Chakra Petch SemiBold e Bold (third_party/fonts, SIL OFL 1.1), una "faccia" per
# ogni misura usata dall'interfaccia. Ogni faccia ha il suo insieme di caratteri: i titoli solo
# maiuscole, cifre e punteggiatura, i testi tutto l'ASCII con le lettere accentate di italiano,
# spagnolo e portoghese e qualche simbolo. Nei testi del gioco si scrive in UTF-8; i simboli fuori
# da Latin-1 hanno un posto da 0x80 a 0x9F (vedi SPECIAL), che in Latin-1 sono caratteri di
# controllo mai usati. Le facce dei titoli hanno anche l'alone: il gioco lo calcola all'avvio
# sfocando il carattere (vedi hud.cpp), qui c'e' solo il suo posto nell'atlante.
#
# Dalla 0.5.3 anche le altre scritture, solo con i caratteri dei testi di lang/testi.txt:
#  - cinese semplificato e giapponese: Noto Sans CJK SC e JP, Medium e Bold (SIL OFL 1.1; del
#    sistema: pacchetto fonts-noto-cjk, oppure la cartella in PUPAZZI_NOTO_CJK), un carattere per
#    punto di codice;
#  - hindi: Poppins Medium e Bold (third_party/fonts, SIL OFL 1.1). Il devanagari va composto
#    (consonanti unite, vocali prima della consonante...): qui ogni parola dei testi si compone una
#    volta con HarfBuzz e diventa una fila di pezzi (glifi del font, ognuno al suo posto, a quarti
#    di pixel); il gioco cerca la parola intera. Una parola che non c'e' (testo nuovo non ancora
#    generato) si disegna lettera per lettera, senza comporla.
# Ogni faccia latina ha le sue facce di scrittura (SCRIPT_OF): per i testi piccoli un po' piu'
# grandi del latino (cinese e giapponese da 14 pixel, l'hindi da 15), nelle facce grandi (@T32...)
# solo i caratteri dei testi segnati cosi' in lang/testi.txt.
#
# Disegni: cuori, X, le facce dei 4 eroi in 3 strati, stelle, freccette, la Terra e i pianeti, la
# rete, le frecce verso i compagni e i bonus delle casse (0.5.4), a ogni misura che serve; il gioco li colora
# disegnandoli uno sopra l'altro.
#
# Nel file generato: per ogni carattere e disegno la posizione nell'atlante (largo 2048, alto
# quanto serve, impacchettato qui), la misura, lo spostamento rispetto alla penna sulla linea di
# base, l'avanzamento in sedicesimi di pixel e i pixel ad alfa 4 bit (due per byte); per ogni
# faccia le coppie di crenatura (da un quarto di pixel in su).
import os
import re
import sys
from math import cos, pi, sin
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_lang  # noqa: E402

FONTS = ROOT + "/third_party/fonts/"
FONT = {600: FONTS + "ChakraPetch-SemiBold.ttf", 700: FONTS + "ChakraPetch-Bold.ttf"}
DEVA_FONT = {"M": FONTS + "Poppins-Medium.ttf", "B": FONTS + "Poppins-Bold.ttf"}
NOTO_DIR = os.environ.get("PUPAZZI_NOTO_CJK", "/usr/share/fonts/opentype/noto")
CJK_FONT = {"M": NOTO_DIR + "/NotoSansCJK-Medium.ttc", "B": NOTO_DIR + "/NotoSansCJK-Bold.ttc"}
CJK_INDEX = {"SC": 2, "JP": 0}     # facce dentro i .ttc: 0 JP, 1 KR, 2 SC, 3 TC, 4 HK
ATLAS_W = 2048
GLOW_PAD = 12          # margine dell'alone: 3 passate di sfocatura a scatola larga 9 (raggio 4)
SS = 16                # i disegni si fanno 16 volte piu' grandi e poi si riducono

SPECIAL = {"→": 0x80, "←": 0x81, "↑": 0x82, "↓": 0x83, "’": 0x84, "…": 0x85, "•": 0x86}
LATIN = "àèéìòùÀÈÉÌÒÙáíóúñÁÍÓÚÑçÇüÜöÖäÄ«»·×°âêîôûãõÂÊÎÔÛÃÕ¿¡ªº"
FULL = "".join(chr(c) for c in range(32, 127)) + LATIN + "".join(SPECIAL)
TITLE = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,:;'-/()+ÀÈÉÌÒÙÁÍÓÚÑÇÜÖÄ×’→ÂÊÎÔÛÃÕ¿¡"

# nome, peso, misura in pixel, caratteri, alone, faccia di scrittura (cinese, giapponese, hindi)
FACES = [
    ("LOGO", 700, 80, "PUAZI", True, None),
    ("T44", 700, 44, TITLE, True, "T44"),
    ("T38", 700, 38, TITLE, True, "T38"),
    ("T32", 700, 32, TITLE, True, "T32"),
    ("T25", 700, 25, TITLE, False, "T25"),
    ("B24", 700, 24, FULL, False, "B22"),
    ("B22", 700, 22, FULL, False, "B22"),
    ("S17", 600, 17, FULL, False, "S16"),
    ("B16", 700, 16, FULL, False, "B16"),
    ("S16", 600, 16, FULL, False, "S16"),
    ("S15", 600, 15, FULL, False, "S16"),
    ("S14", 600, 14, FULL, False, "S16"),
    ("B13", 700, 13, FULL, False, "B13"),
    ("S13", 600, 13, FULL, False, "S13"),
    ("B12", 700, 12, FULL, False, "B13"),
    ("S12", 600, 12, FULL, False, "S13"),
    ("B11", 700, 11, FULL, False, "B13"),
    ("B10", 700, 10, FULL, False, "B13"),
]

# Facce di scrittura: peso (Medium o Bold), misura in pixel per cinese e giapponese e per l'hindi,
# testi (tutti, o solo quelli segnati con la faccia grande), alone.
SCRIPT_FACES = [
    ("S13", "M", 14, 15, None, False),
    ("B13", "B", 14, 15, None, False),
    ("S16", "M", 16, 17, None, False),
    ("B16", "B", 16, 17, None, False),
    ("B22", "B", 22, 22, "@B22", False),
    ("T25", "B", 24, 25, "@T25", False),
    ("T32", "B", 30, 30, "@T32", True),
    ("T38", "B", 36, 36, "@T38", True),
    ("T44", "B", 42, 42, "@T44", True),
]
SCRIPTS = ["SC", "JP", "DEVA"]           # come HUD_CJK_SC, HUD_CJK_JP, HUD_DEVA in hud.h
SCRIPT_LANG = {"SC": "zh", "JP": "ja", "DEVA": "hi"}
# I nomi delle lingue (pagina delle lingue, S16; voce della prima schermata, S13) si scrivono
# ognuno con i suoi caratteri, qualunque sia la lingua scelta.
LANG_NAMES = {"SC": "中文", "JP": "日本語", "DEVA": "हिन्दी"}


def slot(ch):
    c = ord(ch)
    if ch in SPECIAL:
        return SPECIAL[ch]
    assert c < 0x80 or 0xA0 <= c <= 0xFF, ch
    return c


def is_cjk(c):
    o = ord(c)
    return 0x3000 <= o <= 0x30FF or 0x3400 <= o <= 0x9FFF or 0xF900 <= o <= 0xFAFF or 0xFF00 <= o <= 0xFFEF


def is_deva(c):
    o = ord(c)
    return 0x900 <= o <= 0x97F or o in (0x200C, 0x200D)


DEVA_WORD = re.compile("[ऀ-ॿ‌‍]+")


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


# --- disegni -------------------------------------------------------------------------------------
# Ogni disegno riceve un ImageDraw e la funzione P che porta le coordinate (u da 0 a 1 sulla
# larghezza, v da 0 a 1 sull'altezza, y verso il basso) in pixel dell'immagine grande.


class Mapper:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.scale = min(w, h)

    def __call__(self, u, v):
        return (u * self.w, v * self.h)


def ellipse(d, P, cx, cy, rx, ry, rot=0.0, fill=255):
    a = rot * pi / 180.0
    pts = []
    for k in range(96):
        t = 2.0 * pi * k / 96
        x, y = rx * cos(t), ry * sin(t)
        pts.append(P(cx + x * cos(a) - y * sin(a), cy + x * sin(a) + y * cos(a)))
    d.polygon(pts, fill=fill)


def circle(d, P, cx, cy, r, fill=255):
    # Cerchio vero anche in un riquadro non quadrato: r e' in frazioni della larghezza.
    ellipse(d, P, cx, cy, r, r * P.w / P.h, 0.0, fill)


def stroke(d, P, pts, w, fill=255):
    # Spezzata larga w (in frazioni del lato corto) con le punte e i giunti tondi.
    q = [P(u, v) for u, v in pts]
    width = int(round(w * P.scale))
    d.line(q, fill=fill, width=width, joint="curve")
    for x, y in (q[0], q[-1]):
        r = width / 2.0
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def heart_points(P, cx, cy, k):
    pts = []
    for i in range(160):
        t = 2.0 * pi * i / 160
        x = 16.0 * sin(t) ** 3
        y = 13.0 * cos(t) - 5.0 * cos(2 * t) - 2.0 * cos(3 * t) - cos(4 * t)
        pts.append(P(cx + x / 16.0 * k, cy - (y + 2.5) / 16.0 * k))
    return pts


def g_heart(d, P):
    d.polygon(heart_points(P, 0.5, 0.52, 0.43), fill=255)


def g_heart_empty(d, P):
    d.polygon(heart_points(P, 0.5, 0.52, 0.43), fill=255)
    d.polygon(heart_points(P, 0.5, 0.515, 0.25), fill=0)


def g_cross(d, P):
    stroke(d, P, [(0.2, 0.2), (0.8, 0.8)], 0.17)
    stroke(d, P, [(0.8, 0.2), (0.2, 0.8)], 0.17)


# Astro, astronauta della Terra: casco tondo con la visiera.
def astro_1(d, P):
    circle(d, P, 0.5, 0.46, 0.41)
    d.rounded_rectangle([P(0.25, 0.78), P(0.75, 0.97)], radius=0.07 * P.scale, fill=255)
    d.rounded_rectangle([P(0.03, 0.36), P(0.13, 0.58)], radius=0.04 * P.scale, fill=255)
    d.rounded_rectangle([P(0.87, 0.36), P(0.97, 0.58)], radius=0.04 * P.scale, fill=255)


def astro_2(d, P):
    d.rounded_rectangle([P(0.18, 0.27), P(0.82, 0.67)], radius=0.17 * P.scale, fill=255)


def astro_3(d, P):
    ellipse(d, P, 0.34, 0.39, 0.085, 0.055, -25.0)
    circle(d, P, 0.47, 0.34, 0.03)


# Mochi, coniglio della Luna.
def mochi_1(d, P):
    ellipse(d, P, 0.5, 0.68, 0.34, 0.29)
    ellipse(d, P, 0.33, 0.29, 0.105, 0.25, -12.0)
    ellipse(d, P, 0.67, 0.29, 0.105, 0.25, 12.0)


def mochi_2(d, P):
    ellipse(d, P, 0.335, 0.32, 0.048, 0.165, -12.0)
    ellipse(d, P, 0.665, 0.32, 0.048, 0.165, 12.0)
    ellipse(d, P, 0.27, 0.77, 0.065, 0.045)
    ellipse(d, P, 0.73, 0.77, 0.065, 0.045)


def mochi_3(d, P):
    ellipse(d, P, 0.37, 0.64, 0.05, 0.068)
    ellipse(d, P, 0.63, 0.64, 0.05, 0.068)
    ellipse(d, P, 0.5, 0.75, 0.045, 0.032)


# Marzio, marziano del polo.
def marzio_1(d, P):
    circle(d, P, 0.5, 0.62, 0.34)
    stroke(d, P, [(0.4, 0.36), (0.26, 0.12)], 0.065)
    stroke(d, P, [(0.6, 0.36), (0.74, 0.12)], 0.065)
    circle(d, P, 0.25, 0.1, 0.075)
    circle(d, P, 0.75, 0.1, 0.075)


def marzio_2(d, P):
    circle(d, P, 0.5, 0.56, 0.19)


def marzio_3(d, P):
    circle(d, P, 0.52, 0.58, 0.085)
    d.arc([P(0.37, 0.64), P(0.63, 0.86)], start=35, end=145, fill=255, width=int(round(0.045 * P.scale)))


# Zolfo, fiamma di Venere.
def zolfo_1(d, P):
    circle(d, P, 0.5, 0.68, 0.29)
    d.polygon([P(0.215, 0.7), P(0.2, 0.52), P(0.27, 0.35), P(0.34, 0.43), P(0.39, 0.21), P(0.47, 0.31), P(0.54, 0.03),
               P(0.62, 0.3), P(0.7, 0.17), P(0.75, 0.4), P(0.8, 0.52), P(0.785, 0.7)], fill=255)


def zolfo_2(d, P):
    circle(d, P, 0.5, 0.75, 0.19)
    d.polygon([P(0.315, 0.76), P(0.34, 0.56), P(0.43, 0.61), P(0.51, 0.34), P(0.6, 0.57), P(0.67, 0.5), P(0.685, 0.76)], fill=255)


def zolfo_3(d, P):
    ellipse(d, P, 0.415, 0.69, 0.045, 0.062, 8.0)
    ellipse(d, P, 0.585, 0.69, 0.045, 0.062, -8.0)


FACE_LAYERS = [astro_1, astro_2, astro_3, mochi_1, mochi_2, mochi_3, marzio_1, marzio_2, marzio_3, zolfo_1, zolfo_2, zolfo_3]

# Stella a 5 punte (come nei mockup: riquadro 24).
STAR = [(12, 2.6), (14.9, 8.6), (21.5, 9.5), (16.7, 14.1), (17.9, 20.6), (12, 17.4), (6.1, 20.6), (7.3, 14.1), (2.5, 9.5), (9.1, 8.6)]


def g_star(d, P):
    d.polygon([P(x / 24.0, y / 24.0) for x, y in STAR], fill=255)


def g_star_o(d, P):
    stroke(d, P, [(x / 24.0, y / 24.0) for x, y in STAR + STAR[:1]], 2.0 / 24.0)


def g_chev_r(d, P):     # riquadro 8 x 12
    q = [(2 / 8, 2 / 12), (6 / 8, 6 / 12), (2 / 8, 10 / 12)]
    stroke(d, P, q, 2.0 / 8.0)


def g_chev_l(d, P):
    q = [(6 / 8, 2 / 12), (2 / 8, 6 / 12), (6 / 8, 10 / 12)]
    stroke(d, P, q, 2.0 / 8.0)


def g_tri_r(d, P):      # triangolino pieno (riquadro 8 x 10)
    d.polygon([P(0.1, 0.05), P(0.95, 0.5), P(0.1, 0.95)], fill=255)


def g_tri_u(d, P):
    d.polygon([P(0.05, 0.9), P(0.5, 0.1), P(0.95, 0.9)], fill=255)


def g_tri_d(d, P):
    d.polygon([P(0.05, 0.1), P(0.5, 0.9), P(0.95, 0.1)], fill=255)


# Rete (0.5.4, riquadro 18 x 14): tre archi e il punto.
def g_wifi(d, P):
    for r in (11.5, 7.6, 3.8):
        pts = []
        for i in range(25):
            a = (225.0 + 90.0 * i / 24.0) * pi / 180.0
            pts.append(((9.0 + r * cos(a)) / 18.0, (13.0 + r * sin(a)) / 14.0))
        stroke(d, P, pts, 2.0 / 14.0)
    circle(d, P, 9.0 / 18.0, 12.2 / 14.0, 1.7 / 18.0)


# Freccia verso un compagno fuori dallo schermo (0.5.4, riquadro 20): punta verso k * 45 gradi
# (0 a destra, 2 in basso, 4 a sinistra, 6 in alto).
def g_arrow(k):
    def f(d, P):
        a = k * pi / 4.0
        c, s = cos(a), sin(a)
        pts = [(0.46, 0.0), (-0.34, 0.37), (-0.14, 0.0), (-0.34, -0.37)]
        d.polygon([P(0.5 + x * c - y * s, 0.5 + x * s + y * c) for x, y in pts], fill=255)
    return f


# Bonus delle casse (0.5.4, riquadro 14): >> VELOCITA', fiamma FURIA, scudo INVINCIBILE, fulmine CARICA
# (come sulle monete del gioco, vedi build_powers in models.cpp).
def g_power(k):
    def f(d, P):
        if k == 0:
            for j in range(2):
                x = 0.16 + 0.36 * j
                stroke(d, P, [(x, 0.16), (x + 0.3, 0.5), (x, 0.84)], 0.17)
        elif k == 1:
            d.polygon([P(0.5, 0.02), P(0.78, 0.42), P(0.86, 0.66), P(0.74, 0.88), P(0.5, 0.98), P(0.26, 0.88), P(0.14, 0.66), P(0.22, 0.42)],
                      fill=255)
        elif k == 2:
            d.polygon([P(0.12, 0.08), P(0.88, 0.08), P(0.88, 0.5), P(0.7, 0.78), P(0.5, 0.96), P(0.3, 0.78), P(0.12, 0.5)], fill=255)
        else:
            d.polygon([P(0.64, 0.02), P(0.2, 0.56), P(0.47, 0.56), P(0.34, 0.98), P(0.82, 0.4), P(0.54, 0.4)], fill=255)
    return f


# La Terra (riquadro 26): mare, terre, anello sottile.
def earth_sea(d, P):
    circle(d, P, 0.5, 0.5, 11.0 / 26.0)


def earth_land(d, P):
    k = 1.0 / 26.0
    d.polygon([P(a * k, b * k) for a, b in [(7, 9.5), (9, 7.5), (12, 7.6), (13.2, 9), (12.4, 11.2), (14.2, 13.5), (13.6, 16.4),
                                           (11.4, 17.3), (10.4, 15.6), (8.6, 14.8), (6.4, 13.6), (5.9, 11.4)]], fill=255)
    d.polygon([P(a * k, b * k) for a, b in [(15.2, 6.2), (18.2, 6.8), (20.2, 9.2), (18.4, 9.8), (16.4, 10.2), (15.0, 8.4)]], fill=255)
    d.polygon([P(a * k, b * k) for a, b in [(17.0, 15.0), (19.6, 14.4), (19.0, 17.6), (17.2, 18.2)]], fill=255)


def earth_ring(d, P):
    w = int(round(1.2 / 26.0 * P.scale))
    r = 12.0 / 26.0
    d.ellipse([P(0.5 - r, 0.5 - r), P(0.5 + r, 0.5 + r)], outline=255, width=w)


# Pianeti (riquadro 16; Saturno 20 x 16).
def pl_body(d, P):
    circle(d, P, 0.5, 0.5, 6.5 / 16.0)


def pl_craters(d, P):
    circle(d, P, 6.0 / 16, 6.5 / 16, 1.4 / 16)
    circle(d, P, 10.0 / 16, 10.0 / 16, 1.0 / 16)


def pl_spot(d, P):
    circle(d, P, 9.5 / 16, 6.0 / 16, 1.2 / 16)


def pl_streak(d, P):
    pts = []
    for i in range(17):
        t = i / 16.0          # curva da (3, 9.5) a (13, 9): quadratica con il controllo a (8, 11)
        x = (1 - t) ** 2 * 3 + 2 * (1 - t) * t * 8 + t * t * 13
        y = (1 - t) ** 2 * 9.5 + 2 * (1 - t) * t * 11.2 + t * t * 9
        pts.append((x / 16.0, y / 16.0))
    stroke(d, P, pts, 1.4 / 16.0)


def pl_bands(d, P):
    w = int(round(1.4 / 16.0 * P.scale))
    for y, x0, x1 in ((6.5, 2.0, 14.0), (10.0, 1.8, 14.2)):
        d.line([P(x0 / 16, y / 16), P(x1 / 16, y / 16)], fill=255, width=w)


def sat_body(d, P):     # riquadro 20 x 16
    ellipse(d, P, 0.5, 0.5, 5.2 / 20.0, 5.2 / 16.0)


def sat_ring(d, P):
    w = 1.4
    for k in range(-2, 3):          # anello: ellisse spessa w (piu' ellissi vicine)
        o = w * k / 4.0
        rx, ry = (9.0 + o) / 20.0, (2.6 + o) / 16.0
        d.ellipse([P(0.5 - rx, 8.5 / 16 - ry), P(0.5 + rx, 8.5 / 16 + ry)], outline=255, width=max(1, int(round(w / 4.0 / 16.0 * P.h))))


# nome, funzione, larghezza, altezza
ICONS = [("HEART", g_heart, 12, 12), ("HEART_EMPTY", g_heart_empty, 12, 12), ("CROSS30", g_cross, 30, 30),
         ("STAR15", g_star, 15, 15), ("STAR16", g_star, 16, 16), ("STAR16O", g_star_o, 16, 16),
         ("STAR22", g_star, 22, 22), ("STAR26", g_star, 26, 26), ("STAR30", g_star, 30, 30), ("STAR34", g_star, 34, 34),
         ("CHEV_R", g_chev_r, 8, 12), ("CHEV_L", g_chev_l, 8, 12),
         ("TRI_R", g_tri_r, 8, 10), ("TRI_U", g_tri_u, 10, 8), ("TRI_D", g_tri_d, 10, 8),
         ("EARTH_SEA", earth_sea, 26, 26), ("EARTH_LAND", earth_land, 26, 26), ("EARTH_RING", earth_ring, 26, 26),
         ("PL_BODY", pl_body, 15, 15), ("PL_CRATERS", pl_craters, 15, 15), ("PL_SPOT", pl_spot, 15, 15),
         ("PL_STREAK", pl_streak, 15, 15), ("PL_BANDS", pl_bands, 15, 15), ("SAT_BODY", sat_body, 19, 15), ("SAT_RING", sat_ring, 19, 15),
         ("WIFI", g_wifi, 18, 14)]
ICONS += [("ARW%d" % k, g_arrow(k), 20, 20) for k in range(8)]
ICONS += [("PW%d" % k, g_power(k), 14, 14) for k in range(4)]
for px in (20, 24, 30, 40):
    for k, fn in enumerate(FACE_LAYERS):
        ICONS.append(("FACE%d_%d" % (px, k), fn, px, px))


def draw(fn, w, h):
    big = Image.new("L", (w * SS, h * SS), 0)
    fn(ImageDraw.Draw(big), Mapper(w * SS, h * SS))
    return big.resize((w, h), Image.BOX)



# --- caratteri latini ---------------------------------------------------------------------------


def render_face(name, weight, size, chars):
    font = ImageFont.truetype(FONT[weight], size, layout_engine=ImageFont.Layout.BASIC)
    glyphs = []
    for ch in chars:
        adv = font.getlength(ch)
        if ch == " ":
            glyphs.append((ch, None, 0, 0, adv))
            continue
        mask, off = font.getmask2(ch, mode="L", anchor="ls")
        img = Image.frombytes("L", mask.size, bytes(mask)) if mask.size[0] and mask.size[1] else None
        if img is not None:
            bb = img.getbbox()
            if bb is None:
                img = None
            else:
                img = img.crop(bb)
                off = (off[0] + bb[0], off[1] + bb[1])
        glyphs.append((ch, img, off[0] if img else 0, off[1] if img else 0, adv))
    # Crenatura: quanto cambia la larghezza di una coppia rispetto ai due caratteri da soli.
    kfont = ImageFont.truetype(FONT[weight], size, layout_engine=ImageFont.Layout.RAQM)
    single = {ch: kfont.getlength(ch) for ch in chars}
    kern = []
    letters = [c for c in chars if c != " "]
    for a in letters:
        for b in letters:
            k = kfont.getlength(a + b) - single[a] - single[b]
            q = int(round(k * 16))
            if abs(q) >= 4:          # sotto un quarto di pixel non si vede
                kern.append((slot(a), slot(b), q))
    asc = int(round(size * 0.992))
    desc = int(round(size * 0.308))
    return dict(name=name, weight=weight, size=size, glyphs=glyphs, kern=kern, asc=asc, desc=desc)


# --- testi delle lingue ---------------------------------------------------------------------------


def hud_upper(s):
    # come hud_upper in hud.cpp: a-z e le minuscole di Latin-1 (tranne la divisione)
    out = []
    for c in s:
        o = ord(c)
        if "a" <= c <= "z" or (0xE0 <= o <= 0xFE and o != 0xF7):
            c = chr(o - 32)
        out.append(c)
    return "".join(out)


def gather():
    """Per ogni (scrittura, faccia di scrittura) i caratteri (cinese, giapponese) o le parole (hindi)
    che servono; e i problemi: caratteri che nessuna faccia sa disegnare."""
    texts, errors = make_lang.parse()
    if errors:
        sys.exit("make_font: lang/testi.txt ha errori (python3 tools/make_lang.py)")
    for t in texts:                   # i %d diventano cifre (i %s sono altri testi della tabella)
        for l in t.t:
            t.t[l] = make_lang.CONV.sub(lambda m: "%" if m.group(6) == "%" else ("" if m.group(6) == "s" else "0"), t.t[l])
    problems = []
    for t in texts:
        for l in ("it", "en", "es", "pt"):
            s = t.t[l]
            bad = sorted(set(c for c in s if c not in FULL))
            if bad:
                problems.append("%s %r: caratteri senza disegno %r" % (l, s[:40], "".join(bad)))
            if any(f.startswith("@T") for f in t.faces):
                bad = sorted(set(c for c in hud_upper(s) if c not in TITLE))
                if bad:
                    problems.append("%s %r (titolo): caratteri senza disegno nei titoli %r" % (l, s[:40], "".join(bad)))
    need = {}
    for script in SCRIPTS:
        lang = SCRIPT_LANG[script]
        for t in texts:
            s = t.t[lang]
            bad = sorted(set(c for c in s if c not in FULL and not (is_deva(c) if script == "DEVA" else is_cjk(c))))
            if bad:
                problems.append("%s %r: caratteri senza disegno %r" % (lang, s[:40], "".join(bad)))
            if any(f.startswith("@T") for f in t.faces):
                bad = sorted(set(c for c in s if c in FULL and hud_upper(c) not in TITLE))
                if bad:
                    problems.append("%s %r (titolo): caratteri senza disegno nei titoli %r" % (lang, s[:40], "".join(bad)))
        for name, _w, _pc, _pd, flag, _glow in SCRIPT_FACES:
            items = set()
            srcs = [t.t[lang] for t in texts if flag is None or flag in t.faces]
            if name in ("S13", "S16"):
                srcs.append(LANG_NAMES[script])
            for s in srcs:
                if script == "DEVA":
                    items |= set(DEVA_WORD.findall(s))
                else:
                    items |= set(c for c in s if is_cjk(c))
            need[(script, name)] = sorted(items)
    return need, problems


# --- cinese e giapponese ------------------------------------------------------------------------

LOAD = None


def ft_load_flags():
    import freetype
    return freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT


def ft_image(face):
    """Il glifo appena caricato in face: immagine ritagliata e spostamento (o None)."""
    g = face.glyph
    bm = g.bitmap
    w, h, pitch = bm.width, bm.rows, bm.pitch
    if w == 0 or h == 0:
        return None, 0, 0
    buf = bytes(bm.buffer)
    img = Image.frombytes("L", (w, h), b"".join(buf[y * pitch:y * pitch + w] for y in range(h)))
    bb = img.getbbox()
    if bb is None:
        return None, 0, 0
    return img.crop(bb), g.bitmap_left + bb[0], -g.bitmap_top + bb[1]


def render_cjk(script, weight, px, chars):
    import freetype
    path = CJK_FONT[weight]
    if not os.path.exists(path):
        sys.exit("make_font: manca %s (pacchetto fonts-noto-cjk, oppure PUPAZZI_NOTO_CJK=<cartella>)" % path)
    face = freetype.Face(path, index=CJK_INDEX[script])
    face.set_pixel_sizes(0, px)
    out = []
    for ch in chars:
        if face.get_char_index(ord(ch)) == 0:
            sys.exit("make_font: %r non c'e' in %s (%s)" % (ch, path, script))
        face.load_char(ch, ft_load_flags())
        img, ox, oy = ft_image(face)
        out.append((ord(ch), img, ox, oy, face.glyph.advance.x / 64.0))
    return out


# --- hindi ----------------------------------------------------------------------------------------


def render_deva(weight, px, words, nominal):
    """Parole composte con HarfBuzz: ([(hash, byte, pezzi, avanzamento)], [pezzi da disegnare]).
    Un pezzo: (glifo, quarto di pixel) -> immagine; nella parola (pezzo, dx, dy) in pixel interi."""
    import freetype
    import uharfbuzz as hb
    path = DEVA_FONT[weight]
    hbfont = hb.Font(hb.Face(hb.Blob.from_file_path(path)))
    hbfont.scale = (px * 64, px * 64)
    face = freetype.Face(path)
    face.set_pixel_sizes(0, px)
    pieces = {}                       # (gid, frac) -> (img, ox, oy)

    def piece(gid, frac):
        key = (gid, frac)
        if key not in pieces:
            face.set_transform(freetype.Matrix(0x10000, 0, 0, 0x10000), freetype.Vector(int(round(frac * 64)), 0))
            face.load_glyph(gid, ft_load_flags())
            pieces[key] = ft_image(face)
            face.set_transform(freetype.Matrix(0x10000, 0, 0, 0x10000), freetype.Vector(0, 0))
        return key

    out = []
    for w in words:
        buf = hb.Buffer()
        buf.add_str(w)
        buf.direction = "ltr"
        buf.script = "Deva"
        buf.language = "hi"
        hb.shape(hbfont, buf, {})
        pen = 0
        run = []
        for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
            if info.codepoint == 0:
                sys.exit("make_font: in Poppins manca un glifo della parola %r" % w)
            x = (pen + pos.x_offset) / 64.0
            q = round(x * 4) / 4.0
            ix = int(q // 1)
            frac = q - ix
            dy = int(round(-pos.y_offset / 64.0))
            k = piece(info.codepoint, frac)
            if pieces[k][0] is not None:
                run.append((k, ix, dy))
            pen += pos.x_advance
        b = w.encode("utf-8")
        out.append((fnv(b), len(b), run, pen / 64.0))
    # Lettere da sole (parole che non ci sono): il glifo di ogni punto di codice, senza comporlo.
    singles = []
    if nominal:
        cmap = sorted(c for c in range(0x900, 0x980) if face.get_char_index(c))
        for c in cmap:
            face.load_char(chr(c), ft_load_flags())
            img, ox, oy = ft_image(face)
            singles.append((c, img, ox, oy, face.glyph.linearHoriAdvance / 65536.0))
    return out, pieces, singles


# --- atlante ---------------------------------------------------------------------------------------


def pack(items):
    # items: (chiave, w, h). Ripiani dall'alto, i piu' alti prima: stessa disposizione a ogni esecuzione.
    order = sorted(range(len(items)), key=lambda i: (-items[i][2], -items[i][1], i))
    pos = [None] * len(items)
    x = y = shelf = 0
    for i in order:
        _, w, h = items[i]
        if x + w + 1 > ATLAS_W:
            y += shelf + 1
            x = shelf = 0
        pos[i] = (x + 1, y + 1)       # un pixel vuoto intorno: il filtro lineare non tocca i vicini
        x += w + 1
        shelf = max(shelf, h)
    used = y + shelf + 2
    h = 512
    while h < used:
        h *= 2
    return pos, used, h


def nibbles(img):
    w, h = img.size
    px = img.load()
    out = bytearray()
    acc, n = 0, 0
    for y in range(h):
        for x in range(w):
            v = min(15, (px[x, y] + 8) // 17)
            if n == 0:
                acc = v
                n = 1
            else:
                out.append(acc | (v << 4))
                n = 0
    if n:
        out.append(acc)
    return out


def build():
    need, problems = gather()
    if problems:
        for p in problems:
            sys.stderr.write("make_font: %s\n" % p)
        sys.exit("make_font: %d testi con caratteri che non si sanno disegnare" % len(problems))
    faces = [dict(render_face(*f[:4]), glow=f[4], script=f[5]) for f in FACES]
    # Facce di scrittura: ognuna una lista di glifi (chiave, img, ox, oy, adv) e per l'hindi le parole.
    sfaces = []
    for script in SCRIPTS:
        for name, weight, pc, pd, flag, glow in SCRIPT_FACES:
            items = need[(script, name)]
            sf = dict(script=script, name=name, glow=glow, glyphs=[], words=[])
            if script == "DEVA":
                words, pieces, singles = render_deva(weight, pd, items, flag is None)
                keys = sorted(pieces)
                index = {k: i for i, k in enumerate(keys)}
                sf["glyphs"] = [(("g",) + k, pieces[k][0], pieces[k][1], pieces[k][2], 0.0) for k in keys]
                base = len(sf["glyphs"])
                sf["glyphs"] += [(c, img, ox, oy, adv) for c, img, ox, oy, adv in singles]
                sf["cps"] = [(c, base + i) for i, (c, _img, _ox, _oy, _adv) in enumerate(singles)]
                sf["words"] = [(h, n, [(index[k], dx, dy) for k, dx, dy in run], adv) for h, n, run, adv in words]
                seen = {}
                for h, n, _run, _adv in sf["words"]:
                    if (h, n) in seen:
                        sys.exit("make_font: due parole hindi con lo stesso hash: cambiare fnv")
                    seen[(h, n)] = 1
                sf["px"] = pd
            else:
                sf["glyphs"] = render_cjk(script, weight, pc, items)
                sf["cps"] = [(g[0], i) for i, g in enumerate(sf["glyphs"])]
                sf["px"] = pc
            sfaces.append(sf)
    icons = [(name, draw(fn, w, h)) for name, fn, w, h in ICONS]
    items, refs = [], []
    for fi, f in enumerate(faces):
        for gi, (ch, img, ox, oy, adv) in enumerate(f["glyphs"]):
            if img is None:
                continue
            items.append(("g", img.size[0], img.size[1]))
            refs.append(("g", fi, gi))
            if f["glow"]:
                items.append(("w", img.size[0] + 2 * GLOW_PAD, img.size[1] + 2 * GLOW_PAD))
                refs.append(("w", fi, gi))
    for si, sf in enumerate(sfaces):
        for gi, (_k, img, ox, oy, adv) in enumerate(sf["glyphs"]):
            if img is None:
                continue
            items.append(("sg", img.size[0], img.size[1]))
            refs.append(("sg", si, gi))
            if sf["glow"]:
                items.append(("sw", img.size[0] + 2 * GLOW_PAD, img.size[1] + 2 * GLOW_PAD))
                refs.append(("sw", si, gi))
    for ii, (name, img) in enumerate(icons):
        items.append(("i", img.size[0], img.size[1]))
        refs.append(("i", ii, 0))
    items.append(("s", 8, 8))
    refs.append(("s", 0, 0))
    pos, used_h, atlas_h = pack(items)
    return faces, sfaces, icons, refs, items, pos, used_h, atlas_h


def write_if_changed(path, text):
    old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
    if old != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)


def numbers(values, per=40):
    vals = list(values)
    return ["  " + ",".join(str(v) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per)]


def main_header():
    faces, sfaces, icons, refs, items, pos, used_h, atlas_h = build()
    where = {refs[i]: pos[i] for i in range(len(refs))}
    data = bytearray()
    glyph_rows = []          # x, y, w, h, ox, oy, adv16, data, glow
    face_maps = []
    face_info = []
    kern_rows = []

    def add_glyphs(glyphs, gkey, wkey, idx_of, glow):
        first = len(glyph_rows)
        for gi, (_k, img, ox, oy, adv) in enumerate(glyphs):
            if img is None:
                glyph_rows.append([0, 0, 0, 0, 0, 0, int(round(adv * 16)), 0xFFFFFFFF, 0xFFFF])
                continue
            x, y = where[(gkey, idx_of, gi)]
            glyph_rows.append([x, y, img.size[0], img.size[1], ox, oy, int(round(adv * 16)), len(data), 0xFFFF])
            data.extend(nibbles(img))
        # Aloni dopo i caratteri della faccia: stessa misura piu' il margine, pixel calcolati dal gioco.
        if glow:
            for gi, (_k, img, ox, oy, adv) in enumerate(glyphs):
                if img is None:
                    continue
                src = first + gi
                x, y = where[(wkey, idx_of, gi)]
                widx = len(glyph_rows)
                glyph_rows.append([x, y, img.size[0] + 2 * GLOW_PAD, img.size[1] + 2 * GLOW_PAD, ox - GLOW_PAD, oy - GLOW_PAD,
                                   glyph_rows[src][6], 0xFFFFFFFF, src])
                glyph_rows[src][8] = widx
        return first

    for fi, f in enumerate(faces):
        first = add_glyphs(f["glyphs"], "g", "w", fi, f["glow"])
        m = [0xFFFF] * 256
        for gi, (ch, _img, _ox, _oy, _adv) in enumerate(f["glyphs"]):
            m[slot(ch)] = first + gi
        kfirst = len(kern_rows)
        kern_rows += sorted(f["kern"])
        face_maps.append(m)
        face_info.append((f["size"], f["asc"], f["desc"], kfirst, len(kern_rows) - kfirst, 1 if f["glow"] else 0))
    cp_rows, word_rows, run_rows, sface_rows = [], [], [], []
    for si, sf in enumerate(sfaces):
        first = add_glyphs(sf["glyphs"], "sg", "sw", si, sf["glow"])
        cps = sorted((c, first + g) for c, g in sf["cps"])
        words = sorted(sf["words"], key=lambda w: (w[0], w[1]))
        sface_rows.append((len(cp_rows), len(cps), len(word_rows), len(words)))
        cp_rows += cps
        for h, n, run, adv in words:
            word_rows.append((h, n, len(run_rows), len(run), int(round(adv * 16))))
            for gi, dx, dy in run:
                assert -32000 < dx < 32000 and -128 <= dy <= 127
                run_rows.append((first + gi, dx, dy))
    sf_index = {(sf["script"], sf["name"]): si for si, sf in enumerate(sfaces)}
    face_sf = []
    for f in faces:
        face_sf.append([sf_index[(s, f["script"])] if f["script"] else 255 for s in SCRIPTS])
    icon_first = len(glyph_rows)
    for ii, (name, img) in enumerate(icons):
        x, y = where[("i", ii, 0)]
        glyph_rows.append([x, y, img.size[0], img.size[1], 0, 0, img.size[0] * 16, len(data), 0xFFFF])
        data.extend(nibbles(img))
    sx, sy = where[("s", 0, 0)]
    assert len(glyph_rows) < 0xFFFF and len(sfaces) < 255

    h = []
    h.append("// Generato da tools/make_font.py: non modificare a mano (i dati sono in font_hud_data.h).")
    h.append("// Caratteri: Chakra Petch SemiBold e Bold, Poppins Medium e Bold (hindi), Noto Sans CJK SC e JP")
    h.append("// Medium e Bold (cinese e giapponese), tutti SIL OFL 1.1: vedi third_party/fonts/LEGGIMI.txt.")
    h.append("// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.")
    h.append("#pragma once")
    h.append("#include <stdint.h>")
    h.append("#define HUD_ATLAS_W %d" % ATLAS_W)
    h.append("#define HUD_ATLAS_H %d" % atlas_h)
    h.append("#define HUD_ATLAS_USED %d" % used_h)
    h.append("#define HUD_GLOW_PAD %d" % GLOW_PAD)
    h.append("#define HUD_SOLID_X %d" % sx)
    h.append("#define HUD_SOLID_Y %d" % sy)
    h.append("#define HUD_NGLYPHS %d" % len(glyph_rows))
    h.append("#define HUD_NFACES %d" % len(faces))
    h.append("#define HUD_NSFACES %d" % len(sfaces))
    h.append("#define HUD_NCPS %d" % max(1, len(cp_rows)))
    h.append("#define HUD_NWORDS %d" % max(1, len(word_rows)))
    h.append("#define HUD_NRUNS %d" % max(1, len(run_rows)))
    h.append("#define HUD_NKERN %d" % max(1, len(kern_rows)))
    h.append("#define HUD_NNIB %d" % len(data))
    h.append("struct HudGlyph { uint16_t x, y; uint8_t w, h; int8_t ox, oy; uint16_t adv16; uint32_t data; uint16_t glow; };")
    h.append("struct HudFace { uint8_t size, asc, desc, glow; uint16_t kern_first, kern_n; };")
    h.append("struct HudKern { uint8_t a, b; int8_t k16; };")
    h.append("// Facce di scrittura (cinese, giapponese, hindi): punti di codice (in ordine) e parole composte")
    h.append("// (in ordine di hash FNV-1a dei byte UTF-8, poi di lunghezza), ognuna una fila di pezzi.")
    h.append("struct HudSFace { uint32_t cp_first, cp_n, word_first, word_n; };")
    h.append("struct HudCp { uint32_t cp; uint16_t glyph; };")
    h.append("struct HudWord { uint32_t hash; uint16_t len, run_n; uint32_t run_first; uint16_t adv16; };")
    h.append("struct HudRun { uint16_t glyph; int16_t dx; int8_t dy; };")
    h.append("enum {")
    for fi, f in enumerate(faces):
        h.append("  HF_%s = %d," % (f["name"], fi))
    h.append("};")
    h.append("enum {")
    for ii, (name, img) in enumerate(icons):
        h.append("  HI_%s = %d," % (name, icon_first + ii))
    h.append("};")
    h.append("extern const HudGlyph HUD_GLYPHS[HUD_NGLYPHS];")
    h.append("extern const HudFace HUD_FACES[HUD_NFACES];")
    h.append("extern const uint16_t HUD_FACE_MAP[HUD_NFACES][256];")
    h.append("extern const uint8_t HUD_FACE_SF[HUD_NFACES][3];   // faccia di scrittura per cinese, giapponese, hindi (255: nessuna)")
    h.append("extern const HudKern HUD_KERN[HUD_NKERN];")
    h.append("extern const HudSFace HUD_SFACES[HUD_NSFACES];")
    h.append("extern const HudCp HUD_CPS[HUD_NCPS];")
    h.append("extern const HudWord HUD_WORDS[HUD_NWORDS];")
    h.append("extern const HudRun HUD_RUNS[HUD_NRUNS];")
    h.append("extern const uint8_t HUD_NIB[HUD_NNIB];")
    write_if_changed(os.path.join(ROOT, "src", "font_hud.h"), "\n".join(h) + "\n")

    o = []
    o.append("// Generato da tools/make_font.py: non modificare a mano. Solo per hud.cpp.")
    o.append('#include "font_hud.h"')
    o.append("const HudGlyph HUD_GLYPHS[HUD_NGLYPHS] = {")
    for r in glyph_rows:
        assert -128 <= r[4] <= 127 and -128 <= r[5] <= 127 and r[2] < 256 and r[3] < 256, r
        o.append("  { %d, %d, %d, %d, %d, %d, %d, %du, %d }," % tuple(r))
    o.append("};")
    o.append("const HudFace HUD_FACES[HUD_NFACES] = {")
    for r in face_info:
        o.append("  { %d, %d, %d, %d, %d, %d }," % (r[0], r[1], r[2], r[5], r[3], r[4]))
    o.append("};")
    o.append("const uint16_t HUD_FACE_MAP[HUD_NFACES][256] = {")
    for m in face_maps:
        o.append("  { " + ", ".join(str(v) for v in m) + " },")
    o.append("};")
    o.append("const uint8_t HUD_FACE_SF[HUD_NFACES][3] = {")
    for r in face_sf:
        o.append("  { %d, %d, %d }," % tuple(r))
    o.append("};")
    o.append("const HudKern HUD_KERN[HUD_NKERN] = {")
    for a, b, k in kern_rows or [(0, 0, 0)]:
        assert -128 <= k <= 127
        o.append("  { %d, %d, %d }," % (a, b, k))
    o.append("};")
    o.append("const HudSFace HUD_SFACES[HUD_NSFACES] = {")
    for si, r in enumerate(sface_rows):
        o.append("  { %d, %d, %d, %d },   // %s %s" % (r + (sfaces[si]["script"], sfaces[si]["name"])))
    o.append("};")
    o.append("const HudCp HUD_CPS[HUD_NCPS] = {")
    for i in range(0, len(cp_rows), 8):
        o.append("  " + " ".join("{ %d, %d }," % r for r in cp_rows[i:i + 8]))
    if not cp_rows:
        o.append("  { 0, 0 },")
    o.append("};")
    o.append("const HudWord HUD_WORDS[HUD_NWORDS] = {")
    for i in range(0, len(word_rows), 4):
        o.append("  " + " ".join("{ %du, %d, %d, %d, %d }," % (r[0], r[1], r[3], r[2], r[4]) for r in word_rows[i:i + 4]))
    if not word_rows:
        o.append("  { 0, 0, 0, 0, 0 },")
    o.append("};")
    o.append("const HudRun HUD_RUNS[HUD_NRUNS] = {")
    for i in range(0, len(run_rows), 8):
        o.append("  " + " ".join("{ %d, %d, %d }," % r for r in run_rows[i:i + 8]))
    if not run_rows:
        o.append("  { 0, 0, 0 },")
    o.append("};")
    o.append("const uint8_t HUD_NIB[HUD_NNIB] = {")
    o += numbers(data)
    o.append("};")
    write_if_changed(os.path.join(ROOT, "src", "font_hud_data.h"), "\n".join(o) + "\n")
    nsg = sum(len(sf["glyphs"]) for sf in sfaces)
    sys.stderr.write("make_font: %d caratteri e disegni (%d di cinese, giapponese e hindi), %d parole hindi, %d coppie, "
                     "%d byte di pixel, atlante alto %d su %d\n" % (len(glyph_rows), nsg, len(word_rows), len(kern_rows), len(data),
                                                                    used_h, atlas_h))


def preview(path):
    faces, sfaces, icons, refs, items, pos, used_h, atlas_h = build()
    img = Image.new("L", (ATLAS_W, used_h), 24)
    for i, r in enumerate(refs):
        kind, a, b = r
        if kind == "g":
            g = faces[a]["glyphs"][b][1]
        elif kind == "sg":
            g = sfaces[a]["glyphs"][b][1]
        elif kind == "i":
            g = icons[a][1]
        else:
            continue
        img.paste(255, pos[i], g)
    img.save(path)


if len(sys.argv) == 3 and sys.argv[1] == "--anteprima":
    preview(sys.argv[2])
elif len(sys.argv) == 1:
    main_header()
else:
    sys.exit("uso: python3 tools/make_font.py [--anteprima FILE.png]")
