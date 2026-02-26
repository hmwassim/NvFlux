#!/bin/sh
# check-deps.sh — verify build and runtime dependencies for nvflux

PASS="[ok]"
FAIL="[missing]"

echo "nvflux dependency check"
echo "─────────────────────────────"

need="cmake gcc make gzip nvidia-smi"
missing=""
for cmd in $need; do
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "  $PASS  $cmd"
    else
        echo "  $FAIL $cmd"
        missing="$missing $cmd"
    fi
done

echo "─────────────────────────────"

if [ -z "$missing" ]; then
    echo "All dependencies present."
    exit 0
fi

echo "Missing:$missing"
echo ""

if [ -f /etc/os-release ]; then
    . /etc/os-release
    dist="${ID_LIKE:-$ID}"
else
    dist=""
fi

echo "Install suggestion for your distro:"
case "$dist" in
    *debian*|*ubuntu*|debian|ubuntu)
        echo "  sudo apt update && sudo apt install build-essential cmake gzip"
        echo "  # NVIDIA: sudo apt install nvidia-utils  (or nvidia-cuda-toolkit)"
        ;;
    *arch*|arch)
        echo "  sudo pacman -Syu base-devel cmake gzip nvidia-utils"
        ;;
    *fedora*|*rhel*|*centos*|fedora|rhel|centos)
        echo "  sudo dnf install @development-tools cmake gzip"
        echo "  # NVIDIA: install via RPM Fusion — https://rpmfusion.org"
        ;;
    *suse*|suse|opensuse*)
        echo "  sudo zypper install -t pattern devel_C_C++ cmake gzip"
        echo "  # NVIDIA: sudo zypper install nvidia-driver"
        ;;
    *void*|void)
        echo "  sudo xbps-install -S base-devel cmake gzip nvidia-utils"
        ;;
    solus)
        echo "  sudo eopkg it -c system.devel"
        echo "  # NVIDIA driver is included in Solus; ensure it is activated"
        ;;
    *)
        echo "  Install a C compiler, cmake, make, gzip, and the NVIDIA driver"
        echo "  utilities (provides nvidia-smi) for your distribution."
        ;;
esac

exit 2
