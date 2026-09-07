/*
 * TIME_$Q_ADD_CALLBACK - Add a callback to the time queue
 *
 * Populates a queue element with callback information and enters
 * it into the specified time queue.
 *
 * Parameters:
 *   queue        - Queue to add to
 *   when         - Expiry, either absolute or relative to *now
 *   is_absolute  - If 0, *now is added to *when to form the expiry
 *   now          - Reference time; also passed on to TIME_$Q_ENTER_ELEM
 *   callback     - Callback function pointer
 *   callback_arg - Argument for callback
 *   flags        - Element flags
 *   interval     - Repeat interval (for periodic callbacks)
 *   qelem        - Queue element structure to populate
 *   status       - Status return
 *
 * Original address: 0x00e16dd4
 *
 * Assembly:
 *   00e16ddc  move.w (0x10,A6),D0w      ; is_absolute
 *   00e16de0  movea.l (0x12,A6),A3      ; now
 *   00e16de4  move.l (0x16,A6),D3       ; callback
 *   00e16de8  move.l (0x1a,A6),D4       ; callback_arg
 *   00e16dec  move.w (0x1e,A6),D2w      ; flags
 *   00e16df0  movea.l (0x24,A6),A2      ; qelem
 *   00e16df4  movea.l (0xc,A6),A0       ; when
 *   00e16df8  move.l (A0),(0xc,A2)      ; qelem->expire = *when
 *   00e16dfc  move.w (0x4,A0),(0x10,A2)
 *   00e16e02  tst.w D0w / bne 0x00e16e14
 *   00e16e06  pea (A3) / pea (0xc,A2) / jsr ADD48   ; expire += *now
 *   00e16e14  move.l D3,(0x4,A2)        ; qelem->callback
 *   00e16e18  move.l D4,(0x8,A2)        ; qelem->callback_arg
 *   00e16e1c  move.w D2w,(0x12,A2)      ; qelem->flags
 *   00e16e20  movea.l (0x20,A6),A0      ; interval
 *   00e16e24  move.l (A0),(0x14,A2) / move.w (0x4,A0),(0x18,A2)
 *   00e16e2e  push status / qelem / A3 (now) / queue
 *   00e16e3a  bsr TIME_$Q_ENTER_ELEM
 */

#include "time/time_internal.h"

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status)
{
    /* 0xE16DF8: copy the expiration time from 'when' */
    qelem->expire_high = when->high;
    qelem->expire_low = when->low;

    /* 0xE16E02: a zero flag means 'when' is relative to 'now' */
    if (is_absolute == 0) {
        ADD48((clock_t *)&qelem->expire_high, now);
    }

    /* Set callback info */
    qelem->callback = (uint32_t)(uintptr_t)callback;
    qelem->callback_arg = (uint32_t)(uintptr_t)callback_arg;
    qelem->flags = flags;

    /* Set repeat interval */
    qelem->interval_high = interval->high;
    qelem->interval_low = interval->low;

    /* 0xE16E3A: 'now' is the reference time handed on, not 'when' */
    TIME_$Q_ENTER_ELEM(queue, now, qelem, status);
}
