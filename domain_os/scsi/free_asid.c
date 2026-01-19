/*
 * SCSI_$FREE_ASID - Free per-process SCSI resources (Stub)
 *
 * Original address: 0x00e88800
 *
 * Called during process cleanup (from PROC2_$DELETE_CLEANUP and
 * PROC2_$CLEANUP_HANDLERS_INTERNAL) to release any SCSI-related
 * resources associated with a process's address space ID.
 *
 * In this Domain/OS build, SCSI support is not enabled, so this
 * function is a no-op - it simply returns immediately.
 *
 * Assembly:
 *   00e88800    rts
 */

#include "scsi/scsi.h"

void SCSI_$FREE_ASID(void)
{
    /* No-op in this build - SCSI is not supported */
    return;
}
