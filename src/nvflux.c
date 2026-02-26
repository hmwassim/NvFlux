#include "nvflux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <limits.h>

#define MAX_CLOCKS 512   /* large enough for any GPU; deduplicated after sort */
#define READ_BUF 4096

/* Target percentile (from high end, 0=max clock, 100=min clock) per profile.
 * These drive pick_clock_at_pct() and are the only place to tune behavior. */
#define GFX_PCT_PERFORMANCE  5   /* near top:    ~95th pct by frequency  */
#define GFX_PCT_BALANCED    50   /* middle:      ~50th pct by frequency  */
#define GFX_PCT_POWERSAVER  95   /* near bottom: ~5th  pct by frequency  */

/* Temperature thresholds (degrees Celsius).
 * At WARN, the user is notified but the profile is still applied.
 * At LIMIT, applying a hotter profile (performance/balanced) is refused. */
#define TEMP_WARN_C   80
#define TEMP_LIMIT_C  90

/* How long to wait for the driver to settle after locking clocks.
 * Polls every READBACK_POLL_MS up to READBACK_TIMEOUT_MS total. */
#define READBACK_POLL_MS     100
#define READBACK_TIMEOUT_MS  2000

/* allowed commands */
static const char *allowed_cmds[] = {
    "performance", "balanced", "powersaver", "auto", "reset", "status", "clock", "--restore", NULL
};

/* discovered nvidia-smi path */
static char nvsmipath[512] = {0};

/* find nvidia-smi in common locations or PATH */
static int find_nvidia_smi(void) {
    const char *candidates[] = { "/usr/bin/nvidia-smi", "/usr/local/bin/nvidia-smi", "/bin/nvidia-smi", NULL };
    for (int i = 0; candidates[i]; ++i) {
        if (access(candidates[i], X_OK) == 0) {
            snprintf(nvsmipath, sizeof(nvsmipath), "%s", candidates[i]);
            return 0;
        }
    }
    const char *path = getenv("PATH");
    if (!path) return -1;
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    char *p = tmp;
    while (p) {
        char *colon = strchr(p, ':');
        if (colon) *colon = '\0';
        char cand[PATH_MAX];
        if (strlen(p) + 12 > sizeof(cand)) {
            if (!colon) break;
            p = colon + 1;
            continue;
        }
        snprintf(cand, sizeof(cand), "%s/nvidia-smi", p);
        if (access(cand, X_OK) == 0) {
            if (strlen(cand) < sizeof(nvsmipath)) {
                snprintf(nvsmipath, sizeof(nvsmipath), "%s", cand);
                return 0;
            }
        }
        if (!colon) break;
        p = colon + 1;
    }
    return -1;
}

/* state path for real user */
static void get_state_path(uid_t real_uid, char *out, size_t len) {
    struct passwd *pw = getpwuid(real_uid);
    const char *home = pw ? pw->pw_dir : NULL;
    if (!home) home = "/";
    snprintf(out, len, "%s/.local/state/nvflux/state", home);
}

/* write mode to state file (owned by real user) */
static int write_state(uid_t real_uid, const char *mode) {
    char path[PATH_MAX];
    get_state_path(real_uid, path, sizeof(path));
    char dir[PATH_MAX];
    strncpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755); /* ignore errors */
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    if (fchown(fd, real_uid, -1) < 0) {
        /* ignore; best effort */
    }
    ssize_t w = write(fd, mode, strlen(mode));
    close(fd);
    return (w == (ssize_t)strlen(mode)) ? 0 : -1;
}

/* read mode from state file; returns 1 on success */
static int read_state(uid_t real_uid, char *buf, size_t len) {
    char path[PATH_MAX];
    get_state_path(real_uid, path, sizeof(path));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t r = read(fd, buf, len-1);
    close(fd);
    if (r <= 0) return 0;
    buf[r] = '\0';
    /* strip trailing newlines/spaces */
    while (r > 0 && isspace((unsigned char)buf[r-1])) { buf[r-1] = '\0'; r--; }
    return 1;
}

/* execute argv (argv[0]=path) and capture stdout into outbuf (NUL-terminated).
 * if capture_stderr is non-zero, stderr is merged into the output buffer;
 * otherwise stderr is silenced (redirected to /dev/null) so that nvidia-smi
 * error/warning messages cannot contaminate parsed numeric output. */
static int exec_capture(char *const argv[], char *outbuf, size_t outlen, int capture_stderr) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (capture_stderr) {
            dup2(pipefd[1], STDERR_FILENO);
        } else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        }
        close(pipefd[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    /* parent */
    close(pipefd[1]);
    ssize_t total = 0;
    while (total < (ssize_t)(outlen - 1)) {
        ssize_t r = read(pipefd[0], outbuf + total, outlen - 1 - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        total += r;
    }
    close(pipefd[0]);
    outbuf[total] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* parse integers from CSV/noheader output into clocks array (descending, deduped).
 * nvidia-smi may repeat the same graphics clock values once per memory clock level;
 * deduplication ensures percentile math operates on unique clock steps only. */
int nvflux_parse_clocks(const char *txt, int *clocks, int max) {
    int count = 0;
    const char *p = txt;
    while (*p && count < max) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        long v = strtol(p, (char **)&p, 10);
        clocks[count++] = (int)v;
        while (*p && (*p == ',' || isspace((unsigned char)*p))) p++;
    }
    /* sort descending */
    for (int i = 0; i < count; ++i) {
        int best = i;
        for (int j = i+1; j < count; ++j) if (clocks[j] > clocks[best]) best = j;
        if (best != i) {
            int tmp = clocks[i]; clocks[i] = clocks[best]; clocks[best] = tmp;
        }
    }
    /* deduplicate: remove consecutive equal values */
    int unique = 0;
    for (int i = 0; i < count; ++i) {
        if (unique == 0 || clocks[i] != clocks[unique - 1])
            clocks[unique++] = clocks[i];
    }
    return unique;
}

/* Select a clock from a sorted-descending deduplicated array by percentile.
 * pct_from_top=0 → highest clock, pct_from_top=100 → lowest clock.
 * Works correctly for any GPU regardless of how many clock steps it exposes. */
static int pick_clock_at_pct(const int *clocks, int count, int pct_from_top) {
    if (count <= 0) return -1;
    int idx = (count - 1) * pct_from_top / 100;
    return clocks[idx];
}

/* get supported memory clocks */
static int get_mem_clocks(int *clocks, int max) {
    char out[READ_BUF];
    char *argv[] = { nvsmipath, "--query-supported-clocks=memory", "--format=csv,noheader,nounits", NULL };
    int rc = exec_capture(argv, out, sizeof(out), 0);
    if (rc < 0) return -1;
    return nvflux_parse_clocks(out, clocks, max);
}

/* get current memory clock */
static int get_current_mem_clock(void) {
    char out[READ_BUF];
    /* try a couple of query forms for compatibility with different nvidia-smi versions */
    char *q1[] = { nvsmipath, "--query-gpu=clocks.mem", "--format=csv,noheader,nounits", NULL };
    char *q2[] = { nvsmipath, "--query-gpu=memory.clock", "--format=csv,noheader,nounits", NULL };

    if (exec_capture(q1, out, sizeof(out), 0) >= 0) {
        /* parse first integer */
        const char *p = out;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) return -1;
        return (int)strtol(p, NULL, 10);
    }

    if (exec_capture(q2, out, sizeof(out), 0) >= 0) {
        const char *p = out;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) return -1;
        return (int)strtol(p, NULL, 10);
    }

    return -1;
}

/* run nvidia-smi without capture */
static int run_nvsmicmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* enable persistence mode */
static int enable_persistence(void) {
    char *argv[] = { nvsmipath, "-pm", "1", NULL }; /* fallback short option */
    return run_nvsmicmd(argv);
}

/* lock memory clocks to memclk (best-effort) */
static int lock_memory_clocks(int memclk) {
    char arg[64];
    /* some nvidia-smi variants expect two values (min,max) */
    snprintf(arg, sizeof(arg), "--lock-memory-clocks=%d,%d", memclk, memclk);
    char *argv[] = { nvsmipath, arg, NULL };
    return run_nvsmicmd(argv);
}

/* reset memory clocks (unlock) */
static int reset_memory_clocks(void) {
    /* older/newer nvidia-smi have slightly different option names; try both */
    char *try1[] = { nvsmipath, "--reset-memory-clocks", NULL };
    char *try2[] = { nvsmipath, "--reset-locks", NULL };

    if (run_nvsmicmd(try1) == 0) return 0;
    return run_nvsmicmd(try2);
}

/* get all supported graphics clocks (sorted descending).
 * NOTE: do NOT pass --mem-clock here — nvidia-smi prepends the mem clock value
 * to every CSV row when that option is used, which contaminates the parsed
 * output.  All memory clock levels share the same graphics clock range anyway. */
static int get_graphics_clocks(int *clocks, int max) {
    char out[READ_BUF];
    char *argv[] = { nvsmipath, "--query-supported-clocks=graphics", "--format=csv,noheader,nounits", NULL };
    int rc = exec_capture(argv, out, sizeof(out), 0);
    if (rc < 0) return -1;
    return nvflux_parse_clocks(out, clocks, max);
}

/* Query the actual maximum lockable (non-boost) graphics clock.
 * --query-supported-clocks includes boost clocks that --lock-gpu-clocks cannot
 * honor; clocks.max.gr returns the real ceiling the driver will enforce.
 * Returns -1 if the query fails (caller should skip filtering in that case). */
static int get_max_lockable_gfx_clock(void) {
    char out[READ_BUF];
    char *q[] = { nvsmipath, "--query-gpu=clocks.max.gr", "--format=csv,noheader,nounits", NULL };
    if (exec_capture(q, out, sizeof(out), 0) < 0) return -1;
    const char *p = out;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return -1;
    return (int)strtol(p, NULL, 10);
}

/* Remove boost clocks from a sorted-descending deduplicated array in-place.
 * Any value above max_lockable is a boost clock the driver won't actually lock
 * to; strip them so percentile selection operates only on real lock targets. */
static int filter_lockable_clocks(int *clocks, int count, int max_lockable) {
    if (max_lockable <= 0) return count; /* query failed; keep all */
    int out = 0;
    for (int i = 0; i < count; ++i)
        if (clocks[i] <= max_lockable)
            clocks[out++] = clocks[i];
    return out;
}

/* get current graphics clock */
static int get_current_graphics_clock(void) {
    char out[READ_BUF];
    char *q1[] = { nvsmipath, "--query-gpu=clocks.gr", "--format=csv,noheader,nounits", NULL };
    char *q2[] = { nvsmipath, "--query-gpu=clocks.current.graphics", "--format=csv,noheader,nounits", NULL };

    if (exec_capture(q1, out, sizeof(out), 0) >= 0) {
        const char *p = out;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (*p) return (int)strtol(p, NULL, 10);
    }
    if (exec_capture(q2, out, sizeof(out), 0) >= 0) {
        const char *p = out;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (*p) return (int)strtol(p, NULL, 10);
    }
    return -1;
}

/* query GPU temperature in degrees Celsius; returns -1 on failure */
static int get_gpu_temp(void) {
    char out[READ_BUF];
    char *argv[] = { nvsmipath, "--query-gpu=temperature.gpu", "--format=csv,noheader,nounits", NULL };
    if (exec_capture(argv, out, sizeof(out), 0) < 0) return -1;
    const char *p = out;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return -1;
    return (int)strtol(p, NULL, 10);
}

/* Poll until clocks change away from (before_mem, before_gfx) AND two
 * consecutive reads agree, meaning the driver has fully applied the new lock.
 * Falls back to whatever was last read if the timeout expires. */
static void read_clocks_stable(int before_mem, int before_gfx, int *out_mem, int *out_gfx) {
    int prev_mem = -1, prev_gfx = -1;
    int elapsed = 0;
    while (elapsed < READBACK_TIMEOUT_MS) {
        usleep(READBACK_POLL_MS * 1000);
        elapsed += READBACK_POLL_MS;
        int mem = get_current_mem_clock();
        int gfx = get_current_graphics_clock();
        /* Wait until the values have moved away from the pre-lock snapshot */
        if (mem == before_mem && gfx == before_gfx) {
            prev_mem = mem; prev_gfx = gfx;
            continue;
        }
        /* Then wait for two identical reads in a row (settled) */
        if (mem == prev_mem && gfx == prev_gfx && mem > 0) {
            *out_mem = mem;
            *out_gfx = gfx;
            return;
        }
        prev_mem = mem;
        prev_gfx = gfx;
    }
    /* timeout: return whatever we last read */
    *out_mem = prev_mem > 0 ? prev_mem : before_mem;
    *out_gfx = prev_gfx > 0 ? prev_gfx : before_gfx;
}

/* Check GPU temperature and refuse or warn before applying a profile.
 * hot_profile: 1 if this is performance/balanced (higher load); 0 for powersaver. */
static int check_temp_safety(int hot_profile) {
    int temp = get_gpu_temp();
    if (temp < 0) return 0; /* can't read temp, allow */
    if (hot_profile && temp >= TEMP_LIMIT_C) {
        fprintf(stderr,
            "Error: GPU temperature is %d°C (>= %d°C limit).\n"
            "       Cool down the GPU before applying a high-performance profile.\n",
            temp, TEMP_LIMIT_C);
        return -1;
    }
    if (temp >= TEMP_WARN_C) {
        fprintf(stderr,
            "Warning: GPU temperature is %d°C (>= %d°C threshold). "
            "Monitor thermals.\n", temp, TEMP_WARN_C);
    }
    return 0;
}

/* lock graphics clocks to gfxclk (best-effort) */
static int lock_graphics_clocks(int gfxclk) {
    char arg[64];
    snprintf(arg, sizeof(arg), "--lock-gpu-clocks=%d,%d", gfxclk, gfxclk);
    char *argv[] = { nvsmipath, arg, NULL };
    return run_nvsmicmd(argv);
}

/* reset graphics clocks (unlock) */
static int reset_graphics_clocks(void) {
    char *try1[] = { nvsmipath, "--reset-gpu-clocks", NULL };
    if (run_nvsmicmd(try1) == 0) return 0;
    /* fallback: some drivers use broader reset */
    char *try2[] = { nvsmipath, "--reset-clocks", NULL };
    return run_nvsmicmd(try2);
}

/* Print the profile result line.  Shows what was requested and what the driver
 * actually applied; adds a note if the driver adjusted either value. */
static void print_profile_result(const char *label,
                                  int req_mem,  int req_gfx,
                                  int real_mem, int real_gfx) {
    int mem = real_mem > 0 ? real_mem : req_mem;
    int gfx = real_gfx > 0 ? real_gfx : req_gfx;
    printf("%s: memory %d MHz, graphics %d MHz", label, mem, gfx);
    if (real_mem > 0 && real_mem != req_mem)
        printf(" (memory requested %d MHz, driver applied %d MHz)", req_mem, real_mem);
    if (real_gfx > 0 && real_gfx != req_gfx)
        printf(" (graphics requested %d MHz, driver applied %d MHz)", req_gfx, real_gfx);
    printf("\n");
}

/* apply a profile: enable persistence, lock memory and graphics clocks */
static int apply_profile(int memclk, int gfxclk) {
    if (enable_persistence() != 0) {
        fprintf(stderr, "Failed to enable persistence mode\n");
        return -1;
    }
    if (lock_memory_clocks(memclk) != 0) {
        fprintf(stderr, "Failed to lock memory clocks to %d MHz\n", memclk);
        return -1;
    }
    if (gfxclk > 0 && lock_graphics_clocks(gfxclk) != 0) {
        fprintf(stderr, "Warning: could not lock graphics clocks to %d MHz (may require driver 510+)\n", gfxclk);
    }
    return 0;
}

/* check allowed */
static int is_allowed(const char *cmd) {
    for (int i = 0; allowed_cmds[i]; ++i) if (strcmp(cmd, allowed_cmds[i]) == 0) return 1;
    return 0;
}

/* check NVIDIA driver/runtime status */
static int check_nvidia_runtime(void) {
    char out[READ_BUF];
    char *argv[] = { nvsmipath, "--query-gpu=name", "--format=csv,noheader", NULL };
    int rc = exec_capture(argv, out, sizeof(out), 1); /* capture stderr to detect driver errors */
    if (rc < 0) {
        /* exec failure / cannot run nvidia-smi */
        fprintf(stderr, "Error: failed to execute %s. Is nvidia-smi available and executable?\n", nvsmipath);
        return -1;
    }
    /* if output contains "No devices were found" or is empty -> driver not present/working */
    if (out[0] == '\0' || strstr(out, "No devices were found")) {
        fprintf(stderr, "Error: no NVIDIA GPUs detected or driver not loaded.\n");
        fprintf(stderr, "Hint: install or enable the NVIDIA driver for your distro (see README).\n");
        return -2;
    }
    return 0;
}

/* main entry (delegated from src/main.c) */
int nvflux_run(int argc, char **argv) {
    /* provide a helpful --help / -h from the privileged runner as well */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <performance|balanced|powersaver|auto|reset|status|clock|--restore|--help>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("nvflux - manage NVIDIA GPU profiles (safe, limited set of nvidia-smi ops)\n\n");
        printf("Usage:\n");
        printf("  nvflux <command>\n\n");
        printf("Commands:\n");
        printf("  performance     Lock GPU memory & graphics clocks to highest supported\n");
        printf("  balanced        Lock GPU memory & graphics clocks to a mid value\n");
        printf("  powersaver      Lock GPU memory & graphics clocks to lowest supported\n");
        printf("  auto | reset    Reset all GPU clock locks to automatic behavior\n");
        printf("  status          Show last saved profile for the calling user\n");
        printf("  clock           Print current memory & graphics clocks (MHz)\n");
        printf("  --restore       Reapply last saved profile for the calling user\n");
        printf("  -h, --help      Show this help message\n\n");
        printf("Notes:\n");
        printf("  - nvflux is intended to be installed setuid root; installer script sets that up.\n");
        printf("  - Only the above commands are allowed; nvflux validates inputs before running nvidia-smi.\n");
        return 0;
    }

    if (find_nvidia_smi() < 0) {
        fprintf(stderr, "Error: nvidia-smi not found in common locations or PATH.\n");
        fprintf(stderr, "Hint: install NVIDIA drivers / nvidia-utils for your distro. See README.\n");
        return 2;
    }

    if (check_nvidia_runtime() != 0) {
        /* check_nvidia_runtime already printed a helpful message */
        return 3;
    }

    uid_t real_uid = getuid();
    uid_t effective_uid = geteuid();

    if (effective_uid != 0) {
        fprintf(stderr, "Error: this program needs to be installed setuid root (installer will do this).\n");
        return 4;
    }

    const char *cmd = argv[1];
    if (!is_allowed(cmd)) {
        fprintf(stderr, "Unknown or disallowed command: %s\n", cmd);
        return 5;
    }

    if (strcmp(cmd, "status") == 0) {
        char mode[128] = {0};
        if (read_state(real_uid, mode, sizeof(mode))) {
            if (mode[0] >= 'a' && mode[0] <= 'z') {
                mode[0] = mode[0] - 'a' + 'A';
            }
            printf("%s\n", mode);
        } else {
            printf("Default\n");
        }
        return 0;
    }

    if (strcmp(cmd, "clock") == 0) {
        int mem = get_current_mem_clock();
        int gfx = get_current_graphics_clock();
        if (mem < 0 && gfx < 0) {
            fprintf(stderr, "Failed to query current clocks\n");
            return 1;
        }
        if (mem >= 0) printf("Memory:   %d MHz\n", mem);
        if (gfx >= 0) printf("Graphics: %d MHz\n", gfx);
        return 0;
    }

    if (strcmp(cmd, "--restore") == 0) {
        char mode[128] = {0};
        if (!read_state(real_uid, mode, sizeof(mode))) {
            fprintf(stderr, "No saved mode to restore\n");
            return 1;
        }
        cmd = mode;
    }

    int mem_clocks[MAX_CLOCKS];
    int mem_count = get_mem_clocks(mem_clocks, MAX_CLOCKS);
    if (mem_count <= 0) { fprintf(stderr, "Failed to query supported memory clocks\n"); return 1; }

    int mem_max = mem_clocks[0];
    int mem_mid = mem_clocks[mem_count / 2];
    int mem_low = mem_clocks[mem_count - 1];

    /* Query graphics clocks once (sorted descending, deduplicated).
     * Strip boost clocks the driver cannot actually lock to, then
     * pick by percentile from the high end using the named constants above. */
    int gfx_clocks[MAX_CLOCKS];
    int gfx_count = get_graphics_clocks(gfx_clocks, MAX_CLOCKS);
    int max_lockable_gfx = get_max_lockable_gfx_clock();
    gfx_count = filter_lockable_clocks(gfx_clocks, gfx_count, max_lockable_gfx);

    if (strcmp(cmd, "performance") == 0) {
        if (check_temp_safety(1) != 0) return 1;
        int gfx = pick_clock_at_pct(gfx_clocks, gfx_count, GFX_PCT_PERFORMANCE);
        int before_mem = get_current_mem_clock();
        int before_gfx = get_current_graphics_clock();
        if (apply_profile(mem_max, gfx) != 0) return 1;
        int real_mem, real_gfx;
        read_clocks_stable(before_mem, before_gfx, &real_mem, &real_gfx);
        print_profile_result("Performance", mem_max, gfx, real_mem, real_gfx);
        write_state(real_uid, "performance");
        return 0;
    } else if (strcmp(cmd, "balanced") == 0) {
        if (check_temp_safety(1) != 0) return 1;
        int gfx = pick_clock_at_pct(gfx_clocks, gfx_count, GFX_PCT_BALANCED);
        int before_mem = get_current_mem_clock();
        int before_gfx = get_current_graphics_clock();
        if (apply_profile(mem_mid, gfx) != 0) return 1;
        int real_mem, real_gfx;
        read_clocks_stable(before_mem, before_gfx, &real_mem, &real_gfx);
        print_profile_result("Balanced", mem_mid, gfx, real_mem, real_gfx);
        write_state(real_uid, "balanced");
        return 0;
    } else if (strcmp(cmd, "powersaver") == 0) {
        if (check_temp_safety(0) != 0) return 1;
        int gfx = pick_clock_at_pct(gfx_clocks, gfx_count, GFX_PCT_POWERSAVER);
        int before_mem = get_current_mem_clock();
        int before_gfx = get_current_graphics_clock();
        if (apply_profile(mem_low, gfx) != 0) return 1;
        int real_mem, real_gfx;
        read_clocks_stable(before_mem, before_gfx, &real_mem, &real_gfx);
        print_profile_result("Power Saver", mem_low, gfx, real_mem, real_gfx);
        write_state(real_uid, "powersaver");
        return 0;
    } else if (strcmp(cmd, "auto") == 0 || strcmp(cmd, "reset") == 0) {
        if (reset_memory_clocks() != 0) { fprintf(stderr, "Failed to reset memory clocks\n"); return 1; }
        reset_graphics_clocks(); /* best-effort */
        printf("Auto: clocks reset to driver-managed\n");
        write_state(real_uid, "auto");
        return 0;
    }

    return 0;
}