#!/usr/bin/env bash
# install.sh - Build and install nvflux
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_BIN="/usr/local/bin/nvflux"

die() { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

[ "$(id -u)" -eq 0 ] || die "must run as root (sudo $0)"

# ──────────────────────────────────────────────────────────────────────────────
# Check if NVIDIA drivers are installed (required)
# ──────────────────────────────────────────────────────────────────────────────
check_nvidia() {
    info "Checking for NVIDIA drivers..."

    # Search common paths for nvidia-smi
    if command -v nvidia-smi >/dev/null 2>&1; then
        info "Found: $(command -v nvidia-smi)"
        return 0
    fi

    for path in /usr/bin/nvidia-smi /usr/local/bin/nvidia-smi; do
        if [ -x "$path" ]; then
            info "Found: $path"
            return 0
        fi
    done

    # Not found - prompt user
    echo ""
    echo "⚠ NVIDIA drivers not detected!"
    echo ""
    echo "Please install NVIDIA drivers for your distribution first,"
    echo "then run: sudo ./install.sh"
    echo ""
    echo "Installation instructions: https://www.nvidia.com/object/unix.html"
    echo ""
    die "Aborting: NVIDIA drivers required"
}

# ──────────────────────────────────────────────────────────────────────────────
# Detect and install build dependencies (works on any distro)
# ──────────────────────────────────────────────────────────────────────────────
install_deps() {
    info "Checking build dependencies..."

    # Check if compiler and make are already installed
    if command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1; then
        info "Build tools already installed"
        return 0
    fi

    # Detect package manager and install dependencies
    if command -v apt-get >/dev/null 2>&1; then
        # Debian, Ubuntu, Linux Mint, Pop!_OS, Kali
        info "Detected APT (Debian/Ubuntu family)"
        apt-get update -qq
        apt-get install -y -qq build-essential make || die "Failed to install dependencies"
    elif command -v dnf >/dev/null 2>&1; then
        # Fedora, RHEL, CentOS 8+
        info "Detected DNF (Fedora/RHEL family)"
        dnf install -y gcc make gcc-c++ >/dev/null || die "Failed to install dependencies"
    elif command -v yum >/dev/null 2>&1; then
        # CentOS 7, older RHEL
        info "Detected YUM (CentOS/RHEL)"
        yum install -y gcc make gcc-c++ >/dev/null || die "Failed to install dependencies"
    elif command -v pacman >/dev/null 2>&1; then
        # Arch Linux, Manjaro, EndeavourOS
        info "Detected Pacman (Arch family)"
        pacman -Sy --noconfirm base-devel make >/dev/null || die "Failed to install dependencies"
    elif command -v zypper >/dev/null 2>&1; then
        # openSUSE
        info "Detected Zypper (openSUSE)"
        zypper install -y -q patterns-devel-base-devel_basis make >/dev/null || \
        zypper install -y -q gcc make >/dev/null || die "Failed to install dependencies"
    elif command -v xbps-install >/dev/null 2>&1; then
        # Void Linux
        info "Detected XBPS (Void Linux)"
        xbps-install -Sy base-devel make >/dev/null || die "Failed to install dependencies"
    elif command -v emerge >/dev/null 2>&1; then
        # Gentoo
        info "Detected Portage (Gentoo)"
        emerge --quiet sys-devel/gcc sys-devel/make || die "Failed to install dependencies"
    elif command -v apk >/dev/null 2>&1; then
        # Alpine Linux
        info "Detected APK (Alpine Linux)"
        apk add --quiet build-base make >/dev/null || die "Failed to install dependencies"
    elif command -v nix-env >/dev/null 2>&1; then
        # NixOS
        info "Detected Nix (NixOS)"
        nix-env -iA nixpkgs.gcc nixpkgs.gnumake >/dev/null || die "Failed to install dependencies"
    elif command -v eopkg >/dev/null 2>&1; then
        # Solus
        info "Detected Eopkg (Solus)"
        eopkg install -y -c system.devel make >/dev/null || die "Failed to install dependencies"
    elif command -v pi >/dev/null 2>&1; then
        # Pardus
        info "Detected Pi (Pardus)"
        pi install -y build-essential make >/dev/null || die "Failed to install dependencies"
    else
        info "Unknown package manager - please install gcc and make manually"
        info "Continuing anyway (build may fail if tools are missing)..."
        return 0
    fi

    info "Dependencies installed"
}

# ──────────────────────────────────────────────────────────────────────────────
# Main installation
# ──────────────────────────────────────────────────────────────────────────────

# Check for NVIDIA drivers first (will abort if not found)
check_nvidia

# Install build dependencies
install_deps

info "Building nvflux..."
make -C "$SCRIPT_DIR" clean >/dev/null 2>&1 || true
make -C "$SCRIPT_DIR"

info "Installing to $INSTALL_BIN..."
make -C "$SCRIPT_DIR" install

info "Cleaning up build artifacts..."
make -C "$SCRIPT_DIR" clean

info "Creating state directory..."
mkdir -p /var/lib/nvflux
chmod 755 /var/lib/nvflux

info "Installing autostart (system-wide)..."
AUTOSTART_DIR="/etc/xdg/autostart"
mkdir -p "$AUTOSTART_DIR"
cat > "$AUTOSTART_DIR/nvflux-restore.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=nvflux
Comment=Restore NVIDIA GPU clock profile
Exec=/usr/local/bin/nvflux --restore
Terminal=false
Categories=Utility;
Hidden=false
X-GNOME-Autostart-enabled=true
X-KDE-autostart-after=panel
EOF
chmod 644 "$AUTOSTART_DIR/nvflux-restore.desktop"

echo ""
echo "✓ nvflux $(nvflux --version) installed"
echo ""
echo "Usage:"
echo "  nvflux powersave     # Lock memory (audio fix)"
echo "  nvflux balanced      # Mid tier"
echo "  nvflux performance   # Highest tier"
echo "  nvflux ultra         # Max clocks"
echo "  nvflux auto          # Unlock (driver-managed)"
echo "  nvflux status        # Show profile"
echo "  nvflux clock         # Show memory clock"
echo ""
echo "Autostart is enabled. Profile will be restored on login."
