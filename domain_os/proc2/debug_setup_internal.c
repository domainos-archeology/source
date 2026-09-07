/*
 * DEBUG_SETUP_INTERNAL - Set up debug relationship between processes
 *
 * Links a target process to a debugger process. The target is added
 * to the debugger's list of debug targets, and the target's debugger_idx
 * is set to point back to the debugger.
 *
 * From: 0x00e4194c
 *
 * Parameters:
 *   target_idx   - Index of process to be debugged
 *   debugger_idx - Index of debugger process
 *   flag         - If negative (bit 7 set), write debug data via XPD
 *
 * Original assembly:
 *   00e4194c    link.w A6,-0x24
 *   00e41950    movem.l {  A5 A3 A2 D4 D3 D2},-(SP)
 *   ...
 *   00e41990    move.w D3w,(-0xbe,A2)      ; target->debugger_idx = debugger_idx
 *   00e41994    move.w (-0xc0,A3),(-0xbc,A2)  ; target->next = debugger->first
 *   00e4199a    move.w D2w,(-0xc0,A3)      ; debugger->first = target_idx
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
 * The full entry is 0xE4 bytes but proc2_info_t only defines through 0xBF.
 */
/*
 * The 14-byte ptrace option record at entry+0xCE is xpd/xpd.h's
 * xpd_$ptrace_opts_t; proc2_info_t declares it as raw bytes so that xpd.h
 * need not be included by proc2.h.
 */
#define ENTRY_PTRACE_OPTS(entry) \
    ((xpd_$ptrace_opts_t *)((entry)->ptrace_opts))

#define ENTRY_DEBUG_ADDR(entry) \
    ((void *)((uint8_t *)(entry) + 0x96))

#define ENTRY_FLAGS_BYTE(entry) \
    (*((uint8_t *)(entry) + 0x2B))

/* Static data for XPD_$WRITE calls - appears to be small constants */
/*
 * Constant cells in the code region, passed by reference to XPD_$WRITE
 * (cell address = pea instruction address + 2 + d; values read with gsk):
 *   0x00E41A20 (0x00E419E8 `pea (0x36,PC)`) - the length longword, 1
 *   0x00E41A1C (0x00E419E4 `pea (0x36,PC)`) - the source bytes, 0xFFFFFFFF
 */
static const int32_t debug_write_len = 1;
static const uint32_t debug_write_buffer = 0xFFFFFFFFu;

void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag)
{
    proc2_info_t *target_entry;
    proc2_info_t *debugger_entry;
    xpd_$ptrace_opts_t local_opts;
    status_$t status;

    target_entry = P2_INFO_ENTRY(target_idx);
    debugger_entry = P2_INFO_ENTRY(debugger_idx);

    /*
     * If target is already being debugged, unlink from the old debugger first.
     */
    if (target_entry->debugger_idx != 0) {
        DEBUG_UNLINK_FROM_LIST(target_idx);
    }

    /*
     * Set up the debug relationship:
     * 1. Set target's debugger reference
     * 2. Insert target at head of debugger's target list
     */
    target_entry->debugger_idx = debugger_idx;
    target_entry->next_debug_target_idx = debugger_entry->first_debug_target_idx;
    debugger_entry->first_debug_target_idx = target_idx;

    /*
     * If the internal debug flag is set, awaken the guardian.
     * This notifies the debugger of state changes.
     */
    if ((ENTRY_FLAGS_BYTE(target_entry) & DEBUG_FLAG_AWAKEN_GUARDIAN) != 0) {
        PROC2_$AWAKEN_GUARDIAN(&target_idx);
    }

    /*
     * Copy ptrace options to local, reset them, then copy back.
     * This ensures consistent debug state for the newly-attached process.
     */
    local_opts = *ENTRY_PTRACE_OPTS(target_entry);
    XPD_$RESET_PTRACE_OPTS(&local_opts);
    *ENTRY_PTRACE_OPTS(target_entry) = local_opts;

    /*
     * If flag is negative (bit 7 set), write debug initialization data.
     * The offset is computed from cr_rec_2 field + 0x90.
     */
    if (flag < 0) {
        uint32_t offset = target_entry->cr_rec_2 + 0x90;
        /*
         * 0x00E419EC-0x00E419F8: `movea.l (-0x78,A2),A0; pea (0x90,A0)` -
         * the target address is a 32-bit VA pushed BY VALUE.
         */
        XPD_$WRITE(ENTRY_DEBUG_ADDR(target_entry), ARCH_VA_TO_PTR(offset),
                   &debug_write_len, &debug_write_buffer, &status);
    }

    /*
     * Awaken guardian again after setup is complete.
     */
    if ((ENTRY_FLAGS_BYTE(target_entry) & DEBUG_FLAG_AWAKEN_GUARDIAN) != 0) {
        PROC2_$AWAKEN_GUARDIAN(&target_idx);
    }
}
