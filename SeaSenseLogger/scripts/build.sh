#!/usr/bin/env bash
set -euo pipefail

# SeaSense Logger build helper for arduino-cli
# Usage:
#   ./scripts/build.sh s3
#   ./scripts/build.sh s3-octal

TARGET="${1:-s3}"
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

echo "FQBN: $FQBN"
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"
