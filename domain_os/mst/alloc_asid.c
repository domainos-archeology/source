/*
 * MST_$ALLOC_ASID - Allocate a new Address Space ID
 *
 * This function allocates a new ASID for a process. Each process requires
 * a unique ASID to maintain its private address space mappings.
 *
 * The function:
 * 1. Acquires the MST lock
 * 2. Searches the ASID bitmap for a free slot
 * 3. Marks the slot as allocated
 * 4. Initializes the segment table page for the new ASID
 * 5. Sets up the first MST entry with the OS_WIRED UID
 * 6. Releases the lock and returns the new ASID
 *
 * Note: OS_WIRED_$UID is a special UID that marks wired (pinned) memory
 * segments that should never be paged out.
 */

#include "mst.h"

/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h via mst.h */

/* External: Initialize segment table page for an ASID */
extern status_$t FUN_00e43f40(uint16_t asid, uint16_t flags, void *table_ptr);

/* External: OS wired memory UID */
extern uid_t OS_WIRED_$UID;

/*
 * MST_$ALLOC_ASID - Allocate a new Address Space ID
 *
 * @param status_ret  Output: status code (status_$ok on success)
 * @return The allocated ASID, or 0 on failure
 */
uint16_t MST_$ALLOC_ASID(status_$t *status_ret)
{
    int16_t probe;
    uint16_t asid;
    uint16_t table_index;
    status_$t status;
    uint8_t *mst_page;

    /* Lock the ASID allocation lock */
    ML_$LOCK(MST_LOCK_ASID);

    /*
     * Search for a free ASID by scanning the bitmap.
     * ASID 0 is reserved for the kernel/global mappings.
     */
    for (asid = 0, probe = MST_MAX_ASIDS - 1; probe >= 0; asid++, probe--) {
        /*
         * Check if this ASID is free (bit not set in bitmap).
         * The bitmap uses big-endian bit ordering.
         */
        uint16_t byte_offset = (uint16_t)(((MST_MAX_ASIDS - 1) | 0x0f) - asid) >> 3;
        if ((MST_$ASID_LIST[byte_offset] & (1 << (asid & 7))) == 0) {
            /* Found a free ASID - mark it as allocated */
            MST_$SET(MST_$ASID_LIST, MST_MAX_ASIDS, asid);

            /* Get the segment table index for this ASID */
            table_index = MST_ASID_BASE[asid] * 2;

            /* Initialize the segment table page for this ASID */
            status = FUN_00e43f40(asid, 0, &MST[table_index / 2]);
            if (status != status_$ok) {
                goto done;
            }

            /*
             * Get pointer to the first MST entry for this ASID.
             * The entry is at MST_PAGE_TABLE_BASE + (page_index * 0x400)
             */
            mst_page = (uint8_t *)((uintptr_t)MST_PAGE_TABLE_BASE + (uint32_t)MST[table_index / 2] * 0x400);

            /*
             * Check if the entry is already set to OS_WIRED_$UID.
             * If not, initialize it.
             */
            if (*(uint32_t *)mst_page != OS_WIRED_$UID.high ||
                *(uint32_t *)(mst_page + 4) != OS_WIRED_$UID.low) {
                /*
                 * If there's already a non-zero UID, fail - this segment
                 * is already in use by something else.
                 */
                if (*(uint32_t *)mst_page != 0) {
                    status = status_$no_space_available;
                    goto done;
                }

                /* Initialize with OS_WIRED UID */
                *(uint32_t *)mst_page = OS_WIRED_$UID.high;
                *(uint32_t *)(mst_page + 4) = OS_WIRED_$UID.low;
                *(uint16_t *)(mst_page + 8) = 0;        /* area_id = 0 */
                *(uint16_t *)(mst_page + 10) &= 0x3e00; /* Clear flags except reserved bits */
            }

            status = status_$ok;
            goto done;
        }
    }

    /* No free ASID found */
    status = status_$no_asid_available;

done:
    ML_$UNLOCK(MST_LOCK_ASID);
    *status_ret = status;

    if (status != status_$ok) {
        return 0;
    }
    return asid;
}
