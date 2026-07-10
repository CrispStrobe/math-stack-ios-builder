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

**All four LGPL libraries are wired for the shared path.** Each `build_*.sh`
takes a `LINKAGE` env var (default `static`, which reproduces the existing
behavior byte-for-byte). Build them in dependency order:

```bash
LINKAGE=shared ./build_gmp.sh    # no deps
LINKAGE=shared ./build_mpfr.sh   # links GMP
LINKAGE=shared ./build_mpc.sh    # links GMP + MPFR
LINKAGE=shared ./build_flint.sh  # links GMP + MPFR
```

`LINKAGE=shared` configures each with `--enable-shared --disable-static` and
hands the per-arch `.dylib`s to `wrap_dynamic_framework.sh`, which fixes the
install name to `@rpath/<Name>.framework/<Name>`, wraps each into an embeddable
`.framework` bundle (binary + Info.plist + Headers), and assembles the dynamic
`.xcframework`.

**How the inter-library dependencies stay relinkable:** the tricky part is that
MPFR links GMP, MPC links GMP+MPFR, and FLINT links GMP+MPFR. Each library's
install name is rewritten to `@rpath/<Name>.framework/<Name>` on the
install-tree dylib *before* the next library links against it, so a dependent
records e.g. `@rpath/GMP.framework/GMP` (not a raw `/usr/local/...` path) as its
`LC_LOAD_DYLIB`. At runtime all the frameworks resolve via the app bundle's
Frameworks `@rpath` -- which is exactly what makes them independently swappable
(the LGPL relink point). **The app must "Embed & Sign" every one of them**
(GMP, MPFR, MPC, FLINT frameworks), not just link them.

**Verification status:**
- The full four-library shared chain has been built end-to-end and inspected.
  All four produce 3-slice dynamic `.xcframework`s (ios-arm64, ios-simulator
  universal, macos universal); every slice is a genuine `dylib` with install id
  `@rpath/<Name>.framework/<Name>`; and the inter-library dependencies resolve
  correctly (verified with `otool -l`):
  - MPFR records `@rpath/GMP.framework/GMP`
  - MPC records `@rpath/GMP.framework/GMP` and `@rpath/MPFR.framework/MPFR`
  - FLINT records `@rpath/GMP.framework/GMP` and `@rpath/MPFR.framework/MPFR`
- What has NOT been done: no permissive app has yet linked, embedded, signed,
  and run these frameworks on a real device / App Store submission. The dynamic
  frameworks are built and structurally correct; do that end-to-end app test
  (Embed & Sign all four, run on device) before shipping a real release.
- The committed binaries in this repo remain the **static** build (Route A,
  what CrispCalc ships). Route B frameworks are produced on demand by running
  `LINKAGE=shared ./build_*.sh`; they are not committed.

## Quick decision

- App is (A)GPL and open source → **Route A** (static, default scripts). Done.
- App must be permissive/proprietary → **Route B** (dynamic frameworks, all
  four wired). Run the full chain + an on-device test before release.
