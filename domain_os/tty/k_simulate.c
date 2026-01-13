// TTY_$K_SIMULATE_TERMINAL_INPUT - Simulate input character
// Address: 0x00e1c148
// Size: 188 bytes

#include "tty.h"

// External spin lock functions
extern ushort ML_$SPIN_LOCK(uint32_t *lock);
extern void ML_$SPIN_UNLOCK(uint32_t *lock, ushort token);

// External process functions
extern void PROC2_$GET_MY_UPIDS(short *upid, short *reserved1, short *upgid);
extern void PROC2_$UPGID_TO_UID(short *upgid, uid_$t *uid, status_$t *status);

void TTY_$K_SIMULATE_TERMINAL_INPUT(short *line_ptr, char *ch_ptr, status_$t *status)
{
    tty_desc_t *tty;
    ushort token;

    // Process ID info
    short upid;
    short reserved1;
    short upgid[3];
    uid_$t my_uid;

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
            *status = status_$tty_access_denied;
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
