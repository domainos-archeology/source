/*
 * DISK_$RTN_QBLKS - Return queue blocks
 *
 * Returns previously allocated queue blocks to the free pool.
 * This is a wrapper that calls an internal deallocation function.
 *
 * @param blocks  Pointer to blocks to return
 */

#include "disk/disk_internal.h"

void DISK_$RTN_QBLKS(void *blocks)
{
    /* Note: The signature in header may need adjustment.
     * The decompiled version takes 3 parameters.
     * Using vol_idx=0 and param_3=0 as defaults.
     */
    FUN_00e3c01a(0, blocks, NULL);
}
