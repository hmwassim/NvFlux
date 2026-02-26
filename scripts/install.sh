#!/bin/sh
# install.sh — build and install nvflux
set -e

OUT=/usr/local/bin/nvflux
MANDIR=/usr/local/share/man/man1
MAN_SRC=man/nvflux.1

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer must be run as root (use sudo)." >&2
    exit 1
fi

# ── dependency check ─────────────────────────────────────────────────
echo "Checking build dependencies..."

missing=""
need() { command -v "$1" >/dev/null 2>&1 || missing="$missing $1"; }

need cmake
need gcc
need make
need nvidia-smi

if [ -n "$missing" ]; then
    echo "Warning: missing tools:$missing"

    if [ -f /etc/os-release ]; then
        . /etc/os-release
        dist="${ID_LIKE:-$ID}"
    else
        dist=""
    fi

    echo "Install suggestion:"
    case "$dist" in
        *debian*|*ubuntu*|debian|ubuntu)
            echo "  sudo apt update && sudo apt install build-essential cmake gzip"
            echo "  # NVIDIA: sudo apt install nvidia-utils"
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
            ;;
        *void*|void)
            echo "  sudo xbps-install -S base-devel cmake gzip nvidia-utils"
            ;;
        solus)
            echo "  sudo eopkg it -c system.devel"
            ;;
        *)
            echo "  Install a C compiler, cmake, make, gzip, and the NVIDIA driver utilities."
            ;;
    esac
    echo "Continuing; build may fail if critical tools are absent."
fi

# ── build ─────────────────────────────────────────────────────────────
echo "Building nvflux..."

if command -v cmake >/dev/null 2>&1 && [ -f CMakeLists.txt ]; then
    rm -rf build
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DCMAKE_C_COMPILER=gcc --log-level=WARNING 2>&1 | sed '/^--/d'
    make -j"$(nproc 2>/dev/null || echo 1)" VERBOSE=0 2>&1 | sed '/^\[.*[0-9]%\]/d'
    BUILT_BIN="$(pwd)/nvflux"
    cd ..
else
    if ! command -v gcc >/dev/null 2>&1; then
        echo "gcc not found — install a C compiler and rerun." >&2
        exit 1
    fi
    gcc -O2 -Wall -Iinclude -o nvflux \
        src/main.c src/nvflux.c src/hw.c src/state.c
    BUILT_BIN="$(pwd)/nvflux"
fi

if [ ! -x "$BUILT_BIN" ]; then
    echo "Build failed: binary not found at $BUILT_BIN" >&2
    exit 1
fi

# ── install ───────────────────────────────────────────────────────────
echo "Installing $BUILT_BIN -> $OUT"
install -Dm755 "$BUILT_BIN" "$OUT"
chown root:root "$OUT"
chmod 4755 "$OUT"

if [ -f "$MAN_SRC" ]; then
    mkdir -p "$MANDIR"
    gzip -c "$MAN_SRC" > "$MANDIR/nvflux.1.gz"
    echo "Installed man page -> $MANDIR/nvflux.1.gz"
    command -v mandb >/dev/null 2>&1 && mandb >/dev/null 2>&1 || true
fi

# ── state dir ─────────────────────────────────────────────────────────
user="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"
home_dir="$(eval echo "~$user")"
state_dir="$home_dir/.local/state/nvflux"
mkdir -p "$state_dir"
chown -R "$user":"$user" "$home_dir/.local" 2>/dev/null || true

# ── done ─────────────────────────────────────────────────────────────
echo "─────────────────────────────"
echo "nvflux installed successfully"
echo "  Binary:  $OUT"
echo "  Running: nvflux --help"
echo "─────────────────────────────"
