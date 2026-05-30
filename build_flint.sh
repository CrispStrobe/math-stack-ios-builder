#!/bin/bash
############################################################################
#
# build_flint.sh 
#
# Builds the FLINT library by using the native 'make install' step to
# reliably collect headers and libraries, creating a clean XCFramework.
#
############################################################################
set -e # Exit immediately if a command exits with a non-zero status.

# --- Configuration ---
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-flint"
readonly LIBDIR="$BUILDDIR/lib"
readonly HEADERDIR="$BUILDDIR/include"
readonly LIBNAME="flint"
readonly VERSION="3.3.1"
readonly SOFTWARETAR="$SCRIPTDIR/$LIBNAME-$VERSION.tar.gz"

# --- Dependency Paths (MUST be built first) ---
readonly GMP_BUILDDIR="$SCRIPTDIR/build-gmp"
readonly GMP_LIBDIR="$GMP_BUILDDIR/lib"
readonly GMP_HEADERS_DIR="$GMP_BUILDDIR/include"

readonly MPFR_BUILDDIR="$SCRIPTDIR/build-mpfr"
readonly MPFR_LIBDIR="$MPFR_BUILDDIR/lib"
readonly MPFR_HEADERS_DIR="$MPFR_BUILDDIR/include"

# --- Architectures & Deployment Targets ---
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"

readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

# --- Utility Functions ---
# shellcheck disable=SC2329,SC2317
cleanup() { echo "[CLEANUP] FLINT build script finished."; }
trap cleanup EXIT

logMsg() { printf "[FLINT BUILD] %s\n" "$1"; }
errorExit() { logMsg "❌ ERROR: $1"; logMsg "Build failed."; exit 1; }

# --- Build Functions ---

checkDependencies() {
    logMsg "Checking for pre-built GMP and MPFR dependencies..."
    if [ ! -d "$GMP_BUILDDIR" ]; then errorExit "GMP build directory not found."; fi
    if [ ! -d "$MPFR_BUILDDIR" ]; then errorExit "MPFR build directory not found."; fi
    logMsg "✅ Dependencies found."
}

extractSoftware() {
    local extractdir="$BUILDDIR/source"
    logMsg "Creating build directory and extracting FLINT source..."
    mkdir -p "$extractdir"
    if [ ! -f "$SOFTWARETAR" ]; then errorExit "FLINT archive not found at '$SOFTWARETAR'."; fi
    tar -xzf "$SOFTWARETAR" -C "$extractdir" --strip-components 1 || errorExit "Failed to extract FLINT tarball."
}

configureAndMake() {
    local platform=$1
    local arch=$2
    local extractdir="$BUILDDIR/source"
    
    logMsg "================================================================="
    logMsg "Configuring FLINT for PLATFORM: $platform, ARCH: $arch"
    logMsg "================================================================="
    
    unset CC CXX CFLAGS CXXFLAGS LDFLAGS LIBS SDKROOT CC_FOR_BUILD
    
    local sdkpath
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    if [ ! -d "$sdkpath" ]; then errorExit "SDK path for '$platform' not found."; fi

    local target_cc
    target_cc=$(xcrun --sdk "$platform" -f clang)
    local temp_lib_dir="$BUILDDIR/temp-deps-$platform-$arch"
    mkdir -p "$temp_lib_dir"
    ln -sf "$GMP_LIBDIR/libgmp-$platform-$arch.a" "$temp_lib_dir/libgmp.a"
    ln -sf "$MPFR_LIBDIR/libmpfr-$platform-$arch.a" "$temp_lib_dir/libmpfr.a"

    local target_cflags
    local target_ldflags
    
    if [[ "$platform" == "iphoneos" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION -I$GMP_HEADERS_DIR -I$MPFR_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION -L$temp_lib_dir"
    elif [[ "$platform" == "iphonesimulator" ]]; then
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION -I$GMP_HEADERS_DIR -I$MPFR_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION -L$temp_lib_dir"
    else # macosx
        target_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION -I$GMP_HEADERS_DIR -I$MPFR_HEADERS_DIR"
        target_ldflags="-arch $arch -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION -L$temp_lib_dir"
    fi
    
    cd "$extractdir"
    
    if [ -f "Makefile" ]; then
      make distclean &> /dev/null || true
    fi
    
    local host_triplet
    host_triplet=$([[ "$arch" == "arm64" ]] && echo "aarch64" || echo "$arch")-apple-darwin
    local configure_args=(
        "--host=$host_triplet"
        "--disable-shared"
        "--enable-static"
        "--disable-assembly"
        "--with-gmp=$GMP_BUILDDIR"
        "--with-mpfr=$MPFR_BUILDDIR"
        "--disable-thread-safe"
    )
    if [[ "$platform" != "macosx" ]]; then
        configure_args+=("--build=$(uname -m)-apple-darwin")
    fi
    
    env CC="$target_cc" CFLAGS="$target_cflags" LDFLAGS="$target_ldflags" LIBS="-lmpfr -lgmp" CC_FOR_BUILD="/usr/bin/clang" \
        ./configure "${configure_args[@]}"

    logMsg "Building FLINT for $platform $arch..."
    make -j"$(sysctl -n hw.ncpu)"
    
    logMsg "Installing FLINT to temporary location..."
    local install_dir="$BUILDDIR/install-$platform-$arch"
    rm -rf "$install_dir"
    make install DESTDIR="$install_dir"

    logMsg "Copying built FLINT library..."
    mkdir -p "$LIBDIR"
    cp "$install_dir/usr/local/lib/lib$LIBNAME.a" "$LIBDIR/lib$LIBNAME-$platform-$arch.a"
    
    rm -rf "$temp_lib_dir"
}

#
# THIS FUNCTION HAS BEEN CORRECTED
#
createXCFramework() {
    local framework_name="FLINT"
    local framework_dir="$SCRIPTDIR/$framework_name.xcframework"

    logMsg "================================================================="
    logMsg "Creating and patching $framework_name.xcframework"
    logMsg "================================================================="

    rm -rf "$framework_dir"

    # Define source library paths
    local device_lib="$LIBDIR/lib$LIBNAME-iphoneos-arm64.a"
    local sim_universal_lib="$LIBDIR/lib$LIBNAME-iphonesimulator-universal.a"
    local mac_universal_lib="$LIBDIR/lib$LIBNAME-macosx-universal.a"

    logMsg "Creating universal libraries..."
    lipo -create -output "$sim_universal_lib" "$LIBDIR"/lib$LIBNAME-iphonesimulator-*.a
    lipo -create -output "$mac_universal_lib" "$LIBDIR"/lib$LIBNAME-macosx-*.a

    # Step 1: Create the XCFramework
    logMsg "Assembling initial XCFramework..."
    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR" \
        -library "$sim_universal_lib" -headers "$HEADERDIR" \
        -library "$mac_universal_lib" -headers "$HEADERDIR" \
        -output "$framework_dir"

    # Step 2: Immediately Patch the XCFramework
    logMsg "Patching generated framework for CocoaPods compatibility..."
    
    # Rename the binaries inside the framework to be consistent
    mv "$framework_dir/ios-arm64/libflint-iphoneos-arm64.a" "$framework_dir/ios-arm64/$framework_name"
    mv "$framework_dir/ios-arm64_x86_64-simulator/libflint-iphonesimulator-universal.a" "$framework_dir/ios-arm64_x86_64-simulator/$framework_name"
    mv "$framework_dir/macos-arm64_x86_64/libflint-macosx-universal.a" "$framework_dir/macos-arm64_x86_64/$framework_name"

    # Edit the manifest (Info.plist) to reflect the new, consistent binary names
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
logMsg "Starting FLINT build..."
checkDependencies
if [ -d "$BUILDDIR" ]; then logMsg "Cleaning old FLINT build directory..."; rm -rf "$BUILDDIR"; fi
extractSoftware

logMsg "--- Building for iOS Device ---"
for arch in $DEVARCHS; do configureAndMake "iphoneos" "$arch"; done

# Capture headers from the clean 'install' directory
logMsg "Capturing installed headers..."
rm -rf "$HEADERDIR"
cp -R "$BUILDDIR/install-iphoneos-arm64/usr/local/include/." "$HEADERDIR/"
logMsg "✅ Headers captured successfully."

logMsg "--- Building for iOS Simulator ---"
for arch in $SIMARCHS; do configureAndMake "iphonesimulator" "$arch"; done

logMsg "--- Building for macOS ---"
for arch in $MACARCHS; do configureAndMake "macosx" "$arch"; done

createXCFramework

logMsg "🚀 FLINT build process completed successfully!"
exit 0