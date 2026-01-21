/*
 * smd/start_blt.c - SMD_$START_BLT implementation
 *
 * Starts a hardware BLT (bit block transfer) operation.
 *
 * Original address: 0x00E272BC (trampoline), 0x00E15D1E (implementation)
 *
 * Assembly analysis:
 * The function copies BLT parameters from the source structure to the
 * hardware register block, then initiates the operation. If the async
 * flag (bit 4) is set, it sets up interrupt-driven completion; otherwise
 * it polls for completion.
 *
 * Parameters are copied from params to hw_regs:
 *   params[1] -> hw_regs[1] (bit position)
 *   params[3] -> hw_regs[3] (pattern/ROP)
 *   params[2] -> hw_regs[2] (mask)
 *   params[4] -> hw_regs[4] (y extent)
 *   params[5] -> hw_regs[5] (x extent)
 *   params[6] -> hw_regs[6] (y start)
 *   params[7] -> hw_regs[7] (x start)
 *
 * The control word is computed from params[0] and hw->video_flags,
 * then written to hw_regs[0] to start the operation.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$START_BLT - Start BLT operation
 *
 * Initiates a hardware BLT operation by copying parameters to the
 * hardware register block and writing the control word.
 *
 * Parameters:
 *   params  - BLT parameters (16-bit words)
 *   hw      - Display hardware info
 *   hw_regs - Hardware BLT register block
 *
 * BLT control word bits:
 *   bit 4: async mode (use interrupt completion)
 *   bit 0-3: other control flags
 */
void SMD_$START_BLT(uint16_t *params, smd_display_hw_t *hw, uint16_t *hw_regs)
{
    uint16_t control;

    /* Copy BLT parameters to hardware registers */
    hw_regs[1] = params[1];  /* Bit position */
    hw_regs[3] = params[3];  /* Pattern/ROP */
    hw_regs[2] = params[2];  /* Mask */
    hw_regs[4] = params[4];  /* Y extent */
    hw_regs[5] = params[5];  /* X extent */
    hw_regs[6] = params[6];  /* Y start */
    hw_regs[7] = params[7];  /* X start */

    /* Compute control word: combine params[0] (masked) with video_flags */
    control = hw->video_flags | (params[0] & 0xFFDE);

    /* Check if async mode (bit 4) is set */
    if ((control & 0x10) != 0) {
        /* Async mode - set up for interrupt completion */
        hw->lock_state = 1;      /* Mark as busy */
        hw->field_24 = 0;        /* Clear field at +0x20 */
        hw->field_1c = hw->op_ec.value;  /* Save EC value for completion check */
    }

    /* Write control word to start the operation */
    hw_regs[0] = control;

    /* If not async, poll for completion */
    if ((control & 0x10) == 0) {
        /* Poll until bit 15 (busy) clears */
        while ((int16_t)hw_regs[0] < 0) {
            /* Busy wait */
        }
    }
}
