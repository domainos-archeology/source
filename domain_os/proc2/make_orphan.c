/*
 * PROC2_$MAKE_ORPHAN - Make process an orphan
 *
 * Detaches a process from its parent. The process becomes an orphan
 * with no parent. If the process is already an orphan, returns
 * status_$proc2_already_orphan.
 *
 * This function manipulates the parent-child linked list in the
 * proc2_info_t structure. The relevant (undocumented) fields are:
 *   - offset 0x1E: parent index
 *   - offset 0x20: first child index (in parent)
 *   - offset 0x22: next sibling index
 *   - offset 0x1C: child count or related field
 *
 * TODO: Update proc2_info_t to include child list fields
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID to orphan
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00e40d1a
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for undocumented structure fields
 * These offsets are relative to the proc2_info_t table base (0xEA551C for M68K)
 * The offsets -0xC6, etc. are from the end of the entry (A2 + index*0xE4)
 */
#if defined(ARCH_M68K)
    #define P2_CHILD_BASE(idx)      ((int16_t*)(0xEA551C + ((idx) * 0xE4)))
    #define P2_PARENT_IDX(idx)      (*(P2_CHILD_BASE(idx) - 0x63))
    #define P2_FIRST_CHILD(idx)     (*(P2_CHILD_BASE(idx) - 0x62))
    #define P2_NEXT_SIBLING(idx)    (*(P2_CHILD_BASE(idx) - 0x61))
    #define P2_CHILD_FIELD(idx)     (*(P2_CHILD_BASE(idx) - 0x64))
#else
    /* TODO: Non-M68K implementation needs structure field access */
    static int16_t p2_dummy_field;
    #define P2_PARENT_IDX(idx)      (p2_dummy_field)
    #define P2_FIRST_CHILD(idx)     (p2_dummy_field)
    #define P2_NEXT_SIBLING(idx)    (p2_dummy_field)
    #define P2_CHILD_FIELD(idx)     (p2_dummy_field)
#endif

void PROC2_$MAKE_ORPHAN(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t target_idx;
    int16_t parent_idx;
    int16_t sibling_idx;
    status_$t status;
    uid_t uid_copy;

    /* Copy UID to local storage */
    uid_copy = *proc_uid;

    ML_$LOCK(PROC2_LOCK_ID);

    target_idx = PROC2_$FIND_INDEX(&uid_copy, &status);

    /* Allow orphaning zombie processes */
    if (status == status_$proc2_zombie) {
        status = 0;
    }

    if (status == 0) {
        /* Get parent index */
        parent_idx = P2_PARENT_IDX(target_idx);

        if (parent_idx == 0) {
            /* Already an orphan */
            status = status_$proc2_already_orphan;
        } else {
            /* Find our position in parent's child list */
            sibling_idx = P2_FIRST_CHILD(parent_idx);

            if (target_idx == sibling_idx) {
                /* We're the first child - pass 0 as prev sibling */
                sibling_idx = 0;
            } else {
                /* Search for the sibling that points to us */
                while (P2_NEXT_SIBLING(sibling_idx) != P2_CHILD_FIELD(target_idx)) {
                    sibling_idx = P2_NEXT_SIBLING(sibling_idx);
                    if (sibling_idx == 0) {
                        /* Internal error - process not in parent's child list */
                        /* CRASH_SYSTEM(&PROC2_Internal_Error); */
                        status = 0x80000000;  /* Fatal error */
                        goto done;
                    }
                }
            }

            /* Detach from parent */
            PROC2_$DETACH_FROM_PARENT(target_idx, sibling_idx);
        }
    }

done:
    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}
