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

/* Error status for unbalanced super mode calls */
static status_$t Exit_Super_Called_More_Than_Enter_Super = 0x00230002;

void ACL_$EXIT_SUPER(void)
{
    if (ACL_$SUPER_COUNT[PROC1_$CURRENT] == 0) {
        CRASH_SYSTEM(&Exit_Super_Called_More_Than_Enter_Super);
    }
    ACL_$SUPER_COUNT[PROC1_$CURRENT]--;
}
