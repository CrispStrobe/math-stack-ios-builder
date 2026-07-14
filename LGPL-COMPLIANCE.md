# LGPL compliance for the math stack

This build system compiles **GMP** (**LGPL-3.0-or-later / GPL-2.0-or-later**),
**MPFR** and **MPC** (**LGPL-3.0-or-later**), **FLINT**
(**LGPL-2.1-or-later**), plus **SymEngine** (MIT, no copyleft). The LGPL
libraries carry an obligation that MIT/BSD ones do not: whoever receives your
app must receive a practical way to **modify the LGPL library and use that
modified version with the application**. For static linking this usually means
providing enough application object files, source, build scripts, dependency
versions, and installation information to rebuild or relink the combined work.

This document is engineering guidance, not legal advice. Treat it as a checklist
for what the build system must support; get legal review before distributing a
commercial or App Store release that depends on the static native stack.

The obligation is triggered by *distribution* (App Store, a download, TestFlight
to people outside your org). It does not apply to purely private/in-house use.

## The tension on iOS specifically

The classic way to satisfy the relink requirement -- ship the LGPL parts as a
**dynamic** library the user can swap out -- is awkward on iOS: Apple wants
everything code-signed and historically disallowed loose `.dylib`s. It IS
doable (dynamic libraries wrapped as embedded `.framework`s are accepted), just
more work than static linking. So there are two routes, and this repo can serve
both.

## Route A — static link + complete rebuild/relink path

Static linking is compatible with LGPL distribution only if recipients can
replace the LGPL parts with modified builds. For LGPLv3 libraries, that means
the "Corresponding Application Code" and any needed installation information for
the combined work. For LGPLv2.1 libraries, that means object files and/or another
workable relink path. In practice, the cleanest way for an open-source app is to
publish the complete app source, exact dependency versions, build scripts, and
signing/rebuild instructions needed to produce a new app binary with modified
GMP/MPFR/MPC/FLINT libraries.

Licensing the whole app under GPL or AGPL can be a sensible policy choice for a
project that already wants copyleft, but it is not a magic substitute for the
LGPL relinkability obligation. AGPL is only relevant if you intentionally want
network-use source disclosure; it is not required by these LGPL libraries.

Requirements:
- **Complete corresponding source/build material available** for the application
  and the native stack: source, object files where needed, exact versions, build
  scripts, and enough instructions to rebuild or relink with modified LGPL
  libraries.
- If the distributed app has installation restrictions, provide whatever
  installation information is needed for a recipient to run their rebuilt
  version on the target device class.
- Attribution: reproduce/point to each library's license (see the
  `assets/licenses/` pattern in CrispCalc).

The default build scripts here (`--enable-static --disable-shared`) produce
static frameworks. Those are convenient for app integration, but they increase
the documentation burden: the consuming app must provide the rebuild/relink path
above when it distributes binaries.

## Route B — dynamic (relinkable) frameworks + keep your app permissive

If your app must stay **permissive or proprietary** (MIT/BSD/closed), the usual
route is to make the LGPL libraries independently replaceable. On iOS/macOS that
means shipping GMP/MPFR/MPC/FLINT as **dynamic `.framework`s embedded in the app
bundle** (Xcode target -> Frameworks -> "Embed & Sign"), so a user could in
principle drop in their own rebuilt library and re-sign the app.

Requirements:
- LGPL parts are **dynamically** linked (embedded dynamic frameworks, not baked
  into the main executable).
- You provide or point to the library source/build scripts, exact library
  versions, notices, and any information a recipient needs to rebuild compatible
  replacement frameworks and install/re-sign the application with them.
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
- The committed binaries in downstream packages may still be the **static**
  build. If you distribute those static binaries, make sure the consuming app
  provides the rebuild/relink path described in Route A. Route B frameworks are
  produced on demand by running `LINKAGE=shared ./build_*.sh`; they are not
  committed here.

## Quick decision

- App source/build is published enough for recipients to rebuild or relink with
  modified LGPL libraries -> **Route A** (static, default scripts).
- App must be permissive/proprietary → **Route B** (dynamic frameworks, all
  four wired). Run the full chain + an on-device test before release.
