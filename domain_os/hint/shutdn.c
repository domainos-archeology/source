/*
 * HINT_$SHUTDN - Shut down the hint subsystem
 *
 * Unmaps the hint file and releases associated resources.
 * Called during system shutdown.
 *
 * Original address: 0x00E49908
 */

#include "hint/hint_internal.h"

/* Unlock mode constant */
static uint16_t hint_unlock_mode = 0;

void HINT_$SHUTDN(void)
{
    hint_file_t *saved_ptr;
    status_$t status;

    saved_ptr = HINT_$HINTFILE_PTR;

    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    /* Clear the global pointer before unmapping */
    HINT_$HINTFILE_PTR = NULL;

    /* Unmap the hint file
     * Parameters:
     *   1: Mode (privileged unmap)
     *   &UID_$NIL: UID for unmap (NIL = use pointer)
     *   saved_ptr: Pointer to unmap
     *   0x7FFF: Size
     *   0: ASID
     *   &status: Status return
     */
    MST_$UNMAP_PRIVI(1, (uid_t *)&UID_$NIL, (uint32_t)saved_ptr, 0x7FFF, 0, &status);

    /* Unlock the hint file */
    FILE_$UNLOCK(&HINT_$HINTFILE_UID, &hint_unlock_mode, &status);
}
