/*
 * TTY kernel-level process group and session functions (re-verified
 * 2026-09-22).  All four load A5 = 0x00E8242C, call
 * tty = TTY_$I_GET_DESC(*line_ptr, status) and return when status != 0.
 *   TTY_$K_SET_PGROUP     0x00E67900..0x00E67940 (66): two longwords from
 *                         *uid_ptr -> (0x4c,A0), (0x50,A0)
 *   TTY_$K_INQ_PGROUP     0x00E67942..0x00E67984 (68): the reverse copy
 *   TTY_$K_SET_SESSION_ID 0x00E67986..0x00E679C2 (62): word -> (0x54,A0)
 *   TTY_$K_INQ_SESSION_ID 0x00E679C4..0x00E67A00 (62): word <- (0x54,A0)
 */

#include "tty/tty_internal.h"

void TTY_$K_SET_PGROUP(short *line_ptr, uid_t *uid_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Copy the process group UID
    tty->pgroup_uid.high = uid_ptr->high;
    tty->pgroup_uid.low = uid_ptr->low;
}

void TTY_$K_INQ_PGROUP(short *line_ptr, uid_t *uid_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Return the process group UID
    uid_ptr->high = tty->pgroup_uid.high;
    uid_ptr->low = tty->pgroup_uid.low;
}

void TTY_$K_SET_SESSION_ID(short *line_ptr, short *sid_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    tty->session_id = *sid_ptr;
}

void TTY_$K_INQ_SESSION_ID(short *line_ptr, short *sid_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    *sid_ptr = tty->session_id;
}
