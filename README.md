# NvFlux

A minimal, setuid-root helper for NVIDIA GPU memory clock locking on Linux.

[![License: MIT](https://img.shields.io/badge/license-MIT-red.svg)](LICENSE)

## Overview

`nvflux` lets desktop users switch NVIDIA GPU profiles without `sudo` by locking memory clocks via `nvidia-smi`. Clock tiers are queried live from the driver — nothing is hard-coded, so it works on any NVIDIA GPU.

| Profile     | Memory clock                       |
|-------------|------------------------------------|
| performance | Locked to highest supported tier   |
| balanced    | Locked to mid-range supported tier |
| powersave   | Locked to lowest supported tier    |
| auto        | Unlocked (driver managed)          |

## Quick Start

```sh
./scripts/check-deps.sh     # check build dependencies
sudo ./scripts/install.sh   # build, install, set setuid bit

nvflux performance           # lock to max clock
nvflux balanced              # lock to mid clock
nvflux powersave             # lock to min clock
nvflux auto                  # unlock
nvflux status                # show saved profile (no root needed)
nvflux clock                 # current mem clock in MHz
nvflux --restore             # re-apply saved profile
```

## Requirements

| Dependency   | Purpose                         |
|--------------|---------------------------------|
| `nvidia-smi` | Clock locking (required)        |
| GCC / Clang  | Build (required)                |
| CMake ≥ 3.10 | Build (preferred; gcc fallback) |

## Autostart

### i3 / Sway
```
exec --no-startup-id nvflux --restore
```

### GNOME / KDE / XFCE
```ini
# ~/.config/autostart/nvflux-restore.desktop
[Desktop Entry]
Type=Application
Name=nvflux restore
Exec=nvflux --restore
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

## Building

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest -V
```

## Source Layout

```
src/
  main.c      entry point + --version
  nvflux.c    command dispatch, profile table, --restore logic
  gpu.c       all nvidia-smi subprocess calls
  gpu.h
  state.c     per-user state file (read/write, recursive mkdir)
  state.h
  exec.c      fork/exec primitives (exec_capture, run_cmd)
  exec.h
include/
  nvflux.h    public API (nvflux_run, nvflux_parse_clocks, version)
tests/
  test_nvflux.c  unit tests (no root / no GPU required)
```

## Security Model

- Input validated against a hardcoded profile table before any action.
- All subprocess calls use `execv` directly — no shell involvement.
- State file owned by real UID (not root), mode 0600.
- See [docs/SECURITY.md](docs/SECURITY.md).

## Uninstall

```sh
sudo ./scripts/uninstall.sh
```

## License

MIT — see [LICENSE](LICENSE).
