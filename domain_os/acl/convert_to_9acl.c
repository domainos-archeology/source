/*
 * ACL_$CONVERT_TO_9ACL - Convert ACL to 9-entry format
 *
 * Converts an ACL to the 9-entry ACL format used by Domain/OS.
 *
 * Parameters:
 *   type          - ACL type code
 *   source_uid    - Source UID to convert (can be UID_$NIL)
 *   dir_uid       - Directory UID for context
 *   default_prot  - Default protection to apply if source is NIL
 *   result_uid    - Output: converted ACL UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E48CE8
 */

#include "acl/acl_internal.h"

/* Forward declaration for internal image helper at 0xe47b78 */
void acl_$image_internal(void *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, void *len_out, void *data_out,
                         void *flag_out, status_$t *status);

/* ACL workspace buffer (A5-relative at offset 0) */
extern uint8_t ACL_$WORKSPACE[64];  /* 0xE7CF54 */

void ACL_$CONVERT_TO_9ACL(int16_t type, uid_t *source_uid, uid_t *dir_uid,
                          void *default_prot, uid_t *result_uid, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t len_buf[4];
    uint8_t data_buf[48];
    int8_t flag_buf[4];

    /* Enter superuser mode temporarily */
    ACL_$SUPER_COUNT[pid]++;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&ACL_$EXCLUSION_LOCK);

    /* Set locksmith owner and override flag */
    ACL_$LOCKSMITH_OWNER_PID = PROC1_$CURRENT;
    ACL_$LOCKSMITH_OVERRIDE = -1;  /* 0xFF */

    /* Acquire lock #10 */
    ML_$LOCK(10);

    /* Get image of source UID into workspace */
    acl_$image_internal(source_uid, 0x400, -1,  /* 0xFF */
                        ACL_$WORKSPACE, len_buf, data_buf, flag_buf, status_ret);

    /* Release lock #10 */
    ML_$UNLOCK(10);

    if (*status_ret == status_$ok) {
        /* Check if source is UID_$NIL */
        if (source_uid->high == UID_$NIL.high &&
            source_uid->low == UID_$NIL.low) {
            /* Copy default protection to workspace offsets */
            uint32_t *def_prot = (uint32_t *)default_prot;
            uint32_t *workspace = (uint32_t *)ACL_$WORKSPACE;

            /* Copy to offset 0x02 (user SID) */
            workspace[0] = def_prot[0];  /* offset 0x02 */
            workspace[1] = def_prot[1];  /* offset 0x06 */

            /* Copy to offset 0x1A */
            workspace[6] = def_prot[0];  /* offset 0x1A */
            workspace[7] = def_prot[1];  /* offset 0x1E */
        }

        /* Check flag_buf to determine how to proceed */
        if (flag_buf[0] < 0) {
            /* Flag indicates existing ACL can be reused */
            result_uid->high = source_uid->high;
            result_uid->low = source_uid->low;
            /* Clear bit 0 of low word */
            result_uid->low &= ~0x01000000;
        } else {
            /* Need to create new ACL */
            result_uid->high = UID_$NIL.high;
            result_uid->low = UID_$NIL.low;

            /* Create primitive ACL */
            ACL_$PRIM_CREATE(ACL_$WORKSPACE, len_buf, dir_uid, type,
                            result_uid, status_ret);
        }
    }

    /* Clear locksmith override */
    ACL_$LOCKSMITH_OVERRIDE = 0;

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&ACL_$EXCLUSION_LOCK);

    /* Exit superuser mode */
    ACL_$SUPER_COUNT[pid]--;
}
