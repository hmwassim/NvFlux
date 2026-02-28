# NvFlux

A minimal, secure setuid-root helper for unprivileged NVIDIA GPU profile management.

[![License: MIT](https://img.shields.io/badge/license-MIT-red.svg)](LICENSE)

## Overview

nvflux lets desktop users switch NVIDIA GPU power profiles without `sudo` by providing a carefully constrained interface to a handful of `nvidia-smi` operations. Designed for:

- **Desktop Linux users** who want convenient GPU profile switching
- **Laptop users** needing battery vs performance trade-offs
- **Wayland users** where `nvidia-settings` PowerMizer is broken (driver ≤ 580)
- **Anyone using HDMI or DisplayPort audio** from their GPU (see [audio fix](#fix-hdmi--displayport-audio-dropouts) below)

**Key Features:**
- ✅ No security risk (allowlist-only commands, no shell invocation)
- ✅ Clock tiers queried live from the driver — no hard-coded values
- ✅ Hopper+ support via `--lock-memory-clocks-deferred` fallback
- ✅ Per-user state persistence (XDG-compliant)
- ✅ Distro-agnostic, well-tested with unit tests
- ✅ Fixes HDMI/DisplayPort audio dropouts caused by GPU P-state transitions

## Quick Start

```bash
# 1. Check dependencies
./scripts/check-deps.sh

# 2. Install (requires root)
sudo ./scripts/install.sh

# 3. Use it
nvflux ultra         # Lock GPU core + memory to max clocks
nvflux performance   # Lock memory to highest tier
nvflux balanced      # Lock memory to mid-range tier
nvflux powersave     # Lock memory to lowest tier
nvflux auto          # Unlock all clocks (driver-managed)
nvflux status        # Show current profile
```

## Documentation

- **[Installation Guide](docs/INSTALLATION.md)** - Detailed setup per distro
- **[Security Model](docs/SECURITY.md)** - How nvflux stays safe
- **[Contributing](CONTRIBUTING.md)** - Development guidelines

## Profiles

| Command       | Memory clock | GPU core    | 
|---------------|--------------|-------------|
| `ultra`       | max tier     | max         |
| `performance` | max tier     | adaptive    | 
| `balanced`    | mid tier     | adaptive    | 
| `powersave`   | min tier     | adaptive    | 
| `auto`        | driver       | driver      | 

## Usage Examples

### Basic Commands

```bash
nvflux ultra          # Max GPU core + memory
nvflux performance    # Max memory, adaptive GPU core
nvflux balanced       # Mid memory
nvflux powersave      # Min memory

nvflux auto           # unlock everything (driver-managed)

nvflux status         # Show saved profile
nvflux clock          # Print current memory clock in MHz
nvflux --restore      # Re-apply saved profile on login
```

### Fix HDMI / DisplayPort Audio Dropouts

When a monitor is connected to the GPU via HDMI or DisplayPort, its audio is driven by
the GPU's built-in HDMI/DP audio controller — which shares a clock domain with the GPU
memory subsystem. NVIDIA's dynamic power management continuously raises and lowers memory
clocks based on desktop load (P-state transitions). Each transition briefly disrupts the
audio controller's clock source, causing stuttering, dropouts, or sudden silence in the
monitor's speakers. The issue is most noticeable during low-activity moments — idle
desktop, scrolling, switching windows — exactly when the GPU is most aggressive about
dropping to a lower P-state.

Locking the memory clock to any fixed tier prevents the driver from issuing P-state
transitions on the memory bus. The audio controller gets a stable, uninterrupted clock
and the dropouts disappear. `powersave` is the recommended setting: it pins to the
lowest memory tier which is plenty to keep the P-state stable, while keeping power
consumption and fan noise at their minimum:

```bash
nvflux powersave
nvflux --restore   # add this to your autostart to re-apply after reboot
```

Use `balanced` or `performance` if you also want higher memory bandwidth for rendering.

### Autostart Setup

**i3 / Sway** (`~/.config/i3/config`):
```
exec --no-startup-id nvflux --restore
```

**systemd user service** — see [docs/INSTALLATION.md](docs/INSTALLATION.md#autostart-configuration)

### Integration Examples

See [examples/](examples/) for a shell menu and a rofi launcher.

## Architecture

```
nvflux_new/
├── include/nvflux.h    # Shared types: Profile enum, version
└── src/
    ├── main.c          # Argument parsing, dispatch, exit codes
    ├── nvidia.h/.c     # All nvidia-smi interaction (find, exec, parse, lock)
    ├── profile.h/.c    # Profile apply logic, name ↔ enum mapping
    └── state.h/.c      # XDG state file read/write
```

A static library (`nvflux_core`) containing `nvidia.c`, `profile.c`, and `state.c` is
linked into both the binary and the test suite, so tests run without root.

**Security model:**
- All commands validated against a fixed allowlist before touching nvidia-smi
- nvidia-smi is exec'd directly — no shell, no string injection
- State file created with mode 0600, owned by the real (unprivileged) UID
- See [docs/SECURITY.md](docs/SECURITY.md) for the full analysis

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest -V        # Run tests
```

**gcc fallback** (no cmake):
```bash
gcc -O2 -std=c11 -Iinclude \
    src/main.c src/nvidia.c src/profile.c src/state.c \
    -o nvflux
```

## Requirements

- **Runtime:** NVIDIA drivers with `nvidia-smi` (Volta+ for clock locking)
- **Build:** C11 compiler, CMake 3.10+, gzip

| Distro        | Command                                               |
|---------------|-------------------------------------------------------|
| Arch Linux    | `sudo pacman -S nvidia-utils base-devel cmake gzip`   |
| Debian/Ubuntu | `sudo apt install build-essential cmake gzip` |
| Fedora        | `sudo dnf install @development-tools cmake gzip`      |
| openSUSE      | `sudo zypper install -t pattern devel_C_C++ cmake gzip` |
| Solus         | `sudo eopkg it -c system.devel`                       |
| Void Linux    | `sudo xbps-install -S base-devel cmake gzip nvidia-utils` |

## Testing

```bash
cd build && ctest -V
```

Tests cover: clock CSV parsing (sorted output, units, empty input, capping), and full
`profile_from_str` / `profile_to_str` round-trips for all five profiles.

## Troubleshooting

**"nvidia-smi not found"**  
Install NVIDIA drivers for your distro; ensure `nvidia-smi` is in PATH or `/usr/bin`.

**"No devices were found"**  
The kernel module isn't loaded: `sudo modprobe nvidia`

**"Permission denied" after install**  
Check: `ls -l /usr/local/bin/nvflux` should show `-rwsr-xr-x`  
Fix: `sudo chown root:root /usr/local/bin/nvflux && sudo chmod 4755 /usr/local/bin/nvflux`

**Memory clock not changing (Hopper+ GPU)**  
nvflux detects this automatically and falls back to `--lock-memory-clocks-deferred`.
The setting takes effect after a driver reload:
```bash
sudo rmmod nvidia_uvm nvidia_drm nvidia_modeset nvidia
sudo modprobe nvidia
```

## Uninstallation

```bash
sudo ./scripts/uninstall.sh
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Before submitting a PR:
1. Add / update unit tests for any logic changes
2. Run `ctest` to verify
3. Note any security implications when touching privilege code

## License

See [LICENSE](LICENSE).

## See Also

- [nvidia-smi documentation](https://docs.nvidia.com/deploy/nvidia-smi/index.html)
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html)
