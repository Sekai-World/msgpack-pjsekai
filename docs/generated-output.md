# Generated Output

The generator writes a proto-style tree instead of one monolithic source file.

```text
<out>/msgpack-pjsekai.h               umbrella header
<out>/msgpack-pjsekai.files           source list for build systems
<out>/generated/msgpack-pjsekai-common.[ch]
<out>/generated/msgpack-pjsekai-bridge.[ch]
<out>/generated/<TypeName>.[ch]
<out>/wrappers/js/*                   npm package, WASM-backed
<out>/wrappers/python/*               pip package, ctypes-backed
<out>/wrappers/go/*                   Go module, cgo-backed
<out>/wrappers/java/*                 Maven package, JNI-backed
```

Package-manager entrypoints are generated with the wrappers:

```text
<out>/wrappers/js/package.json
<out>/wrappers/js/msgpack-pjsekai.js
<out>/wrappers/js/msgpack-pjsekai.d.ts
<out>/wrappers/python/pyproject.toml
<out>/wrappers/python/msgpack_pjsekai/__init__.py
<out>/wrappers/python/msgpack_pjsekai/__init__.pyi
<out>/wrappers/python/msgpack_pjsekai/py.typed
<out>/wrappers/go/go.mod
<out>/wrappers/go/msgpack_pjsekai.go
<out>/wrappers/go/msgpack-pjsekai-bridge.h
<out>/wrappers/java/pom.xml
<out>/wrappers/java/src/main/java/io/github/sekaiworld/msgpackpjsekai/MsgpackPjsekai.java
<out>/wrappers/java/src/main/native/msgpack_pjsekai_jni.c
<out>/wrappers/java/src/main/native/msgpack-pjsekai-bridge.h
```

## C API per generated type

```c
int Type_pack(msgpack_packer *pk, const Type *value);
int Type_unpack(const msgpack_object *obj, Type *out);
void Type_free(Type *value);
```

The generated C bridge remains available for C applications that want type-name lookup or byte-buffer helpers:

```c
size_t mpj_type_count(void);
const char *mpj_type_name(size_t type_id);
int mpj_type_id(const char *type_name);
size_t mpj_field_count(int type_id);
const char *mpj_field_name(int type_id, size_t field_id);
int mpj_field_key_kind(int type_id, size_t field_id);
const char *mpj_field_key_string(int type_id, size_t field_id);
int64_t mpj_field_key_int(int type_id, size_t field_id);
mpj_buffer *mpj_pack_bytes(int type_id, const void *value);
int mpj_unpack_bytes(int type_id, const uint8_t *data, size_t size, void *out);
```

## Static wrapper API

Wrappers are generated directly by `msgpack-pjsekai-gen`; no separate wrapper script is needed. They serialize language-native objects by calling the generated project C bridge and return the natural byte type for each ecosystem:

- JavaScript: classes return `Uint8Array` via a WASM build of the C bridge.
- Python: dataclasses return `bytes` via `ctypes` and `libmsgpack_pjsekai`.
- Go: structs return `[]byte` via cgo and `libmsgpack_pjsekai`.
- Java: nested classes return `byte[]` via JNI and `libmsgpack_pjsekai`.

## Code hints

- C structs are visible in `<out>/generated/<TypeName>.h`.
- TypeScript users get `<TypeName>`, `<TypeName>Init`, `schemas`, `types()`, and `fields()` from `msgpack-pjsekai.d.ts`.
- Python users get dataclass fields and method signatures from `msgpack_pjsekai/__init__.pyi`.
- Go and Java expose real fields directly on generated structs/classes.

## Package usage

- JavaScript: `cd <out>/wrappers/js && npm install && npm run check && npm pack --dry-run`; initialize the wrapper with a generated C-bridge WASM module.
- Python: `cd <out>/wrappers/python && python3 -m pip install .`; set `MSGPACK_PJSEKAI_LIB` or call `load_c_library(path)`.
- Go: import `github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go` and set `CGO_LDFLAGS=-L/path/to/lib` for a built `libmsgpack_pjsekai`.
- Java: `cd <out>/wrappers/java && mvn package`, then depend on `io.github.sekai-world:msgpack-pjsekai-java` and provide the JNI/native libraries at runtime.
