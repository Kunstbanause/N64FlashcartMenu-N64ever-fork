#!/usr/bin/env bash
# Build the SD-card cover-art payload for N64ever, with pre-generated .cache files.
#
# Produces  sdcard-boxart-full/menu/metadata/<C>/<O>/<D>/{boxart_front,boxart_back,gamepak_front}.png
# plus a matching .cache beside each PNG, so covers appear instantly from the very first boot
# instead of being PNG-decoded on the console.
#
# Layout rationale: a ROM code is 4 chars where bytes 0-2 are the game identity and byte 3 is the
# region. boxart.c's try_metadata_png() does a path_pop (strips one segment), so covers placed at
# the 3-char level match ANY region dump of the same game. One cover serves E/P/J/U/...
#
# Only front/back/cart load from SD metadata. box3d / cart3d / logo are DFS-or-custom only and are
# deliberately not copied.
#
# Usage:
#   ./tools/make_sdcard_boxart.sh            # build into ./sdcard-boxart-full
#   ./tools/make_sdcard_boxart.sh /path/out  # build into a custom directory
#
# Then copy the resulting menu/ onto your SD card root (merge with any existing menu/).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/assets/images/boxart"
OUT="${1:-$REPO/sdcard-boxart-full}"
SPNGDIR="$REPO/src/libs/libspng/spng"
# NOTE: not .hosttools/bin — that directory is created by Docker and is root-owned, so an
# unprivileged host compile into it fails. .hostbin/ is ours.
GEN="$REPO/.hostbin/gen_boxart_cache"

[ -d "$SRC" ] || { echo "error: no box-art source at $SRC" >&2; exit 1; }
[ -f "$SPNGDIR/spng.c" ] || { echo "error: libspng submodule missing ($SPNGDIR). Run: git submodule update --init" >&2; exit 1; }

# --- 1. build the cache generator against the fork's OWN libspng --------------
# Using the same libspng the firmware uses is what makes the output byte-identical.
if [ ! -x "$GEN" ] || [ "$REPO/tools/gen_boxart_cache.c" -nt "$GEN" ]; then
  echo "[1/3] compiling gen_boxart_cache against the fork's libspng..."
  mkdir -p "$(dirname "$GEN")"
  cc -O2 -o "$GEN" "$REPO/tools/gen_boxart_cache.c" "$SPNGDIR/spng.c" -I "$SPNGDIR" -lz -lm
else
  echo "[1/3] gen_boxart_cache already built"
fi

# --- 2. lay out the SD tree ---------------------------------------------------
echo "[2/3] laying out cover library..."
rm -rf "$OUT"; mkdir -p "$OUT"

prefixes=$(ls "$SRC" | grep -E '^[A-Za-z0-9]{4}$' | cut -c1-3 | sort -u)
games=0; with_front=0; files=0

for p in $prefixes; do
  games=$((games+1))
  # Pick one source dir per game; prefer widely-available regions first.
  srcdir=""
  for r in E P J U A X F D I S; do
    if [ -d "$SRC/$p$r" ]; then srcdir="$SRC/$p$r"; break; fi
  done
  [ -z "$srcdir" ] && srcdir=$(ls -d "$SRC/$p"? 2>/dev/null | head -1)
  [ -z "$srcdir" ] && continue

  dest="$OUT/menu/metadata/${p:0:1}/${p:1:1}/${p:2:1}"
  mkdir -p "$dest"
  if [ -f "$srcdir/front.png" ]; then cp "$srcdir/front.png" "$dest/boxart_front.png"; with_front=$((with_front+1)); files=$((files+1)); fi
  if [ -f "$srcdir/back.png"  ]; then cp "$srcdir/back.png"  "$dest/boxart_back.png";  files=$((files+1)); fi
  if [ -f "$srcdir/cart.png"  ]; then cp "$srcdir/cart.png"  "$dest/gamepak_front.png"; files=$((files+1)); fi
done

echo "      unique games (3-char prefixes): $games"
echo "      games with a front cover:       $with_front"
echo "      image files written:            $files"

# --- 3. pre-generate .cache beside every PNG ---------------------------------
# The menu reads the PNG's size before trusting the cache, so the PNGs MUST stay on the card.
# That check also makes it self-healing: swap a cover and its stale cache is rebuilt on device.
echo "[3/3] pre-generating .cache files..."
find "$OUT" -name '*.png' -print0 | xargs -0 "$GEN"

pngs=$(find "$OUT" -name '*.png'   | wc -l)
caches=$(find "$OUT" -name '*.cache' | wc -l)
echo "      PNGs=$pngs caches=$caches"
find "$OUT" -name '*.png' | while read -r p; do
  [ -f "${p%.png}.cache" ] || echo "      WARN no cache: $p"
done | head

echo
echo "done -> $OUT"
echo "total payload: $(du -sh "$OUT" | cut -f1)"
echo "Copy '$OUT/menu' onto your SD card root (merge into any existing menu/)."
