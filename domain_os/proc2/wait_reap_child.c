/*
 * PROC2_$WAIT_REAP_CHILD - Reap a child process and collect exit status
 *
 * This function performs the actual work of reaping a child process:
 * 1. Clears any debug state
 * 2. Unlinks from allocation list
 * 3. Unlinks from parent's child list (for live children)
 * 4. Cleans up process group
 * 5. Adds to free list
 * 6. Copies exit status, resource usage, and other info to result buffer
 *
 * Parameters:
 *   child_idx      - Index of child process to reap
 *   parent_idx     - Index of parent (waiting) process
 *   prev_sibling   - Index of previous sibling in child list (0 if first)
 *   result         - Pointer to result buffer (at least 0x68 bytes)
 *   pid_ret        - Pointer to receive reaped child's UPID
 *
 * Original address: 0x00e3fb34
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for wait-related fields
 */
#if defined(M68K)
    #define P2_BASE                 0xEA551C

    /* Entry base pointer */
    #define P2_ENTRY_BASE(idx)      ((uint8_t*)(P2_BASE + ((idx) * 0xE4)))

    /* Linked list fields */
    #define P2_ALLOC_PREV(idx)      (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xD0))  /* offset 0x14 */
    #define P2_ALLOC_NEXT(idx)      (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xD2))  /* offset 0x12 */
    #define P2_FIRST_CHILD(idx)     (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC4))  /* offset 0x20 */
    #define P2_NEXT_SIBLING(idx)    (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC2))  /* offset 0x22 */
    #define P2_FLAGS(idx)           (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xBA))  /* offset 0x2A */
    #define P2_UPID(idx)            (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xCE))  /* offset 0x16 */
    #define P2_DEBUG_IDX(idx)       (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xBE))  /* offset 0x26 */
    #define P2_SELF_IDX(idx)        (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC8))  /* offset 0x1C */

    /* Exit status and resource usage fields */
    #define P2_EXIT_STATUS_PTR(idx) ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0x4C))  /* offset 0x98 */
    #define P2_RUSAGE_PTR(idx)      ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0x40))  /* offset 0xA4 */
    #define P2_UID_PTR(idx)         ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0xDC))  /* offset 0x08 */
    #define P2_ACCT_PTR(idx)        ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0x84))  /* offset 0x60 */
    #define P2_FLAGS_WORD(idx)      (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0x44))  /* offset 0xA0 */

    /* Free list head */
    extern int16_t P2_INFO_ALLOC_PTR;
    extern int16_t P2_FREE_LIST_HEAD;  /* at A5+0x1E2 */
#else
    static int16_t p2_wr_dummy16;
    static uint32_t p2_wr_dummy32;
    #define P2_ALLOC_PREV(idx)      (p2_wr_dummy16)
    #define P2_ALLOC_NEXT(idx)      (p2_wr_dummy16)
    #define P2_FIRST_CHILD(idx)     (p2_wr_dummy16)
    #define P2_NEXT_SIBLING(idx)    (p2_wr_dummy16)
    #define P2_FLAGS(idx)           (p2_wr_dummy16)
    #define P2_UPID(idx)            (p2_wr_dummy16)
    #define P2_DEBUG_IDX(idx)       (p2_wr_dummy16)
    #define P2_SELF_IDX(idx)        (p2_wr_dummy16)
    #define P2_EXIT_STATUS_PTR(idx) ((uint32_t*)&p2_wr_dummy32)
    #define P2_RUSAGE_PTR(idx)      ((uint32_t*)&p2_wr_dummy32)
    #define P2_UID_PTR(idx)         ((uint32_t*)&p2_wr_dummy32)
    #define P2_ACCT_PTR(idx)        ((uint32_t*)&p2_wr_dummy32)
    #define P2_FLAGS_WORD(idx)      (p2_wr_dummy16)
    extern int16_t P2_INFO_ALLOC_PTR;
    extern int16_t P2_FREE_LIST_HEAD;
#endif

void PROC2_$WAIT_REAP_CHILD(int16_t child_idx, int16_t parent_idx,
                            int16_t prev_sibling, uint32_t *result,
                            int16_t *pid_ret)
{
    proc2_info_t *child_entry;
    uint32_t *src;
    uint32_t *dst;
    int16_t i;
    int16_t alloc_prev, alloc_next;

    child_entry = P2_INFO_ENTRY(child_idx);

    /* Clear debug state if child was being debugged */
    if (P2_DEBUG_IDX(child_idx) != 0) {
        DEBUG_CLEAR_INTERNAL(P2_SELF_IDX(child_idx), 0);
    }

    /* Unlink from allocation list */
    alloc_prev = P2_ALLOC_PREV(child_idx);
    alloc_next = P2_ALLOC_NEXT(child_idx);

    if (alloc_prev == 0) {
        /* We're at head of list */
        P2_INFO_ALLOC_PTR = alloc_next;
    } else {
        /* Update previous entry's next pointer */
        P2_ALLOC_NEXT(alloc_prev) = alloc_next;
    }

    /* Update next entry's prev pointer */
    P2_ALLOC_NEXT(alloc_next) = alloc_prev;  /* Actually P2_ALLOC_PREV(alloc_next) */

    /* Unlink from parent's child list if not a zombie */
    if (P2_FLAGS(child_idx) >= 0) {  /* Check if not zombie (bit 15 not set) */
        if (prev_sibling == 0) {
            /* We're first child - update parent's first_child */
            P2_FIRST_CHILD(parent_idx) = P2_NEXT_SIBLING(child_idx);
        } else {
            /* Update previous sibling's next pointer */
            P2_NEXT_SIBLING(prev_sibling) = P2_NEXT_SIBLING(child_idx);
        }
    }

    /* Clean up process group (mode 1 = refcount only) */
    PGROUP_CLEANUP_INTERNAL(child_entry, 1);

    /* Clear the "stopped" flag (bit 5) */
    P2_FLAGS(child_idx) &= ~0x20;

    /* Add to free list */
    P2_ALLOC_NEXT(child_idx) = P2_FREE_LIST_HEAD;
    P2_FREE_LIST_HEAD = child_idx;

    /* Copy exit status (2 longwords at offset 0x48) */
    result[0x12] = P2_EXIT_STATUS_PTR(child_idx)[0];
    result[0x13] = P2_EXIT_STATUS_PTR(child_idx)[1];

    /* Copy resource usage (5 longwords at offset 0x50) */
    src = P2_RUSAGE_PTR(child_idx);
    dst = &result[0x14];
    for (i = 0; i < 5; i++) {
        *dst++ = *src++;
    }

    /* Copy UID (2 longwords at offset 0x38) */
    result[0x0E] = P2_UID_PTR(child_idx)[0];
    result[0x0F] = P2_UID_PTR(child_idx)[1];

    /* Copy accounting info (14 longwords at offset 0x00) */
    src = P2_ACCT_PTR(child_idx);
    dst = result;
    for (i = 0; i < 14; i++) {
        *dst++ = *src++;
    }

    /* Set flags in result buffer */
    ((int8_t*)result)[0x65] = (P2_FLAGS_WORD(child_idx) < 0) ? -1 : 0;
    ((int8_t*)result)[0x66] = ((P2_FLAGS_WORD(child_idx) & 0x4000) != 0) ? -1 : 0;

    /* Return the child's UPID */
    *pid_ret = P2_UPID(child_idx);
}
