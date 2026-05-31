#!/bin/bash
############################################################################
#
# build_wasm_deps.sh
#
# Build the GMP + MPFR + MPC + FLINT math stack to WebAssembly (static libs)
# for the FULL-capability SymEngine WASM build (build_wasm_flint.sh). This
# is the path the original boostmp WASM plan deliberately avoided; it works
# but GMP must use the generic-C path (--disable-assembly) and a NATIVE
# build compiler (CC_FOR_BUILD=/usr/bin/cc — the toolchain clang lacks the
# SDK sysroot and fails GMP's build-system-compiler probe).
#
# Requires emsdk on PATH. Bootstrap it with:
#   git clone https://github.com/emscripten-core/emsdk && cd emsdk
#   EMSDK_OS=macos ./emsdk install latest && EMSDK_OS=macos ./emsdk activate latest
# (EMSDK_OS=macos works around emsdk mis-detecting recent macOS where
# platform.mac_ver() returns empty.)
#
# Output: build-wasm-deps/sysroot/lib/{libgmp,libmpfr,libmpc,libflint}.a
############################################################################
set -e
: "${EMSDK_OS:=macos}"; export EMSDK_OS
: "${EMSDK:?source emsdk_env.sh first (emcc must be on PATH)}"

ROOT=$(cd "$(dirname "$0")" && pwd)
WASM="$ROOT/build-wasm-deps"
PREFIX="$WASM/sysroot"
NCPU=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu)
BUILD_CC=/usr/bin/cc   # native build-system compiler (has the SDK sysroot)
mkdir -p "$WASM"

log() { printf "[WASM DEPS] %s\n" "$1"; }

build_gmp() {
  [ -f "$PREFIX/lib/libgmp.a" ] && { log "GMP cached"; return; }
  log "GMP -> WASM"
  rm -rf "$WASM/gmp-6.3.0"; tar xf "$ROOT/gmp-6.3.0.tar.bz2" -C "$WASM"
  cd "$WASM/gmp-6.3.0"
  emconfigure ./configure --host=none --disable-assembly --enable-static \
    --disable-shared --enable-cxx=no --prefix="$PREFIX" \
    CFLAGS="-O2" CC_FOR_BUILD="$BUILD_CC"
  emmake make -j"$NCPU"; emmake make install
}

build_mpfr() {
  [ -f "$PREFIX/lib/libmpfr.a" ] && { log "MPFR cached"; return; }
  log "MPFR -> WASM"
  rm -rf "$WASM/mpfr-4.2.2"; tar xf "$ROOT/mpfr-4.2.2.tar.xz" -C "$WASM"
  cd "$WASM/mpfr-4.2.2"
  emconfigure ./configure --host=none --disable-shared --enable-static \
    --with-gmp="$PREFIX" --prefix="$PREFIX" CC_FOR_BUILD="$BUILD_CC" CFLAGS="-O2"
  emmake make -j"$NCPU"; emmake make install
}

build_mpc() {
  [ -f "$PREFIX/lib/libmpc.a" ] && { log "MPC cached"; return; }
  log "MPC -> WASM"
  rm -rf "$WASM/mpc-1.3.1"; tar xf "$ROOT/mpc-1.3.1.tar.gz" -C "$WASM"
  cd "$WASM/mpc-1.3.1"
  emconfigure ./configure --host=none --disable-shared --enable-static \
    --with-gmp="$PREFIX" --with-mpfr="$PREFIX" --prefix="$PREFIX" \
    CC_FOR_BUILD="$BUILD_CC" CFLAGS="-O2"
  emmake make -j"$NCPU"; emmake make install
}

build_flint() {
  [ -f "$PREFIX/lib/libflint.a" ] && { log "FLINT cached"; return; }
  log "FLINT -> WASM (slow)"
  rm -rf "$WASM/flint-3.3.1"; tar xf "$ROOT/flint-3.3.1.tar.gz" -C "$WASM"
  cd "$WASM/flint-3.3.1"
  emconfigure ./configure --host=none --disable-shared --enable-static \
    --disable-assembly --disable-thread-safe \
    --with-gmp="$PREFIX" --with-mpfr="$PREFIX" --prefix="$PREFIX" \
    CC_FOR_BUILD="$BUILD_CC" CFLAGS="-O2"
  emmake make -j"$NCPU"; emmake make install
}

build_gmp
build_mpfr
build_mpc
build_flint
log "Done."
ls -la "$PREFIX/lib/"*.a
