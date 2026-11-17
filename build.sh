#!/usr/bin/env bash
set -eo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE"
case "$HERE" in */.github/workflows) ROOT="$(cd "$HERE/../.." && pwd)";; esac

echo "== HERE: $HERE"
echo "== ROOT: $ROOT"

WORK="$HERE/_build"
rm -rf "$WORK"; mkdir -p "$WORK/lib/arm64-v8a"
cd "$WORK"

pick() {
  if   [ -f "$HERE/$1" ]; then cp "$HERE/$1" .
  elif [ -f "$ROOT/$1" ]; then cp "$ROOT/$1" .
  else echo "FALTA: $1"; echo "--- ROOT ---"; ls -la "$ROOT"; echo "--- HERE ---"; ls -la "$HERE"; exit 1; fi
  echo "ok $1"
}
pick main.cpp
pick prepare.py
pick compat.h
pick android_main.cpp
pick AndroidManifest.xml

python3 prepare.py
[ -f tracker.cpp ] || { echo "prepare.py no genero tracker.cpp"; exit 1; }

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/usr/local/lib/android/sdk}}"
NDK="${ANDROID_NDK_LATEST_HOME:-}"
[ -n "$NDK" ] || NDK="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)"
[ -n "$NDK" ] || NDK="$SDK/ndk-bundle"
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
JAR="$(ls -d "$SDK"/platforms/* | sort -V | tail -1)/android.jar"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
GLUE="$NDK/sources/android/native_app_glue"
T=aarch64-linux-android26

echo "== NDK: $NDK"
echo "== BT : $BT"
echo "== JAR: $JAR"
[ -x "$TC/clang++" ] || { echo "no hay clang++ en $TC"; ls "$NDK/toolchains/llvm/prebuilt"; exit 1; }

"$TC/clang" --target=$T -c -O2 -fPIC -I"$GLUE" "$GLUE/android_native_app_glue.c" -o glue.o
"$TC/clang++" --target=$T -std=c++17 -O2 -fPIC -shared -fno-strict-aliasing \
  -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable \
  -I"$GLUE" android_main.cpp glue.o -o lib/arm64-v8a/libnitro.so \
  -lEGL -lGLESv2 -laaudio -landroid -llog -static-libstdc++
echo "== libnitro.so OK"

"$BT/aapt2" link -o base.apk -I "$JAR" --manifest AndroidManifest.xml \
  --min-sdk-version 26 --target-sdk-version 34

python3 -c "import zipfile; z=zipfile.ZipFile('base.apk','a',zipfile.ZIP_DEFLATED); z.write('lib/arm64-v8a/libnitro.so','lib/arm64-v8a/libnitro.so'); z.close()"

"$BT/zipalign" -f 4 base.apk aligned.apk

keytool -genkeypair -keystore k.jks -alias key -storepass android \
  -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=dev"
java -jar "$BT/lib/apksigner.jar" sign --ks k.jks --ks-pass pass:android \
  --out nitro.apk aligned.apk

cp nitro.apk "$HERE/nitro.apk"
cp nitro.apk "$ROOT/nitro.apk"
echo "== LISTO: $ROOT/nitro.apk"
ls -la nitro.apk
