/*
 * PROC2_$ZOMBIE_LIST - List zombie processes
 *
 * Iterates through all 57 process slots and returns UIDs of
 * zombie processes (flags 0x2100 set - valid and zombie).
 * Supports pagination via start_index.
 *
 * Parameters:
 *   uid_list - Array to receive process UIDs
 *   max_count - Pointer to max entries (capped at 57)
 *   count - Pointer to receive actual count
 *   start_index - Pointer to starting index (1-57)
 *   more_flag - Pointer to receive "more available" flag
 *   last_index - Pointer to receive last processed index
 *
 * Original address: 0x00e40548
 */

#include "proc2.h"

/* External FIM cleanup functions */
extern status_$t FIM_$CLEANUP(void *context);
extern void FIM_$RLS_CLEANUP(void *context);
extern void FIM_$POP_SIGNAL(void *context);

void PROC2_$ZOMBIE_LIST(uid_$t *uid_list, uint16_t *max_count, uint16_t *count,
                        int32_t *start_index, uint8_t *more_flag, int32_t *last_index)
{
    uint16_t max_entries;
    uint16_t found_count;
    int32_t start;
    int16_t slot;
    int16_t last_slot;
    int16_t first_match;
    proc2_info_t *entry;
    uid_$t *out_ptr;
    uint8_t fim_context[24];
    status_$t status;

    max_entries = *max_count;
    start = *start_index;
    *more_flag = 0;
    *last_index = 0;
    first_match = 0;
    last_slot = 0;
    found_count = 0;

    /* Cap at 57 entries */
    if (max_entries > 57) {
        max_entries = 57;
    }

    /* Set up FIM cleanup context */
    status = FIM_$CLEANUP(fim_context);

    if (status == 0x00120035) {
        ML_$LOCK(PROC2_LOCK_ID);

        out_ptr = uid_list;

        /* Iterate through all 57 slots */
        for (slot = 1; slot <= 57; slot++) {
            entry = P2_INFO_ENTRY(slot);

            /* Check if process is valid AND zombie:
             * flags & 0x0100 (valid) and flags & 0x2000 (zombie)
             */
            if (((entry->flags & 0x0100) != 0) && ((entry->flags & 0x2000) != 0)) {
                first_match = slot;

                /* Only include if at or past start_index */
                if (slot >= start) {
                    found_count++;
                    if (found_count <= max_entries) {
                        out_ptr->high = entry->uid.high;
                        out_ptr->low = entry->uid.low;
                        out_ptr++;
                        last_slot = slot;
                    }
                }
            }
        }

        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        *count = found_count;

        /* Adjust count if we hit the max */
        if (found_count < max_entries) {
            max_entries = found_count;
        }
        if (max_entries < found_count) {
            *count = max_entries;
            *more_flag = 1;
        }

        *last_index = last_slot;
    } else {
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
