/*
 * DISK_$GET_QBLKS - Get queue blocks for I/O
 *
 * Allocates queue blocks for disk I/O operations.
 * This is a wrapper that calls an internal allocation function.
 *
 * @param vol_idx  Volume index
 * @param count    Number of blocks to allocate
 * @param status   Output: Status code
 * @return Pointer to allocated blocks
 */

#include "disk.h"

/* Internal allocation function */
extern void *FUN_00e3be8a(int16_t vol_idx, int16_t mode, void *count, status_$t *status);

void *DISK_$GET_QBLKS(int16_t vol_idx, int16_t count, status_$t *status)
{
    return FUN_00e3be8a(vol_idx, 0, (void *)(uintptr_t)count, status);
}
