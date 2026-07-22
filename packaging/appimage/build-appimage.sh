#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
appdir="${2:-Shudder.AppDir}"
app_id="${SHUDDER_APP_ID:-}"

copy_qt_tree() {
  local source_dir="$1"
  local target_dir="$2"
  shift 2
  [[ -d "$source_dir" ]] || return 0
  for name in "$@"; do
    [[ -e "$source_dir/$name" ]] || continue
    mkdir -p "$target_dir"
    cp -a "$source_dir/$name" "$target_dir/"
  done
}

if [[ -z "$app_id" ]]; then
  config_header="$build_dir/src/generated/shudder_config.h"
  if [[ -f "$config_header" ]]; then
    app_id="$(sed -n 's/^#define SHUDDER_APP_ID "\(.*\)"/\1/p' "$config_header" | head -n 1)"
  fi
  if [[ -z "$app_id" ]]; then
    shopt -s nullglob
    desktop_files=("$build_dir"/packaging/*.desktop)
    shopt -u nullglob
    if [[ "${#desktop_files[@]}" -ne 1 ]]; then
      echo "Set SHUDDER_APP_ID or configure CMake so exactly one desktop file exists in $build_dir/packaging" >&2
      exit 1
    fi
    app_id="${desktop_files[0]##*/}"
    app_id="${app_id%.desktop}"
  fi
fi

rm -rf "$appdir"
cmake --install "$build_dir" --prefix "$PWD/$appdir/usr"
install -Dm755 packaging/appimage/AppRun "$appdir/AppRun"
install -Dm644 "assets/icons/hicolor/scalable/apps/shudder.svg" "$appdir/$app_id.svg"
install -Dm644 "$build_dir/packaging/$app_id.desktop" "$appdir/$app_id.desktop"
install -Dm644 "$build_dir/packaging/$app_id.metainfo.xml" "$appdir/usr/share/metainfo/$app_id.appdata.xml"

qt_plugin_dir=""
if command -v pkg-config >/dev/null 2>&1; then
  qt_lib_dir="$(pkg-config --variable=libdir Qt6Core 2>/dev/null || true)"
  if [[ -n "$qt_lib_dir" && -d "$qt_lib_dir/qt6/plugins" ]]; then
    qt_plugin_dir="$qt_lib_dir/qt6/plugins"
  fi
fi
if [[ -z "$qt_plugin_dir" ]]; then
  for candidate in /usr/lib/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/qt/plugins; do
    if [[ -d "$candidate" ]]; then
      qt_plugin_dir="$candidate"
      break
    fi
  done
fi
if [[ -n "$qt_plugin_dir" ]]; then
  for plugin in libqgif.so libqwebp.so libqjpeg.so; do
    if [[ -f "$qt_plugin_dir/imageformats/$plugin" ]]; then
      install -Dm755 "$qt_plugin_dir/imageformats/$plugin" "$appdir/usr/plugins/imageformats/$plugin"
    fi
  done
  copy_qt_tree "$qt_plugin_dir" "$appdir/usr/plugins" platforms wayland-decoration-client xcbglintegrations platformthemes iconengines tls networkinformation
fi

qt_qml_dir=""
if command -v qmake6 >/dev/null 2>&1; then
  qt_qml_dir="$(qmake6 -query QT_INSTALL_QML 2>/dev/null || true)"
  [[ -d "$qt_qml_dir" ]] || qt_qml_dir=""
fi
if [[ -z "$qt_qml_dir" ]]; then
  for candidate in /usr/lib/qt6/qml /usr/lib64/qt6/qml /usr/lib/qt/qml; do
    if [[ -d "$candidate" ]]; then
      qt_qml_dir="$candidate"
      break
    fi
  done
fi
if [[ -n "$qt_qml_dir" ]]; then
  copy_qt_tree "$qt_qml_dir" "$appdir/usr/qml" Qt QtQml QtQuick QtWebChannel QtWebEngine
fi

qt_libexec_dir=""
if command -v qmake6 >/dev/null 2>&1; then
  qt_libexec_dir="$(qmake6 -query QT_INSTALL_LIBEXECS 2>/dev/null || true)"
  [[ -d "$qt_libexec_dir" ]] || qt_libexec_dir=""
fi
if [[ -z "$qt_libexec_dir" ]]; then
  for candidate in /usr/lib/qt6/libexec /usr/lib64/qt6/libexec /usr/lib/qt/libexec; do
    if [[ -d "$candidate" ]]; then
      qt_libexec_dir="$candidate"
      break
    fi
  done
fi
if [[ -n "$qt_libexec_dir" && -x "$qt_libexec_dir/QtWebEngineProcess" ]]; then
  install -Dm755 "$qt_libexec_dir/QtWebEngineProcess" "$appdir/usr/libexec/QtWebEngineProcess"
fi

qt_resources_dir=""
if command -v qmake6 >/dev/null 2>&1; then
  qt_resources_dir="$(qmake6 -query QT_INSTALL_RESOURCES 2>/dev/null || true)"
  [[ -d "$qt_resources_dir" ]] || qt_resources_dir=""
fi
if [[ -z "$qt_resources_dir" ]]; then
  for candidate in /usr/share/qt6/resources /usr/lib/qt6/resources /usr/lib64/qt6/resources; do
    if [[ -d "$candidate" ]]; then
      qt_resources_dir="$candidate"
      break
    fi
  done
fi
if [[ -n "$qt_resources_dir" ]]; then
  copy_qt_tree "$qt_resources_dir" "$appdir/usr/resources" qtwebengine_resources.pak qtwebengine_devtools_resources.pak qtwebengine_resources_100p.pak qtwebengine_resources_200p.pak v8_context_snapshot.bin icudtl.dat
fi

if ! command -v appimagetool >/dev/null 2>&1; then
  echo "appimagetool is required to build Shudder-x86_64.AppImage" >&2
  exit 1
fi

rm -f "Shudder-x86_64.AppImage" "Shudder-x86_64.AppImage.sha256"
appimagetool "$appdir" "Shudder-x86_64.AppImage"
sha256sum "Shudder-x86_64.AppImage" > "Shudder-x86_64.AppImage.sha256"
