#!/usr/bin/env bash
# One-time: build the libdragon HOST tools from the bundled source into .hosttools/bin/.
#
# We do NOT ship prebuilt host-tool binaries, on purpose: some of them statically embed
# third-party libraries with their own licenses (notably x264 = GPL-2.0 inside mksprite, and
# FreeType inside mkfont). Distributing those *binaries* would carry GPL/FTL obligations.
# Building them locally from the included libdragon source sidesteps all of that — you compile
# public-domain libdragon + its vendored libs yourself; nothing license-encumbered is
# redistributed by this project.
#
# build-rom.sh runs this automatically the first time if .hosttools/bin is missing the tools.
set -euo pipefail
REL="$(cd "$(dirname "$0")" && pwd)"

echo "[build-tools] compiling libdragon host tools from source (one-time)..."
docker run --rm -v "$REL:/project" -w /project n64flashcartmenu-dev:latest bash -lc '
  set -e
  N64_INST=/opt/libdragon
  # A few tools include libdragon headers / n64.mk:
  mkdir -p $N64_INST/include $N64_INST/mips64-elf/include
  cp /project/libdragon/n64.mk $N64_INST/include/n64.mk
  cp -r /project/libdragon/include/. $N64_INST/mips64-elf/include/
  # Build in a copy so the source tree stays clean:
  cp -r /project/libdragon /tmp/ld && cd /tmp/ld/tools && find . -name "*.o" -delete
  N64_INST=$N64_INST make -j"$(nproc)" \
    mksprite mkfont mkdfs dumpdfs audioconv64 n64sym n64tool n64elfcompress ed64romconfig
  mkdir -p /project/.hosttools/bin
  cp mksprite/mksprite mkfont/mkfont mkdfs/mkdfs dumpdfs/dumpdfs audioconv64/audioconv64 \
     n64sym/n64sym n64tool n64elfcompress/n64elfcompress ed64romconfig /project/.hosttools/bin/
'
echo "[build-tools] done -> .hosttools/bin/"
