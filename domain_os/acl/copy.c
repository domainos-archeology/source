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

void ACL_$COPY(uid_t *source_acl_uid, uid_t *dest_uid, uid_t *source_type,
               uid_t *dest_type, status_$t *status_ret)
{
    int16_t prot_type = 5;      /* A6-0xB6 */

    /*
     * A6-0xB0: the 0x38-byte record AST_$GET_ACL_ATTRIBUTES fills.  Its
     * default_acl field is A6-0xAC and its acl_data block A6-0xA4 - the two
     * cells every other call in this function passes around, so they are
     * fields of one record here, not separate locals.
     */
    ast_$acl_attr_t acl_attr;

    file_$obj_loc_t loc_rec;    /* A6-0x78: object-location record */
    uid_t temp_uid;             /* A6-0x58 */
    uint8_t current_sids[36];   /* A6-0x50: ACL_$GET_RE_SIDS output 2 */
    uint8_t saved_sids[36];     /* A6-0x28: ACL_$GET_RE_SIDS output 1 */
    status_$t local_status;     /* A6-0xB4 */

    /* Check if source is UID_$NIL - use default ACL */
    if (source_acl_uid->high == UID_$NIL.high &&
        source_acl_uid->low == UID_$NIL.low) {
        ACL_$DEF_ACLDATA(acl_attr.acl_data, &acl_attr.default_acl);
    }
    /* Check if source type is FILEIN or DIRIN - get from AST */
    else if ((source_type->high == ACL_$FILEIN_ACL.high &&
              source_type->low == ACL_$FILEIN_ACL.low) ||
             (source_type->high == ACL_$DIRIN_ACL.high &&
              source_type->low == ACL_$DIRIN_ACL.low)) {
        /* Seed the location record: UID at +0x08, clear bit 6 of +0x1D */
        loc_rec.uid.high = source_acl_uid->high;
        loc_rec.uid.low = source_acl_uid->low;
        loc_rec.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

        /* Get ACL attributes from AST */
        AST_$GET_ACL_ATTRIBUTES(&loc_rec, 0x21, &acl_attr, &local_status);
        *status_ret = local_status;
        if (local_status != status_$ok) {
            return;
        }
    }
    /* Otherwise get default protection from directory */
    else {
        DIR_$GET_DEF_PROTECTION(source_acl_uid, source_type,
                                acl_attr.acl_data, &acl_attr.default_acl,
                                status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Check for special owner UID (0x01000000 in low word indicates inherit) */
        temp_uid.high = UID_$NIL.high;
        temp_uid.low = UID_$NIL.low | 0x01000000;

        if (acl_attr.default_acl.high == temp_uid.high &&
            acl_attr.default_acl.low == temp_uid.low) {
            /*
             * 0x00E493F6: ACL_$GET_RE_SIDS writes 36 bytes into EACH of its
             * two output buffers - A6-0x28 and A6-0x50 - and A6-0x50 is a
             * cell distinct from temp_uid at A6-0x58.
             */
            ACL_$GET_RE_SIDS(saved_sids, current_sids, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }

            /* 0x00E4940E: 4 longwords, i.e. 16 bytes, not 8 */
            {
                int i;
                for (i = 0; i < 16; i++) {
                    acl_attr.acl_data[i] = current_sids[i];
                }
            }

            /* 0x00E4941E: bytes 0x18/0x19/0x1B of the ACL data block */
            acl_attr.acl_data[0x18] = 0x0F;
            acl_attr.acl_data[0x19] = 0x07;
            acl_attr.acl_data[0x1B] = acl_attr.acl_data[0x19];

            acl_attr.default_acl.high = UID_$NIL.high;
            acl_attr.default_acl.low = UID_$NIL.low;

            /* Convert to 9-entry ACL (0x00E4944C pea's the acl_data block) */
            ACL_$CONVERT_TO_9ACL(acl_attr.acl_data, &acl_attr.default_acl,
                                 source_acl_uid, source_type,
                                 &acl_attr.default_acl, status_ret);
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
        FILE_$SET_PROT(dest_uid, (uint16_t *)&prot_type, acl_attr.acl_data,
                       &acl_attr.default_acl, status_ret);

        /* Handle old-style ACL if new style fails with specific error */
        if ((*status_ret & 0x7FFFFFFF) == 0x00230010) {
            FILE_$OLD_AP(dest_uid, (uint16_t *)&prot_type,
                         acl_attr.acl_data, &acl_attr.default_acl,
                         status_ret);
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
        FILE_$SET_PROT(dest_uid, (uint16_t *)&prot_type, acl_attr.acl_data,
                       &acl_attr.default_acl, status_ret);
        return;
    }

    /* FILE_MERGE_ACL type - same as FILEIN */
    if (dest_type->high == ACL_$FILE_MERGE_ACL.high &&
        dest_type->low == ACL_$FILE_MERGE_ACL.low) {
        FILE_$SET_PROT(dest_uid, (uint16_t *)&prot_type, acl_attr.acl_data,
                       &acl_attr.default_acl, status_ret);
        return;
    }

    /* DIRIN_ACL type */
    if (dest_type->high == ACL_$DIRIN_ACL.high &&
        dest_type->low == ACL_$DIRIN_ACL.low) {
        if (source_type->high == ACL_$DIRIN_ACL.high &&
            source_type->low == ACL_$DIRIN_ACL.low) {
            prot_type = 4;
        }
        DIR_$SET_PROTECTION(dest_uid, acl_attr.acl_data,
                            &acl_attr.default_acl, &prot_type, status_ret);
        return;
    }

    /* DIR_MERGE_ACL type - same as DIRIN */
    if (dest_type->high == ACL_$DIR_MERGE_ACL.high &&
        dest_type->low == ACL_$DIR_MERGE_ACL.low) {
        DIR_$SET_PROTECTION(dest_uid, acl_attr.acl_data,
                            &acl_attr.default_acl, &prot_type, status_ret);
        return;
    }

    /* Default: set as default protection */
    DIR_$SET_DEF_PROTECTION(dest_uid, dest_type, acl_attr.acl_data, &acl_attr.default_acl,
                       status_ret);
}
