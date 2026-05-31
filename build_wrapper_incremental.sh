#!/bin/bash
############################################################################
#
# build_wrapper_incremental.sh
#
# Fast path: recompile ONLY the Flutter wrapper (flutter_symengine_wrapper.c
# + flutter_symengine_cas.cpp) against the already-built per-arch
# libsymengine.a, repack the combined static archive, and regenerate
# SymEngineFlutterWrapper.xcframework. Skips the GMP/MPFR/MPC/FLINT/SymEngine
# compile (build_symengine.sh does that). Requires a prior full build so the
# per-arch build-symengine/<platform>-<arch>/.../libsymengine.a exist.
#
############################################################################
set -e

SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-symengine"
readonly LIBDIR="$BUILDDIR/lib"
readonly COMBINED_LIB_BASENAME="symengine_flutter_wrapper"
readonly HEADERDIR_FINAL="$BUILDDIR/include"
readonly VERSION_SYMENGINE="0.11.2"
readonly SYMENGINE_SOURCE="$SCRIPTDIR/symengine-$VERSION_SYMENGINE"
readonly FLUTTER_WRAPPER_SRC="$SCRIPTDIR/src"
readonly GMP_BUILDDIR="$SCRIPTDIR/build-gmp"
readonly MPFR_BUILDDIR="$SCRIPTDIR/build-mpfr"
readonly MPC_BUILDDIR="$SCRIPTDIR/build-mpc"
readonly FLINT_BUILDDIR="$SCRIPTDIR/build-flint"
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"
readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

logMsg() { printf "[WRAPPER INC] %s\n" "$1"; }
errorExit() { logMsg "❌ ERROR: $1"; exit 1; }

compileWrapper() {
    local platform=$1 arch=$2
    local build_dir="$BUILDDIR/$platform-$arch"
    [ -d "$build_dir" ] || errorExit "missing $build_dir (run build_symengine.sh first)"

    local sdkpath cc cxx minflag
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    cc=$(xcrun --sdk "$platform" -f clang)
    cxx=$(xcrun --sdk "$platform" -f clang++)
    case "$platform" in
        iphoneos) minflag="-miphoneos-version-min=$IOS_MIN_VERSION" ;;
        iphonesimulator) minflag="-mios-simulator-version-min=$IOS_MIN_VERSION" ;;
        *) minflag="-mmacosx-version-min=$MACOS_MIN_VERSION" ;;
    esac

    local cflags="-arch $arch -isysroot $sdkpath $minflag -O2 \
-I$SYMENGINE_SOURCE -I$build_dir \
-I$GMP_BUILDDIR/include -I$MPFR_BUILDDIR/include \
-I$MPC_BUILDDIR/include -I$FLINT_BUILDDIR/include"

    logMsg "Compiling wrapper for $platform-$arch"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h" "$build_dir/"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.c" "$build_dir/"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_cas.cpp" "$build_dir/"
    cd "$build_dir"

    # shellcheck disable=SC2086
    "$cc" $cflags -c flutter_symengine_wrapper.c -o flutter_symengine_wrapper.o
    # shellcheck disable=SC2086
    "$cxx" $cflags -std=c++14 -c flutter_symengine_cas.cpp -o flutter_symengine_cas.o

    local symengine_lib
    symengine_lib=$(find . -name "libsymengine.a" -type f | head -1)
    [ -n "$symengine_lib" ] || errorExit "no libsymengine.a in $build_dir"

    rm -rf temp_extract; mkdir -p temp_extract; cd temp_extract
    ar x "../$symengine_lib"
    cp "../flutter_symengine_wrapper.o" "../flutter_symengine_cas.o" .
    ar rcs "../lib${COMBINED_LIB_BASENAME}.a" ./*.o
    cd ..; rm -rf temp_extract

    mkdir -p "$LIBDIR"
    cp "lib${COMBINED_LIB_BASENAME}.a" \
        "$LIBDIR/lib${COMBINED_LIB_BASENAME}-$platform-$arch.a"
    cd "$SCRIPTDIR"
}

makeXCFramework() {
    local framework_dir="$SCRIPTDIR/SymEngineFlutterWrapper.xcframework"
    local consistent="lib${COMBINED_LIB_BASENAME}.a"
    rm -rf "$framework_dir"

    local device_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphoneos-arm64.a"
    local sim_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-universal.a"
    local mac_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-universal.a"
    lipo -create -output "$sim_lib" \
        "$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-x86_64.a" \
        "$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-arm64.a"
    lipo -create -output "$mac_lib" \
        "$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-x86_64.a" \
        "$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-arm64.a"

    rm -rf "$HEADERDIR_FINAL"; mkdir -p "$HEADERDIR_FINAL"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h" "$HEADERDIR_FINAL/"

    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR_FINAL" \
        -library "$sim_lib" -headers "$HEADERDIR_FINAL" \
        -library "$mac_lib" -headers "$HEADERDIR_FINAL" \
        -output "$framework_dir"

    mv "$framework_dir/ios-arm64/$(basename "$device_lib")" \
        "$framework_dir/ios-arm64/$consistent"
    mv "$framework_dir/ios-arm64_x86_64-simulator/$(basename "$sim_lib")" \
        "$framework_dir/ios-arm64_x86_64-simulator/$consistent"
    mv "$framework_dir/macos-arm64_x86_64/$(basename "$mac_lib")" \
        "$framework_dir/macos-arm64_x86_64/$consistent"

    local plist="$framework_dir/Info.plist" count
    count=$(/usr/libexec/PlistBuddy -c "Print :AvailableLibraries:" "$plist" | grep -c "Dict")
    for (( i=0; i<count; i++ )); do
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:BinaryPath $consistent" "$plist"
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:LibraryPath $consistent" "$plist"
    done
    logMsg "✅ Rebuilt $framework_dir"
}

for a in $DEVARCHS; do compileWrapper "iphoneos" "$a"; done
for a in $SIMARCHS; do compileWrapper "iphonesimulator" "$a"; done
for a in $MACARCHS; do compileWrapper "macosx" "$a"; done
makeXCFramework
logMsg "Done."
