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
 * The routine has NO result.  Nothing between the "move.w
 * (0x48,A5,D0w*0x1),D0w" at 0x00E6FBAA and the "rts" at 0x00E6FBC4 loads D0:
 * on the no-unit path it still holds that zero (over PROC1_$AS_ID*2 in its
 * high half) and on the other path it holds whatever SMD_$REL_DISPLAY's
 * trailing EC_$ADVANCE left there.  The tree used to declare a uint16_t
 * result and return a hard 0, which is a value the image never computes.
 * (source-lpk8)
 */
void SMD_$OP_WAIT_U(void)
{
    /* 0x00E6FBA2-0x00E6FBAE */
    if (SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] != 0) {
        /* 0x00E6FBB0-0x00E6FBB8: acquire, which blocks until the unit is free */
        SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);

        /* 0x00E6FBBA */
        SMD_$REL_DISPLAY();
    }
}
