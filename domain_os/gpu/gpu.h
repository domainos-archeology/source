/*
 * gpu/gpu.h - GPU Subsystem Public Interface
 *
 * Graphics Processing Unit initialization and termination.
 * On SAU2 hardware, this is a stub that returns "device not present".
 */

#ifndef GPU_H
#define GPU_H

#include "base/base.h"

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/* GPU subsystem ID (0x2d = 45) */
#define STATUS_GPU_SUBSYSTEM        0x2d

/* GPU device not present on this hardware */
#define status_$gpu_device_not_present  0x2d0001

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * GPU_$INIT - Initialize GPU subsystem
 *
 * Initializes the GPU subsystem. On SAU2 hardware without a GPU,
 * this simply returns status_$gpu_device_not_present.
 *
 * Parameters:
 *   status_ret - Pointer to receive initialization status
 *
 * Original address: 0x00E707FC (18 bytes)
 */
void GPU_$INIT(status_$t *status_ret);

/*
 * GPU_$TERM - Terminate GPU subsystem
 *
 * Terminates the GPU subsystem and releases resources.
 * On SAU2 hardware without a GPU, this is a no-op.
 *
 * Original address: 0x00E7080E (2 bytes)
 */
void GPU_$TERM(void);

/* GPU data */
extern int8_t GPU_$PRESENT;


#endif /* GPU_H */
