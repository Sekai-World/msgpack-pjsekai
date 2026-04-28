# Serialization And Deserialization

Generated wrappers use a protobuf-like object flow: create a value, set fields, call `encode`, then decode into a new instance of the same type. No wrapper API exposes native pointers. Wrappers do not depend on language MessagePack packages; they call the generated project C bridge through WASM, `ctypes`, cgo, or JNI.

## C

The C output is still the source of truth for exact field presence. Set `has_<field>` before packing a field. Strings copied during unpack must be released with `Type_free`.

```c
#include "msgpack-pjsekai.h"
#include "generated/msgpack-pjsekai-bridge.h"

Sekai_AssetBundleElement value = {0};
value.has_bundleName = true;
value.bundleName = "example";
value.has_crc = true;
value.crc = 1234;

int type_id = mpj_type_id("Sekai_AssetBundleElement");
mpj_buffer *bytes = mpj_pack_bytes(type_id, &value);

Sekai_AssetBundleElement out = {0};
mpj_unpack_bytes(type_id, mpj_buffer_data(bytes), mpj_buffer_size(bytes), &out);

Sekai_AssetBundleElement_free(&out);
mpj_buffer_delete(bytes);
```

## JavaScript / TypeScript

```js
import { useMsgpackPjsekaiWasm, Sekai_AssetBundleElement, fields } from 'msgpack-pjsekai';
import createModule from './path/to/msgpack-pjsekai-wasm.js';

useMsgpackPjsekaiWasm(await createModule());
console.log(fields('Sekai_AssetBundleElement'));

const value = new Sekai_AssetBundleElement({
  bundleName: 'example',
  crc: 1234,
});

const data = value.encode();      // Uint8Array
const out = new Sekai_AssetBundleElement().decode(data);
```

`msgpack-pjsekai.d.ts` exposes each generated class and its `<TypeName>Init` object shape so editors can autocomplete fields.

## Python

```python
from msgpack_pjsekai import load_c_library, Sekai_AssetBundleElement, fields

load_c_library('/path/to/libmsgpack_pjsekai.so')
print(fields('Sekai_AssetBundleElement'))

value = Sekai_AssetBundleElement(bundleName='example', crc=1234)
data = value.encode()             # bytes
out = Sekai_AssetBundleElement().decode(data)
```

The generated package includes `py.typed` and `__init__.pyi`, so static analyzers and editors can see each dataclass field.

## Go

```go
package main

import mpj "github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go"

func main() {
    value := mpj.Sekai_AssetBundleElement{BundleName: "example", Crc: 1234}
    data, _ := value.Encode()      // []byte
    var out mpj.Sekai_AssetBundleElement
    _ = out.Decode(data)
    _ = out.BundleName
}
```

Generated Go structs use value fields, not pointer fields. Encoding writes the struct fields into a MessagePack map using the original `[Key(...)]` values.

## Java

```java
import io.github.sekaiworld.msgpackpjsekai.MsgpackPjsekai;

MsgpackPjsekai.Sekai_AssetBundleElement value = new MsgpackPjsekai.Sekai_AssetBundleElement();
value.bundleName = "example";
value.crc = 1234L;

byte[] data = value.encode();
MsgpackPjsekai.Sekai_AssetBundleElement out = new MsgpackPjsekai.Sekai_AssetBundleElement().decode(data);
```

Java fields use boxed primitives (`Long`, `Boolean`, `Double`) so `null` means the field is absent and non-null values are serialized.

## Complex fields

Primitive fields are converted to language-native scalar types. Complex C# fields such as arrays, lists, dictionaries, and unknown types are represented as generic MessagePack-capable values:

- JavaScript: `unknown` / normal JS arrays and objects.
- Python: `Any`.
- Go: `any`.
- Java: `Object`; decoded maps/lists use `LinkedHashMap` and `ArrayList`, while `Map`, `Iterable`, `byte[]`, and scalar types are accepted during encoding.
