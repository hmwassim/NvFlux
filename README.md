# NvFlux

Fix HDMI/DisplayPort audio dropouts on NVIDIA GPUs — and take deterministic control of GPU clock profiles from the command line.

[![License: MIT](https://img.shields.io/badge/license-MIT-red.svg)](LICENSE)

---

## The Problem

If your monitor is connected to your NVIDIA GPU via HDMI or DisplayPort and you use its
built-in speakers or audio output, you have probably heard this: random audio stutters,
brief dropouts, or sudden silence — most often during quiet desktop moments like
scrolling, switching windows, or leaving the machine idle.

This is not a driver bug — it is GPU power management working as designed. NVIDIA
continuously shifts the GPU between P-states based on instantaneous load. The GPU's
memory controller and its HDMI/DP audio controller share the same clock domain. Every
time the driver raises or lowers the memory clock, the audio controller's clock source
is momentarily disrupted and the audio stream drops out.

**The fix is simple: lock the memory clock.**

```bash
git clone https://github.com/hmwassim/NvFlux.git
cd NvFlux
./setup.sh install   # install nvflux
nvflux powersave     # lock the memory clock
nvflux --restore     # add to autostart to survive reboots
```

`powersave` pins to the lowest memory tier — enough to stop P-state transitions, with
minimal extra power draw or fan noise. Use `balanced` or `performance` if you also need
higher memory bandwidth for rendering or gaming.

---

## How It Works

- NVIDIA drivers dynamically scale memory clocks based on instantaneous load
- The memory controller and HDMI/DP audio controller share a clock source
- Memory clock change → audio clock destabilization → dropout
- Locking the memory clock to any fixed tier prevents P-state transitions on the memory bus

NvFlux is a thin, controlled wrapper around `nvidia-smi` clock-locking commands with
strict argument validation, no shell execution, minimal privileged surface, and
XDG-compliant state storage.

---

## Optional: Full Audio Stack Tuning

`nvflux powersave` fixes dropouts caused by GPU P-state transitions. If you still hear
crackling or intermittent dropouts, the audio server itself may be suspending idle
devices. Run the companion command for a complete fix:

```bash
./setup.sh audio
```

What it does:
- Disables `snd_hda_intel` power saving in `/etc/modprobe.d/` (takes effect on reboot)
- Adds your user to the `audio` group
- Disables device suspend in **WirePlumber** (PipeWire setups), or disables
  `module-suspend-on-idle` in **PulseAudio** — auto-detected, no manual config needed
- Restarts the audio server immediately so changes take effect without a reboot

The script is idempotent — safe to run multiple times.

---

## Installation

```bash
git clone https://github.com/hmwassim/NvFlux.git
cd NvFlux
./setup.sh install   # build and install nvflux
```

See [docs/INSTALLATION.md](docs/INSTALLATION.md) for per-distro dependency commands and
autostart setup.

---

## Profiles

| Profile       | Memory clock | GPU core | Use when                                   |
|---------------|--------------|----------|--------------------------------------------|
| `powersave`   | lowest tier  | adaptive | Audio fix, idle desktop, battery saving    |
| `balanced`    | mid tier     | adaptive | General desktop + light rendering          |
| `performance` | max tier     | adaptive | Gaming, GPU compute, heavy rendering       |
| `ultra`       | max tier     | max      | Benchmarking, maximum sustained throughput |
| `auto`        | driver       | driver   | Restore NVIDIA default dynamic behavior    |

---

## Command Reference

```bash
nvflux powersave     # Lock memory to lowest tier  (recommended for audio fix)
nvflux balanced      # Lock memory to mid tier
nvflux performance   # Lock memory to highest tier
nvflux ultra         # Lock memory + GPU core to maximum
nvflux auto          # Unlock everything — driver-managed P-states

nvflux status        # Show the last saved profile
nvflux clock         # Print current memory clock in MHz
nvflux --restore     # Re-apply the last saved profile (use in autostart)
nvflux --version
nvflux --help
```

---

## Autostart

**i3 / Sway** (`~/.config/i3/config` or `~/.config/sway/config`):
```
exec --no-startup-id nvflux --restore
```

**systemd user service** — see [docs/INSTALLATION.md](docs/INSTALLATION.md#autostart-configuration).

---

## Security Model

NvFlux installs a minimal setuid-root binary for clock locking.

- Fixed allowlist of accepted arguments — no arbitrary input reaches `nvidia-smi`
- `nvidia-smi` is exec'd directly — no shell, no string injection
- Privileges are dropped immediately after the privileged operation
- Read-only commands (`status`, `clock`) never elevate
- State file created mode `0600`, always owned by the real (unprivileged) user

See [docs/SECURITY.md](docs/SECURITY.md) for the full analysis.

---

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest -V          # unit tests — no GPU or root required
```

**gcc fallback** (no cmake):
```bash
gcc -O2 -std=c11 -Iinclude \
    src/main.c src/nvidia.c src/profile.c src/state.c \
    -o nvflux
```

---

## Requirements

**Runtime:** NVIDIA drivers with `nvidia-smi` (Volta+ for clock locking) — install
these through your distro's preferred method before running nvflux.

**Build dependencies:** C11 compiler, CMake 3.10+, gzip

**Arch Linux**
```bash
sudo pacman -S base-devel cmake gzip
```
**Debian / Ubuntu**
```bash
sudo apt install build-essential cmake gzip
```
**Fedora**
```bash
sudo dnf install @development-tools cmake gzip
```
**openSUSE**
```bash
sudo zypper install -t pattern devel_C_C++ cmake gzip
```
**Solus**
```bash
sudo eopkg it -c system.devel
```
**Void Linux**
```bash
sudo xbps-install -S base-devel cmake gzip
```

---

## Troubleshooting

**Audio dropout still happens after `nvflux powersave`**  
Run `nvflux clock` several times — the value must be stable. If it keeps changing, the
lock did not apply.

**Memory clock not changing (Hopper / Ada Lovelace GPU)**  
NvFlux automatically falls back to `--lock-memory-clocks-deferred`. The lock takes
effect after a driver reload:
```bash
sudo rmmod nvidia_uvm nvidia_drm nvidia_modeset nvidia && sudo modprobe nvidia
```

**`nvidia-smi not found`**  
Install NVIDIA drivers and ensure `nvidia-smi` is in PATH or `/usr/bin`.

**`No devices were found`**  
```bash
sudo modprobe nvidia
```

**Permission denied after install**  
`ls -l /usr/local/bin/nvflux` must show `-rwsr-xr-x`.
```bash
sudo chown root:root /usr/local/bin/nvflux && sudo chmod 4755 /usr/local/bin/nvflux
```

---

## Architecture

```
include/nvflux.h    — Profile enum, version string
src/
  main.c            — argument parsing, privilege checks, exit codes
  nvidia.h/.c       — all nvidia-smi interaction (find, exec, parse, lock)
  profile.h/.c      — profile apply logic, name ↔ enum mapping
  state.h/.c        — XDG state file (~/.local/state/nvflux/state)
tests/
  test_parse.c      — unit tests (no GPU, no root needed)
```

---

## Uninstallation

```bash
sudo ./scripts/uninstall.sh
```

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE).

## See Also

- [nvidia-smi documentation](https://docs.nvidia.com/deploy/nvidia-smi/index.html)
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html)

