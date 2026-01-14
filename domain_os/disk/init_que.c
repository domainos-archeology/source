/*
 * DISK_$INIT_QUE - Initialize a disk I/O queue
 *
 * Initializes a queue structure for disk I/O operations.
 * The queue structure contains:
 *   +0x00: Head pointer (long)
 *   +0x04: Flags (long)
 *   +0x08: First list pointer
 *   +0x0c: Second list pointer
 *   +0x10: List 1 entry (8 bytes)
 *   +0x18: List 2 entry (8 bytes)
 *
 * @param queue  Queue structure to initialize
 */

#include "disk.h"

void DISK_$INIT_QUE(void *queue)
{
    uint32_t *q = (uint32_t *)queue;

    /* Set up list pointers to embedded list entries */
    q[2] = (uint32_t)(uintptr_t)(q + 4);  /* +0x08 -> +0x10 */
    q[3] = (uint32_t)(uintptr_t)(q + 6);  /* +0x0c -> +0x18 */

    /* Set high bit of flags */
    *(uint8_t *)((uint8_t *)queue + 4) |= 0x80;

    /* Initialize first list entry */
    q[4] = 0;                              /* +0x10: next ptr = NULL */
    *(uint16_t *)((uint8_t *)queue + 0x14) = 0xffff;  /* +0x14: marker */

    /* Initialize second list entry */
    q[6] = 0;                              /* +0x18: next ptr = NULL */
    *(uint16_t *)((uint8_t *)queue + 0x1c) = 0xffff;  /* +0x1c: marker */

    /* Clear other flags, preserve high nibble */
    q[1] &= 0xfff0000f;

    /* Clear head pointer */
    q[0] = 0;
}
