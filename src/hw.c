/*
 * hw.c — NvFlux hardware query / control layer
 *
 * All communication with the NVIDIA driver is done here through
 * nvidia-smi.  Other modules must not invoke nvidia-smi directly.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "../include/hw.h"

/* ------------------------------------------------------------------ */
/*  nvidia-smi path, resolved once by hw_check_runtime()              */
/* ------------------------------------------------------------------ */

static char nvsmipath[256] = "";

static const char * const NVSMI_SEARCH_PATHS[] = {
    "/usr/bin/nvidia-smi",
    "/usr/local/bin/nvidia-smi",
    "/opt/cuda/bin/nvidia-smi",
    NULL
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/*
 * exec_capture — fork/exec argv, capture stdout into buf.
 * stderr is always redirected to /dev/null.
 * Returns the number of bytes written to buf (NUL-terminated),
 * or -1 on fork/exec failure.
 */
static int exec_capture(const char * const *argv, char *buf, int len)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execv(argv[0], (char * const *)argv);
        _exit(127);
    }

    /* parent */
    close(pipefd[1]);

    int total = 0;
    ssize_t n;
    while (total < len - 1 &&
           (n = read(pipefd[0], buf + total, len - 1 - total)) > 0)
        total += (int)n;
    buf[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    return total;
}

/*
 * hw_run_silent — fork/exec argv, discard stdout and stderr.
 * Returns the process exit code, or -1 on fork error.
 */
static int hw_run_silent(const char * const *argv)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execv(argv[0], (char * const *)argv);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

/*
 * parse_sort_clocks — parse a whitespace/comma/newline separated list
 * of integers from txt into clocks[0..max-1], sort descending.
 * Returns the count.  Does NOT deduplicate.
 */
static int parse_sort_clocks(const char *txt, int *clocks, int max)
{
    int n = 0;
    const char *p = txt;
    while (*p && n < max) {
        while (*p && (*p < '0' || *p > '9'))
            p++;
        if (!*p)
            break;
        clocks[n++] = (int)strtol(p, (char **)&p, 10);
    }
    /* insertion sort — arrays are small enough */
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

/*
 * dedup_sorted — remove consecutive duplicates in place from a sorted
 * descending array.  Returns the new count.
 */
static int dedup_sorted(int *clocks, int n)
{
    if (n <= 1)
        return n;
    int out = 1;
    for (int i = 1; i < n; i++)
        if (clocks[i] != clocks[i - 1])
            clocks[out++] = clocks[i];
    return out;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int hw_check_runtime(void)
{
    if (nvsmipath[0] != '\0')   /* already found */
        return 0;

    for (int i = 0; NVSMI_SEARCH_PATHS[i]; i++) {
        if (access(NVSMI_SEARCH_PATHS[i], X_OK) == 0) {
            strncpy(nvsmipath, NVSMI_SEARCH_PATHS[i], sizeof(nvsmipath) - 1);
            break;
        }
    }
    if (nvsmipath[0] == '\0') {
        fprintf(stderr, "nvflux: nvidia-smi not found\n");
        return -1;
    }

    /* confirm a GPU is visible */
    const char * const probe[] = { nvsmipath, "-L", NULL };
    char buf[HW_READ_BUF];
    int n = exec_capture(probe, buf, sizeof(buf));
    if (n <= 0 || strstr(buf, "GPU") == NULL) {
        fprintf(stderr, "nvflux: no NVIDIA GPU detected\n");
        return -1;
    }
    return 0;
}

int hw_enable_persistence(void)
{
    const char * const argv[] = {
        nvsmipath, "-pm", "1", NULL
    };
    return (hw_run_silent(argv) == 0) ? 0 : -1;
}

int hw_get_mem_clocks(int *clocks, int max)
{
    const char * const argv[] = {
        nvsmipath,
        "--query-supported-clocks=memory",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[HW_READ_BUF];
    if (exec_capture(argv, buf, sizeof(buf)) < 0)
        return -1;

    int n = parse_sort_clocks(buf, clocks, max);
    return dedup_sorted(clocks, n);
}

int hw_get_graphics_clocks(int mem_mhz, int *clocks, int max)
{
    char memarg[64];
    snprintf(memarg, sizeof(memarg), "--mem-clock=%d", mem_mhz);

    const char * const argv[] = {
        nvsmipath,
        "--query-supported-clocks=gr",
        "--format=csv,noheader,nounits",
        memarg,
        NULL
    };
    char buf[HW_READ_BUF];
    if (exec_capture(argv, buf, sizeof(buf)) < 0)
        return -1;

    int raw[HW_MAX_CLOCKS];
    int n = parse_sort_clocks(buf, raw, HW_MAX_CLOCKS);

    /* nvidia-smi can inject the memory clock value into the output;
     * strip any entry that equals mem_mhz */
    int out = 0;
    for (int i = 0; i < n && out < max; i++)
        if (raw[i] != mem_mhz)
            clocks[out++] = raw[i];
    out = dedup_sorted(clocks, out);

    if (out >= 2)
        return out;

    /* fallback: global query (no --mem-clock filter) */
    const char * const argv2[] = {
        nvsmipath,
        "--query-supported-clocks=gr",
        "--format=csv,noheader,nounits",
        NULL
    };
    if (exec_capture(argv2, buf, sizeof(buf)) < 0)
        return -1;

    n = parse_sort_clocks(buf, raw, HW_MAX_CLOCKS);
    out = 0;
    for (int i = 0; i < n && out < max; i++)
        if (raw[i] != mem_mhz)
            clocks[out++] = raw[i];
    return dedup_sorted(clocks, out);
}

int hw_get_max_lockable_gfx(void)
{
    const char * const argv[] = {
        nvsmipath,
        "--query-gpu=clocks.max.gr",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[256];
    if (exec_capture(argv, buf, sizeof(buf)) <= 0)
        return -1;
    int v = atoi(buf);
    return (v > 0) ? v : -1;
}

int hw_current_mem_clock(void)
{
    const char * const argv[] = {
        nvsmipath,
        "--query-gpu=clocks.current.memory",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[256];
    if (exec_capture(argv, buf, sizeof(buf)) <= 0)
        return -1;
    int v = atoi(buf);
    return (v > 0) ? v : -1;
}

int hw_current_graphics_clock(void)
{
    const char * const argv[] = {
        nvsmipath,
        "--query-gpu=clocks.current.graphics",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[256];
    if (exec_capture(argv, buf, sizeof(buf)) <= 0)
        return -1;
    int v = atoi(buf);
    return (v > 0) ? v : -1;
}

void hw_current_clocks(int *mem_out, int *gfx_out)
{
    /* Query both clocks in one nvidia-smi invocation: output is "mem, gfx\n" */
    const char * const argv[] = {
        nvsmipath,
        "--query-gpu=clocks.current.memory,clocks.current.graphics",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[256];
    *mem_out = -1;
    *gfx_out = -1;
    if (exec_capture(argv, buf, sizeof(buf)) <= 0)
        return;
    /* parse two comma-separated integers */
    char *p = buf;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    *mem_out = (int)strtol(p, &p, 10);
    /* skip separator (', ') */
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    *gfx_out = (int)strtol(p, NULL, 10);
    if (*mem_out <= 0) *mem_out = -1;
    if (*gfx_out <= 0) *gfx_out = -1;
}

int hw_gpu_temp(void)
{
    const char * const argv[] = {
        nvsmipath,
        "--query-gpu=temperature.gpu",
        "--format=csv,noheader,nounits",
        NULL
    };
    char buf[256];
    if (exec_capture(argv, buf, sizeof(buf)) <= 0)
        return -1;
    int v = atoi(buf);
    return (v > 0) ? v : -1;
}

int hw_lock_memory_clocks(int min_mhz, int max_mhz)
{
    char range[64];
    snprintf(range, sizeof(range), "%d,%d", min_mhz, max_mhz);
    const char * const argv[] = {
        nvsmipath, "--lock-memory-clocks", range, NULL
    };
    return (hw_run_silent(argv) == 0) ? 0 : -1;
}

int hw_lock_graphics_clocks(int min_mhz, int max_mhz)
{
    char range[64];
    snprintf(range, sizeof(range), "%d,%d", min_mhz, max_mhz);
    const char * const argv[] = {
        nvsmipath, "--lock-gpu-clocks", range, NULL
    };
    return (hw_run_silent(argv) == 0) ? 0 : -1;
}

int hw_reset_memory_clocks(void)
{
    const char * const argv[] = {
        nvsmipath, "--reset-memory-clocks", NULL
    };
    return (hw_run_silent(argv) == 0) ? 0 : -1;
}

int hw_reset_graphics_clocks(void)
{
    const char * const argv[] = {
        nvsmipath, "--reset-gpu-clocks", NULL
    };
    return (hw_run_silent(argv) == 0) ? 0 : -1;
}
