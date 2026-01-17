/*
 * is_process_audited.c - AUDIT_$IS_PROCESS_AUDITED
 *
 * Checks if the current process is being audited.
 * Returns true if the suspension counter is zero.
 *
 * Original address: 0x00E70D94
 *
 * Original assembly:
 *   00e70d94    link.w A6,-0x4
 *   00e70d98    pea (A5)
 *   00e70d9a    lea (0xe854d8).l,A5          ; A5 = &AUDIT_$DATA
 *   00e70da0    move.w (0x00e20608).l,D0w    ; D0 = PROC1_$CURRENT
 *   00e70da6    add.w D0w,D0w                ; D0 = pid * 2
 *   00e70da8    tst.w (-0x2,A5,D0w*0x1)      ; test suspend_count[pid]
 *   00e70dac    seq D0b                       ; D0 = (count == 0) ? 0xFF : 0
 *   00e70dae    movea.l (-0x8,A6),A5
 *   00e70db2    unlk A6
 *   00e70db4    rts
 */

#include "audit/audit_internal.h"

int8_t AUDIT_$IS_PROCESS_AUDITED(void)
{
    int16_t pid = PROC1_$CURRENT;

    /* Return true (0xFF) if suspension count is zero */
    return (AUDIT_$DATA.suspend_count[pid] == 0) ? (int8_t)-1 : 0;
}
