/*
 * PROC2_$SET_TTY - Set TTY for current process
 *
 * Sets the controlling TTY UID for the current process.
 * Note: This function takes only one parameter (no status return).
 *
 * Parameters:
 *   tty_uid - Pointer to TTY UID (8 bytes) to set
 *
 * Original address: 0x00e41c04
 */

#include "proc2.h"

void PROC2_$SET_TTY(uid_t *tty_uid)
{
    proc2_info_t *info;
    uint16_t index;

    /* Get current process's PROC2 index */
    index = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Get pointer to process info entry */
    info = P2_INFO_ENTRY(index);

    /* Copy the 8-byte TTY UID */
    info->tty_uid = *tty_uid;
}
