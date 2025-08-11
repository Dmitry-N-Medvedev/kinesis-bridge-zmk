#!/usr/bin/env bash
# update_workspace.sh — init & update NCS west workspace
# Usage:
#   ./update_workspace.sh
#
# Env:
#   NCS_VERSION (default: v3.0.2)
#   WORKSPACE   (default: $HOME/ncs-workspace)

set -euo pipefail

NCS_VERSION="${NCS_VERSION:-v3.0.2}"
WORKSPACE="${WORKSPACE:-$HOME/ncs-workspace}"
ENV_SCRIPT="$HOME/ncs_${NCS_VERSION}_env.sh"

log() { printf '%s\n' ">> $*"; }
die() {
  printf '%s\n' "!! $*" >&2
  exit 1
}

command -v nrfutil >/dev/null || die "nrfutil not found (install_ncs_toolchain.sh first)."
if ! command -v west >/dev/null 2>&1; then
  [ -f "$ENV_SCRIPT" ] || die "Env not found: $ENV_SCRIPT. Run install_ncs_toolchain.sh first."
  # shellcheck disable=SC1090
  source "$ENV_SCRIPT"
fi
command -v west >/dev/null || die "west not found after sourcing $ENV_SCRIPT."

mkdir -p "$WORKSPACE"
cd "$WORKSPACE"

if [ ! -f .west/config ]; then
  log "Initializing workspace from sdk-nrf@$NCS_VERSION ..."
  west init -m https://github.com/nrfconnect/sdk-nrf --mr "$NCS_VERSION"
else
  log "Workspace already initialized."
fi

log "Updating projects ..."
west update

log "Exporting Zephyr CMake package ..."
west zephyr-export

log "Workspace ready at: $WORKSPACE"
