/*
 * VOLX_$GET_UIDS - Get volume UIDs by physical location
 *
 * Looks up a volume by its physical device location and returns
 * both the logical volume UID and root directory UID.
 *
 * Original address: 0x00E6B62C
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$GET_UIDS
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   lv_uid_ret  - Output: receives logical volume UID
 *   dir_uid_ret - Output: receives root directory UID
 *   status      - Output: status code
 *
 * Algorithm:
 *   1. Search VOLX table for matching device location via FIND_VOLX
 *   2. If found, return both UIDs from the entry
 *   3. If not found, return status_$volume_logical_vol_not_mounted
 */
void VOLX_$GET_UIDS(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *lv_uid_ret, uid_t *dir_uid_ret, status_$t *status)
{
    int16_t vol_idx;
    volx_entry_t *entry;

    vol_idx = FIND_VOLX(*dev, *bus, *ctlr, *lv_num);

    if (vol_idx == 0) {
        *status = status_$volume_logical_vol_not_mounted;
        return;
    }

    entry = &VOLX_$TABLE_BASE[vol_idx];

    /* Return the logical volume UID */
    *lv_uid_ret = entry->lv_uid;

    /* Return the directory UID */
    *dir_uid_ret = entry->dir_uid;

    *status = status_$ok;
}
