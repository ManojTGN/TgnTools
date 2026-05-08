#!/usr/bin/env bash
set -euo pipefail

: "${TOOL_NAME:=quickrun}"
: "${OS_LABEL:=linux}"
: "${ARCH:=x64}"
: "${VERSION:=dev}"

CC="${CC:-cc}"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

OUT_DIR="build"
mkdir -p "$OUT_DIR"
find "$OUT_DIR" -maxdepth 1 -type f -delete 2>/dev/null || true

BIN="$TOOL_NAME"
case "$OS_LABEL" in
    windows) BIN="${BIN}.exe" ;;
esac

# libuiohook lives under vendor/uiohook (git submodule). Fail loudly if the
# submodule has not been initialized.
UIOHOOK_DIR="vendor/uiohook"
if [ ! -d "$UIOHOOK_DIR/src" ]; then
    echo "error: $UIOHOOK_DIR is missing or empty" >&2
    echo "       run: git submodule update --init --recursive" >&2
    exit 1
fi

PLATFORM_LIBS=""
PLATFORM_INC=""
PLATFORM_SRC=""
case "$OS_LABEL" in
    windows)
        PLATFORM_LIBS="-mwindows -luser32 -ladvapi32 -lshell32"
        PLATFORM_INC="-I$UIOHOOK_DIR/include -I$UIOHOOK_DIR/src -I$UIOHOOK_DIR/src/windows"
        PLATFORM_SRC="$UIOHOOK_DIR/src/logger.c $UIOHOOK_DIR/src/windows/input_helper.c $UIOHOOK_DIR/src/windows/input_hook.c $UIOHOOK_DIR/src/windows/post_event.c $UIOHOOK_DIR/src/windows/system_properties.c"
        ;;
    macos)
        PLATFORM_LIBS="-framework Carbon -framework ApplicationServices"
        PLATFORM_INC="-I$UIOHOOK_DIR/include -I$UIOHOOK_DIR/src -I$UIOHOOK_DIR/src/darwin"
        PLATFORM_SRC="$UIOHOOK_DIR/src/logger.c $UIOHOOK_DIR/src/darwin/input_helper.c $UIOHOOK_DIR/src/darwin/input_hook.c $UIOHOOK_DIR/src/darwin/post_event.c $UIOHOOK_DIR/src/darwin/system_properties.c"
        ;;
    linux|*)
        PLATFORM_LIBS="-lX11 -lXtst -lXinerama -lXt -lpthread"
        PLATFORM_INC="-I$UIOHOOK_DIR/include -I$UIOHOOK_DIR/src -I$UIOHOOK_DIR/src/x11"
        PLATFORM_SRC="$UIOHOOK_DIR/src/logger.c $UIOHOOK_DIR/src/x11/input_helper.c $UIOHOOK_DIR/src/x11/input_hook.c $UIOHOOK_DIR/src/x11/post_event.c $UIOHOOK_DIR/src/x11/system_properties.c"
        ;;
esac

cat > "$OUT_DIR/_build_info.h" <<EOF
#define QUICKRUN_VERSION "$VERSION"
#define QUICKRUN_COMMIT "$COMMIT"
EOF

# Windows-only: embed icon.ico as the app/tray icon resource via windres.
ICON_OBJ=""
case "$OS_LABEL" in
    windows)
        if [ -f "icon.ico" ]; then
            echo "Embedding icon from icon.ico"
            cp icon.ico "$OUT_DIR/_app_icon.ico"
            cat > "$OUT_DIR/_app_icon.rc" <<EOF
1 ICON "_app_icon.ico"
EOF
            (cd "$OUT_DIR" && windres _app_icon.rc -O coff -o _app_icon.res)
            ICON_OBJ="$OUT_DIR/_app_icon.res"
        else
            echo "warning: icon.ico not found - building without app icon"
        fi
        ;;
esac

echo "Building $TOOL_NAME ($OS_LABEL/$ARCH) with $CC, version=$VERSION commit=$COMMIT"

"$CC" \
    -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
    -std=gnu99 \
    $PLATFORM_INC \
    -include "$OUT_DIR/_build_info.h" \
    -o "$OUT_DIR/$BIN" \
    src/main.c src/config.c src/json.c src/keyspec.c src/action.c src/log.c src/tray.c \
    $PLATFORM_SRC \
    $ICON_OBJ \
    $PLATFORM_LIBS

rm -f "$OUT_DIR/_build_info.h" "$OUT_DIR/_app_icon.ico" "$OUT_DIR/_app_icon.rc" "$OUT_DIR/_app_icon.res"

cp README.md           "$OUT_DIR/" 2>/dev/null || true
cp config.example.json "$OUT_DIR/" 2>/dev/null || true
cp config.json         "$OUT_DIR/" 2>/dev/null || true

echo "Done."
ls -la "$OUT_DIR"
