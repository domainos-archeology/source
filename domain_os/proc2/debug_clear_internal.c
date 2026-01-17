/*
 * DEBUG_CLEAR_INTERNAL - Clear debug relationship for a process
 *
 * Removes a process from its debugger's target list and clears
 * the debug state. If the process is not a zombie, it may be
 * resumed to continue execution.
 *
 * From: 0x00e41a24
 *
 * Parameters:
 *   proc_idx - Index of process to stop debugging
 *   flag     - If negative (bit 7 set), write debug data and resume
 *
 * Original assembly:
 *   00e41a24    link.w A6,-0xc
 *   00e41a28    movem.l {  A5 A2 D3 D2},-(SP)
 *   ...
 *   00e41a54    bsr.w DEBUG_UNLINK_FROM_LIST
 *   00e41a5a    bclr.b #0x4,(-0xb9,A2)     ; clear awaken flag
 *   00e41a60    move.w (-0xba,A2),D0w      ; get flags word
 *   00e41a64    btst.l #0xd,D0             ; test zombie bit
 *   ...
 */

#include "proc2/proc2_internal.h"

/*
 * Internal debug flag in flags byte (offset 0x2B from entry)
 * Bit 4 (0x10) indicates guardian should be awakened on debug events.
 */
#define DEBUG_FLAG_AWAKEN_GUARDIAN  0x10

/*
 * Get pointer to extended fields beyond the base proc2_info_t structure.
 */
#define ENTRY_DEBUG_ADDR(entry) \
    ((void *)((uint8_t *)(entry) + 0x96))

#define ENTRY_FLAGS_BYTE(entry) \
    (*((uint8_t *)(entry) + 0x2B))

/* Static data for XPD_$WRITE calls */
static const uint32_t debug_clear_data1 = 0;
static const uint32_t debug_clear_data2 = 0;

void DEBUG_CLEAR_INTERNAL(int16_t proc_idx, int8_t flag)
{
    proc2_info_t *entry;
    status_$t status[2];  /* XPD_$WRITE may need multiple status values */

    entry = P2_INFO_ENTRY(proc_idx);

    /* If not being debugged, nothing to do */
    if (entry->debugger_idx == 0) {
        return;
    }

    /* Unlink from debugger's target list */
    DEBUG_UNLINK_FROM_LIST(proc_idx);

    /* Clear the internal debug awaken flag (bit 4) */
    ENTRY_FLAGS_BYTE(entry) &= ~DEBUG_FLAG_AWAKEN_GUARDIAN;

    /*
     * Check if process is a zombie (bit 13 = 0x2000 in flags).
     * Zombie processes just need their guardian awakened.
     */
    if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
        /* Process is zombie - awaken guardian and return */
        PROC2_$AWAKEN_GUARDIAN(&proc_idx);
        return;
    }

    /*
     * Process is not a zombie.
     * If flag is negative (bit 7 set), write debug clear data and resume.
     */
    if (flag < 0) {
        uint32_t offset = entry->cr_rec_2 + 0x90;

        /* Write debug data to clear debug state */
        XPD_$WRITE(ENTRY_DEBUG_ADDR(entry), offset,
                   &debug_clear_data1, &debug_clear_data2, status);

        /* Clear awaken flag again after XPD write */
        ENTRY_FLAGS_BYTE(entry) &= ~DEBUG_FLAG_AWAKEN_GUARDIAN;

        /* Resume the process via PROC1 */
        PROC1_$RESUME(entry->level1_pid, status);
    }
}
