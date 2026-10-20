#!/bin/bash
# Pupazzi - dal PC alla console (Anbernic RG35XX Pro con Stock OS MOD).
#
#   ./deploy.sh installa [IP]      copia il gioco sulla console via SSH
#   ./deploy.sh risultati [IP]     scarica i registri nella cartella risultati/
#   ./deploy.sh rimuovi [IP]       toglie il gioco dalla console
#   ./deploy.sh sd CARTELLA        copia su una scheda SD montata sul PC (CARTELLA = la cartella Roms della scheda)
#   ./deploy.sh mappa FILE [IP]    mette sulla console una mappa modificata (per esempio terra1.txt)
#   ./deploy.sh togli-mappa NOME [IP]   toglie la mappa modificata: torna quella di serie
#
# IP predefinito 192.168.1.228 (o variabile PUPAZZI_IP); utente root, password root (o PUPAZZI_PASS).
# La console deve essere accesa, in Wi-Fi, con SSH attivo. Se c'e' sshpass la password non viene chiesta.
#
# Sulla console tutto finisce in /mnt/mmc/Roms/APPS: la voce di menu Pupazzi.sh, la cartella
# pupazzi/ e l'icona Imgs/Pupazzi.png. Un aggiornamento della MOD svuota quella cartella: dopo
# basta rifare "installa".

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/dist/APPS"
APPS="/mnt/mmc/Roms/APPS"
IP="${PUPAZZI_IP:-192.168.1.228}"
# Avanzi delle versioni prima, relativi ad APPS: voci di menu e risultati del benchmark della prova
# tecnica 0.1.0; le mappe di serie della prateria (fino alla 0.4).
OLD="Pupazzi_GPU.sh Pupazzi_Bench.sh Imgs/Pupazzi_GPU.png Imgs/Pupazzi_Bench.png pupazzi/out/bench pupazzi/out/RISULTATI.txt pupazzi/out/pupazzi_bench.txt"
OLD="$OLD pupazzi/mappe/originali/prateria1.txt pupazzi/mappe/originali/prateria2.txt pupazzi/mappe/originali/prateria3.txt pupazzi/mappe/originali/prateria_boss.txt"

die() { echo "ERRORE: $*" >&2; exit 1; }

# ssh_run 'comando': esegue un comando sulla console. PUPAZZI_SSH sostituisce ssh (serve alle prove).
ssh_run() {
  if [ -n "${PUPAZZI_SSH:-}" ]; then
    $PUPAZZI_SSH "root@$IP" "$1"
    return
  fi
  # Console di casa con password di fabbrica e chiave che cambia a ogni reinstallazione: niente controllo della chiave.
  local opts=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8)
  if command -v sshpass > /dev/null 2>&1; then
    SSHPASS="${PUPAZZI_PASS:-root}" sshpass -e ssh "${opts[@]}" "root@$IP" "$1"
  else
    ssh "${opts[@]}" "root@$IP" "$1"
  fi
}

need_dist() {
  [ -f "$SRC/pupazzi/arm32/pupazzi_libretro.so" ] && [ -f "$SRC/pupazzi/run.sh" ] ||
    die "manca $SRC: serve 'make dist' (nel pacchetto consegnato c'e' gia')"
}

file_list() { (cd "$SRC" && find . -type f | LC_ALL=C sort); }

cmd_installa() {
  need_dist
  echo "Copio su root@$IP:$APPS ($(cat "$SRC/pupazzi/VERSIONE" 2> /dev/null)) ..."
  ssh_run "true" || die "la console non risponde su $IP (accesa? SSH attivo? IP giusto?)"
  # Prima si tolgono le voci della versione precedente e si mette da parte il suo registro.
  ssh_run "cd '$APPS' 2> /dev/null && rm -rf $OLD && { [ ! -f pupazzi/out/pupazzi_log.txt ] || mv -f pupazzi/out/pupazzi_log.txt pupazzi/out/pupazzi_log.txt.vecchio; }; true"
  # La scheda della console e' FAT montata con tutti i permessi a 777: l'archivio li dichiara cosi',
  # altrimenti tar sulla console protesta perche' non puo' cambiarli. Il controllo vero e' la somma qui sotto.
  local errs here there
  errs="$(tar -C "$SRC" --owner=0 --group=0 --mode='a+rwx' -cf - Pupazzi.sh pupazzi Imgs |
    ssh_run "mkdir -p '$APPS' && cd '$APPS' && { tar --no-same-owner -p -xf - 2>&1; sync; }")"
  here="$(cd "$SRC" && file_list | xargs md5sum)"
  there="$(ssh_run "cd '$APPS' && md5sum $(file_list | tr '\n' ' ')" 2> /dev/null)"
  if [ -z "$here" ] || [ "$here" != "$there" ]; then
    [ -n "$errs" ] && echo "$errs" >&2
    die "i file sulla console non corrispondono a quelli del PC"
  fi
  echo "Installato e verificato ($(file_list | wc -l) file)."
  echo "Sulla console: App Center -> APPS -> Pupazzi."
}

cmd_risultati() {
  local base d n=2 st incomplete=0
  base="$HERE/risultati/$(date +%Y%m%d_%H%M%S)"
  d="$base"
  while [ -e "$d" ]; do d="${base}_$n"; n=$((n + 1)); done
  mkdir -p "$d" || die "non posso creare $d"
  # Se la scheda era piena o in sola lettura lo script di avvio ha scritto in /tmp/pupazzi_out:
  # si prende la cartella con l'avvio piu' recente.
  ssh_run "if [ /tmp/pupazzi_out/avvio.log -nt '$APPS/pupazzi/out/avvio.log' ]; then cd /tmp/pupazzi_out; else cd '$APPS/pupazzi/out' || cd /tmp/pupazzi_out; fi 2> /dev/null && tar -cf - ." | tar -C "$d" -xf - 2> /dev/null
  st=("${PIPESTATUS[@]}")
  if [ -z "$(ls -A "$d")" ]; then
    rmdir "$d"
    die "nessun risultato da $IP (console spenta o non raggiungibile, oppure il gioco non e' ancora stato lanciato)"
  fi
  if [ "${st[0]}" != 0 ] || [ "${st[1]}" != 0 ]; then incomplete=1; fi
  # Un solo file di testo con tutto cio' che serve per capire com'e' andata.
  local out="$d/DA_MANDARE.txt" f
  {
    echo "=== PUPAZZI: registri dalla console ($(date '+%Y-%m-%d %H:%M:%S')) ==="
    [ -f "$d/console.txt" ] && { echo; echo "=== console ==="; cat "$d/console.txt"; }
    [ -f "$d/avvio.log" ] && { echo; echo "=== avvio.log: ultimi avvii ==="; tail -n 40 "$d/avvio.log"; }
    [ -f "$d/pupazzi_stato.txt" ] && { echo; echo "=== ultima partita ==="; cat "$d/pupazzi_stato.txt"; }
    [ -f "$d/pupazzi_log.txt" ] && { echo; echo "=== registro del gioco (ultime 300 righe) ==="; tail -n 300 "$d/pupazzi_log.txt"; }
    for f in "$d"/retroarch*.log; do
      [ -f "$f" ] || continue
      echo
      echo "=== ${f##*/}: righe principali ==="
      grep -a -v -F '[pupazzi]' "$f" |
        grep -a -E 'ERROR|WARN|\[GL\]|\[GLCore\]|\[Mali\]|\[EGL\]|\[Video\]|\[Audio\]|\[Input\]|\[Network\]|\[Autoconf\]|RetroArch [0-9]|Built:' |
        head -n 100
    done
  } > "$out" 2> /dev/null
  echo "Scaricato in $d"
  echo "File da mandare: $out ($(wc -c < "$out") byte)"
  if [ "$incomplete" = 1 ]; then
    echo "ATTENZIONE: il trasferimento si e' interrotto, i file sono incompleti. Riprova." >&2
    return 1
  fi
}

cmd_rimuovi() {
  ssh_run "true" || die "la console non risponde su $IP"
  ssh_run "cd '$APPS' && rm -rf pupazzi Pupazzi.sh Imgs/Pupazzi.png $OLD \
/mnt/vendor/deep/retro/config/Pupazzi /tmp/pupazzi_retroarch.cfg /tmp/pupazzi_out && sync" ||
    die "rimozione non riuscita o incompleta (scheda in sola lettura? connessione caduta?)"
  echo "Rimosso da $IP."
}

cmd_mappa() {
  local f="${1:-}" name
  [ -n "$f" ] && [ -f "$f" ] || die "indica il file della mappa, per esempio: ./deploy.sh mappa terra1.txt"
  name="$(basename "$f")"
  case "$name" in
    *.txt) ;;
    *) die "il file della mappa deve finire in .txt (stesso nome di quella di serie, per esempio terra1.txt)" ;;
  esac
  ssh_run "true" || die "la console non risponde su $IP"
  ssh_run "mkdir -p '$APPS/pupazzi/mappe' && cat > '$APPS/pupazzi/mappe/$name' && sync" < "$f" ||
    die "copia della mappa non riuscita"
  echo "Mappa $name copiata. Al prossimo avvio il gioco la usa (se ha errori, il menu delle mappe dice quali)."
}

cmd_togli_mappa() {
  local name="${1:-}"
  [ -n "$name" ] || die "indica il nome della mappa, per esempio: ./deploy.sh togli-mappa terra1"
  name="${name%.txt}"
  ssh_run "true" || die "la console non risponde su $IP"
  ssh_run "rm -f '$APPS/pupazzi/mappe/$name.txt' && sync" || die "rimozione non riuscita"
  echo "Tolta la mappa modificata $name: torna quella di serie."
}

cmd_sd() {
  need_dist
  local roms="${1:-}"
  [ -n "$roms" ] && [ -d "$roms" ] || die "indica la cartella Roms della scheda, per esempio: ./deploy.sh sd /media/${USER:-utente}/NOME/Roms"
  roms="${roms%/}"
  [ -d "$roms/Roms/APPS" ] && roms="$roms/Roms"
  [ -d "$roms/APPS" ] || die "in $roms non c'e' la cartella APPS: non sembra la cartella Roms della console"
  (cd "$roms/APPS" && rm -rf $OLD)
  cp -r "$SRC/." "$roms/APPS/" || die "copia fallita (serve sudo?)"
  sync
  local here there
  here="$(cd "$SRC" && file_list | xargs md5sum)"
  there="$(cd "$roms/APPS" && file_list | xargs md5sum)"
  [ "$here" = "$there" ] || die "i file sulla scheda non corrispondono a quelli del PC"
  echo "Copiato e verificato in $roms/APPS. Smonta la scheda prima di toglierla."
}

case "${1:-}" in
  installa)  IP="${2:-$IP}"; cmd_installa ;;
  risultati) IP="${2:-$IP}"; cmd_risultati ;;
  rimuovi)   IP="${2:-$IP}"; cmd_rimuovi ;;
  sd)        cmd_sd "${2:-}" ;;
  mappa)     IP="${3:-$IP}"; cmd_mappa "${2:-}" ;;
  togli-mappa) IP="${3:-$IP}"; cmd_togli_mappa "${2:-}" ;;
  *)         sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//' ;;
esac
exit $?
