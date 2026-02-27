/*
 * gpu.h - nvidia-smi operations
 *
 * All interaction with the GPU driver goes through this module.
 * Functions here call nvidia-smi via exec.h; no shell is ever used.
 */
#ifndef NVFLUX_GPU_H
#define NVFLUX_GPU_H

#define GPU_MAX_CLOCKS 128

/*
 * gpu_find_smi - locate nvidia-smi binary.
 * Checks well-known paths first, then walks PATH.
 * Returns 0 on success, -1 if not found.
 * Must be called before any other gpu_* function.
 */
int gpu_find_smi(void);

/*
 * gpu_check_driver - verify at least one NVIDIA GPU is visible.
 * Prints a diagnostic and returns -1 if not; 0 on success.
 */
int gpu_check_driver(void);

/*
 * gpu_mem_clocks - query supported memory clock tiers from the driver.
 * Fills clocks[] (size >= GPU_MAX_CLOCKS) sorted descending.
 * Returns the number of tiers found, or -1 on error.
 */
int gpu_mem_clocks(int *clocks, int max);

/*
 * gpu_current_mem_clock - return current memory clock in MHz, or -1 on error.
 * Tries multiple query forms for compatibility across nvidia-smi versions.
 */
int gpu_current_mem_clock(void);

/*
 * gpu_enable_persistence - enable persistence mode (-pm 1).
 * Required so clock locks survive driver power-state transitions.
 * Returns 0 on success, non-zero on failure.
 */
int gpu_enable_persistence(void);

/*
 * gpu_lock_mem - lock memory clock to exactly mhz MHz.
 * Returns 0 on success, non-zero on failure.
 */
int gpu_lock_mem(int mhz);

/*
 * gpu_unlock_mem - remove memory clock lock; driver resumes auto management.
 * Tries multiple option names for compatibility across driver versions.
 * Returns 0 on success, non-zero on failure.
 */
int gpu_unlock_mem(void);

#endif /* NVFLUX_GPU_H */
