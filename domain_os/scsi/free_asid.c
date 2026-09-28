/*
 * SCSI_$FREE_ASID - Free per-process SCSI resources (Stub)
 *
 * Original address: 0x00e88800
 *
 * Called during process cleanup (from PROC2_$DELETE_CLEANUP and
 * PROC2_$CLEANUP_HANDLERS_INTERNAL) to release any SCSI-related
 * resources associated with a process's address space ID.
 *
 * Re-emitted from the image: a lone `rts` at 0x00E88800 (the 4-byte
 * SCSI_WIRED_PROC segment, where the SAU2 map also puts SCSI_$PROC_START at
 * the same address).  Sole caller 0x00E7444A (PROC2_$DELETE).
 *
 * Assembly:
 *   00e88800    rts
 */

#include "scsi/scsi_internal.h"

void SCSI_$FREE_ASID(void)
{
    /* No-op in this build - SCSI is not supported */
    return;
}
