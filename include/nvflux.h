/*
 * nvflux.h - public API
 *
 * Only the symbols needed by main.c and the test suite are exposed here.
 * Everything else is internal to its own translation unit.
 */
#ifndef NVFLUX_H
#define NVFLUX_H

#define NVFLUX_VERSION "1.0.0"

/*
 * nvflux_run - process argv and execute the requested command.
 *
 * Return values (used as process exit codes):
 *   0  success
 *   1  usage / operational error (message already printed)
 *   2  nvidia-smi not found
 *   3  not running as root
 *   4  no NVIDIA GPU / driver not loaded
 *   5  unknown command
 */
int nvflux_run(int argc, char **argv);

/*
 * nvflux_parse_clocks - parse integers from whitespace/comma-separated text
 * into clocks[], sorted descending.  Returns the number of values parsed
 * (<= max).  Used internally and exposed for unit testing.
 */
int nvflux_parse_clocks(const char *txt, int *clocks, int max);

#endif /* NVFLUX_H */
