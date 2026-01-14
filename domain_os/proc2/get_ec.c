/*
 * PROC2_$GET_EC - Get process eventcount
 *
 * Returns a registered eventcount for the specified process.
 * The key parameter must be 0 (the only valid eventcount key).
 *
 * Parameters:
 *   proc_uid  - UID of target process
 *   ec_ret    - Pointer to receive eventcount
 *   status_ret - Pointer to receive status
 *
 * The returned eventcount is from the FIM delivery EC table,
 * registered through EC2_$REGISTER_EC1.
 *
 * Original address: 0x00e400c2
 */

#include "proc2.h"
#include "ec/ec.h"

/* External EC tables and functions */
#if defined(M68K)
    #define FIM_DELIV_EC_BASE   0xE224C4
    #define FIM_DELIV_EC(asid)  ((ec_$eventcount_t*)(FIM_DELIV_EC_BASE + (asid) * 12))
#else
    extern ec_$eventcount_t *fim_deliv_ec_table;
    #define FIM_DELIV_EC(asid)  (&fim_deliv_ec_table[(asid)])
#endif

void PROC2_$GET_EC(uid_$t *proc_uid, int16_t *key, void **ec_ret, status_$t *status_ret)
{
    int16_t proc_idx;
    proc2_info_t *entry;
    ec_$eventcount_t *ec;
    void *registered_ec;
    status_$t status;

    /* Only key 0 is valid */
    if (*key != 0) {
        *status_ret = status_$proc2_bad_eventcount_key;
        return;
    }

    ML_$LOCK(PROC2_LOCK_ID);

    /* Find the process */
    proc_idx = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == status_$ok) {
        /* Get the process entry */
        entry = P2_INFO_ENTRY(proc_idx);

        /* Get EC from FIM delivery table using process's ASID */
        ec = FIM_DELIV_EC(entry->asid);

        /* Register the EC for external use */
        registered_ec = EC2_$REGISTER_EC1(ec, &status);
    } else {
        registered_ec = NULL;
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *ec_ret = registered_ec;
    *status_ret = status;
}
