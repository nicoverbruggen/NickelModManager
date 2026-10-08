#!/bin/sh
# Run inside a kobuild SDK image, with the repository mounted at /work.
set -eu
qt=$1
tests=$2
probes=$3
output=build/qt$qt
runtime=/usr/arm-linux-gnueabihf
case "$qt" in
    5) libraries=$runtime/lib:/tc/qt5/sysroot/usr/lib
       name=KoboRoot.tgz; prefix=usr/local/Kobo/imageformats ;;
    6) libraries=/tc/arm-kobo-linux-gnueabihf/qt6/lib:/usr/lib/arm-linux-gnueabihf:$runtime/lib
       name=Kobo.tgz; prefix=imageformats ;;
    *) echo 'Expected Qt 5 or 6' >&2; exit 2 ;;
esac
cmake -S . -B "$output" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    "-DNICKELMODMANAGER_QT=$qt" "-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE" \
    "-DBUILD_TESTING=$tests" "-DNICKELMODMANAGER_PROBES=$probes" \
    "-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-arm;-L;$runtime;-E;LD_LIBRARY_PATH=$libraries"
cmake --build "$output"
if [ "$tests" = ON ]; then
    (cd "$output" && ctest --output-on-failure)
fi
stage=$(mktemp -d "$output/package.XXXXXX")
trap 'rm -rf "$stage"' 0
trap 'exit 1' HUP INT TERM
mkdir -p "$stage/$prefix"
package() {
    library=$1
    folder=$2
    arm-linux-gnueabihf-strip --strip-unneeded "$output/$library"
    cp -p "$output/$library" "$stage/$prefix/$library"
    chmod 755 "$stage/$prefix/$library"
    mkdir -p "$output/$folder"
    tar --owner=0 --group=0 --numeric-owner --mode=0755 -czf "$output/$folder$name" \
        -C "$stage" "$prefix/$library"
    echo "$output/$folder$name"
}
package libnickelmm.so ''
if [ "$probes" = ON ]; then
    package libnmm-probe-a.so probe-a/
    package libnmm-probe-b.so probe-b/
fi
