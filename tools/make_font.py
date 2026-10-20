#!/usr/bin/env python3
# Genera src/font_hud.h: caratteri e disegni dell'HUD (dalla 0.5.2, stile Orbita), gia' alla misura
# esatta in pixel a 640x480, con i bordi sfumati.
#   python3 tools/make_font.py > src/font_hud.h        (serve Pillow con raqm per le coppie)
#   python3 tools/make_font.py --anteprima FILE.png    (l'atlante come lo costruisce il gioco, alone escluso)
#
# Caratteri: Chakra Petch SemiBold e Bold (third_party/fonts, SIL OFL 1.1), una "faccia" per ogni
# misura usata dall'interfaccia. Ogni faccia ha il suo insieme di caratteri: i titoli solo maiuscole,
# cifre e punteggiatura, i testi tutto l'ASCII con le lettere accentate di italiano e spagnolo e
# qualche simbolo. Nei testi del gioco si scrive in UTF-8; i simboli fuori da Latin-1 hanno un
# posto da 0x80 a 0x9F (vedi SPECIAL), che in Latin-1 sono caratteri di controllo mai usati.
# Le facce dei titoli hanno anche l'alone: il gioco lo calcola all'avvio sfocando il carattere
# (vedi hud.cpp), qui c'e' solo il suo posto nell'atlante.
#
# Disegni: cuori, X, le facce dei 4 eroi in 3 strati, stelle, freccette, la Terra e i pianeti, a
# ogni misura che serve; il gioco li colora disegnandoli uno sopra l'altro.
#
# Nel file generato: per ogni carattere e disegno la posizione nell'atlante (2048 x 512, impacchettato
# qui), la misura, lo spostamento rispetto alla penna sulla linea di base, l'avanzamento in sedicesimi
# di pixel e i pixel ad alfa 4 bit (due per byte); per ogni faccia le coppie di crenatura (da un
# quarto di pixel in su).
import sys
from math import cos, pi, sin
from PIL import Image, ImageDraw, ImageFont

ROOT = __file__.rsplit("/tools/", 1)[0] if "/tools/" in __file__ else "."
FONT = {600: ROOT + "/third_party/fonts/ChakraPetch-SemiBold.ttf", 700: ROOT + "/third_party/fonts/ChakraPetch-Bold.ttf"}
ATLAS_W, ATLAS_H = 2048, 512
GLOW_PAD = 12          # margine dell'alone: 3 passate di sfocatura a scatola larga 9 (raggio 4)
SS = 16                # i disegni si fanno 16 volte piu' grandi e poi si riducono

SPECIAL = {"→": 0x80, "←": 0x81, "↑": 0x82, "↓": 0x83, "’": 0x84, "…": 0x85, "•": 0x86}
LATIN = "àèéìòùÀÈÉÌÒÙáíóúñÁÍÓÚÑçÇüÜöÖäÄ«»·×°"
FULL = "".join(chr(c) for c in range(32, 127)) + LATIN + "".join(SPECIAL)
TITLE = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,:;'-/()+ÀÈÉÌÒÙÁÍÓÚÑÇÜÖÄ×’→"

# nome, peso, misura in pixel, caratteri, alone
FACES = [
    ("LOGO", 700, 80, "PUAZI", True),
    ("T44", 700, 44, TITLE, True),
    ("T38", 700, 38, TITLE, True),
    ("T32", 700, 32, TITLE, True),
    ("T25", 700, 25, TITLE, False),
    ("B24", 700, 24, FULL, False),
    ("B22", 700, 22, FULL, False),
    ("S17", 600, 17, FULL, False),
    ("B16", 700, 16, FULL, False),
    ("S16", 600, 16, FULL, False),
    ("S15", 600, 15, FULL, False),
    ("S14", 600, 14, FULL, False),
    ("B13", 700, 13, FULL, False),
    ("S13", 600, 13, FULL, False),
    ("B12", 700, 12, FULL, False),
    ("S12", 600, 12, FULL, False),
    ("B11", 700, 11, FULL, False),
    ("B10", 700, 10, FULL, False),
]


def slot(ch):
    c = ord(ch)
    if ch in SPECIAL:
        return SPECIAL[ch]
    assert c < 0x80 or 0xA0 <= c <= 0xFF, ch
    return c


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
         ("PL_STREAK", pl_streak, 15, 15), ("PL_BANDS", pl_bands, 15, 15), ("SAT_BODY", sat_body, 19, 15), ("SAT_RING", sat_ring, 19, 15)]
for px in (20, 24, 30, 40):
    for k, fn in enumerate(FACE_LAYERS):
        ICONS.append(("FACE%d_%d" % (px, k), fn, px, px))


def draw(fn, w, h):
    big = Image.new("L", (w * SS, h * SS), 0)
    fn(ImageDraw.Draw(big), Mapper(w * SS, h * SS))
    return big.resize((w, h), Image.BOX)


# --- caratteri ----------------------------------------------------------------------------------


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
        if y + h + 1 > ATLAS_H:
            sys.exit("atlante pieno")
        pos[i] = (x + 1, y + 1)       # un pixel vuoto intorno: il filtro lineare non tocca i vicini
        x += w + 1
        shelf = max(shelf, h)
    return pos, y + shelf + 2


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
    faces = [dict(render_face(*f[:4]), glow=f[4]) for f in FACES]
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
    for ii, (name, img) in enumerate(icons):
        items.append(("i", img.size[0], img.size[1]))
        refs.append(("i", ii, 0))
    items.append(("s", 8, 8))
    refs.append(("s", 0, 0))
    pos, used_h = pack(items)
    return faces, icons, refs, items, pos, used_h


def main_header():
    faces, icons, refs, items, pos, used_h = build()
    where = {refs[i]: pos[i] for i in range(len(refs))}
    data = bytearray()
    glyph_rows = []          # x, y, w, h, ox, oy, adv16, data, glow
    face_maps = []
    face_info = []
    kern_rows = []
    for fi, f in enumerate(faces):
        m = [0xFFFF] * 256
        first = len(glyph_rows)
        for gi, (ch, img, ox, oy, adv) in enumerate(f["glyphs"]):
            idx = len(glyph_rows)
            m[slot(ch)] = idx
            if img is None:
                glyph_rows.append([0, 0, 0, 0, 0, 0, int(round(adv * 16)), 0xFFFFFFFF, 0xFFFF])
                continue
            x, y = where[("g", fi, gi)]
            glyph_rows.append([x, y, img.size[0], img.size[1], ox, oy, int(round(adv * 16)), len(data), 0xFFFF])
            data += nibbles(img)
        # Aloni dopo i caratteri della faccia: stessa misura piu' il margine, pixel calcolati dal gioco.
        if f["glow"]:
            for gi, (ch, img, ox, oy, adv) in enumerate(f["glyphs"]):
                if img is None:
                    continue
                src = first + gi
                x, y = where[("w", fi, gi)]
                widx = len(glyph_rows)
                glyph_rows.append([x, y, img.size[0] + 2 * GLOW_PAD, img.size[1] + 2 * GLOW_PAD, ox - GLOW_PAD, oy - GLOW_PAD,
                                   glyph_rows[src][6], 0xFFFFFFFF, src])
                glyph_rows[src][8] = widx
        kfirst = len(kern_rows)
        kern_rows += sorted(f["kern"])
        face_maps.append(m)
        face_info.append((f["size"], f["asc"], f["desc"], kfirst, len(kern_rows) - kfirst, 1 if f["glow"] else 0))
    icon_first = len(glyph_rows)
    for ii, (name, img) in enumerate(icons):
        x, y = where[("i", ii, 0)]
        glyph_rows.append([x, y, img.size[0], img.size[1], 0, 0, img.size[0] * 16, len(data), 0xFFFF])
        data += nibbles(img)
    sx, sy = where[("s", 0, 0)]

    o = []
    o.append("// Generato da tools/make_font.py: non modificare a mano.")
    o.append("// Caratteri: Chakra Petch SemiBold e Bold (SIL OFL 1.1), vedi third_party/fonts/LEGGIMI.txt.")
    o.append("// Disegni (cuori, facce, stelle, pianeti...) fatti da make_font.py.")
    o.append("#pragma once")
    o.append("#include <stdint.h>")
    o.append("#define HUD_ATLAS_W %d" % ATLAS_W)
    o.append("#define HUD_ATLAS_H %d" % ATLAS_H)
    o.append("#define HUD_ATLAS_USED %d" % used_h)
    o.append("#define HUD_GLOW_PAD %d" % GLOW_PAD)
    o.append("#define HUD_SOLID_X %d" % sx)
    o.append("#define HUD_SOLID_Y %d" % sy)
    o.append("#define HUD_NGLYPHS %d" % len(glyph_rows))
    o.append("#define HUD_NFACES %d" % len(faces))
    o.append("struct HudGlyph { uint16_t x, y; uint8_t w, h; int8_t ox, oy; uint16_t adv16; uint32_t data; uint16_t glow; };")
    o.append("struct HudFace { uint8_t size, asc, desc, glow; uint16_t kern_first, kern_n; };")
    o.append("struct HudKern { uint8_t a, b; int8_t k16; };")
    o.append("enum {")
    for fi, f in enumerate(faces):
        o.append("  HF_%s = %d," % (f["name"], fi))
    o.append("};")
    o.append("enum {")
    for ii, (name, img) in enumerate(icons):
        o.append("  HI_%s = %d," % (name, icon_first + ii))
    o.append("};")
    o.append("static const HudGlyph HUD_GLYPHS[%d] = {" % len(glyph_rows))
    for r in glyph_rows:
        assert -128 <= r[4] <= 127 and -128 <= r[5] <= 127 and r[2] < 256 and r[3] < 256, r
        o.append("  { %d, %d, %d, %d, %d, %d, %d, %du, %d }," % tuple(r))
    o.append("};")
    o.append("static const HudFace HUD_FACES[%d] = {" % len(faces))
    for r in face_info:
        o.append("  { %d, %d, %d, %d, %d, %d }," % (r[0], r[1], r[2], r[5], r[3], r[4]))
    o.append("};")
    o.append("static const uint16_t HUD_FACE_MAP[%d][256] = {" % len(faces))
    for m in face_maps:
        o.append("  { " + ", ".join(str(v) for v in m) + " },")
    o.append("};")
    o.append("static const HudKern HUD_KERN[%d] = {" % max(1, len(kern_rows)))
    for a, b, k in kern_rows or [(0, 0, 0)]:
        assert -128 <= k <= 127
        o.append("  { %d, %d, %d }," % (a, b, k))
    o.append("};")
    o.append("static const uint8_t HUD_NIB[%d] = {" % len(data))
    for i in range(0, len(data), 40):
        o.append("  " + ",".join("0x%02x" % b for b in data[i:i + 40]) + ",")
    o.append("};")
    print("\n".join(o))
    sys.stderr.write("make_font: %d caratteri e disegni, %d coppie, %d byte di pixel, atlante alto %d su %d\n" %
                     (len(glyph_rows), len(kern_rows), len(data), used_h, ATLAS_H))


def preview(path):
    faces, icons, refs, items, pos, used_h = build()
    img = Image.new("L", (ATLAS_W, used_h), 24)
    for i, r in enumerate(refs):
        kind, a, b = r
        if kind == "g":
            g = faces[a]["glyphs"][b][1]
            img.paste(255, pos[i], g)
        elif kind == "i":
            g = icons[a][1]
            img.paste(255, pos[i], g)
    img.save(path)


if len(sys.argv) == 3 and sys.argv[1] == "--anteprima":
    preview(sys.argv[2])
else:
    main_header()
