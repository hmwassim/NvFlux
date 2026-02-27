#include "exec.h"

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

int exec_capture(char *const argv[], char *outbuf, size_t outlen) {
    int pfd[2];
    if (pipe(pfd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }

    if (pid == 0) {
        /* child: redirect stdout+stderr into the pipe, then exec */
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        execv(argv[0], argv);
        _exit(127);  /* execv failed */
    }

    /* parent: drain the pipe */
    close(pfd[1]);
    ssize_t total = 0;
    while (total < (ssize_t)(outlen - 1)) {
        ssize_t r = read(pfd[0], outbuf + total, outlen - 1 - (size_t)total);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        total += r;
    }
    close(pfd[0]);
    outbuf[total] = '\0';

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
