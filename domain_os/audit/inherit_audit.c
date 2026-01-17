/*
 * inherit_audit.c - AUDIT_$INHERIT_AUDIT
 *
 * Copies the audit suspension counter from the current process
 * to a child process during process creation.
 *
 * Original address: 0x00E7169C
 *
 * Original assembly:
 *   00e7169c    link.w A6,0x0
 *   00e716a0    pea (A5)
 *   00e716a2    lea (0xe854d8).l,A5          ; A5 = &AUDIT_$DATA
 *   00e716a8    move.w (0x00e20608).l,D0w    ; D0 = PROC1_$CURRENT
 *   00e716ae    movea.l (0x8,A6),A0          ; A0 = child_pid pointer
 *   00e716b2    add.w D0w,D0w                ; D0 = parent_pid * 2
 *   00e716b4    move.w (A0),D1w              ; D1 = *child_pid
 *   00e716b6    add.w D1w,D1w                ; D1 = child_pid * 2
 *   00e716b8    move.w (-0x2,A5,D0w*0x1),(-0x2,A5,D1w*0x1)
 *                                            ; suspend_count[child] = suspend_count[parent]
 *   00e716be    movea.l (0xc,A6),A1          ; A1 = status_ret pointer
 *   00e716c2    clr.l (A1)                   ; *status_ret = 0
 *   00e716c4    movea.l (-0x4,A6),A5
 *   00e716c8    unlk A6
 *   00e716ca    rts
 */

#include "audit/audit_internal.h"

void AUDIT_$INHERIT_AUDIT(int16_t *child_pid, status_$t *status_ret)
{
    int16_t parent_pid = PROC1_$CURRENT;
    int16_t child = *child_pid;

    /* Copy parent's suspension counter to child */
    AUDIT_$DATA.suspend_count[child] = AUDIT_$DATA.suspend_count[parent_pid];

    *status_ret = status_$ok;
}
