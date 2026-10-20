#!/bin/bash
# Pupazzi - voce del menu APPS della console. Tutto il resto e' nella cartella pupazzi/ qui accanto.
progdir="$(cd "$(dirname "$0")" || exit; pwd)/pupazzi"
exec /bin/bash "$progdir/run.sh" gpu
