/*
 * DISK_$GET_QBLKS - Get queue blocks for disk I/O
 *
 * Allocates queue blocks for disk I/O operations. This is a
 * wrapper that calls disk_$get_qblks_internal with a 0 second argument.
 *
 * @param count      Number of queue blocks to allocate
 * @param qblk_head  Output: pointer to head of queue block list
 * @param qblk_tail  Output: pointer to tail of queue block list
 *
 * Original address: 0x00E3BFF4
 */

#include "disk/disk_internal.h"

void DISK_$GET_QBLKS(int16_t count, int32_t *qblk_head, uint32_t *qblk_tail)
{
    /* disk_$get_qblks_internal signature in disk_internal.h uses void* for parameters.
     * Cast to match the declared signature. */
    disk_$get_qblks_internal(count, 0, (void *)qblk_head, (status_$t *)qblk_tail);
}
