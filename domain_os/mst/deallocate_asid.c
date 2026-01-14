/*
 * MST_$DEALLOCATE_ASID - Deallocate an Address Space ID
 *
 * This is a wrapper function that calls MST_$FREE_ASID to perform
 * the actual deallocation. The separation allows for different
 * calling conventions or future expansion.
 */

#include "mst.h"

/*
 * MST_$DEALLOCATE_ASID - Deallocate an Address Space ID
 *
 * @param asid        The ASID to deallocate
 * @param status_ret  Output: status code
 */
void MST_$DEALLOCATE_ASID(uint16_t asid, status_$t *status_ret)
{
    MST_$FREE_ASID(asid, status_ret);
}
