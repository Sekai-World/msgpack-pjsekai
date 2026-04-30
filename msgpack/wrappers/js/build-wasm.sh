#!/usr/bin/env sh
set -eu

if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found; install and activate Emscripten SDK before building the WASM package" >&2
  exit 127
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
OUT_JS=${1:-"$SCRIPT_DIR/msgpack-pjsekai-wasm.js"}
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

mkdir -p "$TMP_DIR/include/msgpack"
sed -e 's/@MSGPACK_ENDIAN_BIG_BYTE@/0/g' \
    -e 's/@MSGPACK_ENDIAN_LITTLE_BYTE@/1/g' \
    "$ROOT/deps/cmake/sysdep.h.in" > "$TMP_DIR/include/msgpack/sysdep.h"
cp "$TMP_DIR/include/msgpack/sysdep.h" "$TMP_DIR/include/sysdep.h"
sed -e 's/@MSGPACK_ENDIAN_BIG_BYTE@/0/g' \
    -e 's/@MSGPACK_ENDIAN_LITTLE_BYTE@/1/g' \
    "$ROOT/deps/cmake/pack_template.h.in" > "$TMP_DIR/include/msgpack/pack_template.h"
cp "$TMP_DIR/include/msgpack/pack_template.h" "$TMP_DIR/include/pack_template.h"

SOURCES=$(awk -v root="$ROOT" '
  /^[[:space:]]*($|#|\*)/ { next }
  { print root "/" $0 }
' "$ROOT/msgpack-pjsekai.files")

emcc \
  $SOURCES \
  "$SCRIPT_DIR/msgpack-pjsekai-wasm-bridge.cpp" \
  "$ROOT/deps/src/objectc.c" \
  "$ROOT/deps/src/unpack.c" \
  "$ROOT/deps/src/version.c" \
  "$ROOT/deps/src/vrefbuffer.c" \
  "$ROOT/deps/src/zone.c" \
  -I"$ROOT" \
  -I"$ROOT/generated" \
  -I"$TMP_DIR/include" \
  -I"$ROOT/deps/include" \
  -O1 \
  --bind \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web,node \
  -sALLOW_MEMORY_GROWTH=1 \
  -o "$OUT_JS"
