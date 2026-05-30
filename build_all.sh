#!/bin/bash
#############################################################################
#
# build_all.sh
#
# Master build script for the entire symbolic math stack.
# This runs each library's individual build script in the correct
# dependency order. Each script is self-contained and produces a
# finished, CocoaPods-compatible .xcframework.
#
#############################################################################
set -e

SCRIPTDIR=$(cd "$(dirname "$0")" && pwd)
readonly SCRIPTDIR

logMsg() {
    echo "================================================================="
    printf "🚀 [MASTER BUILD] %s\n" "$1"
    echo "================================================================="
}

# --- Build Execution ---

logMsg "Starting full build of all mathematical libraries..."

logMsg "Building GMP..."
"$SCRIPTDIR/build_gmp.sh"

logMsg "Building MPFR (depends on GMP)..."
"$SCRIPTDIR/build_mpfr.sh"

logMsg "Building MPC (depends on GMP, MPFR)..."
"$SCRIPTDIR/build_mpc.sh"

logMsg "Building FLINT (depends on GMP, MPFR)..."
"$SCRIPTDIR/build_flint.sh"

logMsg "Building SymEngine + Flutter Wrapper (depends on all others)..."
"$SCRIPTDIR/build_symengine.sh"

logMsg "✅ All libraries built successfully!"
logMsg "The following XCFrameworks have been created and are ready for use:"
ls -d ./*.xcframework

exit 0
