#!/bin/sh
# Installs hawser on this machine: the binary and its service.
#
# Usage: sudo scripts/install.sh <path to the hawser binary>
#
# The binary is static, so the same file works on any Linux. Build it with:
#   docker build --target binary --output dist .
#
# This sets nothing up and starts nothing. Afterwards run `hawser init` on the
# coordinator or `hawser join` on any other machine, then:
#   systemctl enable --now hawser
set -eu

[ "$(id -u)" = "0" ] || { echo "install: run this as root"; exit 1; }
binary="${1:?usage: install.sh <path to the hawser binary>}"
[ -f "$binary" ] || { echo "install: $binary is not a file"; exit 1; }

# The kernel part. Present in every Linux since 5.6; this only checks it loads.
if ! modprobe wireguard 2>/dev/null && [ ! -d /sys/module/wireguard ]; then
  echo "install: this kernel has no WireGuard. hawser needs Linux 5.6 or later."
  exit 1
fi

install -m 0755 "$binary" /usr/local/bin/hawser
install -m 0644 "$(dirname "$0")/../packaging/hawser.service" /etc/systemd/system/hawser.service
systemctl daemon-reload

echo "Installed $(/usr/local/bin/hawser version)."
echo "Next: 'hawser init ...' on the coordinator, or 'hawser join <invite>' on any other machine."
echo "Then: systemctl enable --now hawser"
