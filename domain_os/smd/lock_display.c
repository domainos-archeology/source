/*
 * smd/lock_display.c - SMD_$LOCK_DISPLAY implementation
 *
 * Low-level display lock with interrupt disable. Manages the lock state
 * machine for exclusive display access during operations like scrolling.
 *
 * Original address: 0x00E15CCE
 *
 * Lock states:
 *   0 = unlocked
 *   3 = scroll done (transitions to 4 when param2[0] == 1)
 *   4 = post-scroll locked
 *   5 = initial lock
 *
 * Assembly analysis:
 *   - Disables interrupts (ori #0x700,SR)
 *   - Checks lock_data[1] (offset 2) for current state
 *   - If state == 0: set state to 5, return success (0xFF)
 *   - If state == 3 and param2[0] == 1: set state to 4, clear param2[0x12], return success
 *   - Otherwise: return failure (0x00)
 *   - Re-enables interrupts (andi #-0x701,SR)
 */

#include "smd/smd_internal.h"

/*
 * SMD_$LOCK_DISPLAY - Lock display for exclusive access
 *
 * Acquires a low-level lock on the display hardware. This function operates
 * with interrupts disabled to ensure atomicity.
 *
 * Parameters:
 *   lock_data - Pointer to display hardware structure (smd_display_hw_t)
 *               The lock state is at offset +2 (lock_state field)
 *   param2    - Secondary parameter structure
 *               offset +0: condition flag (must be 1 for state 3 transition)
 *               offset +0x24: field to clear on successful state 3 transition
 *
 * Returns:
 *   0xFF (-1) if lock acquired successfully
 *   0x00 if lock not available
 *
 * Lock state machine:
 *   State 0 (unlocked): Transitions to state 5, returns success
 *   State 3 (scroll done): If param2[0] == 1, transitions to state 4, returns success
 *   Other states: Returns failure (lock busy)
 */
int16_t SMD_$LOCK_DISPLAY(smd_display_hw_t *lock_data, int16_t *param2)
{
    int16_t state;
    uint8_t high_byte;
    uint16_t sr;

    DISABLE_INTERRUPTS(sr);

    /* Read current lock state from offset +2 */
    state = lock_data->lock_state;
    high_byte = (uint8_t)(state >> 8);

    if (state == SMD_LOCK_STATE_UNLOCKED) {
        /* State 0: Unlocked - acquire lock, transition to state 5 */
        lock_data->lock_state = SMD_LOCK_STATE_LOCKED_5;
        return (int16_t)((high_byte << 8) | 0xFF);  /* Success */
    }

    if (state == SMD_LOCK_STATE_SCROLL_DONE && param2[0] == 1) {
        /* State 3: Scroll done and condition met - transition to state 4 */
        lock_data->lock_state = SMD_LOCK_STATE_LOCKED_4;
        param2[0x12] = 0;  /* Clear field at offset 0x24 (0x12 * 2) */
        ENABLE_INTERRUPTS(sr);
        return (int16_t)((high_byte << 8) | 0xFF);  /* Success */
    }

    /* Lock not available */
    ENABLE_INTERRUPTS(sr);
    return (int16_t)(high_byte << 8);  /* Failure (0x00 in low byte) */
}
