#!/bin/sh
# Genera il sorgente C++ con le mappe incorporate nel core, nell'ordine della campagna.
#   sh tools/embed_maps.sh maps/terra1.txt ... > build/gen/maps_data.cpp
echo '// Generato da tools/embed_maps.sh: non modificare, si cambiano i file in maps/.'
echo '#include "maps.h"'
echo 'const MapText MAP_TEXTS[] = {'
for f in "$@"; do
  id=$(basename "$f" .txt)
  printf '  { "%s",\n' "$id"
  sed -e 's/\r$//' -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' "$f"
  printf '  },\n'
done
echo '};'
echo "const int MAP_TEXT_COUNT = $#;"
