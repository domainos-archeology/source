/*
 * TIME_$Q_ADD_CALLBACK - Fill in a queue element and enter it
 *
 * Frame (0x00E16DDC): 0x08 queue, 0x0C when, 0x10 is_absolute (word, D0),
 * 0x12 now (A3), 0x16 callback (D3), 0x1A callback_arg (D4), 0x1E flags
 * (word, D2), 0x20 interval, 0x24 elem (A2), 0x28 status.
 *
 *   00e16df4  elem->expire = *when
 *   00e16e02  tst.w D0w / bne                  ; relative -> ADD48(&expire, now)
 *   00e16e14  elem->callback = D3; callback_arg = D4; flags = D2
 *   00e16e20  elem->interval = *interval
 *   00e16e2e  TIME_$Q_ENTER_ELEM(queue, now, elem, status)   ; `now`, not `when`
 *
 * No result slot, no cleanup: the frame is dropped by unlk.
 *
 * Original address: 0x00e16dd4, 116 bytes
 */

#include "time/time_internal.h"
#include "dxm/dxm.h"

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *elem, status_$t *status)
{
    /* 0x00E16DF8 */
    elem->expire_high = when->high;
    elem->expire_low = when->low;

    /* 0x00E16E02 */
    if (is_absolute == 0) {
        ADD48((clock_t *)&elem->expire_high, now);
    }

    /*
     * 0x00E16E14.  The callback field is a 4-byte code address in the image;
     * DXM_$CALLBACK_CELL is that cast on m68k and a registry handle on a
     * 64-bit host (see dxm/dxm.h), which is what lets TIME_$Q_SCAN_QUEUE call
     * through the field again with dxm_$callback_fn().
     */
    elem->callback = DXM_$CALLBACK_CELL(callback);
    elem->callback_arg = ARCH_PTR_TO_VA(callback_arg);
    elem->flags = flags;

    /* 0x00E16E20 */
    elem->interval_high = interval->high;
    elem->interval_low = interval->low;

    /* 0x00E16E3A */
    TIME_$Q_ENTER_ELEM(queue, now, elem, status);
}
