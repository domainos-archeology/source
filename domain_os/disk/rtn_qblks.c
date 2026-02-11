/*
 * DISK_$RTN_QBLKS - Return queue blocks
 *
 * Returns previously allocated queue blocks to the free pool.
 * This is a wrapper that calls an internal deallocation function.
 *
 * @param count      Number of queue blocks to return
 * @param qblk_head  Head of queue block list
 * @param qblk_tail  Tail of queue block list
 *
 * Original address: 0x00E3C0C4
 */

#include "disk/disk_internal.h"

void DISK_$RTN_QBLKS(int16_t count, int32_t qblk_head, uint32_t qblk_tail)
{
    /* disk_$rtn_qblks_internal is declared in disk_internal.h with void* parameters.
     * Cast to match the declared signature. */
    disk_$rtn_qblks_internal(count, (void *)(uintptr_t)qblk_head, (void *)(uintptr_t)qblk_tail);
}
