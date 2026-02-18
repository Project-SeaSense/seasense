#!/usr/bin/env bash
set -euo pipefail

# SeaSense Logger build helper for arduino-cli
# Usage:
#   ./scripts/build.sh s3
#   ./scripts/build.sh s3-octal
#
# Optional env vars:
#   ENABLE_N2K=1   -> enables FEATURE_NMEA2000 at compile time

TARGET="${1:-s3}"
ENABLE_N2K="${ENABLE_N2K:-0}"
SKETCH_DIR="$(cd "$(dirname "$0")/.." && pwd)"

case "$TARGET" in
  s3)
    # huge_app avoids text overflow on 4MB default layout
    FQBN='esp32:esp32:esp32s3:PartitionScheme=huge_app,FlashSize=4M'
    ;;
  s3-octal)
    FQBN='esp32:esp32:esp32s3-octal'
    ;;
  *)
    echo "Unknown target: $TARGET (use: s3 | s3-octal)" >&2
    exit 2
    ;;
esac

BUILD_FLAGS=()
if [[ "$ENABLE_N2K" == "1" ]]; then
  BUILD_FLAGS+=(--build-property build.extra_flags='-DFEATURE_NMEA2000=1')
  echo "NMEA2000: ENABLED"
else
  echo "NMEA2000: disabled"
fi

echo "FQBN: $FQBN"
arduino-cli compile --fqbn "$FQBN" ${BUILD_FLAGS[@]+"${BUILD_FLAGS[@]}"} "$SKETCH_DIR"
