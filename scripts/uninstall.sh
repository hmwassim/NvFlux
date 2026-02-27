#!/bin/sh
set -e

[ "$(id -u)" -eq 0 ] || { echo "Run as root:  sudo $0" >&2; exit 1; }

for f in /usr/local/bin/nvflux /usr/local/share/man/man1/nvflux.1.gz; do
    [ -e "$f" ] && { rm -f "$f"; echo "Removed $f"; }
done

command -v mandb >/dev/null 2>&1 && mandb >/dev/null 2>&1 || true

# Remove shell completions from all standard locations
for f in \
    /usr/share/bash-completion/completions/nvflux \
    /etc/bash_completion.d/nvflux \
    /usr/share/zsh/site-functions/_nvflux \
    /usr/local/share/zsh/site-functions/_nvflux \
    /usr/share/zsh/vendor-completions/_nvflux \
    /usr/share/fish/vendor_completions.d/nvflux.fish \
    /usr/share/fish/completions/nvflux.fish; do
    [ -e "$f" ] && { rm -f "$f"; echo "Removed $f"; }
done

USER=${SUDO_USER:-$(logname 2>/dev/null || whoami)}
STATE_DIR="$(eval echo "~$USER")/.local/state/nvflux"
[ -d "$STATE_DIR" ] && { rm -rf "$STATE_DIR"; echo "Removed $STATE_DIR"; }

echo "Uninstall complete."
