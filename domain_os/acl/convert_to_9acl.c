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

void ACL_$CONVERT_TO_9ACL(void *type, uid_t *source_uid, uid_t *dir_uid,
                          void *default_prot, uid_t *result_uid, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t len_buf[4];
    uint8_t data_buf[48];
    int8_t flag_buf[4];

    /* Enter superuser mode temporarily */
    ACL_$UNWIRED_DATA.super_count[pid]++;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&ACL_$WIRED_DATA.exclusion_lock);

    /* Set locksmith owner and override flag */
    ACL_$UNWIRED_DATA.locksmith_owner_pid = PROC1_$CURRENT;
    ACL_$UNWIRED_DATA.locksmith_override = -1;  /* 0xFF */

    /* Acquire lock #10 */
    ML_$LOCK(10);

    /* Get image of source UID into workspace */
    acl_$image_internal(source_uid, 0x400, -1,  /* 0xFF */
                        ACL_$UNWIRED_DATA.workspace, len_buf,
                        (acl_$prot_data_t *)(void *)data_buf, flag_buf, status_ret);

    /* Release lock #10 */
    ML_$UNLOCK(10);

    if (*status_ret == status_$ok) {
        /* Check if source is UID_$NIL */
        if (source_uid->high == UID_$NIL.high &&
            source_uid->low == UID_$NIL.low) {
            /*
             * 0x00E48D88-0x00E48D9A.  A0 is the workspace and A1 the caller's
             * default protection UID, reloaded between the two pairs:
             *   move.l (A1)+,(0x2,A0)   acl_uid.high
             *   move.l (A1)+,(0x6,A0)   acl_uid.low
             *   movea.l D3,A1
             *   move.l (A1)+,(0x1a,A0)  initial_acl_uid.high
             *   move.l (A1)+,(0x1e,A0)  initial_acl_uid.low
             * so both eight-byte UIDs of the image get the same value - at
             * +0x02 and +0x1A, not at +0x00 and +0x18.  (source-9j7d)
             */
            acl_$image_t *image = (acl_$image_t *)ACL_$UNWIRED_DATA.workspace;
            const uid_t *def_prot = (const uid_t *)default_prot;

            image->acl_uid = *def_prot;
            image->initial_acl_uid = *def_prot;
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
            ACL_$PRIM_CREATE(ACL_$UNWIRED_DATA.workspace, len_buf, dir_uid, type,
                            result_uid, status_ret);
        }
    }

    /* Clear locksmith override */
    ACL_$UNWIRED_DATA.locksmith_override = 0;

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&ACL_$WIRED_DATA.exclusion_lock);

    /* Exit superuser mode */
    ACL_$UNWIRED_DATA.super_count[pid]--;
}
