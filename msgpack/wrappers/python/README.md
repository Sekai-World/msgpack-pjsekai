# Python object wrapper

This directory is a pip-installable package named `msgpack-pjsekai`. It has no Python MessagePack dependency; encode/decode calls the generated C bridge through `ctypes`.

Build or install `libmsgpack_pjsekai` from this repository first, then set `MSGPACK_PJSEKAI_LIB` or call `load_c_library(path)` before encoding or decoding.

Install from PyPI after a release is published:

```sh
python3 -m pip install msgpack-pjsekai
```

```sh
cd wrappers/python
python3 -m pip install .
```

Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `python3 -m build` and `python3 -m twine upload dist/*` after PyPI trusted publishing or an API token is configured.

`msgpack_pjsekai/__init__.pyi` and `py.typed` contain one generated class per MessagePack struct for editor field hints.

```python
from msgpack_pjsekai import load_c_library, Sekai_AssetBundleElement

load_c_library('/path/to/libmsgpack_pjsekai.so')

value = Sekai_AssetBundleElement(bundleName='example', crc=1234)
data = value.encode()  # bytes
out = Sekai_AssetBundleElement().decode(data)
print(out.bundleName)
```
