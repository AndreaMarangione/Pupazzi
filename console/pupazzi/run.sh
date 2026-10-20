#!/bin/bash
# Pupazzi - avvio sulla console (Anbernic RG35XX Pro con Stock OS MOD).
#
#   run.sh         il gioco, disegnato dalla GPU (normale)
#   run.sh sw      il gioco disegnato dalla CPU (solo per confronto, in caso di problemi con la GPU)
#   run.sh gpu64 | sw64   lo stesso con il RetroArch a 64 bit
#
# Usa il RetroArch della console. La configurazione di RetroArch non viene modificata: a ogni
# avvio se ne fa una copia in /tmp con le poche voci che servono qui. I registri finiscono
# nella cartella out/ accanto a questo script (./deploy.sh risultati li porta sul PC).

MODE="${1:-gpu}"
HERE="$(cd "$(dirname "$0")" && pwd)"
RA_DIR="/mnt/vendor/deep/retro"
RA32="$RA_DIR/retroarch"
RA64="$RA_DIR/retroarch64"
RA_CFG="/.config/retroarch/retroarch.cfg"
RA_CFG_FACTORY="$RA_DIR/retroarch.cfg"       # quella di fabbrica, se manca quella dell'utente
RUN_CFG=""
CORE32="$HERE/arm32/pupazzi_libretro.so"
CORE64="$HERE/arm64/pupazzi_libretro.so"
# Il RetroArch a 64 bit della console ha bisogno di una SDL2 piu' recente di quella in /usr/lib.
SDL64="/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.2800.5"

# Stesse librerie che il menu della console prepara per le applicazioni.
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib32:/usr/lib:/mnt/vendor/lib}"

# usable CARTELLA: ci si puo' scrivere e c'e' almeno 1 MB libero?
usable() {
  local d="$1" free
  mkdir -p "$d" 2>/dev/null || return 1
  ( : > "$d/.prova" ) 2>/dev/null || return 1
  rm -f "$d/.prova"
  free=$(df -Pk "$d" 2>/dev/null | awk 'NR == 2 { print $4 }')
  case "$free" in
    ''|*[!0-9]*) return 0 ;;        # df non risponde: ci si fida della prova di scrittura
  esac
  [ "$free" -ge 1024 ]
}

# Risultati accanto allo script; con la scheda piena o in sola lettura si ripiega su /tmp.
OUT="$HERE/out"
if ! usable "$OUT"; then
  OUT="/tmp/pupazzi_out"
  mkdir -p "$OUT"
fi
note() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$OUT/avvio.log" 2>/dev/null; }

# I log crescono a ogni avvio: oltre i 300 KB si riparte, tenendo la copia precedente.
rotate() {
  local f="$1" size
  size=$(stat -c %s "$f" 2>/dev/null || echo 0)
  if [ "$size" -gt 300000 ]; then mv -f "$f" "$f.vecchio"; fi
}

# Copia della configurazione di RetroArch con le voci necessarie al gioco:
#  - SELECT+START chiude RetroArch e si torna al menu della console;
#  - niente viene salvato nella configurazione o nella cronologia dell'utente;
#  - il Remote RetroPad di RetroArch resta spento (network_remote_enable*): le porte da 55400 a
#    55403, dove le altre console che fanno da controller mandano i comandi (giocatori da 1 a 4),
#    le apre il gioco, che a ogni fotogramma legge tutti i comandi arrivati (RetroArch ne leggeva
#    uno solo e i comandi arrivavano sempre piu' in ritardo). Sulle altre console non cambia nulla.
# Lascia in RUN_CFG il percorso della copia; fallisce se non riesce a scriverla da nessuna parte.
make_cfg() {
  local keys='load_dummy_on_core_shutdown|config_save_on_exit|history_list_enable|network_remote_enable|network_remote_enable_user_p[1-4]'
  local base="$RA_CFG" target
  [ -f "$base" ] || base="$RA_CFG_FACTORY"
  [ -f "$base" ] || { base=""; note "ATTENZIONE: nessuna configurazione di RetroArch trovata, si usano i valori predefiniti"; }
  for target in /tmp/pupazzi_retroarch.cfg "$OUT/pupazzi_retroarch.cfg"; do
    {
      [ -n "$base" ] && grep -v -E "^[[:space:]]*($keys)[[:space:]]*=" "$base"
      cat <<'CFG'
load_dummy_on_core_shutdown = "false"
config_save_on_exit = "false"
history_list_enable = "false"
network_remote_enable = "false"
network_remote_enable_user_p1 = "false"
network_remote_enable_user_p2 = "false"
network_remote_enable_user_p3 = "false"
network_remote_enable_user_p4 = "false"
CFG
    } > "$target" 2>/dev/null
    if grep -q '^network_remote_enable_user_p4 = "false"$' "$target" 2>/dev/null; then
      RUN_CFG="$target"
      return 0
    fi
  done
  return 1
}

# run_ra BIT RENDERER CARTELLA SECONDI NOME_LOG [VAR=valore ...]
# Avvia RetroArch (32 o 64 bit) con il core corrispondente. SECONDI = 0: nessun limite di tempo.
# Il log di RetroArch va in CARTELLA/NOME_LOG.
run_ra() {
  local bits="$1" rend="$2" out="$3" limit="$4" log="$3/$5"
  shift 5
  local bin="$RA32" core="$CORE32" pre=""
  if [ "$bits" = 64 ]; then
    bin="$RA64"
    core="$CORE64"
    [ -f "$SDL64" ] && pre="$SDL64"
  fi
  mkdir -p "$out"
  rm -f "$out/pupazzi_stato.txt"
  rotate "$out/pupazzi_log.txt"
  if [ ! -x "$bin" ]; then echo "non trovato: $bin" > "$log"; return 127; fi
  if [ ! -f "$core" ]; then echo "non trovato: $core" > "$log"; return 127; fi
  local cmd=(env "PUPAZZI_OUT=$out" "PUPAZZI_RENDERER=$rend" "PUPAZZI_MAPS=$HERE/mappe" "$@")
  [ -n "$pre" ] && cmd+=("LD_PRELOAD=$pre")
  cmd+=("$bin" -v -c "$RUN_CFG" -L "$core")
  local rc
  if [ "$limit" -gt 0 ]; then
    # --foreground: RetroArch resta nello stesso gruppo di processi di un avvio normale.
    timeout --foreground -k 15 "$limit" "${cmd[@]}" > /dev/null 2> "$log"
  else
    "${cmd[@]}" > /dev/null 2> "$log"
  fi
  rc=$?
  note "retroarch a $bits bit, renderer $rend: uscita $rc"
  return $rc
}

# Il core scrive in pupazzi_stato.txt come e' finita la sessione (e a che punto era la partita).
status_of() {
  sed -n 's/^uscita=//p' "$1/pupazzi_stato.txt" 2>/dev/null | head -n 1
}

cpu_info() {
  local c g f lo hi
  echo "cpu accese: $(cat /sys/devices/system/cpu/online 2>/dev/null)  presenti: $(cat /sys/devices/system/cpu/present 2>/dev/null)"
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    [ -d "$c/cpufreq" ] || continue
    g=$(cat "$c/cpufreq/scaling_governor" 2>/dev/null)
    f=$(cat "$c/cpufreq/scaling_cur_freq" 2>/dev/null)
    lo=$(cat "$c/cpufreq/scaling_min_freq" 2>/dev/null)
    hi=$(cat "$c/cpufreq/scaling_max_freq" 2>/dev/null)
    echo "${c##*/}: governor=$g freq=$f kHz (min $lo, max $hi)"
  done
  for c in /sys/class/thermal/thermal_zone*; do
    [ -r "$c/temp" ] && echo "${c##*/} ($(cat "$c/type" 2>/dev/null)): $(cat "$c/temp" 2>/dev/null)"
  done
}

system_info() {
  echo "--- console ---"
  uname -srm
  if [ -r /proc/device-tree/model ]; then tr -d '\0' < /proc/device-tree/model; echo; fi
  grep -m 1 -E '^(Hardware|model name)' /proc/cpuinfo 2>/dev/null
  grep -m 1 -E '^Features' /proc/cpuinfo 2>/dev/null
  grep -E '^(MemTotal|MemAvailable)' /proc/meminfo 2>/dev/null
  echo "hdmi: $(cat /sys/class/extcon/hdmi/state 2>/dev/null)"
  echo "fb0: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null) a $(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null) bit"
  cpu_info
  echo "ambiente: utente $(id -u 2>/dev/null), HOME=${HOME-<non impostata>}, cartella $(pwd), stdin $(readlink /proc/$$/fd/0 2>/dev/null)"
  echo "risultati in: $OUT   configurazione usata: $RUN_CFG"
  echo
  echo "--- RetroArch ---"
  ls -l "$RA32" "$RA64" 2>&1
  echo "configurazione di partenza: $RA_CFG"
  grep -E '^(video_driver|video_context_driver|video_threaded|video_vsync|video_swap_interval|video_max_swapchain_images|video_hard_sync|video_frame_delay|video_refresh_rate|video_scale_integer|video_smooth|video_shader_enable|aspect_ratio_index|audio_driver|audio_sync|audio_latency|input_driver|input_joypad_driver|input_max_users|run_ahead_enabled|rewind_enable)[[:space:]]*=' "$RA_CFG" 2>/dev/null
}

rotate "$OUT/avvio.log"
note "avvio: $MODE (pacchetto $(cat "$HERE/VERSIONE" 2>/dev/null))"
if ! make_cfg; then
  note "ERRORE: impossibile scrivere la copia della configurazione, RetroArch non viene avviato"
  exit 1
fi
# Stessa cartella di lavoro da cui il menu della console avvia RetroArch.
cd "$RA_DIR" 2>/dev/null
# Com'e' messa la console a ogni avvio (CPU, memoria, HDMI, RetroArch): aiuta a leggere i registri.
system_info > "$OUT/console.txt" 2>&1
case "$MODE" in
  gpu)   run_ra 32 gpu "$OUT" 0 retroarch.log ;;
  sw)    run_ra 32 software "$OUT" 0 retroarch_sw.log ;;
  gpu64) run_ra 64 gpu "$OUT" 0 retroarch_gpu64.log ;;
  sw64)  run_ra 64 software "$OUT" 0 retroarch_sw64.log ;;
  *)     echo "uso: run.sh [gpu|sw|gpu64|sw64]" ;;
esac
st=$(status_of "$OUT")
note "fine: ${st:-nessuno stato} ($(tr '\n' ' ' < "$OUT/pupazzi_stato.txt" 2>/dev/null))"
rm -f "$RUN_CFG"
sync
exit 0
