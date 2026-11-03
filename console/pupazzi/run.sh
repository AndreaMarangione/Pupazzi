#!/bin/bash
# Pupazzi - launch on the console (Anbernic RG35XX Pro with Stock OS MOD).
#
#   run.sh         the game, drawn by the GPU (normal)
#   run.sh sw      the game drawn by the CPU (only for comparison, in case of problems with the GPU)
#   run.sh gpu64 | sw64   the same with the 64-bit RetroArch
#
# Uses the console's RetroArch. The RetroArch configuration is not modified: at every launch a
# copy of it is made in /tmp with the few entries needed here. The logs end up in the out/
# folder next to this script (./deploy.sh risultati brings them to the PC).

MODE="${1:-gpu}"
HERE="$(cd "$(dirname "$0")" && pwd)"
RA_DIR="/mnt/vendor/deep/retro"
RA32="$RA_DIR/retroarch"
RA64="$RA_DIR/retroarch64"
RA_CFG="/.config/retroarch/retroarch.cfg"
RA_CFG_FACTORY="$RA_DIR/retroarch.cfg"       # the factory one, if the user's one is missing
RUN_CFG=""
CORE32="$HERE/arm32/pupazzi_libretro.so"
CORE64="$HERE/arm64/pupazzi_libretro.so"
# The console's 64-bit RetroArch needs a newer SDL2 than the one in /usr/lib.
SDL64="/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.2800.5"

# Same libraries that the console menu sets up for applications.
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib32:/usr/lib:/mnt/vendor/lib}"

# usable DIR: is it writable and is there at least 1 MB free?
usable() {
  local d="$1" free
  mkdir -p "$d" 2>/dev/null || return 1
  ( : > "$d/.prova" ) 2>/dev/null || return 1
  rm -f "$d/.prova"
  free=$(df -Pk "$d" 2>/dev/null | awk 'NR == 2 { print $4 }')
  case "$free" in
    ''|*[!0-9]*) return 0 ;;        # df does not answer: trust the write test
  esac
  [ "$free" -ge 1024 ]
}

# Results next to the script; with the SD card full or read-only, fall back to /tmp.
OUT="$HERE/out"
if ! usable "$OUT"; then
  OUT="/tmp/pupazzi_out"
  mkdir -p "$OUT"
fi
note() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$OUT/avvio.log" 2>/dev/null; }

# The logs grow at every launch: beyond 300 KB they start over, keeping the previous copy.
rotate() {
  local f="$1" size
  size=$(stat -c %s "$f" 2>/dev/null || echo 0)
  if [ "$size" -gt 300000 ]; then mv -f "$f" "$f.vecchio"; fi
}

# Copy of the RetroArch configuration with the entries the game needs:
#  - SELECT+START closes RetroArch and goes back to the console menu;
#  - nothing is saved in the user's configuration or history;
#  - RetroArch's Remote RetroPad stays off (network_remote_enable*): ports 55400 to 55403, where
#    the other consoles acting as controllers send their commands (players 1 to 4), are opened by
#    the game, which at every frame reads all the commands that have arrived (RetroArch read only
#    one of them and the commands arrived later and later). Nothing changes on the other consoles.
# Leaves the path of the copy in RUN_CFG; fails if it cannot write it anywhere.
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

# run_ra BITS RENDERER DIR SECONDS LOG_NAME [VAR=value ...]
# Starts RetroArch (32 or 64 bit) with the matching core. SECONDS = 0: no time limit.
# The RetroArch log goes to DIR/LOG_NAME.
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
    # --foreground: RetroArch stays in the same process group as in a normal launch.
    timeout --foreground -k 15 "$limit" "${cmd[@]}" > /dev/null 2> "$log"
  else
    "${cmd[@]}" > /dev/null 2> "$log"
  fi
  rc=$?
  note "retroarch a $bits bit, renderer $rend: uscita $rc"
  return $rc
}

# The core writes to pupazzi_stato.txt how the session ended (and how far the game had got).
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
# Same working directory from which the console menu starts RetroArch.
cd "$RA_DIR" 2>/dev/null
# State of the console at every launch (CPU, memory, HDMI, RetroArch): helps when reading the logs.
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
