/*
 * flutter_symengine_cas.cpp
 *
 * C++ implementations the C cwrapper.h API can't express. Compiled for
 * native builds and the FLINT-enabled WASM build (never the boostmp WASM,
 * which has no FLINT). The result strings are malloc()'d so the existing
 * Dart FFI free path works.
 *
 *  - flutter_symengine_factor_cpp: real polynomial factorization.
 *      * univariate over Z  -> SymEngine UIntPolyFlint + FLINT fmpz_poly_factor
 *      * multivariate over Z -> FLINT fmpz_mpoly_factor (SymEngine has no
 *        multivariate FLINT wrapper, so we bridge MIntPoly -> fmpz_mpoly)
 *      * anything else       -> expand (fallback)
 *  - flutter_symengine_simplify_cpp: SymEngine's real simplify() (replaces
 *      the historical expand-alias), with expand as a fallback.
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
#include <symengine/symbol.h>
#include <symengine/simplify.h>
#include <symengine/visitor.h>
#include <symengine/number.h>
#include <symengine/constants.h>
#include <symengine/polys/basic_conversions.h>
#include <symengine/polys/uintpoly_flint.h>
#include <symengine/polys/msymenginepoly.h>
#include <symengine/polys/cancel.h>
#include <symengine/series.h>
#include <symengine/solve.h>
#include <symengine/add.h>
#include <symengine/subs.h>
#include <symengine/matrix.h>

#include <flint/fmpz.h>
#include <flint/fmpz_mpoly.h>
#include <flint/fmpz_mpoly_factor.h>

using SymEngine::Basic;
using SymEngine::free_symbols;
using SymEngine::from_basic;
using SymEngine::integer;
using SymEngine::integer_class;
using SymEngine::is_a;
using SymEngine::MIntPoly;
using SymEngine::mul;
using SymEngine::parse;
using SymEngine::pow;
using SymEngine::RCP;
using SymEngine::set_basic;
using SymEngine::str;
using SymEngine::Symbol;
using SymEngine::UIntPolyFlint;

extern "C" char *flutter_symengine_expand(const char *expression);

static char *cas_dup(const std::string &s)
{
    char *out = static_cast<char *>(malloc(s.size() + 1));
    if (out) {
        std::memcpy(out, s.c_str(), s.size() + 1);
    }
    return out;
}

// --- univariate over Z via SymEngine + FLINT fmpz_poly_factor -----------
static bool factor_univariate(const RCP<const Basic> &expr, std::string &out)
{
    RCP<const UIntPolyFlint> poly;
    try {
        poly = from_basic<UIntPolyFlint>(expr, true);
    } catch (...) {
        return false;
    }
    auto facs = SymEngine::factors(*poly);
    if (facs.empty()) {
        return false;
    }
    RCP<const Basic> product;
    bool first = true;
    for (const auto &fe : facs) {
        RCP<const Basic> base = fe.first->as_symbolic();
        RCP<const Basic> term = (fe.second == 1)
                                    ? base
                                    : pow(base, integer(integer_class(fe.second)));
        product = first ? term : mul(product, term);
        first = false;
    }
    out = str(*product);
    return true;
}

// --- multivariate over Z via FLINT fmpz_mpoly_factor -------------------
// SymEngine has no multivariate FLINT wrapper, so convert MIntPoly's
// (exponent-vector -> integer) dict into a FLINT fmpz_mpoly, factor it, and
// pretty-print each factor with the original variable names.
//
// NATIVE ONLY: FLINT's fmpz_mpoly_factor aborts under wasm32 (the univariate
// fmpz_poly_factor is fine). On WASM, multivariate input falls through to
// expand instead — a graceful degradation rather than a hard crash.
#ifndef __EMSCRIPTEN__
static bool factor_multivariate(const RCP<const Basic> &expr, std::string &out)
{
    RCP<const MIntPoly> poly;
    try {
        set_basic gens = free_symbols(*expr);
        if (gens.empty()) {
            return false;
        }
        poly = from_basic<MIntPoly>(expr, gens, true);
    } catch (...) {
        return false;
    }

    const set_basic &vars = poly->get_vars();
    const slong nv = static_cast<slong>(vars.size());
    if (nv < 1) {
        return false;
    }

    std::vector<std::string> names;
    names.reserve(vars.size());
    for (const auto &v : vars) {
        if (!is_a<Symbol>(*v)) {
            return false; // generator isn't a plain symbol — bail
        }
        names.push_back(static_cast<const Symbol &>(*v).get_name());
    }
    std::vector<const char *> cnames;
    cnames.reserve(names.size());
    for (const auto &n : names) {
        cnames.push_back(n.c_str());
    }

    fmpz_mpoly_ctx_t ctx;
    fmpz_mpoly_ctx_init(ctx, nv, ORD_LEX);
    fmpz_mpoly_t A;
    fmpz_mpoly_init(A, ctx);

    for (const auto &kv : poly->get_poly().get_dict()) {
        const auto &e = kv.first; // vec_uint, aligned to `vars` order
        std::vector<ulong> exp(static_cast<size_t>(nv), 0);
        for (slong i = 0; i < nv && i < static_cast<slong>(e.size()); i++) {
            exp[static_cast<size_t>(i)] = static_cast<ulong>(e[static_cast<size_t>(i)]);
        }
        fmpz_t c;
        fmpz_init(c);
        // Portable across INTEGER_CLASS (flint native / gmp wasm): decimal str.
        const std::string cs = str(*integer(kv.second));
        fmpz_set_str(c, cs.c_str(), 10);
        fmpz_mpoly_push_term_fmpz_ui(A, c, exp.data(), ctx);
        fmpz_clear(c);
    }
    fmpz_mpoly_sort_terms(A, ctx);
    fmpz_mpoly_combine_like_terms(A, ctx);

    fmpz_mpoly_factor_t fac;
    fmpz_mpoly_factor_init(fac, ctx);
    bool ok = fmpz_mpoly_factor(fac, A, ctx) != 0;

    if (ok) {
        const slong n = fmpz_mpoly_factor_length(fac, ctx);
        fmpz_t con;
        fmpz_init(con);
        fmpz_mpoly_factor_get_constant_fmpz(con, fac, ctx);
        char *conStr = fmpz_get_str(NULL, 10, con);

        std::vector<std::string> pieces;
        if (std::strcmp(conStr, "1") != 0) {
            pieces.push_back(conStr);
        }
        for (slong i = 0; i < n; i++) {
            fmpz_mpoly_t base;
            fmpz_mpoly_init(base, ctx);
            fmpz_mpoly_factor_get_base(base, fac, i, ctx);
            const slong e = fmpz_mpoly_factor_get_exp_si(fac, i, ctx);
            char *ps = fmpz_mpoly_get_str_pretty(base, cnames.data(), ctx);
            std::string piece = std::string("(") + ps + ")";
            if (e != 1) {
                piece += "^" + std::to_string(e);
            }
            pieces.push_back(piece);
            flint_free(ps);
            fmpz_mpoly_clear(base, ctx);
        }
        flint_free(conStr);
        fmpz_clear(con);

        // A single bare irreducible polynomial: drop the redundant parens.
        if (pieces.size() == 1 && pieces[0].size() > 1 && pieces[0].front() == '(' &&
            pieces[0].back() == ')') {
            out = pieces[0].substr(1, pieces[0].size() - 2);
        } else {
            out.clear();
            for (size_t i = 0; i < pieces.size(); i++) {
                if (i) {
                    out += "*";
                }
                out += pieces[i];
            }
        }
    }

    fmpz_mpoly_factor_clear(fac, ctx);
    fmpz_mpoly_clear(A, ctx);
    fmpz_mpoly_ctx_clear(ctx);
    return ok && !out.empty();
}
#endif // !__EMSCRIPTEN__

extern "C" char *flutter_symengine_factor_cpp(const char *expression)
{
    try {
        RCP<const Basic> expr = parse(expression);
        std::string out;
        if (factor_univariate(expr, out)) {
            return cas_dup(out);
        }
#ifndef __EMSCRIPTEN__
        if (factor_multivariate(expr, out)) {
            return cas_dup(out);
        }
#endif
        return flutter_symengine_expand(expression);
    } catch (const std::exception &ex) {
        return cas_dup(std::string("Error in factor: ") + ex.what());
    } catch (...) {
        return cas_dup("Error in factor: unknown error");
    }
}

// --- real simplify (replacing the expand-alias) -------------------------
// SymEngine's simplify() collects like terms but doesn't cancel rational
// functions, so we add the canonical case — (x^2-1)/(x-1) -> x+1 — via the
// univariate cancel() (gcd of numerator/denominator), then fall back to
// simplify() for everything else.
extern "C" char *flutter_symengine_simplify_cpp(const char *expression)
{
    using SymEngine::as_numer_denom;
    using SymEngine::cancel;
    using SymEngine::div;
    using SymEngine::eq;
    using SymEngine::is_a_Number;
    using SymEngine::one;
    using SymEngine::outArg;
    try {
        RCP<const Basic> expr = parse(expression);

        RCP<const Basic> num, den;
        as_numer_denom(expr, outArg(num), outArg(den));
        // Only attempt cancellation on a genuine rational function with
        // non-constant numerator AND denominator (cancel() dereferences an
        // empty generator set for constants).
        if (!eq(*den, *one) && !is_a_Number(*num) && !is_a_Number(*den)) {
            try {
                RCP<const UIntPolyFlint> rn, rd, g;
                cancel<UIntPolyFlint>(num, den, outArg(rn), outArg(rd), outArg(g));
                if (!rn.is_null() && !rd.is_null()) {
                    RCP<const Basic> res =
                        div(rn->as_symbolic(), rd->as_symbolic());
                    return cas_dup(str(*res));
                }
            } catch (...) {
                // not a univariate integer rational — fall through
            }
        }

        return cas_dup(str(*SymEngine::simplify(expr)));
    } catch (...) {
        // Anything simplify() can't handle falls back to expand.
        return flutter_symengine_expand(expression);
    }
}

// --- Taylor/Maclaurin series via SymEngine's series() -------------------
// Expansion about `point`. SymEngine only expands about 0, so a non-zero
// point is handled by the standard shift: substitute x -> x + x0, expand
// about 0, then substitute x -> (x - x0) back into the truncated
// polynomial (the powers of (x - x0) stay unexpanded, which is the form
// users expect a Taylor polynomial in).
extern "C" char *flutter_symengine_series_cpp(const char *expression,
                                              const char *symbol,
                                              const char *point,
                                              int order)
{
    using SymEngine::add;
    using SymEngine::eq;
    using SymEngine::map_basic_basic;
    using SymEngine::sub;
    using SymEngine::zero;
    try {
        if (order < 1 || order > 64) {
            return cas_dup("Error in series: order must be in 1..64");
        }
        RCP<const Basic> expr = parse(expression);
        RCP<const Symbol> x = SymEngine::symbol(symbol);
        RCP<const Basic> x0 = parse(point);

        const bool shifted = !eq(*x0, *zero);
        if (shifted) {
            map_basic_basic shift{{x, add(x, x0)}};
            expr = expr->subs(shift);
        }
        auto ser = SymEngine::series(expr, x, static_cast<unsigned>(order));
        RCP<const Basic> poly = ser->as_basic();
        if (shifted) {
            map_basic_basic unshift{{x, sub(x, x0)}};
            poly = poly->subs(unshift);
        }
        return cas_dup(str(*poly));
    } catch (const std::exception &ex) {
        return cas_dup(std::string("Error in series: ") + ex.what());
    } catch (...) {
        return cas_dup("Error in series: not expandable at this point");
    }
}

// --- Symbolic linear-system solve via SymEngine's linsolve() ------------
// `equations`: ';'-separated. Each may be "lhs = rhs" or an expression
// implicitly equal to 0. `symbols`: ','-separated unknowns. Returns the
// solutions "[v1, v2, ...]" in the same order as `symbols`, or an error
// string (non-linear input, inconsistent/underdetermined system).
extern "C" char *flutter_symengine_linsolve_cpp(const char *equations,
                                                const char *symbols)
{
    using SymEngine::sub;
    using SymEngine::vec_basic;
    using SymEngine::vec_sym;
    try {
        vec_basic eqs;
        std::string eqstr(equations);
        size_t start = 0;
        while (start <= eqstr.size()) {
            size_t end = eqstr.find(';', start);
            if (end == std::string::npos) end = eqstr.size();
            std::string piece = eqstr.substr(start, end - start);
            // trim
            size_t a = piece.find_first_not_of(" \t");
            size_t b = piece.find_last_not_of(" \t");
            if (a != std::string::npos) {
                piece = piece.substr(a, b - a + 1);
                size_t eqpos = piece.find('=');
                RCP<const Basic> e;
                if (eqpos != std::string::npos) {
                    e = sub(parse(piece.substr(0, eqpos)),
                            parse(piece.substr(eqpos + 1)));
                } else {
                    e = parse(piece);
                }
                eqs.push_back(e);
            }
            start = end + 1;
        }

        vec_sym syms;
        std::string symstr(symbols);
        start = 0;
        while (start <= symstr.size()) {
            size_t end = symstr.find(',', start);
            if (end == std::string::npos) end = symstr.size();
            std::string piece = symstr.substr(start, end - start);
            size_t a = piece.find_first_not_of(" \t");
            size_t b = piece.find_last_not_of(" \t");
            if (a != std::string::npos) {
                syms.push_back(SymEngine::symbol(piece.substr(a, b - a + 1)));
            }
            start = end + 1;
        }

        if (eqs.empty() || syms.empty()) {
            return cas_dup("Error in linsolve: empty equations or symbols");
        }

        vec_basic sol = SymEngine::linsolve(eqs, syms);
        std::string out = "[";
        for (size_t i = 0; i < sol.size(); i++) {
            if (i) out += ", ";
            out += str(*sol[i]);
        }
        out += "]";
        return cas_dup(out);
    } catch (const std::exception &ex) {
        return cas_dup(std::string("Error in linsolve: ") + ex.what());
    } catch (...) {
        return cas_dup("Error in linsolve: system is not linear or has no "
                       "unique solution");
    }
}
