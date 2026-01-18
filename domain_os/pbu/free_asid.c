/*
 * PBU_$FREE_ASID - Free an Address Space ID
 *
 * On systems without PBU hardware, this is a no-op stub.
 *
 * Reverse engineered from Domain/OS at address 0x00e590f8
 */

#include "pbu/pbu.h"

/*
 * PBU_$FREE_ASID
 *
 * This function would normally release an Address Space ID (ASID)
 * back to the PBU hardware for reuse. On systems without PBU
 * hardware, this is simply a no-op.
 */
void PBU_$FREE_ASID(void)
{
    /* No-op on systems without PBU hardware */
    return;
}
