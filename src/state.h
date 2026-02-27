/*
 * state.h - per-user profile state file
 *
 * State is stored in ~/.local/state/nvflux/state (XDG-compatible).
 * The file contains a single profile name, owned by the real UID and
 * mode 0600.  The directory tree is created on first write.
 */
#ifndef NVFLUX_STATE_H
#define NVFLUX_STATE_H

#include <stddef.h>
#include <sys/types.h>

/* Maximum length of a stored profile name (including NUL). */
#define NVFLUX_STATE_MAX 32

/*
 * state_write - write profile to the state file, owned by uid.
 * Creates ~/.local/state/nvflux/ recursively if needed.
 * Failures are silent; the operation is best-effort.
 */
void state_write(uid_t uid, const char *profile);

/*
 * state_read - read profile name into buf (capacity >= NVFLUX_STATE_MAX).
 * Returns 1 on success, 0 if no file exists or it cannot be read.
 * On success, buf is NUL-terminated with trailing whitespace stripped.
 */
int state_read(uid_t uid, char *buf, size_t len);

#endif /* NVFLUX_STATE_H */
