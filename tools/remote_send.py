#!/usr/bin/env python3
# Pupazzi - fa da Remote RetroPad: manda a una console i comandi di un controller come il core
# "Remote RetroPad" di RetroArch su un'altra console (stesso messaggio UDP di 20 byte), per provare
# i comandi dalla rete senza una seconda console (dal PC alla console, o al frontend di prova).
#
#   python3 tools/remote_send.py IP GIOCATORE [EVENTO ...] [--porta 55400] [--tremolio N] [--raffica N]
#
# EVENTO come nel frontend di prova (tools/host.cpp), ma con i tempi in secondi:
#   NOME@inizio-fine          un tasto: B Y SELECT START UP DOWN LEFT RIGHT A X L R L2 R2 L3 R3
#   asse=valore@inizio-fine   una levetta: lx ly rx ry, valore tra -1 e 1 (y positivo = in basso)
# Esempio, giocatore 2 sulla console 192.168.1.228: levetta sinistra a destra per 2 secondi, poi
# un salto (R2):
#   python3 tools/remote_send.py 192.168.1.228 2 "lx=1@0-2" "R2@2.5-2.6"
#
# Come il vero Remote RetroPad, 60 volte al secondo manda solo cio' che e' cambiato, prima i 16
# tasti e poi le levette. --tremolio N aggiunge a ogni invio un tremolio casuale fino a N (su 32767)
# a tutte e quattro le levette, come una levetta vera che non sta mai ferma: fino a 4 messaggi a
# fotogramma, le condizioni in cui RetroArch restava indietro. --raffica N all'inizio manda N
# messaggi delle levette tutti insieme (gli ultimi quattro le rimettono al centro).
import argparse
import random
import socket
import struct
import sys
import time

JOYPAD, ANALOG = 1, 5      # RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ANALOG
BUTTONS = ("B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R", "L2", "R2", "L3", "R3")
AXES = ("lx", "ly", "rx", "ry")    # (index, id): sinistra x y = (0, 0) (0, 1), destra x y = (1, 0) (1, 1)
BASE_PORT = 55400


def message(device, index, id_, state, port=0, pad=b"\0\0"):
    """struct remote_joypad_message { int port, device, index, id; uint16_t state; }: 20 byte."""
    return struct.pack("<iiiiH", port, device, index, id_, state & 0xFFFF) + pad


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def axis_value(v):
    """Valore della levetta in [-1, 1] -> intero a 16 bit, come il frontend di prova."""
    a = int(f32(f32(v) * 32767.0))
    return max(-32768, min(32767, a))


def parse_event(tok):
    """'[porta:]NOME@inizio-fine' o '[porta:]asse=valore@inizio-fine' -> dizionario.
    Senza porta: None. inizio e fine sono numeri (secondi qui, fotogrammi nel frontend di prova)."""
    port = None
    head, sep, span = tok.strip().partition("@")
    if not sep:
        raise ValueError("manca @inizio-fine in '%s'" % tok)
    if ":" in head:
        p, head = head.split(":", 1)
        port = int(p)
    a, sep, b = span.partition("-")
    if not sep:
        raise ValueError("intervallo non valido in '%s'" % tok)
    ev = {"port": port, "t0": float(a), "t1": float(b), "btn": None, "axis": None, "val": 0.0}
    if "=" in head:
        name, val = head.split("=", 1)
        if name not in AXES:
            raise ValueError("asse non valido in '%s'" % tok)
        ev["axis"] = AXES.index(name)
        ev["val"] = float(val)
    else:
        if head not in BUTTONS:
            raise ValueError("tasto non valido in '%s'" % tok)
        ev["btn"] = BUTTONS.index(head)
    return ev


def state_at(events, t, port=None):
    """Tasti (maschera) e levette (interi) in t: tasti in OR, per ogni asse l'ultimo evento che vale."""
    mask, vals = 0, [0.0] * 4
    for e in events:
        if (port is not None and e["port"] != port) or not (e["t0"] <= t <= e["t1"]):
            continue
        if e["btn"] is not None:
            mask |= 1 << e["btn"]
        else:
            vals[e["axis"]] = e["val"]
    return mask, [axis_value(v) for v in vals]


class RemotePad:
    """Un controller come lo vede il Remote RetroPad: ricorda l'ultimo stato mandato e manda solo
    cio' che cambia, nello stesso ordine (i 16 tasti, poi levetta sinistra x y, destra x y)."""

    def __init__(self, ip, port, sock=None):
        self.sock = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addr = (ip, port)
        self.btn = 0
        self.ax = [0, 0, 0, 0]
        self.sent = 0

    def raw(self, device, index, id_, state, pad=b"\0\0"):
        self.sock.sendto(message(device, index, id_, state, pad=pad), self.addr)
        self.sent += 1

    def set(self, btn=None, axes=None):
        """Nuovo stato (None: com'era); restituisce quanti messaggi ha mandato."""
        n = self.sent
        if btn is not None:
            for i in range(16):
                s = (btn >> i) & 1
                if s != (self.btn >> i) & 1:
                    self.raw(JOYPAD, 0, i, s)
            self.btn = btn
        if axes is not None:
            for a in range(4):
                if axes[a] is not None and axes[a] != self.ax[a]:
                    self.ax[a] = axes[a]
                    self.raw(ANALOG, a // 2, a % 2, axes[a])
        return self.sent - n


def main():
    ap = argparse.ArgumentParser(description="Fa da Remote RetroPad verso una console (o il frontend di prova).")
    ap.add_argument("ip", help="IP della console che fa girare il gioco (127.0.0.1 per il frontend di prova)")
    ap.add_argument("giocatore", type=int, choices=(1, 2, 3, 4))
    ap.add_argument("eventi", nargs="*", help="es. lx=1@0-2 R2@2.5-2.6 (tempi in secondi)")
    ap.add_argument("--porta", type=int, default=BASE_PORT, help="porta del giocatore 1 (predefinita 55400)")
    ap.add_argument("--tremolio", type=int, default=0, metavar="N", help="tremolio casuale fino a N (su 32767) sulle levette")
    ap.add_argument("--raffica", type=int, default=0, metavar="N", help="all'inizio N messaggi delle levette tutti insieme")
    args = ap.parse_args()
    try:
        events = [parse_event(t) for t in args.eventi]
    except ValueError as e:
        sys.exit("remote_send: %s" % e)
    port = args.porta + args.giocatore - 1
    pad = RemotePad(args.ip, port)
    t_start = time.monotonic()
    if args.raffica > 0:
        for k in range(args.raffica):
            a = k % 4 if k >= args.raffica - 4 else random.randrange(4)
            v = 0 if k >= args.raffica - 4 else random.randint(-32767, 32767)
            pad.raw(ANALOG, a // 2, a % 2, v)
        print("raffica: %d messaggi delle levette mandati insieme" % args.raffica)
    end = max([e["t1"] for e in events] + [0.0]) + 0.1
    tick = 0
    while True:
        t = tick / 60.0
        if t > end:
            break
        delay = t_start + t - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        btn, ax = state_at(events, t)
        if args.tremolio > 0:
            ax = [max(-32768, min(32767, v + random.randint(-args.tremolio, args.tremolio))) for v in ax]
        pad.set(btn, ax)
        tick += 1
    pad.set(0, [0, 0, 0, 0])      # alla fine tutto lasciato e al centro
    print("mandati %d messaggi in %.1f s a %s:%d (giocatore %d)" % (pad.sent, time.monotonic() - t_start, args.ip, port, args.giocatore))


if __name__ == "__main__":
    main()
