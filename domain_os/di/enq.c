/*
 * DI_$ENQ - Enqueue a deferred-interrupt element
 *
 * Re-emitted from the image (0x00E209A8..0x00E209D4, 46 bytes).  Written in
 * assembler (no link frame, arguments read straight off SP) but with the
 * ordinary stack calling convention, so it is emitted as C:
 *
 *   00e209a8  movea.l (0xc,SP),A0          ; elem                 (arg 3)
 *   00e209ac  tst.b (0xc,A0) / beq         ; elem->enqueued
 *   00e209b2  pea (0x440,PC)               ; &0x00E20DF4 = 0x000A000C
 *   00e209b6  bra.w 0x00e20b5a             ; -> jsr CRASH_SYSTEM ; bra.b 0x00e20b54
 *   00e209ba  move.l (-0x3ba,PC),(A0)      ; elem->next = DI_$Q_HEAD (0xE20602)
 *   00e209be  move.l A0,(0x00e20602).l     ; DI_$Q_HEAD = elem
 *   00e209c4  move.l (0x4,SP),(0x4,A0)     ; elem->arg1 = arg 1
 *   00e209ca  move.l (0x8,SP),(0x8,A0)     ; elem->arg2 = arg 2
 *   00e209d0  st (0xc,A0)                  ; elem->enqueued = TRUE
 *
 * The double-enqueue arm jumps to the shared crash stub at 0x00E20B5A; the
 * `bra.b` after its `jsr CRASH_SYSTEM` lands in PROC1_$SET_LOCK's body, so
 * DI_$ENQ never resumes and the element is not queued.  Callers:
 * 0x00E2B226 and 0x00E2B25E (TIME).
 *
 * Original address: 0x00e209a8
 */

#include "di/di_internal.h"
#include "misc/crash_system.h"

/* Head of the deferred-interrupt queue, 0xE20602 (SAU2 map: DI_$Q_HEAD). */
di_queue_elem_t *DI_$Q_HEAD = NULL;

/* The status cell at 0x00E20DF4 (bytes 00 0a 00 0c). */
static const status_$t di_$enq_crash_status_00e20df4 =
    status_$proc1_bad_deferred_interrupt_queue;

void DI_$ENQ(uint32_t arg1, uint32_t arg2, di_queue_elem_t *elem)
{
    /* 0x00E209AC: tst.b (0xc,A0) / beq.b 0x00E209BA */
    if (elem->enqueued != 0) {
        /* 0x00E209B2-0x00E209B6: crash and do not come back here */
        CRASH_SYSTEM(&di_$enq_crash_status_00e20df4);
        return;
    }

    /* 0x00E209BA-0x00E209D0 */
    elem->next = DI_$Q_HEAD;
    DI_$Q_HEAD = elem;
    elem->arg1 = arg1;
    elem->arg2 = arg2;
    elem->enqueued = 0xFF;
}
