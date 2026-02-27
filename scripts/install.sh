#!/bin/sh
# NvFlux installer: build, install setuid root, install man page.
set -e

INSTALL_BIN=/usr/local/bin/nvflux
MAN_DIR=/usr/local/share/man/man1

[ "$(id -u)" -eq 0 ] || { echo "Run as root:  sudo $0" >&2; exit 1; }

./scripts/check-deps.sh || true  # advisory only

echo "Building nvflux..."

if command -v cmake >/dev/null 2>&1 && [ -f CMakeLists.txt ]; then
    cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -Wno-dev
    cmake --build _build --parallel "$(nproc 2>/dev/null || echo 1)"
    BINARY=_build/nvflux
elif command -v gcc >/dev/null 2>&1; then
    gcc -O2 -Wall -I include -I src \
        -o _nvflux_tmp \
        src/main.c src/nvflux.c src/gpu.c src/state.c src/exec.c
    BINARY=_nvflux_tmp
else
    echo "Error: install cmake+make or gcc and retry." >&2; exit 1
fi

[ -x "$BINARY" ] || { echo "Build failed: no executable produced." >&2; exit 1; }

install -Dm755 "$BINARY" "$INSTALL_BIN"
chown root:root "$INSTALL_BIN"
chmod 4755      "$INSTALL_BIN"
echo "Installed $INSTALL_BIN (setuid root)"

if [ -f man/nvflux.1 ]; then
    mkdir -p "$MAN_DIR"
    gzip -c man/nvflux.1 > "$MAN_DIR/nvflux.1.gz"
    command -v mandb >/dev/null 2>&1 && mandb >/dev/null 2>&1 || true
    echo "Installed man page $MAN_DIR/nvflux.1.gz"
fi

# ── Shell completions ─────────────────────────────────────────────────────────

# bash
if [ -f completions/bash/nvflux ]; then
    if command -v pkg-config >/dev/null 2>&1; then
        BASH_COMP_DIR=$(pkg-config --variable=completionsdir bash-completion 2>/dev/null)
    fi
    : "${BASH_COMP_DIR:=/usr/share/bash-completion/completions}"
    [ -d /etc/bash_completion.d ] && [ ! -d "$BASH_COMP_DIR" ] && \
        BASH_COMP_DIR=/etc/bash_completion.d
    mkdir -p "$BASH_COMP_DIR"
    install -Dm644 completions/bash/nvflux "$BASH_COMP_DIR/nvflux"
    echo "Installed bash completion $BASH_COMP_DIR/nvflux"
fi

# zsh
if [ -f completions/zsh/_nvflux ]; then
    # Try common site-functions directories in order of preference
    for d in /usr/share/zsh/site-functions \
              /usr/local/share/zsh/site-functions \
              /usr/share/zsh/vendor-completions; do
        if [ -d "$d" ]; then
            ZSH_COMP_DIR="$d"
            break
        fi
    done
    # If zsh is installed but none of the dirs exist yet, use the first standard path
    if [ -z "$ZSH_COMP_DIR" ] && command -v zsh >/dev/null 2>&1; then
        ZSH_COMP_DIR=/usr/share/zsh/site-functions
    fi
    if [ -n "$ZSH_COMP_DIR" ]; then
        mkdir -p "$ZSH_COMP_DIR"
        install -Dm644 completions/zsh/_nvflux "$ZSH_COMP_DIR/_nvflux"
        echo "Installed zsh completion $ZSH_COMP_DIR/_nvflux"
    fi
fi

# fish
if [ -f completions/fish/nvflux.fish ]; then
    for d in /usr/share/fish/vendor_completions.d \
              /usr/share/fish/completions; do
        if [ -d "$d" ]; then
            FISH_COMP_DIR="$d"
            break
        fi
    done
    if [ -z "$FISH_COMP_DIR" ] && command -v fish >/dev/null 2>&1; then
        FISH_COMP_DIR=/usr/share/fish/vendor_completions.d
    fi
    if [ -n "$FISH_COMP_DIR" ]; then
        mkdir -p "$FISH_COMP_DIR"
        install -Dm644 completions/fish/nvflux.fish "$FISH_COMP_DIR/nvflux.fish"
        echo "Installed fish completion $FISH_COMP_DIR/nvflux.fish"
    fi
fi

# Create state directory owned by the invoking (non-root) user
USER=${SUDO_USER:-$(logname 2>/dev/null || whoami)}
HOME_DIR=$(eval echo "~$USER")
STATE_DIR="$HOME_DIR/.local/state/nvflux"
mkdir -p "$STATE_DIR"
chown "$USER:$USER" "$STATE_DIR" 2>/dev/null || true
echo "State directory: $STATE_DIR"

rm -rf _build _nvflux_tmp 2>/dev/null || true
echo "Done.  Try: nvflux --help"
