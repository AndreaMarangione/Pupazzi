#!/bin/bash
# Release archive: the sources of HEAD (git archive, without the files marked export-ignore in
# .gitattributes) plus dist/ already built (console and PC cores), in a reproducible tar.gz.
# Usage: package.sh VERSION BUILD_ID OUTPUT.tar.gz   (run from the top of the repository after make dist)
set -euo pipefail
version=$1; build=$2; out=$3
[ -f dist/APPS/pupazzi/arm32/pupazzi_libretro.so ] || { echo "dist/ missing: run make dist first" >&2; exit 1; }
grep -qx "$version build $build" dist/APPS/pupazzi/VERSIONE || { echo "dist/ is not version $version build $build" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive --format=tar --prefix=pupazzi/ HEAD | tar -x -C "$tmp"
cp -r dist "$tmp/pupazzi/"
stamp=$(git log -1 --format=%ct)
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$stamp" -C "$tmp" -cf - pupazzi | gzip -9 -n > "$out"
echo "$out: $(tar -tzf "$out" | wc -l) entries, $(du -k "$out" | cut -f1) KB"
