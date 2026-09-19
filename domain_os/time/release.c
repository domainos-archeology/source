/*
 * TIME_$RELEASE - Release the current address space's interval timers
 *
 * Called during process cleanup: pulls the real interval-timer element out
 * of TIME_$RTEQ and the virtual one out of the current process's VT queue,
 * and zeroes each element's EXPIRY (offsets 0x0C/0x10 of the element - not
 * its interval at 0x14/0x18; bead source-e4a2).
 *
 * Original address: 0x00e58b58, 170 bytes
 *
 *   00e58b60  pea (-0x4,A6)                          ; status
 *   00e58b64..00e58b7a  pea 0xE297F0 + as_id*0x1C    ; TIME_$ITIMER_DB[0][as_id]
 *   00e58b7e  move.l #0xe2a7a0,-(SP)                 ; TIME_$RTEQ
 *   00e58b84  jsr TIME_$Q_REMOVE_ELEM
 *   00e58ba4  clr.l (0xc,A0,D2w) / clr.w (0x10,A0,D2w)   ; real.expire = 0
 *   00e58bac  pea (-0x4,A6)
 *   00e58bb4  pea (0x658,A1)                         ; TIME_$ITIMER_DB[1][as_id]
 *   00e58bb8..00e58bcc  pea (-0xc,A2,D1w), A2 = 0xE2A4A0, D1 = PROC1_$CURRENT*12
 *                                                    ; &TIME_$VTQ[cur-1]
 *   00e58bd0  jsr TIME_$Q_REMOVE_ELEM                ; args reclaimed by unlk
 *   00e58bf0  clr.l (0x664,A0) / clr.w (0x668,A0)    ; virt.expire = 0
 *
 * The status written by either removal is never examined.
 */

#include "time/time_internal.h"

void TIME_$RELEASE(void)
{
    status_$t status;               /* A6-0x4 */
    time_queue_elem_t *real_entry;
    time_queue_elem_t *virt_entry;

    /* 0x00E58B60..0x00E58B8A */
    real_entry = time_$itimer_entry(0, PROC1_$AS_ID);
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, real_entry, &status);

    /* 0x00E58B8E..0x00E58BA8 */
    real_entry->expire_high = 0;
    real_entry->expire_low = 0;

    /* 0x00E58BAC..0x00E58BD0: the virtual half is real + 0x658 */
    virt_entry = time_$itimer_entry(1, PROC1_$AS_ID);
    TIME_$Q_REMOVE_ELEM(&TIME_$VTQ[PROC1_$CURRENT - 1], virt_entry, &status);

    /* 0x00E58BD6..0x00E58BF4 */
    virt_entry->expire_high = 0;
    virt_entry->expire_low = 0;
}
