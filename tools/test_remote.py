#!/usr/bin/env python3
# Pupazzi - prove dei comandi dalla rete (Remote RetroPad) con il frontend di prova.
#
#   python3 tools/test_remote.py [--host build/native/host] [--core build/native/pupazzi_libretro.so]
#                                [--prima CORE_DI_PRIMA.so]
#
# Il frontend di prova fa girare il gioco (mappa 1, 2 giocatori) e da qui si mandano i messaggi
# del Remote RetroPad (tools/remote_send.py) alle porte del gioco su 127.0.0.1 (porte libere scelte
# a caso, PUPAZZI_REMOTO_PORTA). Per sapere prima di quale fotogramma arrivano, il frontend si
# ferma (SIGSTOP) mentre si mandano e riparte subito; nel tratto col tremolio invece si manda
# mentre gira. Con PUPAZZI_TRACE il core scrive l'impronta della partita a ogni fotogramma e lo
# stato arrivato dalla rete ("rete F giocatore G: ..."): da quelle righe si rifanno gli stessi
# comandi come pad del frontend (--in) e si rigioca. Le impronte devono essere identiche
# fotogramma per fotogramma: nessun ritardo e nessun movimento fantasma dopo il rilascio.
#
#  a) raffica di 200 messaggi delle levette: tutti nel fotogramma dopo l'invio, vale l'ultimo;
#  b) dopo un tratto di tremolio continuo (piu' messaggi che fotogrammi) levetta tenuta 2 secondi e
#     lasciata: arriva nel fotogramma dopo l'invio (al massimo due) e il personaggio si ferma come
#     con il pad;
#  c) tasti premuti e lasciati: la carta scelta con A dalla rete, salto, colpi; pad del frontend e
#     rete sullo stesso giocatore; messaggi di misura sbagliata; un'altra console sulla stessa
#     porta; gioco fermo per piu' di mezzo secondo (i comandi in coda si scartano);
#  d) senza messaggi: stessa partita con e senza porte aperte (con --prima anche uguale al core di
#     prima, con i comandi scritti e con i giocatori automatici).
import argparse
import os
import random
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True          # niente __pycache__ in tools/
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import remote_send as rs  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACE_RE = re.compile(r"^traccia (\d+) ([0-9a-f]+)$")
NET_RE = re.compile(r"^rete (\d+) giocatore (\d+): (\d+) messaggi, tasti ([0-9a-f]+), levette (-?\d+) (-?\d+) (-?\d+) (-?\d+)$")
GAME_ENV = {"PUPAZZI_TRACE": "1", "PUPAZZI_RENDERER": "software", "PUPAZZI_RES": "320", "PUPAZZI_MAPPA": "1",
            "PUPAZZI_GIOCATORI": "2", "PUPAZZI_HUD": "0"}
# Sotto queste soglie le levette sono dentro le zone morte del core (sinistra per asse, destra
# circolare, vedi read_input): per il gioco valgono zero. Il tremolio delle prove resta sotto.
DEAD_SMALL = 6000
JITTER = 2000

failures = []


def check(cond, what):
    print(("ok      " if cond else "ERRORE  ") + what)
    if not cond:
        failures.append(what)
    return cond


def free_base():
    """Quattro porte UDP consecutive libere (aperte senza SO_REUSEADDR, come fa il core)."""
    for _ in range(500):
        base = random.randrange(20000, 32000, 4)    # sotto le porte che il sistema da' alle connessioni in uscita
        socks = []
        try:
            for p in range(4):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                socks.append(s)
                s.bind(("0.0.0.0", base + p))
            return base
        except OSError:
            continue
        finally:
            for s in socks:
                s.close()
    sys.exit("test_remote: nessuna porta libera")


class Host:
    """Il frontend di prova in un processo a parte, con il registro letto mentre gira."""

    def __init__(self, args, frames, script="", env=None, core=None):
        self.dir = tempfile.mkdtemp(prefix="rete_", dir=args.tmp)
        e = os.environ.copy()
        e.update(GAME_ENV)
        e.update(env or {})
        e["PUPAZZI_OUT"] = self.dir
        e["PUPAZZI_SALVATAGGI"] = self.dir
        cmd = [args.host, core or args.core, "--frames", str(frames), "--quiet"]
        if script:
            cmd += ["--in", script]
        self.frames = frames
        self.p = subprocess.Popen(cmd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.path = os.path.join(self.dir, "pupazzi_log.txt")
        self.f = None
        self.buf = ""
        self.lines = []
        self.frame = -1         # ultimo fotogramma scritto nel registro ("traccia")
        self.next_ok = 0        # prima di cosi' non si manda la volta dopo (vedi window)
        self.stopped = False

    def read(self):
        if self.f is None:
            try:
                self.f = open(self.path, "r", errors="replace")
            except FileNotFoundError:
                return
        data = self.f.read()
        if not data:
            return
        parts = (self.buf + data).split("\n")
        self.buf = parts.pop()
        for line in parts:
            self.lines.append(line)
            m = TRACE_RE.match(line)
            if m:
                self.frame = int(m.group(1))

    def wait_frame(self, f, timeout=60.0):
        t0 = time.monotonic()
        while self.frame < f:
            self.read()
            if self.frame >= f:
                break
            if self.p.poll() is not None:
                self.read()
                if self.frame < f:
                    raise RuntimeError("il frontend si e' chiuso al fotogramma %d (aspettavo il %d)" % (self.frame, f))
            if time.monotonic() - t0 > timeout:
                raise RuntimeError("fotogramma %d non arrivato" % f)
            time.sleep(0.0003)

    def stop(self):
        os.kill(self.p.pid, signal.SIGSTOP)
        self.stopped = True
        t0 = time.monotonic()
        while True:
            with open("/proc/%d/stat" % self.p.pid) as st:
                state = st.read().rsplit(")", 1)[1].split()[0]
            if state in ("T", "t"):
                break
            if time.monotonic() - t0 > 5.0:
                raise RuntimeError("il frontend non si ferma")
            time.sleep(0.0002)
        self.read()
        return self.frame

    def cont(self):
        os.kill(self.p.pid, signal.SIGCONT)
        self.stopped = False

    def window(self, target, send, hold=0.0):
        """Ferma il frontend dal fotogramma target in poi, manda, lo fa ripartire. Restituisce
        l'ultimo fotogramma scritto prima dell'invio: cio' che si e' mandato vale dal successivo
        (o da quello dopo, se il frontend si e' fermato a meta' della lettura dei pad)."""
        self.wait_frame(max(target, self.next_ok))
        last = self.stop()
        try:
            if hold > 0:
                time.sleep(hold)
            send()
        finally:
            self.cont()
        self.next_ok = last + 3     # cosi' ogni invio cade in un fotogramma suo
        return last

    def finish(self):
        if self.stopped:
            self.cont()
        out = self.p.communicate(timeout=300)[0]
        self.read()
        if self.buf:
            self.lines.append(self.buf)
            self.buf = ""
        if self.p.returncode != 0 or "host: fotogrammi=" not in out:
            raise RuntimeError("il frontend e' finito male (%s):\n%s" % (self.p.returncode, out))
        if self.f:
            self.f.close()
        return self

    def kill(self):
        if self.p.poll() is None:
            try:
                os.kill(self.p.pid, signal.SIGCONT)
                self.p.kill()
                self.p.wait(timeout=10)
            except OSError:
                pass

    def traces(self):
        t = {}
        for line in self.lines:
            m = TRACE_RE.match(line)
            if m:
                t[int(m.group(1))] = m.group(2)
        return t

    def net(self):
        """Righe "rete": (fotogramma, giocatore 1-4, messaggi, tasti, [levette])."""
        out = []
        for line in self.lines:
            m = NET_RE.match(line)
            if m:
                out.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4), 16),
                            [int(m.group(k)) for k in range(5, 9)]))
        return out

    def has(self, text):
        return any(text in line for line in self.lines)

    def find(self, prefix):
        return [line for line in self.lines if line.startswith(prefix)]


def run_plain(args, frames, script, env=None, core=None):
    h = Host(args, frames, script, env, core)
    try:
        return h.finish()
    except Exception:
        h.kill()
        raise


def same_traces(a, b, frames, what):
    ta, tb = a.traces(), b.traces()
    missing = [f for f in range(frames) if f not in ta or f not in tb]
    if missing:
        return check(False, "%s: impronte mancanti (dal fotogramma %d)" % (what, missing[0]))
    diff = [f for f in range(frames) if ta[f] != tb[f]]
    return check(not diff, "%s: impronta della partita identica in tutti i %d fotogrammi%s" %
                 (what, frames, "" if not diff else " (diversa dal fotogramma %d)" % diff[0]))


# --- comandi rifatti come pad del frontend ------------------------------------

def axis_text(a):
    """Testo per --in che il frontend di prova trasforma esattamente nell'intero a."""
    v = rs.f32(a / 32767.0)
    for _ in range(200):
        txt = "%.9g" % v
        got = rs.axis_value(float(txt))
        if got == a:
            return txt
        bits = struct.unpack("<I", struct.pack("<f", v))[0]
        up = (got < a) == (v >= 0.0)        # verso valori piu' grandi in modulo?
        bits = bits + 1 if up else bits - 1
        v = struct.unpack("<f", struct.pack("<I", bits))[0]
    raise RuntimeError("valore %d non rappresentabile con --in" % a)


def merged_states(local_script, net_lines, frames):
    """Per ogni fotogramma e giocatore cio' che arriva al gioco: pad del frontend (come in
    tools/host.cpp) e rete insieme come in read_input (tasti in OR, per asse il piu' lontano dal
    centro), con le levette dentro le zone morte a zero."""
    events = [rs.parse_event(t) for t in local_script.split(",")] if local_script else []
    for e in events:
        e["t0"], e["t1"] = int(e["t0"]), int(e["t1"])
    by_player = {p: sorted((f, b, ax) for (f, g, n, b, ax) in net_lines if g == p + 1) for p in range(4)}
    out = [[None] * frames for _ in range(4)]
    for p in range(4):
        net = (0, [0, 0, 0, 0])
        k = 0
        lst = by_player[p]
        for f in range(frames):
            while k < len(lst) and lst[k][0] <= f:
                net = (lst[k][1], lst[k][2])
                k += 1
            lb, lax = rs.state_at(events, f, port=p)
            btn = lb | net[0]
            ax = [net[1][a] if abs(net[1][a]) > abs(lax[a]) else lax[a] for a in range(4)]
            for a in (0, 1):
                if abs(ax[a]) < DEAD_SMALL:
                    ax[a] = 0
            if ax[2] * ax[2] + ax[3] * ax[3] < DEAD_SMALL * DEAD_SMALL:
                ax[2] = ax[3] = 0
            out[p][f] = (btn, tuple(ax))
    return out


def script_from_states(states, frames):
    ev = []
    for p in range(4):
        st = states[p]
        for bit in range(16):
            f = 0
            while f < frames:
                if st[f][0] >> bit & 1:
                    g = f
                    while g + 1 < frames and st[g + 1][0] >> bit & 1:
                        g += 1
                    ev.append("%d:%s@%d-%d" % (p, rs.BUTTONS[bit], f, g))
                    f = g + 1
                else:
                    f += 1
        for a in range(4):
            f = 0
            while f < frames:
                v = st[f][1][a]
                if v:
                    g = f
                    while g + 1 < frames and st[g + 1][1][a] == v:
                        g += 1
                    ev.append("%d:%s=%s@%d-%d" % (p, rs.AXES[a], axis_text(v), f, g))
                    f = g + 1
                else:
                    f += 1
    script = ",".join(ev)
    if len(ev) > 250 or len(script) > 4000:
        raise RuntimeError("troppi eventi per il frontend di prova (%d, %d caratteri)" % (len(ev), len(script)))
    return script


def replay_check(args, run, local_script, frames, what):
    """Rigioca con il pad del frontend gli stessi comandi arrivati dalla rete (e i suoi):
    stessa partita, fotogramma per fotogramma. Senza i comandi dalla rete deve cambiare."""
    states = merged_states(local_script, run.net(), frames)
    script = script_from_states(states, frames)
    replay = run_plain(args, frames, script, {"PUPAZZI_REMOTO": "0"})
    ok = same_traces(run, replay, frames, what + ", rigiocata con il pad del frontend")
    alone = run_plain(args, frames, local_script, {"PUPAZZI_REMOTO": "0"})
    ta, tb = run.traces(), alone.traces()
    check(any(ta.get(f) != tb.get(f) for f in range(frames)), what + ": senza i comandi dalla rete la partita e' diversa")
    return ok


def applied(run, player, last, what, n=None, btn=None, ax=None):
    """La riga "rete" del giocatore nel fotogramma dopo l'invio (al massimo due dopo)."""
    rows = [r for r in run.net() if r[1] == player and last < r[0] <= last + 2]
    if not rows:
        return check(False, "%s: nessuna riga 'rete' per il giocatore %d nei fotogrammi %d-%d" % (what, player, last + 1, last + 2))
    f, _, got_n, got_b, got_ax = rows[0]
    good = len(rows) == 1 and (n is None or got_n == n) and (btn is None or got_b == btn) and (ax is None or got_ax == list(ax))
    return check(good, "%s: arrivato al fotogramma %d (inviato dopo il %d, %d dopo): %d messaggi, tasti %04x, levette %s%s" %
                 (what, f, last, f - last, got_n, got_b, " ".join(map(str, got_ax)),
                  "" if good else " -- attesi %s messaggi, tasti %s, levette %s, una sola riga (righe: %d)" %
                  (n, "%04x" % btn if btn is not None else "-", ax, len(rows))))


BTN = {name: 1 << i for i, name in enumerate(rs.BUTTONS)}


# --- prove ------------------------------------------------------------------------

def test_main(args):
    print("--- comandi dalla rete durante la partita (a, b, c)")
    frames = 540
    base = free_base()
    # Pad del frontend: G1 sceglie la carta; G2 tiene la levetta a sinistra e R mentre arriva altro dalla rete.
    local = "0:A@5-7,1:lx=-0.5@400-470,1:R@400-470"
    h = Host(args, frames, local, {"PUPAZZI_REMOTO_PORTA": str(base)})
    try:
        sa, sb, sc = (socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3))
        pad2 = rs.RemotePad("127.0.0.1", base + 1, sa)
        pad2b = rs.RemotePad("127.0.0.1", base + 1, sb)     # un'altra console (o il Remote RetroPad ripartito)
        pad1 = rs.RemotePad("127.0.0.1", base, sc)
        sent2 = [0]
        marks = {}

        def send2(pad, btn=None, axes=None):
            n = pad.set(btn, axes)
            sent2[0] += n
            return n

        # c) G2 sceglie la carta con A dalla rete
        marks["A"] = h.window(8, lambda: send2(pad2, BTN["A"]))
        marks["A0"] = h.window(12, lambda: send2(pad2, 0))
        # b) tremolio continuo su tutte e quattro le levette, mandato mentre il gioco gira
        h.wait_frame(45)
        jit0, jit_frames0 = sent2[0], h.frame
        while h.frame < 105:
            send2(pad2, None, [random.randint(-JITTER, JITTER) for _ in range(4)])
            h.read()
            time.sleep(0.0005)
        jitter_msgs, jitter_frames = sent2[0] - jit0, h.frame - jit_frames0
        h.next_ok = h.frame + 4     # gli ultimi del tremolio letti prima della levetta tenuta
        hold_ax = [32767] + pad2.ax[1:]
        marks["tieni"] = h.window(105, lambda: send2(pad2, None, hold_ax))
        marks["lascia"] = h.window(marks["tieni"] + 120, lambda: send2(pad2, None, [0, 0, 0, 0]))
        # a) raffica di 200 messaggi delle levette, con byte di riempimento a caso: vale l'ultimo per asse
        burst_final = [-32767, 16383, 32767, 0]

        def burst():
            for k in range(196):
                a = random.randrange(4)
                pad2.raw(rs.ANALOG, a // 2, a % 2, random.randint(-32768, 32767), pad=os.urandom(2))
            for a in range(4):
                pad2.raw(rs.ANALOG, a // 2, a % 2, burst_final[a], pad=os.urandom(2))
            pad2.ax = list(burst_final)
            sent2[0] += 200
        marks["raffica"] = h.window(245, burst)
        marks["raffica0"] = h.window(265, lambda: send2(pad2, None, [0, 0, 0, 0]))

        # misura sbagliata (ignorati) e messaggi giusti ma che non toccano nulla (contati)
        def odd():
            for size in (0, 19, 21, 40):
                sa.sendto(os.urandom(size), ("127.0.0.1", base + 1))
            for dev, idx, id_ in ((rs.JOYPAD, 0, 16), (rs.JOYPAD, 0, -1), (rs.ANALOG, 2, 0), (rs.ANALOG, 0, 2), (2, 0, 0)):
                pad2.raw(dev, idx, id_, 1)
            sent2[0] += 5
        marks["strani"] = h.window(285, odd)
        # c) salto (R2) e colpi (R)
        marks["R2R"] = h.window(300, lambda: send2(pad2, BTN["R2"] | BTN["R"]))
        marks["R2off"] = h.window(306, lambda: send2(pad2, BTN["R"]))
        marks["Roff"] = h.window(330, lambda: send2(pad2, 0))
        # un'altra console sulla stessa porta: si riparte da fermi, poi vale il suo stato
        marks["lx"] = h.window(345, lambda: send2(pad2, None, [32767, 0, 0, 0]))
        marks["altra"] = h.window(355, lambda: send2(pad2b, None, [0, 32767, 0, 0]))
        marks["altra0"] = h.window(365, lambda: send2(pad2b, None, [0, 0, 0, 0]))
        # pad del frontend (levetta a sinistra a meta', R) e rete (levetta a destra, L) insieme
        marks["misti"] = h.window(420, lambda: send2(pad2b, BTN["L"], [32767, 0, 0, 0]))
        marks["misti0"] = h.window(440, lambda: send2(pad2b, 0, [0, 0, 0, 0]))
        # giocatore 1 dalla sua porta: mira a destra
        marks["g1"] = h.window(480, lambda: pad1.set(None, [0, 0, 32767, 0]))
        marks["g1off"] = h.window(500, lambda: pad1.set(None, [0, 0, 0, 0]))
        h.finish()
    except Exception:
        h.kill()
        raise

    check(h.has("comandi dalle altre console: porte %d-%d aperte (giocatori 1-4)" % (base, base + 3)), "porte aperte nel registro")
    check(h.has("giocatore 2: comandi dalla rete ricevuti (da 127.0.0.1)"), "primo comando del giocatore 2 nel registro")
    check(h.has("giocatore 1: comandi dalla rete ricevuti (da 127.0.0.1)"), "primo comando del giocatore 1 nel registro")
    check(any(re.match(r"^G2 \(.*\) prende la carta", line) for line in h.lines), "c) G2 sceglie la carta con A dalla rete")
    applied(h, 2, marks["A"], "c) A premuto", n=1, btn=BTN["A"])
    applied(h, 2, marks["A0"], "c) A lasciato", n=1, btn=0)
    rows = [r for r in h.net() if r[1] == 2 and marks["A0"] + 2 < r[0] <= marks["tieni"]]
    per_frame = [r[2] for r in rows]
    print("        tremolio: %d messaggi in %d fotogrammi (al massimo %d in uno)" % (jitter_msgs, jitter_frames, max(per_frame or [0])))
    check(sum(per_frame) == jitter_msgs, "b) tremolio: tutti i %d messaggi letti man mano (letti %d)" % (jitter_msgs, sum(per_frame)))
    check(jitter_msgs > jitter_frames + 20, "b) tremolio: piu' messaggi che fotogrammi (%d in %d)" % (jitter_msgs, jitter_frames))
    applied(h, 2, marks["tieni"], "b) levetta tenuta, dopo il tremolio", n=1, ax=hold_ax)
    applied(h, 2, marks["lascia"], "b) levetta lasciata dopo 2 secondi", ax=[0, 0, 0, 0])
    applied(h, 2, marks["raffica"], "a) raffica di 200 messaggi", n=200, btn=0, ax=burst_final)
    applied(h, 2, marks["raffica0"], "a) dopo la raffica, levette al centro", n=3, ax=[0, 0, 0, 0])
    applied(h, 2, marks["strani"], "messaggi senza effetto: contati, stato invariato", n=5, btn=0, ax=[0, 0, 0, 0])
    applied(h, 2, marks["R2R"], "c) R2 e R premuti", n=2, btn=BTN["R2"] | BTN["R"])
    applied(h, 2, marks["R2off"], "c) R2 lasciato", n=1, btn=BTN["R"])
    applied(h, 2, marks["Roff"], "c) R lasciato", n=1, btn=0)
    applied(h, 2, marks["altra"], "un'altra console sulla stessa porta: si riparte da fermi", n=1, ax=[0, 32767, 0, 0])
    check(h.has("giocatore 2: comandi dalla rete ora da 127.0.0.1:%d (prima da 127.0.0.1:%d)" % (sb.getsockname()[1], sa.getsockname()[1])),
          "cambio di console nel registro")
    applied(h, 2, marks["misti"], "pad del frontend e rete insieme", n=2, btn=BTN["L"], ax=[32767, 0, 0, 0])
    applied(h, 1, marks["g1"], "giocatore 1 dalla porta %d" % base, n=1, ax=[0, 0, 32767, 0])
    stats2 = h.find("comandi dalla rete giocatore 2: ")
    nframes2 = len([r for r in h.net() if r[1] == 2 and r[2] > 0])
    want = ("comandi dalla rete giocatore 2: %d messaggi in %d fotogrammi, al massimo 200 in un fotogramma" % (sent2[0], nframes2))
    check(len(stats2) == 1 and stats2[0].startswith(want) and "; 4 di misura sbagliata ignorati" in stats2[0]
          and "; 1 cambi di console o di Remote RetroPad" in stats2[0],
          "riga di fine sessione: %s" % (stats2[0] if stats2 else "manca"))
    stats1 = h.find("comandi dalla rete giocatore 1: ")
    check(len(stats1) == 1 and stats1[0].startswith("comandi dalla rete giocatore 1: 2 messaggi in 2 fotogrammi, al massimo 1 in un fotogramma"),
          "riga di fine sessione: %s" % (stats1[0] if stats1 else "manca"))
    replay_check(args, h, local, frames, "a, b, c) partita con i comandi dalla rete")


def test_stall(args):
    print("--- gioco fermo piu' di mezzo secondo (menu di RetroArch)")
    frames = 300
    local = "0:A@5-7,1:A@5-7"
    for attempt in range(3):
        base = free_base()
        h = Host(args, frames, local, {"PUPAZZI_REMOTO_PORTA": str(base)})
        try:
            pad2 = rs.RemotePad("127.0.0.1", base + 1)
            m_hold = h.window(60, lambda: pad2.set(None, [32767, 0, 0, 0]))
            # fermo 0,7 secondi; nel frattempo la console-controller manda R premuto e levetta lasciata
            m_stall = h.window(100, lambda: pad2.set(BTN["R"], [0, 0, 0, 0]), hold=0.7)
            m_rel = h.window(140, lambda: pad2.set(0))
            h.finish()
        except Exception:
            h.kill()
            raise
        # Fermato proprio dentro la lettura della rete, dopo che il core ha preso l'ora: per lui
        # la pausa non c'e' stata e i comandi valgono (raro). Si rifa'.
        late = [r for r in h.net() if r[1] == 2 and m_stall < r[0] <= m_stall + 2 and r[2] == 2]
        if not late:
            break
        print("        fermato a meta' della lettura della rete: si rifa'")
    applied(h, 2, m_hold, "levetta tenuta", n=1, ax=[32767, 0, 0, 0])
    applied(h, 2, m_stall, "dopo la pausa: comandi in coda scartati, si riparte da fermi", n=0, btn=0, ax=[0, 0, 0, 0])
    stall = [line for line in h.lines if line.startswith("comandi dalla rete: gioco fermo per ")]
    check(len(stall) == 1 and "2 comandi arrivati nel frattempo scartati: giocatori 2 ripartono da fermi" in stall[0],
          "riga della pausa: %s" % (stall[0] if stall else "manca"))
    applied(h, 2, m_rel, "R lasciato dopo la pausa (stato gia' a riposo)", n=1, btn=0, ax=[0, 0, 0, 0])
    stats = h.find("comandi dalla rete giocatore 2: ")
    check(len(stats) == 1 and "; 2 scartati dopo una pausa del gioco" in stats[0], "riga di fine sessione: %s" % (stats[0] if stats else "manca"))
    replay_check(args, h, local, frames, "pausa")


def test_quiet(args):
    print("--- senza messaggi dalla rete (d)")
    frames = 600
    script = ("0:A@5-7,1:A@5-7,0:lx=1@40-160,0:ly=-0.5@90-200,1:ly=-1@50-200,1:lx=0.5@120-260,0:R@60-90,"
              "1:R2@100-104,0:rx=0.5@120-300,0:ry=-0.25@150-240,1:rx=-1@200-330,1:L@220-226,0:L2@250-252,1:X@300-302")
    base = free_base()
    a = run_plain(args, frames, script, {"PUPAZZI_REMOTO_PORTA": str(base)})
    b = run_plain(args, frames, script, {"PUPAZZI_REMOTO": "0"})
    check(a.has("comandi dalle altre console: porte %d-%d aperte" % (base, base + 3)) and a.has("comandi dalla rete: nessun messaggio arrivato")
          and not a.net(), "porte aperte, nessun messaggio, nessuna riga 'rete'")
    check(b.has("comandi dalle altre console: spenti (PUPAZZI_REMOTO=0)"), "PUPAZZI_REMOTO=0: porte non aperte")
    same_traces(a, b, frames, "d) comandi scritti, porte aperte e PUPAZZI_REMOTO=0")
    # Porte gia' occupate: il core lo scrive e gioca lo stesso.
    busy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    busy.bind(("0.0.0.0", base + 2))
    c = run_plain(args, frames, script, {"PUPAZZI_REMOTO_PORTA": str(base)})
    busy.close()
    line = [x for x in c.lines if x.startswith("comandi dalle altre console:")]
    check(len(line) == 1 and "porta %d (giocatore 3) non disponibile: Address already in use" % (base + 2) in line[0],
          "porta occupata: %s" % (line[0] if line else "manca"))
    same_traces(a, c, frames, "d) con una porta occupata")
    if args.prima:
        old = run_plain(args, frames, script, core=args.prima)
        same_traces(a, old, frames, "d) comandi scritti, uguale al core di prima")
        bots = {"PUPAZZI_BOT": "2", "PUPAZZI_REMOTO_PORTA": str(base)}
        n = 1500
        same_traces(run_plain(args, n, "", bots), run_plain(args, n, "", {"PUPAZZI_BOT": "2"}, core=args.prima), n,
                    "d) 2 giocatori automatici, uguale al core di prima")


def main():
    ap = argparse.ArgumentParser(description="Prove dei comandi dalla rete (Remote RetroPad) con il frontend di prova.")
    ap.add_argument("--host", default=os.path.join(ROOT, "build/native/host"))
    ap.add_argument("--core", default=os.path.join(ROOT, "build/native/pupazzi_libretro.so"))
    ap.add_argument("--prima", help="core di prima, da confrontare senza messaggi dalla rete")
    ap.add_argument("--seme", type=int, default=None, help="seme dei numeri casuali (tremolio, raffica)")
    args = ap.parse_args()
    seed = args.seme if args.seme is not None else random.randrange(1 << 30)
    random.seed(seed)
    print("seme %d" % seed)
    for f in (args.host, args.core):
        if not os.path.exists(f):
            sys.exit("test_remote: manca %s (prima: make)" % f)
    with tempfile.TemporaryDirectory(prefix="pupazzi_test_remote_") as tmp:
        args.tmp = tmp
        test_main(args)
        test_stall(args)
        test_quiet(args)
    if failures:
        print("%d prove fallite" % len(failures))
        sys.exit(1)
    print("tutte le prove dei comandi dalla rete passate")


if __name__ == "__main__":
    main()
