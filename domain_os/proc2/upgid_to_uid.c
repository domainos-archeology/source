/*
 * PROC2_$UPGID_TO_UID - Convert UPGID to UID
 *
 * Converts a Unix Process Group ID to a UID by combining with UID_NIL.
 * The UPGID is stored in the low 16 bits of the UID's high word.
 *
 * Parameters:
 *   upgid - Pointer to UPGID value
 *   uid_ret - Pointer to receive process group UID
 *   status_ret - Status return (always status_$ok)
 *
 * Original address: 0x00e4100c
 */

#include "proc2/proc2_internal.h"

/* UID_NIL at 0xe1737c - used as base for synthetic UIDs */
#if defined(ARCH_M68K)
#define UID_NIL (*(uid_t*)0xE1737C)
#else
#define UID_NIL uid_nil
#endif

/* Internal helper: create UID from UPGID
 * Original address: 0x00e4232a
 */
static void PROC2_$UPGID_TO_UID_INTERNAL(uid_t *uid_ret, uint16_t upgid)
{
    uid_t result;

    /* Start with UID_NIL */
    result.high = UID_NIL.high;
    result.low = UID_NIL.low;

    /* Replace low 16 bits of high word with upgid */
    result.high = (result.high & 0xFFFF0000) | upgid;

    uid_ret->high = result.high;
    uid_ret->low = result.low;
}

void PROC2_$UPGID_TO_UID(uint16_t *upgid, uid_t *uid_ret, status_$t *status_ret)
{
    uint16_t upgid_val;
    uid_t result;

    upgid_val = *upgid;

    ML_$LOCK(PROC2_LOCK_ID);

    PROC2_$UPGID_TO_UID_INTERNAL(&result, upgid_val);

    ML_$UNLOCK(PROC2_LOCK_ID);

    uid_ret->high = result.high;
    uid_ret->low = result.low;
    *status_ret = status_$ok;
}
