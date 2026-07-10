#!/bin/bash
#############################################################################
#
# build_gmp.sh (Corrected for Consistent Naming)
#
# Creates a GMP.xcframework that is 100% compatible with CocoaPods out of the box.
# It correctly creates universal binaries and then patches the final
# XCFramework to ensure the internal binary is named "libgmp.a".
#
#############################################################################
set -e

# --- Configuration ---
# LINKAGE: "static" (default, Route A / AGPL combined-work) or "shared"
# (Route B / LGPL-relinkable dynamic frameworks). See LGPL-COMPLIANCE.md.
LINKAGE="${LINKAGE:-static}"
readonly LINKAGE
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-gmp"
readonly LIBDIR="$BUILDDIR/lib"
readonly HEADERDIR="$BUILDDIR/include"
readonly LIBNAME="gmp"
readonly VERSION="6.3.0"
readonly SOFTWARETAR="$SCRIPTDIR/$LIBNAME-$VERSION.tar.bz2"
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"
readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

# --- Utility Functions ---
# shellcheck disable=SC2329,SC2317
cleanup() { echo "[CLEANUP] GMP build script finished."; }
trap cleanup EXIT
logMsg() { printf "[GMP BUILD] %s\n" "$1"; }
errorExit() { logMsg "❌ ERROR: $1"; logMsg "Build failed."; exit 1; }

# --- Core Build Functions ---
extractSoftware() {
    local extractdir="$BUILDDIR/source"
    logMsg "Creating build directory and extracting source..."
    mkdir -p "$extractdir"
    if [ ! -f "$SOFTWARETAR" ]; then errorExit "Software archive not found at '$SOFTWARETAR'."; fi
    tar -xjf "$SOFTWARETAR" -C "$extractdir" --strip-components 1 || errorExit "Failed to extract tarball."
}

configureAndMake() {
    local platform=$1; local arch=$2; local extractdir="$BUILDDIR/source"
    logMsg "================================================================="
    logMsg "Configuring for PLATFORM: $platform, ARCH: $arch"
    logMsg "================================================================="
    
    unset CC CXX CFLAGS CXXFLAGS LDFLAGS LIBS SDKROOT CC_FOR_BUILD
    
    local sdkpath
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    if [ ! -d "$sdkpath" ]; then errorExit "SDK path not found for platform '$platform'."; fi

    local target_cc
    target_cc=$(xcrun --sdk "$platform" -f clang)
    local target_cflags; local target_ldflags
    
    if [[ "$platform" == "iphoneos" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION"
        target_ldflags="-arch $arch -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION"
    elif [[ "$platform" == "iphonesimulator" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION"
        target_ldflags="-arch $arch -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION"
    else # macosx
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION"
        target_ldflags="-arch $arch -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION"
    fi
    
    cd "$extractdir"
    make distclean &> /dev/null || true
    
    local host_triplet
    host_triplet=$([[ "$arch" == "arm64" ]] && echo "aarch64" || echo "$arch")-apple-darwin
    local configure_args=("--host=$host_triplet" "--disable-assembly")
    # LINKAGE (default "static") selects the static (Route A) or shared
    # (Route B, LGPL-relinkable) build. Static is byte-identical to the
    # original scripts. See LGPL-COMPLIANCE.md.
    if [[ "$LINKAGE" == "shared" ]]; then
        configure_args+=("--enable-shared" "--disable-static")
    else
        configure_args+=("--enable-static" "--disable-shared")
    fi
    if [[ "$platform" != "macosx" ]]; then
        configure_args+=("--build=$(uname -m)-apple-darwin")
    fi

    env CC="$target_cc" CFLAGS="$target_cflags" LDFLAGS="$target_ldflags" CC_FOR_BUILD="/usr/bin/clang" \
        ./configure "${configure_args[@]}"

    make -j"$(sysctl -n hw.ncpu)"
    make install DESTDIR="$BUILDDIR/install-$platform-$arch"

    mkdir -p "$LIBDIR"
    if [[ "$LINKAGE" == "shared" ]]; then
        # dylib is versioned (libgmp.10.dylib); resolve the unversioned symlink
        # to the real file so we copy a genuine Mach-O, not a symlink.
        local built_dylib
        built_dylib=$(readlink -f "$BUILDDIR/install-$platform-$arch/usr/local/lib/lib$LIBNAME.dylib" 2>/dev/null \
            || echo "$BUILDDIR/install-$platform-$arch/usr/local/lib/lib$LIBNAME.dylib")
        # Fix the install name to @rpath NOW, before any dependent library
        # (MPFR/MPC/FLINT) links against this dylib -- otherwise the dependent
        # records GMP's raw /usr/local/lib/... path as its load command and
        # can't resolve it at runtime. Fix it on the install-tree original so
        # dependents that link via --with-gmp=<install prefix> also get @rpath,
        # then copy (the copy inherits the fix). The wrapper re-applies the same
        # -id on the framework copy (idempotent).
        install_name_tool -id "@rpath/GMP.framework/GMP" "$built_dylib"
        cp "$built_dylib" "$LIBDIR/lib$LIBNAME-$platform-$arch.dylib"
    else
        cp "$BUILDDIR/install-$platform-$arch/usr/local/lib/lib$LIBNAME.a" "$LIBDIR/lib$LIBNAME-$platform-$arch.a"
    fi
}

createXCFramework() {
    local framework_name="GMP"
    local framework_dir="$SCRIPTDIR/$framework_name.xcframework"
    # Define the consistent binary name that CocoaPods expects.
    local consistent_binary_name="libgmp.a"
    
    logMsg "Creating and patching $framework_name.xcframework..."
    rm -rf "$framework_dir"

    local device_lib="$LIBDIR/lib$LIBNAME-iphoneos-arm64.a"
    local sim_universal_lib="$LIBDIR/lib$LIBNAME-iphonesimulator-universal.a"
    local mac_universal_lib="$LIBDIR/lib$LIBNAME-macosx-universal.a"
    
    lipo -create -output "$sim_universal_lib" "$LIBDIR/lib$LIBNAME-iphonesimulator-x86_64.a" "$LIBDIR/lib$LIBNAME-iphonesimulator-arm64.a"
    lipo -create -output "$mac_universal_lib" "$LIBDIR/lib$LIBNAME-macosx-x86_64.a" "$LIBDIR/lib$LIBNAME-macosx-arm64.a"
    
    mkdir -p "$HEADERDIR"; cp "$BUILDDIR/install-iphoneos-arm64/usr/local/include/gmp.h" "$HEADERDIR/"

    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR" \
        -library "$sim_universal_lib" -headers "$HEADERDIR" \
        -library "$mac_universal_lib" -headers "$HEADERDIR" \
        -output "$framework_dir"

    logMsg "Patching generated framework for consistent naming..."
    
    # Rename the internal binaries to the consistent name.
    mv "$framework_dir/ios-arm64/libgmp-iphoneos-arm64.a" "$framework_dir/ios-arm64/$consistent_binary_name"
    mv "$framework_dir/ios-arm64_x86_64-simulator/libgmp-iphonesimulator-universal.a" "$framework_dir/ios-arm64_x86_64-simulator/$consistent_binary_name"
    mv "$framework_dir/macos-arm64_x86_64/libgmp-macosx-universal.a" "$framework_dir/macos-arm64_x86_64/$consistent_binary_name"

    local PLIST_PATH="$framework_dir/Info.plist"
    local COUNT
    COUNT=$(/usr/libexec/PlistBuddy -c "Print :AvailableLibraries:" "$PLIST_PATH" | grep -c "Dict")
    for (( i=0; i<COUNT; i++ )); do
        
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:BinaryPath $consistent_binary_name" "$PLIST_PATH"
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:LibraryPath $consistent_binary_name" "$PLIST_PATH"
    done
    
    logMsg "✅ Successfully created and patched $framework_dir"
}

# Route B: wrap the per-arch dylibs into a dynamic (embeddable, relinkable)
# GMP.xcframework via the shared wrap_dynamic_framework.sh helper. Kept fully
# separate from the static createXCFramework above so the default path is
# untouched.
createDynamicXCFramework() {
    logMsg "Assembling DYNAMIC GMP.xcframework (Route B / LGPL-relinkable)..."
    local sim_universal="$LIBDIR/lib$LIBNAME-iphonesimulator-universal.dylib"
    local mac_universal="$LIBDIR/lib$LIBNAME-macosx-universal.dylib"
    lipo -create -output "$sim_universal" \
        "$LIBDIR/lib$LIBNAME-iphonesimulator-x86_64.dylib" \
        "$LIBDIR/lib$LIBNAME-iphonesimulator-arm64.dylib"
    lipo -create -output "$mac_universal" \
        "$LIBDIR/lib$LIBNAME-macosx-x86_64.dylib" \
        "$LIBDIR/lib$LIBNAME-macosx-arm64.dylib"

    mkdir -p "$HEADERDIR"; cp "$BUILDDIR/install-iphoneos-arm64/usr/local/include/gmp.h" "$HEADERDIR/"

    "$SCRIPTDIR/wrap_dynamic_framework.sh" "GMP" "$HEADERDIR/gmp.h" "$SCRIPTDIR" \
        "ios-arm64:arm64:$LIBDIR/lib$LIBNAME-iphoneos-arm64.dylib" \
        "ios-arm64_x86_64-simulator:universal:$sim_universal" \
        "macos-arm64_x86_64:universal:$mac_universal"
}

# --- Main Build Logic ---
logMsg "Starting GMP build (LINKAGE=$LINKAGE)..."
if [ -d "$BUILDDIR" ]; then rm -rf "$BUILDDIR"; fi
extractSoftware
for arch in $DEVARCHS; do configureAndMake "iphoneos" "$arch"; done
for arch in $SIMARCHS; do configureAndMake "iphonesimulator" "$arch"; done
for arch in $MACARCHS; do configureAndMake "macosx" "$arch"; done
if [[ "$LINKAGE" == "shared" ]]; then
    createDynamicXCFramework
else
    createXCFramework
fi
logMsg "🚀 GMP build process completed successfully!"
exit 0
