/*
 * PROC2_$SET_SERVER - Set or clear a process's server flag
 *
 * Re-emitted from the image (0x00E41468..0x00E414DA, 116 bytes).
 *
 * Frame (link.w A6,-0x8; A5 = 0xE7BE84):
 *   (0x8,A6)  proc_uid (pushed by value)   (0xC,A6) server_flag byte ptr -> D2b
 *   (0x10,A6) status_ret <- A6-0x4
 *
 *   00e4149e  move.b D2b,D1b / lsr.b #0x7,D1b    ; D1 = flag bit 7 (0 or 1)
 *   00e414b2  andi.b #-0x3,(-0xba,A1)            ; HIGH byte of +0x2A &= ~0x02
 *   00e414b8  add.b D1b,D1b                      ; 0 or 2
 *   00e414ba  or.b D1b,(-0xba,A1)                ; HIGH byte |= it
 *
 * i.e. flags bit 0x0200 (PROC2_FLAG_SERVER) := bit 7 of *server_flag.
 *
 * Only reference: the SVC table entry at 0x00E7B872.
 *
 * Original address: 0x00e41468
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_SERVER(uid_t *proc_uid, int8_t *server_flag, status_$t *status_ret)
{
    int8_t flag_val;             /* D2b */
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A1 (biased) */
    status_$t status;            /* A6-0x4 */

    /* 0x00E41476-0x00E41488: the byte is read before the lock */
    flag_val = *server_flag;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4148A-0x00E41496 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E41498: tst.l (-0x4,A6) / bne */
    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);
        /* 0x00E4149E-0x00E414BA */
        entry->flags &= (uint16_t)~PROC2_FLAG_SERVER;
        entry->flags |= (uint16_t)((((uint8_t)flag_val >> 7) & 1) << 9);
    }

    /* 0x00E414BE-0x00E414CE */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
