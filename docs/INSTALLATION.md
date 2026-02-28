# Installation Guide

## Quick Install

```bash
# 1. Verify dependencies
./scripts/check-deps.sh

# 2. Build and install
sudo ./scripts/install.sh

# 3. Verify
nvflux --version
ls -l /usr/local/bin/nvflux   # should be -rwsr-xr-x (setuid root)
```

## Manual Build

```bash
# CMake (recommended)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo install -Dm4755 nvflux /usr/local/bin/nvflux
sudo gzip -c ../man/nvflux.1 > /usr/local/share/man/man1/nvflux.1.gz

# Or with gcc directly (no cmake needed)
gcc -O2 -std=c11 -Iinclude \
    src/main.c src/nvidia.c src/profile.c src/state.c \
    -o nvflux
sudo install -Dm4755 nvflux /usr/local/bin/nvflux
```

## Distribution-Specific Dependencies

### Arch Linux
```bash
sudo pacman -S nvidia-utils base-devel cmake gzip
```

### Debian / Ubuntu
```bash
sudo apt install build-essential cmake gzip
```

### Fedora
```bash
sudo dnf install @development-tools cmake gzip
```

### openSUSE
```bash
sudo zypper install -t pattern devel_C_C++ cmake gzip
```

### Solus
```bash
sudo eopkg it -c system.devel
```

### Void Linux
```bash
sudo xbps-install -S base-devel cmake gzip nvidia-utils
```

## Autostart Configuration

nvflux can restore your last profile on login.

### i3 / Sway
Add to `~/.config/i3/config` or `~/.config/sway/config`:
```
exec --no-startup-id nvflux --restore
```

### systemd user service
Create `~/.config/systemd/user/nvflux-restore.service`:
```ini
[Unit]
Description=Restore nvflux GPU profile
After=graphical-session.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nvflux --restore

[Install]
WantedBy=graphical-session.target
```

Enable it:
```bash
systemctl --user enable --now nvflux-restore.service
```

## Shell Completions

The install script handles this automatically. For manual installation:

```bash
# Bash
sudo install -Dm0644 completions/bash/nvflux /etc/bash_completion.d/nvflux

# Fish
sudo install -Dm0644 completions/fish/nvflux.fish \
    /usr/share/fish/vendor_completions.d/nvflux.fish

# Zsh
sudo install -Dm0644 completions/zsh/_nvflux \
    /usr/share/zsh/site-functions/_nvflux
```

## Uninstallation

```bash
sudo ./scripts/uninstall.sh
```

This removes the binary, man page, and shell completions.
User state files (`~/.local/state/nvflux/`) are left intact.
