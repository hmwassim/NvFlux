# nvflux

NVIDIA GPU clock manager for Linux. Fixes HDMI/DisplayPort audio dropouts by locking memory clocks.

Works on any distro, any desktop, any user. No sudo needed after install.

---

## Quick Start

```bash
git clone https://github.com/hmwassim/nvflux.git
cd nvflux
sudo ./install.sh    # Install (only time you need sudo)
nvflux powersave     # Lock clocks - fixes audio dropouts
```

That's it. The profile sticks across reboots and works for all users.

---

## The Problem

NVIDIA GPUs constantly adjust memory clocks based on load. This causes audio dropouts because the memory controller and HDMI/DP audio share the same clock domain. Every clock change = brief audio glitch.

**Fix:** Lock the memory clock. No more transitions, no more dropouts.

---

## Profiles

| Profile | What it does | When to use |
|---------|--------------|-------------|
| `powersave` | Lowest memory clock | Audio fix, desktop use (recommended) |
| `balanced` | Mid memory clock | Light gaming, general use |
| `performance` | Highest memory clock | Gaming, rendering |
| `ultra` | Max memory + GPU clock | Benchmarking |
| `auto` | Unlock everything | Reset to defaults |

---

## Usage

```bash
nvflux powersave     # Lock to lowest (audio fix)
nvflux balanced      # Lock to mid
nvflux performance   # Lock to highest
nvflux ultra         # Lock everything to max
nvflux auto          # Unlock (driver-managed)

nvflux status        # What's my current profile?
nvflux clocks        # What are my current clocks? (memory + GPU)
```

**No sudo needed** - the binary handles privileges automatically.

---

## How It Works

- **Binary:** `/usr/local/bin/nvflux` (setuid root)
- **State:** `/var/lib/nvflux/state` (shared by all users)
- **Autostart:** `/etc/xdg/autostart/nvflux-restore.desktop` (runs on login for all users)

The setuid bit lets the binary run with root privileges when needed, so you never have to type `sudo`.

---

## Installation

### Requirements

- NVIDIA GPU (Maxwell or newer, Volta+ for full support)
- NVIDIA drivers installed (`nvidia-smi` must exist)
- Any Linux distro

### Install

```bash
git clone https://github.com/hmwassim/nvflux.git
cd nvflux
sudo ./install.sh
```

The installer:
1. Checks for NVIDIA drivers (aborts if missing)
2. Installs build tools if needed (gcc, make)
3. Builds nvflux
4. Sets up system-wide autostart

### Supported Distros

Auto-detects and installs build dependencies for:

- Debian/Ubuntu/Mint/Pop!_OS (APT)
- Fedora/RHEL/CentOS (DNF/YUM)
- Arch/Manjaro (Pacman)
- openSUSE (Zypper)
- Gentoo (Portage)
- Void (XBPS)
- Alpine (APK)
- NixOS (Nix)
- Solus (Eopkg)

If your distro isn't listed, just make sure `gcc` and `make` are installed.

---

## Multi-User Setup

Everything is system-wide:

```bash
# Admin installs and sets profile
sudo ./install.sh
nvflux powersave

# User1 logs in → autostart applies "powersave"
# User2 logs in → autostart applies "powersave"
# User3 logs in → autostart applies "powersave"
```

All users share the same profile. No per-user config needed.

---

## Uninstall

```bash
sudo ./uninstall.sh
```

Removes the binary, autostart, and state file.

---

## Troubleshooting

**"NVIDIA drivers not detected"**
Install drivers for your distro first. Check https://www.nvidia.com/object/unix.html

**"nvflux requires root privileges"**
The setuid bit wasn't set. Re-run: `sudo ./install.sh`

**Audio still dropping out**
Run `nvflux clock` a few times - the number should be stable. If it's changing, the lock didn't apply.

**Want to change profile later**
Just run `nvflux <profile>` - no sudo needed.

---

## Why This Exists

Most GPU clock tools are overengineered - GUI apps, complex configs, systemd dependencies. This is just a single binary that does one thing: locks memory clocks so audio doesn't stutter.

No GUI, no config files, no systemd requirements. Just `nvflux powersave` and you're done.

---

## License

MIT
