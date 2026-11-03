#!/bin/sh
# Tests of the GPU renderer on a software OpenGL ES (Mesa, headless). Usage: make test-gl
#  1. the core gets the context and does not leave the GL state dirty;
#  2. the image is the same whatever GL state it finds at the start of the frame (--dirty);
#  3. software and GPU draw the same scene (the differing pixels are counted).
H=build/native/host
C=build/native/pupazzi_libretro.so
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
# Game played by two bots: enemies, shots, particles and HUD in the scene. No automatic switch
# from 8 to 4 samples (it depends on how fast the machine is: different images).
export PUPAZZI_BOT=2 PUPAZZI_PERF=1 PUPAZZI_AA_ADATTA=0
N=420
fail=0

out=$(PUPAZZI_OUT="$T" PUPAZZI_RENDERER=gpu PUPAZZI_HUD=fisso $H $C --gl --frames $N 2>&1)
case "$out" in
  *"renderer hardware: si"*) ;;
  *) echo "saltata: qui non c'e' un OpenGL ES utilizzabile senza schermo (serve Mesa)"; exit 0 ;;
esac
case "$out" in *"STATO GL SPORCO"*) echo "ERRORE: il core lascia sporco lo stato GL"; echo "$out" | grep SPORCO; fail=1 ;; esac

# With and without antialiasing (PUPAZZI_AA=0, and "prova": the tile-based GPU path with a single
# sample): the same image whatever GL state is found.
for aa in auto 0 prova; do
  ref=""
  for d in 0 1 2 4 8 16 31; do
    o=$(PUPAZZI_AA=$aa PUPAZZI_OUT="$T" PUPAZZI_RENDERER=gpu PUPAZZI_HUD=fisso $H $C --gl --frames $N --quiet --dirty $d 2>&1)
    h=$(echo "$o" | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p' | tail -n 1)
    case "$o" in *"STATO GL SPORCO"*) echo "ERRORE: stato GL sporco dopo il fotogramma con --dirty $d (antialias $aa)"; fail=1 ;; esac
    [ -z "$ref" ] && ref=$h
    if [ -z "$h" ] || [ "$h" != "$ref" ]; then echo "ERRORE: con --dirty $d l'immagine cambia ($h invece di $ref, antialias $aa)"; fail=1; fi
  done
  [ $fail = 0 ] && echo "stato GL (antialias $aa): stessa immagine con ogni stato di partenza ($ref)"
done

# The tile-based GPU path (texture with the samples, then copy to the screen) with a single
# sample must give exactly the image without antialiasing.
h0=$(PUPAZZI_PERF=0 PUPAZZI_AA=0 PUPAZZI_OUT="$T" PUPAZZI_RENDERER=gpu $H $C --gl --frames $N --quiet 2>&1 | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p' | tail -n 1)
o1=$(PUPAZZI_PERF=0 PUPAZZI_AA=prova PUPAZZI_OUT="$T" PUPAZZI_RENDERER=gpu $H $C --gl --frames $N 2>&1)
h1=$(echo "$o1" | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p' | tail -n 1)
case "$o1" in
  *"antialias 4x pronto"*) ;;
  *) echo "ERRORE: con PUPAZZI_AA=prova il percorso a tessere non e' partito"; fail=1 ;;
esac
if [ -n "$h0" ] && [ "$h0" = "$h1" ]; then echo "antialias, percorso a tessere con un campione: stessa immagine ($h0)"
else echo "ERRORE: percorso a tessere con un campione diverso ($h1 invece di $h0)"; fail=1; fi
# 0.5.4: up to 8 samples (prova8) and 8 rejected by the GPU (prova8no: it switches to 4), always
# with one real sample: same image.
for m in prova8:8x prova8no:4x; do
  o2=$(PUPAZZI_PERF=0 PUPAZZI_AA=${m%%:*} PUPAZZI_OUT="$T" PUPAZZI_RENDERER=gpu $H $C --gl --frames $N 2>&1)
  h2=$(echo "$o2" | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p' | tail -n 1)
  case "$o2" in *"antialias ${m##*:} pronto"*) ;; *) echo "ERRORE: con PUPAZZI_AA=${m%%:*} non c'e' \"antialias ${m##*:} pronto\""; fail=1 ;; esac
  if [ "${m%%:*}" = prova8no ]; then
    case "$o2" in *"antialias 8x rifiutato"*) ;; *) echo "ERRORE: con PUPAZZI_AA=prova8no gli 8 campioni non sono stati rifiutati"; fail=1 ;; esac
  fi
  if [ -n "$h0" ] && [ "$h0" = "$h2" ]; then echo "antialias ${m%%:*}: ${m##*:}, stessa immagine ($h2)"
  else echo "ERRORE: antialias ${m%%:*}, immagine diversa ($h2 invece di $h0)"; fail=1; fi
done
# 0.5.4: with "the best" at 8 samples, below 55 frames per second in a game it switches by itself
# to 4 (here the host deliberately runs at 30 per second).
mkdir -p "$T/adatta"
o3=$(PUPAZZI_AA_ADATTA=1 PUPAZZI_AA=prova8 PUPAZZI_RES=320 PUPAZZI_OUT="$T/adatta" PUPAZZI_SALVATAGGI="$T/adatta" PUPAZZI_HUD=0 PUPAZZI_RENDERER=gpu $H $C --gl --frames 200 --tempo 30 2>&1)
case "$o3" in
  *"antialias 8x pronto"*"si passa a 4"*"antialias 4x pronto"*) echo "antialias 8x a 30 fotogrammi al secondo: si passa da solo a 4" ;;
  *) echo "ERRORE: a 30 fotogrammi al secondo l'antialias non passa da 8 a 4 campioni"; fail=1 ;;
esac

# Software and GPU without antialiasing: the same scene, except for a few pixels on the edges.
mkdir -p "$T/sw" "$T/gl" "$T/aa"
PUPAZZI_OUT="$T" PUPAZZI_HUD=0 PUPAZZI_RENDERER=software $H $C --frames $N --quiet --dump "$T/sw" --at 30,250,419 > /dev/null 2>&1
PUPAZZI_AA=0 PUPAZZI_OUT="$T" PUPAZZI_HUD=0 PUPAZZI_RENDERER=gpu $H $C --gl --frames $N --quiet --dump "$T/gl" --at 30,250,419 > /dev/null 2>&1
for f in f00030.ppm f00250.ppm f00419.ppm; do
  if ! [ -f "$T/sw/$f" ] || ! [ -f "$T/gl/$f" ]; then echo "ERRORE: manca il fotogramma $f"; fail=1; continue; fi
  n=$(cmp -l "$T/sw/$f" "$T/gl/$f" 2>/dev/null | wc -l)
  echo "software e GPU, $f: $n byte diversi"
  [ "$n" -gt 3000 ] && { echo "ERRORE: troppe differenze tra i due renderer"; fail=1; }
done

# The interface (menus, pause with the popup, in-game HUD) with the two renderers: the same shapes
# and the same glyphs. Pixels with a channel differing by more than 24 are counted (the rounding on
# the soft edges stays below that); a few dozen come from the 3D scene.
pix() { python3 - "$1" "$2" <<'PYEOF'
import sys
def ppm(p):
    b = open(p, 'rb').read().split(b'\n', 3)
    w, h = map(int, b[1].split())
    return w * h, b[3]
n, a = ppm(sys.argv[1])
_, b = ppm(sys.argv[2])
print(sum(1 for i in range(0, n * 3, 3) if max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])) > 24))
PYEOF
}
if command -v python3 > /dev/null; then
  MENU_IN="0:A@200-202,0:RIGHT@280-282,0:A@320-322,0:LEFT@360-362,0:A@380-382,0:A@450-452,0:A@470-472,0:DOWN@550-552,0:A@562-564,0:UP@600-602,0:A@620-622,0:RIGHT@700-702,1:A@720-722"
  PAUSE_IN="0:A@40-42,1:A@40-42,0:START@100-102,0:DOWN@130-132,0:A@150-152,0:B@175-177,0:DOWN@190-192"
  # In Italian and (0.5.3) in Hindi and Japanese: shaped words and Chinese characters.
  for r in software:sw gpu:gl; do
    R=${r%%:*}; D=$T/ui_${r##*:}; G=""; [ "$R" = gpu ] && G=--gl
    for L in it hi ja; do
      mkdir -p "$D/m$L" "$D/p$L"
      PUPAZZI_LINGUA=$L PUPAZZI_BOT= PUPAZZI_PERF=0 PUPAZZI_AA=0 PUPAZZI_OUT="$D/m$L" PUPAZZI_SALVATAGGI="$D/m$L" PUPAZZI_RENDERER=$R $H $C $G --frames 761 --quiet --dump "$D/m$L" --at 150,260,345,430,540,580,760 --in "$MENU_IN" > /dev/null 2>&1
      PUPAZZI_LINGUA=$L PUPAZZI_BOT= PUPAZZI_PERF=0 PUPAZZI_AA=0 PUPAZZI_OUT="$D/p$L" PUPAZZI_SALVATAGGI="$D/p$L" PUPAZZI_MAPPA=1 PUPAZZI_GIOCATORI=2 PUPAZZI_RENDERER=$R $H $C $G --frames 201 --quiet --dump "$D/p$L" --at 30,120,160,200 --in "$PAUSE_IN" > /dev/null 2>&1
    done
    mkdir -p "$D/b"
    PUPAZZI_BOT=4 PUPAZZI_PERF=0 PUPAZZI_AA=0 PUPAZZI_OUT="$D/b" PUPAZZI_MAPPA=1 PUPAZZI_RENDERER=$R $H $C $G --frames 420 --quiet --dump "$D/b" --at 150,419 > /dev/null 2>&1
  done
  worst=0; nf=0
  for f in "$T"/ui_sw/*/*.ppm; do
    g="$T/ui_gl/${f#$T/ui_sw/}"
    if ! [ -f "$g" ]; then echo "ERRORE: manca il fotogramma ${f#$T/ui_sw/} della GPU"; fail=1; continue; fi
    n=$(pix "$f" "$g")
    nf=$((nf + 1))
    [ "$n" -gt "$worst" ] && worst=$n
    [ "$n" -gt 600 ] && { echo "ERRORE: interfaccia, ${f#$T/ui_sw/}: $n pixel diversi tra software e GPU"; fail=1; }
  done
  [ "$nf" -lt 35 ] && { echo "ERRORE: interfaccia, solo $nf fotogrammi confrontati"; fail=1; }
  echo "interfaccia, software e GPU: $nf fotogrammi, al massimo $worst pixel diversi"
else
  echo "interfaccia: python3 assente, confronto saltato"
fi

# With antialiasing (if the GPU can do it) only the edges change: something, but not too much.
o=$(PUPAZZI_OUT="$T" PUPAZZI_HUD=0 PUPAZZI_RENDERER=gpu $H $C --gl --frames $N --dump "$T/aa" --at 30,250,419 2>&1)
case "$o" in
  *"antialias 8x pronto"*|*"antialias 4x pronto"*|*"antialias 2x pronto"*)
    for f in f00030.ppm f00250.ppm f00419.ppm; do
      n=$(cmp -l "$T/gl/$f" "$T/aa/$f" 2>/dev/null | wc -l)
      echo "antialias, $f: $n byte diversi dall'immagine senza"
      if [ "$n" -eq 0 ] || [ "$n" -gt 184320 ]; then echo "ERRORE: l'antialias non cambia nulla o cambia troppo"; fail=1; fi
    done ;;
  *) echo "antialias: non disponibile con questo OpenGL ES, prova saltata" ;;
esac
exit $fail
