#!/usr/bin/env bash
# Makes Purrlude's x86-64 AppImage from a Linux Release build:
#
#     tools/packaging/make_appimage.sh build/Purrlude_artefacts/Release out/
#
# The build folder holds the app and what it finds next to itself (libonnxruntime, verovio/,
# soundfonts/, licenses/); it all goes into usr/bin. linuxdeploy adds the shared libraries the
# app needs to usr/lib (not the system's own: glibc, ALSA, X11, fonts, libcurl...), and
# appimagetool packs it with the static type-2 runtime, so users don't need FUSE 2. The model
# for separating instruments isn't included (the app downloads it when asked). Build on the
# oldest Linux it should run on:
# the AppImage needs a glibc at least as new as the build machine's.
#
# The tools are pinned releases, checked against their SHA-256s.
set -euo pipefail

build=$(cd "$1" && pwd)
out=$(mkdir -p "$2" && cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

version=$(sed -nE 's/^project\(Purrlude VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
[[ -n "$version" ]] || { echo "No version in CMakeLists.txt" >&2; exit 1; }

fetch() {   # url, file, sha256
    curl -fsSL --retry 3 -o "$work/$2" "$1"
    echo "$3  $work/$2" | sha256sum --check --quiet
    chmod +x "$work/$2"
}

fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
      linuxdeploy c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
      appimagetool ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
      runtime-x86_64 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

# The tools are AppImages themselves: run them unpacked, so the build machine needn't have FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1

appdir="$work/Purrlude.AppDir"
mkdir -p "$appdir/usr/bin"
cp -a "$build/." "$appdir/usr/bin/"
rm -rf "$appdir/usr/bin/models"   # never ship the model: it's downloaded on first use
cp "$root/branding/purrlude-icon-256.png" "$work/purrlude.png"

# libcurl (for downloading the separation model) comes from the system, not the AppImage: a
# bundled one looks for TLS certificates where Debian keeps them (Fedora and openSUSE keep them
# elsewhere), and would bring its own OpenSSL and GnuTLS, missing the system's security updates.
# Every desktop distribution has libcurl.so.4.
LD_LIBRARY_PATH="$appdir/usr/bin" "$work/linuxdeploy" \
    --appdir "$appdir" \
    --exclude-library "libcurl.so*" \
    --executable "$appdir/usr/bin/Purrlude" \
    --desktop-file "$root/tools/packaging/purrlude.desktop" \
    --icon-file "$work/purrlude.png"

# Every library the app needs must be found inside the AppImage or be a system one.
if LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/Purrlude" | grep "not found"; then
    echo "Missing libraries (above)" >&2
    exit 1
fi

file="$out/Purrlude-$version-x86_64.AppImage"
ARCH=x86_64 "$work/appimagetool" --no-appstream --runtime-file "$work/runtime-x86_64" "$appdir" "$file"
echo "Made $file ($(du -h "$file" | cut -f1))"
