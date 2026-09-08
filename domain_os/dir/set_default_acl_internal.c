/*
 * dir_$set_default_acl_internal - Set default ACL on directory page
 *
 * Performs ACL conversion and storage on a directory's page data.
 * Called by dir_$do_op_set_default_acl after opening the directory.
 *
 * Converts the incoming ACL to internal 10-ACL or funky-ACL format,
 * validates that the ACL object resides on the same volume as the
 * directory, then copies the 44-byte ACL data (11 uint32_t values)
 * into the directory page at the appropriate offset:
 *   - ACL_$DIR_ACL type:  data at 0x1A, UID at 0x46
 *   - ACL_$FILE_ACL type: data at 0x4E, UID at 0x7A
 *
 * If the ACL UID changed, sets attribute 6 on the new ACL object.
 * If flush_flag is negative, flushes the page via FILE_$FW_PARTIAL
 * (tst.b D2 / bpl at 0x00E52F34).
 * If the old ACL UID was non-nil, truncates the old ACL object.
 *
 * Parameters:
 *   handle      - Open directory handle (from dir_$open_dir)
 *   acl_type    - Pointer to the ACL type UID, ACL_$DIR_ACL (0x00E1744C,
 *                 compared at 0x00E52E64) or ACL_$FILE_ACL (0x00E17444,
 *                 compared at 0x00E52EB4).  Arrives in D5.
 *   src_acl_uid - Pointer to the source ACL object UID.  Arrives in A2;
 *                 its "funky" bits are tested at 0x00E52DA0
 *                 (and.w (0x4,A2),D0w) to pick ACL_$CONVERT_FUNKY_ACL
 *                 over ACL_$CONVERT_TO_10ACL.
 *   flush_flag  - If negative (bit 7 set), flush the page with
 *                 FILE_$FW_PARTIAL (0x00E52F34)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E52D70
 * Original size: 566 bytes
 */

#include "dir/dir_internal.h"

/* DIR_$ONE_PAGE_L - 0x00000400 constant used as FILE_$FW_PARTIAL byte count */

void dir_$set_default_acl_internal(uint32_t handle, void *acl_type,
                                   void *src_acl_uid, char flush_flag,
                                   status_$t *status_ret)
{
    uint32_t *type_ptr = (uint32_t *)acl_type;
    uint8_t *page_data;
    uid_t acl_uid;
    uint32_t acl_data[12];
    uint8_t extra_buf[8];
    uid_t old_acl_uid;
    status_$t local_status;
    uint16_t attr_val;
    uint8_t trunc_buf[4];
    uint8_t acl_high_byte;

    /*
     * AST location descriptor struct (30+ bytes)
     * Same layout as in do_op_drop_dir:
     *   +0x00: location result (2 bytes)
     *   +0x02: vol_id (2 bytes, int16_t)
     *   +0x08: uid.high (4 bytes, input)
     *   +0x0C: uid.low (4 bytes, input)
     *   +0x1D: flags byte (bit 6 cleared before call)
     */
    file_$obj_loc_t loc_desc;   /* the 0x20-byte AST location record */
    uint32_t get_loc_buf1;
    uint32_t get_loc_buf2;

    *status_ret = status_$ok;

    /* Map page 0 */
    page_data = (uint8_t *)dir_$map_page((void *)handle, 0);

    /* Check ACL format and convert appropriately */
    {
        uint16_t format_field = *(uint16_t *)((uint8_t *)src_acl_uid + 4);
        uint16_t format_check = ((format_field & 0xFF0) >> 4) & 0xE0;

        if (format_check == 0) {
            /* Standard format - convert to 10ACL */
            ACL_$CONVERT_TO_10ACL(src_acl_uid, (void *)(uintptr_t)handle,
                                  &acl_uid, acl_data, status_ret);
            if (*status_ret != status_$ok) {
                goto audit;
            }
        } else {
            /* Non-standard ("funky") format */
            ACL_$CONVERT_FUNKY_ACL(src_acl_uid, acl_data, &acl_uid,
                                   extra_buf, status_ret);
            if (*status_ret != status_$ok) {
                goto audit;
            }
            /* Clear bit 0 of acl_uid.low (byte 0 of low word on big-endian) */
            acl_uid.low &= 0xFEFFFFFF;
        }
    }

    /* Save high byte for later checks */
    acl_high_byte = (uint8_t)(acl_uid.high >> 24);

    /* If ACL UID is non-nil, verify it's on the same volume */
    if (acl_high_byte != 0) {
        /* Set up location descriptor with ACL UID */
        loc_desc.uid.high = acl_uid.high;
        loc_desc.uid.low = acl_uid.low;
        /* Clear bit 6 of flags byte at offset 0x1D */
        loc_desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

        AST_$GET_LOCATION(&loc_desc, 1, &get_loc_buf1,
                          &get_loc_buf2, &local_status);

        if (local_status != status_$ok ||
            *(int16_t *)((char *)(uintptr_t)handle + 0x3A) !=
                (int16_t)loc_desc.volume ||
            loc_desc.flags < 0) {
            *status_ret = local_status;
            if (*status_ret == file_$object_not_found ||
                *status_ret == status_$ok) {
                *status_ret = file_$objects_on_different_volumes;
            }
            goto audit;
        }
    }

    /* Determine which ACL slot to update based on type */
    if (type_ptr[0] == ACL_$DIR_ACL.high &&
        type_ptr[1] == ACL_$DIR_ACL.low) {

        /* DIR ACL - save old UID, copy data to offset 0x1A */
        old_acl_uid.high = *(uint32_t *)(page_data + 0x46);
        old_acl_uid.low = *(uint32_t *)(page_data + 0x4A);

        /* Copy 44 bytes (11 uint32_t) of ACL data to page+0x1A */
        {
            uint32_t *dst = (uint32_t *)(page_data + 0x1A);
            uint32_t *src = acl_data;
            int16_t n;
            for (n = 10; n >= 0; n--) {
                *dst++ = *src++;
            }
        }

        /* If UID unchanged, we're done */
        if (old_acl_uid.high == acl_uid.high &&
            old_acl_uid.low == acl_uid.low) {
            goto audit;
        }

        /* Update ACL UID at page+0x46 */
        *(uint32_t *)(page_data + 0x46) = acl_uid.high;
        *(uint32_t *)(page_data + 0x4A) = acl_uid.low;

    } else if (type_ptr[0] == ACL_$FILE_ACL.high &&
               type_ptr[1] == ACL_$FILE_ACL.low) {

        /* FILE ACL - save old UID, copy data to offset 0x4E */
        old_acl_uid.high = *(uint32_t *)(page_data + 0x7A);
        old_acl_uid.low = *(uint32_t *)(page_data + 0x7E);

        /* Copy 44 bytes (11 uint32_t) of ACL data to page+0x4E */
        {
            uint32_t *dst = (uint32_t *)(page_data + 0x4E);
            uint32_t *src = acl_data;
            int16_t n;
            for (n = 10; n >= 0; n--) {
                *dst++ = *src++;
            }
        }

        /* If UID unchanged, we're done */
        if (old_acl_uid.high == acl_uid.high &&
            old_acl_uid.low == acl_uid.low) {
            goto audit;
        }

        /* Update ACL UID at page+0x7A */
        *(uint32_t *)(page_data + 0x7A) = acl_uid.high;
        *(uint32_t *)(page_data + 0x7E) = acl_uid.low;

    } else {
        /* Unknown ACL type */
        *status_ret = status_$naming_bad_type;
        goto audit;
    }

    /* If new ACL UID is non-nil, set attribute 6 on it */
    if (acl_high_byte != 0) {
        attr_val = 1;
        AST_$SET_ATTRIBUTE(&acl_uid, 6, &attr_val, status_ret);
        if (*status_ret != status_$ok) {
            goto audit;
        }
    }

    /* If flush_flag is negative, flush the page (0x00E52F34) */
    if ((int8_t)flush_flag < 0) {
        FILE_$FW_PARTIAL((uid_t *)(uintptr_t)handle,
                         (uint32_t *)&DIR_$CONST_ZERO_L,
                         (int32_t *)&DIR_$ONE_PAGE_L /* const; only read by callee */, status_ret);
        if (*status_ret != status_$ok) {
            goto audit;
        }
    }

    /* If old ACL UID was non-nil, truncate the old ACL object */
    if ((uint8_t)(old_acl_uid.high >> 24) != 0) {
        AST_$TRUNCATE(&old_acl_uid, 0, 3, trunc_buf, &local_status);
    }

audit:
    /* Audit logging if enabled */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        audit_$log_prot_op(*status_ret, (uid_t *)(uintptr_t)handle,
                           acl_data, acl_type, &acl_uid, 4);
    }
}
