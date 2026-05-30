#!/bin/bash
############################################################################
#
# build-mpfr.sh (Corrected & Robust Version)
#
# Builds the MPFR library for iOS, Simulator, and macOS, creating a
# single MPFR.xcframework.
#
# This script correctly handles cross-compilation environments to avoid
# configuration errors and build warnings.
#
############################################################################
set -e # Exit immediately if a command exits with a non-zero status.

# --- Configuration ---
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-mpfr"
readonly LIBDIR="$BUILDDIR/lib"
readonly HEADERDIR="$BUILDDIR/include"
readonly LIBNAME="mpfr"
readonly VERSION="4.2.2"
readonly SOFTWARETAR="$SCRIPTDIR/$LIBNAME-$VERSION.tar.xz"

# GMP dependency paths (must exist from previous GMP build)
readonly GMP_BUILDDIR="$SCRIPTDIR/build-gmp"
readonly GMP_LIBDIR="$GMP_BUILDDIR/lib"
readonly GMP_HEADERS_DIR="$GMP_BUILDDIR/source"

# Architectures
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"

# Minimum deployment targets
readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

# --- Functions ---
# shellcheck disable=SC2329,SC2317
cleanup() {
    echo "[CLEANUP] MPFR build script finished."
}
trap cleanup EXIT

logMsg() {
    printf "[MPFR BUILD] %s\n" "$1"
}

errorExit() {
    logMsg "❌ ERROR: $1"
    logMsg "Build failed."
    exit 1
}

checkGmpDependency() {
    logMsg "Checking GMP dependency..."
    if [ ! -d "$GMP_BUILDDIR" ] || [ ! -f "$GMP_HEADERS_DIR/gmp.h" ]; then
        errorExit "GMP build not found. Please run ./build_gmp.sh first."
    fi
    logMsg "GMP dependency check passed."
}

extractSoftware() {
    local extractdir="$BUILDDIR/source"
    logMsg "Creating build directory and extracting MPFR source..."
    mkdir -p "$extractdir"

    if [ ! -f "$SOFTWARETAR" ]; then
        errorExit "MPFR archive not found at $SOFTWARETAR. Please download mpfr-$VERSION.tar.xz"
    fi

    tar -xf "$SOFTWARETAR" -C "$extractdir" --strip-components 1 || errorExit "Failed to extract MPFR tarball."
}

#
# THIS FUNCTION CONTAINS THE PRIMARY FIX
#
configureAndMake() {
    local platform=$1
    local arch=$2
    local extractdir="$BUILDDIR/source"
    
    logMsg "================================================================="
    logMsg "Configuring MPFR for PLATFORM: $platform, ARCH: $arch"
    logMsg "================================================================="
    
    # Unset variables to ensure a completely clean state from any previous run.
    unset CC CXX CFLAGS CXXFLAGS LDFLAGS LIBS SDKROOT CC_FOR_BUILD

    # --- Define variables locally WITHOUT exporting them ---

    local sdkpath
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    if [ ! -d "$sdkpath" ]; then
        errorExit "SDK path not found for platform '$platform'. Is Xcode installed?"
    fi
    logMsg "Using SDK: $sdkpath"

    # Set up a temporary directory for the correctly-named GMP library,
    # as the configure script expects `libgmp.a`.
    local temp_gmp_lib_dir="$BUILDDIR/temp-gmp-$platform-$arch"
    mkdir -p "$temp_gmp_lib_dir"
    ln -sf "$GMP_LIBDIR/libgmp-$platform-$arch.a" "$temp_gmp_lib_dir/libgmp.a"

    local target_cc
    target_cc=$(xcrun --sdk "$platform" -f clang)

    local target_cflags
    local target_ldflags

    if [[ "$platform" == "iphoneos" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION -I$GMP_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION -L$temp_gmp_lib_dir"
    elif [[ "$platform" == "iphonesimulator" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION -I$GMP_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION -L$temp_gmp_lib_dir"
    else # macosx
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION -I$GMP_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION -L$temp_gmp_lib_dir"
    fi

    cd "$extractdir"
    
    make distclean &> /dev/null || true
    
    local host_triplet
    host_triplet=$( [[ "$arch" == "arm64" ]] && echo "aarch64" || echo "$arch" )-apple-darwin

    local configure_args=(
        "--host=$host_triplet"
        "--with-gmp-include=$GMP_HEADERS_DIR"
        "--with-gmp-lib=$temp_gmp_lib_dir"
        "--disable-shared"
        "--enable-static"
    )

    local build_cc="/usr/bin/clang"
    local build_host_triplet
    
    if [[ "$platform" != "macosx" ]]; then
        build_host_triplet=$(uname -m)-apple-darwin
        configure_args+=( "--build=$build_host_triplet" )
        
        logMsg "Configuring with (Cross-Compilation):"
        logMsg "  CC_FOR_BUILD: $build_cc"
        logMsg "  BUILD_HOST:   $build_host_triplet"
    else
        logMsg "Configuring with (Native Compilation):"
    fi
    
    logMsg "  TARGET_HOST:  $host_triplet"
    logMsg "  CC (target):  $target_cc"
    logMsg "  CFLAGS:       $target_cflags"
    logMsg "  LDFLAGS:      $target_ldflags"

    # *** KEY CHANGE HERE ***
    # Pass environment variables on the SAME LINE as the command to prevent leakage.
    env \
        CC="$target_cc" \
        CFLAGS="$target_cflags" \
        LDFLAGS="$target_ldflags" \
        LIBS="-lgmp" \
        CC_FOR_BUILD="$build_cc" \
        ./configure "${configure_args[@]}"

    logMsg "Building MPFR for $platform $arch..."
    make -j"$(sysctl -n hw.ncpu)"
    make install DESTDIR="$BUILDDIR/install-$platform-$arch"

    logMsg "Copying built MPFR library..."
    mkdir -p "$LIBDIR"
    cp "$BUILDDIR/install-$platform-$arch/usr/local/lib/lib$LIBNAME.a" "$LIBDIR/lib$LIBNAME-$platform-$arch.a"
    
    # Clean up temp directory
    rm -rf "$temp_gmp_lib_dir"
}

createFramework() {
    local framework_name="MPFR"
    local framework_dir="$SCRIPTDIR/$framework_name.xcframework"
    logMsg "Creating $framework_name.xcframework..."
    rm -rf "$framework_dir"

    # Define source library paths using the correct LIBNAME for mpfr
    local device_lib="$LIBDIR/lib$LIBNAME-iphoneos-arm64.a"
    local sim_universal_lib="$LIBDIR/lib$LIBNAME-iphonesimulator-universal.a"
    local mac_universal_lib="$LIBDIR/lib$LIBNAME-macosx-universal.a"
    
    # Create universal "fat" libs for simulator and macOS
    lipo -create -output "$sim_universal_lib" "$LIBDIR/lib$LIBNAME-iphonesimulator-x86_64.a" "$LIBDIR/lib$LIBNAME-iphonesimulator-arm64.a"
    lipo -create -output "$mac_universal_lib" "$LIBDIR/lib$LIBNAME-macosx-x86_64.a" "$LIBDIR/lib$LIBNAME-macosx-arm64.a"
    
    # --- FIX 1: Copy MPFR's own headers, not GMP's ---
    mkdir -p "$HEADERDIR"
    # The 'make install' step places the correct headers in this directory
    cp "$BUILDDIR/install-iphoneos-arm64/usr/local/include/"*.h "$HEADERDIR/"

    # Step 1: Create the XCFramework
    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR" \
        -library "$sim_universal_lib" -headers "$HEADERDIR" \
        -library "$mac_universal_lib" -headers "$HEADERDIR" \
        -output "$framework_dir"

    # Step 2: Immediately Patch the XCFramework
    logMsg "Patching generated framework for CocoaPods compatibility..."
    
    # --- FIX 2: Use the correct MPFR library filenames for renaming ---
    mv "$framework_dir/ios-arm64/libmpfr-iphoneos-arm64.a" "$framework_dir/ios-arm64/$framework_name"
    mv "$framework_dir/ios-arm64_x86_64-simulator/libmpfr-iphonesimulator-universal.a" "$framework_dir/ios-arm64_x86_64-simulator/$framework_name"
    mv "$framework_dir/macos-arm64_x86_64/libmpfr-macosx-universal.a" "$framework_dir/macos-arm64_x86_64/$framework_name"

    # Edit the manifest (Info.plist)
    local PLIST_PATH="$framework_dir/Info.plist"
    local COUNT
    COUNT=$(/usr/libexec/PlistBuddy -c "Print :AvailableLibraries:" "$PLIST_PATH" | grep -c "Dict")
    for (( i=0; i<COUNT; i++ )); do
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:BinaryPath $framework_name" "$PLIST_PATH"
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:LibraryPath $framework_name" "$PLIST_PATH"
    done
    
    logMsg "✅ Successfully created and patched $framework_dir"
}

# --- Main Build Logic ---
logMsg "Starting MPFR build for iOS, Simulator, and macOS..."

checkGmpDependency

if [ -d "$BUILDDIR" ]; then
    logMsg "Cleaning old MPFR build directory..."
    rm -rf "$BUILDDIR"
fi

extractSoftware

logMsg "--- Building MPFR for iOS Device ---"
for arch in $DEVARCHS; do
    configureAndMake "iphoneos" "$arch"
done

logMsg "--- Building MPFR for iOS Simulator ---"
for arch in $SIMARCHS; do
    configureAndMake "iphonesimulator" "$arch"
done

logMsg "--- Building MPFR for macOS ---"
for arch in $MACARCHS; do
    configureAndMake "macosx" "$arch"
done

createFramework

logMsg "🚀 MPFR build process completed successfully!"
exit 0