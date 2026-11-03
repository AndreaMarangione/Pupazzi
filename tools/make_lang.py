#!/usr/bin/env python3
# Game texts in the 7 languages: reads lang/testi.txt, checks it and generates src/engine/lang_data.h.
#   python3 tools/make_lang.py              checks and writes src/engine/lang_data.h
#   python3 tools/make_lang.py --controlla  only the checks (make test): exits with 1 on an error
#   python3 tools/make_lang.py --elenco     the texts the code passes to tr() or marks with N_()
#
# lang/testi.txt: one block per text, separated by blank lines; lines starting with # are
# comments. In the block one line per language, "xx text", in the order it en es pt zh ja hi: the
# it line is the key, that is the Italian text written exactly as in the code. Before the languages
# an optional line with the big faces the text is drawn in (@T25 @T32 @T38 @T44 @B22): for
# Chinese, Japanese and Hindi those faces have only the glyphs that are needed (make_font.py).
#
# Checks: every text of the code (tr("..."), tr_in(x, "..."), N_("...")) and every map name in
# maps/*.txt is in the table; the table has all the languages, no duplicate key, and every
# translation has the same %d, %s, %u as the Italian (in the same order, or all with the
# position: %1$d, %2$s...).
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGS = ["it", "en", "es", "pt", "zh", "ja", "hi"]
FACES = {"@T25", "@T32", "@T38", "@T44", "@B22"}
TABLE = os.path.join(ROOT, "lang", "testi.txt")
OUT = os.path.join(ROOT, "src", "engine", "lang_data.h")


class Text:
    def __init__(self, line):
        self.line = line
        self.faces = set()
        self.t = {}


def parse(path=TABLE):
    """Blocks of lang/testi.txt: list of Text, and errors (line, message)."""
    texts, errors = [], []
    cur = None
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\r")
        if line.startswith("#"):
            continue
        if not line.strip():
            if cur is not None:
                texts.append(cur)
                cur = None
            continue
        if cur is None:
            cur = Text(n)
        if line.startswith("@"):
            if cur.t:
                errors.append((n, "le facce (@...) vanno prima delle lingue"))
            for w in line.split():
                if w not in FACES:
                    errors.append((n, "faccia sconosciuta %s (si usano %s)" % (w, " ".join(sorted(FACES)))))
                cur.faces.add(w)
            continue
        m = re.match(r"^([a-z]{2}) (.*)$", line)
        if not m or m.group(1) not in LANGS:
            errors.append((n, "riga non valida (serve \"xx testo\", xx tra %s): %r" % (" ".join(LANGS), line[:60])))
            continue
        lang, text = m.group(1), m.group(2)
        if lang in cur.t:
            errors.append((n, "lingua %s due volte nello stesso blocco" % lang))
        if not cur.t and lang != "it":
            errors.append((n, "il blocco deve cominciare con la riga it"))
        if text != text.strip():
            errors.append((n, "spazi all'inizio o alla fine: %r" % text))
        if not text:
            errors.append((n, "testo vuoto"))
        cur.t[lang] = text
    if cur is not None:
        texts.append(cur)
    for t in texts:
        missing = [l for l in LANGS if l not in t.t]
        if missing:
            errors.append((t.line, "mancano le lingue %s (%r)" % (" ".join(missing), t.t.get("it", "")[:50])))
    seen = {}
    for t in texts:
        k = t.t.get("it")
        if k is None:
            continue
        if k in seen:
            errors.append((t.line, "chiave doppia %r (anche a riga %d)" % (k, seen[k])))
        seen[k] = t.line
    return texts, errors


# --- printf formats ------------------------------------------------------------------------------

# printf conversions as the game uses them (without the space flag: "il 40% più" is text, not "% p").
CONV = re.compile(r"%(?:(\d+)\$)?([-+#0]*)(\d+|\*)?(?:\.(\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGcsp%])")


def conversions(s):
    """[(position or None, type)] of the %... in s (no %%); a % that does not start a conversion is text."""
    out = []
    i = 0
    while True:
        j = s.find("%", i)
        if j < 0:
            return out
        m = CONV.match(s, j)
        if not m:
            i = j + 1
            continue
        if m.group(6) != "%":
            kind = {"i": "d"}.get(m.group(6), m.group(6))
            if kind in "dux":
                kind = "d" if kind == "d" else "u"
            out.append((int(m.group(1)) if m.group(1) else None, (m.group(5) or "") + kind))
        i = m.end()


def lone_percent(s):
    """The first % in s that does not start a conversion (nor %%), or None. In a text used as a
    printf format, printf would read it anyway ("50% di" becomes "% d", with the space flag)."""
    i = 0
    while True:
        j = s.find("%", i)
        if j < 0:
            return None
        m = CONV.match(s, j)
        if not m:
            return s[j:j + 4]
        i = m.end()


def check_format(it, tr):
    a, b = conversions(it), conversions(tr)
    if a is None:
        return "formato italiano non valido"
    if b is None:
        return "formato non valido"
    if any(p is not None for p, _ in a):
        return "nell'italiano niente posizioni (%1$d...)"
    want = [k for _, k in a]
    if all(p is None for p, _ in b):
        if [k for _, k in b] != want:
            return "servono %s nello stesso ordine (oppure con le posizioni %%1$...)" % " ".join("%" + k for k in want)
        return None
    if any(p is None for p, _ in b):
        return "con le posizioni (%1$d) le vogliono tutti i %"
    got = sorted(b)
    if got != [(i + 1, k) for i, k in enumerate(want)] or len(set(p for p, _ in b)) != len(b):
        return "posizioni sbagliate: servono %s" % " ".join("%%%d$%s" % (i + 1, k) for i, k in enumerate(want))
    return None


# --- texts in the code ---------------------------------------------------------------------------

LIT = r'"(?:[^"\\\n]|\\.)*"'
CALL = re.compile(r"\b(?:tr|N_)\(\s*((?:" + LIT + r"\s*)+)\)|\btr_in\([^,()]*,\s*((?:" + LIT + r"\s*)+)\)")
ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "0": "\0"}


def unescape(lits):
    out = b""
    for m in re.finditer(LIT, lits):
        body = m.group(0)[1:-1]
        i = 0
        while i < len(body):
            c = body[i]
            if c == "\\":
                d = body[i + 1]
                if d == "x":
                    h = re.match(r"[0-9a-fA-F]+", body[i + 2:]).group(0)
                    out += bytes([int(h, 16)])
                    i += 2 + len(h)
                    continue
                out += ESC[d].encode()
                i += 2
                continue
            out += c.encode("utf-8")
            i += 1
    return out.decode("utf-8")


def code_keys():
    """{text: [file:line, ...]} of the texts in the code."""
    keys = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.cpp"), recursive=True) + glob.glob(os.path.join(ROOT, "src", "**", "*.h"), recursive=True)):
        if path.endswith(("lang_data.h", "font_hud.h", "font_hud_data.h", "libretro.h")):
            continue
        src = open(path, encoding="utf-8").read()
        # comments do not count (but their lines do, for the messages)
        clean = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), src)
        for m in CALL.finditer(clean):
            lits = m.group(1) or m.group(2)
            text = unescape(lits)
            line = clean.count("\n", 0, m.start()) + 1
            keys.setdefault(text, []).append("%s:%d" % (os.path.relpath(path, ROOT), line))
    return keys


NAME_MAX = 39                         # bytes of a map name (Level::name in level.h, minus the 0)


def map_names(errors=None):
    """{name: file:line} of the built-in maps, read like level.cpp does: only the lines before ---,
    the key in lowercase or uppercase, surrounding spaces removed. A name longer than NAME_MAX bytes
    would be truncated by the game (which would then no longer find it in the table): error."""
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "maps", "*.txt"))):
        lines = open(path, encoding="utf-8").read().split("\n")
        if "---" not in (l.strip() for l in lines):          # not a map (LEGGIMI.txt)
            continue
        for n, line in enumerate(lines, 1):
            if line.strip() == "---":
                break
            m = re.match(r"^[ \t]*nome[ \t]*:[ \t]*(.*?)[ \t\r]*$", line, re.I | re.A)
            if m:
                where = "%s:%d" % (os.path.relpath(path, ROOT), n)
                if errors is not None and len(m.group(1).encode("utf-8")) > NAME_MAX:
                    errors.append((0, "nome di mappa troppo lungo %r (%s): al massimo %d byte" % (m.group(1), where, NAME_MAX)))
                out[m.group(1)] = where
    return out


def check(texts, errors):
    by_key = {t.t["it"]: t for t in texts if "it" in t.t}
    for t in texts:
        it = t.t.get("it")
        if it is None:
            continue
        for l in LANGS[1:]:
            if l in t.t:
                e = check_format(it, t.t[l])
                if e:
                    errors.append((t.line, "%s, %s: %s (%r)" % (it[:40], l, e, t.t[l][:60])))
        hi = t.t.get("hi", "")
        for m in re.finditer(r"%(?:\d+\$)?s", hi):
            near = hi[m.start() - 1:m.start()] + hi[m.end():m.end() + 1]
            if any("\u0900" <= c <= "\u097f" for c in near):
                errors.append((t.line, "%s, hi: %%s attaccato a una parola (%r): le parole hindi si compongono intere, serve uno spazio"
                               % (it[:40], hi[max(0, m.start() - 6):m.end() + 6])))
        if conversions(it):                  # used as a format: a lone % is written %%
            for l in LANGS:
                p = lone_percent(t.t.get(l, ""))
                if p is not None:
                    errors.append((t.line, "%s, %s: %r in un testo con %%d o %%s: un %% da solo si scrive %%%%" % (it[:40], l, p)))
    keys = code_keys()
    for k, where in sorted(keys.items()):
        if k and k not in by_key:
            errors.append((0, "manca nella tabella %r (%s)" % (k, ", ".join(where[:3]))))
    names = map_names(errors)
    for k, where in names.items():
        if k not in by_key:
            errors.append((0, "manca nella tabella il nome di mappa %r (%s)" % (k, where)))
    unused = [k for k in by_key if k not in keys and k not in names]
    return unused


# --- uscita ----------------------------------------------------------------------------------

def fnv(s):
    h = 2166136261
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def c_str(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "?":
            out.append("?")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))     # ottale: al massimo 3 cifre, non si attacca a quelle dopo
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


BIG = ["@B22", "@T25", "@T32", "@T38", "@T44"]       # bit delle facce grandi in LANG_BIG


def generate(texts):
    rows = sorted(((fnv(t.t["it"]), t) for t in texts), key=lambda r: (r[0], r[1].t["it"]))
    o = ["// Generato da tools/make_lang.py da lang/testi.txt: non modificare a mano.",
         "#pragma once",
         "#include <stdint.h>",
         "#define LANG_NTEXTS %d" % len(rows),
         "// Hash FNV-1a dell'italiano, in ordine crescente; per ogni testo le 7 lingue (%s)." % " ".join(LANGS),
         "static const uint32_t LANG_HASH[LANG_NTEXTS] = {"]
    for i in range(0, len(rows), 8):
        o.append("  " + ", ".join("0x%08xu" % h for h, _ in rows[i:i + 8]) + ",")
    o.append("};")
    o.append("static const char* const LANG_TEXT[LANG_NTEXTS][%d] = {" % len(LANGS))
    for _, t in rows:
        o.append("  { " + ", ".join(c_str(t.t[l]) for l in LANGS) + " },")
    o.append("};")
    o.append("// Facce grandi in cui si disegna il testo (prove): bit 0 B22, 1 T25, 2 T32, 3 T38, 4 T44.")
    o.append("static const uint8_t LANG_BIG[LANG_NTEXTS] = {")
    bits = [sum(1 << i for i, f in enumerate(BIG) if f in t.faces) for _, t in rows]
    for i in range(0, len(bits), 32):
        o.append("  " + ", ".join(str(b) for b in bits[i:i + 32]) + ",")
    o.append("};")
    return "\n".join(o) + "\n"


def write(texts, path=OUT):
    data = generate(texts)
    old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
    if old != data:
        with open(path, "w", encoding="utf-8") as f:
            f.write(data)
    return len(texts)


def main():
    args = sys.argv[1:]
    if args == ["--elenco"]:
        for k, where in sorted(code_keys().items()):
            print("%-60s %s" % (k, where[0]))
        return 0
    texts, errors = parse()
    unused = check(texts, errors)
    for line, msg in errors:
        sys.stderr.write("lang/testi.txt:%s: %s\n" % (line, msg) if line else "make_lang: %s\n" % msg)
    if errors:
        sys.stderr.write("make_lang: %d errori\n" % len(errors))
        return 1
    if unused:
        sys.stderr.write("make_lang: nella tabella ma non nel codice: %s\n" % ", ".join(repr(k) for k in unused[:20]))
    if args == ["--controlla"]:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else None
        if old != generate(texts):
            sys.stderr.write("make_lang: src/engine/lang_data.h non e' aggiornato: python3 tools/make_lang.py\n")
            return 1
        print("make_lang: %d testi in %d lingue, tutto a posto" % (len(texts), len(LANGS)))
        return 0
    n = write(texts)
    sys.stderr.write("make_lang: %d testi in %d lingue -> %s\n" % (n, len(LANGS), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
