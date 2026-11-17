set -e
python3 prepare.py
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}
BT=$(ls -d $SDK/build-tools/* | tail -1)
NDK=${ANDROID_NDK_LATEST_HOME:-$(ls -d $SDK/ndk/* | tail -1)}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
JAR=$(ls -d $SDK/platforms/* | tail -1)/android.jar
GLUE=$NDK/sources/android/native_app_glue
T=aarch64-linux-android26
mkdir -p out/lib/arm64-v8a

$TC/clang --target=$T -c -O2 -fPIC -I$GLUE $GLUE/android_native_app_glue.c -o out/glue.o
$TC/clang++ --target=$T -std=c++17 -O2 -fPIC -shared -fno-strict-aliasing -I$GLUE \
  android_main.cpp out/glue.o -o out/lib/arm64-v8a/libnitro.so \
  -lEGL -lGLESv2 -laaudio -landroid -llog -static-libstdc++

$BT/aapt2 link -o out/base.apk -I $JAR --manifest AndroidManifest.xml \
  --min-sdk-version 26 --target-sdk-version 34
(cd out && zip -qr base.apk lib)
$BT/zipalign -f 4 out/base.apk out/aligned.apk
keytool -genkeypair -keystore debug.keystore -alias key -storepass android \
  -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=dev"
java -jar $BT/lib/apksigner.jar sign --ks debug.keystore --ks-pass pass:android \
  --out nitro.apk out/aligned.apk
echo LISTO