/*
 * PROC2_$DETACH_FROM_PARENT - Detach process from its parent's child list
 *
 * Removes a process from its parent's child list and handles cleanup:
 * 1. Unlinks from parent's sibling chain
 * 2. Clears parent pointer
 * 3. For zombies: cleans up pgroup and adds to free list
 * 4. For non-zombies: sets orphan flag
 *
 * Parameters:
 *   child_idx        - Index of process to detach
 *   prev_sibling_idx - Index of previous sibling (0 if first child)
 *
 * Original address: 0x00e40df4
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for parent/child relationships
 */
#if defined(M68K)
    #define P2_BASE                 0xEA551C

    /* Parent index at offset 0x1E */
    #define P2_DP_PARENT(idx)       (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC6))

    /* First child at offset 0x20 */
    #define P2_DP_FIRST_CHILD(idx)  (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC4))

    /* Next sibling at offset 0x22 */
    #define P2_DP_NEXT_SIB(idx)     (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC2))

    /* Flags word at offset 0x2A */
    #define P2_DP_FLAGS(idx)        (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xBA))

    /* Allocation list pointers */
    #define P2_DP_ALLOC_PREV(idx)   (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xD0))
    #define P2_DP_ALLOC_NEXT(idx)   (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xD2))

    /* Entry pointer calculation */
    #define P2_DP_ENTRY(idx)        ((proc2_info_t*)(P2_BASE + (idx) * 0xE4 - 0xE4))

    /* Global variables */
    extern int16_t P2_INFO_ALLOC_PTR;   /* at A5+0x1E0 */
    extern int16_t P2_FREE_LIST_HEAD;   /* at A5+0x1E2 */
#else
    static int16_t p2_dp_dummy16;
    #define P2_DP_PARENT(idx)       (p2_dp_dummy16)
    #define P2_DP_FIRST_CHILD(idx)  (p2_dp_dummy16)
    #define P2_DP_NEXT_SIB(idx)     (p2_dp_dummy16)
    #define P2_DP_FLAGS(idx)        (p2_dp_dummy16)
    #define P2_DP_ALLOC_PREV(idx)   (p2_dp_dummy16)
    #define P2_DP_ALLOC_NEXT(idx)   (p2_dp_dummy16)
    #define P2_DP_ENTRY(idx)        ((proc2_info_t*)0)
    extern int16_t P2_INFO_ALLOC_PTR;
    extern int16_t P2_FREE_LIST_HEAD;
#endif

/* Flag bit definitions */
#define FLAG_ZOMBIE         0x2000  /* Bit 13: Process is a zombie */
#define FLAG_ORPHAN         0x8000  /* Bit 15: Process is an orphan */

void PROC2_$DETACH_FROM_PARENT(int16_t child_idx, int16_t prev_sibling_idx)
{
    int16_t parent_idx;
    int16_t alloc_prev, alloc_next;

    /* Verify parent pointer is valid */
    parent_idx = P2_DP_PARENT(child_idx);
    if (parent_idx == 0) {
        /* Internal error - should have a parent */
        CRASH_SYSTEM(&PROC2_Internal_Error);
        return;
    }

    /* Unlink from parent's child list */
    if (prev_sibling_idx == 0) {
        /* We're the first child - update parent's first_child pointer */
        P2_DP_FIRST_CHILD(parent_idx) = P2_DP_NEXT_SIB(child_idx);
    } else {
        /* Update previous sibling's next pointer */
        P2_DP_NEXT_SIB(prev_sibling_idx) = P2_DP_NEXT_SIB(child_idx);
    }

    /* Clear our parent pointer */
    P2_DP_PARENT(child_idx) = 0;

    /* Check if zombie or live process */
    if ((P2_DP_FLAGS(child_idx) & FLAG_ZOMBIE) == 0) {
        /* Not a zombie - just set orphan flag */
        P2_DP_FLAGS(child_idx) |= FLAG_ORPHAN;
    } else {
        /* Zombie - clean up pgroup and add to free list */
        PGROUP_CLEANUP_INTERNAL(P2_DP_ENTRY(child_idx), 1);

        /* Unlink from allocation list */
        alloc_prev = P2_DP_ALLOC_PREV(child_idx);
        alloc_next = P2_DP_ALLOC_NEXT(child_idx);

        if (alloc_prev == 0) {
            /* At head of allocation list */
            P2_INFO_ALLOC_PTR = alloc_next;
        } else {
            /* Update previous entry's next pointer */
            P2_DP_ALLOC_NEXT(alloc_prev) = alloc_next;
        }

        /* Update next entry's prev pointer */
        P2_DP_ALLOC_PREV(alloc_next) = alloc_prev;

        /* Add to free list */
        P2_DP_ALLOC_NEXT(child_idx) = P2_FREE_LIST_HEAD;
        P2_FREE_LIST_HEAD = child_idx;
    }
}
