#!/usr/bin/env bash
#
# setup.sh — unified entry point for NvFlux
#
# Usage (run from the repo root as your normal user):
#
#   ./setup.sh install       build and install nvflux (prompts for sudo)
#   ./setup.sh uninstall     remove nvflux (prompts for sudo)
#   ./setup.sh audio         apply low-latency audio tuning
#   ./setup.sh audio undo    remove audio tuning
#   ./setup.sh help
#
# Each command does exactly one thing. Combine manually if you need more:
#
#   ./setup.sh install && ./setup.sh audio
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

die()  { echo "error: $*" >&2; exit 1; }
usage() {
    cat <<EOF
Usage: ./setup.sh <command>

Commands:
  install          Build and install nvflux  (requires sudo)
  uninstall        Remove nvflux             (requires sudo)
  audio            Apply low-latency audio tuning (PipeWire or PulseAudio)
  audio undo       Remove audio tuning
  help             Show this help
EOF
}

[ $# -eq 0 ] && usage && exit 0

[ -f "$SCRIPT_DIR/scripts/install.sh" ] || \
    die "run this script from the NvFlux repo root (cd NvFlux && ./setup.sh ...)"

CMD="${1:-}"
shift

case "$CMD" in
    install)
        [ $# -eq 0 ] || die "unexpected argument '$1'. Usage: ./setup.sh install"
        "$SCRIPT_DIR/scripts/check-deps.sh"
        sudo "$SCRIPT_DIR/scripts/install.sh"
        ;;

    uninstall)
        [ $# -eq 0 ] || die "unexpected argument '$1'. Usage: ./setup.sh uninstall"
        sudo "$SCRIPT_DIR/scripts/uninstall.sh"
        ;;

    audio)
        SUBCMD="${1:-}"
        case "$SUBCMD" in
            "")
                "$SCRIPT_DIR/scripts/setup-audio.sh"
                ;;
            undo)
                "$SCRIPT_DIR/scripts/setup-audio.sh" --undo
                ;;
            *)
                die "unknown audio subcommand '$SUBCMD'. Usage: ./setup.sh audio [undo]"
                ;;
        esac
        ;;

    help|--help|-h)
        usage
        ;;

    *)
        die "unknown command '$CMD'. Run './setup.sh help' for usage."
        ;;
esac

