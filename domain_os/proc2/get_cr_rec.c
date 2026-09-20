/*
 * PROC2_$GET_CR_REC - Get creation record UIDs from an EC2 handle
 *
 * Resolves the caller's EC2 handle to its level-1 eventcount, derives the
 * process table index from that eventcount's position in PROC2_$EC, and
 * returns the entry's parent UID and process UID when the entry is bound
 * (flags 0x0100) or a zombie (0x2000).
 *
 * Parameters:
 *   ec_handle  (0x08,A6) longword EC2 handle, copied to (-0x8,A6)
 *   parent_uid (0x0C,A6) A3: out, entry+0x08
 *   proc_uid   (0x10,A6) A4: out, entry+0x00
 *   status_ret (0x14,A6) A2: status out
 *
 * Original address: 0x00e4015c (142 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 *   0x00E40172..0x00E40184  EC2_$GET_EC1_ADDR(&(-0x8,A6), &(-0x4,A6)) -> A0
 *   0x00E40186..0x00E40192  idx = (A0 - 0xE2B978) / 0x18 + 1   (divs.w)
 *   0x00E40194              status tested only AFTER the index arithmetic
 *   0x00E401AA..0x00E401B8  flags bit 8 or bit 13
 *   0x00E401C2..0x00E401DE  copies, then clr.l (A2)
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_CR_REC(uint32_t *ec_handle, uid_t *parent_uid, uid_t *proc_uid,
                       status_$t *status_ret)
{
    uint32_t handle;            /* (-0x8,A6): the 4 bytes EC2 reads */
    status_$t status;           /* (-0x4,A6) */
    ec_$eventcount_t *ec1;      /* A0 */
    int16_t proc_idx;           /* D0w */
    proc2_info_t *entry;

    handle = *ec_handle;
    ec1 = EC2_$GET_EC1_ADDR((ec2_$eventcount_t *)&handle, &status);

    /* (A0 - PROC2_$EC) / sizeof(proc2_ec_entry_t) + 1; the stride is 0x18
     * on the target (asserted in proc2_internal.h).  Computed before the
     * status test exactly as the image does. */
    proc_idx = (int16_t)(((uintptr_t)ec1 - (uintptr_t)PROC2_$EC) /
                         sizeof(proc2_ec_entry_t)) + 1;

    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(proc_idx);
        if ((entry->flags & 0x0100) != 0 ||
            (entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
            /* 0x00E401C2..0x00E401DA */
            *parent_uid = entry->parent_uid;
            *proc_uid = entry->uid;
            *status_ret = status_$ok;
            return;
        }
    }

    /* 0x00E401BA */
    *status_ret = status_$proc2_uid_not_found;
}
