# LGPL compliance for the math stack

This build system compiles **GMP, MPFR, MPC** (all **LGPL-3.0-or-later**)
and **FLINT** (**LGPL-2.1-or-later**), plus **SymEngine** (MIT, no copyleft).
The LGPL libraries carry an obligation that MIT/BSD ones don't: whoever
receives your app must be able to **relink it against a modified version of
the LGPL library**. This document explains the two established ways to satisfy
that, and which one to pick.

The obligation is triggered by *distribution* (App Store, a download, TestFlight
to people outside your org). It does not apply to purely private/in-house use.

## The tension on iOS specifically

The classic way to satisfy the relink requirement -- ship the LGPL parts as a
**dynamic** library the user can swap out -- is awkward on iOS: Apple wants
everything code-signed and historically disallowed loose `.dylib`s. It IS
doable (dynamic libraries wrapped as embedded `.framework`s are accepted), just
more work than static linking. So there are two routes, and this repo can serve
both.

## Route A — static link + copyleft the whole app (what CrispCalc does)

LGPLv3 §4 (and the LGPLv2.1 equivalent) explicitly permits an alternative to
providing a relink mechanism: **release the entire combined work under the GPL
or AGPL, with complete corresponding source.** If your app is going to be open
source under (A)GPL-3.0 anyway, this is the simplest path and static linking is
fine.

Requirements:
- App licensed GPL-3.0 or AGPL-3.0 (AGPL if it's network-facing).
- **Complete corresponding source publicly available** (a public repo link in
  the app satisfies this -- an actual working link, not just a promise).
- Attribution: reproduce/point to each library's license (see the
  `assets/licenses/` pattern in CrispCalc).

The default build scripts here (`--enable-static --disable-shared`) produce
static frameworks, which is exactly what Route A wants.

## Route B — dynamic (relinkable) frameworks + keep your app permissive

If your app must stay **permissive or proprietary** (MIT/BSD/closed), you cannot
use Route A -- you have to give users the relink capability instead. On iOS/macOS
that means shipping GMP/MPFR/MPC/FLINT as **dynamic `.framework`s embedded in the
app bundle** (Xcode target → Frameworks → "Embed & Sign"), so a user could in
principle drop in their own rebuilt library and re-sign the app.

Requirements:
- LGPL parts are **dynamically** linked (embedded dynamic frameworks, not baked
  into the main executable).
- You provide, or point to, the **object files / build recipe** for your app
  sufficient to relink -- in practice: publish the exact library version + these
  build scripts (this repo), plus your app's own linkable objects or a documented
  way to rebuild. Publishing this repo + the pinned versions covers the library
  side.
- SymEngine and your own wrapper can still be static; only the LGPL libraries
  must be the relinkable dynamic ones.
- Attribution as in Route A.

### Building the dynamic variant

`build_gmp.sh` is the **worked reference** for the shared path: it takes a
`LINKAGE` env var (default `static`, which reproduces the existing behavior
byte-for-byte). `LINKAGE=shared` configures with `--enable-shared
--disable-static` and hands the resulting per-arch `.dylib`s to
`wrap_dynamic_framework.sh`, which fixes the install name to
`@rpath/GMP.framework/GMP`, wraps each into an embeddable `.framework` bundle
(binary + Info.plist + Headers), and assembles the dynamic `.xcframework`:

```bash
LINKAGE=shared ./build_gmp.sh
```

The other three libraries need the **same one-line configure change** --
swap `--enable-static --disable-shared` for `--enable-shared --disable-static`
in their `configure_args` -- and then the identical
`wrap_dynamic_framework.sh` call. They are intentionally NOT wired for
`LINKAGE` yet, to avoid touching the scripts CrispCalc's shipping static build
depends on; extend them when a real permissive consumer needs it.

**Status of this route:** the shared configure+compile has been validated for
GMP on the iOS simulator (a clean `arm64` `libgmp.dylib` is produced). The full
multi-platform, all-four-libraries framework-wrapping run has NOT been executed
end-to-end here, and no permissive app has linked+embedded+run the result on a
device yet. FLINT additionally links against GMP/MPFR/MPC, so its dynamic build
needs their dylibs' `@rpath` install names resolvable at link time -- an extra
wrinkle to work through when you do the full build. Treat Route B as "approach
proven and documented, final artifacts not yet produced/verified" -- do a full
build + an on-device test with your actual app before shipping.

## Quick decision

- App is (A)GPL and open source → **Route A** (static, default scripts). Done.
- App must be permissive/proprietary → **Route B** (dynamic frameworks). More
  work; verify end-to-end before release.
