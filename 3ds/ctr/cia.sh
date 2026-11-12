#!/bin/sh
# Packs ctr/pokedaisy.cia from what `make cia-parts` left in build-release/: run in this folder by `make cia` in ../
# (in an ubuntu:24.04 container: makerom's release binaries want a newer glibc than the devkitARM image has).
#   cia.sh <makerom>
set -eu
MAKEROM="${1:-makerom}"
B=build-release
"$MAKEROM" -f cia -o pokedaisy.cia -elf "$B/pokedaisy.elf" -rsf "$B/pokedaisy.rsf" -target t -exefslogo \
    -icon "$B/icon.icn" -banner "$B/banner.bnr" -major 0 -minor 0 -micro 2
echo "built pokedaisy.cia"
