# Go object wrapper

This directory is a Go module. It has no Go MessagePack dependency; encode/decode calls the generated C bridge through cgo.

Build or install `libmsgpack_pjsekai` from this repository first, then point cgo at its directory with `CGO_LDFLAGS=-L/path/to/lib` and set your platform runtime library path (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, or `PATH`).

Install after a release tag is pushed:

```sh
go get github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go@v6.5.0
```

```sh
cd wrappers/go
go build ./...
```

Publish by pushing the subdirectory module tag `msgpack/wrappers/go/v6.5.0`; the repository `Publish Packages` workflow verifies this tag.

Use it from another module with a local replace while developing from this generated tree:

```sh
go mod edit -replace github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go=/absolute/path/to/wrappers/go
go get github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go
```

```go
package main

import mpj "github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go"

func main() {
    value := mpj.Sekai_AssetBundleElement{BundleName: "example", Crc: 1234}
    data, _ := value.Encode()
    var out mpj.Sekai_AssetBundleElement
    _ = out.Decode(data)
    _ = out.BundleName
}
```
