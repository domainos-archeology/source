/*
 * PROC2_$WAIT_TRY_LIVE_CHILD - Try to collect status from a live child
 *
 * Checks if a live (non-zombie) child process has stopped or exited
 * and can be waited on. If the child has changed state, collects the
 * status information.
 *
 * Parameters:
 *   child_idx    - Index of child process to check
 *   options      - Wait options (bit 1 = WUNTRACED)
 *   parent_idx   - Index of parent (waiting) process
 *   prev_idx     - Index of previous sibling in child list
 *   found        - Output: set to -1 if child status collected
 *   result       - Pointer to result buffer
 *   pid_ret      - Pointer to receive child's UPID if found
 *
 * Original address: 0x00e3fc5c
 */

#include "proc2/proc2_internal.h"

/* Wait option bits */
#define WUNTRACED   0x0002

/*
 * Raw memory access macros for wait-related fields
 */
#if defined(M68K)
    #define P2_BASE                 0xEA551C

    /* Flag byte at offset 0x2B (high byte of flags word at 0x2A) */
    #define P2_FLAG_BYTE(idx)       (*(uint8_t*)(P2_BASE + (idx) * 0xE4 - 0xB9))

    /* Debug list index at offset 0x26 */
    #define P2_DBG_LIST(idx)        (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xBE))

    /* Owner session at offset 0x1C */
    #define P2_OWNER_SESS(idx)      (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC8))

    /* Flags word at offset 0x2A */
    #define P2_FLAGS_W(idx)         (*(uint16_t*)(P2_BASE + (idx) * 0xE4 - 0xBA))

    /* UPID at offset 0x32 */
    #define P2_UPID_W(idx)          (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xCE))

    /* Signal number at offset 0x50 */
    #define P2_STOP_SIG(idx)        (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0x50))
#else
    static uint8_t p2_tc_dummy8;
    static int16_t p2_tc_dummy16;
    static uint16_t p2_tc_dummy16u;
    #define P2_FLAG_BYTE(idx)       (p2_tc_dummy8)
    #define P2_DBG_LIST(idx)        (p2_tc_dummy16)
    #define P2_OWNER_SESS(idx)      (p2_tc_dummy16)
    #define P2_FLAGS_W(idx)         (p2_tc_dummy16u)
    #define P2_UPID_W(idx)          (p2_tc_dummy16)
    #define P2_STOP_SIG(idx)        (p2_tc_dummy16)
#endif

/* Flag bit definitions */
#define FLAG_STOPPED    0x40    /* Bit 6: Process is stopped */
#define FLAG_REPORTED   0x20    /* Bit 5: Stop already reported to parent */
#define FLAG_ZOMBIE     0x2000  /* Bit 13: Process is a zombie */

void PROC2_$WAIT_TRY_LIVE_CHILD(int16_t child_idx, uint16_t options,
                                 int16_t parent_idx, int16_t prev_idx,
                                 int8_t *found, uint32_t *result,
                                 int16_t *pid_ret)
{
    uint8_t flags;

    *found = 0;

    flags = P2_FLAG_BYTE(child_idx);

    /*
     * Check if child is stopped but not yet reported.
     * Flags bit 6 = stopped, bit 5 = already reported
     */
    if ((flags & FLAG_STOPPED) != 0 &&
        (flags & FLAG_REPORTED) == 0 &&
        (options & WUNTRACED) != 0) {
        /*
         * Child is stopped and WUNTRACED option set.
         * Mark as reported and return stop status.
         */
        P2_FLAG_BYTE(child_idx) |= FLAG_REPORTED;

        /* Build stop status: (signal << 8) | 0x7F */
        int32_t stop_status = (int32_t)P2_STOP_SIG(child_idx);
        stop_status = (stop_status << 8) | 0x7F;
        ((uint32_t*)result)[0x12] = stop_status;  /* offset 0x48 */

        *pid_ret = P2_UPID_W(child_idx);
        *found = -1;
        return;
    }

    /*
     * Check if child's debug state prevents us from waiting on it.
     * If child has a debugger attached that isn't in our session, skip it.
     */
    if (P2_DBG_LIST(child_idx) != 0) {
        if (P2_DBG_LIST(child_idx) != P2_OWNER_SESS(parent_idx)) {
            return;
        }
    }

    /*
     * Check if child is a zombie that we can reap.
     * Flags bit 13 = zombie
     */
    if ((P2_FLAGS_W(child_idx) & FLAG_ZOMBIE) == 0) {
        /* Not a zombie - nothing to do */
        return;
    }

    /* Reap the child and collect its exit status */
    PROC2_$WAIT_REAP_CHILD(child_idx, parent_idx, prev_idx, result, pid_ret);
    *found = -1;
}
