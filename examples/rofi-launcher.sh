#!/bin/sh
# rofi / dmenu launcher for nvflux.  Works on X11 and Wayland (via Xwayland).
command -v nvflux >/dev/null 2>&1 || {
    command -v notify-send >/dev/null 2>&1 && notify-send "nvflux" "nvflux not installed"
    echo "Error: nvflux not installed." >&2; exit 1
}

if command -v rofi >/dev/null 2>&1; then
    MENU="rofi -dmenu -p GPU:"
elif command -v dmenu >/dev/null 2>&1; then
    MENU="dmenu -p GPU:"
else
    echo "Error: install rofi or dmenu." >&2; exit 1
fi

choice=$(printf "Performance\nBalanced\nPower Save\nAuto" | $MENU) || exit 0

notify() { command -v notify-send >/dev/null 2>&1 && notify-send "nvflux" "$1" || true; }

case "$choice" in
    "Performance") nvflux performance && notify "Performance" ;;
    "Balanced")    nvflux balanced    && notify "Balanced"    ;;
    "Power Save")  nvflux powersave   && notify "Power Save"  ;;
    "Auto")        nvflux auto        && notify "Auto"        ;;
esac
