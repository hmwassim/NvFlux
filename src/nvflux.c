/*
 * nvflux.c - command dispatch and profile logic
 *
 * This is the only file that ties exec, gpu, and state together.
 * It validates input, applies the requested profile, and persists state.
 *
 * Exit codes (see include/nvflux.h):
 *   0  success
 *   1  operational error
 *   2  nvidia-smi not found
 *   3  not running as root
 *   4  no GPU / driver not loaded
 *   5  unknown command
 */

#include "nvflux.h"
#include "gpu.h"
#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Profile table
 *
 * clock_tier: index semantics into the sorted-descending clock array
 *   0  = highest tier  (performance)
 *   1  = mid tier      (balanced)    → index count/2
 *   2  = lowest tier   (powersave)   → index count-1
 *  -1  = unlock        (auto)
 *
 * persist: whether to enable persistence mode before locking.
 *   Only needed when actually locking clocks; not needed for unlock.
 * -------------------------------------------------------------------- */

typedef struct {
    const char *name;
    int clock_tier;
    int persist;
} Profile;

static const Profile profiles[] = {
    { "performance",  0,  1 },
    { "balanced",     1,  1 },
    { "powersave",    2,  1 },
    { "auto",        -1,  0 },
    { NULL, 0, 0 }
};

/* -----------------------------------------------------------------------
 * nvflux_parse_clocks - exposed for unit testing
 * -------------------------------------------------------------------- */

int nvflux_parse_clocks(const char *txt, int *clocks, int max) {
    int n = 0;
    const char *p = txt;
    while (*p && n < max) {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) break;
        clocks[n++] = (int)strtol(p, (char **)&p, 10);
    }
    /* selection sort descending */
    for (int i = 0; i < n; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j)
            if (clocks[j] > clocks[best]) best = j;
        if (best != i) { int t = clocks[i]; clocks[i] = clocks[best]; clocks[best] = t; }
    }
    return n;
}

/* -----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static void print_help(const char *prog) {
    printf(
        "nvflux %s — NVIDIA GPU memory clock lock\n\n"
        "Usage:\n"
        "  %s <command>\n\n"
        "Commands:\n"
        "  performance    Lock memory to the highest supported clock tier\n"
        "  balanced       Lock memory to the mid-range supported clock tier\n"
        "  powersave      Lock memory to the lowest supported clock tier\n"
        "  auto           Unlock memory clocks (driver managed)\n"
        "  status         Print the last saved profile for this user\n"
        "  clock          Print the current memory clock in MHz\n"
        "  --restore      Re-apply the last saved profile\n"
        "  -v, --version  Print version and exit\n"
        "  -h, --help     Print this help and exit\n\n"
        "Notes:\n"
        "  nvflux must be installed setuid root (scripts/install.sh handles this).\n"
        "  Clock tiers are queried live from the driver; no values are hard-coded.\n",
        NVFLUX_VERSION, prog);
}

/* Lookup a profile by name. Returns NULL if not found. */
static const Profile *find_profile(const char *name) {
    for (const Profile *p = profiles; p->name; ++p)
        if (strcmp(p->name, name) == 0) return p;
    return NULL;
}

/* -----------------------------------------------------------------------
 * nvflux_run
 * -------------------------------------------------------------------- */

int nvflux_run(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command>  (try --help)\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    /* ── status: read-only, no root or GPU needed ───────────────────── */
    if (strcmp(cmd, "status") == 0) {
        char mode[NVFLUX_STATE_MAX] = {0};
        if (state_read((uid_t)getuid(), mode, sizeof(mode)))
            /* capitalise first letter for display: "Performance" */
            printf("%c%s\n", toupper((unsigned char)mode[0]), mode + 1);
        else
            printf("Default (no profile saved)\n");
        return 0;
    }

    /* ── All remaining commands need nvidia-smi ─────────────────────── */
    if (gpu_find_smi() < 0) {
        fprintf(stderr,
            "Error: nvidia-smi not found.\n"
            "Hint:  Install NVIDIA drivers / nvidia-utils for your distro.\n");
        return 2;
    }

    /* ── Privilege check ────────────────────────────────────────────── */
    if (geteuid() != 0) {
        fprintf(stderr,
            "Error: nvflux must run as root.\n"
            "Hint:  sudo scripts/install.sh sets the setuid bit.\n");
        return 3;
    }

    /* ── Driver sanity check ────────────────────────────────────────── */
    if (gpu_check_driver() != 0) return 4;

    uid_t real_uid = (uid_t)getuid();

    /* ── clock: query current MHz ───────────────────────────────────── */
    if (strcmp(cmd, "clock") == 0) {
        int mhz = gpu_current_mem_clock();
        if (mhz < 0) {
            fprintf(stderr, "Error: failed to query current memory clock.\n");
            return 1;
        }
        printf("%d MHz\n", mhz);
        return 0;
    }

    /* ── --restore: load saved profile name and dispatch as that ────── */
    char restored[NVFLUX_STATE_MAX] = {0};
    if (strcmp(cmd, "--restore") == 0) {
        if (!state_read(real_uid, restored, sizeof(restored))) {
            fprintf(stderr,
                "No profile saved.\n"
                "Run 'nvflux performance|balanced|powersave|auto' first.\n");
            return 1;
        }
        /* validate before dispatch so a corrupted state file gives a
         * clear message rather than a misleading "unknown command" */
        if (!find_profile(restored)) {
            fprintf(stderr,
                "Error: saved profile '%s' is not valid.\n"
                "Hint:  Remove ~/.local/state/nvflux/state and set a profile.\n",
                restored);
            return 1;
        }
        cmd = restored;
    }

    /* ── Profile dispatch ───────────────────────────────────────────── */
    const Profile *profile = find_profile(cmd);
    if (!profile) {
        fprintf(stderr, "Unknown command: %s  (try --help)\n", cmd);
        return 5;
    }

    /* Query supported clock tiers (only needed when locking) */
    int clocks[GPU_MAX_CLOCKS];
    int count = 0;
    if (profile->clock_tier >= 0) {
        count = gpu_mem_clocks(clocks, GPU_MAX_CLOCKS);
        if (count <= 0) {
            fprintf(stderr, "Error: failed to query supported memory clocks.\n");
            return 1;
        }
    }

    /* 1. Enable persistence mode so the lock survives driver power-state
     *    transitions (e.g. GPU going idle between commands). */
    if (profile->persist) {
        if (gpu_enable_persistence() != 0) {
            fprintf(stderr, "Error: failed to enable persistence mode.\n");
            return 1;
        }
    }

    /* 2. Apply clock lock or unlock */
    if (profile->clock_tier < 0) {
        /* auto: remove lock entirely */
        if (gpu_unlock_mem() != 0) {
            fprintf(stderr, "Error: failed to unlock memory clocks.\n");
            return 1;
        }
    } else {
        /* Resolve tier index into the sorted clock array:
         *   tier 0 (performance) → index 0         (highest)
         *   tier 1 (balanced)    → index count/2   (middle)
         *   tier 2 (powersave)   → index count-1   (lowest) */
        int idx = (profile->clock_tier == 0) ? 0
                : (profile->clock_tier == 2) ? count - 1
                : count / 2;
        int mhz = clocks[idx];
        if (gpu_lock_mem(mhz) != 0) {
            fprintf(stderr, "Error: failed to lock memory clock to %d MHz.\n", mhz);
            return 1;
        }
        printf("Memory clock locked to %d MHz (%s).\n", mhz, profile->name);
    }

    /* 3. Save profile for future --restore / status */
    state_write(real_uid, profile->name);

    return 0;
}
