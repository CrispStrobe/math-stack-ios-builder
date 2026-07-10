#!/bin/bash
#############################################################################
#
# wrap_dynamic_framework.sh
#
# Turns per-arch dynamic .dylib builds of an autoconf C library into an
# embeddable, code-signable dynamic .xcframework -- the Route B (dynamic /
# LGPL-relinkable) counterpart to the static createXCFramework in the
# build_*.sh scripts. See LGPL-COMPLIANCE.md.
#
# For each platform slice it:
#   1. rewrites the dylib's install name to @rpath/<Name>.framework/<Name>
#   2. wraps it in a proper <Name>.framework bundle (binary + Info.plist +
#      Headers)
#   3. feeds the frameworks to `xcodebuild -create-xcframework -framework ...`
#
# Usage:
#   wrap_dynamic_framework.sh <Name> <header.h|header_dir> <out_dir> \
#       <platform:arch:dylib> [<platform:arch:dylib> ...]
#
# The second arg may be a single header file (GMP/MPFR/MPC) or a directory
# whose contents are copied wholesale into the framework's Headers/ (FLINT,
# which ships a whole flint/ header tree).
#
# where each positional slice is e.g.
#   ios-arm64:arm64:/path/libgmp-iphoneos-arm64.dylib
#   (the first field is the xcframework slice id / min-os label, the second
#    the arch, the third the built dylib; pre-lipo universal slices are fine
#    -- pass one dylib per xcframework slice.)
#
# This script is generic; the build_*.sh scripts supply the slices.
#
#############################################################################
set -euo pipefail

NAME="$1"; HEADER="$2"; OUT_DIR="$3"; shift 3
FRAMEWORK_BINARY_NAME="$NAME"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

log() { printf "[WRAP %s] %s\n" "$NAME" "$1"; }

framework_args=()

for slice in "$@"; do
    slice_id="${slice%%:*}"
    rest="${slice#*:}"
    arch="${rest%%:*}"
    dylib="${rest#*:}"

    [ -f "$dylib" ] || { echo "❌ missing dylib: $dylib" >&2; exit 1; }

    fw_dir="$WORK/$slice_id/$NAME.framework"
    mkdir -p "$fw_dir/Headers"

    # 1. copy the dylib in as the framework's binary (unversioned layout, which
    #    is what iOS frameworks use; macOS also accepts it for embedded 3rd-party
    #    frameworks).
    cp "$dylib" "$fw_dir/$FRAMEWORK_BINARY_NAME"

    # 2. install name -> @rpath so the app can find it embedded in its bundle,
    #    and a user could relink against a swapped-in copy (the LGPL point).
    install_name_tool -id "@rpath/$NAME.framework/$FRAMEWORK_BINARY_NAME" \
        "$fw_dir/$FRAMEWORK_BINARY_NAME"

    # 3. headers (single file or a whole directory tree) + a minimal Info.plist.
    if [ -d "$HEADER" ]; then
        cp -R "$HEADER"/. "$fw_dir/Headers/"
    else
        cp "$HEADER" "$fw_dir/Headers/"
    fi

    /usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string com.crispstrobe.mathstack.$NAME" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :CFBundleExecutable string $FRAMEWORK_BINARY_NAME" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :CFBundleName string $NAME" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string FMWK" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string 1.0" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string 1" "$fw_dir/Info.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Add :MinimumOSVersion string 13.0" "$fw_dir/Info.plist" >/dev/null

    log "wrapped $slice_id ($arch) -> $NAME.framework"
    framework_args+=(-framework "$fw_dir")
done

rm -rf "$OUT_DIR/$NAME.xcframework"
xcodebuild -create-xcframework "${framework_args[@]}" -output "$OUT_DIR/$NAME.xcframework"

log "✅ created $OUT_DIR/$NAME.xcframework"
log "NOTE: the app target must 'Embed & Sign' this framework (not just link it)"
log "for the LGPL relink capability to hold. See LGPL-COMPLIANCE.md, Route B."
