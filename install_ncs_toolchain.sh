#!/usr/bin/env bash
# install_ncs_toolchain.sh — macOS, prepares NCS v3.0.2 toolchain (first-time safe)
# Usage:
#   ./install_ncs_toolchain.sh
# Config:
#   NCS_VERSION (default: v3.0.2)

set -euo pipefail

NCS_VERSION="${NCS_VERSION:-v3.0.2}"
ENV_SCRIPT="$HOME/ncs_${NCS_VERSION}_env.sh"
ENV_LOADER="$HOME/use_ncs_${NCS_VERSION}.sh"

log() { printf '%s\n' ">> $*"; }
die() {
  printf '%s\n' "!! $*" >&2
  exit 1
}

# --- prerequisites ---
if ! command -v brew >/dev/null 2>&1; then
  die "Homebrew not found. Install it from https://brew.sh and rerun this script."
fi

# nrfutil (standalone) via Homebrew cask; avoids old pip nrfutil
if ! command -v nrfutil >/dev/null 2>&1; then
  log "Installing nrfutil (Homebrew cask)..."
  brew install --cask nrfutil
fi

# lsof (used to check busy serial ports later)
if ! command -v lsof >/dev/null 2>&1; then
  log "Installing lsof..."
  brew install lsof
fi

# --- keep nrfutil current if Nordic suggests ---
log "Checking for nrfutil self-upgrade (optional)..."
nrfutil self-upgrade || true

# --- nrfutil plugins ---
log "Ensuring nrfutil plugins are installed..."
nrfutil list | grep -q sdk-manager || nrfutil install sdk-manager
nrfutil list | grep -q toolchain-manager || nrfutil install toolchain-manager
nrfutil list | grep -q nrf5sdk-tools || nrfutil install nrf5sdk-tools

# --- SDK + toolchain ---
log "Installing NCS $NCS_VERSION (SDK + toolchain) if missing..."
nrfutil sdk-manager install "$NCS_VERSION" || true
nrfutil toolchain-manager install --ncs-version "$NCS_VERSION" || true

# --- env script ---
log "Generating env script: $ENV_SCRIPT"
nrfutil toolchain-manager env --ncs-version "$NCS_VERSION" --as-script >"$ENV_SCRIPT"

# Verify env enables west
log "Verifying environment (west available)..."
bash -lc "source \"$ENV_SCRIPT\" && west --version >/dev/null" || die "Failed to load env or find west. Check $ENV_SCRIPT."

# also create a small loader you can run directly
cat >"$ENV_LOADER" <<EOF
#!/usr/bin/env bash
# use_ncs_${NCS_VERSION}.sh — load NCS toolchain env for this shell
set -euo pipefail
# shellcheck disable=SC1090
source "$ENV_SCRIPT"
echo "NCS environment loaded: $ENV_SCRIPT"
command -v west >/dev/null && west --version || true
EOF
chmod +x "$ENV_LOADER"

log "Done.
Next steps:
  1) Load the environment for this shell:
       source \"$ENV_SCRIPT\"
     or:
       \"$ENV_LOADER\"
  2) Then run the build/flash script.
"
