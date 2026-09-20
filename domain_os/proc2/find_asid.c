/*
 * PROC2_$FIND_ASID - Find the address space id of a process by UID
 *
 * Under the PROC2 lock, looks the UID up and returns entry+0x96 (asid), or
 * entry+0x98 (asid_alt) when *use_alt is true and the entry has flag
 * 0x0800.  When the lookup fails the returned word is whatever D2 held on
 * entry (the register is never initialised on that path).
 *
 * Parameters:
 *   proc_uid   (0x08,A6) UID to look up
 *   use_alt    (0x0C,A6) pointer to a Domain boolean (tst.b / bpl)
 *   status_ret (0x10,A6) status from PROC2_$FIND_INDEX
 *
 * Original address: 0x00e40724 (144 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 * A1 = 0xEA551C + idx*0xE4 = entry + 0xE4:
 *   (-0xBA,A1) = +0x2A flags   (-0x4C,A1) = +0x98 asid_alt   (-0x4E,A1) = +0x96 asid
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *use_alt,
                          status_$t *status_ret)
{
    int16_t index;            /* D1w */
    status_$t status;         /* (-0x4,A6) */
    uint16_t result = 0;      /* D2w: not written by the image on the error
                                 path; 0 stands in for the stale register */
    proc2_info_t *entry;

    /* 0x00E40732 ML_$LOCK(4) */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40740..0x00E4074E */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E40750 tst.l (-0x4,A6) */
    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);
        /* 0x00E40756..0x00E40776: *use_alt < 0 and flags bit 11 */
        if (*use_alt < 0 && (entry->flags & 0x0800) != 0) {
            result = entry->asid_alt;      /* 0x00E4077E */
        } else {
            result = entry->asid;          /* 0x00E40790 */
        }
    }

    /* 0x00E40794 ML_$UNLOCK(4); 0x00E407A0 *status_ret = status */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
    return result;
}
