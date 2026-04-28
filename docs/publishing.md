# Publishing Packages

Generated wrappers are normal package-manager projects. Publish them after regenerating `msgpack/`, committing the generated snapshot, and tagging the release. The wrappers intentionally do not depend on language MessagePack libraries; they call the generated project C bridge, so the native/WASM runtime must be built and published or documented with each release.

## Release checklist

1. Update `PACKAGE_VERSION` in `cli/main.c` for the client dump being published, then regenerate wrappers: `msgpack-pjsekai-gen /path/to/dump.cs msgpack`.
2. Build `msgpack_pjsekai_shared` and, for Java, the JNI library against it.
3. Run validation: Python compile/roundtrip with `MSGPACK_PJSEKAI_LIB`, `node --check` plus WASM smoke tests when available, `go build ./...` with `CGO_LDFLAGS=-L/path/to/lib`, Java Maven/Javac/JNI, and generated C syntax/build checks.
4. Commit the regenerated files.
5. Ensure package versions match in `package.json`, `pyproject.toml`, `go.mod` tag, and `pom.xml`.
6. Confirm package metadata such as license, repository URL, developers, and SCM coordinates before publishing.
7. Create the Go module tag `msgpack/wrappers/go/vX.Y.Z` and push it.
8. Run the GitHub Actions workflow `Publish Packages` with version `X.Y.Z`.

## Native C bridge

Build the C bridge shared library from the generated snapshot:

```sh
cmake -S . -B build-generated \
  -DMSGPACK_PJSEKAI_BUILD_GENERATED=ON \
  -DMSGPACK_PJSEKAI_GENERATED_DIR=$PWD/msgpack
cmake --build build-generated --target msgpack_pjsekai_shared
```

Use the resulting `libmsgpack_pjsekai.so`, `.dylib`, or `.dll` for Python, Go, and the Java JNI library. JavaScript needs an Emscripten build of the same generated C bridge exported as WASM.

## JavaScript / npm

One-time registry setup:

- Create the package on npm or reserve the package name.
- Configure npm trusted publishing for this repository and `.github/workflows/publish-packages.yml`.
- The workflow uses GitHub OIDC plus `npm publish --provenance`; no long-lived `NPM_TOKEN` is required.

Manual publish from the generated package, if needed:

```sh
cd msgpack/wrappers/js
npm install
npm run check
npm publish --access public --provenance
```

Users install it with:

```sh
npm install msgpack-pjsekai
```

They must also initialize the package with a WASM module built from the generated C bridge:

```js
import createModule from './path/to/msgpack-pjsekai-wasm.js';
import { useMsgpackPjsekaiWasm } from 'msgpack-pjsekai';

useMsgpackPjsekaiWasm(await createModule());
```

## Python / PyPI

One-time registry setup:

- Create the PyPI project or publish the first version from an authorized account.
- Add a PyPI trusted publisher for this repository, workflow `publish-packages.yml`, and environment `pypi`.
- The workflow uses `pypa/gh-action-pypi-publish` with GitHub OIDC; no long-lived PyPI API token is required.

Manual build/upload, if needed:

```sh
cd msgpack/wrappers/python
python3 -m pip install --upgrade build twine
python3 -m build
python3 -m twine upload dist/*
```

Users install it with:

```sh
python3 -m pip install msgpack-pjsekai
export MSGPACK_PJSEKAI_LIB=/path/to/libmsgpack_pjsekai.so
```

## Go modules

Go modules are published by reachable Git tags, not by uploading to a registry. Because the module lives under `msgpack/wrappers/go`, tag releases with the subdirectory prefix:

```sh
git tag msgpack/wrappers/go/v6.4.1
git push origin msgpack/wrappers/go/v6.4.1
```

Users install it with:

```sh
go get github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go@v6.4.1
export CGO_LDFLAGS="-L/path/to/lib"
```

## Java / Maven Central

One-time registry setup:

- Verify the Maven Central namespace for `io.github.sekai-world` in Central Portal.
- Create Central Portal user tokens and store them as GitHub environment secrets on `maven-central`: `CENTRAL_USERNAME`, `CENTRAL_PASSWORD`.
- Create a GPG signing key and store `GPG_PRIVATE_KEY` plus `GPG_PASSPHRASE` in the same environment.
- Confirm the generated POM metadata (`license`, `developers`, `scm`) matches the final GitHub repository before publishing.

Manual publish, if needed:

```sh
cd msgpack/wrappers/java
mvn -Prelease -DskipTests deploy
```

Users install it with Maven:

```xml
<dependency>
  <groupId>io.github.sekai-world</groupId>
  <artifactId>msgpack-pjsekai-java</artifactId>
  <version>6.4.1</version>
</dependency>
```

Or Gradle:

```gradle
implementation("io.github.sekai-world:msgpack-pjsekai-java:6.4.1")
```

They must also provide `libmsgpack_pjsekai_jni` and `libmsgpack_pjsekai` on `java.library.path` or the platform runtime library path.

## References

- npm trusted publishing: https://docs.npmjs.com/trusted-publishers
- PyPI trusted publishing: https://docs.pypi.org/trusted-publishers/
- Go module version tags: https://go.dev/ref/mod#vcs-version
- Maven Central Portal publishing: https://central.sonatype.org/publish/publish-portal-maven/
