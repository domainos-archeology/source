/*
 * DIR_$OLD_CLEANUP - Legacy directory cleanup
 *
 * Checks if the current process has an active directory handle
 * and calls FUN_00e54734 to release it.
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
 *   lsl.w #3, D0                 ; multiply by 8 (slot size)
 *   lea (0, A5, D0*1), A0        ; A5=0xe7fd24, compute slot address
 *   tst.l (0x2b8, A0)            ; test handle pointer
 *   beq skip                     ; skip if no handle
 *   pea (-4, A6)                 ; push address for status
 *   bsr FUN_00e54734             ; release handle
 *
 * The handle pointer is at DAT_00e7ffdc + current * 8
 * (0xe7ffdc = 0xe7fd24 + 0x2b8)
 */
void DIR_$OLD_CLEANUP(void)
{
    status_$t status;

    /* Check if current process has an active handle */
    if (*((uint32_t *)(&DAT_00e7ffdc + (int16_t)(PROC1_$CURRENT << 3))) != 0) {
        FUN_00e54734(&status);
    }
}
