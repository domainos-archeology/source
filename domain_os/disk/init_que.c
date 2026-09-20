/*
 * DISK_$INIT_QUE - Initialise a driver's elevator queue
 *
 * 0x00E3C598 - 0x00E3C5D8 (66 bytes).  Builds the disk_$que_t described in
 * disk/disk.h: both list heads point at their own sentinel, the direction
 * bit is set and the cylinder bits of `position` are cleared.  Rewritten
 * through the record on 2026-09-19 so the sentinel's cylinder word is
 * stored in the high half of a longword on every host.
 *
 * Argument: (0x8,A6) queue -> disk_$que_t.
 */

#include "disk/disk_internal.h"

void DISK_$INIT_QUE(void *queue)
{
    disk_$que_t *q = (disk_$que_t *)queue;

    /* 0x00E3C598 - 0x00E3C5D0 */
    q->list_a = ARCH_PTR_TO_VA(&q->sentinel_a);         /* +0x08 -> +0x10 */
    q->list_b = ARCH_PTR_TO_VA(&q->sentinel_b);         /* +0x0c -> +0x18 */
    q->position |= DISK_QUE_DIRECTION_BIT;              /* bset.b #7,(0x4,A0) */
    q->sentinel_a.next = 0;
    q->sentinel_a.daddr = (q->sentinel_a.daddr & 0x0000FFFFu) | DISK_QUE_SENTINEL_CYL;
    q->sentinel_b.next = 0;
    q->sentinel_b.daddr = (q->sentinel_b.daddr & 0x0000FFFFu) | DISK_QUE_SENTINEL_CYL;
    q->position &= 0xfff0000fu;                         /* andi.l #0xfff0000f */
    q->current = 0;
}
