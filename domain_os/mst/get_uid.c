/*
 * MST_$GET_UID, MST_$GET_UID_ASID, MST_$GET_VA_INFO - Query segment information
 *
 * These functions retrieve information about memory mappings:
 * - MST_$GET_UID: Get UID for an address in current process
 * - MST_$GET_UID_ASID: Get UID for an address in specified ASID
 * - MST_$GET_VA_INFO: Get full segment information
 */

#include "mst.h"

/* ML_$LOCK, ML_$UNLOCK declared in ml/ml.h via mst.h */

/* External process ASID */
extern int16_t PROC1_$AS_ID;

/* Internal lookup function */
extern void FUN_00e4411c(uint16_t asid, uint32_t va, void *param,
                         void **entry_out, status_$t *status);

/*
 * MST_$GET_VA_INFO - Get full information about a virtual address
 *
 * @param asid_p        Pointer to ASID to query
 * @param va_ptr        Pointer to virtual address
 * @param uid_out       Output: UID of mapped object
 * @param adjusted_va   Output: Adjusted virtual address
 * @param param_5       Parameter passed to internal lookup
 * @param active_flag   Output: Segment active flag
 * @param modified_flag Output: Segment modified flag
 * @param status_ret    Output: status code
 */
void MST_$GET_VA_INFO(uint16_t *asid_p,
                      uint32_t *va_ptr,
                      uid_$t *uid_out,
                      uint32_t *adjusted_va,
                      void *param_5,
                      int8_t *active_flag,
                      int8_t *modified_flag,
                      status_$t *status_ret)
{
    uint16_t asid;
    uint32_t va;
    void *entry;
    status_$t status;
    uint32_t entry_copy[4];  /* Copy of 16-byte MST entry */

    asid = *asid_p;
    va = *va_ptr;

    /* Validate ASID */
    if (asid >= MST_MAX_ASIDS) {
        *status_ret = status_$reference_to_illegal_address;
        return;
    }

    /* Lock and look up the entry */
    ML_$LOCK(MST_LOCK_ASID);
    FUN_00e4411c(asid, va, param_5, &entry, &status);

    if (status == status_$ok) {
        /* Copy entry data while locked */
        uint32_t *src = (uint32_t *)entry;
        entry_copy[0] = src[0];  /* UID high */
        entry_copy[1] = src[1];  /* UID low */
        entry_copy[2] = src[2];  /* area_id, flags */
        entry_copy[3] = src[3];  /* page_info, reserved */
    }

    ML_$UNLOCK(MST_LOCK_ASID);

    *status_ret = status;
    if (status != status_$ok) {
        return;
    }

    /* Copy UID */
    uid_out->high = entry_copy[0];
    uid_out->low = entry_copy[1];

    /*
     * Calculate adjusted virtual address.
     * Combines segment base (from entry word 2, high 16 bits) with
     * offset within segment (low 15 bits of original VA).
     */
    uint16_t seg_base = (uint16_t)(entry_copy[2] >> 16);
    *adjusted_va = ((uint32_t)seg_base << 15) + (va & 0x7fff);

    /*
     * Extract flags from entry word 2, low 16 bits.
     * Bit 15 = active, Bit 14 = modified
     */
    uint16_t flags = (uint16_t)entry_copy[2];
    *active_flag = (flags & 0x8000) ? -1 : 0;
    *modified_flag = (flags & 0x4000) ? -1 : 0;
}

/*
 * MST_$GET_UID - Get UID for address in current process
 *
 * Simple wrapper around MST_$GET_VA_INFO using current process ASID.
 *
 * @param va_ptr      Pointer to virtual address
 * @param uid_out     Output: UID of mapped object
 * @param adjusted_va Output: Adjusted virtual address
 * @param status_ret  Output: status code
 */
void MST_$GET_UID(uint32_t *va_ptr,
                  uid_$t *uid_out,
                  uint32_t *adjusted_va,
                  status_$t *status_ret)
{
    int8_t active_flag;
    int8_t modified_flag;
    int8_t unused;

    MST_$GET_VA_INFO((uint16_t *)&PROC1_$AS_ID,
                     va_ptr,
                     uid_out,
                     adjusted_va,
                     &unused,
                     &active_flag,
                     &modified_flag,
                     status_ret);
}

/*
 * MST_$GET_UID_ASID - Get UID for address in specified ASID
 *
 * @param asid_p      Pointer to ASID to query
 * @param va_ptr      Pointer to virtual address
 * @param uid_out     Output: UID of mapped object
 * @param adjusted_va Output: Adjusted virtual address
 * @param status_ret  Output: status code
 */
void MST_$GET_UID_ASID(uint16_t *asid_p,
                       uint32_t *va_ptr,
                       uid_$t *uid_out,
                       uint32_t *adjusted_va,
                       status_$t *status_ret)
{
    uint16_t asid;
    int8_t active_flag;
    int8_t modified_flag;
    int8_t unused;

    /* Copy ASID to local (in case pointer points to static storage) */
    asid = *asid_p;

    MST_$GET_VA_INFO(&asid,
                     va_ptr,
                     uid_out,
                     adjusted_va,
                     &unused,
                     &active_flag,
                     &modified_flag,
                     status_ret);
}
