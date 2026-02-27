#include "gpu.h"
#include "exec.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * nvidia-smi binary path (resolved once by gpu_find_smi)
 * -------------------------------------------------------------------- */

static char g_nvsmi[PATH_MAX] = {0};

#define READ_BUF 4096

/* -----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

/*
 * find_tool - locate a binary by name.
 * Checks each path in candidates[] first (common install locations),
 * then walks the PATH environment variable.
 * Writes the resolved absolute path into out[] and returns 0.
 * Returns -1 if not found in either place.
 */
static int find_tool(const char *name, const char **candidates,
                     char *out, size_t outlen) {
    /* 1. check well-known paths */
    for (int i = 0; candidates[i]; ++i) {
        if (access(candidates[i], X_OK) == 0) {
            snprintf(out, outlen, "%s", candidates[i]);
            return 0;
        }
    }

    /* 2. walk PATH */
    const char *env_path = getenv("PATH");
    if (!env_path) return -1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", env_path);

    for (char *seg = tmp, *end; seg; seg = end ? end + 1 : NULL) {
        end = strchr(seg, ':');
        if (end) *end = '\0';

        char cand[PATH_MAX];
        int n = snprintf(cand, sizeof(cand), "%s/%s", seg, name);
        if (n > 0 && n < (int)sizeof(cand) && access(cand, X_OK) == 0) {
            snprintf(out, outlen, "%s", cand);
            return 0;
        }
        if (!end) break;
    }
    return -1;
}

/*
 * first_int - parse the first decimal integer found in s.
 * Returns the integer value, or -1 if none found.
 */
static int first_int(const char *s) {
    while (*s && !isdigit((unsigned char)*s)) s++;
    return *s ? (int)strtol(s, NULL, 10) : -1;
}

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

int gpu_find_smi(void) {
    const char *known[] = {
        "/usr/bin/nvidia-smi",
        "/usr/local/bin/nvidia-smi",
        "/bin/nvidia-smi",
        NULL
    };
    return find_tool("nvidia-smi", known, g_nvsmi, sizeof(g_nvsmi));
}

int gpu_check_driver(void) {
    char out[READ_BUF];
    char *argv[] = { g_nvsmi, "--query-gpu=name", "--format=csv,noheader", NULL };

    int rc = exec_capture(argv, out, sizeof(out));
    if (rc < 0) {
        fprintf(stderr, "Error: failed to execute %s.\n", g_nvsmi);
        return -1;
    }
    if (out[0] == '\0' || strstr(out, "No devices were found")) {
        fprintf(stderr,
            "Error: no NVIDIA GPU detected or driver not loaded.\n"
            "Hint:  sudo modprobe nvidia\n");
        return -1;
    }
    return 0;
}

int gpu_mem_clocks(int *clocks, int max) {
    char out[READ_BUF];
    char *argv[] = {
        g_nvsmi,
        "--query-supported-clocks=memory",
        "--format=csv,noheader,nounits",
        NULL
    };
    if (exec_capture(argv, out, sizeof(out)) < 0) return -1;

    /* parse all integers from the output */
    int n = 0;
    const char *p = out;
    while (*p && n < max) {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) break;
        clocks[n++] = (int)strtol(p, (char **)&p, 10);
    }

    /* sort descending (simple selection sort; n is at most GPU_MAX_CLOCKS) */
    for (int i = 0; i < n; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j)
            if (clocks[j] > clocks[best]) best = j;
        if (best != i) { int t = clocks[i]; clocks[i] = clocks[best]; clocks[best] = t; }
    }
    return n;
}

int gpu_current_mem_clock(void) {
    char out[READ_BUF];
    /*
     * nvidia-smi query key changed across driver generations:
     *   clocks.mem   - current drivers (R450+)
     *   memory.clock - older drivers
     * Try both; return the first that yields a number.
     */
    char *q1[] = { g_nvsmi, "--query-gpu=clocks.mem",   "--format=csv,noheader,nounits", NULL };
    char *q2[] = { g_nvsmi, "--query-gpu=memory.clock", "--format=csv,noheader,nounits", NULL };
    char **tries[] = { q1, q2, NULL };

    for (int i = 0; tries[i]; ++i) {
        if (exec_capture(tries[i], out, sizeof(out)) >= 0) {
            int v = first_int(out);
            if (v >= 0) return v;
        }
    }
    return -1;
}

int gpu_enable_persistence(void) {
    /* -pm 1: enable persistence mode so the driver stays loaded between
     * commands and clock locks are not silently dropped on idle. */
    char *argv[] = { g_nvsmi, "-pm", "1", NULL };
    return run_cmd(argv);
}

int gpu_lock_mem(int mhz) {
    char arg[64];
    /* nvidia-smi --lock-memory-clocks expects "min,max"; setting both to
     * the same value locks to exactly that frequency. */
    snprintf(arg, sizeof(arg), "--lock-memory-clocks=%d,%d", mhz, mhz);
    char *argv[] = { g_nvsmi, arg, NULL };
    return run_cmd(argv);
}

int gpu_unlock_mem(void) {
    /* The flag name differs between older and newer driver branches. */
    char *try1[] = { g_nvsmi, "--reset-memory-clocks", NULL };
    char *try2[] = { g_nvsmi, "--reset-locks",         NULL };
    if (run_cmd(try1) == 0) return 0;
    return run_cmd(try2);
}
