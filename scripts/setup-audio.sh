#!/usr/bin/env bash
#
# setup-audio.sh — low-latency audio tuning for HDMI/DP output
#
# Usage:
#   ./scripts/setup-audio.sh          apply audio tuning
#   ./scripts/setup-audio.sh --undo   remove audio tuning
#
# Run as your normal user (NOT sudo).
# Will sudo internally only for /etc/modprobe.d/ and usermod.
#
set -euo pipefail

[ "$(id -u)" -eq 0 ] && echo "error: run as your normal user, not root." >&2 && exit 1

UNDO=0
for arg in "$@"; do
    case "$arg" in
        --undo) UNDO=1 ;;
        *) echo "error: unknown argument '$arg'" >&2; exit 1 ;;
    esac
done

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
ok()   { echo "    OK: $*"; }
skip() { echo "    skip: $*"; }

MODPROBE_CONF="/etc/modprobe.d/99-audio-disable-powersave.conf"
MODPROBE_LINE="options snd_hda_intel power_save=0 power_save_controller=N"
WP_CONF="$HOME/.config/wireplumber/wireplumber.conf.d/51-disable-suspend.conf"
PA_DEFAULT_PA="$HOME/.config/pulse/default.pa"
PA_DAEMON_CONF="$HOME/.config/pulse/daemon.conf"

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# UNDO
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if [ "$UNDO" -eq 1 ]; then
    info "Kernel: removing snd_hda_intel power-save config"
    if [ -f "$MODPROBE_CONF" ]; then
        sudo rm -f "$MODPROBE_CONF"
        ok "removed $MODPROBE_CONF (takes effect on next reboot)"
    else
        skip "$MODPROBE_CONF not found"
    fi

    info "WirePlumber: removing suspend config"
    if [ -f "$WP_CONF" ]; then
        rm -f "$WP_CONF"
        ok "removed $WP_CONF"
    else
        skip "$WP_CONF not found"
    fi

    info "PulseAudio: removing suspend config"
    if [ -f "$PA_DEFAULT_PA" ] && grep -q "module-suspend-on-idle" "$PA_DEFAULT_PA"; then
        rm -f "$PA_DEFAULT_PA"
        ok "removed $PA_DEFAULT_PA"
    else
        skip "$PA_DEFAULT_PA not modified by nvflux"
    fi
    if [ -f "$PA_DAEMON_CONF" ] && grep -q "exit-idle-time" "$PA_DAEMON_CONF"; then
        sed -i '/^exit-idle-time/d' "$PA_DAEMON_CONF"
        ok "removed exit-idle-time from $PA_DAEMON_CONF"
    else
        skip "$PA_DAEMON_CONF not modified by nvflux"
    fi

    info "Restarting audio server"
    if systemctl --user is-active --quiet pipewire 2>/dev/null || pgrep -u "$USER" pipewire &>/dev/null; then
        systemctl --user restart pipewire wireplumber
        ok "PipeWire + WirePlumber restarted"
    elif systemctl --user is-active --quiet pulseaudio 2>/dev/null || pgrep -u "$USER" pulseaudio &>/dev/null; then
        systemctl --user restart pulseaudio 2>/dev/null \
            || { pulseaudio --kill; pulseaudio --start --daemonize; }
        ok "PulseAudio restarted"
    else
        skip "no running audio server detected"
    fi

    echo ""
    echo "Audio tuning removed."
    exit 0
fi

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# APPLY
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# ── 1. Kernel: disable HDA power saving ──────────────────────────────────────
info "Kernel: disabling snd_hda_intel power saving"
if [ -f "$MODPROBE_CONF" ] && grep -qF "$MODPROBE_LINE" "$MODPROBE_CONF" 2>/dev/null; then
    skip "$MODPROBE_CONF already set"
else
    echo "$MODPROBE_LINE" | sudo tee "$MODPROBE_CONF" > /dev/null
    ok "wrote $MODPROBE_CONF (takes effect on next reboot)"
fi

# ── 2. Groups: add user to audio group ───────────────────────────────────────
info "Groups: adding $USER to 'audio'"
if id -nG "$USER" | grep -qw audio; then
    skip "$USER already in audio group"
else
    sudo usermod -aG audio "$USER"
    ok "added $USER to audio group (re-login to take effect)"
fi

# ── 3. Detect audio server ────────────────────────────────────────────────────
AUDIO_SERVER=""
if systemctl --user is-active --quiet pipewire 2>/dev/null \
   || pgrep -u "$USER" pipewire &>/dev/null; then
    AUDIO_SERVER="pipewire"
elif systemctl --user is-active --quiet pulseaudio 2>/dev/null \
   || pgrep -u "$USER" pulseaudio &>/dev/null; then
    AUDIO_SERVER="pulseaudio"
else
    echo ""
    echo "Warning: could not detect a running audio server (PipeWire or PulseAudio)."
    echo "         Kernel setting was applied. Re-run after starting your audio server."
    exit 0
fi
info "Detected audio server: $AUDIO_SERVER"

# ── 4a. PipeWire + WirePlumber ────────────────────────────────────────────────
if [ "$AUDIO_SERVER" = "pipewire" ]; then
    info "WirePlumber: disabling node suspend"
    if [ -f "$WP_CONF" ]; then
        skip "$WP_CONF already exists"
    else
        mkdir -p "$(dirname "$WP_CONF")"
        cat > "$WP_CONF" << 'EOF'
monitor.alsa.rules = [
  {
    matches = [
      { node.name = "~alsa_output.*" }
    ]
    actions = {
      update-props = {
        session.suspend-timeout-seconds = 0
      }
    }
  }
]
EOF
        ok "wrote $WP_CONF"
    fi

    info "Restarting PipeWire + WirePlumber"
    systemctl --user restart pipewire wireplumber
    ok "services restarted"
fi

# ── 4b. PulseAudio ────────────────────────────────────────────────────────────
if [ "$AUDIO_SERVER" = "pulseaudio" ]; then
    info "PulseAudio: disabling suspend-on-idle"
    mkdir -p "$(dirname "$PA_DEFAULT_PA")"

    if [ -f "$PA_DEFAULT_PA" ] && grep -q "module-suspend-on-idle" "$PA_DEFAULT_PA"; then
        skip "$PA_DEFAULT_PA already modified"
    else
        cat > "$PA_DEFAULT_PA" << 'EOF'
.include /etc/pulse/default.pa
unload-module module-suspend-on-idle
EOF
        ok "wrote $PA_DEFAULT_PA"
    fi

    info "PulseAudio: disabling idle exit"
    if [ -f "$PA_DAEMON_CONF" ] && grep -q "exit-idle-time" "$PA_DAEMON_CONF"; then
        skip "$PA_DAEMON_CONF already modified"
    else
        echo "exit-idle-time = -1" >> "$PA_DAEMON_CONF"
        ok "set exit-idle-time = -1 in $PA_DAEMON_CONF"
    fi

    info "Restarting PulseAudio"
    systemctl --user restart pulseaudio 2>/dev/null \
        || { pulseaudio --kill; pulseaudio --start --daemonize; }
    ok "PulseAudio restarted"
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "Audio tuning applied. Changes:"
echo "  - snd_hda_intel power saving:  disabled (takes effect after reboot)"
echo "  - Audio server suspend:        disabled (active now)"
echo "  - User audio group:            set"
echo ""
echo "Combine with:  nvflux powersave  (GPU P-state lock)"
echo "for a complete fix of HDMI/DP audio dropouts."


