/*
 * XPD_$FIND_INDEX - Find and validate a debug target process
 *
 * This function locates a process by UID and verifies that:
 * 1. The current process is the debugger for the target
 * 2. The target is currently suspended (has XPD_FLAG_SUSPENDED set)
 *
 * This is an internal helper used by many XPD functions that need
 * to operate on suspended debug targets.
 *
 * Original address: 0x00e5af38
 */

#include "xpd/xpd.h"

/*
 * Process table base address for index calculations
 * Process info is at: 0xEA551C + index * 0xE4
 *
 * Key offsets within each process entry (relative to base + index * 0xE4):
 *   0x-BE (-0xBE): Debugger process index (who is debugging this process)
 *   0x-B9 (-0xB9): Debug flags byte (bit 4 = suspended)
 */
#define PROC_TABLE_BASE     0xEA551C
#define PROC_ENTRY_SIZE     0xE4

/* Offset from current process to debugger mapping table */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

int16_t XPD_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int16_t current_idx;
    int16_t debugger_idx;
    uint8_t flags;

    /* Find the process by UID in the PROC2 table */
    index = PROC2_$FIND_INDEX(proc_uid, status_ret);

    if (*status_ret != status_$ok) {
        return index;
    }

    /*
     * Calculate offset into process table
     * proc_offset = index * 0xE4
     */
    proc_offset = index * PROC_ENTRY_SIZE;

    /*
     * Get the current process's index in the process table
     * This is looked up through PROC1_$CURRENT via a mapping table
     */
    current_idx = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));

    /*
     * Get the debugger index for the target process
     * This is at offset -0xBE from the process entry base
     */
    debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset - 0xBE);

    /* Verify that the current process is the debugger for this target */
    if (debugger_idx != current_idx) {
        *status_ret = status_$xpd_proc_not_debug_target;
        return index;
    }

    /*
     * Check that the target is suspended (bit 4 of flags byte)
     * Flags byte is at offset -0xB9 from process entry base
     */
    flags = *(uint8_t *)(PROC_TABLE_BASE + proc_offset - 0xB9);

    if ((flags & XPD_FLAG_SUSPENDED) == 0) {
        *status_ret = status_$xpd_target_not_suspended;
    }

    return index;
}
