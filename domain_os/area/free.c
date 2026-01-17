/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 * AREA_$SHUTDOWN - Shutdown area subsystem
 * AREA_$FREE_FROM - Free areas from specific context
 *
 * Original addresses:
 *   AREA_$FREE_ASID: 0x00E07E80
 *   AREA_$SHUTDOWN: 0x00E07F0E
 *   AREA_$FREE_FROM: 0x00E07FC6
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/* Number of ASIDs to iterate */
#define ASID_COUNT          58

/* Number of UID hash buckets */
#define UID_HASH_BUCKETS    11

/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 *
 * Called when an address space is being destroyed. Iterates through
 * all areas owned by the specified ASID and deletes them.
 *
 * Parameters:
 *   asid - Address space ID whose areas should be freed
 *
 * Original address: 0x00E07E80
 */
void AREA_$FREE_ASID(int16_t asid)
{
    area_$entry_t *entry;
    area_$entry_t *next;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;
    status_$t status;

    ML_$LOCK(ML_LOCK_AREA);

    /* Get head of ASID's area list */
    int asid_offset = asid * sizeof(uint32_t);
    area_$entry_t **asid_list = (area_$entry_t **)((char *)globals + 0x4D8 + asid_offset);
    entry = *asid_list;

    /* Iterate through all areas for this ASID */
    while (entry != NULL) {
        /* Save next pointer before modifying entry */
        next = entry->next;

        /* Mark BSTE as invalid */
        entry->first_bste = -1;

        /* Delete the area */
        area_$internal_delete(entry, entry->reserved_2a, &status, 0);

        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* Clear prev pointer and add to free list */
        entry->prev = NULL;
        entry->next = AREA_$FREE_LIST;
        AREA_$FREE_LIST = entry;
        AREA_$N_FREE++;

        entry = next;
    }

    /* Clear the ASID list head */
    *asid_list = NULL;

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$SHUTDOWN - Shutdown area subsystem
 *
 * Called during system shutdown. Frees all areas from all ASIDs,
 * then cleans up the UID hash table.
 *
 * Original address: 0x00E07F0E
 */
void AREA_$SHUTDOWN(void)
{
    int i;
    area_$uid_hash_t *hash_entry;
    area_$uid_hash_t *next_hash;
    area_$entry_t *entry;
    area_$entry_t *next_entry;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;
    status_$t status;

    /* First, free all areas from all ASIDs */
    for (i = 0; i < ASID_COUNT; i++) {
        AREA_$FREE_ASID(i);
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Clean up UID hash table */
    for (i = 0; i < UID_HASH_BUCKETS; i++) {
        /* Get hash bucket */
        hash_entry = (area_$uid_hash_t *)((uint32_t *)((char *)globals + 0x454))[i];

        while (hash_entry != NULL) {
            /* Process all entries linked to this hash bucket */
            entry = hash_entry->first_entry;

            while (entry != NULL) {
                next_entry = entry->next;

                /* Mark BSTE as invalid */
                entry->first_bste = -1;

                /* Delete the area */
                area_$internal_delete(entry, entry->reserved_2a, &status, 0);

                if (status != status_$ok) {
                    CRASH_SYSTEM(&status);
                }

                /* Clear prev pointer and add to free list */
                entry->prev = NULL;
                entry->next = AREA_$FREE_LIST;
                AREA_$FREE_LIST = entry;
                AREA_$N_FREE++;

                entry = next_entry;
            }

            /* Clear first_entry and return hash entry to pool */
            hash_entry->first_entry = NULL;
            next_hash = hash_entry->next;

            /* Link hash entry back to free pool */
            hash_entry->next = *(area_$uid_hash_t **)((char *)globals + 0x450);
            *(area_$uid_hash_t **)((char *)globals + 0x450) = hash_entry;

            hash_entry = next_hash;
        }
    }

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$FREE_FROM - Free areas from specific context
 *
 * This function appears to handle freeing areas that were created
 * from a specific remote context.
 *
 * Original address: 0x00E07FC6
 *
 * TODO: Full analysis needed - the decompilation for this function
 * was not provided. This is a stub based on the signature.
 */
void AREA_$FREE_FROM(uint32_t param_1)
{
    /* TODO: Implement based on Ghidra analysis */
    (void)param_1;
}
