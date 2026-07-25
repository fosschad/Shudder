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
test -x "$root/usr/libexec/shudder-host-tool-probe"
test -x "$root/usr/libexec/QtWebEngineProcess"
test -s "$root/usr/resources/qtwebengine_resources.pak"
compgen -G "$root/usr/lib/libQt6Core.so.6*" >/dev/null
compgen -G "$root/usr/plugins/platforms/libqwayland*.so" >/dev/null

shopt -s nullglob globstar
runtime_elfs=(
  "$root/usr/bin/shudder"
  "$root/usr/libexec/shudder-host-tool-probe"
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

run_host_tool() {
  env \
    APPDIR="$root" \
    APPIMAGE="$artifact" \
    ARGV0="$artifact" \
    PATH="$root/usr/bin:$PATH" \
    SHUDDER_APPIMAGE_HOST_LD_LIBRARY_PATH="/opt/shudder-host-lib" \
    LD_LIBRARY_PATH="$root/usr/lib" \
    PYTHONHOME="$root/usr" \
    PYTHONPATH="$root/usr/qml" \
    QT_PLUGIN_PATH="$root/usr/plugins" \
    QML2_IMPORT_PATH="$root/usr/qml" \
    QML_IMPORT_PATH="$root/usr/qml" \
    QTWEBENGINEPROCESS_PATH="$root/usr/libexec/QtWebEngineProcess" \
    QTWEBENGINE_RESOURCES_PATH="$root/usr/resources" \
    QTWEBENGINE_LOCALES_PATH="$root/usr/translations/qtwebengine_locales" \
    "$root/usr/libexec/shudder-host-tool-probe" "$@"
}

if ! python_ssl="$(run_host_tool python3 -c 'import ssl, sys; print(sys.executable); print(ssl.OPENSSL_VERSION)')"; then
  echo "Host Python failed through the AppImage process boundary" >&2
  exit 1
fi
if [[ "$python_ssl" != *"OpenSSL "* ]]; then
  printf '%s\n' "$python_ssl"
  echo "Host Python could not import ssl through the AppImage process boundary" >&2
  exit 1
fi
printf 'Host Python SSL verification:\n%s\n' "$python_ssl"

if ! streamlink_version="$(run_host_tool streamlink --version)"; then
  echo "Host Streamlink failed through the AppImage process boundary" >&2
  exit 1
fi
if [[ "$streamlink_version" != *"streamlink"* && "$streamlink_version" != *"Streamlink"* ]]; then
  printf '%s\n' "$streamlink_version"
  echo "Host Streamlink did not report its version through the AppImage process boundary" >&2
  exit 1
fi
printf 'Host Streamlink verification: %s\n' "$streamlink_version"

if ! crypto_trace="$(SHUDDER_VERIFY_HOST_LIBRARIES=1 run_host_tool python3 -c 'import ssl; print(ssl.OPENSSL_VERSION)' 2>&1)"; then
  printf '%s\n' "$crypto_trace"
  echo "Host Python library isolation verification failed" >&2
  exit 1
fi
echo "Verified host Python does not load libcrypto.so.3 from the AppImage."
