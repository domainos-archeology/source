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

#include "proc2.h"

/* External reference to current PROC1 process */
extern uint16_t PROC1_CURRENT;

void PROC2_$GET_TTY_DATA(uid_t *tty_uid, uint16_t *tty_flags)
{
    int16_t my_index;
    proc2_info_t *entry;

    /* Get my proc2 index from PID mapping table */
    my_index = P2_PID_TO_INDEX(PROC1_CURRENT);

    entry = P2_INFO_ENTRY(my_index);

    /* Copy TTY UID (8 bytes) */
    tty_uid->high = entry->tty_uid.high;
    tty_uid->low = entry->tty_uid.low;

    /* Copy TTY flags */
    *tty_flags = entry->tty_flags;
}
