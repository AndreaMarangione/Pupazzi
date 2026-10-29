#!/bin/sh
# Prove del gioco in rete (0.5.4): piu' copie del gioco su questa macchina, che si parlano con
# messaggi UDP (porte da 56410 in su, per non toccare quelle vere). Ogni copia guida il suo
# giocatore con il computer (PUPAZZI_BOT=1) e scrive nel registro l'impronta della partita a ogni
# fotogramma (PUPAZZI_TRACE=1): su tutte le copie devono essere uguali, fotogramma per fotogramma.
#
#   sh tools/test_net.sh            tutte le prove (qualche minuto)
#   sh tools/test_net.sh perdite    solo quella (tre_console perdite scollegamento caduta
#                                   chi_crea_esce rifiuto fine_al_titolo quattro_console progressi
#                                   mappa_intera)
#   TEMPO=60 sh tools/test_net.sh   fotogrammi al secondo di ogni copia (120: il doppio del vero)
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOST=$ROOT/build/native/host
CORE=$ROOT/build/native/pupazzi_libretro.so
OUT=${OUT:-$ROOT/build/rete}
TEMPO=${TEMPO:-120}
ONLY=${1:-}
FAILS=0
PIDS=""

[ -x "$HOST" ] && [ -f "$CORE" ] || { echo "manca $HOST o $CORE: make native tools"; exit 2; }

say() { printf '%s\n' "$*"; }
ok() { say "  ok     $*"; }
bad() { say "  ERRORE $*"; FAILS=$((FAILS + 1)); }

# avvia DIR FOTOGRAMMI [VAR=valore ...] [-- opzioni dell'host]: una copia in sottofondo.
start() {
  dir=$1; frames=$2; shift 2
  rm -rf "$dir"; mkdir -p "$dir"
  envs=""; opts=""
  while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; opts="$*"; break; fi
    envs="$envs $1"; shift
  done
  # shellcheck disable=SC2086
  env PUPAZZI_OUT="$dir" PUPAZZI_SALVATAGGI="$dir" PUPAZZI_RES=320 PUPAZZI_HUD=0 PUPAZZI_TRACE=1 $envs \
    "$HOST" "$CORE" --frames "$frames" --tempo "$TEMPO" --quiet $opts > "$dir/host.txt" 2>&1 &
  PIDS="$PIDS $!"
  LASTPID=$!
}

wait_all() {
  for p in $PIDS; do wait "$p" 2>/dev/null; done
  PIDS=""
}

log() { cat "$1/pupazzi_log.txt" 2>/dev/null; }

# Impronte di due copie: fotogrammi in comune e quanti diversi; almeno MIN in comune.
same() {
  a=$1; b=$2; min=$3
  log "$a" | sed -n 's/.*rete traccia \([0-9]*\) \([0-9a-f]*\).*/\1 \2/p' | sort -k1,1 > "$a/tr.txt"
  log "$b" | sed -n 's/.*rete traccia \([0-9]*\) \([0-9a-f]*\).*/\1 \2/p' | sort -k1,1 > "$b/tr.txt"
  res=$(join "$a/tr.txt" "$b/tr.txt" | awk '{n++; if ($2 != $3) { d++; if (!f) f = $1 } } END { printf "%d %d %d", n, d + 0, f + 0 }')
  n=${res%% *}; rest=${res#* }; d=${rest%% *}; first=${rest#* }
  if [ "$d" -ne 0 ]; then bad "$(basename "$a") e $(basename "$b"): $d fotogrammi diversi su $n (il primo: $first)"
  elif [ "$n" -lt "$min" ]; then bad "$(basename "$a") e $(basename "$b"): solo $n fotogrammi in comune (ne servono $min)"
  else ok "$(basename "$a") e $(basename "$b"): $n fotogrammi in comune, tutti uguali"; fi
}

has() {   # has DIR "testo" "spiegazione"
  if log "$1" | grep -q -- "$2"; then ok "$3"; else bad "$3 (nel registro di $(basename "$1") manca \"$2\")"; fi
}

hasnt() {
  if log "$1" | grep -q -- "$2"; then bad "$3 (nel registro di $(basename "$1"): \"$(log "$1" | grep -m1 -- "$2")\")"; else ok "$3"; fi
}

want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }

BOT="PUPAZZI_BOT=1"

# 1) Tre console con i giocatori automatici; il giocatore 2 mette in pausa con START ogni tanto.
if want tre_console; then
  say "1) tre console, il giocatore 2 mette in pausa ogni tanto"
  D=$OUT/tre_console; P=PUPAZZI_RETE_PORTA=56410
  start "$D/a" 3600 $P $BOT PUPAZZI_RETE=crea:2
  sleep 0.3
  start "$D/b" 3500 $P $BOT PUPAZZI_RETE=unisciti -- --in "0:START@1200-1202,0:START@1350-1352,0:START@1500-1502,0:START@1650-1652,0:START@1800-1802"
  sleep 0.3
  start "$D/c" 3500 $P $BOT PUPAZZI_RETE=unisciti
  wait_all
  has "$D/a" "si comincia con 3 console" "si comincia in tre"
  same "$D/a" "$D/b" 3000
  same "$D/a" "$D/c" 3000
  has "$D/a" "pausa (giocatore 2)" "lo START del giocatore 2 mette in pausa chi ha creato la partita"
  has "$D/c" "pausa (giocatore 2)" "... e anche la terza console"
  has "$D/a" "carte: si sceglie" "si arriva alle carte"
  for x in a b c; do hasnt "$D/$x" "ERRORE" "nessun errore ($x)"; done
fi

# 2) Messaggi persi (20%) e in ritardo (da 10 a 50 ms) in tutti e due i sensi.
if want perdite; then
  say "2) due console, un messaggio su cinque perso e gli altri in ritardo da 10 a 50 ms"
  D=$OUT/perdite; P=PUPAZZI_RETE_PORTA=56420; L="PUPAZZI_RETE_PERDITA=20 PUPAZZI_RETE_RITARDO=10+40"
  start "$D/a" 3600 $P $BOT $L PUPAZZI_RETE=crea:1
  sleep 0.3
  start "$D/b" 3500 $P $BOT $L PUPAZZI_RETE=unisciti
  wait_all
  same "$D/a" "$D/b" 3000
  has "$D/b" "rimasta senza fotogrammi" "statistiche della rete di chi si e' unito nel registro"
  for x in a b; do hasnt "$D/$x" "ERRORE" "nessun errore ($x)"; done
fi

# 3) Una console esce (si chiude il gioco): il suo eroe lascia la partita, le altre continuano.
if want scollegamento; then
  say "3) tre console, la terza esce a meta'"
  D=$OUT/scollegamento; P=PUPAZZI_RETE_PORTA=56430
  start "$D/a" 3600 $P $BOT PUPAZZI_RETE=crea:2
  sleep 0.3
  start "$D/b" 3800 $P $BOT PUPAZZI_RETE=unisciti
  sleep 0.3
  start "$D/c" 1600 $P $BOT PUPAZZI_RETE=unisciti
  wait_all
  has "$D/a" "giocatore 3 (.*) esce dalla partita" "chi ha creato la partita vede uscire il giocatore 3"
  has "$D/b" "la console del giocatore 3 non c'e' piu'" "anche l'altra console"
  same "$D/a" "$D/b" 3000
  same "$D/a" "$D/c" 1200
  hasnt "$D/a" "si continua da soli" "chi ha creato la partita non resta solo (la seconda console c'e' ancora)"
fi

# 4) Una console si spegne di colpo: dopo 2 secondi senza messaggi esce.
if want caduta; then
  say "4) tre console, la terza si spegne di colpo"
  D=$OUT/caduta; P=PUPAZZI_RETE_PORTA=56440; W=PUPAZZI_RETE_ATTESA=2000
  start "$D/a" 3600 $P $BOT $W PUPAZZI_RETE=crea:2
  sleep 0.3
  start "$D/b" 3500 $P $BOT $W PUPAZZI_RETE=unisciti
  sleep 0.3
  start "$D/c" 3500 $P $BOT $W PUPAZZI_RETE=unisciti
  CPID=$LASTPID
  sleep 14
  kill -9 "$CPID" 2>/dev/null
  wait_all
  has "$D/a" "giocatore 3 non risponde" "chi ha creato la partita si accorge che il giocatore 3 non risponde"
  has "$D/b" "la console del giocatore 3 non c'e' piu'" "il suo eroe esce anche sull'altra console"
  same "$D/a" "$D/b" 3000
fi

# 5) Esce chi ha creato la partita: le altre tornano al titolo.
if want chi_crea_esce; then
  say "5) esce chi ha creato la partita"
  D=$OUT/chi_crea_esce; P=PUPAZZI_RETE_PORTA=56450
  start "$D/a" 1500 $P $BOT PUPAZZI_RETE=crea:1
  sleep 0.3
  start "$D/b" 2200 $P $BOT PUPAZZI_RETE=unisciti
  wait_all
  has "$D/b" "partita in rete finita su questa console: si torna al titolo" "l'altra console torna al titolo"
  same "$D/a" "$D/b" 1200
fi

# 6) Versione diversa: chi ha creato la partita non fa entrare.
if want rifiuto; then
  say "6) versione diversa"
  D=$OUT/rifiuto; P=PUPAZZI_RETE_PORTA=56460
  start "$D/a" 600 $P PUPAZZI_RETE=crea:1
  sleep 0.3
  start "$D/b" 500 $P PUPAZZI_RETE=unisciti! PUPAZZI_RETE_VERSIONE=0.0.1
  wait_all
  has "$D/a" "ha la versione 0.0.1 (qui" "chi ha creato la partita rifiuta la versione 0.0.1"
  has "$D/b" "la partita non mi fa entrare" "l'altra console lo sa"
  hasnt "$D/a" "si comincia" "e la partita non comincia"
fi

# 7) Finire la partita in rete da COME SI GIOCA? (B, poi A): tutte al titolo allo stesso fotogramma.
if want fine_al_titolo; then
  say "7) fine da COME SI GIOCA? (B e poi A su chi ha creato la partita)"
  D=$OUT/fine_al_titolo; P=PUPAZZI_RETE_PORTA=56470
  start "$D/a" 1000 $P PUPAZZI_RETE=crea:1 -- --in "0:B@600-602,0:A@660-662"
  sleep 0.3
  start "$D/b" 1000 $P PUPAZZI_RETE=unisciti
  wait_all
  has "$D/a" "partita in rete finita da COME SI GIOCA" "B e poi A finiscono la partita in rete"
  has "$D/a" "torna al titolo (fotogramma" "chi l'ha creata torna al titolo"
  has "$D/b" "partita finita al fotogramma" "l'altra console ci arriva da sola"
  ta=$(log "$D/a" | sed -n 's/.*torna al titolo (fotogramma \([0-9]*\)).*/\1/p' | head -1)
  tb=$(log "$D/b" | sed -n 's/.*partita finita al fotogramma \([0-9]*\).*/\1/p' | head -1)
  if [ -n "$ta" ] && [ "$ta" = "$tb" ]; then ok "allo stesso fotogramma ($ta)"; else bad "fotogrammi diversi: $ta e $tb"; fi
  same "$D/a" "$D/b" 50
fi

# 8) Quattro console.
if want quattro_console; then
  say "8) quattro console"
  D=$OUT/quattro_console; P=PUPAZZI_RETE_PORTA=56480
  start "$D/a" 2700 $P $BOT PUPAZZI_RETE=crea:3
  for x in b c d; do sleep 0.3; start "$D/$x" 2600 $P $BOT PUPAZZI_RETE=unisciti; done
  wait_all
  has "$D/a" "si comincia con 4 console" "si comincia in quattro"
  for x in b c d; do same "$D/a" "$D/$x" 2200; done
fi

# 9) I progressi sono quelli di chi crea la partita (qui il giro 2 della Terra): le altre console
# giocano con quelli.
if want progressi; then
  say "9) i progressi di chi crea la partita"
  D=$OUT/progressi; P=PUPAZZI_RETE_PORTA=56490
  start "$D/a" 1500 $P $BOT PUPAZZI_RETE=crea:1
  printf 'versione=2\nnome=CASA\nmappe.terra=4\ngiro.terra=2\nstelle.terra2=7\n' > "$D/a/partita1.sav"
  sleep 0.3
  start "$D/b" 1400 $P $BOT PUPAZZI_RETE=unisciti
  wait_all
  has "$D/a" "=== mappa 1: Le Piramidi (TERRA, giro 2) ===" "chi crea gioca al giro 2"
  has "$D/b" "=== mappa 1: Le Piramidi (TERRA, giro 2) ===" "... e anche l'altra console"
  same "$D/a" "$D/b" 1100
fi

# 10) Una mappa intera in tre: le stelle si salvano solo su chi ha creato la partita.
if want mappa_intera; then
  say "10) una mappa intera in tre, poi le stelle"
  D=$OUT/mappa_intera; P=PUPAZZI_RETE_PORTA=56500
  start "$D/a" 10000 $P $BOT PUPAZZI_RETE=crea:2
  printf 'versione=2\nnome=CASA\n' > "$D/a/partita1.sav"
  for x in b c; do
    sleep 0.3
    start "$D/$x" 9800 $P $BOT PUPAZZI_RETE=unisciti
    printf 'versione=2\nnome=MIA\nmappe.terra=1\n' > "$D/$x/partita1.sav"
    cp "$D/$x/partita1.sav" "$D/$x/prima.sav"
  done
  wait_all
  has "$D/a" "mappa 1 (Le Piramidi) completata" "la mappa finisce"
  has "$D/c" "mappa 1 (Le Piramidi) completata" "... anche sulle altre console"
  same "$D/a" "$D/b" 9000
  same "$D/a" "$D/c" 9000
  if grep -q "stelle.terra1=" "$D/a/partita1.sav"; then ok "le stelle si salvano su chi ha creato la partita"
  else bad "nel salvataggio di chi ha creato la partita mancano le stelle della mappa 1"; fi
  for x in b c; do
    if cmp -s "$D/$x/partita1.sav" "$D/$x/prima.sav"; then ok "il salvataggio della console $x resta com'era"
    else bad "il salvataggio della console $x e' cambiato"; fi
  done
fi

if [ "$FAILS" -eq 0 ]; then say "tutto bene"; exit 0; fi
say "$FAILS errori"
exit 1
