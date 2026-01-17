/*
 * DEBUG_UNLINK_FROM_LIST - Remove process from debugger's target list
 *
 * When a process is being debugged, it is linked into the debugger's
 * list of debug targets. This function removes it from that list.
 *
 * The debug target list is a singly-linked list:
 * - debugger->first_debug_target_idx: head of list
 * - target->next_debug_target_idx: next target in list
 * - target->debugger_idx: back-pointer to debugger
 *
 * From: 0x00e418b0
 *
 * Original assembly:
 *   00e418b0    link.w A6,-0x14
 *   00e418b4    movem.l {  A2 D3 D2},-(SP)
 *   00e418b8    move.w (0x8,A6),D0w         ; D0 = proc_idx
 *   00e418bc    movea.l #0xea551c,A0
 *   00e418c2    move.w D0w,D1w
 *   00e418c4    muls.w #0xe4,D1
 *   00e418c8    lea (0x0,A0,D1*0x1),A0      ; A0 = entry for proc_idx
 *   00e418cc    tst.w (-0xbe,A0)            ; test debugger_idx
 *   00e418d0    beq.b return                ; if not being debugged, return
 *   ...                                     ; unlink from debugger's list
 *   00e41932    pea (0x14,PC)               ; push error status
 *   00e41936    jsr CRASH_SYSTEM            ; crash if not found in list
 */

#include "proc2/proc2_internal.h"
#include "misc/crash_system.h"

/* Error status for process not found in debug list */
static const status_$t Proc2_UID_Not_Found_err = status_$proc2_uid_not_found;

void DEBUG_UNLINK_FROM_LIST(int16_t proc_idx)
{
    proc2_info_t *entry;
    proc2_info_t *debugger_entry;
    int16_t debugger_idx;
    int16_t current_idx;
    int16_t prev_idx;

    entry = P2_INFO_ENTRY(proc_idx);

    /* If not being debugged, nothing to do */
    if (entry->debugger_idx == 0) {
        return;
    }

    /* Get the debugger's entry */
    debugger_idx = entry->debugger_idx;
    debugger_entry = P2_INFO_ENTRY(debugger_idx);

    /* Clear our debugger reference */
    entry->debugger_idx = 0;

    /*
     * Walk the debugger's target list to find and remove this process.
     * The list is singly-linked through next_debug_target_idx.
     */
    prev_idx = 0;
    current_idx = debugger_entry->first_debug_target_idx;

    while (current_idx != 0) {
        if (current_idx == proc_idx) {
            /* Found it - unlink from list */
            if (prev_idx == 0) {
                /* Removing from head of list */
                debugger_entry->first_debug_target_idx = entry->next_debug_target_idx;
            } else {
                /* Removing from middle/end of list */
                proc2_info_t *prev_entry = P2_INFO_ENTRY(prev_idx);
                prev_entry->next_debug_target_idx = entry->next_debug_target_idx;
            }
            return;
        }

        /* Move to next entry in list */
        prev_idx = current_idx;
        current_idx = P2_INFO_ENTRY(current_idx)->next_debug_target_idx;
    }

    /*
     * Process was not found in debugger's list - this is a fatal error.
     * The data structures are corrupted.
     */
    CRASH_SYSTEM(&Proc2_UID_Not_Found_err);
}
