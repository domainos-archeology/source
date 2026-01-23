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

/* Forward declarations for external functions */
void REM_FILE_$ACL_CREATE(void *local_data, void *acl_data, int32_t type,
                          uid_t *dir_uid, uid_t *file_uid_ret, status_$t *status_ret);
void acl_$prim_create_internal(int32_t type, void *acl_data, int16_t data_len,
                               void *offset, int16_t flag, void *mapped_addr,
                               void *local_buf, status_$t *status_ret);

/* ACL magic value for validation */
#define ACL_MAGIC_VALUE 0xFEDCA983

void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      int16_t type, uid_t *file_uid_ret, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uid_t local_uid;
    status_$t local_status;
    uint8_t acl_attr_buf[8];
    char local_flags[4];
    uint8_t local_byte;
    uint8_t local_data[16];
    void *mapped_addr;
    int16_t expected_len;
    int16_t num_entries;
    int i;
    uint8_t *acl_bytes = (uint8_t *)acl_data;
    uint32_t *mapped_words;

    /* Copy directory UID to local */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;

    /* Clear bit 6 of local_data flag */
    local_data[13] &= 0xBF;

    /* Get ACL attributes from AST */
    AST_$GET_ACL_ATTRIBUTES(acl_attr_buf, 1, local_flags, &local_status);
    if (local_status != status_$ok) {
        *status_ret = local_status;
        return;
    }

    /* Check if remote operation needed */
    if ((local_flags[1] & 0x01) == 0 && (local_data[13] & 0x80)) {
        /* Remote creation */
        REM_FILE_$ACL_CREATE(local_data, acl_data, type, dir_uid, file_uid_ret, status_ret);
        return;
    }

    /* Enter superuser mode temporarily */
    ACL_$SUPER_COUNT[pid]++;

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
    mapped_addr = MST_$MAPS(PROC1_$AS_ID, 0xFF6A, file_uid_ret, 0, 0x400, 0x16, 0, 0xFF,
                            NULL, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    if (local_flags[0] == '\0') {
        /* Call internal creation helper */
        acl_$prim_create_internal(type, acl_data, *data_len, (uint8_t *)acl_data + 2,
                                  0, mapped_addr, NULL, status_ret);
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
    MST_$UNMAP_PRIVI(1, file_uid_ret, mapped_addr, 0x400, PROC1_$AS_ID, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    /* Make the file immutable */
    FILE_$MK_IMMUTABLE(file_uid_ret, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    /* Purify the AST entry */
    AST_$PURIFY(file_uid_ret, 2, 0, NULL, 0, status_ret);
    if ((*status_ret & 0xFFFF) != 0) {
        goto cleanup_error;
    }

    goto cleanup;

cleanup_error:
    /* Set high bit of status to indicate error during cleanup */
    *status_ret |= 0x80000000;

cleanup:
    /* Exit superuser mode */
    ACL_$SUPER_COUNT[pid]--;
}
