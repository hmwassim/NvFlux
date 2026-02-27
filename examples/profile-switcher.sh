#!/bin/sh
# Interactive GPU profile switcher using nvflux.
set -e

command -v nvflux >/dev/null 2>&1 || { echo "Error: nvflux not installed." >&2; exit 1; }

printf "Current profile : %s\n" "$(nvflux status)"
printf "Current mem clock: %s\n\n" "$(nvflux clock)"

cat <<'EOF'
  1) Performance  (highest clock tier)
  2) Balanced     (mid clock tier)
  3) Power Save   (lowest clock tier)
  4) Auto         (unlock, driver managed)
  5) Exit
EOF

printf "\nChoice [1-5]: "
read -r choice

case "$choice" in
    1) nvflux performance ;;
    2) nvflux balanced    ;;
    3) nvflux powersave   ;;
    4) nvflux auto        ;;
    5) exit 0             ;;
    *) echo "Invalid choice." >&2; exit 1 ;;
esac

printf "\nNew profile : %s\n" "$(nvflux status)"
printf "New mem clock: %s\n"  "$(nvflux clock)"
