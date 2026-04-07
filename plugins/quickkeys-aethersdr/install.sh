#!/usr/bin/env bash
# Install quickkeys-aethersdr as a systemd user service.
set -e

DEST="$HOME/.config/AetherSDR/plugins/quickkeys-aethersdr"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE="quickkeys-aethersdr.service"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing Quick Keys daemon to $DEST..."
mkdir -p "$DEST" "$SERVICE_DIR"

cp "$SCRIPT_DIR/quickkeys_aethersdr.py" "$DEST/"
chmod +x "$DEST/quickkeys_aethersdr.py"

# Copy config only if it doesn't already exist (preserve user customisation)
if [ ! -f "$DEST/config.json" ]; then
    cp "$SCRIPT_DIR/config.json" "$DEST/"
    echo "Default config installed at $DEST/config.json — edit to customise button mappings."
else
    echo "Existing config preserved at $DEST/config.json"
fi

# Install systemd user service
sed "s|%h|$HOME|g" "$SCRIPT_DIR/$SERVICE" > "$SERVICE_DIR/$SERVICE"

systemctl --user daemon-reload
systemctl --user enable "$SERVICE"
systemctl --user start  "$SERVICE"

echo ""
echo "Done. Check status with:"
echo "  systemctl --user status $SERVICE"
echo "  journalctl --user -u $SERVICE -f"
