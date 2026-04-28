# JavaScript object wrapper

This directory is an npm package named `msgpack-pjsekai`. It does not depend on a JavaScript MessagePack package; encode/decode calls the generated C bridge through a WASM module.

The WASM module must be built from the generated C sources and must export `mpj_buffer_*`, `mpj_value_*`, `_malloc`, and `_free`; this wrapper only talks to that project C ABI.

Install from npm after a release is published:

```sh
npm install msgpack-pjsekai
```

```sh
cd wrappers/js
npm install
npm run check
npm pack --dry-run
```

Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `npm publish --access public --provenance` after npm trusted publishing is configured.

`msgpack-pjsekai.d.ts` contains one generated class per MessagePack struct, so editors can show the available fields.

```js
import createModule from './path/to/msgpack-pjsekai-wasm.js';
import { useMsgpackPjsekaiWasm, Sekai_AssetBundleElement } from 'msgpack-pjsekai';

useMsgpackPjsekaiWasm(await createModule());

const value = new Sekai_AssetBundleElement({
  bundleName: 'example',
  crc: 1234,
});

const bytes = value.encode(); // Uint8Array
const out = new Sekai_AssetBundleElement().decode(bytes);
console.log(out.bundleName);
```
