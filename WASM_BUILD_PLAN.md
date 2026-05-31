# SymEngine → WebAssembly Build Plan

**Opportunity #2: Close the web cliff — SymEngine → WASM**

Status: validated plan, not yet executed.
Branch: `feature/wasm-emscripten` (math-stack-ios-builder),
        `feature/wasm-web-impl` (symbolic_math_bridge).
Date: 2026-05-31.

---

## 0. Current state (verified)

| Fact | Detail |
|------|--------|
| Web CAS | **Zero.** `symbolic_math_bridge_web.dart` is a throw-only stub. `calculator_engine.dart:76-79` routes web through `NumericFallbackEvaluator` (scalar only). |
| Native stack | SymEngine 0.11.2, GMP 6.3.0, MPFR 4.2.2, MPC 1.3.1, FLINT 3.3.1, `INTEGER_CLASS=flint`. All 5 native platforms shipped. |
| Wrapper surface | 55 exported C functions in `flutter_symengine_wrapper.c` (core CAS, trig/hyp/exp/log/sqrt/gamma, ntheory, MPFR precision, Bessel, matrices). |
| emcc | NOT installed. cmake 4.1.1, ninja, node v25.8.2 present. |
| Web index.html | Stock Flutter scaffolding. `<script src="flutter_bootstrap.js" async>`. |
| Conditional export | `symbolic_math_bridge.dart` uses `dart.library.io` to switch between `_io.dart` (FFI) and `_web.dart` (stub). |

---

## 1. INTEGER_CLASS decision: boostmp

**Choice: `INTEGER_CLASS=boostmp` for the WASM slice.**

### Why not GMP under Emscripten?

GMP uses heavily hand-tuned assembly (x86_64 / ARM) for its inner loops.
Emscripten targets WASM (a stack machine), so those assembly files must be
either disabled or rewritten — GMP's `configure` detects "unknown host" and
falls back to generic C, but this path is fragile, rarely tested, and the
resulting GMP is ~5-10x slower than native. Every Emscripten-GMP port on
GitHub is either stale, patched, or abandoned. It's a maintenance trap.

### Why boostmp works

`Boost.Multiprecision` is **header-only C++** (no assembly, no configure,
no autotools). Drop the Boost headers into the include path and set
`-DINTEGER_CLASS=boostmp`. SymEngine's CMake already supports this
(`CMakeLists.txt:171-173`).

### Capability delta vs native (`INTEGER_CLASS=flint`)

| Capability | Native (flint) | WASM (boostmp) | Impact |
|------------|---------------|----------------|--------|
| `evaluate`, `expand`, `differentiate`, `substitute`, `solve` | full | **full** | none |
| Unary math (sin, cos, … gamma) | full | **full** | none |
| `simplify`, `factor` | aliased to `expand` (fake — Opp #1) | same | none |
| `integrate` | hard-error stub | same | none |
| `gcd`, `lcm`, `factorial`, `fibonacci` | GMP ntheory | **Boost ntheory** | functionally equivalent |
| `isprime`, `nextprime`, `prevprime` | GMP `mpz_probab_prime_p` | **not available** | ntheory primality gone on web |
| `factorint` | FLINT `fmpz_factor` | **not available** | integer factoring gone on web |
| `modpow`, `modinv`, `totient`, `jacobi` | GMP / FLINT | **not available** | modular arithmetic gone |
| `pi(N)`, `e(N)`, `evalf(expr, N)`, `cevalf(expr, N)` | MPFR / MPC | **not available** | arbitrary-precision gone |
| `besselj`, `bessely` | MPFR direct | **not available** | Bessel gone |
| Matrix ops (`det`, `inv`, `+`, `*`) | full | **full** | none |

**Key constraint**: SymEngine's CMake enforces `INTEGER_CLASS=boostmp
cannot be used with FLINT, ARB or MPFR` (line 262-264). So the WASM
build gets the symbolic engine (the core CAS value) but loses the
precision arc and number-theory primitives that depend on GMP/MPFR/FLINT.

**This is the right trade-off.** The features lost are exactly the ones
the pure-Dart `NumericFallbackEvaluator` already cannot do. What we gain
is the entire symbolic manipulation layer: `expand`, `differentiate`,
`solve`, `substitute`, trig/hyp identities, `gcd`/`lcm`/`factorial`/
`fibonacci`, and matrix operations — all of which currently return
"requires native library" on web.

---

## 2. Emsdk bootstrap

```bash
# Install on /mnt/storage (large, persistent) — emsdk is ~1.5 GB
cd /mnt/storage
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest    # downloads LLVM-to-WASM, ~1 GB
./emsdk activate latest
source ./emsdk_env.sh     # sets PATH, EMSDK, etc.

# Verify
emcc --version            # should report emscripten 4.x
```

Persist activation in session via `. /mnt/storage/emsdk/emsdk_env.sh`.

---

## 3. Boost headers

Boost.Multiprecision is header-only. Only the headers are needed, not a
compiled Boost install.

```bash
cd /mnt/storage
# Boost 1.87 (latest stable as of May 2026)
wget https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz
tar xzf boost_1_87_0.tar.gz
# Only need the headers: boost_1_87_0/boost/
export BOOST_ROOT=/mnt/storage/boost_1_87_0
```

---

## 4. Emscripten build of SymEngine + wrapper

### 4a. Build SymEngine as a static WASM library

```bash
cd /mnt/akademie_storage/math-stack-ios-builder-wasm

# Create a WASM-specific build directory
mkdir -p build-wasm && cd build-wasm

emcmake cmake ../symengine-0.11.2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DINTEGER_CLASS=boostmp \
  -DWITH_MPFR=OFF \
  -DWITH_MPC=OFF \
  -DWITH_FLINT=OFF \
  -DWITH_ARB=OFF \
  -DWITH_ECM=OFF \
  -DWITH_PRIMESIEVE=OFF \
  -DWITH_LLVM=OFF \
  -DWITH_PTHREAD=OFF \
  -DWITH_COTIRE=OFF \
  -DWITH_SYMENGINE_THREAD_SAFE=OFF \
  -DBUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBoost_INCLUDE_DIR=$BOOST_ROOT \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

emmake make -j$(nproc)
# Produces: build-wasm/symengine/libsymengine.a (WASM bitcode archive)
```

**Expected issues and mitigations:**

1. **`basic_parse` uses `<cstdio>` / `<iostream>`** — Emscripten provides
   these; should work out of the box.
2. **`cwrapper.cpp` C++ name mangling** — The wrapper's `extern "C"` block
   already handles this.
3. **No threading** — `WITH_PTHREAD=OFF` avoids SharedArrayBuffer /
   COOP/COEP requirements.

### 4b. Compile the Flutter wrapper to WASM

The C wrapper (`src/flutter_symengine_wrapper.c`) must be compiled against
the boostmp build, with the GMP/MPFR/FLINT-dependent functions stubbed out.

**Strategy: create a `flutter_symengine_wrapper_wasm.c`** that:
- `#include`s the original wrapper for the SymEngine-only functions
- Stubs out all GMP-direct, MPFR-direct, and FLINT-direct functions with
  error returns
- Adds `EMSCRIPTEN_KEEPALIVE` to every exported function

Alternatively (cleaner): add `#ifdef __EMSCRIPTEN__` guards to the
existing `flutter_symengine_wrapper.c` around the GMP/MPFR/FLINT blocks
and add `EMSCRIPTEN_KEEPALIVE` attributes. This keeps one source file.

**Recommended approach: `#ifdef __EMSCRIPTEN__` in the existing wrapper.**

```c
// At the top of flutter_symengine_wrapper.c, add:
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

// Then prefix every exported function:
// char* flutter_symengine_evaluate(...) →
// WASM_EXPORT char* flutter_symengine_evaluate(...)

// Guard GMP-direct sections:
#ifndef __EMSCRIPTEN__
#include <gmp.h>
// ... isprime, prevprime, modpow, modinv, jacobi ...
#else
char* flutter_symengine_isprime(const char* n) {
    return create_error_string("isprime", "not available in web build");
}
// ... etc for all GMP/MPFR/FLINT-only functions ...
#endif

// Guard MPFR sections similarly:
#ifndef __EMSCRIPTEN__
#include <mpfr.h>
// ... bessel_eval, pi_with_precision, evalf_with_precision, cevalf ...
#else
// ... stubs returning error strings ...
#endif

// Guard FLINT sections:
#ifndef __EMSCRIPTEN__
#include <flint/fmpz.h>
#include <flint/fmpz_factor.h>
// ... factorint, totient ...
#else
// ... stubs ...
#endif
```

### 4c. Link into a .wasm module

```bash
cd /mnt/akademie_storage/math-stack-ios-builder-wasm

emcc \
  -O2 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="SymEngineModule" \
  -s EXPORTED_FUNCTIONS="@wasm_exports.json" \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","_malloc","_free"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s MAXIMUM_MEMORY=268435456 \
  -s NO_EXIT_RUNTIME=1 \
  -I symengine-0.11.2 \
  -I build-wasm \
  -I $BOOST_ROOT \
  src/flutter_symengine_wrapper.c \
  build-wasm/symengine/libsymengine.a \
  -lembind \
  -o symengine.js
# Produces: symengine.js (loader/glue) + symengine.wasm
```

### 4d. Exported functions list (`wasm_exports.json`)

```json
[
  "_flutter_symengine_evaluate",
  "_flutter_symengine_solve",
  "_flutter_symengine_expand",
  "_flutter_symengine_factor",
  "_flutter_symengine_differentiate",
  "_flutter_symengine_integrate",
  "_flutter_symengine_simplify",
  "_flutter_symengine_substitute",
  "_flutter_symengine_free_string",
  "_flutter_symengine_abs",
  "_flutter_symengine_sin",
  "_flutter_symengine_cos",
  "_flutter_symengine_tan",
  "_flutter_symengine_asin",
  "_flutter_symengine_acos",
  "_flutter_symengine_atan",
  "_flutter_symengine_sinh",
  "_flutter_symengine_cosh",
  "_flutter_symengine_tanh",
  "_flutter_symengine_asinh",
  "_flutter_symengine_acosh",
  "_flutter_symengine_atanh",
  "_flutter_symengine_exp",
  "_flutter_symengine_log",
  "_flutter_symengine_sqrt",
  "_flutter_symengine_gamma",
  "_flutter_symengine_gcd",
  "_flutter_symengine_lcm",
  "_flutter_symengine_factorial",
  "_flutter_symengine_fibonacci",
  "_flutter_symengine_get_pi",
  "_flutter_symengine_get_e",
  "_flutter_symengine_get_euler_gamma",
  "_flutter_symengine_isprime",
  "_flutter_symengine_nextprime",
  "_flutter_symengine_prevprime",
  "_flutter_symengine_factorint",
  "_flutter_symengine_modpow",
  "_flutter_symengine_modinv",
  "_flutter_symengine_totient",
  "_flutter_symengine_jacobi",
  "_flutter_symengine_pi_with_precision",
  "_flutter_symengine_e_with_precision",
  "_flutter_symengine_euler_gamma_with_precision",
  "_flutter_symengine_sqrt2_with_precision",
  "_flutter_symengine_evalf_with_precision",
  "_flutter_symengine_cevalf_with_precision",
  "_flutter_symengine_besselj",
  "_flutter_symengine_bessely",
  "_flutter_symengine_matrix_new",
  "_flutter_symengine_matrix_free",
  "_flutter_symengine_matrix_set_element",
  "_flutter_symengine_matrix_get_element",
  "_flutter_symengine_matrix_to_string",
  "_flutter_symengine_matrix_det",
  "_flutter_symengine_matrix_inv",
  "_flutter_symengine_matrix_add",
  "_flutter_symengine_matrix_mul",
  "_flutter_symengine_version",
  "_flutter_symengine_test_basic_operations",
  "_flutter_symengine_test_symbolic",
  "_malloc",
  "_free"
]
```

All 55 C functions exported (stubbed ones return error strings on WASM).
The `_malloc`/`_free` exports are needed for the Dart side to allocate
strings into WASM memory.

### 4e. Expected WASM size

SymEngine with `boostmp`, no MPFR/FLINT/GMP: the C++ template
instantiations for the symbolic engine + Boost multiprecision headers
will produce roughly **3-6 MB uncompressed** `.wasm`. With gzip/brotli
(standard for web hosting), expect **~1-2 MB transferred**. Acceptable
for a load-on-first-CAS-call pattern.

---

## 5. Dart `js_interop` web implementation (symbolic_math_bridge)

Replace the throw stub `symbolic_math_bridge_web.dart` with a real
`dart:js_interop` implementation that calls into the WASM module.

### 5a. New file: `lib/src/symbolic_math_bridge_wasm.dart`

This file mirrors `symbolic_math_bridge_io.dart`'s public API but calls
into JS `ccall`/`cwrap` from the Emscripten glue.

```dart
// WASM (dart:js_interop) implementation of the symbolic-math bridge.
// Selected by the conditional export in `symbolic_math_bridge.dart`
// when `dart.library.io` is absent AND the WASM module has loaded.

import 'dart:js_interop';

import 'symbolic_math_exceptions.dart';

/// JS interop binding to the SymEngineModule Emscripten module.
@JS('SymEngineModule')
external JSObject? get _symEngineModuleRaw;

/// Whether the WASM module has finished loading.
bool get isWasmLoaded => _symEngineModuleRaw != null;

/// Call a WASM function via Module.ccall(name, returnType, argTypes, args).
String _ccallString(String funcName, List<String> argTypes, List<Object> args) {
  final module = _symEngineModuleRaw;
  if (module == null) {
    throw SymbolicMathNotAvailableException('WASM module not loaded');
  }
  // ccall returns a JS string for 'string' return type
  final result = module.callMethod(
    'ccall'.toJS,
    funcName.toJS,
    'string'.toJS,
    argTypes.map((t) => t.toJS).toList().toJS,
    args.map((a) => a is int ? a.toJS : (a as String).toJS).toList().toJS,
  );
  return (result as JSString).toDart;
}

String _callUnaryString(String funcName, String input) {
  final result = _ccallString(funcName, ['string'], [input]);
  if (result.startsWith('Error')) {
    throw SymbolicMathException(funcName, result);
  }
  return result;
}

String _callBinaryString(String funcName, String a, String b) {
  final result = _ccallString(funcName, ['string', 'string'], [a, b]);
  if (result.startsWith('Error')) {
    throw SymbolicMathException(funcName, result);
  }
  return result;
}

String _callTernaryString(String funcName, String a, String b, String c) {
  final result = _ccallString(funcName, ['string', 'string', 'string'], [a, b, c]);
  if (result.startsWith('Error')) {
    throw SymbolicMathException(funcName, result);
  }
  return result;
}

// ... (SymEngineMatrix and SymbolicMathBridge classes follow,
//      mirroring the _io.dart API surface)
```

The full implementation wraps every method from the web stub's API
surface using `ccall`. Memory management for strings is handled by
Emscripten's `ccall` with `'string'` return type (it calls
`UTF8ToString` and `_free` internally when the return type is `string`).

### 5b. Conditional export update

Update `lib/symbolic_math_bridge.dart`:

```dart
export 'src/symbolic_math_exceptions.dart';
export 'src/symbolic_math_bridge_web.dart'  // throw stub (pre-WASM-load)
    if (dart.library.io) 'src/symbolic_math_bridge_io.dart';

// Note: The web.dart file itself will be updated to check isWasmLoaded
// and delegate to the WASM implementation when available, rather than
// always throwing.
```

**Alternative (cleaner):** Make `symbolic_math_bridge_web.dart` the
conditional-import target that internally switches between "throw stub"
(WASM not loaded) and "WASM bridge" (WASM loaded). This avoids changing
the conditional export and keeps the `dart.library.io` gate as-is.

### 5c. Two-phase loading pattern

```dart
class SymbolicMathBridge {
  static bool _wasmLoaded = false;
  static bool _wasmLoading = false;

  SymbolicMathBridge() {
    if (!_wasmLoaded) {
      // Pre-WASM: constructor throws, same as today.
      // CalculatorEngine catches this and sets _nativeAvailable = false.
      // After WASM loads, the engine re-initializes.
      throw SymbolicMathNotAvailableException('SymEngine (web build)');
    }
    // WASM loaded: all methods delegate to ccall.
  }

  /// Call this from CrispCalc's main() to begin async WASM loading.
  /// When complete, fire a callback so the engine can re-try init.
  static Future<bool> loadWasm() async {
    if (_wasmLoaded) return true;
    if (_wasmLoading) return false;
    _wasmLoading = true;
    // The <script> tag in index.html loads symengine.js which defines
    // SymEngineModule as a factory. Call it to instantiate + fetch .wasm.
    // ... JS interop to call SymEngineModule() and await its promise ...
    _wasmLoaded = true;
    _wasmLoading = false;
    return true;
  }
}
```

### 5d. Functions to implement (priority order)

**Phase 1 — Core CAS (highest user-visible impact):**
- `evaluate(expression)` → `flutter_symengine_evaluate`
- `expand(expression)` → `flutter_symengine_expand`
- `differentiate(expression, symbol)` → `flutter_symengine_differentiate`
- `substitute(expression, symbol, value)` → `flutter_symengine_substitute`
- `solve(expression, symbol)` → `flutter_symengine_solve`
- `callUnary(funcName, expression)` → `flutter_symengine_{funcName}`
  (sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, asinh, acosh,
  atanh, exp, log, sqrt, gamma, abs)

**Phase 2 — Number theory + constants:**
- `gcd`, `lcm`, `factorial`, `fibonacci` (work with boostmp)
- `getPi`, `getE`, `getEulerGamma` (symbolic constants, work)
- `simplify`, `factor` (aliased to expand, but still callable)
- `integrate` (hard-error stub, but callable)

**Phase 3 — Stubs that return clean errors:**
- `isprime`, `nextprime`, `prevprime` → "not available in web build"
- `factorint`, `modpow`, `modinv`, `totient`, `jacobi` → same
- `mpfrHighPrecisionPi`, `mpfrEvalf`, `mpfrCevalf` → same
- `mpfrBesselJ`, `mpfrBesselY` → same
- `evaluateWithPrecision`, `gmpPower` → same

**Phase 4 — Matrix ops:**
- `createMatrix`, `set`, `get`, `det`, `inv`, `add`, `mul`
- Matrices use opaque pointers — WASM interop will pass `int` handles
  (pointer-as-number) through `ccall` with `'number'` type.

---

## 6. WASM asset hosting in CrispCalc

### 6a. Ship the artifacts

Copy from math-stack-ios-builder's build output:
```
CrispCalc/web/symengine.js     ← Emscripten glue/loader
CrispCalc/web/symengine.wasm   ← the WASM binary
```

### 6b. Update `web/index.html`

```html
<body>
  <!-- Load SymEngine WASM module before Flutter boots -->
  <script>
    // SymEngineModule is defined by the Emscripten-generated symengine.js.
    // We load it eagerly so it's ready by the time Dart code initializes
    // SymbolicMathBridge. The module factory returns a promise that
    // resolves when the .wasm is compiled and ready.
    var symEngineReady = false;
    var symEngineInstance = null;
  </script>
  <script src="symengine.js"></script>
  <script>
    SymEngineModule().then(function(instance) {
      symEngineInstance = instance;
      // Rebind global so Dart's @JS('SymEngineModule') finds the instance
      window.SymEngineModule = instance;
      symEngineReady = true;
      console.log('SymEngine WASM loaded');
    }).catch(function(err) {
      console.warn('SymEngine WASM failed to load:', err);
    });
  </script>
  <script src="flutter_bootstrap.js" async></script>
</body>
```

### 6c. Loading UX

On app start, `CalculatorEngine` constructor tries `SymbolicMathBridge()`.
If WASM hasn't loaded yet, it throws → `_nativeAvailable = false` →
NumericFallbackEvaluator handles scalar math.

After WASM loads (the JS promise resolves), fire a Dart callback that:
1. Re-instantiates `SymbolicMathBridge()` (now succeeds)
2. Sets `_nativeAvailable = true`
3. Any pending/future CAS calls now route through WASM

The user sees: calculator works immediately (numeric fallback), then
CAS features silently become available (typically within 1-3 seconds of
page load, depending on `.wasm` download + compile time).

---

## 7. CrispCalc integration

### 7a. `isNativeAvailable` flip

Currently `calculator_engine.dart:19-29` does a sync try/catch on
`SymbolicMathBridge()`. For WASM, the bridge exposes a static
`loadWasm()` future. Wire it:

```dart
CalculatorEngine() {
  try {
    _bridge = SymbolicMathBridge();
    _nativeAvailable = true;
  } catch (e) {
    _bridge = null;
    _nativeAvailable = false;
    // On web: start async WASM load, re-init on completion
    if (kIsWeb) {
      SymbolicMathBridge.loadWasm().then((loaded) {
        if (loaded) {
          _bridge = SymbolicMathBridge();
          _nativeAvailable = true;
          _log('SymEngine WASM loaded — CAS available');
          // Notify UI to refresh if needed
        }
      });
    }
  }
}
```

### 7b. NumericFallback as pre-load path

The existing `NumericFallbackEvaluator` remains the synchronous pre-load
path. Before WASM is ready, all numeric expressions still work via Dart.
After WASM loads, `_nativeAvailable` flips true and CAS calls route
through the WASM bridge. No behavior change for numeric-only expressions.

### 7c. Bumping the bridge ref in pubspec

After the WASM web impl lands on `symbolic_math_bridge`'s
`feature/wasm-web-impl` branch and is merged to main:

```yaml
# CrispCalc/pubspec.yaml
symbolic_math_bridge:
  git:
    url: https://github.com/CrispStrobe/symbolic_math_bridge.git
    ref: "<new-main-HEAD-sha>"  # bridge with WASM web impl
```

---

## 8. CI considerations

### GitHub Actions workflow: `build-wasm.yml`

```yaml
name: Build SymEngine WASM
on:
  push:
    branches: [feature/wasm-emscripten, main]
  workflow_dispatch:

jobs:
  build-wasm:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: latest
      - name: Install Boost headers
        run: |
          wget -q https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz
          tar xzf boost_1_87_0.tar.gz
      - name: Build SymEngine (boostmp, WASM)
        run: |
          mkdir build-wasm && cd build-wasm
          emcmake cmake ../symengine-0.11.2 \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DINTEGER_CLASS=boostmp \
            -DWITH_MPFR=OFF -DWITH_MPC=OFF -DWITH_FLINT=OFF \
            -DWITH_ARB=OFF -DWITH_ECM=OFF -DWITH_PRIMESIEVE=OFF \
            -DWITH_LLVM=OFF -DWITH_PTHREAD=OFF -DWITH_COTIRE=OFF \
            -DWITH_SYMENGINE_THREAD_SAFE=OFF \
            -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF \
            -DBoost_INCLUDE_DIR=$GITHUB_WORKSPACE/boost_1_87_0
          emmake make -j$(nproc)
      - name: Compile wrapper + link WASM module
        run: |
          emcc -O2 -s WASM=1 -s MODULARIZE=1 \
            -s EXPORT_NAME="SymEngineModule" \
            -s EXPORTED_FUNCTIONS="@wasm_exports.json" \
            -s 'EXPORTED_RUNTIME_METHODS=["ccall","cwrap","UTF8ToString","stringToUTF8","_malloc","_free"]' \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s INITIAL_MEMORY=16MB -s MAXIMUM_MEMORY=256MB \
            -s NO_EXIT_RUNTIME=1 \
            -I symengine-0.11.2 -I build-wasm -I boost_1_87_0 \
            src/flutter_symengine_wrapper.c \
            build-wasm/symengine/libsymengine.a \
            -o symengine.js
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: symengine-wasm
          path: |
            symengine.js
            symengine.wasm
```

### Cache the build

SymEngine + Boost compile from scratch takes ~5-10 min on a GHA runner.
Cache `build-wasm/` via `actions/cache` keyed on the SymEngine version +
wrapper source hash.

---

## 9. Coordination with pure-Dart web CAS branch

The user is building a pragmatic pure-Dart web CAS on
`feature/numeric-fallback-and-symbolic-survey` in CrispCalc
(`lib/engine/symbolic_web.dart` — real expand/diff/linear+quad solve).

**Supersession path:**
1. The pure-Dart CAS ships first → web gets basic symbolic ops immediately.
2. When WASM lands, `SymbolicMathBridge` on web resolves via WASM instead
   of throwing → the `_nativeAvailable` flag flips → all CAS calls bypass
   both `NumericFallbackEvaluator` and the pure-Dart symbolic layer,
   routing through the full SymEngine.
3. The pure-Dart symbolic layer becomes dead code on web (can be removed
   or kept as a synchronous-before-WASM-load fallback).

**No conflict**: this plan touches only `math-stack-ios-builder` (build
scripts, wrapper `#ifdef`s) and `symbolic_math_bridge` (web impl Dart
file). It does not edit `CrispCalc/lib/engine/`.

---

## 10. Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| SymEngine C++ doesn't compile cleanly under Emscripten | Medium | `boostmp` eliminates the hard dep chain; most issues will be in cwrapper's `<iostream>` usage. Patch locally if needed. |
| WASM blob > 10 MB | Low-Medium | `-O2` + `--closure 1` on the JS glue. If still large, `-Os` or strip debug. Brotli gets it to ~1/3. |
| `ccall` string marshalling perf | Low | Emscripten's ccall with `'string'` type handles malloc/free internally. For hot paths, can switch to manual `_malloc` + `stringToUTF8` + `_free`. |
| Matrix opaque pointers across JS/WASM boundary | Medium | WASM pointers are `i32` indices. Pass as `'number'` type in ccall. The Dart side holds the int handle and passes it back on each matrix call. |
| SharedArrayBuffer / COOP/COEP for threading | N/A | Threading is OFF (`WITH_PTHREAD=OFF`). No SAB required. |
| Boost.Multiprecision precision loss vs GMP | Low | For CAS (symbolic manipulation), precision of the integer class barely matters — it's exact rational arithmetic. Only affects `evalf` paths, which are already stubbed out. |
| `basic_str` / `basic_parse` depend on locale | Low | Emscripten's C locale is "C" by default; no locale-dependent surprises. |

---

## 11. Execution sequence

1. **Bootstrap emsdk** on this machine (§2). ~15 min.
2. **Download Boost headers** (§3). ~2 min.
3. **Add `#ifdef __EMSCRIPTEN__` guards** to `flutter_symengine_wrapper.c`
   (§4b). Create `wasm_exports.json` (§4d). ~30 min.
4. **Build SymEngine** with `emcmake` (§4a). ~10-15 min.
5. **Link WASM module** with `emcc` (§4c). ~2 min.
6. **Validate** — run a smoke test via Node.js:
   ```bash
   node -e "
     const factory = require('./symengine.js');
     factory().then(m => {
       console.log('version:', m.ccall('flutter_symengine_version','string',[],[]));
       console.log('2+3:', m.ccall('flutter_symengine_evaluate','string',['string'],['2+3']));
       console.log('diff x^3:', m.ccall('flutter_symengine_differentiate','string',['string','string'],['x**3','x']));
       console.log('solve x^2-4:', m.ccall('flutter_symengine_solve','string',['string','string'],['x**2-4','x']));
     });
   "
   ```
7. **Write `symbolic_math_bridge_wasm.dart`** (§5). ~1-2 hours.
8. **Update `symbolic_math_bridge_web.dart`** to delegate to WASM when
   loaded (§5b-5c). ~30 min.
9. **Copy WASM artifacts** to CrispCalc web/ and update index.html (§6).
10. **Wire async WASM loading** in CalculatorEngine (§7a). ~30 min.
11. **Test end-to-end** with `flutter run -d chrome`. ~30 min.
12. **Add CI workflow** (§8). ~30 min.
13. **Bump bridge ref** in CrispCalc pubspec (§7c).

---

## 12. Validation criteria (done = all green)

- [ ] `emcc --version` works on this machine
- [ ] `emcmake cmake` configures SymEngine with `INTEGER_CLASS=boostmp` without error
- [ ] `emmake make` produces `libsymengine.a` (WASM bitcode)
- [ ] `emcc` links `symengine.js` + `symengine.wasm` without unresolved symbols
- [ ] Node.js smoke test: `evaluate("2+3")` → `"5"`
- [ ] Node.js smoke test: `differentiate("x**3", "x")` → `"3*x**2"`
- [ ] Node.js smoke test: `solve("x**2-4", "x")` → `"[-2, 2]"` (or equivalent)
- [ ] Node.js smoke test: `isprime("7")` → `"Error in isprime: not available in web build"`
- [ ] `.wasm` size < 10 MB uncompressed
- [ ] `flutter run -d chrome` loads CrispCalc, CAS calls work after WASM init
- [ ] NumericFallbackEvaluator still works before WASM loads
- [ ] CI workflow passes on GitHub Actions
