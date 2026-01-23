/*
 * ACL_$COPY - Copy ACL from source to destination
 *
 * Copies ACL protection from a source object to a destination object,
 * handling various ACL types (file, directory, inherit, merge, subsys).
 *
 * Parameters:
 *   source_acl_uid - Source ACL UID (or UID_$NIL for default)
 *   dest_uid       - Destination file/directory UID
 *   source_type    - Source ACL type UID
 *   dest_type      - Destination ACL type UID
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4930A
 */

#include "acl/acl_internal.h"
#include "ast/ast.h"
#include "dir/dir.h"
#include "file/file.h"

/* ACL data buffer structure used internally */
typedef struct acl_buffer_t {
    uid_t owner_uid;       /* 0x00: Owner UID */
    uint8_t acl_data[44];  /* 0x08: ACL data */
} acl_buffer_t;

void ACL_$COPY(uid_t *source_acl_uid, uid_t *dest_uid, uid_t *source_type,
               uid_t *dest_type, status_$t *status_ret)
{
    int16_t prot_type = 5;
    uid_t owner_uid;
    uint8_t acl_data[44];
    uint8_t acl_attr_buf[8];
    uid_t local_uid;
    uid_t temp_uid;
    uint8_t saved_sids[36];
    status_$t local_status;

    /* Check if source is UID_$NIL - use default ACL */
    if (source_acl_uid->high == UID_$NIL.high &&
        source_acl_uid->low == UID_$NIL.low) {
        ACL_$DEF_ACLDATA(acl_data, &owner_uid);
    }
    /* Check if source type is FILEIN or DIRIN - get from AST */
    else if ((source_type->high == ACL_$FILEIN_ACL.high &&
              source_type->low == ACL_$FILEIN_ACL.low) ||
             (source_type->high == ACL_$DIRIN_ACL.high &&
              source_type->low == ACL_$DIRIN_ACL.low)) {
        /* Copy source UID to local */
        local_uid.high = source_acl_uid->high;
        local_uid.low = source_acl_uid->low;

        /* Get ACL attributes from AST */
        AST_$GET_ACL_ATTRIBUTES(acl_attr_buf, 0x21, acl_data, &local_status);
        *status_ret = local_status;
        if (local_status != status_$ok) {
            return;
        }
    }
    /* Otherwise get default protection from directory */
    else {
        DIR_$GET_DEF_PROTECTION(source_acl_uid, source_type, acl_data,
                                &owner_uid, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Check for special owner UID (0x01000000 in low word indicates inherit) */
        temp_uid.high = UID_$NIL.high;
        temp_uid.low = UID_$NIL.low | 0x01000000;

        if (owner_uid.high == temp_uid.high && owner_uid.low == temp_uid.low) {
            /* Need to inherit from current SIDs */
            ACL_$GET_RE_SIDS(saved_sids, &temp_uid, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }

            /* Use the current SID as owner */
            ((uid_t *)acl_data)->high = temp_uid.high;
            ((uid_t *)acl_data)->low = temp_uid.low;

            /* Set default permissions */
            acl_data[32] = 0x0F;  /* Owner rights */
            acl_data[33] = 0x07;  /* Group rights */
            acl_data[35] = 0x07;  /* Other rights */

            owner_uid.high = UID_$NIL.high;
            owner_uid.low = UID_$NIL.low;

            /* Convert to 9-entry ACL */
            ACL_$CONVERT_TO_9ACL((int16_t)acl_data, &owner_uid, source_acl_uid,
                                 source_type, &owner_uid, status_ret);
            prot_type = 6;
        }
    }

    /* Now apply the ACL to destination based on dest_type */

    /* FILE_SUBS_ACL type */
    if (dest_type->high == ACL_$FILE_SUBS_ACL.high &&
        dest_type->low == ACL_$FILE_SUBS_ACL.low) {
        /* If source is also FILEIN, use type 4 */
        if (source_type->high == ACL_$FILEIN_ACL.high &&
            source_type->low == ACL_$FILEIN_ACL.low) {
            prot_type = 4;
        }
        FILE_$SET_PROT(dest_uid, &prot_type, acl_data, &owner_uid, status_ret);

        /* Handle old-style ACL if new style fails with specific error */
        if ((*status_ret & 0x7FFFFFFF) == 0x00230010) {
            FILE_$OLD_AP(dest_uid, &prot_type, acl_data, &owner_uid, status_ret);
        }
        return;
    }

    /* FILEIN_ACL type */
    if (dest_type->high == ACL_$FILEIN_ACL.high &&
        dest_type->low == ACL_$FILEIN_ACL.low) {
        if (source_type->high == ACL_$FILEIN_ACL.high &&
            source_type->low == ACL_$FILEIN_ACL.low) {
            prot_type = 4;
        }
        FILE_$SET_PROT(dest_uid, &prot_type, acl_data, &owner_uid, status_ret);
        return;
    }

    /* FILE_MERGE_ACL type - same as FILEIN */
    if (dest_type->high == ACL_$FILE_MERGE_ACL.high &&
        dest_type->low == ACL_$FILE_MERGE_ACL.low) {
        FILE_$SET_PROT(dest_uid, &prot_type, acl_data, &owner_uid, status_ret);
        return;
    }

    /* DIRIN_ACL type */
    if (dest_type->high == ACL_$DIRIN_ACL.high &&
        dest_type->low == ACL_$DIRIN_ACL.low) {
        if (source_type->high == ACL_$DIRIN_ACL.high &&
            source_type->low == ACL_$DIRIN_ACL.low) {
            prot_type = 4;
        }
        DIR_$SET_PROTECTION(dest_uid, acl_data, &owner_uid, &prot_type, status_ret);
        return;
    }

    /* DIR_MERGE_ACL type - same as DIRIN */
    if (dest_type->high == ACL_$DIR_MERGE_ACL.high &&
        dest_type->low == ACL_$DIR_MERGE_ACL.low) {
        DIR_$SET_PROTECTION(dest_uid, acl_data, &owner_uid, &prot_type, status_ret);
        return;
    }

    /* Default: set as default protection */
    DIR_$SET_DEF_PROTECTION(dest_uid, dest_type, acl_data, &owner_uid, status_ret);
}
