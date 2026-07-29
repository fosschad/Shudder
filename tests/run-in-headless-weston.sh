#!/bin/sh
set -eu

runtime_dir="$(mktemp -d)"
socket_name="wayland-shudder-test-$$"
chmod 700 "$runtime_dir"

cleanup() {
  set +e
  if kill -0 "$weston_pid" 2>/dev/null; then
    kill "$weston_pid"
    wait "$weston_pid"
  fi
  rm -rf "$runtime_dir"
}
trap cleanup EXIT INT TERM

XDG_RUNTIME_DIR="$runtime_dir" weston \
  --backend=headless-backend.so \
  --socket="$socket_name" \
  --idle-time=0 \
  --log="$runtime_dir/weston.log" &
weston_pid=$!

attempt=0
while [ ! -S "$runtime_dir/$socket_name" ] && [ "$attempt" -lt 50 ]; do
  sleep 0.1
  attempt=$((attempt + 1))
done
test -S "$runtime_dir/$socket_name"

set +e
env \
  DISPLAY= \
  XDG_RUNTIME_DIR="$runtime_dir" \
  WAYLAND_DISPLAY="$socket_name" \
  DBUS_SESSION_BUS_ADDRESS= \
  LIBGL_ALWAYS_SOFTWARE=1 \
  MESA_LOADER_DRIVER_OVERRIDE=llvmpipe \
  NO_AT_BRIDGE=1 \
  QT_QPA_PLATFORM=wayland \
  QSG_RHI_BACKEND=opengl \
  SHUDDER_MPV_HWDEC=no \
  "$@"
status=$?
set -e
if [ "$status" -ne 0 ]; then
  cat "$runtime_dir/weston.log"
fi
exit "$status"
