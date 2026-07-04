#!/bin/bash
# Build a FULL-capability SymEngine WASM: INTEGER_CLASS=gmp with FLINT + MPFR
# + MPC, linking the WASM-compiled math stack in build-wasm-deps/sysroot.
# Produces symengine.js + symengine.wasm with the real number-theory /
# precision / Bessel / FLINT-factor functions (no longer stubbed).
set -e
export EMSDK_OS=macos
source /Users/christianstrobele/code/emsdk/emsdk_env.sh >/dev/null 2>&1
ROOT=/Users/christianstrobele/code/math-stack-ios-builder
WASM=$ROOT/build-wasm-deps
PREFIX=$WASM/sysroot
SYMSRC=$ROOT/symengine-0.11.2
NCPU=$(sysctl -n hw.ncpu)
cd "$ROOT"

# --- MPC -> WASM (depends on GMP + MPFR, both already built) -------------
if [ ! -f "$PREFIX/lib/libmpc.a" ]; then
  echo "=== MPC -> WASM ==="
  rm -rf "$WASM/mpc-1.3.1"; tar xf "$ROOT/mpc-1.3.1.tar.gz" -C "$WASM"
  cd "$WASM/mpc-1.3.1"
  emconfigure ./configure --host=none --disable-shared --enable-static \
    --with-gmp="$PREFIX" --with-mpfr="$PREFIX" --prefix="$PREFIX" \
    CC_FOR_BUILD=/usr/bin/cc CFLAGS="-O2" >/tmp/mpc_conf.log 2>&1 \
    || { echo "MPC CONFIGURE FAILED"; tail -20 /tmp/mpc_conf.log; exit 1; }
  emmake make -j"$NCPU" >/tmp/mpc_make.log 2>&1 \
    || { echo "MPC MAKE FAILED"; tail -25 /tmp/mpc_make.log; exit 1; }
  emmake make install >/tmp/mpc_install.log 2>&1
  echo "MPC OK"
  cd "$ROOT"
fi

# --- SymEngine -> WASM (INTEGER_CLASS=gmp, WITH_FLINT/MPFR/MPC) ----------
SYMBUILD=$WASM/symengine-build
if [ -f "$SYMBUILD/symengine/libsymengine.a" ] && [ -z "${FORCE_SYM:-}" ]; then
  echo "=== SymEngine WASM cached (set FORCE_SYM=1 to rebuild) ==="
else
echo "=== SymEngine -> WASM (gmp+flint+mpfr+mpc) ==="
rm -rf "$SYMBUILD"; mkdir -p "$SYMBUILD"; cd "$SYMBUILD"
emcmake cmake "$SYMSRC" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DINTEGER_CLASS=gmp \
  -DWITH_GMP=ON -DWITH_MPFR=ON -DWITH_MPC=ON -DWITH_FLINT=ON \
  -DWITH_LLVM=OFF -DWITH_ARB=OFF -DWITH_ECM=OFF -DWITH_PRIMESIEVE=OFF \
  -DWITH_PTHREAD=OFF -DWITH_SYMENGINE_THREAD_SAFE=OFF \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF \
  -DGMP_INCLUDE_DIR="$PREFIX/include" -DGMP_LIBRARY="$PREFIX/lib/libgmp.a" \
  -DMPFR_INCLUDE_DIR="$PREFIX/include" -DMPFR_LIBRARY="$PREFIX/lib/libmpfr.a" \
  -DMPC_INCLUDE_DIR="$PREFIX/include" -DMPC_LIBRARY="$PREFIX/lib/libmpc.a" \
  -DFLINT_INCLUDE_DIR="$PREFIX/include" -DFLINT_LIBRARY="$PREFIX/lib/libflint.a" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/tmp/sym_cmake.log 2>&1 \
  || { echo "SYMENGINE CMAKE FAILED"; tail -30 /tmp/sym_cmake.log; exit 1; }
emmake make -j"$NCPU" symengine >/tmp/sym_make.log 2>&1 \
  || { echo "SYMENGINE MAKE FAILED"; tail -40 /tmp/sym_make.log; exit 1; }
fi
SYMLIB=$(find "$SYMBUILD" -name libsymengine.a | head -1)
echo "SymEngine OK: $SYMLIB"

# --- Wrapper + cas.cpp -> link -> symengine.js/.wasm --------------------
echo "=== link wrapper -> symengine.js ==="
cd "$ROOT"
INCS="-I$SYMSRC -I$SYMBUILD -I$PREFIX/include"
emcc -O2 $INCS -DWASM_WITH_FLINT -sNO_DISABLE_EXCEPTION_CATCHING -c src/flutter_symengine_wrapper.c -o "$WASM/wrap.o" \
  >/tmp/wrap_c.log 2>&1 || { echo "WRAP C FAILED"; tail -25 /tmp/wrap_c.log; exit 1; }
em++ -O2 -std=c++14 $INCS -DWASM_WITH_FLINT -sNO_DISABLE_EXCEPTION_CATCHING -c src/flutter_symengine_cas.cpp -o "$WASM/cas.o" \
  >/tmp/wrap_cpp.log 2>&1 || { echo "WRAP CPP FAILED"; tail -25 /tmp/wrap_cpp.log; exit 1; }

OUTDIR=$ROOT/build-wasm-out
mkdir -p "$OUTDIR"
em++ -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME="SymEngineModule" \
  -s EXPORTED_FUNCTIONS="@$ROOT/wasm_exports.json" \
  -s 'EXPORTED_RUNTIME_METHODS=["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
  -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=33554432 -s MAXIMUM_MEMORY=536870912 \
  -s NO_EXIT_RUNTIME=1 -s ENVIRONMENT=web -sNO_DISABLE_EXCEPTION_CATCHING \
  "$WASM/wrap.o" "$WASM/cas.o" "$SYMLIB" \
  "$PREFIX/lib/libflint.a" "$PREFIX/lib/libmpc.a" \
  "$PREFIX/lib/libmpfr.a" "$PREFIX/lib/libgmp.a" \
  -o "$OUTDIR/symengine.js" >/tmp/link.log 2>&1 \
  || { echo "LINK FAILED"; tail -40 /tmp/link.log; exit 1; }

echo "=== BUILT ==="
ls -la "$OUTDIR/symengine.js" "$OUTDIR/symengine.wasm"
