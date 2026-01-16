/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 *
 * Original address: 0x00E60D36
 * Size: 122 bytes
 *
 * Releases all locks held on files within a specific volume.
 * Iterates through all locks using FILE_$READ_LOCK_ENTRYI and
 * unlocks each one using FILE_$PRIV_UNLOCK.
 *
 * Parameters:
 *   vol_uid    - UID of volume to unlock
 *   status_ret - Output status code
 *
 * Assembly analysis:
 *   - Uses iteration index starting at 1
 *   - Calls FILE_$READ_LOCK_ENTRYI to get next lock
 *   - Calls FILE_$PRIV_UNLOCK for each lock found
 *   - Loops until status != status_$ok
 *   - Maps status_$file_obj_not_locked_by_this_process to status_$ok
 */

#include "file/file_internal.h"

/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 */
void FILE_$UNLOCK_VOL(uid_t *vol_uid, status_$t *status_ret)
{
    uint16_t iter_index;
    uid_t local_uid;
    status_$t local_status;
    uint32_t dtv_out[2];

    /* Lock entry info buffer from FILE_$READ_LOCK_ENTRYI */
    struct {
        uint32_t    context;        /* 0x00 */
        uint32_t    node_low;       /* 0x04 */
        uint32_t    node_high;      /* 0x08 */
        uint32_t    uid_high;       /* 0x0C */
        uint32_t    uid_low;        /* 0x10 */
        uint16_t    next;           /* 0x14 */
        uint16_t    sequence;       /* 0x16 */
        uint8_t     refcount;       /* 0x18 */
        uint8_t     flags1;         /* 0x19 */
        uint8_t     rights;         /* 0x1A */
        uint8_t     flags2;         /* 0x1B */
    } lock_info;

    /* Copy volume UID to local for modification */
    local_uid.high = vol_uid->high;
    local_uid.low = vol_uid->low;

    /* Start iteration at index 1 */
    iter_index = 1;

    /*
     * Loop through all locks on this volume
     */
    while (1) {
        /* Get next lock entry */
        FILE_$READ_LOCK_ENTRYI(&local_uid, &iter_index, &lock_info, status_ret);

        if (*status_ret != status_$ok) {
            break;
        }

        /*
         * Unlock this entry
         * Combine mode=0 (from lock_info.flags2 bits 3-6 shifted left is the mode,
         * but we pass the raw sequence/flags), with ASID from flags
         *
         * From assembly:
         *   move.w (-0x14,A6),-(SP)   ; lock_info.sequence (offset 0x16 from base -0x28)
         *   st -(SP)                   ; remote_flags = -1 (0xFF)
         *   move.l (-0x1c,A6),-(SP)   ; lock_info.context (offset 0x00)
         *   move.l (-0x20,A6),-(SP)   ; lock_info.node_low (offset 0x04)
         */
        FILE_$PRIV_UNLOCK(&lock_info.uid_high,  /* Use UID from lock info */
                          0,                     /* lock_index = 0 (search) */
                          (uint32_t)lock_info.sequence << 16,  /* mode_asid: mode=sequence?, asid=0 */
                          -1,                    /* remote_flags = -1 (remote unlock) */
                          lock_info.context,     /* param_5 = context */
                          lock_info.node_low,    /* param_6 = node address */
                          dtv_out,               /* dtv_out */
                          &local_status);
    }

    /*
     * Map not-locked status to success
     * status_$file_obj_not_locked_by_this_process = 0x0F000C
     */
    if (*status_ret == status_$file_obj_not_locked_by_this_process) {
        *status_ret = status_$ok;
    }
}
