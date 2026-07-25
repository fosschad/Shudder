#!/usr/bin/env bash
set -euo pipefail

artifact="${1:?Usage: verify-appimage.sh <AppImage>}"
artifact="$(readlink -f "$artifact")"
checksum="$artifact.sha256"

test -s "$artifact"
test -s "$checksum"
(
  cd "$(dirname "$artifact")"
  sha256sum -c "$(basename "$checksum")"
)

extract_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$extract_dir"
}
trap cleanup EXIT

(
  cd "$extract_dir"
  "$artifact" --appimage-extract >/dev/null
)

root="$extract_dir/squashfs-root"
test -x "$root/AppRun"
test -x "$root/usr/bin/shudder"
test -x "$root/usr/libexec/QtWebEngineProcess"
compgen -G "$root/usr/lib/libQt6Core.so.6*" >/dev/null
compgen -G "$root/usr/plugins/platforms/libqwayland*.so" >/dev/null

shopt -s nullglob globstar
runtime_elfs=(
  "$root/usr/bin/shudder"
  "$root/usr/libexec/QtWebEngineProcess"
  "$root"/usr/plugins/**/*.so
  "$root"/usr/qml/**/*.so
)

checked=0
for runtime_elf in "${runtime_elfs[@]}"; do
  dependencies="$(env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH LD_LIBRARY_PATH="$root/usr/lib" ldd "$runtime_elf")"
  if [[ "$dependencies" == *"not found"* ]]; then
    printf '%s\n' "$dependencies"
    echo "Unresolved shared-library dependencies in $runtime_elf" >&2
    exit 1
  fi
  ((++checked))
done
printf 'Verified dependencies for %d AppImage runtime files.\n' "$checked"
