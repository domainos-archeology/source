/*
 * PROC2_$PGUID_TO_UPGID - Convert process group UID to UPGID
 *
 * Converts a process group UID to its Unix Process Group ID.
 * For synthetic UIDs (first byte = 0), extracts UPGID from high word.
 * For real process UIDs, looks up the process and returns parent's UPGID.
 *
 * Parameters:
 *   pgroup_uid - Pointer to process group UID
 *   upgid_ret - Pointer to receive UPGID
 *   status_ret - Status return (always status_$ok)
 *
 * Original address: 0x00e41072
 */

#include "proc2.h"

/* Internal helper: get UPGID from UID
 * Original address: 0x00e422cc
 */
static uint16_t PROC2_$UID_TO_UPGID_INTERNAL(uid_$t *pgroup_uid)
{
    uint16_t upgid;
    int16_t index;
    int16_t parent_idx;
    proc2_info_t *entry;
    status_$t status;
    uint8_t first_byte;

    upgid = 0;

    /* Check first byte of UID */
    first_byte = (pgroup_uid->high >> 24) & 0xFF;

    if (first_byte == 0) {
        /* Synthetic UID - extract UPGID from high word */
        upgid = pgroup_uid->high & 0xFFFF;
    } else {
        /* Real process UID - look up in table */
        index = PROC2_$FIND_INDEX(pgroup_uid, &status);

        if (status == status_$ok) {
            entry = P2_INFO_ENTRY(index);
            parent_idx = entry->parent_idx;

            if (parent_idx != 0) {
                /* Look up in parent UPID table */
                upgid = P2_PARENT_UPID(parent_idx);
            }
        }
    }

    return upgid;
}

void PROC2_$PGUID_TO_UPGID(uid_$t *pgroup_uid, uint16_t *upgid_ret, status_$t *status_ret)
{
    uint16_t upgid;

    ML_$LOCK(PROC2_LOCK_ID);

    upgid = PROC2_$UID_TO_UPGID_INTERNAL(pgroup_uid);

    ML_$UNLOCK(PROC2_LOCK_ID);

    *upgid_ret = upgid;
    *status_ret = status_$ok;
}
