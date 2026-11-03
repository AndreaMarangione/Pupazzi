#!/bin/bash
# Release notes: the section of CHANGELOG.md for this version, followed by the install steps.
# Usage: release_notes.sh VERSION ARCHIVE_NAME
set -euo pipefail
version=$1; archive=$2
section=$(awk -v v="$version" '
  /^## \[/ { if (on) exit; if (index($0, "## [" v "]") == 1) { on = 1; next } }
  on { print }
' CHANGELOG.md)
if [ -z "$(printf '%s' "$section" | tr -d '[:space:]')" ]; then
  echo "CHANGELOG.md has no section for $version" >&2
  exit 1
fi
printf '%s\n' "$section" | sed -e '/./,$!d'
cat <<NOTES

### Installazione

Console accesa, in Wi-Fi, con SSH attivo. Dal PC:

\`\`\`
tar -xzf $archive
cd pupazzi
./deploy.sh installa IP_DELLA_CONSOLE
\`\`\`

Senza SSH: scheda della console nel PC e \`./deploy.sh sd /percorso/della/scheda\`. Sulla console:
**App Center → APPS → Pupazzi**. Per il gioco in rete va installata su ogni console. Sul PC Linux:
\`retroarch -L pupazzi/dist/pc/pupazzi_libretro.so\`.
NOTES
