/*
 * PROC2_$WHO_AM_I - Return the calling process's UID
 *
 * Re-emitted from the image (0x00E73862..0x00E738A6, 70 bytes) and
 * verified; the previous body was faithful.  A5 is loaded with 0xE86054
 * (PROC2_CREATE_DAT) and never used.
 *
 *   00e73870  move.w PROC1_$CURRENT,D0w ; add.w D0w,D0w
 *   00e73886  move.w (0x3eb6,A1),D0w    ; PROC2_$DATA.pid_to_index[PROC1_$CURRENT]
 *   00e7388c  muls.w #0xe4,D1 ; lea (-0xe4,A1),A1   ; entry base
 *   00e73898  move.l (A1)+,(A2) ; move.l (A1)+,(0x4,A2)
 *
 * No lock.  Only reference: the SVC table entry at 0x00E7B39E.
 *
 * Original address: 0x00e73862
 */

#include "proc2/proc2_internal.h"

void PROC2_$WHO_AM_I(uid_t *proc_uid)
{
    proc2_info_t *entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E73898-0x00E7389A */
    proc_uid->high = entry->uid.high;
    proc_uid->low = entry->uid.low;
}
