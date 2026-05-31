# Mathematical Computing Stack Builder for Flutter, and especially for iOS

[](https://developer.apple.com/ios/)
[](https://developer.apple.com/macos/)
[](https://developer.apple.com/documentation/xcode/building_a_universal_macos_binary)
[](https://opensource.org/licenses/MIT)

A complete build system for compiling a powerful mathematical computing stack for Flutter. This project compiles **GMP, MPFR, MPC, FLINT, and SymEngine** into self-contained, static `XCFrameworks` for Apple platforms (iOS devices, simulators, macOS) and **WebAssembly** for browsers via Emscripten.

This provides a suite of tools for arbitrary-precision arithmetic, number theory, and symbolic manipulation directly within Flutter applications — both native and web.

-----

## What This Builds

### Native (Apple platforms)

The build process creates five interdependent `XCFrameworks`. The final product, `SymEngineFlutterWrapper.xcframework`, bundles the SymEngine library with a C wrapper designed for seamless Flutter FFI integration.

| Framework                           | Library                                                              | Purpose                                          |
| ----------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------ |
| `GMP.xcframework`                   | [GNU Multiple Precision](https://gmplib.org/) 6.3.0                  | Arbitrary-precision integer arithmetic.          |
| `MPFR.xcframework`                  | [Multiple-Precision Floating-Point](https://www.mpfr.org/) 4.2.2     | Correctly rounded arbitrary-precision floats.    |
| `MPC.xcframework`                   | [Multiple-Precision Complex](http://www.multiprecision.org/) 1.3.1    | Arbitrary-precision complex number arithmetic.   |
| `FLINT.xcframework`                 | [Fast Library for Number Theory](https://flintlib.org/) 3.3.1        | Advanced number theory, polynomials, and matrices. |
| `SymEngineFlutterWrapper.xcframework` | [SymEngine](https://symengine.org/) 0.11.2 + Flutter C Wrapper       | Fast symbolic manipulation with a C FFI layer.     |

### WebAssembly

An Emscripten build produces `symengine.js` + `symengine.wasm` (~1.1 MB)
using `INTEGER_CLASS=boostmp` (Boost.Multiprecision, header-only — no
GMP/MPFR/FLINT native deps). The WASM module provides the full CAS
core (evaluate, expand, differentiate, solve, substitute, 17 unary math
functions, gcd/lcm/factorial/fibonacci, matrix ops). Functions that
require GMP/MPFR/FLINT (isprime, factorint, Bessel, arbitrary-precision
evalf, modular arithmetic) return clean error strings. See
[`WASM_BUILD_PLAN.md`](WASM_BUILD_PLAN.md) for the full build
instructions and capability delta.

-----

## Requirements

### Native (Apple) build

  - **macOS** with Xcode and Command Line Tools installed.
  - **CMake**: Required for building SymEngine. Install via Homebrew: `brew install cmake`.
  - **Source Archives**: The following tarballs must be present in the project's root directory:
      - `gmp-6.3.0.tar.bz2`
      - `mpfr-4.2.2.tar.xz`
      - `mpc-1.3.1.tar.gz`
      - `flint-3.3.1.tar.gz`
      - `symengine-0.11.2.tar.gz`

### WASM build

  - **Emscripten SDK** (emsdk): `./emsdk install latest && ./emsdk activate latest`.
  - **Boost headers** (1.87+, header-only — no compiled libraries needed).
  - **CMake** 3.10+.
  - SymEngine source (`symengine-0.11.2/` directory).

See [`WASM_BUILD_PLAN.md`](WASM_BUILD_PLAN.md) for step-by-step instructions.

-----

## Build Instructions

A master script, `build_all.sh`, handles the entire build process, running the individual library scripts in the correct dependency order.

```bash
# First, make all build scripts executable
chmod +x build_*.sh

# Run the master script to build the entire stack
./build_all.sh

# All five XCFrameworks have been created in the root directory.
```

The master script is the recommended way to build, as it ensures all dependencies are met. It simply automates the process of running each `build_gmp.sh`, `build_mpfr.sh`, etc., in the correct sequence.

-----

## Integration with Flutter

This project is the foundational component for the **symbolic\_math\_bridge** Flutter plugin. The generated XCFrameworks are consumed by the plugin, providing unified access to the entire math stack.

### Unified Architecture

A single Flutter plugin provides access to all libraries, from high-level symbolic algebra down to low-level arbitrary-precision arithmetic.

```dart
// High-level symbolic computing (SymEngine)
final result = casBridge.evaluate('solve(x^2 - 1, x)');

// Direct arbitrary-precision integer arithmetic (GMP)
final bigInt = casBridge.gmpPower(2, 256); // 2^256

// Direct arbitrary-precision floating-point (MPFR) 
final pi = casBridge.mpfrGetPi(128); // 128-bit precision π

// Direct complex number arithmetic (MPC)
final complexResult = casBridge.mpcMultiply(3, 4, 1, 2); // (3+4i) * (1+2i)

// Direct number theory functions (FLINT)
final isPrime = casBridge.flintIsPrime(997); // true
```

### The Symbol Linking Solution

To prevent the linker from stripping unused symbols from the static C libraries—a common issue in native integration—this system uses a simple and robust solution:

1.  **Force Symbol Loading**: The plugin's Objective-C bridge file contains direct references to key functions from all five libraries, ensuring they are linked into the final application binary.
2.  **XCFramework Integration**: Packaging as XCFrameworks provides clean header access and simplifies integration with CocoaPods and Xcode.
3.  **Unified Plugin**: A single plugin, `symbolic_math_bridge`, manages all five libraries, eliminating the need for multiple, separate packages.

-----

## Companion Repositories

This build system is one part of a complete mathematical computing solution for Flutter:

  - **[math-stack-ios-builder](https://github.com/CrispStrobe/math-stack-ios-builder)** (This Repository): Builds the XCFramework libraries.
  - **[symbolic\_math\_bridge](https://github.com/CrispStrobe/symbolic_math_bridge)**: The Flutter plugin that provides a unified Dart API to all libraries.
  - **[math-stack-test](https://github.com/CrispStrobe/math-stack-test)**: A demo Flutter app showcasing the complete functionality.

-----

## Technical Notes

### Build Dependencies

The build order is critical and enforced by the `build_all.sh` script:

  - **MPFR** requires **GMP**.
  - **MPC** requires **GMP** and **MPFR**.
  - **FLINT** requires **GMP** and **MPFR**.
  - **SymEngine** requires **GMP**, **MPFR**, **MPC**, and **FLINT**.

### SymEngine Flutter C Wrapper

The SymEngine build creates the core library and bundles it with a C wrapper that provides a simplified, FFI-friendly API. The function names are prefixed to avoid conflicts.

```c
// C wrapper functions exposed for FFI
char* flutter_symengine_evaluate(const char* expression);
char* flutter_symengine_solve(const char* expression, const char* symbol);
char* flutter_symengine_expand(const char* expression);
// ... and many more ...
void flutter_symengine_free_string(char* str);
```

-----

## Troubleshooting

  - **"Command not found" or "Permission Denied"**: Run `chmod +x build_*.sh` to make the build scripts executable.
  - **"SDK path not found"**: Install the Xcode Command Line Tools via `xcode-select --install`.
  - **Dependency Error (e.g., "GMP build not found")**: Ensure you are using the `build_all.sh` script or running the individual scripts in the correct order.
  - **Symbol Linking Issues in Flutter ("symbol not found")**:
      - Verify the plugin's `.podspec` includes the necessary linker flags (`-all_load`).
      - Ensure the Objective-C bridge file includes references to force-load symbols.
      - Confirm all XCFrameworks are properly included in the plugin's Podspec.

-----

## License

This build system is released under the **MIT License**. The underlying mathematical libraries are available under their own open-source licenses (primarily LGPL), which you must comply with in your application.