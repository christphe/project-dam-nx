#!/bin/sh
# project-dam: assemble the release archive from a finished Switch build.
#
#   tools/switch/package_release.sh <version> [out_dir]
#
# Run from the repo root after the NRO is built (build-switch/). Produces
# <out_dir>/project-dam-<version>.zip with:
#
#   switch/project-dam/project-dam.nro   copy switch/ to the root of the SD card
#   prepare-assets/                      the ROM converter (Python 3, run on a PC)
#   README.txt, LICENSE, NOTICE
#
# No game content: players supply their own ROM, and prepare-assets builds
# the converted model/level files from it.
set -eu

VERSION="${1:?usage: package_release.sh <version> [out_dir]}"
OUT="${2:-dist}"
NRO=build-switch/ge007.aarch64.nro
[ -f "$NRO" ] || { echo "error: $NRO missing; build the NRO first" >&2; exit 1; }

STAGE="$OUT/project-dam-$VERSION"
rm -rf "$STAGE"
mkdir -p "$STAGE/switch/project-dam/data"

cp "$NRO" "$STAGE/switch/project-dam/project-dam.nro"

# The converter, assembled like tools_pc/bundle-linux.sh (and make_sd.sh).
PREP="$STAGE/prepare-assets"
mkdir -p "$PREP/vendor/scripts" "$PREP/vendor/assets/obseg"
cp tools_pc/dist/prepare-assets/prepare-assets.py "$PREP/"
cp tools_pc/d43_emit.py tools_pc/d69_emit.py       "$PREP/"
cp tools_pc/d88_emit.py tools_pc/d88_propdefs.py   "$PREP/"
cp scripts/filelist.u.csv                          "$PREP/vendor/scripts/"
cp assets/obseg/file_resource_table.inc.c          "$PREP/vendor/assets/obseg/"
find assets -iname 'modelfileheader.inc.c' -print0 \
  | xargs -0 -I{} cp --parents {} "$PREP/vendor/"

cp LICENSE NOTICE "$STAGE/"
sed "s/@VERSION@/$VERSION/g" tools/switch/README-release.txt > "$STAGE/README.txt"

(cd "$OUT" && rm -f "project-dam-$VERSION.zip" && zip -qr "project-dam-$VERSION.zip" "project-dam-$VERSION")
echo "$OUT/project-dam-$VERSION.zip"
