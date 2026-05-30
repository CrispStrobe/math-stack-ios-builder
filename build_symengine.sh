#!/bin/bash
############################################################################
#
# build_symengine.sh (SINGLE COMPILATION POINT - includes Flutter wrapper)
#
############################################################################
set -e

# --- Configuration ---
SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR
readonly BUILDDIR="$SCRIPTDIR/build-symengine"
readonly LIBDIR="$BUILDDIR/lib"

readonly COMBINED_LIB_BASENAME="symengine_flutter_wrapper" 

readonly HEADERDIR_FINAL="$BUILDDIR/include"
readonly VERSION_SYMENGINE="0.11.2"
readonly SYMENGINE_SOURCE="$SCRIPTDIR/symengine-$VERSION_SYMENGINE"

# Flutter wrapper source directory
readonly FLUTTER_WRAPPER_SRC="$SCRIPTDIR/src"

# Dependency paths
readonly GMP_BUILDDIR="$SCRIPTDIR/build-gmp"
readonly MPFR_BUILDDIR="$SCRIPTDIR/build-mpfr"
readonly MPC_BUILDDIR="$SCRIPTDIR/build-mpc"
readonly FLINT_BUILDDIR="$SCRIPTDIR/build-flint"

# Architectures
readonly DEVARCHS="arm64"
readonly SIMARCHS="x86_64 arm64"
readonly MACARCHS="x86_64 arm64"

# Minimum deployment targets
readonly IOS_MIN_VERSION="13.0"
readonly MACOS_MIN_VERSION="10.15"

# --- Utility Functions ---
# shellcheck disable=SC2329
cleanup() { echo "[CLEANUP] SymEngine build script finished."; }
trap cleanup EXIT

logMsg() { printf "[SYMENGINE BUILD] %s\n" "$1"; }
errorExit() { logMsg "❌ ERROR: $1"; logMsg "Build failed."; exit 1; }

checkCMake() {
    if ! command -v cmake &> /dev/null; then
        errorExit "CMake not found. Please run: brew install cmake"
    fi
    logMsg "✅ CMake found: $(cmake --version | head -1)"
}

checkDependencies() {
    logMsg "Checking for dependency build directories..."
    for dep_dir in "$GMP_BUILDDIR" "$MPFR_BUILDDIR" "$MPC_BUILDDIR" "$FLINT_BUILDDIR"; do
        if [ ! -d "$dep_dir" ]; then
            errorExit "Dependency directory '$dep_dir' not found. Please build all dependencies first."
        fi
    done
    logMsg "✅ All dependency directories found."
}

checkFlutterWrapperSource() {
    logMsg "Checking for Flutter wrapper source files..."
    if [ ! -f "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h" ]; then
        errorExit "Flutter wrapper header not found: $FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h"
    fi
    if [ ! -f "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.c" ]; then
        errorExit "Flutter wrapper source not found: $FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.c"
    fi
    logMsg "✅ Flutter wrapper source files found."
}

downloadSymEngine() {
    if [ -d "$SYMENGINE_SOURCE" ]; then return; fi
    logMsg "Downloading SymEngine v$VERSION_SYMENGINE..."
    local tarball="$SCRIPTDIR/symengine-$VERSION_SYMENGINE.tar.gz"
    if [ ! -f "$tarball" ]; then
        curl -L -o "$tarball" "https://github.com/symengine/symengine/archive/v$VERSION_SYMENGINE.tar.gz"
    fi
    tar -xzf "$tarball" -C "$SCRIPTDIR"
    if [ ! -d "$SYMENGINE_SOURCE" ]; then errorExit "Failed to extract SymEngine source"; fi
}

# Copy Flutter wrapper source files to build directory
copyFlutterWrapperToDir() {
    local target_dir="$1"
    mkdir -p "$target_dir"
    
    logMsg "Copying Flutter wrapper source files to $target_dir"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h" "$target_dir/"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.c" "$target_dir/"
    
    logMsg "✅ Copied Flutter wrapper source files to $target_dir"
}

configureAndMake() {
    local platform=$1; local arch=$2; local build_dir="$BUILDDIR/$platform-$arch"
    logMsg "================================================================="
    logMsg "Configuring SymEngine + Wrapper for PLATFORM: $platform, ARCH: $arch"
    logMsg "================================================================="

    local sdkpath
    sdkpath=$(xcrun --sdk "$platform" --show-sdk-path)
    rm -rf "$build_dir"; mkdir -p "$build_dir"; cd "$build_dir"
    copyFlutterWrapperToDir "$build_dir"

    local temp_lib_dir="$build_dir/temp-deps"; mkdir -p "$temp_lib_dir"
    ln -sf "$GMP_BUILDDIR/lib/libgmp-$platform-$arch.a" "$temp_lib_dir/libgmp.a"
    ln -sf "$MPFR_BUILDDIR/lib/libmpfr-$platform-$arch.a" "$temp_lib_dir/libmpfr.a"
    ln -sf "$MPC_BUILDDIR/lib/libmpc-$platform-$arch.a" "$temp_lib_dir/libmpc.a"
    ln -sf "$FLINT_BUILDDIR/lib/libflint-$platform-$arch.a" "$temp_lib_dir/libflint.a"

    local cmake_args=("-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DCMAKE_BUILD_TYPE=Release" "-DBUILD_SHARED_LIBS=OFF" "-DWITH_GMP=ON" "-DWITH_MPFR=ON" "-DWITH_MPC=ON" "-DWITH_FLINT=ON" "-DINTEGER_CLASS=flint" "-DBUILD_TESTS=OFF" "-DBUILD_BENCHMARKS=OFF" "-DGMP_INCLUDE_DIR=$GMP_BUILDDIR/include" "-DGMP_LIBRARY=$temp_lib_dir/libgmp.a" "-DMPFR_INCLUDE_DIR=$MPFR_BUILDDIR/include" "-DMPFR_LIBRARY=$temp_lib_dir/libmpfr.a" "-DMPC_INCLUDE_DIR=$MPC_BUILDDIR/include" "-DMPC_LIBRARY=$temp_lib_dir/libmpc.a" "-DFLINT_INCLUDE_DIR=$FLINT_BUILDDIR/include" "-DFLINT_LIBRARY=$temp_lib_dir/libflint.a")
    if [[ "$platform" == "iphoneos" ]] || [[ "$platform" == "iphonesimulator" ]]; then
        cmake_args+=("-DCMAKE_SYSTEM_NAME=iOS" "-DCMAKE_OSX_ARCHITECTURES=$arch" "-DCMAKE_OSX_SYSROOT=$sdkpath" "-DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_MIN_VERSION")
    else
        cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=$arch" "-DCMAKE_OSX_SYSROOT=$sdkpath" "-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_MIN_VERSION")
    fi

    cmake "$SYMENGINE_SOURCE" -DCMAKE_C_COMPILER="$(xcrun --sdk "$platform" -f clang)" -DCMAKE_CXX_COMPILER="$(xcrun --sdk "$platform" -f clang++)" "${cmake_args[@]}"
    make -j"$(sysctl -n hw.ncpu)"

    local target_cc
    target_cc=$(xcrun --sdk "$platform" -f clang)
    local target_cflags
    if [[ "$platform" == "iphoneos" ]]; then target_cflags="-arch $arch -isysroot $sdkpath -miphoneos-version-min=$IOS_MIN_VERSION"; elif [[ "$platform" == "iphonesimulator" ]]; then target_cflags="-arch $arch -isysroot $sdkpath -mios-simulator-version-min=$IOS_MIN_VERSION"; else target_cflags="-arch $arch -isysroot $sdkpath -mmacosx-version-min=$MACOS_MIN_VERSION"; fi
    target_cflags="$target_cflags -I$SYMENGINE_SOURCE -I$build_dir -I$GMP_BUILDDIR/include -I$MPFR_BUILDDIR/include -I$MPC_BUILDDIR/include -I$FLINT_BUILDDIR/include"
    # shellcheck disable=SC2086
    "$target_cc" $target_cflags -c "flutter_symengine_wrapper.c" -o "flutter_symengine_wrapper.o"
    
    local symengine_lib
    symengine_lib=$(find . -name "libsymengine.a" -type f | head -1)
    if [ -z "$symengine_lib" ]; then errorExit "Could not find built SymEngine library."; fi

    mkdir -p temp_extract; cd temp_extract
    ar x "../$symengine_lib"
    cp "../flutter_symengine_wrapper.o" .
    ar rcs "../lib${COMBINED_LIB_BASENAME}.a" ./*.o
    cd ..; rm -rf temp_extract

    mkdir -p "$LIBDIR"
    cp "lib${COMBINED_LIB_BASENAME}.a" "$LIBDIR/lib${COMBINED_LIB_BASENAME}-$platform-$arch.a"
}

createFlutterWrapperXCFramework() {
    local framework_name="SymEngineFlutterWrapper"
    local framework_dir="$SCRIPTDIR/$framework_name.xcframework"
    # Define the consistent binary name.
    local consistent_binary_name="lib${COMBINED_LIB_BASENAME}.a"
    
    logMsg "================================================================="
    logMsg "Creating and patching $framework_name.xcframework"
    logMsg "================================================================="

    rm -rf "$framework_dir"

    local device_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphoneos-arm64.a"
    local sim_universal_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-universal.a"
    local mac_universal_lib="$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-universal.a"
    
    lipo -create -output "$sim_universal_lib" "$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-x86_64.a" "$LIBDIR/lib${COMBINED_LIB_BASENAME}-iphonesimulator-arm64.a"
    lipo -create -output "$mac_universal_lib" "$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-x86_64.a" "$LIBDIR/lib${COMBINED_LIB_BASENAME}-macosx-arm64.a"

    rm -rf "$HEADERDIR_FINAL"; mkdir -p "$HEADERDIR_FINAL"
    cp "$FLUTTER_WRAPPER_SRC/flutter_symengine_wrapper.h" "$HEADERDIR_FINAL/"

    xcodebuild -create-xcframework \
        -library "$device_lib" -headers "$HEADERDIR_FINAL" \
        -library "$sim_universal_lib" -headers "$HEADERDIR_FINAL" \
        -library "$mac_universal_lib" -headers "$HEADERDIR_FINAL" \
        -output "$framework_dir"

    logMsg "Patching generated framework for consistent naming..."
    
    # Rename internal binaries to the consistent name.
    mv "$framework_dir/ios-arm64/$(basename "$device_lib")" "$framework_dir/ios-arm64/$consistent_binary_name"
    mv "$framework_dir/ios-arm64_x86_64-simulator/$(basename "$sim_universal_lib")" "$framework_dir/ios-arm64_x86_64-simulator/$consistent_binary_name"
    mv "$framework_dir/macos-arm64_x86_64/$(basename "$mac_universal_lib")" "$framework_dir/macos-arm64_x86_64/$consistent_binary_name"

    local PLIST_PATH="$framework_dir/Info.plist"
    local COUNT
    COUNT=$(/usr/libexec/PlistBuddy -c "Print :AvailableLibraries:" "$PLIST_PATH" | grep -c "Dict")
    for (( i=0; i<COUNT; i++ )); do
        # Update Info.plist to point to the consistent name.
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:BinaryPath $consistent_binary_name" "$PLIST_PATH"
        /usr/libexec/PlistBuddy -c "Set :AvailableLibraries:$i:LibraryPath $consistent_binary_name" "$PLIST_PATH"
    done
    
    logMsg "✅ Successfully created and patched $framework_dir"
}

# --- Main Build Logic ---
logMsg "Starting SymEngine + Flutter wrapper build..."
checkCMake; checkDependencies; checkFlutterWrapperSource; downloadSymEngine
if [ -d "$BUILDDIR" ]; then rm -rf "$BUILDDIR"; fi
for ARCH in $DEVARCHS; do configureAndMake "iphoneos" "$ARCH"; done
for ARCH in $SIMARCHS; do configureAndMake "iphonesimulator" "$ARCH"; done
for ARCH in $MACARCHS; do configureAndMake "macosx" "$ARCH"; done
createFlutterWrapperXCFramework
logMsg "🚀 SymEngine + Flutter wrapper build process completed successfully!"
exit 0

