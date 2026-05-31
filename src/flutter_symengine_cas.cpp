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
#include <symengine/add.h>
#include <symengine/functions.h>
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

// --- trig simplification rewrite engine -----------------------------------
// Applies trigonometric identities bottom-up on the expression tree.
// Uses a fixed-point loop (max 5 iterations) until the expression stabilizes.
static RCP<const Basic> trig_simplify_once(RCP<const Basic> expr);

// Recursively simplify children first, then apply rules to parent.
static RCP<const Basic> trig_simplify_recurse(RCP<const Basic> expr)
{
    using SymEngine::Add;
    using SymEngine::add;
    using SymEngine::Cos;
    using SymEngine::cos;
    using SymEngine::div;
    using SymEngine::eq;
    using SymEngine::Mul;
    using SymEngine::Number;
    using SymEngine::one;
    using SymEngine::Pow;
    using SymEngine::Sin;
    using SymEngine::sin;
    using SymEngine::Tan;
    using SymEngine::two;
    using SymEngine::zero;

    // --- Bottom-up: simplify children first ---
    if (is_a<Add>(*expr)) {
        const Add &a = static_cast<const Add &>(*expr);
        SymEngine::vec_basic terms;
        if (!eq(*a.get_coef(), *zero)) {
            terms.push_back(a.get_coef());
        }
        for (const auto &kv : a.get_dict()) {
            RCP<const Basic> term = mul(kv.second, kv.first);
            terms.push_back(trig_simplify_recurse(term));
        }
        expr = add(terms);
    } else if (is_a<Mul>(*expr)) {
        const Mul &m = static_cast<const Mul &>(*expr);
        SymEngine::vec_basic factors;
        if (!eq(*m.get_coef(), *one)) {
            factors.push_back(m.get_coef());
        }
        for (const auto &kv : m.get_dict()) {
            RCP<const Basic> factor = pow(kv.first, kv.second);
            factors.push_back(trig_simplify_recurse(factor));
        }
        expr = mul(factors);
    } else if (is_a<Pow>(*expr)) {
        const Pow &p = static_cast<const Pow &>(*expr);
        RCP<const Basic> base = trig_simplify_recurse(p.get_base());
        RCP<const Basic> exp = trig_simplify_recurse(p.get_exp());
        expr = pow(base, exp);
    } else if (is_a<Sin>(*expr)) {
        const Sin &s = static_cast<const Sin &>(*expr);
        expr = sin(trig_simplify_recurse(s.get_arg()));
    } else if (is_a<Cos>(*expr)) {
        const Cos &c = static_cast<const Cos &>(*expr);
        expr = cos(trig_simplify_recurse(c.get_arg()));
    } else if (is_a<Tan>(*expr)) {
        const Tan &t = static_cast<const Tan &>(*expr);
        expr = SymEngine::tan(trig_simplify_recurse(t.get_arg()));
    }

    // --- Now apply trig rules to this node ---
    return trig_simplify_once(expr);
}

// Helper: check if expr is sin^2(u) or cos^2(u). Returns the argument u
// and whether it's sin (true) or cos (false). Returns false if not a match.
static bool is_trig_squared(const RCP<const Basic> &expr,
                            RCP<const Basic> &arg, bool &is_sin_type)
{
    using SymEngine::Cos;
    using SymEngine::Pow;
    using SymEngine::Sin;

    if (!is_a<Pow>(*expr)) return false;
    const Pow &p = static_cast<const Pow &>(*expr);
    if (!SymEngine::eq(*p.get_exp(), *integer(2))) return false;
    if (is_a<Sin>(*p.get_base())) {
        arg = static_cast<const Sin &>(*p.get_base()).get_arg();
        is_sin_type = true;
        return true;
    }
    if (is_a<Cos>(*p.get_base())) {
        arg = static_cast<const Cos &>(*p.get_base()).get_arg();
        is_sin_type = false;
        return true;
    }
    return false;
}

// Helper: check if expr is tan^2(u). Returns arg u.
static bool is_tan_squared(const RCP<const Basic> &expr,
                           RCP<const Basic> &arg)
{
    using SymEngine::Pow;
    using SymEngine::Tan;

    if (!is_a<Pow>(*expr)) return false;
    const Pow &p = static_cast<const Pow &>(*expr);
    if (!SymEngine::eq(*p.get_exp(), *integer(2))) return false;
    if (is_a<Tan>(*p.get_base())) {
        arg = static_cast<const Tan &>(*p.get_base()).get_arg();
        return true;
    }
    return false;
}

// Apply trig identities to a single node (after children are simplified).
static RCP<const Basic> trig_simplify_once(RCP<const Basic> expr)
{
    using SymEngine::Add;
    using SymEngine::add;
    using SymEngine::Cos;
    using SymEngine::cos;
    using SymEngine::div;
    using SymEngine::eq;
    using SymEngine::Mul;
    using SymEngine::Number;
    using SymEngine::one;
    using SymEngine::Pow;
    using SymEngine::Sin;
    using SymEngine::sin;
    using SymEngine::Tan;
    using SymEngine::two;
    using SymEngine::zero;

    // --- Rule 3: Double angle in Mul: 2*sin(u)*cos(u) -> sin(2*u) ---
    if (is_a<Mul>(*expr)) {
        const Mul &m = static_cast<const Mul &>(*expr);
        // Look for sin(u) and cos(u) factors in the Mul dict
        RCP<const Basic> sin_arg, cos_arg;
        bool has_sin = false, has_cos = false;
        for (const auto &kv : m.get_dict()) {
            if (is_a<Sin>(*kv.first) && eq(*kv.second, *one)) {
                sin_arg = static_cast<const Sin &>(*kv.first).get_arg();
                has_sin = true;
            } else if (is_a<Cos>(*kv.first) && eq(*kv.second, *one)) {
                cos_arg = static_cast<const Cos &>(*kv.first).get_arg();
                has_cos = true;
            }
        }
        if (has_sin && has_cos && eq(*sin_arg, *cos_arg)) {
            // Check if coefficient is 2*k for some k
            RCP<const Basic> coef = m.get_coef();
            // Build remaining factors (everything except sin and cos)
            SymEngine::vec_basic remaining;
            for (const auto &kv : m.get_dict()) {
                if (is_a<Sin>(*kv.first) && eq(*kv.second, *one) &&
                    eq(*static_cast<const Sin &>(*kv.first).get_arg(), *sin_arg))
                    continue;
                if (is_a<Cos>(*kv.first) && eq(*kv.second, *one) &&
                    eq(*static_cast<const Cos &>(*kv.first).get_arg(), *cos_arg))
                    continue;
                remaining.push_back(pow(kv.first, kv.second));
            }
            // coef must contain factor of 2
            if (is_a<SymEngine::Integer>(*coef)) {
                auto &ci = static_cast<const SymEngine::Integer &>(*coef);
                auto val = ci.as_integer_class();
                if (val % 2 == 0) {
                    RCP<const Basic> half_coef = integer(val / 2);
                    remaining.insert(remaining.begin(), half_coef);
                    remaining.push_back(sin(mul(two, sin_arg)));
                    return mul(remaining);
                }
            }
        }
    }

    // --- Rules 1, 2, 5 apply to Add nodes ---
    if (!is_a<Add>(*expr)) {
        // --- Rule 4: Power reduction for standalone sin²/cos² ---
        // sin²(u) -> (1 - cos(2u))/2, cos²(u) -> (1 + cos(2u))/2
        RCP<const Basic> trig_arg;
        bool is_sin_type;
        if (is_trig_squared(expr, trig_arg, is_sin_type)) {
            RCP<const Basic> cos2u = cos(mul(two, trig_arg));
            if (is_sin_type) {
                return div(SymEngine::sub(one, cos2u), two);
            } else {
                return div(add(one, cos2u), two);
            }
        }
        return expr;
    }

    const Add &a = static_cast<const Add &>(*expr);

    // Collect terms as (base, coeff) pairs for analysis.
    // Each term in Add is coeff * base (from get_dict()).
    // We look for pairs c*sin²(u) and c*cos²(u) with same c and u.

    // Build a map: for each trig arg u, track sin²(u) coeff and cos²(u) coeff.
    struct TrigPair {
        RCP<const SymEngine::Number> sin2_coeff; // null if not present
        RCP<const SymEngine::Number> cos2_coeff; // null if not present
    };
    // Map from arg (as string for easy comparison) to info.
    // We use vec_basic keys for proper equality.
    std::vector<std::pair<RCP<const Basic>, TrigPair>> trig_pairs;

    auto find_or_add = [&](const RCP<const Basic> &arg) -> TrigPair & {
        for (auto &kv : trig_pairs) {
            if (eq(*kv.first, *arg)) return kv.second;
        }
        trig_pairs.push_back({arg, {nullptr, nullptr}});
        return trig_pairs.back().second;
    };

    // Also track tan²(u) + 1 patterns
    struct TanInfo {
        RCP<const Basic> arg;
        RCP<const SymEngine::Number> coeff;
    };
    std::vector<TanInfo> tan_squares;

    // Scan the Add dict
    for (const auto &kv : a.get_dict()) {
        // kv.first is the base, kv.second is the coefficient (a Number)
        RCP<const Basic> base = kv.first;
        RCP<const SymEngine::Number> coeff = kv.second;

        RCP<const Basic> trig_arg;
        bool is_sin_type;
        if (is_trig_squared(base, trig_arg, is_sin_type)) {
            TrigPair &tp = find_or_add(trig_arg);
            if (is_sin_type) {
                tp.sin2_coeff = coeff;
            } else {
                tp.cos2_coeff = coeff;
            }
        }

        RCP<const Basic> tan_arg;
        if (is_tan_squared(base, tan_arg)) {
            tan_squares.push_back({tan_arg, coeff});
        }
    }

    bool changed = false;

    // --- Rule 1/2: Pythagorean identity ---
    // For each arg where we have both sin²(u) and cos²(u) with same coeff c:
    // replace c*sin²(u) + c*cos²(u) with c.
    SymEngine::vec_basic new_terms;
    // Start with the constant
    RCP<const Basic> constant = a.get_coef();

    // Track which bases have been consumed by Pythagorean
    std::vector<RCP<const Basic>> consumed_bases;

    for (const auto &tp : trig_pairs) {
        if (tp.second.sin2_coeff && tp.second.cos2_coeff &&
            eq(*tp.second.sin2_coeff, *tp.second.cos2_coeff)) {
            // Found a Pythagorean pair! Replace with coeff.
            RCP<const SymEngine::Number> c = tp.second.sin2_coeff;
            constant = SymEngine::addnum(
                SymEngine::rcp_static_cast<const Number>(constant), c);
            changed = true;
            // Mark both sin²(u) and cos²(u) as consumed
            consumed_bases.push_back(pow(sin(tp.first), integer(2)));
            consumed_bases.push_back(pow(cos(tp.first), integer(2)));
        }
    }

    // --- Rule 5: tan²(u) + 1 -> 1/cos²(u) ---
    // Check if constant term contains a 1 that pairs with a tan²(u) of coeff 1
    // More generally: c*tan²(u) + c -> c/cos²(u)
    // We look for tan²(u) with coeff c where the overall constant has at least c.
    for (const auto &ti : tan_squares) {
        // Check if this tan² base is already consumed
        bool already_consumed = false;
        RCP<const Basic> tan_sq_base = pow(SymEngine::tan(ti.arg), integer(2));
        for (const auto &cb : consumed_bases) {
            if (eq(*cb, *tan_sq_base)) { already_consumed = true; break; }
        }
        if (already_consumed) continue;

        // We need c from the constant. The constant must have ti.coeff available.
        if (is_a<SymEngine::Integer>(*constant) && is_a<SymEngine::Integer>(*ti.coeff)) {
            auto const_val = static_cast<const SymEngine::Integer &>(*constant).as_integer_class();
            auto coeff_val = static_cast<const SymEngine::Integer &>(*ti.coeff).as_integer_class();
            if (coeff_val > 0 && const_val >= coeff_val) {
                // Apply: c*tan²(u) + c -> c/cos²(u)
                constant = integer(const_val - coeff_val);
                RCP<const Basic> sec2 = pow(cos(ti.arg), integer(-2));
                new_terms.push_back(mul(ti.coeff, sec2));
                consumed_bases.push_back(tan_sq_base);
                changed = true;
            }
        }
    }

    if (!changed) return expr;

    // Rebuild the Add with unconsumed terms
    if (!eq(*constant, *zero)) {
        new_terms.push_back(constant);
    }
    for (const auto &kv : a.get_dict()) {
        bool consumed = false;
        for (const auto &cb : consumed_bases) {
            if (eq(*kv.first, *cb)) { consumed = true; break; }
        }
        if (!consumed) {
            new_terms.push_back(mul(kv.second, kv.first));
        }
    }

    if (new_terms.empty()) return zero;
    return add(new_terms);
}

// Top-level trig simplify: fixed-point loop.
static RCP<const Basic> trig_simplify(RCP<const Basic> expr)
{
    for (int i = 0; i < 5; i++) {
        RCP<const Basic> simplified = trig_simplify_recurse(expr);
        if (SymEngine::eq(*simplified, *expr)) break;
        expr = simplified;
    }
    return expr;
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

        RCP<const Basic> result = SymEngine::simplify(expr);
        result = trig_simplify(result);
        return cas_dup(str(*result));
    } catch (...) {
        // Anything simplify() can't handle falls back to expand.
        return flutter_symengine_expand(expression);
    }
}
