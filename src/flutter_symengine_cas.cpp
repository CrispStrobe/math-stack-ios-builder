/*
 * flutter_symengine_cas.cpp
 *
 * C++ implementations that the C cwrapper.h API can't express. SymEngine's
 * C interface exposes no polynomial factorization, but the C++ core does
 * (FLINT-backed: fmpz_poly_factor). This file provides a real `factor` for
 * native builds (which link FLINT). It is NOT compiled for the Emscripten /
 * WASM target, which uses INTEGER_CLASS=boostmp without FLINT — there the C
 * wrapper keeps the historical expand-alias.
 *
 * The result string is malloc()'d so the existing Dart FFI free path works,
 * exactly like the C wrapper's other return values.
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

#include <symengine/basic.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/parser.h>
#include <symengine/pow.h>
#include <symengine/printers.h>
#include <symengine/polys/basic_conversions.h>
#include <symengine/polys/uintpoly_flint.h>

using SymEngine::Basic;
using SymEngine::from_basic;
using SymEngine::integer;
using SymEngine::integer_class;
using SymEngine::mul;
using SymEngine::parse;
using SymEngine::pow;
using SymEngine::RCP;
using SymEngine::str;
using SymEngine::UIntPolyFlint;

// The C wrapper's expand — reused as the fall-back when the input isn't a
// univariate integer polynomial (multivariate, rational coeffs, etc.).
extern "C" char *flutter_symengine_expand(const char *expression);

static char *cas_dup(const std::string &s)
{
    char *out = static_cast<char *>(malloc(s.size() + 1));
    if (out) {
        std::memcpy(out, s.c_str(), s.size() + 1);
    }
    return out;
}

extern "C" char *flutter_symengine_factor_cpp(const char *expression)
{
    try {
        RCP<const Basic> expr = parse(expression);

        // Try to view the input as a univariate integer polynomial.
        // from_basic auto-detects the single generator and throws if the
        // expression is multivariate, non-polynomial, or has non-integer
        // coefficients (ex=true expands first so (x+1)^2 etc. convert).
        RCP<const UIntPolyFlint> poly;
        try {
            poly = from_basic<UIntPolyFlint>(expr, true);
        } catch (...) {
            return flutter_symengine_expand(expression);
        }

        // FLINT factorization: vector of (factor, multiplicity) pairs,
        // including a leading constant content factor when != 1.
        std::vector<std::pair<RCP<const UIntPolyFlint>, long>> facs
            = SymEngine::factors(*poly);
        if (facs.empty()) {
            return flutter_symengine_expand(expression);
        }

        // Rebuild a *product* expression (SymEngine's mul() does not
        // distribute, so the factored form is preserved in the string).
        RCP<const Basic> product;
        bool first = true;
        for (const auto &fe : facs) {
            RCP<const Basic> base = fe.first->as_symbolic();
            RCP<const Basic> term
                = (fe.second == 1)
                      ? base
                      : pow(base, integer(integer_class(fe.second)));
            product = first ? term : mul(product, term);
            first = false;
        }
        return cas_dup(str(*product));
    } catch (const std::exception &ex) {
        return cas_dup(std::string("Error in factor: ") + ex.what());
    } catch (...) {
        return cas_dup("Error in factor: unknown error");
    }
}
