/*
 * nvflux.c — NvFlux profile logic and public API
 *
 * Implements the high-level profile modes (performance, balanced,
 * powersave, auto, reset, status, clock, --restore) by coordinating
 * the hardware layer (hw.h) and the state layer (state.h).
 *
 * nvflux_parse_clocks() is the only function published in nvflux.h and
 * is tested directly by the unit-test suite.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../include/nvflux.h"
#include "../include/hw.h"
#include "../include/state.h"

/* ------------------------------------------------------------------ */
/*  Tuning constants                                                   */
/* ------------------------------------------------------------------ */

/* Graphics-clock percentile (from the top) for each profile */
#define GFX_PCT_PERFORMANCE   5
#define GFX_PCT_BALANCED     50
#define GFX_PCT_POWERSAVE    95

/* Memory-clock percentile (from the top) for each profile */
#define MEM_PCT_PERFORMANCE   0
#define MEM_PCT_BALANCED     50
#define MEM_PCT_POWERSAVE   100

/* Temperature thresholds */
#define TEMP_WARN_C   80          /* print a warning above this           */
#define TEMP_LIMIT_C  90          /* refuse non-powersave above this      */

/* Readback polling */
#define READBACK_POLL_MS     100
#define READBACK_TIMEOUT_MS 1000

/* ------------------------------------------------------------------ */
/*  Public API — nvflux_parse_clocks                                   */
/*                                                                     */
/*  Parses a whitespace/comma/newline-separated integer list from txt, */
/*  stores up to max values in clocks[], sorted descending.            */
/*  Does NOT deduplicate (the unit tests rely on this behaviour).       */
/*  Returns the count.                                                 */
/* ------------------------------------------------------------------ */

int nvflux_parse_clocks(const char *txt, int *clocks, int max) {
    int n = 0;
    const char *p = txt;
    while (*p && n < max) {
        while (*p && (*p < '0' || *p > '9'))
            p++;
        if (!*p)
            break;
        clocks[n++] = (int)strtol(p, (char **)&p, 10);
    }
    /* insertion sort descending */
    for (int i = 1; i < n; i++) {
        int key = clocks[i], j = i - 1;
        while (j >= 0 && clocks[j] < key) {
            clocks[j + 1] = clocks[j];
            j--;
        }
        clocks[j + 1] = key;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Static helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * pick_clock_at_pct — select an index from a sorted-descending array.
 *   pct=0   → clocks[0]          (highest)
 *   pct=100 → clocks[count-1]    (lowest)
 */
static int pick_clock_at_pct(const int *clocks, int count, int pct_from_top) {
    if (count <= 0) return -1;
    int idx = (count - 1) * pct_from_top / 100;
    return clocks[idx];
}

/*
 * filter_lockable_clocks — remove entries above max_lockable in place.
 * Returns the new count.
 */
static int filter_lockable_clocks(int *clocks, int count, int max_lockable)
{
    int out = 0;
    for (int i = 0; i < count; i++)
        if (clocks[i] <= max_lockable)
            clocks[out++] = clocks[i];
    return out;
}

/*
 * mode_display_name — human-readable string for a mode identifier.
 */
static const char *mode_display_name(const char *mode)
{
    if (strcmp(mode, "performance") == 0) return "Performance";
    if (strcmp(mode, "balanced")    == 0) return "Balanced";
    if (strcmp(mode, "powersave")   == 0) return "Power Save";
    if (strcmp(mode, "clock")       == 0) return "Custom Clock";
    if (strcmp(mode, "auto")        == 0) return "Auto";
    return mode;
}

/*
 * read_clocks_stable — poll until hardware clocks have changed away
 * from the before_* snapshot AND settled for two consecutive identical
 * reads.  Fills *out_mem and *out_gfx with the stabilised values.
 * Returns 0 on success, -1 on timeout.
 */
static int read_clocks_stable(int before_mem, int before_gfx,
                               int *out_mem, int *out_gfx)
{
    int prev_mem = -1, prev_gfx = -1;
    int changed = 0;
    int elapsed = 0;

    while (elapsed < READBACK_TIMEOUT_MS) {
        struct timespec ts = { 0, READBACK_POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
        elapsed += READBACK_POLL_MS;

        int cm, cg;
        hw_current_clocks(&cm, &cg);   /* single nvidia-smi invocation */

        if (!changed) {
            if (cm != before_mem || cg != before_gfx)
                changed = 1;
        }

        if (changed && cm == prev_mem && cg == prev_gfx) {
            *out_mem = cm;
            *out_gfx = cg;
            return 0;
        }
        prev_mem = cm;
        prev_gfx = cg;
    }
    /* timeout: return last observed values */
    *out_mem = prev_mem;
    *out_gfx = prev_gfx;
    return -1;
}

/*
 * print_profile_result — clean summary after a profile is applied.
 * Shows requested vs. actual when the driver adjusted the values.
 */
static void print_profile_result(const char *profile_label,
                                  int req_mem,  int req_gfx,
                                  int real_mem, int real_gfx,
                                  int temp)
{
    printf("%s profile applied\n", profile_label);

    if (real_mem > 0 && real_mem != req_mem)
        printf("  Memory:   %d MHz  [driver adjusted from %d MHz]\n",
               real_mem, req_mem);
    else if (real_mem > 0)
        printf("  Memory:   %d MHz\n", real_mem);
    else
        printf("  Memory:   %d MHz (readback unavailable)\n", req_mem);

    if (real_gfx > 0 && real_gfx != req_gfx)
        printf("  Graphics: %d MHz  [driver adjusted from %d MHz]\n",
               real_gfx, req_gfx);
    else if (real_gfx > 0)
        printf("  Graphics: %d MHz\n", real_gfx);
    else
        printf("  Graphics: %d MHz (readback unavailable)\n", req_gfx);

    if (temp > 0)
        printf("  Temp:     %d°C\n", temp);
}

/*
 * check_temp_safety — refuse hot profiles at extreme temperatures.
 * Returns 0 if safe to proceed, -1 if the profile should be blocked.
 */
static int check_temp_safety(int is_hot_profile)
{
    int temp = hw_gpu_temp();
    if (temp < 0)
        return 0;   /* can't read temp — proceed anyway */

    if (temp >= TEMP_LIMIT_C && is_hot_profile) {
        fprintf(stderr,
                "nvflux: GPU at %d°C — refusing high-performance profile "
                "(limit %d°C)\n", temp, TEMP_LIMIT_C);
        return -1;
    }
    if (temp >= TEMP_WARN_C)
        fprintf(stderr,
                "nvflux: warning: GPU temperature is %d°C\n", temp);
    return 0;
}

/*
 * apply_profile — core profile application logic.
 *
 *   mem_pct  — memory clock percentile (0=highest, 100=lowest)
 *   gfx_pct  — graphics clock percentile after filtering
 *   mode_str — identifier written to the state file
 */
static int apply_profile(int mem_pct, int gfx_pct, const char *mode_str,
                          uid_t real_uid)
{
    /* 1. Memory clocks */
    int mem_clocks[HW_MAX_CLOCKS];
    int mem_n = hw_get_mem_clocks(mem_clocks, HW_MAX_CLOCKS);
    if (mem_n <= 0) {
        fprintf(stderr, "nvflux: failed to query memory clocks\n");
        return 1;
    }
    int target_mem = pick_clock_at_pct(mem_clocks, mem_n, mem_pct);

    /* 2. Graphics clocks for this memory level */
    int gfx_clocks[HW_MAX_CLOCKS];
    int gfx_n = hw_get_graphics_clocks(target_mem, gfx_clocks, HW_MAX_CLOCKS);
    if (gfx_n <= 0) {
        fprintf(stderr, "nvflux: failed to query graphics clocks\n");
        return 1;
    }

    /* 3. Strip boost clocks the driver can't actually lock */
    int max_lockable = hw_get_max_lockable_gfx();
    if (max_lockable > 0)
        gfx_n = filter_lockable_clocks(gfx_clocks, gfx_n, max_lockable);
    if (gfx_n <= 0) {
        fprintf(stderr, "nvflux: no lockable graphics clocks available\n");
        return 1;
    }

    int target_gfx = pick_clock_at_pct(gfx_clocks, gfx_n, gfx_pct);

    /* 4. Snapshot current clocks before locking */
    int before_mem, before_gfx;
    hw_current_clocks(&before_mem, &before_gfx);

    /* 5. Apply */
    hw_enable_persistence();

    if (hw_lock_memory_clocks(target_mem, target_mem) < 0) {
        fprintf(stderr, "nvflux: failed to lock memory clocks\n");
        return 1;
    }
    if (hw_lock_graphics_clocks(target_gfx, target_gfx) < 0) {
        fprintf(stderr, "nvflux: failed to lock graphics clocks\n");
        return 1;
    }

    /* 6. Wait for driver to settle.
     * Fast path: if clocks were already at the target (same profile re-applied
     * or driver snapped immediately), skip polling and take a single read. */
    int real_mem = -1, real_gfx = -1;
    if (before_mem == target_mem && before_gfx == target_gfx) {
        hw_current_clocks(&real_mem, &real_gfx);
    } else {
        read_clocks_stable(before_mem, before_gfx, &real_mem, &real_gfx);
    }

    /* 7. Print clean summary */
    int temp = hw_gpu_temp();
    print_profile_result(mode_display_name(mode_str),
                         target_mem, target_gfx,
                         real_mem,   real_gfx,
                         temp);

    /* 8. Persist state */
    nvflux_state_t st;
    memset(&st, 0, sizeof(st));
    strncpy(st.mode, mode_str, sizeof(st.mode) - 1);
    st.memory_mhz   = (real_mem  > 0) ? real_mem  : target_mem;
    st.graphics_mhz = (real_gfx  > 0) ? real_gfx  : target_gfx;
    st.temp_c       = (temp      > 0) ? temp       : 0;
    state_write(real_uid, &st);

    return 0;
}

/*
 * apply_clock — lock to explicit user-specified clock values.
 */
static int apply_clock(int req_mem, int req_gfx, uid_t real_uid)
{
    int before_mem, before_gfx;
    hw_current_clocks(&before_mem, &before_gfx);

    hw_enable_persistence();

    if (hw_lock_memory_clocks(req_mem, req_mem) < 0) {
        fprintf(stderr, "nvflux: failed to lock memory clocks\n");
        return 1;
    }
    if (hw_lock_graphics_clocks(req_gfx, req_gfx) < 0) {
        fprintf(stderr, "nvflux: failed to lock graphics clocks\n");
        return 1;
    }

    int real_mem = -1, real_gfx = -1;
    if (before_mem == req_mem && before_gfx == req_gfx) {
        hw_current_clocks(&real_mem, &real_gfx);
    } else {
        read_clocks_stable(before_mem, before_gfx, &real_mem, &real_gfx);
    }

    int temp = hw_gpu_temp();
    print_profile_result("Custom Clock",
                         req_mem, req_gfx,
                         real_mem, real_gfx,
                         temp);

    nvflux_state_t st;
    memset(&st, 0, sizeof(st));
    strncpy(st.mode, "clock", sizeof(st.mode) - 1);
    st.memory_mhz   = (real_mem > 0) ? real_mem : req_mem;
    st.graphics_mhz = (real_gfx > 0) ? real_gfx : req_gfx;
    st.temp_c       = (temp     > 0) ? temp      : 0;
    state_write(real_uid, &st);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Command handlers                                                   */
/* ------------------------------------------------------------------ */

static int cmd_performance(uid_t real_uid)
{
    if (check_temp_safety(1) < 0)
        return 1;
    return apply_profile(MEM_PCT_PERFORMANCE, GFX_PCT_PERFORMANCE,
                         "performance", real_uid);
}

static int cmd_balanced(uid_t real_uid)
{
    if (check_temp_safety(1) < 0)
        return 1;
    return apply_profile(MEM_PCT_BALANCED, GFX_PCT_BALANCED,
                         "balanced", real_uid);
}

static int cmd_powersave(uid_t real_uid)
{
    return apply_profile(MEM_PCT_POWERSAVE, GFX_PCT_POWERSAVE,
                         "powersave", real_uid);
}

static int cmd_auto(uid_t real_uid)
{
    int temp = hw_gpu_temp();

    if (temp < 0) {
        fprintf(stderr,
                "nvflux: auto: cannot read GPU temperature, "
                "falling back to balanced\n");
        return cmd_balanced(real_uid);
    }

    if (temp >= TEMP_WARN_C) {
        printf("Auto: GPU at %d°C — selecting Power Save\n", temp);
        return cmd_powersave(real_uid);
    }
    if (temp <= 60) {
        printf("Auto: GPU at %d°C — selecting Performance\n", temp);
        return cmd_performance(real_uid);
    }
    printf("Auto: GPU at %d°C — selecting Balanced\n", temp);
    return cmd_balanced(real_uid);
}

static int cmd_reset(uid_t real_uid)
{
    hw_reset_memory_clocks();
    hw_reset_graphics_clocks();
    printf("Clock locks removed — GPU running at driver defaults\n");

    nvflux_state_t st;
    memset(&st, 0, sizeof(st));
    strncpy(st.mode, "reset", sizeof(st.mode) - 1);
    int temp = hw_gpu_temp();
    st.temp_c = (temp > 0) ? temp : 0;
    state_write(real_uid, &st);

    return 0;
}

static int cmd_status(uid_t real_uid)
{
    nvflux_state_t st;
    int have_state = (state_read(real_uid, &st) == 0);

    int live_mem, live_gfx;
    hw_current_clocks(&live_mem, &live_gfx);
    int live_temp = hw_gpu_temp();

    printf("─────────────────────────────\n");
    if (have_state && st.mode[0] != '\0') {
        printf("Profile:  %s\n", mode_display_name(st.mode));
        if (st.timestamp[0] != '\0')
            printf("Applied:  %s\n", st.timestamp);

        if (st.memory_mhz > 0)
            printf("Memory:   %d MHz", st.memory_mhz);
        else
            printf("Memory:   (unknown)");
        if (live_mem > 0 && live_mem != st.memory_mhz)
            printf("  (live: %d MHz)", live_mem);
        else if (live_mem > 0 && st.memory_mhz <= 0)
            printf("  live: %d MHz", live_mem);
        putchar('\n');

        if (st.graphics_mhz > 0)
            printf("Graphics: %d MHz", st.graphics_mhz);
        else
            printf("Graphics: (unknown)");
        if (live_gfx > 0 && live_gfx != st.graphics_mhz)
            printf("  (live: %d MHz)", live_gfx);
        else if (live_gfx > 0 && st.graphics_mhz <= 0)
            printf("  live: %d MHz", live_gfx);
        putchar('\n');

        if (st.temp_c > 0) {
            printf("Temp:     %d°C", st.temp_c);
            if (live_temp > 0 && live_temp != st.temp_c)
                printf("  (now: %d°C)", live_temp);
            putchar('\n');
        } else if (live_temp > 0) {
            printf("Temp:     %d°C\n", live_temp);
        }
    } else {
        printf("Profile:  none\n");
        if (live_mem  > 0) printf("Memory:   %d MHz  (live)\n", live_mem);
        if (live_gfx  > 0) printf("Graphics: %d MHz  (live)\n", live_gfx);
        if (live_temp > 0) printf("Temp:     %d°C\n",           live_temp);
    }
    printf("─────────────────────────────\n");
    return 0;
}

static int cmd_restore(uid_t real_uid)
{
    nvflux_state_t st;
    if (state_read(real_uid, &st) < 0 || st.mode[0] == '\0') {
        fprintf(stderr, "nvflux: --restore: no saved state found\n");
        return 1;
    }

    if (strcmp(st.mode, "performance") == 0) return cmd_performance(real_uid);
    if (strcmp(st.mode, "balanced")    == 0) return cmd_balanced(real_uid);
    if (strcmp(st.mode, "powersave")   == 0) return cmd_powersave(real_uid);
    if (strcmp(st.mode, "auto")        == 0) return cmd_auto(real_uid);
    if (strcmp(st.mode, "reset")       == 0) return cmd_reset(real_uid);

    if (strcmp(st.mode, "clock") == 0) {
        if (st.memory_mhz <= 0 || st.graphics_mhz <= 0) {
            fprintf(stderr,
                    "nvflux: --restore: saved clock values are invalid\n");
            return 1;
        }
        return apply_clock(st.memory_mhz, st.graphics_mhz, real_uid);
    }

    fprintf(stderr, "nvflux: --restore: unknown saved mode '%s'\n", st.mode);
    return 1;
}

static int cmd_clock(int mem_mhz, int gfx_mhz, uid_t real_uid)
{
    if (check_temp_safety(1) < 0)
        return 1;

    int max_lockable = hw_get_max_lockable_gfx();
    if (max_lockable > 0 && gfx_mhz > max_lockable) {
        fprintf(stderr,
                "nvflux: clock: requested graphics clock %d MHz exceeds "
                "max lockable %d MHz — clamping\n", gfx_mhz, max_lockable);
        gfx_mhz = max_lockable;
    }

    return apply_clock(mem_mhz, gfx_mhz, real_uid);
}

/* ------------------------------------------------------------------ */
/*  nvflux_run — main entry point                                      */
/* ------------------------------------------------------------------ */

static void print_help(void)
{
    printf(
        "Usage: nvflux <mode> [options]\n"
        "\n"
        "Modes:\n"
        "  performance        Lock to highest memory clock; near-peak graphics\n"
        "  balanced           Mid-range memory and graphics clocks\n"
        "  powersave          Lowest memory clock; near-floor graphics\n"
        "  auto               Choose profile automatically based on GPU temperature\n"
        "  reset              Remove all clock locks (driver defaults)\n"
        "  status             Show current profile, clocks, and temperature\n"
        "  clock <mem> <gfx>  Lock to specific MHz values\n"
        "\n"
        "Service options:\n"
        "  --restore          Re-apply the last saved profile (for system services)\n"
        "\n"
        "Other options:\n"
        "  --help, -h         Show this help\n"
        "  --version, -v      Show version\n"
    );
}

int nvflux_run(int argc, char **argv)
{
    if (argc < 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }

    const char *mode = argv[1];

    /* Validate mode before anything else */
    static const char * const allowed[] = {
        "performance", "balanced", "powersave",
        "auto", "reset", "status", "clock", "--restore",
        NULL
    };
    int valid = 0;
    for (int i = 0; allowed[i]; i++)
        if (strcmp(mode, allowed[i]) == 0) { valid = 1; break; }

    if (!valid) {
        fprintf(stderr, "nvflux: unknown mode '%s'\n", mode);
        fprintf(stderr, "Run 'nvflux --help' for usage.\n");
        return 1;
    }

    /* Capture the real (non-escalated) user now */
    uid_t real_uid = getuid();

    /* status can run without hardware checks */
    if (strcmp(mode, "status") == 0)
        return cmd_status(real_uid);

    if (hw_check_runtime() < 0)
        return 1;

    if (strcmp(mode, "performance") == 0) return cmd_performance(real_uid);
    if (strcmp(mode, "balanced")    == 0) return cmd_balanced(real_uid);
    if (strcmp(mode, "powersave")   == 0) return cmd_powersave(real_uid);
    if (strcmp(mode, "auto")        == 0) return cmd_auto(real_uid);
    if (strcmp(mode, "reset")       == 0) return cmd_reset(real_uid);
    if (strcmp(mode, "--restore")   == 0) return cmd_restore(real_uid);

    if (strcmp(mode, "clock") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "nvflux: clock mode requires two arguments: "
                    "<memory_mhz> <graphics_mhz>\n");
            return 1;
        }
        int mem_mhz = atoi(argv[2]);
        int gfx_mhz = atoi(argv[3]);
        if (mem_mhz <= 0 || gfx_mhz <= 0) {
            fprintf(stderr,
                    "nvflux: clock: invalid values %s / %s\n",
                    argv[2], argv[3]);
            return 1;
        }
        return cmd_clock(mem_mhz, gfx_mhz, real_uid);
    }

    /* unreachable — already validated above */
    return 1;
}

