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

EXPORTED_FUNCTIONS='[
"_malloc",
"_free",
"_mpj_buffer_data",
"_mpj_buffer_delete",
"_mpj_buffer_size",
"_mpj_value_array_get",
"_mpj_value_array_set",
"_mpj_value_bool",
"_mpj_value_data",
"_mpj_value_free",
"_mpj_value_kind",
"_mpj_value_map_key",
"_mpj_value_map_set",
"_mpj_value_map_value",
"_mpj_value_new_array",
"_mpj_value_new_binary",
"_mpj_value_new_bool",
"_mpj_value_new_int",
"_mpj_value_new_map",
"_mpj_value_new_nil",
"_mpj_value_new_number",
"_mpj_value_new_string",
"_mpj_value_new_uint",
"_mpj_value_number",
"_mpj_value_pack_bytes",
"_mpj_value_size",
"_mpj_value_unpack_bytes"
]'

emcc \
  $SOURCES \
  "$ROOT/deps/src/objectc.c" \
  "$ROOT/deps/src/unpack.c" \
  "$ROOT/deps/src/version.c" \
  "$ROOT/deps/src/vrefbuffer.c" \
  "$ROOT/deps/src/zone.c" \
  -I"$ROOT" \
  -I"$ROOT/generated" \
  -I"$TMP_DIR/include" \
  -I"$ROOT/deps/include" \
  -O3 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web,node \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORT_ALL=1 \
  -sEXPORTED_RUNTIME_METHODS='["ccall"]' \
  -sEXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
  -o "$OUT_JS"
