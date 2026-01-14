/*
 * PROC2_$LIST_PGROUP - List process group members
 *
 * Returns UIDs of all processes belonging to the specified process group.
 *
 * Parameters:
 *   pgroup_uid - UID of process group
 *   uid_list - Array to receive member UIDs
 *   max_count - Pointer to max entries (capped at 57)
 *   count - Pointer to receive actual count
 *
 * Original address: 0x00e401ea
 */

#include "proc2.h"

/* External FIM cleanup functions */
extern status_$t FIM_$CLEANUP(void *context);
extern void FIM_$RLS_CLEANUP(void *context);
extern void FIM_$POP_SIGNAL(void *context);


/* Expected status from FIM_$CLEANUP */
#define status_$cleanup_handler_set 0x00120035

void PROC2_$LIST_PGROUP(uid_$t *pgroup_uid, uid_$t *uid_list, uint16_t *max_count, uint16_t *count)
{
    uint16_t max_entries;
    uint16_t found_count;
    int16_t pgroup_idx;
    int16_t index;
    proc2_info_t *entry;
    uid_$t *out_ptr;
    uint8_t fim_context[24];
    status_$t status;

    max_entries = *max_count;
    found_count = 0;

    /* Cap at 57 entries */
    if (max_entries > 57) {
        max_entries = 57;
    }

    /* Set up FIM cleanup context */
    status = FIM_$CLEANUP(fim_context);

    if (status == status_$cleanup_handler_set) {
        ML_$LOCK(PROC2_LOCK_ID);

        /* Get pgroup index from UID */
        pgroup_idx = PROC2_$UID_TO_PGROUP_INDEX(pgroup_uid);

        out_ptr = uid_list;
        index = P2_INFO_ALLOC_PTR;

        if (pgroup_idx != 0) {
            while (index != 0) {
                entry = P2_INFO_ENTRY(index);

                /* Check if process is in this pgroup:
                 * - flags high bit set (0x80 in low byte = 0x0080 in word)
                 * - pgroup_table_idx matches pgroup_idx
                 */
                if (((entry->flags & 0x0080) != 0) && (entry->pgroup_table_idx == pgroup_idx)) {
                    found_count++;
                    if (found_count <= max_entries) {
                        out_ptr->high = entry->uid.high;
                        out_ptr->low = entry->uid.low;
                        out_ptr++;
                    }
                }

                /* Next entry in allocation list */
                index = entry->next_index;
            }
        }

        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        *count = found_count;
        if (max_entries < found_count) {
            *count = max_entries;
        }
    } else {
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
