/*
 * exec.h - fork/exec primitives
 *
 * Two thin wrappers around fork+execv.  No shell is ever involved;
 * all arguments are passed as arrays, preventing any injection.
 */
#ifndef NVFLUX_EXEC_H
#define NVFLUX_EXEC_H

#include <stddef.h>

/*
 * exec_capture - run argv[0] with the given arguments, capturing
 * stdout and stderr into outbuf (NUL-terminated, at most outlen-1 bytes).
 *
 * Returns the child's exit code, or -1 on fork/pipe failure.
 * A return value of 127 means execv itself failed (binary not found/executable).
 */
int exec_capture(char *const argv[], char *outbuf, size_t outlen);

/*
 * run_cmd - run argv[0] with inherited stdio (output goes to the terminal).
 *
 * Returns the child's exit code, or -1 on fork failure.
 */
int run_cmd(char *const argv[]);

#endif /* NVFLUX_EXEC_H */
