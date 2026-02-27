# Installation Guide

## Quick Install

```sh
sudo ./scripts/install.sh
```

Builds the binary, installs to `/usr/local/bin/nvflux` with the setuid root bit, installs the man page, and creates the state directory.

## Dependencies

| Package            | Distro name                              |
|--------------------|------------------------------------------|
| C compiler         | `build-essential` / `base-devel` / `gcc` |
| CMake ≥ 3.10       | `cmake`                                  |
| gzip               | `gzip`                                   |
| nvidia-smi         | `nvidia-utils`                           |

### Debian / Ubuntu
```sh
sudo apt install build-essential cmake gzip nvidia-utils
```

### Arch Linux
```sh
sudo pacman -Syu base-devel cmake gzip nvidia-utils
```

### Fedora
```sh
sudo dnf install @development-tools cmake gzip
# NVIDIA drivers: enable RPM Fusion repository
```

### openSUSE
```sh
sudo zypper install -t pattern devel_C_C++ cmake gzip
```

### Void Linux
```sh
sudo xbps-install -S base-devel cmake gzip nvidia-utils
```

## Manual Build + Install

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

sudo install -Dm4755 nvflux /usr/local/bin/nvflux
sudo gzip -c ../man/nvflux.1 > /usr/local/share/man/man1/nvflux.1.gz
```

## Autostart

### i3 / Sway
```
exec --no-startup-id nvflux --restore
```

### GNOME / KDE / XFCE (XDG autostart)
```ini
# ~/.config/autostart/nvflux-restore.desktop
[Desktop Entry]
Type=Application
Name=nvflux restore
Exec=nvflux --restore
X-GNOME-Autostart-enabled=true
```

### systemd user service
```ini
# ~/.config/systemd/user/nvflux-restore.service
[Unit]
Description=Restore NVIDIA GPU profile
After=graphical-session.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nvflux --restore

[Install]
WantedBy=default.target
```
```sh
systemctl --user enable --now nvflux-restore.service
```

## Uninstall
```sh
sudo ./scripts/uninstall.sh
```
