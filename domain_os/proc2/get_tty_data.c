/*
 * PROC2_$GET_TTY_DATA - Get TTY data for current process
 *
 * Returns the TTY UID and TTY flags for the current process.
 *
 * Parameters:
 *   tty_uid - Pointer to receive TTY UID (8 bytes)
 *   tty_flags - Pointer to receive TTY flags (2 bytes)
 *
 * Original address: 0x00e41bb8
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_TTY_DATA(uid_t *tty_uid, uint16_t *tty_flags)
{
    int16_t my_index;
    proc2_info_t *entry;

    /* Get my proc2 index from PID mapping table */
    my_index = PROC2_$DATA.pid_to_index[PROC1_$CURRENT];

    entry = P2_INFO_ENTRY(my_index);

    /* Copy TTY UID (8 bytes) */
    tty_uid->high = entry->tty_uid.high;
    tty_uid->low = entry->tty_uid.low;

    /*
     * Verified: 0x00E41BF6 reads (-0x88,A0) with A0 = entry + 0xE4, i.e.
     * entry+0x5C -- the session_id word.
     */
    *tty_flags = entry->session_id;
}
