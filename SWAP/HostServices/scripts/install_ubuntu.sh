#!/usr/bin/env bash
# Build and install BTCMinerControl on Ubuntu.
set -euo pipefail

BUILD_DIR="${1:-build}"
PREFIX="${2:-/usr/local}"
CONFIG_DIR="${3:-/etc/btcminercontrol}"

cmake -S "$(dirname "$0")/.." -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" -j"$(nproc)"

sudo cmake --install "$BUILD_DIR"
sudo mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/config.json" ]; then
    sudo cp "$(dirname "$0")/../BTCMinerControl/config.json" "$CONFIG_DIR/config.json"
fi
echo "Edit $CONFIG_DIR/config.json to set your pool credentials."
echo "Then start manually with:"
echo "  $PREFIX/bin/btcminercontrol $CONFIG_DIR/config.json"
echo "or install the systemd service:"
echo "  sudo cp $(dirname "$0")/../packaging/btcminercontrol.service /etc/systemd/system/"
echo "  sudo systemctl daemon-reload"
echo "  sudo systemctl enable --now btcminercontrol"
