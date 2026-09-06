/*
 * PROC2_$UID_TO_PGROUP_INDEX - Convert process group UID to pgroup table index
 *
 * For synthetic UIDs (high byte = 0), extracts the UPGID from the UID
 * and looks it up in the pgroup table.
 *
 * For real process UIDs, looks up the process and returns its pgroup_table_idx.
 *
 * Parameters:
 *   pgroup_uid - UID to convert (either synthetic pgroup UID or real process UID)
 *
 * Returns:
 *   Pgroup table index (1-69), or 0 if not found
 *
 * Original address: 0x00e42272
 */

#include "proc2/proc2_internal.h"

/* PGROUP_FIND_BY_UPGID (0x00e42224) is defined in pgroup_find_by_upgid.c */

int16_t PROC2_$UID_TO_PGROUP_INDEX(uid_t *pgroup_uid)
{
    int16_t result = 0;
    uint8_t high_byte;
    status_$t status;
    int16_t proc_idx;

    /* Extract high byte of UID to determine type */
    high_byte = (pgroup_uid->high >> 24) & 0xFF;

    if (high_byte == 0) {
        /*
         * Synthetic pgroup UID: high byte is 0, UPGID stored in word 1
         * (lower 16 bits of high word)
         */
        uint16_t upgid = (uint16_t)(pgroup_uid->high & 0xFFFF);
        result = PGROUP_FIND_BY_UPGID(upgid);
    } else {
        /*
         * Real process UID: look up the process and get its pgroup_table_idx
         */
        proc_idx = PROC2_$FIND_INDEX(pgroup_uid, &status);
        if (status == status_$ok) {
            proc2_info_t *entry = P2_INFO_ENTRY(proc_idx);
            result = entry->pgroup_table_idx;
        }
    }

    return result;
}
