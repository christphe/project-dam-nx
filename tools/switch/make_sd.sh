#!/bin/sh
# project-dam: assemble the SD-card layout for the Switch build.
#
#   tools/switch/make_sd.sh /path/to/rom.z64 [out_dir]
#
# Runs inside the build container (local/project-dam-build), from the repo
# root, after `docker compose run --rm switch`. Produces:
#
#   <out_dir>/switch/project-dam/project-dam.nro
#   <out_dir>/switch/project-dam/data/ge007.ntsc-final.z64   (copy of your ROM)
#   <out_dir>/switch/project-dam/data/pcmodels-u/, pccg-u/   (converted sidecars)
#
# The sidecars are what the PC build generates on first launch through the
# external prepare-assets converter; Horizon cannot spawn it, so they are
# generated here (same assembly as tools_pc/bundle-linux.sh). Copy
# <out_dir>/switch to the root of the SD card. Nothing here is committed.
set -eu

ROM="${1:?usage: make_sd.sh /path/to/rom.z64 [out_dir]}"
OUT="${2:-build-switch/sd}"
GAME="$OUT/switch/project-dam"
PREP="build-switch/prepare-assets"

[ -f build-switch/ge007.aarch64.nro ] || { echo "error: build the NRO first (docker compose run --rm switch)" >&2; exit 1; }

# --- converter, assembled like tools_pc/bundle-linux.sh -------------------
rm -rf "$PREP"
mkdir -p "$PREP/vendor/scripts" "$PREP/vendor/assets/obseg"
cp tools_pc/dist/prepare-assets/prepare-assets.py "$PREP/"
cp tools_pc/d43_emit.py tools_pc/d69_emit.py       "$PREP/"
cp tools_pc/d88_emit.py tools_pc/d88_propdefs.py   "$PREP/"
cp scripts/filelist.u.csv                          "$PREP/vendor/scripts/"
cp assets/obseg/file_resource_table.inc.c          "$PREP/vendor/assets/obseg/"
find assets -iname 'modelfileheader.inc.c' -print0 \
  | xargs -0 -I{} cp --parents {} "$PREP/vendor/"

# --- SD layout --------------------------------------------------------------
mkdir -p "$GAME/data"
cp build-switch/ge007.aarch64.nro "$GAME/project-dam.nro"
cp "$ROM" "$GAME/data/ge007.ntsc-final.z64"
python3 -I "$PREP/prepare-assets.py" --rom "$GAME/data/ge007.ntsc-final.z64" --out "$GAME"

echo "SD layout ready: $OUT/switch  (copy it to the root of the SD card)"
