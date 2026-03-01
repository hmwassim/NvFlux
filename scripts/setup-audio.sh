#!/usr/bin/env bash
#
# setup-audio.sh — optional low-latency audio tuning for HDMI/DP output
#
# Complements 'nvflux powersave' by disabling audio power saving at the
# kernel and audio-server level, eliminating crackling and dropouts that
# are not caused by GPU P-state transitions.
#
# Auto-detects PipeWire (+ WirePlumber) or PulseAudio and applies the
# appropriate configuration. Safe to run more than once.
#
# Usage: ./scripts/setup-audio.sh   (run as your normal user, NOT sudo)
#        Will sudo internally only for the two steps that need root:
#        writing /etc/modprobe.d/ and adding your user to the audio group.
#
set -euo pipefail

[ "$(id -u)" -eq 0 ] && echo "error: run as your normal user, not root." >&2 && exit 1

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
ok()   { echo "    OK: $*"; }
skip() { echo "    skip: $*"; }

# ── 1. Kernel: disable HDA power saving ──────────────────────────────────────
MODPROBE_CONF="/etc/modprobe.d/99-audio-disable-powersave.conf"
MODPROBE_LINE="options snd_hda_intel power_save=0 power_save_controller=N"

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
    WP_CONF_DIR="$HOME/.config/wireplumber/wireplumber.conf.d"
    WP_CONF="$WP_CONF_DIR/51-disable-suspend.conf"

    info "WirePlumber: disabling node suspend"
    if [ -f "$WP_CONF" ]; then
        skip "$WP_CONF already exists"
    else
        mkdir -p "$WP_CONF_DIR"
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
    PA_CONF_DIR="$HOME/.config/pulse"
    PA_DEFAULT_PA="$PA_CONF_DIR/default.pa"
    PA_DAEMON_CONF="$PA_CONF_DIR/daemon.conf"

    info "PulseAudio: disabling suspend-on-idle"
    mkdir -p "$PA_CONF_DIR"

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


die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
ok()   { echo "    OK: $*"; }
skip() { echo "    skip: $*"; }

# ── 1. Kernel: disable HDA power saving ──────────────────────────────────────
MODPROBE_CONF="/etc/modprobe.d/99-audio-disable-powersave.conf"
MODPROBE_LINE="options snd_hda_intel power_save=0 power_save_controller=N"

info "Kernel: disabling snd_hda_intel power saving"
if [ -f "$MODPROBE_CONF" ] && grep -qF "$MODPROBE_LINE" "$MODPROBE_CONF" 2>/dev/null; then
    skip "$MODPROBE_CONF already set"
else
    sudo mkdir -p /etc/modprobe.d
    echo "$MODPROBE_LINE" | sudo tee "$MODPROBE_CONF" > /dev/null
    ok "wrote $MODPROBE_CONF"
fi

# ── 2. Groups: add user to audio group ───────────────────────────────────────
info "Groups: adding $REAL_USER to 'audio'"
if id -nG "$REAL_USER" | grep -qw audio; then
    skip "$REAL_USER already in audio group"
else
    sudo usermod -aG audio "$REAL_USER"
    ok "added $REAL_USER to audio group (re-login to take effect)"
fi

# ── 3. Detect audio server ────────────────────────────────────────────────────
AUDIO_SERVER=""
if systemctl --user --machine="$REAL_USER@.host" is-active --quiet pipewire 2>/dev/null \
   || pgrep -u "$REAL_USER" pipewire &>/dev/null; then
    AUDIO_SERVER="pipewire"
elif systemctl --user --machine="$REAL_USER@.host" is-active --quiet pulseaudio 2>/dev/null \
   || pgrep -u "$REAL_USER" pulseaudio &>/dev/null; then
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
    WP_CONF_DIR="$REAL_HOME/.config/wireplumber/wireplumber.conf.d"
    WP_CONF="$WP_CONF_DIR/51-disable-suspend.conf"

    info "WirePlumber: disabling node suspend"
    if [ -f "$WP_CONF" ]; then
        skip "$WP_CONF already exists"
    else
        sudo -u "$REAL_USER" mkdir -p "$WP_CONF_DIR"
        sudo -u "$REAL_USER" tee "$WP_CONF" > /dev/null << 'EOF'
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
    sudo -u "$REAL_USER" \
        XDG_RUNTIME_DIR="/run/user/$(id -u "$REAL_USER")" \
        systemctl --user restart pipewire wireplumber
    ok "services restarted"
fi

# ── 4b. PulseAudio ────────────────────────────────────────────────────────────
if [ "$AUDIO_SERVER" = "pulseaudio" ]; then
    PA_CONF_DIR="$REAL_HOME/.config/pulse"
    PA_DEFAULT_PA="$PA_CONF_DIR/default.pa"
    PA_DAEMON_CONF="$PA_CONF_DIR/daemon.conf"

    info "PulseAudio: disabling suspend-on-idle"
    sudo -u "$REAL_USER" mkdir -p "$PA_CONF_DIR"

    if [ -f "$PA_DEFAULT_PA" ] && grep -q "module-suspend-on-idle" "$PA_DEFAULT_PA"; then
        skip "$PA_DEFAULT_PA already modified"
    else
        sudo -u "$REAL_USER" tee "$PA_DEFAULT_PA" > /dev/null << 'EOF'
.include /etc/pulse/default.pa
unload-module module-suspend-on-idle
EOF
        ok "wrote $PA_DEFAULT_PA"
    fi

    info "PulseAudio: disabling idle exit"
    if [ -f "$PA_DAEMON_CONF" ] && grep -q "exit-idle-time" "$PA_DAEMON_CONF"; then
        skip "$PA_DAEMON_CONF already modified"
    else
        echo "exit-idle-time = -1" | sudo -u "$REAL_USER" tee -a "$PA_DAEMON_CONF" > /dev/null
        ok "set exit-idle-time = -1 in $PA_DAEMON_CONF"
    fi

    info "Restarting PulseAudio"
    sudo -u "$REAL_USER" \
        XDG_RUNTIME_DIR="/run/user/$(id -u "$REAL_USER")" \
        systemctl --user restart pulseaudio 2>/dev/null \
        || sudo -u "$REAL_USER" pulseaudio --kill && \
           sudo -u "$REAL_USER" pulseaudio --start --daemonize
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
