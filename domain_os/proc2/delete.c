/*
 * PROC2_$DELETE - Delete current process
 *
 * Deletes the current process by performing cleanup and then
 * entering an infinite loop trying to unbind from the system.
 * This function never returns - the process is destroyed.
 *
 * Steps:
 * 1. Call DELETE_CLEANUP to release resources
 * 2. Loop forever calling PROC1_$UNBIND
 * 3. If UNBIND somehow returns, call CRASH_SYSTEM
 *
 * Original address: 0x00e74398
 */

#include "proc2.h"
#include "../ec/ec.h"

/* External functions */
extern void PROC1_$UNBIND(uint16_t pid, status_$t *status);

/*
 * PROC2_$DELETE_CLEANUP - Internal cleanup routine for process deletion
 * This is a complex function (1692 bytes) that:
 * - Calls XPD_$CLEANUP
 * - Frees ASID resources (SMD, SCSI)
 * - Clears pgroup membership
 * - Handles children and orphan processes
 * - Removes directory entries
 * - Updates UID mappings
 * - Logs accounting data
 * - Notifies guardian/parent processes
 * - Frees various subsystem resources (FILE, NAME, PEB, TERM, ACL, MST)
 * Original address: 0x00e743ce
 *
 * TODO: Full implementation of PROC2_$DELETE_CLEANUP is complex.
 * For now, stub it out as it requires many subsystem dependencies.
 */
static void PROC2_$DELETE_CLEANUP(void)
{
    /*
     * TODO: Implement full cleanup.
     * This involves calling:
     * - XPD_$CLEANUP()
     * - SMD_$FREE_ASID(asid)
     * - FUN_00e0a454()
     * - SCSI_$FREE_ASID()
     * - ML_$LOCK/UNLOCK for pgroup operations
     * - FUN_00e420b8() for pgroup cleanup
     * - DIR_$DROPU() for directory cleanup
     * - EC_$ADVANCE() for eventcount notification
     * - PROC1_$GET_CPU_USAGE() for accounting
     * - FIM_$CLEANUP/RLS_CLEANUP
     * - PACCT_$LOG() for process accounting
     * - PROC2_$AWAKEN_GUARDIAN()
     * - FIM_$FP_ABORT()
     * - FILE_$UNLOCK_ALL()
     * - NAME_$FREE_ASID()
     * - PEB_$PROC_CLEANUP()
     * - TERM_$P2_CLEANUP()
     * - ACL_$FREE_ASID()
     * - PROC1_$SET_ASID(0)
     * - MST_$FREE_ASID()
     *
     * See decompilation at 0x00e743ce for full details.
     */
}

void PROC2_$DELETE(void)
{
    status_$t status;

    /* Perform all cleanup operations */
    PROC2_$DELETE_CLEANUP();

    /* Enter infinite loop to unbind process */
    for (;;) {
        /* Try to unbind current process from the system */
        PROC1_$UNBIND(PROC1_$CURRENT, &status);

        /* If UNBIND returns (shouldn't normally happen), crash */
        CRASH_SYSTEM(&status);
    }
    /* Never reached */
}
