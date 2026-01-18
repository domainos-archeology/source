/*
 * gpu/term.c - GPU_$TERM implementation
 *
 * GPU termination for SAU2 hardware.
 *
 * Original address: 0x00E7080E (2 bytes)
 */

#include "gpu/gpu.h"

/*
 * GPU_$TERM - Terminate GPU subsystem
 *
 * On SAU2 hardware, there is no dedicated GPU to terminate.
 * This function is a no-op.
 *
 * Assembly (0x00E7080E):
 *   rts
 */
void GPU_$TERM(void)
{
    /* No GPU hardware to terminate on SAU2 */
}
