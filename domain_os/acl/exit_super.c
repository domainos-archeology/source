/*
 * ACL_$EXIT_SUPER - Exit superuser mode for current process
 *
 * Decrements the superuser mode counter for the current process.
 * Crashes the system if called without a matching ENTER_SUPER.
 *
 * Original address: 0x00E46FB4
 */

#include "acl/acl_internal.h"
#include "misc/crash_system.h"

/*
 * 0x00E46FF4: the status longword CRASH_SYSTEM is handed by reference
 * ("pea (0x20,PC)" at 0x00E46FD2; 0x00E46FD4 + 0x20 = 0x00E46FF4).  It sits
 * in the four bytes after ACL_$EXIT_SUPER's rts:
 *
 *   00e46ff0  4e 5e 4e 75 00 23 00 03
 *                         ^^^^^^^^^^^
 *
 * 0x00230003 is "exit_super called more often than enter_super" in the SR10.2
 * status text, which is exactly this check.
 */
static status_$t acl_$exit_super_unbalanced_status =
    status_$acl_exit_super_unbalanced;

void ACL_$EXIT_SUPER(void)
{
    /*
     * 0x00E46FC0-0x00E46FD0: index ACL_$UNWIRED_DATA.super_count by PROC1_$CURRENT (the
     * word is doubled, so the array is of words) and crash on zero.
     */
    if (ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT] == 0) {
        CRASH_SYSTEM(&acl_$exit_super_unbalanced_status);
    }

    /*
     * 0x00E46FDC-0x00E46FE8: PROC1_$CURRENT is re-read and the index
     * recomputed before the decrement, exactly as the image does.
     */
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]--;
}
