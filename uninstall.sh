#!/usr/bin/env bash
# uninstall.sh - Remove nvflux
set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "must run as root (sudo $0)"

echo "==> Removing nvflux binary..."
rm -f /usr/local/bin/nvflux

echo "==> Removing autostart..."
rm -f /etc/xdg/autostart/nvflux-restore.desktop

echo "==> Removing system-wide state..."
rm -rf /var/lib/nvflux

echo "✓ nvflux uninstalled"
