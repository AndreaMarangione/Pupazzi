#!/bin/bash
# Pupazzi - entry in the console's APPS menu. Everything else is in the pupazzi/ folder next to it.
progdir="$(cd "$(dirname "$0")" || exit; pwd)/pupazzi"
exec /bin/bash "$progdir/run.sh" gpu
