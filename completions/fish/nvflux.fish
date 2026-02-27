# fish completion for nvflux
# Install to:
#   /usr/share/fish/vendor_completions.d/nvflux.fish   (system-wide)
#   ~/.config/fish/completions/nvflux.fish             (user)

# Suppress default file completion for nvflux
complete -c nvflux -f

# Helper: true when no subcommand has been given yet
set -l __nvflux_cmds performance balanced powersave auto status

# ── Subcommands ────────────────────────────────────────────────────────────────
complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -a performance \
    -d 'Lock memory clock to the highest supported tier'

complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -a balanced \
    -d 'Lock memory clock to the mid-range tier'

complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -a powersave \
    -d 'Lock memory clock to the lowest supported tier'

complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -a auto \
    -d 'Unlock memory clocks (driver-managed frequency)'

complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -a status \
    -d 'Show current lock state and last saved profile'

# ── Flags ──────────────────────────────────────────────────────────────────────
complete -c nvflux \
    -n "not __fish_seen_subcommand_from $__nvflux_cmds" \
    -l restore \
    -d 'Re-apply the last saved profile'

complete -c nvflux -l version -s v -d 'Print version and exit'
complete -c nvflux -l help    -s h -d 'Print usage information and exit'
