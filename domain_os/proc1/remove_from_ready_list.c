/*
 * proc1_$remove_from_ready_list - Unlink a PCB from the ready list
 * Original address: 0x00e206d6 (24 bytes)
 *
 * The body of PROC1_$REMOVE_READY (0x00E206D2).  Register convention:
 * A1 = pcb; `bsr'd from PROC1_$EC_WAITN (0x00E2068C), proc1_$reorder_if_needed
 * (0x00E207F8, 0x00E20812) and proc1_$clr_lock_body (0x00E20EC8).
 *
 * 0x00E206D6  D0 = (A1)                      pcb->nextp
 * 0x00E206D8  D1 = (0x4,A1)                  pcb->prevp
 * 0x00E206DC  A0 = D1; (A0) = D0             prev->nextp = next
 * 0x00E206E0  A0 = D0; (0x4,A0) = D1         next->prevp = prev
 * 0x00E206E6  subq.w #1,(0x00e1ebd0).l       PROC1_$READY_COUNT--
 * 0x00E206EC  rts
 *
 * The PCB's own links are left pointing at its old neighbours.  On m68k
 * this is proc1/sau2/ready_list.s; this C body is for other targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void proc1_$remove_from_ready_list(proc1_t *pcb)
{
    proc1_t *next = pcb->nextp;         /* D0 */
    proc1_t *prev = pcb->prevp;         /* D1 */

    /* 0x00E206DC..0x00E206E2 */
    prev->nextp = next;
    next->prevp = prev;

    /* 0x00E206E6 */
    PROC1_$READY_COUNT--;
}

#endif /* !ARCH_M68K */
