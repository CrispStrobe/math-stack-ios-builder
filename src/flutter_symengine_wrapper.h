/*
 * flutter_symengine_wrapper.h
 * Complete C wrapper for SymEngine for use in Flutter FFI.
 * Manages symbolic math, number theory, and matrix operations.
 */
#ifndef FLUTTER_SYMENGINE_WRAPPER_H
#define FLUTTER_SYMENGINE_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare the opaque struct for matrices.
// The Dart side will only ever see a pointer to this.
typedef struct CDenseMatrix CDenseMatrix;

// Core SymEngine wrapper functions for Flutter FFI
char* flutter_symengine_evaluate(const char* expression);
char* flutter_symengine_solve(const char* expression, const char* symbol);
char* flutter_symengine_expand(const char* expression);
char* flutter_symengine_factor(const char* expression);
char* flutter_symengine_differentiate(const char* expression, const char* symbol);
char* flutter_symengine_integrate(const char* expression, const char* symbol); // NOTE: Not implemented in SymEngine's C API.
char* flutter_symengine_simplify(const char* expression);
char* flutter_symengine_substitute(const char* expression, const char* symbol, const char* value);
void flutter_symengine_free_string(char* str);

// Mathematical Functions
char* flutter_symengine_abs(const char* expression);
char* flutter_symengine_sin(const char* expression);
char* flutter_symengine_cos(const char* expression);
char* flutter_symengine_tan(const char* expression);
char* flutter_symengine_asin(const char* expression);
char* flutter_symengine_acos(const char* expression);
char* flutter_symengine_atan(const char* expression);
char* flutter_symengine_sinh(const char* expression);
char* flutter_symengine_cosh(const char* expression);
char* flutter_symengine_tanh(const char* expression);
char* flutter_symengine_asinh(const char* expression);
char* flutter_symengine_acosh(const char* expression);
char* flutter_symengine_atanh(const char* expression);
char* flutter_symengine_exp(const char* expression);
char* flutter_symengine_log(const char* expression);
char* flutter_symengine_sqrt(const char* expression);
char* flutter_symengine_gamma(const char* expression);

// Number theory functions
char* flutter_symengine_gcd(const char* a, const char* b);
char* flutter_symengine_lcm(const char* a, const char* b);
char* flutter_symengine_factorial(int n);
char* flutter_symengine_fibonacci(int n);

// Constants
char* flutter_symengine_get_pi(void);
char* flutter_symengine_get_e(void);
char* flutter_symengine_get_euler_gamma(void);

// Arbitrary-precision real constants via MPFR (through SymEngine's
// basic_evalf). The argument is decimal digits, 1..10000. Returns a
// string the caller must free with flutter_symengine_free_string.
char* flutter_symengine_pi_with_precision(int decimal_digits);
char* flutter_symengine_e_with_precision(int decimal_digits);
char* flutter_symengine_euler_gamma_with_precision(int decimal_digits);
char* flutter_symengine_sqrt2_with_precision(int decimal_digits);
// Generic arbitrary-precision numeric evaluation of any parseable
// expression (real-valued); backs the calculator's `evalf(expr, N)`.
char* flutter_symengine_evalf_with_precision(const char* expression,
                                             int decimal_digits);
// Bessel functions of the first (J) and second (Y) kind, integer order,
// real argument, via MPFR (SymEngine has no Bessel). Back the
// calculator/grapher `besselj(n, x)` / `bessely(n, x)`.
char* flutter_symengine_besselj(int order, const char* x_str);
char* flutter_symengine_bessely(int order, const char* x_str);

// Number-theory primitives (round 89). All take arbitrary-
// precision decimal strings. `isprime` returns "true" / "false";
// `nextprime` and `prevprime` return a decimal string. Caller
// frees the result with `flutter_symengine_free_string`.
char* flutter_symengine_isprime(const char* n);
char* flutter_symengine_nextprime(const char* n);
char* flutter_symengine_prevprime(const char* n);

// Integer factorization (round 90). Returns "p1^e1*p2^e2*..."
// (with "^1" omitted) or "0" / "1" / "-1" for trivial cases.
// Inputs up to ~90 bits / 27 decimal digits; larger values are
// rejected to keep the per-call time bounded.
char* flutter_symengine_factorint(const char* n);

// Modular arithmetic + multiplicative number theory (round 4 of the
// precision arc). All take arbitrary-precision decimal strings and
// return a decimal string ("true"/"false"-style errors come back as
// the standard "Error: ..." payload). Caller frees with
// `flutter_symengine_free_string`.
//   modpow(a, e, m)  -> a^e mod m   (m > 0; e may be negative if a is
//                                    invertible mod m)
//   modinv(a, m)     -> a^-1 mod m  (errors when gcd(a, m) != 1)
//   totient(n)       -> Euler's phi(n)               (n >= 1)
//   jacobi(a, n)     -> Jacobi symbol (a/n) as -1/0/1 (n odd, n > 0)
char* flutter_symengine_modpow(const char* a, const char* e, const char* m);
char* flutter_symengine_modinv(const char* a, const char* m);
char* flutter_symengine_totient(const char* n);
char* flutter_symengine_jacobi(const char* a, const char* n);

// Matrix operations (using opaque pointers for memory safety)
CDenseMatrix* flutter_symengine_matrix_new(int rows, int cols);
void flutter_symengine_matrix_free(CDenseMatrix* matrix);
int flutter_symengine_matrix_set_element(CDenseMatrix* matrix, int row, int col, const char* value);
char* flutter_symengine_matrix_get_element(CDenseMatrix* matrix, int row, int col);
char* flutter_symengine_matrix_to_string(CDenseMatrix* matrix);
char* flutter_symengine_matrix_det(CDenseMatrix* matrix);
CDenseMatrix* flutter_symengine_matrix_inv(CDenseMatrix* matrix);
CDenseMatrix* flutter_symengine_matrix_add(CDenseMatrix* a, CDenseMatrix* b);
CDenseMatrix* flutter_symengine_matrix_mul(CDenseMatrix* a, CDenseMatrix* b);

// Utility functions
const char* flutter_symengine_version(void);
char* flutter_symengine_test_basic_operations(void);
char* flutter_symengine_test_symbolic(void);

#ifdef __cplusplus
}
#endif

#endif /* FLUTTER_SYMENGINE_WRAPPER_H */

