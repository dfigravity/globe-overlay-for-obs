#!/bin/zsh
# Packages the local macOS build as release artifacts, like other OBS plugins ship:
#   release/<name>-<version>-macos-<arch>.zip   (the .plugin bundle, drag to plugins folder)
#   release/<name>-<version>-macos-<arch>.pkg   (installer → ~/Library/Application Support/obs-studio/plugins)
# Ad-hoc signed: fine for this Mac and for testers who right-click → Open the .pkg.
# Public release needs a Developer ID + notarization (see README).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
"$ROOT/build-local-macos.sh"
NAME=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['name'])")
VERSION=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['version'])")
BUNDLEID=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['platformConfig']['macos']['bundleId'])")
ARCH="${ARCH:-$(uname -m)}"
OUT="${OUT:-$HOME/Library/Caches/$NAME-build}"; BUNDLE="$OUT/$NAME.plugin"
REL="$ROOT/release"; mkdir -p "$REL"
STAGE="$OUT/pkgroot"; rm -rf "$STAGE" "$OUT/pkg"; mkdir -p "$STAGE" "$OUT/pkg"
export COPYFILE_DISABLE=1
cp -R "$BUNDLE" "$STAGE/"
find "$STAGE" -name "._*" -delete; xattr -cr "$STAGE" 2>/dev/null || true  # com.apple.provenance may remain; pkgbuild stores it as harmless ._ entries

# zip without AppleDouble junk
ZIP="$REL/$NAME-$VERSION-macos-$ARCH.zip"; rm -f "$ZIP"
ditto -c -k --norsrc --keepParent "$BUNDLE" "$ZIP"

# installer: component pkg + distribution that installs into the *user's* home Library
pkgbuild --root "$STAGE" --identifier "$BUNDLEID" --version "$VERSION" \
  --install-location "/Library/Application Support/obs-studio/plugins" "$OUT/pkg/component.pkg" >/dev/null
cat > "$OUT/pkg/distribution.xml" <<EOD
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Globe Overlay for OBS $VERSION</title>
  <welcome file="welcome.html"/>
  <options customize="never" require-scripts="false" hostArchitectures="$ARCH"/>
  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
  <choices-outline><line choice="default"/></choices-outline>
  <choice id="default" title="Globe Overlay for OBS"><pkg-ref id="$BUNDLEID"/></choice>
  <pkg-ref id="$BUNDLEID" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
EOD
mkdir -p "$OUT/pkg/res"
cat > "$OUT/pkg/res/welcome.html" <<EOD
<html><body style="font-family:-apple-system;font-size:13px">
<p>Installs <b>Globe Overlay for OBS $VERSION</b> into your OBS plugins folder
(<code>~/Library/Application Support/obs-studio/plugins</code>).</p>
<p>Requires OBS Studio 31 or newer with its Browser Source. Restart OBS after installing, then add the
<b>Globe Overlay (check-in globe)</b> source and paste your Triode check-in link.</p>
</body></html>
EOD
PKG="$REL/$NAME-$VERSION-macos-$ARCH.pkg"; rm -f "$PKG"
productbuild --distribution "$OUT/pkg/distribution.xml" --resources "$OUT/pkg/res" --package-path "$OUT/pkg" "$PKG" >/dev/null
find "$REL" -name '._*' -delete 2>/dev/null || true
echo "release artifacts:"; ls -la "$REL" | grep -v "^\._" | grep "$VERSION"
