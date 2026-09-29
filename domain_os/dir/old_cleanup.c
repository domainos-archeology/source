/*
 * DIR_$OLD_CLEANUP - Legacy directory cleanup
 *
 * Checks if the current process has an active directory handle
 * and calls NAME_$UNLOCK_DIR to release it.
 *
 * Original address: 0x00E54B2A
 * Original size: 46 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_CLEANUP - Legacy directory cleanup
 *
 * Assembly:
 *   move.w PROC1_$CURRENT, D0    ; get current process index
 *   lsl.w #3, D0                 ; multiply by 8 (entry size)
 *   lea (0, A5, D0*1), A0        ; A5 = 0xe7fd24, compute entry address
 *   tst.l (0x2b8, A0)            ; test the HIGH longword of NAME_$OLD_DIR_DATA.lock_uid[cur]
 *   beq skip                     ; skip if this process holds no lock
 *   pea (-4, A6)                 ; push address for status
 *   bsr NAME_$UNLOCK_DIR         ; release the lock
 *
 * The tested longword is NAME_$OLD_DIR_DATA.lock_uid[PROC1_$CURRENT].high
 * (0xe7ffdc = 0xe7fd24 + 0x2b8); see NAME_$OLD_DIR_DATA in name/name.h.
 */
void DIR_$OLD_CLEANUP(void)
{
    status_$t status;

    /* Check if the current process has a directory locked */
    if (NAME_$OLD_DIR_DATA.lock_uid[PROC1_$CURRENT].high != 0) {
        NAME_$UNLOCK_DIR(&status);
    }
}
