/*
 * hw.h — NvFlux hardware query / control layer
 *
 * All functions interact with the NVIDIA driver exclusively through
 * nvidia-smi.  Higher-level profile logic lives in nvflux.c.
 */

#ifndef NVFLUX_HW_H
#define NVFLUX_HW_H

#define HW_MAX_CLOCKS 512        /* max entries in any clock array    */
#define HW_READ_BUF   4096       /* byte budget for exec_capture()    */

/*
 * hw_check_runtime(void)
 *   Verify nvidia-smi is findable and an NVIDIA GPU is present.
 *   Returns 0 on success, -1 with a printed error on failure.
 */
int hw_check_runtime(void);

/*
 * hw_enable_persistence(void)
 *   Enable persistence mode so clock locks survive until reboot.
 *   Returns 0 on success, -1 on error.
 */
int hw_enable_persistence(void);

/*
 * hw_get_mem_clocks(clocks, max)
 *   Fill *clocks (sorted descending, dedup) with all supported memory
 *   clock speeds in MHz.  Returns the count, or -1 on error.
 */
int hw_get_mem_clocks(int *clocks, int max);

/*
 * hw_get_graphics_clocks(mem_mhz, clocks, max)
 *   Fill *clocks with all supported graphics clocks valid at the given
 *   memory speed.  Falls back to a global query when the per-level
 *   result contains fewer than 2 entries.
 *   Returns the count, or -1 on error.
 */
int hw_get_graphics_clocks(int mem_mhz, int *clocks, int max);

/*
 * hw_get_max_lockable_gfx(void)
 *   Return the highest graphics clock the driver will actually honour
 *   with --lock-gpu-clocks (clocks.max.gr, below the boost range).
 *   Returns the MHz value, or -1 on error.
 */
int hw_get_max_lockable_gfx(void);

/*
 * hw_current_mem_clock(void)
 * hw_current_graphics_clock(void)
 *   Return the current operating clock in MHz, or -1 on error.
 */
int hw_current_mem_clock(void);
int hw_current_graphics_clock(void);

/*
 * hw_current_clocks(mem_out, gfx_out)
 *   Query both memory and graphics clocks in a single nvidia-smi invocation.
 *   Writes results into *mem_out and *gfx_out (-1 on failure for each).
 */
void hw_current_clocks(int *mem_out, int *gfx_out);

/*
 * hw_gpu_temp(void)
 *   Return the current GPU core temperature in °C, or -1 on error.
 */
int hw_gpu_temp(void);

/*
 * hw_lock_memory_clocks(min_mhz, max_mhz)
 * hw_lock_graphics_clocks(min_mhz, max_mhz)
 *   Apply clock locks via nvidia-smi.  nvidia-smi output is silenced;
 *   nvflux prints its own clean summary.
 *   Returns 0 on success, -1 on error.
 */
int hw_lock_memory_clocks(int min_mhz, int max_mhz);
int hw_lock_graphics_clocks(int min_mhz, int max_mhz);

/*
 * hw_reset_memory_clocks(void)
 * hw_reset_graphics_clocks(void)
 *   Remove any existing clock lock.
 *   Returns 0 on success, -1 on error.
 */
int hw_reset_memory_clocks(void);
int hw_reset_graphics_clocks(void);

#endif /* NVFLUX_HW_H */
