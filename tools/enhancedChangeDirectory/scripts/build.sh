#!/usr/bin/env bash
set -euo pipefail

: "${TOOL_NAME:=tcd}"
: "${OS_LABEL:=linux}"
: "${ARCH:=x64}"
: "${VERSION:=dev}"

CC="${CC:-cc}"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

OUT_DIR="build"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

BIN="$TOOL_NAME"
case "$OS_LABEL" in
  windows) BIN="${BIN}.exe" ;;
esac

EXTRA_LIBS=""
case "$OS_LABEL" in
  windows) EXTRA_LIBS="-lshell32 -lole32 -luuid" ;;
esac

echo "Building $TOOL_NAME ($OS_LABEL/$ARCH) with $CC, version=$VERSION commit=$COMMIT"

cat > "$OUT_DIR/_build_info.h" <<EOF
#define TCD_VERSION "$VERSION"
#define TCD_COMMIT "$COMMIT"
EOF

"$CC" \
  -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
  -std=c99 \
  -include "$OUT_DIR/_build_info.h" \
  -o "$OUT_DIR/$BIN" \
  src/term.c src/input.c src/fs.c src/json.c src/config.c src/ui.c src/main.c \
  $EXTRA_LIBS

rm -f "$OUT_DIR/_build_info.h"

cp README.md           "$OUT_DIR/" 2>/dev/null || true
cp config.example.json "$OUT_DIR/" 2>/dev/null || true
cp config.json         "$OUT_DIR/" 2>/dev/null || true

mkdir -p "$OUT_DIR/shell"
case "$OS_LABEL" in
    windows)
        cp shell/tcd.bat "$OUT_DIR/shell/" 2>/dev/null || true
        cp shell/tcd.ps1 "$OUT_DIR/shell/" 2>/dev/null || true
        ;;
    *)
        cp shell/tcd.sh  "$OUT_DIR/shell/" 2>/dev/null || true
        ;;
esac

echo "Done."
ls -la "$OUT_DIR"
