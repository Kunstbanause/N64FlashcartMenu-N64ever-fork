#!/usr/bin/env bash
# Reproducible N64FlashcartMenu ROM build via Docker.
#
# The dev image ships only the mips64-elf cross-tools; libdragon itself and the
# libdragon *host* tools (n64sym/n64tool/n64elfcompress) are not installed. We:
#   1. install the prebuilt libdragon library artifacts from the submodule, and
#   2. install Linux host-tool binaries from repo/.hosttools/bin (built once,
#      see below) so the .z64 link step actually runs to completion.
#
# Without the host tools the z64 recipe aborts on the very first command
# (n64sym), which silently leaves output/sc64menu.n64 STALE.
#
# To (re)build the host tools (only needed once, or after a libdragon bump):
#   docker run --rm -v "$PWD/repo:/project" n64flashcartmenu-dev:latest bash -lc '
#     cp -r /project/libdragon /tmp/ld && cd /tmp/ld/tools && find . -name "*.o" -delete
#     N64_INST=/opt/libdragon make n64sym n64tool n64elfcompress
#     mkdir -p /project/.hosttools/bin
#     cp n64sym/n64sym n64tool n64elfcompress/n64elfcompress /project/.hosttools/bin/'
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"

# Host tools are NOT shipped prebuilt (some embed GPL/FTL libs -- see build-tools.sh).
# Build them from the bundled libdragon source the first time, into .hosttools/bin/.
if [ ! -x "$REPO/.hosttools/bin/mksprite" ]; then
  "$REPO/build-tools.sh"
fi

# Resolve the libdragon submodule version on the HOST (git can't reach the
# submodule's gitdir from inside the container). Baked into credits.c so Menu
# Information shows a real SDK version instead of the empty rompak fields.
LIBDRAGON_VERSION="$(git -C "$REPO/libdragon" describe --tags --always --dirty 2>/dev/null || true)"
[ -z "$LIBDRAGON_VERSION" ] && LIBDRAGON_VERSION="unknown"

# Upstream N64FlashcartMenu version this fork is based on (nearest tag on the menu repo).
# Baked into credits.c so Menu Information shows which N64FlashcartMenu release it derives from.
MENU_BASE_VERSION="$(git -C "$REPO" describe --tags --abbrev=0 2>/dev/null || true)"
[ -z "$MENU_BASE_VERSION" ] && MENU_BASE_VERSION="unknown"

# --- Cover art mode ----------------------------------------------------------
# Default 0 = covers on SD card, ~1.9 MB ROM (required for ED64 X-series; see Makefile).
# Override with: BAKE_BOXART=1 ./build-rom.sh
BAKE_BOXART="${BAKE_BOXART:-0}"

# --- Build hygiene -----------------------------------------------------------
# mkdfs packs the ENTIRE filesystem/ tree, and `make clean` only removes the files
# it currently lists -- so leftovers get silently baked into the ROM:
#   * macOS .DS_Store (junk), and
#   * orphan *.sprite
find "$REPO/filesystem" "$REPO/assets" -name '.DS_Store' -delete 2>/dev/null || true

# NOTE: pruning baked sprites for BAKE_BOXART=0 happens INSIDE the container (see below).
# Docker runs as root and creates root-owned files under filesystem/ and build/, which an
# unprivileged host `rm` cannot delete.
if [ -d "$REPO/filesystem" ]; then
  orphans=0
  while IFS= read -r spr; do
    png="$REPO/assets/images/${spr#"$REPO"/filesystem/}"; png="${png%.sprite}.png"
    if [ ! -f "$png" ]; then rm -f "$spr"; orphans=$((orphans+1)); fi
  done < <(find "$REPO/filesystem" -name '*.sprite' 2>/dev/null)
  find "$REPO/filesystem" -type d -empty -delete 2>/dev/null || true
  [ "$orphans" -gt 0 ] && echo "[hygiene] removed $orphans orphan"
fi

docker run --rm -e LIBDRAGON_VERSION="$LIBDRAGON_VERSION" -e MENU_BASE_VERSION="$MENU_BASE_VERSION" -e BAKE_BOXART="$BAKE_BOXART" -v "$REPO:/project" -w /project n64flashcartmenu-dev:latest bash -c '
  set -e
  N64_INST=/opt/libdragon

  # libdragon library artifacts from the submodule
  mkdir -p $N64_INST/include $N64_INST/mips64-elf/lib $N64_INST/mips64-elf/include
  cp /project/libdragon/n64.mk $N64_INST/include/n64.mk
  cp /project/libdragon/libdragon.a /project/libdragon/libdragonsys.a \
     /project/libdragon/n64.ld /project/libdragon/dso.ld /project/libdragon/rsp.ld \
     $N64_INST/mips64-elf/lib/
  cp -r /project/libdragon/include/. $N64_INST/mips64-elf/include/
  mkdir -p $N64_INST/mips64-elf/include/libcart
  cp /project/libdragon/src/libcart/cart.h $N64_INST/mips64-elf/include/libcart/
  # The menu uses <fatfs/ff.h>, which lives in the libdragon src tree, not its
  # public include dir. Install those headers too so recompiling any fatfs-using
  # file works. Without it: "fatfs/ff.h: No such file or directory".
  mkdir -p $N64_INST/mips64-elf/include/fatfs
  cp /project/libdragon/src/fatfs/*.h $N64_INST/mips64-elf/include/fatfs/

  # Linux host tools (persisted in repo/.hosttools/bin)
  cp /project/.hosttools/bin/* $N64_INST/bin/

  # mkdfs packs the ENTIRE filesystem/ tree regardless of the Makefile FILESYSTEM list, so with
  # BAKE_BOXART=0 previously-baked sprites would still be packed and silently yield a ~48 MB ROM.
  # Done here (as root) because Docker owns those files. assets/ is never touched.
  if [ "$BAKE_BOXART" != "1" ] && [ -d /project/filesystem/boxart ]; then
    echo "[hygiene] BAKE_BOXART=0 -> dropping baked filesystem/boxart (covers load from SD)"
    rm -rf /project/filesystem/boxart
    rm -f /project/build/*.dfs   # force a repack; mkdfs only rebuilds on newer mtimes
  fi

  N64_INST=$N64_INST make -j$(nproc) LIBDRAGON_VERSION="$LIBDRAGON_VERSION" MENU_BASE_VERSION="$MENU_BASE_VERSION" BAKE_BOXART="$BAKE_BOXART" "$@"
' -- "$@"
