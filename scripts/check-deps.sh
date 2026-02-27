#!/bin/sh
REQUIRED="gcc cmake make nvidia-smi gzip"
ok=1; missing=""

for cmd in $REQUIRED; do
    command -v "$cmd" >/dev/null 2>&1 || { missing="$missing $cmd"; ok=0; }
done

[ "$ok" -eq 1 ] && { echo "All required dependencies found."; exit 0; }

echo "Missing:$missing"
distro=""
[ -f /etc/os-release ] && { . /etc/os-release; distro="${ID_LIKE:-$ID}"; }
echo "Install suggestions:"
case "$distro" in
    *debian*|*ubuntu*|debian|ubuntu) echo "  sudo apt install build-essential cmake gzip nvidia-utils" ;;
    *arch*|arch)                     echo "  sudo pacman -Syu base-devel cmake gzip nvidia-utils" ;;
    *rhel*|*fedora*|fedora)          echo "  sudo dnf install @development-tools cmake gzip  # NVIDIA: RPM Fusion" ;;
    *suse*|suse)                     echo "  sudo zypper install -t pattern devel_C_C++ cmake gzip" ;;
    *void*|void)                     echo "  sudo xbps-install -S base-devel cmake gzip nvidia-utils" ;;
    *)                               echo "  Install a C compiler, cmake/make, gzip, and NVIDIA driver utilities." ;;
esac
exit 2
