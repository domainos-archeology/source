/*
 * TIME_$ADVANCE_CALLBACK - Queue callback for TIME_$ADVANCE
 *
 * Fired by TIME_$Q_SCAN_QUEUE when an element entered by TIME_$ADVANCE
 * expires.  It is entered through the scan's DIRECT path (the element's
 * flags word is 0, so neither DXM bit is set): the single argument is the
 * address of a longword holding the ELEMENT's address (see
 * time_$callback_arg_t in time/time.h), and the eventcount to advance is
 * that element's callback_arg (offset 0x08), where TIME_$ADVANCE stored it.
 *
 * Original address: 0x00e16434, 32 bytes
 *
 *   00e16434  link.w A6,-0x4
 *   00e16438  pea (A2)
 *   00e1643a  movea.l (0x8,A6),A0      ; arg
 *   00e1643e  movea.l (A0),A1          ; *arg = the time_queue_elem_t
 *   00e16440  movea.l (0x8,A1),A2      ; elem->callback_arg = the eventcount
 *   00e16444  pea (A2)
 *   00e16446  jsr EC_$ADVANCE_WITHOUT_DISPATCH (0x00e20718)
 *   00e1644c  movea.l (-0x8,A6),A2
 *   00e16450  unlk A6
 *   00e16452  rts
 *
 * The prototype in time/time.h keeps `void *arg` (it is only ever passed as
 * an untyped callback pointer); the body reads it as time_$callback_arg_t.
 */

#include "time/time_internal.h"

void TIME_$ADVANCE_CALLBACK(void *arg)
{
    time_$callback_arg_t cell = (time_$callback_arg_t)arg;
    time_queue_elem_t *elem;
    ec_$eventcount_t *ec;

    /* 0x00E1643E: movea.l (A0),A1 */
    elem = (time_queue_elem_t *)*cell;

    /* 0x00E16440: movea.l (0x8,A1),A2 - the callback_arg is a 32-bit VA */
    ec = (ec_$eventcount_t *)ARCH_VA_TO_PTR(elem->callback_arg);

    /* 0x00E16444..0x00E16446 */
    EC_$ADVANCE_WITHOUT_DISPATCH(ec);
}
