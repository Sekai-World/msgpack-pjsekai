# JavaScript object wrapper

This directory is an npm package named `msgpack-pjsekai`. It does not depend on a JavaScript MessagePack package; encode/decode calls the bundled generated WASM bridge.

The npm package includes `msgpack-pjsekai-wasm.js` and `msgpack-pjsekai-wasm.wasm`, built from the generated C sources with Emscripten before packing or publishing.

Install from npm after a release is published:

```sh
npm install msgpack-pjsekai
```

```sh
cd wrappers/js
npm install
npm run build:wasm
npm run check
npm pack --dry-run
```

Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `npm publish --access public --provenance` after npm trusted publishing is configured.

`msgpack-pjsekai.d.ts` contains one generated class per MessagePack struct, so editors can show the available fields.

```js
import createModule from 'msgpack-pjsekai/wasm';
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
