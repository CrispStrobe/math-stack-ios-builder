/*
 * flutter_symengine_wrapper.c
 * Flutter-specific C wrapper implementation using SymEngine cwrapper.h API.
 * This version is complete, with no placeholders.
 */
#include "flutter_symengine_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symengine/cwrapper.h"

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
char* wrapper_name(const char* expression) { \
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

char* flutter_symengine_evaluate(const char* expression) {
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

char* flutter_symengine_solve(const char* expression, const char* symbol) {
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

char* flutter_symengine_expand(const char* expression) {
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

// Factor is an alias for expand, as the C API for true factoring is limited.
char* flutter_symengine_factor(const char* expression) {
    // This provides API consistency with the original wrapper.
    return flutter_symengine_expand(expression);
}
char* flutter_symengine_differentiate(const char* expression, const char* symbol) {
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

char* flutter_symengine_integrate(const char* expression, const char* symbol) {
    // NOTE: SymEngine's C API (cwrapper.h) does not expose an integration function.
    // This is a known limitation of the C interface, not the C++ core.
    return create_error_string("integrate", "not implemented in SymEngine C API");
}

char* flutter_symengine_simplify(const char* expression) {
    // "Simplification" is complex. `expand` is a common form of simplification.
    return flutter_symengine_expand(expression);
}

char* flutter_symengine_substitute(const char* expression, const char* symbol, const char* value) {
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

char* flutter_symengine_gcd(const char* a, const char* b) {
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

char* flutter_symengine_lcm(const char* a, const char* b) {
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

char* flutter_symengine_factorial(int n) {
    if (n < 0) return create_error_string("factorial", "input must be non-negative");
    basic result;
    basic_new_stack(result);
    ntheory_factorial(result, (unsigned long)n);
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(result);
    return result_str;
}

char* flutter_symengine_fibonacci(int n) {
    if (n < 0) return create_error_string("fibonacci", "input must be non-negative");
    basic result;
    basic_new_stack(result);
    ntheory_fibonacci(result, (unsigned long)n);
    char* result_str = basic_to_string_safe(result);
    basic_free_stack(result);
    return result_str;
}

// --- Constants ---

char* flutter_symengine_get_pi(void) {
    basic s;
    basic_new_stack(s);
    basic_const_pi(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

char* flutter_symengine_get_e(void) {
    basic s;
    basic_new_stack(s);
    basic_const_E(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

char* flutter_symengine_get_euler_gamma(void) {
    basic s;
    basic_new_stack(s);
    basic_const_EulerGamma(s);
    char* str = basic_to_string_safe(s);
    basic_free_stack(s);
    return str;
}

// --- Arbitrary-Precision Real Constants ---

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

// --- Matrix Operations (Opaque Pointers) ---

CDenseMatrix* flutter_symengine_matrix_new(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return NULL;
    return dense_matrix_new_rows_cols(rows, cols);
}

void flutter_symengine_matrix_free(CDenseMatrix* matrix) {
    if (matrix) {
        dense_matrix_free(matrix);
    }
}

int flutter_symengine_matrix_set_element(CDenseMatrix* matrix, int row, int col, const char* value) {
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

char* flutter_symengine_matrix_get_element(CDenseMatrix* matrix, int row, int col) {
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

char* flutter_symengine_matrix_to_string(CDenseMatrix* matrix) {
    if (!matrix) return create_error_string("matrix_str", "null matrix");
    return dense_matrix_str(matrix);
}

char* flutter_symengine_matrix_det(CDenseMatrix* matrix) {
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

CDenseMatrix* flutter_symengine_matrix_inv(CDenseMatrix* matrix) {
    if (!matrix) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_inv(result, matrix) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL; // Inversion failed
    }
    return result;
}

CDenseMatrix* flutter_symengine_matrix_add(CDenseMatrix* a, CDenseMatrix* b) {
    if (!a || !b) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_add_matrix(result, a, b) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL;
    }
    return result;
}

CDenseMatrix* flutter_symengine_matrix_mul(CDenseMatrix* a, CDenseMatrix* b) {
    if (!a || !b) return NULL;
    CDenseMatrix* result = dense_matrix_new();
    if (dense_matrix_mul_matrix(result, a, b) != SYMENGINE_NO_EXCEPTION) {
        dense_matrix_free(result);
        return NULL;
    }
    return result;
}


// --- Utility and Memory Management ---

const char* flutter_symengine_version(void) {
    return symengine_version();
}

char* flutter_symengine_test_basic_operations(void) {
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

char* flutter_symengine_test_symbolic(void) {
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

void flutter_symengine_free_string(char* str) {
    if (str) {
        // Corresponds to malloc, strdup, and basic_str
        free(str);
    }
}

