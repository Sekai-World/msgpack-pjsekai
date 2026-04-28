# Java object wrapper

This directory is a Maven package `io.github.sekai-world:msgpack-pjsekai-java`. It has no Java MessagePack dependency; encode/decode calls the generated C bridge through JNI.

Build or install `libmsgpack_pjsekai` plus the generated JNI library `libmsgpack_pjsekai_jni`, then put both on `java.library.path` or the platform runtime library path before calling `encode`/`decode`.

Install from Maven Central after a release is published:

```xml
<dependency>
  <groupId>io.github.sekai-world</groupId>
  <artifactId>msgpack-pjsekai-java</artifactId>
  <version>6.4.1</version>
</dependency>
```

```sh
cd wrappers/java
mvn package
```

Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `mvn -Prelease -DskipTests deploy` after Maven Central credentials and GPG signing are configured.

The generated classes are nested under `io.github.sekaiworld.msgpackpjsekai.MsgpackPjsekai`.

```java
import io.github.sekaiworld.msgpackpjsekai.MsgpackPjsekai;

MsgpackPjsekai.Sekai_AssetBundleElement value = new MsgpackPjsekai.Sekai_AssetBundleElement();
value.bundleName = "example";
value.crc = 1234L;

byte[] data = value.encode();
MsgpackPjsekai.Sekai_AssetBundleElement out = new MsgpackPjsekai.Sekai_AssetBundleElement().decode(data);
System.out.println(out.bundleName);
```
