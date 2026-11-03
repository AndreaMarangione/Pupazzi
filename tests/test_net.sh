#!/bin/sh
# Tests of the LAN game (0.5.4): several copies of the game on this machine, talking to each other
# with UDP messages (ports from 56410 up, so as not to touch the real ones). Each copy drives its
# player with the computer (PUPAZZI_BOT=1) and writes the game hash to the log at every frame
# (PUPAZZI_TRACE=1): they must be equal on all copies, frame by frame.
#
#   sh tests/test_net.sh            all the tests (a few minutes)
#   sh tests/test_net.sh perdite    only that one (tre_console perdite scollegamento caduta
#                                   chi_crea_esce rifiuto fine_al_titolo quattro_console progressi
#                                   mappa_intera menu storia_prima salva_anche_qui)
#   TEMPO=60 sh tests/test_net.sh   frames per second of each copy (120: twice the real rate)
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOST=${HOST:-$ROOT/build/native/host}                   # other binaries (sanitizer): HOST=... CORE=...
CORE=${CORE:-$ROOT/build/native/pupazzi_libretro.so}
OUT=${OUT:-$ROOT/build/rete}
TEMPO=${TEMPO:-120}
ONLY=${1:-}
FAILS=0
PIDS=""

[ -x "$HOST" ] && [ -f "$CORE" ] || { echo "manca $HOST o $CORE: make native tools"; exit 2; }

say() { printf '%s\n' "$*"; }
ok() { say "  ok     $*"; }
bad() { say "  ERRORE $*"; FAILS=$((FAILS + 1)); }

# start DIR FRAMES [VAR=value ...] [-- host options]: one copy in the background.
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

# Hashes of two copies: frames in common and how many differ; at least MIN in common.
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

has() {   # has DIR "text" "explanation"
  if log "$1" | grep -q -- "$2"; then ok "$3"; else bad "$3 (nel registro di $(basename "$1") manca \"$2\")"; fi
}

hasnt() {
  if log "$1" | grep -q -- "$2"; then bad "$3 (nel registro di $(basename "$1"): \"$(log "$1" | grep -m1 -- "$2")\")"; else ok "$3"; fi
}

want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }

BOT="PUPAZZI_BOT=1"

# 1) Three consoles with bots; player 2 pauses with START every now and then.
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

# 2) Lost messages (20%) and delayed ones (10 to 50 ms) in both directions.
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

# 3) A console leaves (the game is closed): its hero leaves the game, the others carry on.
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

# 4) A console switches off suddenly: after 2 seconds without messages it leaves.
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

# 5) The host leaves: the others go back to the title screen.
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

# 6) Different version: the host does not let it in.
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

# 7) Ending the LAN game from COME SI GIOCA? (B, then A): all at the title screen on the same frame.
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

# 8) Four consoles.
if want quattro_console; then
  say "8) quattro console"
  D=$OUT/quattro_console; P=PUPAZZI_RETE_PORTA=56480
  start "$D/a" 2700 $P $BOT PUPAZZI_RETE=crea:3
  for x in b c d; do sleep 0.3; start "$D/$x" 2600 $P $BOT PUPAZZI_RETE=unisciti; done
  wait_all
  has "$D/a" "si comincia con 4 console" "si comincia in quattro"
  for x in b c d; do same "$D/a" "$D/$x" 2200; done
fi

# 9) The progress is the host's (here round 2 of Earth): the other consoles play with it.
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

# 10) A whole map with four consoles: the stars are saved on the host and (0.5.6) on the console
# that joined saving it too (d: in its first free slot, the 2nd), not on the others.
if want mappa_intera; then
  say "10) una mappa intera in quattro, poi le stelle"
  D=$OUT/mappa_intera; P=PUPAZZI_RETE_PORTA=56500
  start "$D/a" 10000 $P $BOT PUPAZZI_RETE=crea:3
  printf 'versione=2\nnome=CASA\n' > "$D/a/partita1.sav"
  for x in b c d; do
    sleep 0.3
    if [ $x = d ]; then start "$D/$x" 9800 $P $BOT PUPAZZI_RETE=unisciti+salva; else start "$D/$x" 9800 $P $BOT PUPAZZI_RETE=unisciti; fi
    printf 'versione=2\nnome=MIA\nmappe.terra=1\n' > "$D/$x/partita1.sav"
    cp "$D/$x/partita1.sav" "$D/$x/prima.sav"
  done
  wait_all
  has "$D/a" "mappa 1 (Le Piramidi) completata" "la mappa finisce"
  has "$D/c" "mappa 1 (Le Piramidi) completata" "... anche sulle altre console"
  same "$D/a" "$D/b" 9000
  same "$D/a" "$D/c" 9000
  same "$D/a" "$D/d" 9000
  if grep -q "stelle.terra1=" "$D/a/partita1.sav"; then ok "le stelle si salvano su chi ha creato la partita"
  else bad "nel salvataggio di chi ha creato la partita mancano le stelle della mappa 1"; fi
  for x in b c d; do
    if cmp -s "$D/$x/partita1.sav" "$D/$x/prima.sav"; then ok "il salvataggio 1 della console $x resta com'era"
    else bad "il salvataggio 1 della console $x e' cambiato"; fi
  done
  ida=$(sed -n 's/^id=//p' "$D/a/partita1.sav"); sa=$(sed -n 's/^stelle.terra1=//p' "$D/a/partita1.sav")
  idd=$(sed -n 's/^id=//p' "$D/d/partita2.sav" 2>/dev/null); sd=$(sed -n 's/^stelle.terra1=//p' "$D/d/partita2.sav" 2>/dev/null)
  if [ -n "$ida" ] && [ "$ida" = "$idd" ] && [ -n "$sa" ] && [ "$sa" = "$sd" ] && grep -q "^nome=CASA" "$D/d/partita2.sav"; then
    ok "la console d l'ha salvata anche lei, nel posto 2: stessa partita ($ida), stesse stelle ($sa)"
  else bad "la console d: posto 2 con id '$idd' e stelle '$sd' (chi l'ha creata: '$ida', '$sa')"; fi
  if [ ! -f "$D/b/partita2.sav" ] && [ ! -f "$D/c/partita2.sav" ]; then ok "le console b e c non l'hanno salvata"
  else bad "le console b o c hanno un salvataggio 2"; fi
fi

# 11) From the menus, with the buttons: the host (title, GIOCO IN RETE, CREA, free slot, name, A to
# start) and the joining console (title, GIOCO IN RETE, UNISCITI, A on the game found).
if want menu; then
  say "11) dai menu, con i tasti"
  D=$OUT/menu; P=PUPAZZI_RETE_PORTA=56510
  # A to start held for 6 frames: afterwards it must not count in COME SI GIOCA? as well.
  start "$D/a" 700 $P -- --in "0:DOWN@30-31,0:DOWN@40-41,0:A@50-51,0:A@70-71,0:A@90-91,0:START@110-111,0:A@420-425"
  sleep 0.3
  # b: A on the game found, then A to "save it here too?" (0.5.6): the first free slot. c: the same
  # with the 4 slots full: then down to the 2nd, A, and A to "overwrite?".
  start "$D/b" 700 $P -- --in "0:DOWN@30-31,0:DOWN@40-41,0:A@50-51,0:DOWN@70-71,0:A@80-81,0:A@220-221,0:A@235-236"
  for k in 1 2 3 4; do printf 'versione=2\nnome=ALTRA %s\n' $k > "$D/c_partita$k.sav"; done
  start "$D/c" 700 $P -- --in "0:DOWN@30-31,0:DOWN@40-41,0:A@50-51,0:DOWN@70-71,0:A@80-81,0:A@220-221,0:A@235-236,0:DOWN@250-251,0:A@262-263,0:A@275-276"
  for k in 1 2 3 4; do mv "$D/c_partita$k.sav" "$D/c/partita$k.sav"; done
  wait_all
  has "$D/a" "partita 1 nuova" "chi crea fa una partita nuova nel posto 1"
  has "$D/a" "partita creata (porta" "e crea la partita in rete"
  has "$D/b" "mi unisco alla partita" "l'altra la trova e si unisce"
  has "$D/b" "si salvera' anche qui, nel posto 1" "... e la salva anche lei, nel primo posto libero"
  has "$D/c" "si salvera' anche qui, nel posto 2" "con i posti pieni sceglie quale sostituire (il 2)"
  has "$D/a" "si comincia con 3 console" "A fa cominciare"
  has "$D/b" "sono il giocatore 2" "l'altra console e' il giocatore 2"
  has "$D/a" "f=600 .*| modalita'" "la A tenuta per cominciare non salta COME SI GIOCA?"
  has "$D/b" "f=600 .*| modalita'" "... nemmeno sull'altra console"
  same "$D/a" "$D/b" 150
  same "$D/a" "$D/c" 150
  ida=$(sed -n 's/^id=//p' "$D/a/partita1.sav")
  if [ -n "$ida" ] && grep -q "^id=$ida" "$D/b/partita1.sav" && grep -q "^id=$ida" "$D/c/partita2.sav"; then
    ok "b e c hanno la partita di chi l'ha creata ($ida)"
  else bad "manca la partita di chi l'ha creata ('$ida') in b/partita1.sav o c/partita2.sav"; fi
  if grep -q "^nome=ALTRA 1" "$D/c/partita1.sav" && grep -q "^nome=ALTRA 3" "$D/c/partita3.sav" && ! grep -q "ALTRA 2" "$D/c/partita2.sav"; then
    ok "c: sostituito solo il salvataggio scelto"
  else bad "c: salvataggi sbagliati"; fi
fi

# 12) First each one plays a different map alone (monsters, shots, cards, random numbers...), then
# the LAN game: it must still start the same.
if want storia_prima; then
  say "12) prima da soli, poi in rete"
  D=$OUT/storia_prima; P=PUPAZZI_RETE_PORTA=56520
  start "$D/a" 3000 $P $BOT PUPAZZI_MAPPA=1 PUPAZZI_RETE=crea:1 PUPAZZI_RETE_DOPO=900
  sleep 0.3
  start "$D/b" 3000 $P $BOT PUPAZZI_MAPPA=2 PUPAZZI_RETE=unisciti PUPAZZI_RETE_DOPO=1300
  wait_all
  has "$D/a" "=== mappa 1: Le Piramidi" "chi crea prima gioca la mappa 1 da solo"
  has "$D/b" "=== mappa 2:" "l'altra la mappa 2"
  has "$D/a" "si comincia con 2 console" "poi la partita in rete"
  same "$D/a" "$D/b" 1300
fi

# 13) (0.5.6) Joining a game already saved here: the same slot is updated, and what this console
# had more (the Moon, reached alone) stays. With the slots full: in the slot given (unisciti+salva:3),
# or not saved at all.
if want salva_anche_qui; then
  say "13) la stessa partita salvata anche qui, di nuovo"
  D=$OUT/salva_anche_qui; P=PUPAZZI_RETE_PORTA=56530; ID=00c0ffee12345678
  start "$D/a" 1500 $P $BOT PUPAZZI_RETE=crea:3
  printf 'versione=2\nnome=CASA\nid=%s\nmappe.terra=3\n' $ID > "$D/a/partita1.sav"
  sleep 0.3
  start "$D/b" 1400 $P $BOT PUPAZZI_RETE=unisciti+salva
  printf 'versione=2\nnome=MIA\n' > "$D/b/partita1.sav"
  printf 'versione=2\nnome=CASA\nid=%s\nmappe.terra=2\nmappe.luna=1\n' $ID > "$D/b/partita2.sav"
  for x in c d; do
    sleep 0.3
    if [ $x = c ]; then start "$D/$x" 1400 $P $BOT PUPAZZI_RETE=unisciti+salva:3; else start "$D/$x" 1400 $P $BOT PUPAZZI_RETE=unisciti+salva; fi
    for k in 1 2 3 4; do printf 'versione=2\nnome=ALTRA %s\n' $k > "$D/$x/partita$k.sav"; cp "$D/$x/partita$k.sav" "$D/$x/prima$k.sav"; done
  done
  wait_all
  has "$D/a" "si comincia con 4 console" "si comincia in quattro"
  has "$D/b" "la stessa partita: si aggiorna" "b ce l'ha gia' nel posto 2: si aggiorna quello"
  if grep -q "^mappe.terra=3" "$D/b/partita2.sav" && grep -q "^mappe.luna=1" "$D/b/partita2.sav" && grep -q "^id=$ID" "$D/b/partita2.sav" && [ ! -f "$D/b/partita3.sav" ]; then
    ok "b: posto 2 con le mappe di chi l'ha creata e la Luna che aveva gia'; nessun posto nuovo"
  else bad "b: posto 2 sbagliato ($(grep -v '^#' "$D/b/partita2.sav" | tr '\n' ' '))"; fi
  if grep -q "^id=$ID" "$D/c/partita3.sav" && cmp -s "$D/c/partita2.sav" "$D/c/prima2.sav"; then ok "c (posti pieni): nel posto 3, come chiesto"
  else bad "c: la partita non e' nel posto 3"; fi
  ch=0; for k in 1 2 3 4; do cmp -s "$D/d/partita$k.sav" "$D/d/prima$k.sav" || ch=1; done
  if [ $ch = 0 ]; then ok "d (posti pieni, nessun posto dato): non la salva"; else bad "d: un salvataggio e' cambiato"; fi
  for x in b c d; do same "$D/a" "$D/$x" 1100; done
fi

if [ "$FAILS" -eq 0 ]; then say "tutto bene"; exit 0; fi
say "$FAILS errori"
exit 1
