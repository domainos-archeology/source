/*
 * MST_$FREE_ASID - Free an Address Space ID and clean up its mappings
 *
 * This function performs the full cleanup of an ASID:
 * 1. Unmaps all private A segments for the ASID
 * 2. Unmaps all private B segments for the ASID
 * 3. Frees area tracking resources via AREA_$FREE_ASID
 * 4. Unwires MST pages used by the ASID
 * 5. Clears the ASID bit in the allocation bitmap
 *
 * If any unmap operation fails, the system crashes - this indicates
 * a serious memory management error that cannot be recovered from.
 */

#include "mst.h"
#include "misc/misc.h"

/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h via mst.h */
extern void AREA_$FREE_ASID(uint16_t asid);

/* External: NIL UID for unmap operations */
extern uid_$t UID_$NIL;

/*
 * MST_$FREE_ASID - Free an Address Space ID
 *
 * @param asid        The ASID to free
 * @param status_ret  Output: status code
 */
void MST_$FREE_ASID(uint16_t asid, status_$t *status_ret)
{
    uint16_t start_page;
    uint16_t end_page;

    /*
     * Unmap all private A segments for this ASID.
     * Private A covers virtual addresses 0 to (MST_$PRIVATE_A_SIZE << 15) - 1
     */
    MST_$UNMAP_PRIVI(1,                           /* mode = 1 */
                     &UID_$NIL,                   /* any UID */
                     0,                           /* start address */
                     (uint32_t)MST_$PRIVATE_A_SIZE << 15, /* length */
                     asid,
                     status_ret);

    if (*status_ret != status_$ok) {
        /* Fatal error - cannot continue */
        CRASH_SYSTEM(status_ret);
        return;
    }

    /*
     * Unmap all private B segments for this ASID.
     * Private B covers 8 segments (256KB) starting at MST_$SEG_PRIVATE_B
     */
    MST_$UNMAP_PRIVI(1,                           /* mode = 1 */
                     &UID_$NIL,                   /* any UID */
                     (uint32_t)MST_$SEG_PRIVATE_B << 15, /* start address */
                     0x40000,                     /* 256KB = 8 segments * 32KB */
                     asid,
                     status_ret);

    if (*status_ret != status_$ok) {
        /* Fatal error - cannot continue */
        CRASH_SYSTEM(status_ret);
        return;
    }

    /* Free area tracking resources for this ASID */
    AREA_$FREE_ASID(asid);

    /* Lock the ASID allocation lock */
    ML_$LOCK(MST_LOCK_ASID);

    /*
     * Unwire MST pages used by this ASID.
     * Calculate the range of pages from ASID base to ASID base + segments per ASID.
     */
    start_page = MST_ASID_BASE[asid];
    end_page = start_page + (MST_$SEG_TN >> 6) - 1;
    mst_$unwire_asid_pages(start_page, end_page);

    /* Clear the ASID bit in the allocation bitmap */
    MST_$SET_CLEAR(MST_$ASID_LIST, MST_MAX_ASIDS, asid);

    /* Unlock and return */
    ML_$UNLOCK(MST_LOCK_ASID);
}
