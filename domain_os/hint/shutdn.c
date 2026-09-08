/*
 * HINT_$SHUTDN - Shut down the hint subsystem
 *
 * Unmaps the hint file and releases associated resources.
 * Called during system shutdown.
 *
 * Original address: 0x00E49908
 */

#include "hint/hint_internal.h"

/*
 * 0x00E49964: the lock-mode word FILE_$UNLOCK is handed by reference
 * ("pea (0x14,PC)" at 0x00E4994E; 0x00E49950 + 0x14 = 0x00E49964).  The cell
 * sits in the two bytes between HINT_$SHUTDN's rts and the next routine's
 * link, and holds FOUR:
 *
 *   00e49960  4e 5e 4e 75 00 04 4e 56
 *                         ^^^^^
 */
static uint16_t hint_$unlock_mode = 4;

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

    /*
     * 0x00E4994A-0x00E49956: unlock the hint file.  Three by-reference
     * arguments and no result slot; the mode is the in-code word 4, not 0.
     */
    FILE_$UNLOCK(&HINT_$HINTFILE_UID, &hint_$unlock_mode, &status);
}
