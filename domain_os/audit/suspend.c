/*
 * suspend.c - AUDIT_$SUSPEND
 *
 * Suspends auditing for the current process by incrementing
 * its suspension counter.
 *
 * Original address: 0x00E70DB6
 *
 * Original assembly:
 *   00e70db6    link.w A6,0x0
 *   00e70dba    move.w (0x00e20608).l,D0w    ; D0 = PROC1_$CURRENT
 *   00e70dc0    lea (0xe854d8).l,A0          ; A0 = &AUDIT_$DATA
 *   00e70dc6    add.w D0w,D0w                ; D0 = pid * 2
 *   00e70dc8    move.w (-0x2,A0,D0w*0x1),D1w ; D1 = suspend_count[pid]
 *   00e70dcc    addq.w #0x1,D1w              ; D1++
 *   00e70dce    move.w D1w,(-0x2,A0,D0w*0x1) ; suspend_count[pid] = D1
 *   00e70dd2    unlk A6
 *   00e70dd4    rts
 */

#include "audit/audit_internal.h"

void AUDIT_$SUSPEND(void)
{
    int16_t pid = PROC1_$CURRENT;

    AUDIT_$DATA.suspend_count[pid]++;
}
