#!/usr/bin/env python3
# Testi del gioco nelle 7 lingue: legge lang/testi.txt, lo controlla e genera src/lang_data.h.
#   python3 tools/make_lang.py              controlla e scrive src/lang_data.h
#   python3 tools/make_lang.py --controlla  solo i controlli (make test): esce con 1 se c'e' un errore
#   python3 tools/make_lang.py --elenco     i testi che il codice passa a tr() o segna con N_()
#
# lang/testi.txt: un blocco per testo, separati da righe vuote; le righe che cominciano con # sono
# commenti. Nel blocco una riga per lingua, "xx testo", nell'ordine it en es pt zh ja hi: la riga
# it e' la chiave, cioe' il testo italiano scritto uguale nel codice. Prima delle lingue una riga
# facoltativa con le facce grandi in cui il testo si disegna (@T25 @T32 @T38 @T44 @B22): per
# cinese, giapponese e hindi quelle facce hanno solo i caratteri che servono (make_font.py).
#
# Controlli: ogni testo del codice (tr("..."), tr_in(x, "..."), N_("...")) e ogni nome di mappa
# di maps/*.txt e' nella tabella; nella tabella ci sono tutte le lingue, nessuna chiave doppia, e
# ogni traduzione ha gli stessi %d, %s, %u dell'italiano (nello stesso ordine, oppure tutti con la
# posizione: %1$d, %2$s...).
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGS = ["it", "en", "es", "pt", "zh", "ja", "hi"]
FACES = {"@T25", "@T32", "@T38", "@T44", "@B22"}
TABLE = os.path.join(ROOT, "lang", "testi.txt")
OUT = os.path.join(ROOT, "src", "lang_data.h")


class Text:
    def __init__(self, line):
        self.line = line
        self.faces = set()
        self.t = {}


def parse(path=TABLE):
    """Blocchi di lang/testi.txt: elenco di Text, ed errori (riga, messaggio)."""
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


# --- formati di printf ---------------------------------------------------------------------------

# Conversioni di printf come le usa il gioco (senza il flag spazio: "il 40% più" e' testo, non "% p").
CONV = re.compile(r"%(?:(\d+)\$)?([-+#0]*)(\d+|\*)?(?:\.(\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGcsp%])")


def conversions(s):
    """[(posizione o None, tipo)] dei %... di s (senza %%); un % che non comincia una conversione e' testo."""
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


# --- testi del codice ----------------------------------------------------------------------------

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
    """{testo: [file:riga, ...]} dei testi del codice."""
    keys = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "*.cpp")) + glob.glob(os.path.join(ROOT, "src", "*.h"))):
        if path.endswith(("lang_data.h", "font_hud.h", "font_hud_data.h", "libretro.h")):
            continue
        src = open(path, encoding="utf-8").read()
        # i commenti non contano (ma le righe si', per i messaggi)
        clean = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), src)
        for m in CALL.finditer(clean):
            lits = m.group(1) or m.group(2)
            text = unescape(lits)
            line = clean.count("\n", 0, m.start()) + 1
            keys.setdefault(text, []).append("%s:%d" % (os.path.relpath(path, ROOT), line))
    return keys


def map_names():
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "maps", "*.txt"))):
        lines = open(path, encoding="utf-8").read().split("\n")
        if "---" not in (l.strip() for l in lines):          # non e' una mappa (LEGGIMI.txt)
            continue
        for n, line in enumerate(lines, 1):
            m = re.match(r"^\s*nome\s*:\s*(.*?)\s*$", line)
            if m:
                out[m.group(1)] = "%s:%d" % (os.path.relpath(path, ROOT), n)
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
    keys = code_keys()
    for k, where in sorted(keys.items()):
        if k and k not in by_key:
            errors.append((0, "manca nella tabella %r (%s)" % (k, ", ".join(where[:3]))))
    names = map_names()
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
            out.append("\\x%02x" % ord(ch))
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
            sys.stderr.write("make_lang: src/lang_data.h non e' aggiornato: python3 tools/make_lang.py\n")
            return 1
        print("make_lang: %d testi in %d lingue, tutto a posto" % (len(texts), len(LANGS)))
        return 0
    n = write(texts)
    sys.stderr.write("make_lang: %d testi in %d lingue -> %s\n" % (n, len(LANGS), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
