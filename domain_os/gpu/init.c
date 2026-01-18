/*
 * gpu/init.c - GPU_$INIT implementation
 *
 * GPU initialization for SAU2 hardware.
 *
 * Original address: 0x00E707FC (18 bytes)
 */

#include "gpu/gpu.h"

/*
 * GPU_$INIT - Initialize GPU subsystem
 *
 * On SAU2 hardware, there is no dedicated GPU. This function
 * simply returns the "device not present" status code.
 *
 * The Apollo workstations used a display subsystem integrated
 * with the main processor rather than a separate GPU.
 *
 * Assembly (0x00E707FC):
 *   link.w  A6,#0
 *   movea.l (0x8,A6),A0              ; A0 = status_ret
 *   move.l  #0x2d0001,(A0)           ; *status_ret = gpu_device_not_present
 *   unlk    A6
 *   rts
 */
void GPU_$INIT(status_$t *status_ret)
{
    *status_ret = status_$gpu_device_not_present;
}
