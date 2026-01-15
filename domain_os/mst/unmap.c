/*
 * MST_$UNMAP - Unmap a memory region from the current process
 *
 * This function unmaps a region of virtual memory from the current
 * process's address space. It is a wrapper around MST_$UNMAP_PRIVI
 * with mode=2 and the current process's ASID.
 */

#include "mst_internal.h"

/*
 * MST_$UNMAP - Unmap memory from current process
 *
 * @param uid           Object UID to match (or NIL for any)
 * @param start_va_ptr  Pointer to starting virtual address
 * @param length_ptr    Pointer to length to unmap
 * @param status_ret    Output: status code
 */
void MST_$UNMAP(uid_t *uid,
                uint32_t *start_va_ptr,
                uint32_t *length_ptr,
                status_$t *status_ret)
{
    MST_$UNMAP_PRIVI(2,            /* mode = 2 */
                     uid,
                     *start_va_ptr,
                     *length_ptr,
                     PROC1_$AS_ID, /* Current process ASID */
                     status_ret);
}
