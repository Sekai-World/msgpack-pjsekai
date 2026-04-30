# msgpack-pjsekai

`msgpack-pjsekai` is a C generator that reads an IL2CPP `dump.cs`, finds MessagePack-CSharp models annotated with `[MessagePackObject]` and `[Key(...)]`, and emits split msgpack-c serializers plus static, protobuf-like language packages.

Generated schemas are meant to be committed when you want to publish a concrete schema snapshot. The private input `dump.cs` stays ignored by default; generated output is not ignored.

## Repository layout

```text
cli/                 generator source (`msgpack-pjsekai-gen`)
msgpack/             generated C schema snapshot and wrapper packages
msgpack/deps/        msgpack-c submodule
docs/                generated-output, serialization, and publishing notes
```

## Build the generator

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target msgpack-pjsekai-gen
```

Without CMake, the generator itself has no external dependencies:

```sh
cc -std=c11 -Wall -Wextra -pedantic -o msgpack-pjsekai-gen cli/main.c
```

## Generate code

```sh
./build/cli/msgpack-pjsekai-gen /path/to/dump.cs msgpack
```

Output shape:

```text
msgpack/msgpack-pjsekai.h
msgpack/msgpack-pjsekai.files
msgpack/generated/*.h
msgpack/generated/*.c
msgpack/wrappers/js/*
msgpack/wrappers/python/*
msgpack/wrappers/go/*
msgpack/wrappers/java/*
```

Generated source files start with `DO NOT EDIT!` and the generator compiler version. Package metadata and docs such as `go.mod`, `package.json`, `pyproject.toml`, `pom.xml`, and wrapper READMEs are generated without that source-file preamble.

Wrapper package-manager versions are generated from `PACKAGE_VERSION` in `cli/main.c`; update that single constant when regenerating packages for a newer client dump.

## Static wrapper API

The non-C wrappers are static, protobuf-like packages with normal language objects and `encode`/`decode` methods. They do not use language-level MessagePack libraries. Serialization goes through the generated project C bridge (`mpj_value_*`, `mpj_buffer_*`) via WASM, `ctypes`, cgo, or JNI, while the public wrapper API never exposes native pointers.

Field hints are generated statically:

- TypeScript: `msgpack/wrappers/js/msgpack-pjsekai.d.ts` has one class and init interface per struct.
- Python: `msgpack/wrappers/python/msgpack_pjsekai/__init__.pyi` and `py.typed` expose dataclass fields.
- Go: `msgpack/wrappers/go/msgpack_pjsekai.go` exposes concrete structs with exported fields.
- Java: `MsgpackPjsekai.<TypeName>` nested classes expose public fields.

## Native runtime

Wrappers need the generated C bridge runtime:

- JavaScript loads the bundled `msgpack-pjsekai/wasm` module published with the npm package.
- Python loads `libmsgpack_pjsekai` with `MSGPACK_PJSEKAI_LIB=/path/to/libmsgpack_pjsekai.so` or `load_c_library(path)`.
- Go links with cgo, for example `CGO_LDFLAGS="-L/path/to/lib"` plus the platform runtime library path.
- Java loads `libmsgpack_pjsekai_jni`, which links against `libmsgpack_pjsekai`; put both on `java.library.path` or the platform runtime library path.

## Installation

After a release is published, install the generated wrappers directly from their package managers.

### JavaScript / npm

```sh
npm install msgpack-pjsekai
```

```js
import createModule from 'msgpack-pjsekai/wasm';
import { useMsgpackPjsekaiWasm, Sekai_AssetBundleElement } from 'msgpack-pjsekai';

useMsgpackPjsekaiWasm(await createModule());

const value = new Sekai_AssetBundleElement({ bundleName: 'example', crc: 1234 });
const bytes = value.encode(); // Uint8Array
const out = new Sekai_AssetBundleElement().decode(bytes);
```

For local development from this repository:

```sh
cd msgpack/wrappers/js
npm install
npm run build:wasm
npm run check
npm pack --dry-run
```

### Python / pip

```sh
python3 -m pip install msgpack-pjsekai
```

```python
from msgpack_pjsekai import load_c_library, Sekai_AssetBundleElement

load_c_library('/path/to/libmsgpack_pjsekai.so')

value = Sekai_AssetBundleElement(bundleName='example', crc=1234)
data = value.encode()  # bytes
out = Sekai_AssetBundleElement().decode(data)
```

For local development from this repository:

```sh
cd msgpack/wrappers/python
python3 -m pip install .
```

### Go modules

```sh
go get github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go@v6.5.0
```

```go
value := msgpackpjsekai.Sekai_AssetBundleElement{BundleName: "example", Crc: 1234}
data, _ := value.Encode()
var out msgpackpjsekai.Sekai_AssetBundleElement
_ = out.Decode(data)
```

For local development from this repository:

```sh
export CGO_LDFLAGS="-L/path/to/lib"
go mod edit -replace github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go=$PWD/msgpack/wrappers/go
go get github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go
```

### Java / Maven

Maven:

```xml
<dependency>
  <groupId>io.github.sekai-world</groupId>
  <artifactId>msgpack-pjsekai-java</artifactId>
  <version>6.5.0</version>
</dependency>
```

Gradle:

```gradle
implementation("io.github.sekai-world:msgpack-pjsekai-java:6.5.0")
```

```java
import io.github.sekaiworld.msgpackpjsekai.MsgpackPjsekai;

MsgpackPjsekai.Sekai_AssetBundleElement value = new MsgpackPjsekai.Sekai_AssetBundleElement();
value.bundleName = "example";
value.crc = 1234L;
byte[] data = value.encode();
MsgpackPjsekai.Sekai_AssetBundleElement out = new MsgpackPjsekai.Sekai_AssetBundleElement().decode(data);
```

For local development from this repository:

```sh
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
cd msgpack/wrappers/java
mvn package
```

## Publishing

The repository includes `.github/workflows/publish-packages.yml` for npm, PyPI, Maven Central, and Go tag verification. See `docs/publishing.md` for registry setup, required secrets/environments, and manual fallback commands.

## Build generated C output

```sh
cmake -S . -B build-generated \
  -DMSGPACK_PJSEKAI_BUILD_GENERATED=ON \
  -DMSGPACK_PJSEKAI_GENERATED_DIR=$PWD/msgpack
cmake --build build-generated --target msgpack_pjsekai
cmake --build build-generated --target msgpack_pjsekai_shared
```

C structs still use `has_<field>` flags so C callers can distinguish absent fields from default values. See `docs/serialization.md` for C and wrapper examples.

## Notes

- No sample `dump.cs` or example schema is generated; run the generator against your own dump.
- Complex fields such as arrays, lists, dictionaries, and unknown C# types are passed through as generic MessagePack-capable values in wrappers.
- String fields unpacked by the C API are owned `char *` values; call the generated `Type_free` when done.
