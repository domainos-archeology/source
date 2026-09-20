/*
 * DEBUG_UNLINK_FROM_LIST - Remove a process from its debugger's target list
 *
 * Re-emitted from the image (0x00E418B0..0x00E41944, 150 bytes).
 *
 * The debug target list is singly linked through the process table:
 *   debugger->first_debug_target_idx (+0x24)  head of the list
 *   target->next_debug_target_idx    (+0x28)  next target
 *   target->debugger_idx             (+0x26)  back link to the debugger
 *
 * Every entry is addressed as A_n = 0xEA551C + idx*0xE4 = entry + 0xE4, so
 * (-0xBE,An) = +0x26, (-0xC0,An) = +0x24, (-0xBC,An) = +0x28.
 *
 * Callers: DEBUG_SETUP_INTERNAL (0x00E4198A), DEBUG_CLEAR_INTERNAL
 * (0x00E41A54).  Both push a `subq.l #2,SP` result slot that nothing fills.
 *
 * Parameters:
 *   proc_idx - (0x8,A6) process table index of the target
 *
 * Original address: 0x00e418b0
 */

#include "proc2/proc2_internal.h"
#include "misc/crash_system.h"

/*
 * Constant status cell in the code region, 0x00E41948 (`pea (0x14,PC)` at
 * 0x00E41932): bytes 00 19 00 01 = status_$proc2_uid_not_found.
 */
static const status_$t proc2_debug_unlink_crash_status = status_$proc2_uid_not_found;

void DEBUG_UNLINK_FROM_LIST(int16_t proc_idx)
{
    proc2_info_t *entry;
    proc2_info_t *debugger_entry;
    int16_t current_idx;
    int16_t prev_idx;

    /* 0x00E418B8-0x00E418C8 */
    entry = P2_INFO_ENTRY(proc_idx);

    /* 0x00E418CC: tst.w (-0xbe,A0) / beq -> exit: not being debugged */
    if (entry->debugger_idx == 0) {
        return;
    }

    /* 0x00E418D2-0x00E418E0: A1 = the debugger's entry (mulu.w here) */
    debugger_entry = P2_INFO_ENTRY((int16_t)entry->debugger_idx);

    /* 0x00E418E4: clr.w (-0xbe,A0) */
    entry->debugger_idx = 0;

    /* 0x00E418E8/0x00E418EC: D1 = debugger->first target, D2 = prev = 0 */
    current_idx = (int16_t)debugger_entry->first_debug_target_idx;
    prev_idx = 0;

    /* 0x00E4192E: tst.w D1w / bne 0x00E418F0 */
    while (current_idx != 0) {
        /* 0x00E418F0: cmp.w D1w,D0w */
        if (current_idx == proc_idx) {
            /* 0x00E418F4: tst.w D2w */
            if (prev_idx == 0) {
                /* 0x00E418F8: debugger->first = entry->next */
                debugger_entry->first_debug_target_idx = entry->next_debug_target_idx;
            } else {
                /* 0x00E41900-0x00E41910: prev->next = entry->next */
                proc2_info_t *prev_entry = P2_INFO_ENTRY(prev_idx);
                prev_entry->next_debug_target_idx = entry->next_debug_target_idx;
            }
            return;   /* 0x00E418FE / 0x00E41916: bra exit */
        }

        /* 0x00E41918-0x00E4192A: prev = current; current = current->next */
        prev_idx = current_idx;
        current_idx = (int16_t)P2_INFO_ENTRY(current_idx)->next_debug_target_idx;
    }

    /*
     * 0x00E41932-0x00E41936: the target was not on its debugger's list.
     * CRASH_SYSTEM is called with the constant cell and, if it returns,
     * the routine simply falls into its epilogue.
     */
    CRASH_SYSTEM(&proc2_debug_unlink_crash_status);
}
