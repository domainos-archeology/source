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

#include "proc2/proc2_internal.h"
#include "misc/misc.h"

/*
 * PROC2_$DELETE_CLEANUP - release every resource the process still owns
 *
 * Original address: 0x00E743CE, 1692 bytes.  Reached only from
 * PROC2_$DELETE's `bsr.b 0x00e743ce` at 0x00E743A6, so it is emitted here
 * as a file-static routine.
 *
 * TODO(source-ld0): PROC2_$DELETE_CLEANUP (0x00E743CE, 1692 bytes) is not
 * yet decompiled; this body is empty.  Everything the original does is
 * missing: XPD_$CLEANUP, SMD_$FREE_ASID, DMA_$FREE_ASID (0x00E0A454),
 * SCSI_$FREE_ASID,
 * the ML_$LOCK/ML_$UNLOCK-bracketed process-group teardown via
 * PGROUP_CLEANUP_INTERNAL, DIR_$DROPU, the EC_$ADVANCE notifications,
 * PROC1_$GET_CPU_USAGE accounting, FIM_$CLEANUP / FIM_$RLS_CLEANUP,
 * PACCT_$LOG, PROC2_$AWAKEN_GUARDIAN, FIM_$FP_ABORT, FILE_$UNLOCK_ALL,
 * NAME_$FREE_ASID, PEB_$PROC_CLEANUP, TERM_$P2_CLEANUP, ACL_$FREE_ASID,
 * PROC1_$SET_ASID(0) and MST_$FREE_ASID.  Tracked by bead source-ld0
 * ("Complete proc2 subsystem (build_info, delete cleanup, signal
 * delivery)").
 */
static void PROC2_$DELETE_CLEANUP(void)
{
}

void PROC2_$DELETE(void)
{
    status_$t status;

    /*
     * Full instruction trace (0x00E74398, 54 bytes):
     *   00e74398  link.w A6,-0x4
     *   00e7439c  movem.l {A5 A3 A2},-(SP)
     *   00e743a0  lea (0xe8605c).l,A5      ; this module's data base
     *   00e743a6  bsr.b 0x00e743ce         ; PROC2_$DELETE_CLEANUP
     *   00e743a8  movea.l #0xe20608,A3     ; &PROC1_$CURRENT
     *   00e743ae  lea (A3),A2
     *   00e743b0  subq.l #0x2,SP           ; Pascal result slot
     *   00e743b2  pea (-0x4,A6)            ; &status
     *   00e743b6  move.w (A2),-(SP)        ; PROC1_$CURRENT
     *   00e743b8  jsr 0x00e14e24.l         ; PROC1_$UNBIND
     *   00e743be  addq.w #0x8,SP
     *   00e743c0  pea (-0x4,A6)
     *   00e743c4  jsr 0x00e1e700.l         ; CRASH_SYSTEM(&status)
     *   00e743ca  addq.w #0x4,SP
     *   00e743cc  bra.b 0x00e743b0         ; retry BOTH calls, forever
     *
     * Note the A5 module base here is 0x00E8605C, not the 0x00E7BE84 the
     * rest of PROC2 uses.
     */
    PROC2_$DELETE_CLEANUP();

    for (;;) {
        PROC1_$UNBIND(PROC1_$CURRENT, &status);

        /* If PROC1_$UNBIND returns, crash -- then try the whole thing again */
        CRASH_SYSTEM(&status);
    }
}
