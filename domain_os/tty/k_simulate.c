/*
 * TTY_$K_SIMULATE_TERMINAL_INPUT - feed one character to the line as if
 * received.  0x00E1C148..0x00E1C202 (188 bytes), A5 = 0x00E2DDB4;
 * re-verified 2026-09-22.
 *   0x00E1C15A  tty = TTY_$I_GET_DESC(*line_ptr, status); status != 0 -> return
 *   0x00E1C176  PROC2_$GET_MY_UPIDS(&(-0x12) upid, &(-0x10), &(-0xe) upgid)
 *   0x00E1C18C  D2 = upid word
 *   0x00E1C190  PROC2_$UPGID_TO_UID(&(-0xe), &(-0x8) uid, status);
 *               status != 0 -> return
 *   0x00E1C1A8  upid != 0: cmpm.l twice against (0x4c,A2) pgroup_uid;
 *               any mismatch -> status 0x350001, return
 *   0x00E1C1C6  token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK); TTY_$I_RCV(tty, *ch_ptr)
 *               (byte in the high half of the word slot); ML_$SPIN_UNLOCK
 */

#include "tty/tty_internal.h"
#include "proc2/proc2.h"

void TTY_$K_SIMULATE_TERMINAL_INPUT(short *line_ptr, char *ch_ptr, status_$t *status)
{
    tty_desc_t *tty;
    ml_$spin_token_t token;

    // Process ID info
    uint16_t upid;          /* (-0x12,A6) */
    uint16_t reserved1;     /* (-0x10,A6) */
    uint16_t upgid[3];      /* (-0xe,A6) */
    uid_t my_uid;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Get the caller's process group info
    PROC2_$GET_MY_UPIDS(&upid, &reserved1, upgid);

    // Convert UPGID to UID
    PROC2_$UPGID_TO_UID(upgid, &my_uid, status);
    if (*status != status_$ok) {
        return;
    }

    // If not the superuser (upid != 0), verify we own this TTY
    if (upid != 0) {
        // Check if our UID matches the TTY's process group UID
        if (my_uid.high != tty->pgroup_uid.high ||
            my_uid.low != tty->pgroup_uid.low) {
            *status = status_$tty_invalid_option;
            return;
        }
    }

    // Acquire spin lock for TTY operations
    token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    // Simulate receiving the character
    TTY_$I_RCV(tty, (uint8_t)*ch_ptr);

    // Release spin lock
    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
}
