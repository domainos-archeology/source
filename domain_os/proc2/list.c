/*
 * PROC2_$LIST - List process UIDs
 *
 * Returns a list of process UIDs. The first entry is always the
 * system process UID at 0xEA551C. Subsequent entries are processes
 * with flag 0x8000 set and ASID != 1.
 *
 * Parameters:
 *   uid_list - Array to receive process UIDs
 *   max_ull - Pointer to max entries (capped at 57)
 *   ull - Pointer to receive actual count
 *
 * Original address: 0x00e402f0
 */

#include "proc2/proc2_internal.h"

/* System process UID at table base */
#if defined(M68K)
#define PROC2_SYSTEM_UID    (*(uid_t*)0xEA551C)
#else
#define PROC2_SYSTEM_UID    proc2_system_uid
#endif

void PROC2_$LIST(uid_t *uid_list, uint16_t *max_ull, uint16_t *ull)
{
    uint16_t max_count;
    uint16_t count;
    int16_t index;
    proc2_info_t *entry;
    uid_t *out_ptr;
    uint8_t fim_context[24];
    status_$t status;

    max_count = *max_ull;
    count = 1;  /* Always include system process */

    /* Cap at 57 entries */
    if (max_count > 57) {
        max_count = 57;
    }

    /* Set up FIM cleanup context */
    status = FIM_$CLEANUP(fim_context);

    if (status == 0x00120035) {  /* Expected status */
        ML_$LOCK(PROC2_LOCK_ID);

        /* First entry is always the system process UID */
        if (max_count != 0) {
            uid_list[0].high = PROC2_SYSTEM_UID.high;
            uid_list[0].low = PROC2_SYSTEM_UID.low;
        }

        out_ptr = &uid_list[1];
        index = P2_INFO_ALLOC_PTR;

        while (index != 0) {
            entry = P2_INFO_ENTRY(index);

            /* Check if process should be listed:
             * - High bit of flags byte (0x8000 flag) must be set
             * - ASID must not be 1
             */
            if (((entry->flags & 0x8000) != 0) && (entry->asid != 1)) {
                count++;
                if (count <= max_count) {
                    out_ptr->high = entry->uid.high;
                    out_ptr->low = entry->uid.low;
                }
                out_ptr++;
            }

            /* Next entry in allocation list */
            index = entry->next_index;
        }

        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        /* Return actual count, capped at max */
        *ull = count;
        if (count < max_count) {
            max_count = count;
        }
        if (max_count < count) {
            *ull = max_count;
        }
    } else {
        /* Cleanup failed */
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *ull = 0;
    }
}
