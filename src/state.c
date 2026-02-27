#include "state.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

/*
 * home_dir - resolve home directory for uid.
 * Prefers getpwuid() for accuracy; falls back to $HOME (useful when
 * called as the real user before setuid elevation, though nvflux is
 * setuid root so getpwuid should always succeed).
 * Returns a pointer to a static or heap string; never NULL.
 */
static const char *home_dir(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0')
        return pw->pw_dir;

    /* fallback: $HOME (only valid if uid == getuid()) */
    const char *h = getenv("HOME");
    return (h && h[0] != '\0') ? h : "/tmp";
}

/*
 * state_path - build the full path to the state file for uid into out[].
 */
static void state_path(uid_t uid, char *out, size_t len) {
    snprintf(out, len, "%s/.local/state/nvflux/state", home_dir(uid));
}

/*
 * mkdirp - recursively create every component of path with mode 0755.
 * Ignores EEXIST at each level.  Does not restore errno.
 */
static void mkdirp(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);

    /* walk every '/' component and mkdir each prefix */
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, 0755);
            *p = '/';
        }
    }
    (void)mkdir(tmp, 0755);  /* final component */
}

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

void state_write(uid_t uid, const char *profile) {
    char path[PATH_MAX];
    state_path(uid, path, sizeof(path));

    /* ensure the full directory tree exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdirp(dir);
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;

    (void)fchown(fd, uid, (gid_t)-1);  /* ensure owned by real user */
    (void)write(fd, profile, strlen(profile));
    close(fd);
}

int state_read(uid_t uid, char *buf, size_t len) {
    char path[PATH_MAX];
    state_path(uid, path, sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    ssize_t r = read(fd, buf, len - 1);
    close(fd);
    if (r <= 0) return 0;

    buf[r] = '\0';
    /* strip trailing whitespace (newlines written by other tools, etc.) */
    while (r > 0 && isspace((unsigned char)buf[r - 1]))
        buf[--r] = '\0';

    return (r > 0) ? 1 : 0;
}
