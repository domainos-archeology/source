/*
 * smd/op_wait_u.c - SMD_$OP_WAIT_U implementation
 *
 * Waits for any pending display operation to complete.
 * Simple wrapper that acquires and immediately releases the display lock.
 *
 * Original address: 0x00E6FB96
 */

#include "smd/smd_internal.h"

/*
 * SMD_$OP_WAIT_U - Wait for operation completion
 *
 * Blocks until any pending display operation completes.
 * This is achieved by acquiring and releasing the display lock,
 * since the lock cannot be acquired until pending operations finish.
 *
 * If the current process has no associated display unit, returns
 * immediately without waiting.
 *
 * Returns:
 *   0 always (for compatibility)
 */
uint16_t SMD_$OP_WAIT_U(void)
{
    /* Check if current process has an associated display unit */
    if (SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] != 0) {
        /* Acquire display lock (blocks until operations complete) */
        SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);

        /* Immediately release */
        SMD_$REL_DISPLAY();
    }

    return 0;
}
