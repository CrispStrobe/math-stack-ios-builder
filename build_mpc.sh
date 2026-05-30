#!/bin/bash
############################################################################
#
# build_mpc.sh (Corrected & Robust Version)
#
# Builds MPC for iOS, Simulator, & macOS, creating a clean XCFramework.
# This script uses modern, safe environment handling to prevent build errors.
#
############################################################################
set -e # Exit immediately if a command exits with a non-zero status.

# --- Configuration ---
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-mpc"
readonly LIBDIR="$BUILDDIR/lib"
readonly HEADERDIR="$BUILDDIR/include"
readonly LIBNAME="mpc"
readonly VERSION="1.3.1"
readonly SOFTWARETAR="$SCRIPTDIR/$LIBNAME-$VERSION.tar.gz"

# --- Dependency Paths ---
readonly GMP_BUILDDIR="$SCRIPTDIR/build-gmp"
readonly GMP_LIBDIR="$GMP_BUILDDIR/lib"
readonly GMP_HEADERDIR="$GMP_BUILDDIR/include"

readonly MPFR_BUILDDIR="$SCRIPTDIR/build-mpfr"
readonly MPFR_LIBDIR="$MPFR_BUILDDIR/lib"
readonly MPFR_HEADERDIR="$MPFR_BUILDDIR/include"

# --- Architectures ---
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"

# --- Deployment Targets ---
readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

# --- Utility Functions ---

# shellcheck disable=SC2329
cleanup() {
    echo "[CLEANUP] MPC build script finished."
}
trap cleanup EXIT

logMsg() {
    printf "[MPC BUILD] %s\n" "$1"
}

errorExit() {
    logMsg "❌ ERROR: $1"
    logMsg "Build failed."
    exit 1
}

# --- Build Functions ---

checkDependencies() {
    logMsg "Checking for pre-built GMP and MPFR dependencies..."
    if [ ! -d "$GMP_BUILDDIR" ] || [ ! -d "$GMP_LIBDIR" ] || [ ! -d "$GMP_HEADERDIR" ]; then
        errorExit "GMP build directory, libraries, or headers not found. Please run build_gmp.sh first."
    fi
    if [ ! -d "$MPFR_BUILDDIR" ] || [ ! -d "$MPFR_LIBDIR" ] || [ ! -d "$MPFR_HEADERDIR" ]; then
        errorExit "MPFR build directory, libraries, or headers not found. Please run build_mpfr.sh first."
    fi
    logMsg "✅ Dependencies found."
}

extractSoftware() {
    local extractdir="$BUILDDIR/source"
    logMsg "Creating build directory and extracting MPC source..."
    mkdir -p "$extractdir"
    
    if [ ! -f "$SOFTWARETAR" ]; then
        errorExit "MPC archive not found at '$SOFTWARETAR'."
    fi

    tar -xzf "$SOFTWARETAR" -C "$extractdir" --strip-components 1 || errorExit "Failed to extract MPC tarball."
}

configureAndMake() {
    local platform=$1
    local arch=$2
    local extractdir="$BUILDDIR/source"
    
    logMsg "================================================================="
    logMsg "Configuring MPC for PLATFORM: $platform, ARCH: $arch"
    logMsg "================================================================="
    
    # Unset variables for a clean state
    unset CC CXX CFLAGS CXXFLAGS LDFLAGS LIBS SDKROOT CC_FOR_BUILD
    
    local sdkpath
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    if [ ! -d "$sdkpath" ]; then
        errorExit "SDK path for platform '$platform' not found. Is Xcode installed?"
    fi
    logMsg "Using SDK: $sdkpath"
    
    # Define local variables for compiler and flags without exporting them
    local target_cc
    target_cc=$(xcrun --sdk "$platform" -f clang)

    local gmp_lib_for_arch="$GMP_LIBDIR/libgmp-$platform-$arch.a"
    local mpfr_lib_for_arch="$MPFR_LIBDIR/libmpfr-$platform-$arch.a"

    if [[ ! -f "$gmp_lib_for_arch" ]] || [[ ! -f "$mpfr_lib_for_arch" ]]; then
        errorExit "Missing dependency for $platform-$arch. Libs not found:\n$gmp_lib_for_arch\n$mpfr_lib_for_arch"
    fi

    local target_cflags
    local target_ldflags
    
    # Common flags plus dependency headers
    local common_cflags="-arch $arch -pipe -Os -isysroot $sdkpath -I$GMP_HEADERDIR -I$MPFR_HEADERDIR"
    local common_ldflags="-arch $arch -isysroot $sdkpath"

    if [[ "$platform" == "iphoneos" ]]; then
        target_cflags="$common_cflags -miphoneos-version-min=$IOS_MIN_VERSION"
        target_ldflags="$common_ldflags -miphoneos-version-min=$IOS_MIN_VERSION"
    elif [[ "$platform" == "iphonesimulator" ]]; then
        target_cflags="$common_cflags -mios-simulator-version-min=$IOS_MIN_VERSION"
        target_ldflags="$common_ldflags -mios-simulator-version-min=$IOS_MIN_VERSION"
    else # macosx
        target_cflags="$common_cflags -mmacosx-version-min=$MACOS_MIN_VERSION"
        target_ldflags="$common_ldflags -mmacosx-version-min=$MACOS_MIN_VERSION"
    fi
    
    cd "$extractdir"
    
    # ./configure sometimes fails if it can't run a `make clean` first
    # So we run it here manually, ignoring errors if it fails.
    if [ -f "Makefile" ]; then
      make distclean &> /dev/null || true
    fi
    
    local host_triplet
    host_triplet=$( [[ "$arch" == "arm64" ]] && echo "aarch64" || echo "$arch" )-apple-darwin
    
    # We pass the full path to the libs, so --with-gmp-lib can point to GMP_LIBDIR
    local configure_args=(
        "--host=$host_triplet"
        "--disable-shared"
        "--enable-static"
        "--with-gmp=$GMP_BUILDDIR/install-$platform-$arch/usr/local"
        "--with-mpfr=$MPFR_BUILDDIR/install-$platform-$arch/usr/local"
    )

    local build_cc="/usr/bin/clang"
    local build_host_triplet=""
    
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
    
    # Pass environment variables on the SAME LINE to prevent pollution.
    env \
        CC="$target_cc" \
        CFLAGS="$target_cflags" \
        LDFLAGS="$target_ldflags -L$GMP_LIBDIR -L$MPFR_LIBDIR" \
        LIBS="-lmpfr -lgmp" \
        CC_FOR_BUILD="$build_cc" \
        ./configure "${configure_args[@]}"

    logMsg "Building MPC for $platform $arch..."
    make -j"$(sysctl -n hw.ncpu)"
    
    logMsg "Installing built library and headers..."
    make install DESTDIR="$BUILDDIR/install-$platform-$arch"
    
    mkdir -p "$LIBDIR"
    cp "$BUILDDIR/install-$platform-$arch/usr/local/lib/lib$LIBNAME.a" "$LIBDIR/lib$LIBNAME-$platform-$arch.a"
}

#
# THIS FUNCTION HAS BEEN CORRECTED
#
createXCFramework() {
    local framework_name="MPC"
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
    lipo -create -output "$sim_universal_lib" \
        "$LIBDIR/lib$LIBNAME-iphonesimulator-x86_64.a" \
        "$LIBDIR/lib$LIBNAME-iphonesimulator-arm64.a"

    lipo -create -output "$mac_universal_lib" \
        "$LIBDIR/lib$LIBNAME-macosx-x86_64.a" \
        "$LIBDIR/lib$LIBNAME-macosx-arm64.a"
        
    logMsg "Copying headers..."
    mkdir -p "$HEADERDIR"
    cp "$BUILDDIR/install-iphoneos-arm64/usr/local/include/mpc.h" "$HEADERDIR/"

    # Step 1: Create the XCFramework (which will have an incorrect manifest)
    logMsg "Assembling initial XCFramework with xcodebuild..."
    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR" \
        -library "$sim_universal_lib" -headers "$HEADERDIR" \
        -library "$mac_universal_lib" -headers "$HEADERDIR" \
        -output "$framework_dir"

    # Step 2: Immediately Patch the XCFramework We Just Created
    logMsg "Patching generated framework for CocoaPods compatibility..."
    
    # Rename the binaries inside the framework to be consistent
    mv "$framework_dir/ios-arm64/libmpc-iphoneos-arm64.a" "$framework_dir/ios-arm64/$framework_name"
    mv "$framework_dir/ios-arm64_x86_64-simulator/libmpc-iphonesimulator-universal.a" "$framework_dir/ios-arm64_x86_64-simulator/$framework_name"
    mv "$framework_dir/macos-arm64_x86_64/libmpc-macosx-universal.a" "$framework_dir/macos-arm64_x86_64/$framework_name"

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

logMsg "Starting MPC build..."

checkDependencies

if [ -d "$BUILDDIR" ]; then
    logMsg "Cleaning old MPC build directory..."
    rm -rf "$BUILDDIR"
fi

extractSoftware

logMsg "--- Building MPC for iOS Device ---"
for arch in $DEVARCHS; do
    configureAndMake "iphoneos" "$arch"
done

logMsg "--- Building MPC for iOS Simulator ---"
for arch in $SIMARCHS; do
    configureAndMake "iphonesimulator" "$arch"
done

logMsg "--- Building MPC for macOS ---"
for arch in $MACARCHS; do
    configureAndMake "macosx" "$arch"
done

createXCFramework

logMsg "🚀 MPC build process completed successfully!"
exit 0