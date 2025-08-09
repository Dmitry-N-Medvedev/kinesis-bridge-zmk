#!/usr/bin/env bash
# build.sh — build + DFU flash for nRF52840 Dongle (PCA10059)
#
# Usage:
#   ./build.sh
#   APP=/path/to/app ./build.sh
#   PORT=/dev/tty.usbmodemXXXX ./build.sh
#
# Env:
#   NCS_VERSION (default: v3.0.2)
#   WORKSPACE   (default: $HOME/ncs-workspace)   # must be initialized via ./update_workspace.sh
#   BOARD       (default: nrf52840dongle)
#   APP         (default: $PWD)                   # your project root (standalone app)
#   BUILD_DIR   (default: $APP/build)             # build dir lives in the project
#   PORT        (default: /dev/tty.usbmodemD5606742A6991, else auto)

set -euo pipefail

NCS_VERSION="${NCS_VERSION:-v3.0.2}"
WORKSPACE="${WORKSPACE:-$HOME/ncs-workspace}"
BOARD="${BOARD:-nrf52840dongle}"
APP="${APP:-$PWD}"
BUILD_DIR="${BUILD_DIR:-$APP/build}"
DFU_ZIP="$APP/app_dfu.zip"
# When sysbuild is off, Zephyr typically emits zephyr/zephyr.hex:
MERGED_HEX="$BUILD_DIR/zephyr/zephyr.hex"
ENV_SCRIPT="$HOME/ncs_${NCS_VERSION}_env.sh"
PORT_DEFAULT="/dev/tty.usbmodemD5606742A6991"

log() { printf '%s\n' ">> $*"; }
die() {
  printf '%s\n' "!! $*" >&2
  exit 1
}

# --- prerequisites ---
command -v nrfutil >/dev/null || die "nrfutil not found."
command -v lsof >/dev/null || die "lsof not found. Install via: brew install lsof"

# Ensure west is available; if not, try to source the env script
if ! command -v west >/dev/null 2>&1; then
  [ -f "$ENV_SCRIPT" ] || die "Env not found: $ENV_SCRIPT. Run install_ncs_toolchain.sh first."
  # shellcheck disable=SC1090
  source "$ENV_SCRIPT"
fi
command -v west >/dev/null || die "west not found after sourcing $ENV_SCRIPT."

# Require a workspace (for west 'build')
[ -f "$WORKSPACE/.west/config" ] || die "West workspace not found at $WORKSPACE. Run ./update_workspace.sh first."
[ -d "$WORKSPACE/zephyr" ] || die "Zephyr tree not found under $WORKSPACE. Re-run ./update_workspace.sh."

# --- clear Zephyr CMake cache (helps with try-compile issues) ---
rm -rf "$HOME/Library/Caches/zephyr" || true

# --- resolve Zephyr SDK toolchain paths (force CMake to use the right tools) ---
: "${ZEPHYR_SDK_INSTALL_DIR:?ZEPHYR_SDK_INSTALL_DIR is not set in your environment}"
SDK_BIN="$ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin"
SDK_LD="$SDK_BIN/arm-zephyr-eabi-ld.bfd"
SDK_AR="$SDK_BIN/arm-zephyr-eabi-gcc-ar"
SDK_NM="$SDK_BIN/arm-zephyr-eabi-nm"
SDK_OBJCOPY="$SDK_BIN/arm-zephyr-eabi-objcopy"
SDK_OBJDUMP="$SDK_BIN/arm-zephyr-eabi-objdump"
SDK_RANLIB="$SDK_BIN/arm-zephyr-eabi-gcc-ranlib"

for f in "$SDK_LD" "$SDK_AR" "$SDK_NM" "$SDK_OBJCOPY" "$SDK_OBJDUMP" "$SDK_RANLIB"; do
  [ -x "$f" ] || die "Missing SDK tool: $f"
done

# --- build (disable sysbuild; project-local build; always pristine; force toolchain vars) ---
log "Building app='$APP' for board='$BOARD' (workspace: $WORKSPACE, build dir: $BUILD_DIR, --no-sysbuild, --pristine) ..."
rm -rf "$BUILD_DIR"
(
  cd "$WORKSPACE"
  CMAKE_VERBOSE_MAKEFILE=ON \
    west build \
    -p always \
    --pristine \
    --no-sysbuild \
    -b "$BOARD" \
    -s "$APP" \
    --build-dir "$BUILD_DIR" \
    -- \
    -DZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    -DZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR" \
    -DCMAKE_LINKER="$SDK_LD" \
    -DCMAKE_AR="$SDK_AR" \
    -DCMAKE_NM="$SDK_NM" \
    -DCMAKE_OBJCOPY="$SDK_OBJCOPY" \
    -DCMAKE_OBJDUMP="$SDK_OBJDUMP" \
    -DCMAKE_RANLIB="$SDK_RANLIB"
)

# Choose the hex we’ll flash
if [ ! -f "$MERGED_HEX" ]; then
  ALT_HEX="$BUILD_DIR/merged.hex"
  [ -f "$ALT_HEX" ] || die "No hex found at $MERGED_HEX or $ALT_HEX"
  MERGED_HEX="$ALT_HEX"
fi
log "HEX ready: $MERGED_HEX"

# --- DFU package ---
log "Creating DFU ZIP at $DFU_ZIP ..."
rm -f "$DFU_ZIP"
nrfutil nrf5sdk-tools pkg generate \
  --hw-version 52 --sd-req 0x00 \
  --application "$MERGED_HEX" \
  --application-version 1 "$DFU_ZIP"
log "DFU ZIP: $DFU_ZIP"

# --- select DFU port ---
if [ -z "${PORT:-}" ]; then
  if [ -e "$PORT_DEFAULT" ]; then
    PORT="$PORT_DEFAULT"
  else
    CANDIDATES=()
    for p in /dev/tty.usbmodem*; do
      [ -e "$p" ] && CANDIDATES+=("$p")
    done
    [ "${#CANDIDATES[@]}" -gt 0 ] || die "No /dev/tty.usbmodem* found. Put dongle in DFU (hold RESET while plugging) or set PORT=..."
    PORT="${CANDIDATES[0]}"
  fi
fi
log "Using DFU port: $PORT"

# Ensure port free
if lsof "$PORT" >/dev/null 2>&1; then
  log "Port is busy:"
  lsof "$PORT" || true
  die "Free the port or set a different PORT."
fi

# --- flash ---
log "Flashing via DFU ..."
nrfutil nrf5sdk-tools dfu usb-serial -pkg "$DFU_ZIP" -p "$PORT"
log "Done."
