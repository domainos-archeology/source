/*
 * PROC2_$GET_EC - Get an EC2 handle for a process's signal-delivery eventcount
 *
 * Only key 0 is accepted.  Under the PROC2 lock the UID is looked up and
 * FIM_$DELIV_EC[entry->asid] is registered with EC2; the registration
 * result (0 when the lookup failed) and the lookup status are returned.
 *
 * Parameters:
 *   proc_uid   (0x08,A6) UID to look up
 *   key        (0x0C,A6) pointer to word: must be 0
 *   ec_ret     (0x10,A6) longword out: the EC2 registration
 *   status_ret (0x14,A6) A2: status out
 *
 * Original address: 0x00e400c2 (154 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 *   0x00E400D8  tst.w (A0) -> 0x19000B bad eventcount key
 *   0x00E400F2..0x00E40100  idx = PROC2_$FIND_INDEX(uid, &(-0x8,A6))
 *   0x00E40118..0x00E4012E  EC2_$REGISTER_EC1(0xE224C4 + asid*12, &(-0x8,A6)) -> A0
 *   0x00E4013A  clr.l D2 on the failure path
 *   0x00E4013C  ML_$UNLOCK(4); then *ec_ret = D2, *status_ret = (-0x8,A6)
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_EC(uid_t *proc_uid, int16_t *key, void **ec_ret,
                   status_$t *status_ret)
{
    int16_t proc_idx;         /* D2w */
    status_$t status;         /* (-0x8,A6) */
    void *registered;         /* D2 */

    if (*key != 0) {
        *status_ret = status_$proc2_bad_eventcount_key;
        return;
    }

    ML_$LOCK(PROC2_LOCK_ID);

    proc_idx = PROC2_$FIND_INDEX(proc_uid, &status);
    if (status == status_$ok) {
        /* (-0x4e,A0,D0) = entry+0x96 asid; pea FIM_$DELIV_EC + asid*12 */
        registered = EC2_$REGISTER_EC1(
            &FIM_$DELIV_EC[P2_INFO_ENTRY(proc_idx)->asid], &status);
    } else {
        registered = NULL;
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *ec_ret = registered;
    *status_ret = status;
}
