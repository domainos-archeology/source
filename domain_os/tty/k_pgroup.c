// TTY kernel-level process group and session functions
//
// TTY_$K_SET_PGROUP - Set process group UID
// Address: 0x00e67900
//
// TTY_$K_INQ_PGROUP - Inquire process group UID
// Address: 0x00e67942
//
// TTY_$K_SET_SESSION_ID - Set session ID
// Address: 0x00e67986
//
// TTY_$K_INQ_SESSION_ID - Inquire session ID
// Address: 0x00e679c4

#include "tty.h"

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
