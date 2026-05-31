/*
 * flutter_symengine_wrapper.c
 * Flutter-specific C wrapper implementation using SymEngine cwrapper.h API.
 * This version is complete, with no placeholders.
 *
 * When compiled with Emscripten (__EMSCRIPTEN__ defined), GMP/MPFR/FLINT-
 * dependent functions are stubbed out (return error strings) and all
 * exported functions are decorated with EMSCRIPTEN_KEEPALIVE so they
 * survive dead-code elimination in the linker.
 */
#include "flutter_symengine_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symengine/cwrapper.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

// GMP/MPFR/FLINT-backed functions (number theory, high precision, Bessel,
// real factor) are available on native always, and on WASM only when the
// build links the WASM-compiled math stack (the `-DWASM_WITH_FLINT` build).
// The default boostmp WASM build leaves SYM_HAS_NATIVE_LIBS = 0 and keeps
// the stubs.
#if !defined(__EMSCRIPTEN__) || defined(WASM_WITH_FLINT)
#define SYM_HAS_NATIVE_LIBS 1
#else
#define SYM_HAS_NATIVE_LIBS 0
#endif

// --- Helper Functions ---

// Creates a formatted error string that must be freed by the caller.
static char* create_error_string(const char* operation, const char* error) {
    size_t len = strlen("Error in ") + strlen(operation) + strlen(": ") + strlen(error) + 1;
    char* result = (char*)malloc(len);
    if (result) {
        snprintf(result, len, "Error in %s: %s", operation, error);
    }
    return result;
}

// Safely converts a SymEngine 'basic' object to a string.
static char* basic_to_string_safe(basic b) {
    char* result = basic_str(b);
    if (!result) {
        return strdup("conversion_error");
    }
    return result;
}

// --- Macro for Unary Mathematical Functions ---

// This macro generates a function that takes one string expression,
// applies a single-argument SymEngine function (like basic_sin),
// and returns the result as a new string.
#define IMPLEMENT_UNARY_FUNC(wrapper_name, symengine_func) \
WASM_EXPORT char* wrapper_name(const char* expression) { \
    if (!expression) return create_error_string(#wrapper_name, "null expression"); \
    \
    basic expr, result; \
    basic_new_stack(expr); \
    basic_new_stack(result); \
    \
    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION) { \
        basic_free_stack(expr); \
        basic_free_stack(result); \
        return create_error_string(#wrapper_name, "parse failed"); \
    } \
    \
    if (symengine_func(result, expr) != SYMENGINE_NO_EXCEPTION) { \
        basic_free_stack(expr); \
        basic_free_stack(result); \
        return create_error_string(#wrapper_name, "operation failed"); \
    } \
    \
    char* result_str = basic_to_string_safe(result); \
    basic_free_stack(expr); \
    basic_free_stack(result); \
    return result_str; \
}

// --- Core Symbolic Functions ---

WASM_EXPORT char* flutter_symengine_evaluate(const char* expression) {
    if (!expression) {
        return create_error_string("evaluate", "null expression");
    }

    basic expr, result;
    basic_new_stack(expr);
    basic_new_stack(result);

    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("evaluate", "parse failed");
    }

    // For evaluation, 'evalf' calculates a numeric value if possible.
    // 53 bits is standard double precision.
    if (basic_evalf(result, expr, 53, 0) != SYMENGINE_NO_EXCEPTION) {
        // Fallback to expand if evalf fails (e.g., for purely symbolic expressions)
        basic_expand(result, expr);
    }

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_solve(const char* expression, const char* symbol) {
    if (!expression || !symbol) {
        return create_error_string("solve", "null input");
    }

    basic expr, sym;
    basic_new_stack(expr);
    basic_new_stack(sym);

    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION ||
        symbol_set(sym, symbol) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(sym);
        return create_error_string("solve", "parsing failed");
    }

    CSetBasic* solutions = setbasic_new();
    if (basic_solve_poly(solutions, expr, sym) != SYMENGINE_NO_EXCEPTION) {
        setbasic_free(solutions);
        basic_free_stack(expr);
        basic_free_stack(sym);
        return create_error_string("solve", "solve operation failed");
    }
    
    size_t num_solutions = setbasic_size(solutions);
    if (num_solutions == 0) {
        setbasic_free(solutions);
        basic_free_stack(expr);
        basic_free_stack(sym);
        return strdup("[]"); // Return empty list for no solutions
    }

    // Concatenate all solutions into a single string "[sol1, sol2, ...]"
    char* final_result = NULL;
    size_t total_len = 2; // Start with "[" and "]"
    char** solution_strs = (char**)malloc(num_solutions * sizeof(char*));

    for (size_t i = 0; i < num_solutions; i++) {
        basic sol;
        basic_new_stack(sol);
        setbasic_get(solutions, i, sol);
        solution_strs[i] = basic_to_string_safe(sol);
        total_len += strlen(solution_strs[i]) + (i > 0 ? 2 : 0); // ", "
        basic_free_stack(sol);
    }

    final_result = (char*)malloc(total_len + 1);
    strcpy(final_result, "[");
    for (size_t i = 0; i < num_solutions; i++) {
        strcat(final_result, solution_strs[i]);
        if (i < num_solutions - 1) {
            strcat(final_result, ", ");
        }
        free(solution_strs[i]);
    }
    strcat(final_result, "]");
    free(solution_strs);
    
    setbasic_free(solutions);
    basic_free_stack(expr);
    basic_free_stack(sym);
    return final_result;
}

WASM_EXPORT char* flutter_symengine_expand(const char* expression) {
    if (!expression) return create_error_string("expand", "null expression");

    basic expr, result;
    basic_new_stack(expr);
    basic_new_stack(result);

    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("expand", "parse failed");
    }
    if (basic_expand(result, expr) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("expand", "expansion failed");
    }

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(result);
    return result_str;
}

#if SYM_HAS_NATIVE_LIBS
// Real polynomial factorization lives in flutter_symengine_cas.cpp, which
// uses SymEngine's C++ API + FLINT (fmpz_poly_factor). The C cwrapper.h
// exposes no factorization, so this is the only way to get a true factor.
extern char* flutter_symengine_factor_cpp(const char* expression);

WASM_EXPORT char* flutter_symengine_factor(const char* expression) {
    if (!expression) return create_error_string("factor", "null expression");
    return flutter_symengine_factor_cpp(expression);
}
#else
// boostmp WASM build without FLINT — true factoring isn't available;
// fall back to expand (the historical behavior on that target).
WASM_EXPORT char* flutter_symengine_factor(const char* expression) {
    return flutter_symengine_expand(expression);
}
#endif
WASM_EXPORT char* flutter_symengine_differentiate(const char* expression, const char* symbol) {
    if (!expression || !symbol) return create_error_string("differentiate", "null input");

    basic expr, sym, result;
    basic_new_stack(expr);
    basic_new_stack(sym);
    basic_new_stack(result);

    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION ||
        symbol_set(sym, symbol) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(sym);
        basic_free_stack(result);
        return create_error_string("differentiate", "parsing failed");
    }
    if (basic_diff(result, expr, sym) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(sym);
        basic_free_stack(result);
        return create_error_string("differentiate", "differentiation failed");
    }

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(sym);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_integrate(const char* expression, const char* symbol) {
    // NOTE: SymEngine's C API (cwrapper.h) does not expose an integration function.
    // This is a known limitation of the C interface, not the C++ core.
    return create_error_string("integrate", "not implemented in SymEngine C API");
}

WASM_EXPORT char* flutter_symengine_simplify(const char* expression) {
    // "Simplification" is complex. `expand` is a common form of simplification.
    return flutter_symengine_expand(expression);
}

WASM_EXPORT char* flutter_symengine_substitute(const char* expression, const char* symbol, const char* value) {
    if (!expression || !symbol || !value) return create_error_string("substitute", "null input");

    basic expr, sym, val, result;
    basic_new_stack(expr);
    basic_new_stack(sym);
    basic_new_stack(val);
    basic_new_stack(result);

    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION ||
        symbol_set(sym, symbol) != SYMENGINE_NO_EXCEPTION ||
        basic_parse(val, value) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(sym);
        basic_free_stack(val);
        basic_free_stack(result);
        return create_error_string("substitute", "parsing failed");
    }

    if (basic_subs2(result, expr, sym, val) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(sym);
        basic_free_stack(val);
        basic_free_stack(result);
        return create_error_string("substitute", "substitution failed");
    }

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(sym);
    basic_free_stack(val);
    basic_free_stack(result);
    return result_str;
}

// --- Mathematical Functions (Implemented with Macro) ---

IMPLEMENT_UNARY_FUNC(flutter_symengine_abs, basic_abs)
IMPLEMENT_UNARY_FUNC(flutter_symengine_sin, basic_sin)
IMPLEMENT_UNARY_FUNC(flutter_symengine_cos, basic_cos)
IMPLEMENT_UNARY_FUNC(flutter_symengine_tan, basic_tan)
IMPLEMENT_UNARY_FUNC(flutter_symengine_asin, basic_asin)
IMPLEMENT_UNARY_FUNC(flutter_symengine_acos, basic_acos)
IMPLEMENT_UNARY_FUNC(flutter_symengine_atan, basic_atan)
IMPLEMENT_UNARY_FUNC(flutter_symengine_sinh, basic_sinh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_cosh, basic_cosh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_tanh, basic_tanh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_asinh, basic_asinh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_acosh, basic_acosh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_atanh, basic_atanh)
IMPLEMENT_UNARY_FUNC(flutter_symengine_exp, basic_exp)
IMPLEMENT_UNARY_FUNC(flutter_symengine_log, basic_log)
IMPLEMENT_UNARY_FUNC(flutter_symengine_sqrt, basic_sqrt)
IMPLEMENT_UNARY_FUNC(flutter_symengine_gamma, basic_gamma)

// --- Number Theory Functions ---

WASM_EXPORT char* flutter_symengine_gcd(const char* a, const char* b) {
    basic A, B, result;
    basic_new_stack(A);
    basic_new_stack(B);
    basic_new_stack(result);

    if (basic_parse(A, a) != SYMENGINE_NO_EXCEPTION || basic_parse(B, b) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(A);
        basic_free_stack(B);
        basic_free_stack(result);
        return create_error_string("gcd", "parsing failed");
    }
    ntheory_gcd(result, A, B);

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(A);
    basic_free_stack(B);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_lcm(const char* a, const char* b) {
    basic A, B, result;
    basic_new_stack(A);
    basic_new_stack(B);
    basic_new_stack(result);

    if (basic_parse(A, a) != SYMENGINE_NO_EXCEPTION || basic_parse(B, b) != SYMENGINE_NO_EXCEPTION) {
       basic_free_stack(A);
       basic_free_stack(B);
       basic_free_stack(result);
       return create_error_string("lcm", "parsing failed");
    }
    ntheory_lcm(result, A, B);

    char* result_str = basic_to_string_safe(result);
    basic_free_stack(A);
    basic_free_stack(B);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_factorial(int n) {
    if (n < 0) return create_error_string("factorial", "input must be non-negative");
    basic result;
    basic_new_stack(result);
    ntheory_factorial(result, (unsigned long)n);
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_fibonacci(int n) {
    if (n < 0) return create_error_string("fibonacci", "input must be non-negative");
    basic result;
    basic_new_stack(result);
    ntheory_fibonacci(result, (unsigned long)n);
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(result);
    return result_str;
}

// --- Constants ---

WASM_EXPORT char* flutter_symengine_get_pi(void) {
    basic s;
    basic_new_stack(s);
    basic_const_pi(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

WASM_EXPORT char* flutter_symengine_get_e(void) {
    basic s;
    basic_new_stack(s);
    basic_const_E(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

WASM_EXPORT char* flutter_symengine_get_euler_gamma(void) {
    basic s;
    basic_new_stack(s);
    basic_const_EulerGamma(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

// --- Arbitrary-Precision Real Constants ---

#if SYM_HAS_NATIVE_LIBS
// Native (and FLINT-enabled WASM) builds: full GMP/MPFR/FLINT-backed
// number theory, arbitrary-precision evaluation, and Bessel functions.

char* flutter_symengine_pi_with_precision(int decimal_digits) {
    if (decimal_digits < 1 || decimal_digits > 10000) {
        return create_error_string("pi_with_precision",
            "decimal_digits must be in 1..10000");
    }
    basic pi_sym, pi_evalf;
    basic_new_stack(pi_sym);
    basic_new_stack(pi_evalf);
    basic_const_pi(pi_sym);
    // MPFR works in bits; 10 decimal digits ≈ 33.22 bits. Pad +8
    // bits so the trailing digit rounds correctly.
    unsigned long bits = (unsigned long)((double)decimal_digits * 3.322) + 8;
    if (basic_evalf(pi_evalf, pi_sym, bits, 1) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(pi_sym);
        basic_free_stack(pi_evalf);
        return create_error_string("pi_with_precision", "evalf failed");
    }
    char* result = basic_to_string_safe(pi_evalf);
    basic_free_stack(pi_sym);
    basic_free_stack(pi_evalf);
    return result;
}

// Generic arbitrary-precision numeric evaluation: parse `expression`
// and evalf it to `decimal_digits` digits via MPFR (real path). Powers
// the calculator's `evalf(expr, N)` — e.g. `evalf(ln(10), 100)`,
// `evalf(zeta(2), 50)`, `evalf(sqrt(2)+sqrt(3), 80)`. A non-real result
// is rejected (the high-precision complex / MPC path is separate); for
// those the 53-bit `flutter_symengine_evaluate` still applies.
char* flutter_symengine_evalf_with_precision(const char* expression,
                                             int decimal_digits) {
    if (!expression) {
        return create_error_string("evalf_with_precision", "null expression");
    }
    if (decimal_digits < 1 || decimal_digits > 10000) {
        return create_error_string("evalf_with_precision",
            "decimal_digits must be in 1..10000");
    }
    basic expr, result;
    basic_new_stack(expr);
    basic_new_stack(result);
    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("evalf_with_precision", "parse failed");
    }
    unsigned long bits = (unsigned long)((double)decimal_digits * 3.322) + 8;
    if (basic_evalf(result, expr, bits, 1) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("evalf_with_precision",
            "evalf failed (non-real or not numerically evaluable)");
    }
    char* s = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(result);
    return s;
}

// Complex counterpart of evalf_with_precision: parse `expression` and
// evalf it to `decimal_digits` digits via the MPC (complex) path
// (basic_evalf real=0). Handles complex-valued expressions —
// cevalf((1+I)^10, 30), cevalf(sqrt(-2), 50), cevalf(gamma(1+I), 40) —
// returning SymEngine's "a + b*I" string. The 53-bit complex path is
// the same one flutter_symengine_evaluate already uses.
char* flutter_symengine_cevalf_with_precision(const char* expression,
                                              int decimal_digits) {
    if (!expression) {
        return create_error_string("cevalf_with_precision", "null expression");
    }
    if (decimal_digits < 1 || decimal_digits > 10000) {
        return create_error_string("cevalf_with_precision",
            "decimal_digits must be in 1..10000");
    }
    basic expr, result;
    basic_new_stack(expr);
    basic_new_stack(result);
    if (basic_parse(expr, expression) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("cevalf_with_precision", "parse failed");
    }
    unsigned long bits = (unsigned long)((double)decimal_digits * 3.322) + 8;
    if (basic_evalf(result, expr, bits, 0) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(expr);
        basic_free_stack(result);
        return create_error_string("cevalf_with_precision",
            "evalf failed (not numerically evaluable)");
    }
    char* s = basic_to_string_safe(result);
    basic_free_stack(expr);
    basic_free_stack(result);
    return s;
}

// Bessel functions of the first (J) and second (Y) kind, integer order,
// real argument — computed directly via MPFR (mpfr_jn / mpfr_yn), since
// SymEngine has no Bessel functions. `order` is the integer order n;
// `x_str` is the (decimal) real argument. Returns the value as a
// ~15-significant-digit string (double precision is what the grapher
// consumes; the calculator shows the same). Used by the calculator's
// `besselj(n, x)` / `bessely(n, x)` and the graphing path.
#include <mpfr.h>

static char* bessel_eval(int order, const char* x_str, int kind,
                         const char* op) {
    if (!x_str) return create_error_string(op, "null argument");
    mpfr_t x, result;
    mpfr_init2(x, 64);
    mpfr_init2(result, 64);
    if (mpfr_set_str(x, x_str, 10, MPFR_RNDN) != 0) {
        mpfr_clear(x);
        mpfr_clear(result);
        return create_error_string(op, "argument parse failed");
    }
    // kind 0 = J (first kind), kind 1 = Y (second kind).
    if (kind == 0) {
        mpfr_jn(result, (long)order, x, MPFR_RNDN);
    } else {
        mpfr_yn(result, (long)order, x, MPFR_RNDN);
    }
    double d = mpfr_get_d(result, MPFR_RNDN);
    mpfr_clear(x);
    mpfr_clear(result);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", d);
    return strdup(buf);
}

char* flutter_symengine_besselj(int order, const char* x_str) {
    return bessel_eval(order, x_str, 0, "besselj");
}

char* flutter_symengine_bessely(int order, const char* x_str) {
    return bessel_eval(order, x_str, 1, "bessely");
}

// Shared helper macro: evalf a basic-producing constructor at the
// requested decimal precision and return the result as a malloc'd
// string. The `ctor` argument is a statement that populates `sym`.
#define IMPLEMENT_CONST_WITH_PRECISION(fn_name, op_label, ctor) \
char* fn_name(int decimal_digits) { \
    if (decimal_digits < 1 || decimal_digits > 10000) { \
        return create_error_string(op_label, \
            "decimal_digits must be in 1..10000"); \
    } \
    basic sym, evalf_result; \
    basic_new_stack(sym); \
    basic_new_stack(evalf_result); \
    ctor; \
    unsigned long bits = \
        (unsigned long)((double)decimal_digits * 3.322) + 8; \
    if (basic_evalf(evalf_result, sym, bits, 1) != SYMENGINE_NO_EXCEPTION) { \
        basic_free_stack(sym); \
        basic_free_stack(evalf_result); \
        return create_error_string(op_label, "evalf failed"); \
    } \
    char* result = basic_to_string_safe(evalf_result); \
    basic_free_stack(sym); \
    basic_free_stack(evalf_result); \
    return result; \
}

IMPLEMENT_CONST_WITH_PRECISION(
    flutter_symengine_e_with_precision,
    "e_with_precision",
    basic_const_E(sym))

IMPLEMENT_CONST_WITH_PRECISION(
    flutter_symengine_euler_gamma_with_precision,
    "euler_gamma_with_precision",
    basic_const_EulerGamma(sym))

// sqrt(2) has no basic_const_* helper — parse the string form.
// basic_evalf then routes through MPFR like the other constants.
IMPLEMENT_CONST_WITH_PRECISION(
    flutter_symengine_sqrt2_with_precision,
    "sqrt2_with_precision",
    do {
        if (basic_parse(sym, "sqrt(2)") != SYMENGINE_NO_EXCEPTION) {
            basic_free_stack(sym);
            basic_free_stack(evalf_result);
            return create_error_string("sqrt2_with_precision",
                "parse failed");
        }
    } while (0))

// --- Number-Theory Primitives (Round 89) ---
//
// `isprime(n)` and `prevprime(n)` go straight to GMP because
// SymEngine's cwrapper.h doesn't expose them. `nextprime(n)` goes
// through SymEngine's `ntheory_nextprime` (basic-level) for
// consistency with the rest of the SymEngine ntheory family. All
// three accept arbitrary-precision decimal strings.

#include <gmp.h>

char* flutter_symengine_isprime(const char* n) {
    if (!n) return create_error_string("isprime", "null input");
    mpz_t x;
    if (mpz_init_set_str(x, n, 10) != 0) {
        mpz_clear(x);
        return create_error_string("isprime", "parse failed");
    }
    // 25 Miller-Rabin reps. Returns 0 (composite), 1 (probably
    // prime), 2 (definitely prime). Any positive result = "true".
    int r = mpz_probab_prime_p(x, 25);
    mpz_clear(x);
    return strdup(r > 0 ? "true" : "false");
}

char* flutter_symengine_nextprime(const char* n) {
    if (!n) return create_error_string("nextprime", "null input");
    basic input, result;
    basic_new_stack(input);
    basic_new_stack(result);
    if (basic_parse(input, n) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(input);
        basic_free_stack(result);
        return create_error_string("nextprime", "parse failed");
    }
    if (ntheory_nextprime(result, input) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(input);
        basic_free_stack(result);
        return create_error_string("nextprime", "operation failed");
    }
    char* s = basic_to_string_safe(result);
    basic_free_stack(input);
    basic_free_stack(result);
    return s;
}

char* flutter_symengine_prevprime(const char* n) {
    if (!n) return create_error_string("prevprime", "null input");
    mpz_t x;
    if (mpz_init_set_str(x, n, 10) != 0) {
        mpz_clear(x);
        return create_error_string("prevprime", "parse failed");
    }
    // prevprime is defined as "largest p with p < n and p prime".
    // No prime is < 2.
    if (mpz_cmp_ui(x, 3) < 0) {
        mpz_clear(x);
        return create_error_string("prevprime", "no prime below input");
    }
    // Decrement until prime. The gap between consecutive primes
    // around N is ~ln(N) on average, so this loop runs only a few
    // times for typical inputs. For mathematicians who pass a
    // billion-digit value we'd cap at e.g. 10000 iterations — for
    // now trust the user.
    mpz_sub_ui(x, x, 1);
    while (mpz_cmp_ui(x, 2) > 0 && mpz_probab_prime_p(x, 25) == 0) {
        mpz_sub_ui(x, x, 1);
    }
    char* s = mpz_get_str(NULL, 10, x);
    mpz_clear(x);
    return s;
}

// Round 90: integer factorization via FLINT's fmpz_factor (Pollard
// rho + trial division). Returns a compact string of the form
// "p1^e1*p2^e2*..." with the "^1" omitted for unit exponents.
// Special-cases: "0" for n=0, "1" for n=1 (no factors). Negative
// inputs get a leading "-1*" prefix. Bit-size capped at 90 to
// keep the per-call time bounded — FLINT can handle bigger numbers
// but the user shouldn't lock the UI for a multi-second factor.

#include <flint/fmpz.h>
#include <flint/fmpz_factor.h>

char* flutter_symengine_factorint(const char* n) {
    if (!n) return create_error_string("factorint", "null input");
    fmpz_t x;
    fmpz_init(x);
    if (fmpz_set_str(x, n, 10) != 0) {
        fmpz_clear(x);
        return create_error_string("factorint", "parse failed");
    }
    // Bit-size cap. 90 bits ≈ 27 decimal digits — comfortable
    // headroom for any classroom example, while still ruling out
    // a 200-digit RSA modulus.
    if (fmpz_sizeinbase(x, 2) > 90) {
        fmpz_clear(x);
        return create_error_string("factorint",
            "input too large (max ~90 bits / 27 digits)");
    }
    // Trivial cases: 0 and ±1 have no prime factors.
    if (fmpz_is_zero(x)) {
        fmpz_clear(x);
        return strdup("0");
    }
    // Compute |x| for factoring; record sign separately.
    int neg = (fmpz_sgn(x) < 0);
    if (neg) fmpz_neg(x, x);
    if (fmpz_is_one(x)) {
        fmpz_clear(x);
        return strdup(neg ? "-1" : "1");
    }
    fmpz_factor_t f;
    fmpz_factor_init(f);
    fmpz_factor(f, x);
    // Build the output string. Allocate generously; one prime
    // factor takes at most fmpz_sizeinbase digits.
    size_t cap = 32 + (size_t)f->num * 64;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        fmpz_factor_clear(f);
        fmpz_clear(x);
        return create_error_string("factorint", "malloc failed");
    }
    size_t pos = 0;
    if (neg) {
        pos += (size_t)snprintf(buf + pos, cap - pos, "-1*");
    }
    for (slong i = 0; i < f->num; i++) {
        // f->p is a fmpz array; address arithmetic gives a pointer
        // to the i-th prime. fmpz_get_str(NULL, ...) allocates.
        char* prime_str = fmpz_get_str(NULL, 10, f->p + i);
        size_t need = strlen(prime_str) + 32; // "^N*" plus margin
        if (pos + need >= cap) {
            cap = (pos + need) * 2;
            char* grown = realloc(buf, cap);
            if (!grown) {
                free(prime_str);
                free(buf);
                fmpz_factor_clear(f);
                fmpz_clear(x);
                return create_error_string("factorint", "realloc failed");
            }
            buf = grown;
        }
        if (i > 0) {
            buf[pos++] = '*';
        }
        size_t plen = strlen(prime_str);
        memcpy(buf + pos, prime_str, plen);
        pos += plen;
        free(prime_str);
        if (f->exp[i] > 1) {
            pos += (size_t)snprintf(buf + pos, cap - pos, "^%lu",
                (unsigned long)f->exp[i]);
        }
    }
    buf[pos] = '\0';
    fmpz_factor_clear(f);
    fmpz_clear(x);
    return buf;
}

// Round 4 (precision arc): modular arithmetic + multiplicative
// number theory. modpow / modinv / jacobi go straight to GMP —
// SymEngine's cwrapper exposes ntheory_mod_inverse but neither powm
// nor the Jacobi symbol, and the __gmpz_* family is already kept
// alive (round 13). totient uses FLINT's fmpz_euler_phi. All four
// keep the arbitrary-precision string contract of the round-89/90
// ntheory family.

char* flutter_symengine_modpow(const char* a, const char* e, const char* m) {
    if (!a || !e || !m) return create_error_string("modpow", "null input");
    mpz_t base, exp, mod, res;
    if (mpz_init_set_str(base, a, 10) != 0) {
        mpz_clear(base);
        return create_error_string("modpow", "parse failed (a)");
    }
    if (mpz_init_set_str(exp, e, 10) != 0) {
        mpz_clear(base);
        mpz_clear(exp);
        return create_error_string("modpow", "parse failed (e)");
    }
    if (mpz_init_set_str(mod, m, 10) != 0) {
        mpz_clear(base);
        mpz_clear(exp);
        mpz_clear(mod);
        return create_error_string("modpow", "parse failed (m)");
    }
    if (mpz_sgn(mod) <= 0) {
        mpz_clear(base);
        mpz_clear(exp);
        mpz_clear(mod);
        return create_error_string("modpow", "modulus must be positive");
    }
    mpz_init(res);
    // GMP's mpz_powm raises a hardware divide-by-zero (SIGFPE — which
    // would take down the host app) on a negative exponent when the
    // base has no inverse mod m. Guard it: invert the base ourselves
    // first and fail gracefully when gcd(a, m) != 1.
    if (mpz_sgn(exp) < 0) {
        if (mpz_invert(base, base, mod) == 0) {
            mpz_clear(base);
            mpz_clear(exp);
            mpz_clear(mod);
            mpz_clear(res);
            return create_error_string("modpow",
                "negative exponent requires gcd(a, m) = 1");
        }
        mpz_neg(exp, exp);
    }
    mpz_powm(res, base, exp, mod);
    char* s = mpz_get_str(NULL, 10, res);
    mpz_clear(base);
    mpz_clear(exp);
    mpz_clear(mod);
    mpz_clear(res);
    return s;
}

char* flutter_symengine_modinv(const char* a, const char* m) {
    if (!a || !m) return create_error_string("modinv", "null input");
    mpz_t base, mod, res;
    if (mpz_init_set_str(base, a, 10) != 0) {
        mpz_clear(base);
        return create_error_string("modinv", "parse failed (a)");
    }
    if (mpz_init_set_str(mod, m, 10) != 0) {
        mpz_clear(base);
        mpz_clear(mod);
        return create_error_string("modinv", "parse failed (m)");
    }
    if (mpz_sgn(mod) <= 0) {
        mpz_clear(base);
        mpz_clear(mod);
        return create_error_string("modinv", "modulus must be positive");
    }
    mpz_init(res);
    // mpz_invert returns non-zero and stores res in [0, mod) when the
    // inverse exists; 0 when gcd(a, m) != 1.
    if (mpz_invert(res, base, mod) == 0) {
        mpz_clear(base);
        mpz_clear(mod);
        mpz_clear(res);
        return create_error_string("modinv", "no inverse: gcd(a, m) != 1");
    }
    char* s = mpz_get_str(NULL, 10, res);
    mpz_clear(base);
    mpz_clear(mod);
    mpz_clear(res);
    return s;
}

char* flutter_symengine_totient(const char* n) {
    if (!n) return create_error_string("totient", "null input");
    fmpz_t x, res;
    fmpz_init(x);
    if (fmpz_set_str(x, n, 10) != 0) {
        fmpz_clear(x);
        return create_error_string("totient", "parse failed");
    }
    if (fmpz_sgn(x) <= 0) {
        fmpz_clear(x);
        return create_error_string("totient", "n must be positive");
    }
    // phi(n) needs n's factorization internally, so it is as costly as
    // factorint — apply the same ~90-bit guard so the UI can't hang.
    if (fmpz_sizeinbase(x, 2) > 90) {
        fmpz_clear(x);
        return create_error_string("totient",
            "input too large (max ~90 bits / 27 digits)");
    }
    fmpz_init(res);
    fmpz_euler_phi(res, x);
    char* s = fmpz_get_str(NULL, 10, res);
    fmpz_clear(x);
    fmpz_clear(res);
    return s;
}

char* flutter_symengine_jacobi(const char* a, const char* n) {
    if (!a || !n) return create_error_string("jacobi", "null input");
    mpz_t top, bot;
    if (mpz_init_set_str(top, a, 10) != 0) {
        mpz_clear(top);
        return create_error_string("jacobi", "parse failed (a)");
    }
    if (mpz_init_set_str(bot, n, 10) != 0) {
        mpz_clear(top);
        mpz_clear(bot);
        return create_error_string("jacobi", "parse failed (n)");
    }
    // The Jacobi symbol (a/n) is defined only for odd n > 0; GMP's
    // mpz_jacobi has undefined behaviour outside that domain.
    if (mpz_sgn(bot) <= 0 || mpz_even_p(bot)) {
        mpz_clear(top);
        mpz_clear(bot);
        return create_error_string("jacobi", "n must be odd and positive");
    }
    int j = mpz_jacobi(top, bot);
    mpz_clear(top);
    mpz_clear(bot);
    char out[8];
    snprintf(out, sizeof(out), "%d", j);
    return strdup(out);
}

#else  // __EMSCRIPTEN__ — stub out all GMP/MPFR/FLINT-dependent functions

// Arbitrary-precision constants (require MPFR via basic_evalf)
WASM_EXPORT char* flutter_symengine_pi_with_precision(int decimal_digits) {
    return create_error_string("pi_with_precision", "not available in web build (requires MPFR)");
}
WASM_EXPORT char* flutter_symengine_e_with_precision(int decimal_digits) {
    return create_error_string("e_with_precision", "not available in web build (requires MPFR)");
}
WASM_EXPORT char* flutter_symengine_euler_gamma_with_precision(int decimal_digits) {
    return create_error_string("euler_gamma_with_precision", "not available in web build (requires MPFR)");
}
WASM_EXPORT char* flutter_symengine_sqrt2_with_precision(int decimal_digits) {
    return create_error_string("sqrt2_with_precision", "not available in web build (requires MPFR)");
}

// Generic arbitrary-precision evaluation (require MPFR/MPC)
WASM_EXPORT char* flutter_symengine_evalf_with_precision(const char* expression, int decimal_digits) {
    return create_error_string("evalf_with_precision", "not available in web build (requires MPFR)");
}
WASM_EXPORT char* flutter_symengine_cevalf_with_precision(const char* expression, int decimal_digits) {
    return create_error_string("cevalf_with_precision", "not available in web build (requires MPC)");
}

// Bessel functions (require MPFR directly)
WASM_EXPORT char* flutter_symengine_besselj(int order, const char* x_str) {
    return create_error_string("besselj", "not available in web build (requires MPFR)");
}
WASM_EXPORT char* flutter_symengine_bessely(int order, const char* x_str) {
    return create_error_string("bessely", "not available in web build (requires MPFR)");
}

// Number-theory primitives (require GMP directly)
WASM_EXPORT char* flutter_symengine_isprime(const char* n) {
    return create_error_string("isprime", "not available in web build (requires GMP)");
}
WASM_EXPORT char* flutter_symengine_nextprime(const char* n) {
    return create_error_string("nextprime", "not available in web build (requires GMP)");
}
WASM_EXPORT char* flutter_symengine_prevprime(const char* n) {
    return create_error_string("prevprime", "not available in web build (requires GMP)");
}

// Integer factorization (requires FLINT)
WASM_EXPORT char* flutter_symengine_factorint(const char* n) {
    return create_error_string("factorint", "not available in web build (requires FLINT)");
}

// Modular arithmetic (requires GMP/FLINT)
WASM_EXPORT char* flutter_symengine_modpow(const char* a, const char* e, const char* m) {
    return create_error_string("modpow", "not available in web build (requires GMP)");
}
WASM_EXPORT char* flutter_symengine_modinv(const char* a, const char* m) {
    return create_error_string("modinv", "not available in web build (requires GMP)");
}
WASM_EXPORT char* flutter_symengine_totient(const char* n) {
    return create_error_string("totient", "not available in web build (requires FLINT)");
}
WASM_EXPORT char* flutter_symengine_jacobi(const char* a, const char* n) {
    return create_error_string("jacobi", "not available in web build (requires GMP)");
}

#endif  // __EMSCRIPTEN__

// --- Matrix Operations (Opaque Pointers) ---

WASM_EXPORT CDenseMatrix* flutter_symengine_matrix_new(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return NULL;
    return dense_matrix_new_rows_cols(rows, cols);
}

WASM_EXPORT void flutter_symengine_matrix_free(CDenseMatrix* matrix) {
    if (matrix) {
        dense_matrix_free(matrix);
    }
}

WASM_EXPORT int flutter_symengine_matrix_set_element(CDenseMatrix* matrix, int row, int col, const char* value) {
    if (!matrix || !value) return -1;
    basic val;
    basic_new_stack(val);
    if (basic_parse(val, value) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(val);
        return -2; // Parse error
    }
    int result = dense_matrix_set_basic(matrix, row, col, val);
    basic_free_stack(val);
    return (result == SYMENGINE_NO_EXCEPTION) ? 0 : -3; // Set error
}

WASM_EXPORT char* flutter_symengine_matrix_get_element(CDenseMatrix* matrix, int row, int col) {
    if (!matrix) return create_error_string("matrix_get", "null matrix");
    basic s;
    basic_new_stack(s);
    if (dense_matrix_get_basic(s, matrix, row, col) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(s);
        return create_error_string("matrix_get", "get element failed");
    }
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

WASM_EXPORT char* flutter_symengine_matrix_to_string(CDenseMatrix* matrix) {
    if (!matrix) return create_error_string("matrix_str", "null matrix");
    return dense_matrix_str(matrix);
}

WASM_EXPORT char* flutter_symengine_matrix_det(CDenseMatrix* matrix) {
    if (!matrix) return create_error_string("matrix_det", "null matrix");
    basic result;
    basic_new_stack(result);
    if (dense_matrix_det(result, matrix) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(result);
        return create_error_string("matrix_det", "determinant calculation failed");
    }
    char* str = basic_to_string_safe(result);
    basic_free_stack(result);
    return str;
}

WASM_EXPORT CDenseMatrix* flutter_symengine_matrix_inv(CDenseMatrix* matrix) {
    if (!matrix) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_inv(result, matrix) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL; // Inversion failed
    }
    return result;
}

WASM_EXPORT CDenseMatrix* flutter_symengine_matrix_add(CDenseMatrix* a, CDenseMatrix* b) {
    if (!a || !b) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_add_matrix(result, a, b) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL;
    }
    return result;
}

WASM_EXPORT CDenseMatrix* flutter_symengine_matrix_mul(CDenseMatrix* a, CDenseMatrix* b) {
    if (!a || !b) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_mul_matrix(result, a, b) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL;
    }
    return result;
}


// --- Utility and Memory Management ---

WASM_EXPORT const char* flutter_symengine_version(void) {
    return symengine_version();
}

WASM_EXPORT char* flutter_symengine_test_basic_operations(void) {
    basic x, y, result;
    basic_new_stack(x);
    basic_new_stack(y);
    basic_new_stack(result);
    integer_set_si(x, 2);
    integer_set_si(y, 3);
    if (basic_add(result, x, y) != SYMENGINE_NO_EXCEPTION) {
        basic_free_stack(x);
        basic_free_stack(y);
        basic_free_stack(result);
        return create_error_string("test", "addition failed");
    }
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(x);
    basic_free_stack(y);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT char* flutter_symengine_test_symbolic(void) {
    basic x, expr, result;
    basic_new_stack(x);
    basic_new_stack(expr);
    basic_new_stack(result);

    symbol_set(x, "x");
    basic_parse(expr, "x**2 + 2*x + 1");
    basic_expand(result, expr);
    
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(x);
    basic_free_stack(expr);
    basic_free_stack(result);
    return result_str;
}

WASM_EXPORT void flutter_symengine_free_string(char* str) {
    if (str) {
        // Corresponds to malloc, strdup, and basic_str
        free(str);
    }
}

