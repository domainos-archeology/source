/*
 * ACL_$PRIM_CREATE - Create a primitive ACL object
 *
 * Creates a new ACL object from the provided ACL data buffer. This is used
 * during file/directory creation to establish the initial ACL.
 *
 * The function:
 * 1. Gets ACL attributes from AST
 * 2. If remote, delegates to REM_FILE_$ACL_CREATE
 * 3. Creates a file with FILE_$PRIV_CREATE
 * 4. Maps the file and copies ACL data
 * 5. Makes the file immutable and purifies it
 *
 * Parameters:
 *   acl_data      - ACL data buffer
 *   data_len      - Pointer to length of ACL data
 *   dir_uid       - Directory UID for context
 *   type          - ACL type code
 *   file_uid_ret  - Output: created file UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47968
 */

#include "acl/acl_internal.h"
#include "ast/ast.h"
#include "file/file.h"
#include "mst/mst.h"
#include "rem_file/rem_file.h"   /* REM_FILE_$ACL_CREATE */

/* ACL magic value for validation */
#define ACL_MAGIC_VALUE 0xFEDCA983

/*
 * 0x00E47B74: longword 0, pushed by the `pea (0x38,PC)` at 0x00E47B3A as
 * AST_$PURIFY's segment-list argument.  Image bytes: 00 00 00 00.
 */
static const uint32_t acl_$prim_create_purify_segments = 0x00000000u;

void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      void *type, uid_t *file_uid_ret, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uid_t local_uid;
    status_$t local_status;
    /* A6-0x58: the 0x38-byte record AST_$GET_ACL_ATTRIBUTES fills */
    ast_$acl_attr_t acl_attr;
    uint8_t local_byte;
    /* A6-0x20: the 0x20-byte object-location record */
    file_$obj_loc_t loc_rec;
    void *mapped_addr;
    /* A6-0x68: MST_$MAPS' map-info output longword (0x00E47A78).  Written by
     * the callee and never read again. */
    uint32_t mst_map_info;
    /* A6-0x72: acl_$prim_create_internal's image-length output word
     * (0x00E47AB4).  Written by the callee and never read again. */
    int16_t internal_image_len;
    int16_t expected_len;
    int16_t num_entries;
    int i;
    uint8_t *acl_bytes = (uint8_t *)acl_data;
    uint32_t *mapped_words;

    /* Copy directory UID into the location record at +0x08 (0x00E4798C) */
    loc_rec.uid.high = dir_uid->high;
    loc_rec.uid.low = dir_uid->low;

    /* 0x00E47994: `bclr.b #0x6,(-0x3,A6)` = the record's flags byte at +0x1D */
    loc_rec.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Get ACL attributes from AST */
    AST_$GET_ACL_ATTRIBUTES(&loc_rec, 1, &acl_attr, &local_status);
    if (local_status != status_$ok) {
        *status_ret = local_status;
        return;
    }

    /*
     * 0x00E479C2 `btst.b #0x0,(-0x55,A6)` tests bit 0 of BYTE 3 of the
     * record's first longword; 0x00E479CA `tst.b (-0x3,A6)` / bpl tests the
     * location record's flags byte for "remote".
     */
    if ((acl_attr.obj_flags[ACL_ATTR_FLAGS_LO] & ACL_ATTR_FLAG_LOCAL) == 0 &&
        loc_rec.flags < 0) {
        /* Remote creation */
        /* 0x00E479D0-0x00E479E4.  D4 is ACL_$PRIM_CREATE's own A6+0x14
         * argument, handed straight through as REM_FILE_$ACL_CREATE's
         * acl_header pointer; 0x00E479DC pea's the location record + 0x10. */
        REM_FILE_$ACL_CREATE(&loc_rec.loc_info, acl_data,
                             type, dir_uid, file_uid_ret,
                             status_ret);
        return;
    }

    /* Enter superuser mode temporarily */
    ACL_$UNWIRED_DATA.super_count[pid]++;

    /* Calculate expected buffer size: 0x34 + num_entries * 0x20 */
    num_entries = *(int16_t *)((uint8_t *)acl_data + 0x0E);
    expected_len = 0x34 + num_entries * 0x20;

    if (*data_len != expected_len) {
        *status_ret = status_$image_buffer_too_small;
        goto cleanup;
    }

    /* Check for non-NIL subsys UID and clear subsys flag bits if present */
    if (*(uint32_t *)((uint8_t *)acl_data + 0x12) != UID_$NIL.high ||
        *(uint32_t *)((uint8_t *)acl_data + 0x16) != UID_$NIL.low) {
        for (i = 0; i < num_entries; i++) {
            /* Clear bit 1 of flag byte at offset 0x4F within each 0x20 entry */
            acl_bytes[0x4F + i * 0x20] &= 0xFD;
        }
    }

    /* Create the ACL file */
    FILE_$PRIV_CREATE(3, &UID_$NIL, dir_uid, file_uid_ret, 0, 0, 0, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    /* Map the file into memory */
    /* 0x00E47A8C `st -(SP)` and 0x00E47A7C `st -(SP)` push arguments 2 and 8
     * as Pascal BOOLEAN bytes (0xFF == true); the word 0xFF6A this call used
     * to pass for argument 2 was never what the callee reads. */
    mapped_addr = MST_$MAPS(PROC1_$AS_ID, true, file_uid_ret, 0, 0x400, 0x16, 0, true,
                            &mst_map_info, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    if (acl_attr.obj_flags[ACL_ATTR_OBJ_TYPE] == 0) {   /* 0x00E47AAA */
        /* Call internal creation helper */
        acl_$prim_create_internal(type, acl_data, *data_len,
                                  (uint8_t *)acl_data + 2, 0, mapped_addr,
                                  &internal_image_len, status_ret);
    } else {
        /* Direct copy of ACL data */
        int16_t word_count = *data_len;
        uint32_t *src = (uint32_t *)acl_data;

        if (word_count < 0) {
            word_count += 3;
        }
        word_count = (word_count >> 2) - 1;

        mapped_words = (uint32_t *)mapped_addr;
        for (i = 0; i <= word_count; i++) {
            mapped_words[i] = src[i];
        }

        /* Write magic value at offset 0x3F8 */
        mapped_words[0xFE] = ACL_MAGIC_VALUE;
    }

    /* Unmap the file */
    /* 0x00E47B06 `pea (0x400).w` pushes the constant 0x400, and D3 holds
     * the mapped start VA returned in A0 by MST_$MAPS. */
    MST_$UNMAP_PRIVI(1, file_uid_ret, ARCH_PTR_TO_VA(mapped_addr), 0x400,
                     PROC1_$AS_ID, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    /* Make the file immutable */
    FILE_$MK_IMMUTABLE(file_uid_ret, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    /* Purify the AST entry */
    /* 0x00E47B34-0x00E47B4C.  `move.l #0x20000` fills flags = 2 and
     * segment = 0; `pea (0x38,PC)` is the zero longword at 0x00E47B74. */
    (void)AST_$PURIFY(file_uid_ret, 2, 0,
                      (uint32_t *)&acl_$prim_create_purify_segments,
                      0, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    goto cleanup;

cleanup_error:
    /* Set high bit of status to indicate error during cleanup */
    *status_ret |= 0x80000000;

cleanup:
    /* Exit superuser mode */
    ACL_$UNWIRED_DATA.super_count[pid]--;
}
