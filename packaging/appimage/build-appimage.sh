#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
appdir="${2:-Shudder.AppDir}"
app_id="${SHUDDER_APP_ID:-}"
config_header="$build_dir/src/generated/shudder_config.h"

if [[ ! -f "$config_header" ]]; then
  echo "Configured build metadata was not found at $config_header" >&2
  exit 1
fi

configured_version="$(sed -n 's/^#define SHUDDER_VERSION "\(.*\)"/\1/p' "$config_header")"
version="${SHUDDER_VERSION:-$configured_version}"
if [[ ! "$configured_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid configured Shudder version: $configured_version" >&2
  exit 1
fi
if [[ "$version" != "$configured_version" ]]; then
  echo "Requested AppImage version $version does not match configured project version $configured_version" >&2
  exit 1
fi

artifact="Shudder-${version}-x86_64.AppImage"

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
  app_id="$(sed -n 's/^#define SHUDDER_APP_ID "\(.*\)"/\1/p' "$config_header")"
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
cmake --build "$build_dir" --target shudder_host_tool_probe
install -Dm755 packaging/appimage/AppRun "$appdir/AppRun"
install -Dm755 "$build_dir/src/shudder-host-tool-probe" "$appdir/usr/libexec/shudder-host-tool-probe"
install -Dm644 "assets/icons/hicolor/scalable/apps/shudder.svg" "$appdir/$app_id.svg"
install -Dm644 "$build_dir/packaging/$app_id.desktop" "$appdir/$app_id.desktop"
install -Dm644 "$build_dir/packaging/$app_id.metainfo.xml" "$appdir/usr/share/metainfo/$app_id.appdata.xml"

qt_plugin_dir=""
qt_qmake="$(command -v qmake6 || command -v qmake || true)"
if [[ -n "$qt_qmake" ]]; then
  qt_plugin_dir="$("$qt_qmake" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
  [[ -d "$qt_plugin_dir" ]] || qt_plugin_dir=""
fi
if [[ -z "$qt_plugin_dir" ]] && command -v pkg-config >/dev/null 2>&1; then
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
  copy_qt_tree "$qt_plugin_dir" "$appdir/usr/plugins" platforms wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration xcbglintegrations platformthemes iconengines tls networkinformation
fi

qt_qml_dir=""
if [[ -n "$qt_qmake" ]]; then
  qt_qml_dir="$("$qt_qmake" -query QT_INSTALL_QML 2>/dev/null || true)"
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
if [[ -n "$qt_qmake" ]]; then
  qt_libexec_dir="$("$qt_qmake" -query QT_INSTALL_LIBEXECS 2>/dev/null || true)"
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
qt_install_prefix=""
qt_install_data=""
if [[ -n "$qt_qmake" ]]; then
  qt_install_prefix="$("$qt_qmake" -query QT_INSTALL_PREFIX 2>/dev/null || true)"
  qt_install_data="$("$qt_qmake" -query QT_INSTALL_DATA 2>/dev/null || true)"
fi
for candidate in "$qt_install_prefix/resources" "$qt_install_data/resources" /usr/share/qt6/resources /usr/lib/qt6/resources /usr/lib64/qt6/resources; do
  if [[ -d "$candidate" ]]; then
    qt_resources_dir="$candidate"
    break
  fi
done
if [[ -z "$qt_resources_dir" && -n "${QT_ROOT_DIR:-}" ]]; then
  for candidate in "$QT_ROOT_DIR/resources" "$QT_ROOT_DIR/share/qt6/resources"; do
    if [[ -d "$candidate" ]]; then
      qt_resources_dir="$candidate"
      break
    fi
  done
fi
if [[ -n "$qt_resources_dir" ]]; then
  copy_qt_tree "$qt_resources_dir" "$appdir/usr/resources" qtwebengine_resources.pak qtwebengine_devtools_resources.pak qtwebengine_resources_100p.pak qtwebengine_resources_200p.pak v8_context_snapshot.bin icudtl.dat
fi

qt_translations_dir=""
if [[ -n "$qt_qmake" ]]; then
  qt_translations_dir="$("$qt_qmake" -query QT_INSTALL_TRANSLATIONS 2>/dev/null || true)"
  [[ -d "$qt_translations_dir" ]] || qt_translations_dir=""
fi
if [[ -d "$qt_translations_dir/qtwebengine_locales" ]]; then
  copy_qt_tree "$qt_translations_dir" "$appdir/usr/translations" qtwebengine_locales
fi

for tool in linuxdeploy appimagetool; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required to build $artifact" >&2
    exit 1
  fi
done

deployment_library_path="${LD_LIBRARY_PATH:-}"
if [[ -n "$qt_qmake" ]]; then
  qt_library_dir="$("$qt_qmake" -query QT_INSTALL_LIBS 2>/dev/null || true)"
  if [[ -d "$qt_library_dir" ]]; then
    deployment_library_path="$qt_library_dir${deployment_library_path:+:$deployment_library_path}"
  fi
fi

# linuxdeploy's bundled patchelf corrupts the GNU hash table in current
# libleancrypto builds. The library needs no RUNPATH because AppRun supplies
# the bundle library directory, so restore the pristine file after deployment.
leancrypto_source=""
while read -r name arrow path _; do
  if [[ "$name" == "libleancrypto.so.1" && "$arrow" == "=>" && -f "$path" ]]; then
    leancrypto_source="$path"
    break
  fi
done < <(LD_LIBRARY_PATH="$deployment_library_path" ldd "$appdir/usr/bin/shudder")

shopt -s nullglob globstar
runtime_elfs=(
  "$appdir"/usr/plugins/**/*.so*
  "$appdir"/usr/qml/**/*.so*
  "$appdir/usr/libexec/QtWebEngineProcess"
  "$appdir/usr/libexec/shudder-host-tool-probe"
)
shopt -u nullglob globstar
deploy_dependency_args=()
for runtime_elf in "${runtime_elfs[@]}"; do
  deploy_dependency_args+=(--deploy-deps-only "$runtime_elf")
done

NO_STRIP=1 LD_LIBRARY_PATH="$deployment_library_path" linuxdeploy \
  --appdir "$appdir" \
  --executable "$appdir/usr/bin/shudder" \
  "${deploy_dependency_args[@]}" \
  --desktop-file "$appdir/$app_id.desktop" \
  --icon-file "$appdir/$app_id.svg"

if [[ -n "$leancrypto_source" && -f "$appdir/usr/lib/libleancrypto.so.1" ]]; then
  install -m755 "$leancrypto_source" "$appdir/usr/lib/libleancrypto.so.1"
fi

rm -f "Shudder-x86_64.AppImage" "Shudder-x86_64.AppImage.sha256" "$artifact" "$artifact.sha256"
appimagetool "$appdir" "$artifact"
sha256sum "$artifact" > "$artifact.sha256"
