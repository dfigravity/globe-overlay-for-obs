#!/bin/zsh
# Local developer build for THIS Mac, without Xcode/CMake: compiles the plugin
# with the command line tools against the libobs inside /Applications/OBS.app,
# packages a .plugin bundle, ad-hoc signs it, and (with --install) copies it to
# ~/Library/Application Support/obs-studio/plugins. Release builds use the
# template's CMake/Xcode path (see README.md); this script is only for testing.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
OBS_APP="${OBS_APP:-/Applications/OBS.app}"
OBS_VER="$(defaults read "$OBS_APP/Contents/Info.plist" CFBundleShortVersionString)"
HDR="${OBS_HEADERS:-$HOME/.local/opt/obs-headers/obs-studio-$OBS_VER/libobs}"
if [ ! -d "$HDR" ]; then
  echo "libobs headers for OBS $OBS_VER not found at $HDR"; echo "fetch: curl -L https://github.com/obsproject/obs-studio/archive/refs/tags/$OBS_VER.tar.gz | tar xz -C ~/.local/opt/obs-headers --include='*/libobs/*'"; exit 1
fi
NAME=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['name'])")
VERSION=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['version'])")
BUNDLEID=$(python3 -c "import json;print(json.load(open('$ROOT/buildspec.json'))['platformConfig']['macos']['bundleId'])")
# Build under $HOME: the Ceres volume sprinkles AppleDouble ._ files into bundles, which codesign rejects.
OUT="${OUT:-$HOME/Library/Caches/$NAME-build}"; GEN="$OUT/gen"; BUNDLE="$OUT/$NAME.plugin"
rm -rf "$BUNDLE"; mkdir -p "$GEN" "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"

# generated sources the CMake build would produce
sed "s/@CMAKE_PROJECT_NAME@/$NAME/; s/@CMAKE_PROJECT_VERSION@/$VERSION/" "$ROOT/src/plugin-support.c.in" > "$GEN/plugin-support.c"
cat > "$GEN/obsconfig.h" <<EOC
#pragma once
#define OBS_DATA_PATH "../../data"
#define OBS_PLUGIN_PATH "../../obs-plugins"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
EOC

ARCH="${ARCH:-$(uname -m)}"
# The newest CommandLineTools SDK (27.0) has tbd entries this ld rejects; prefer an older installed SDK.
SDK="${SDK:-}"
if [ -z "$SDK" ]; then for cand in MacOSX26.5.sdk MacOSX26.sdk MacOSX15.4.sdk MacOSX15.sdk MacOSX.sdk; do
  [ -d "/Library/Developer/CommandLineTools/SDKs/$cand" ] && SDK="/Library/Developer/CommandLineTools/SDKs/$cand" && break; done; fi
echo "SDK: $SDK"
CFLAGS=(-arch "$ARCH" -isysroot "$SDK" -mmacosx-version-min=12.0 -std=c17 -O2 -g -fPIC -fvisibility=hidden
        -Wall -Wextra -Werror -Wno-unused-parameter -Wno-deprecated-declarations
        -I"$HDR" -I"$HOME/.local/opt/simde-0.8.2" -I"$GEN" -I"$ROOT/src")
OBJS=()
for src in "$ROOT/src/plugin-main.c" "$ROOT/src/globe-source.c" "$GEN/plugin-support.c"; do
  obj="$GEN/$(basename "${src%.c}").o"
  clang "${CFLAGS[@]}" -c "$src" -o "$obj"
  OBJS+=("$obj")
done
clang -arch "$ARCH" -isysroot "$SDK" -bundle -o "$BUNDLE/Contents/MacOS/$NAME" "${OBJS[@]}" \
  -F"$OBS_APP/Contents/Frameworks" -framework libobs \
  -Wl,-rpath,@executable_path/../Frameworks -Wl,-rpath,"$OBS_APP/Contents/Frameworks"

cat > "$BUNDLE/Contents/Info.plist" <<EOP
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLEID</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleSupportedPlatforms</key><array><string>MacOSX</string></array>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
</dict></plist>
EOP
cp -R "$ROOT/data/." "$BUNDLE/Contents/Resources/"
find "$BUNDLE" -name '._*' -delete; xattr -cr "$BUNDLE" 2>/dev/null || true
codesign --force --sign - --timestamp=none "$BUNDLE" >/dev/null
echo "built $BUNDLE ($(du -sh "$BUNDLE" | cut -f1)) for OBS $OBS_VER, arch $ARCH"
if [ "$1" = "--install" ]; then
  DEST="$HOME/Library/Application Support/obs-studio/plugins"
  mkdir -p "$DEST"; rm -rf "$DEST/$NAME.plugin"; cp -R "$BUNDLE" "$DEST/"
  echo "installed → $DEST/$NAME.plugin  (restart OBS)"
fi
