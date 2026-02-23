/*
 * dir_$create_dir_obj - Create a new directory file object
 *
 * Creates a new directory file via FILE_$PRIV_CREATE, allocates a handle,
 * validates it, maps page 0, and initializes the directory structure:
 *
 * Page 0 layout after initialization:
 *   +0x00: type byte - upper 2 bits cleared (andi #0x3F)
 *   +0x01: subtype byte - set to 5 (directory type) in low 6 bits
 *   +0x0C: entry 0 link index = 0xFFFF (no overflow)
 *   +0x0E: free entry table pointer (computed)
 *   +0x10: free space (initially 0x400, then adjusted)
 *   +0x12: entry count = 1
 *   +0x14: header size = entry_table_start + 8
 *   +0x16: base data offset = 0x68
 *   +0x1A..0x45: DIR ACL protection data (44 bytes, copied from parent page0+0x1A)
 *   +0x46..0x4D: DIR ACL UID (8 bytes)
 *   +0x4E..0x79: FILE ACL protection data (44 bytes, copied from parent page0+0x4E)
 *   +0x7A..0x81: FILE ACL UID (8 bytes)
 *
 * The initial entry (at the computed offset) is a stub entry:
 *   +0: type=4 (link stub), +1: name_len=1, +4: link_page=0xFFFF
 *
 * After structure init, increments refcount (attribute 6) on the DIR ACL
 * and FILE ACL UIDs (if non-NIL). If FILE ACL is NIL, uses FILE_$FW_PARTIAL
 * instead of AST_$SET_ATTRIBUTE.
 *
 * On failure after FILE_$PRIV_CREATE, deletes the created file via
 * FILE_$DELETE_OBJ.
 *
 * Parameters:
 *   parent_uid  - Parent directory UID (for FILE_$PRIV_CREATE)
 *   page0_data  - Page 0 data from parent (source of ACL data at +0x1A, +0x4E)
 *   dir_acl_uid - DIR default ACL UID to set at page0+0x46
 *   file_acl_uid - FILE default ACL UID to set at page0+0x7A
 *   new_uid_ret - Output: UID of created directory
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E52394
 * Original size: 482 bytes
 */

#include "dir/dir_internal.h"

/* DAT_00e52040 - Truncation constant (0x00000400 = one page) */
static const uint32_t DAT_00e52040_val = 0x00000400;

void dir_$create_dir_obj(uid_t *parent_uid, void *page0_data, uid_t *dir_acl_uid,
                         uid_t *file_acl_uid, uid_t *new_uid_ret,
                         status_$t *status_ret)
{
    void *handle;
    uint8_t *page0;
    status_$t local_status;
    uint16_t attr_val;
    uint8_t delete_buf[4];
    uint8_t local_buf[40];
    char dir_acl_byte;  /* First byte of dir_acl_uid->high, saved for rollback */

    /* Create the file object (type 1 = directory, parent_uid for location) */
    FILE_$PRIV_CREATE(1, &UID_$NIL, parent_uid, new_uid_ret, 0, 0, 0, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Allocate a directory handle */
    handle = DIR_$ALLOC_HANDLE();
    if (handle == NULL) {
        *status_ret = status_$naming_directory_locked;
        return;
    }

    /* Copy new UID into handle */
    *(uint32_t *)handle = new_uid_ret->high;
    *((uint32_t *)handle + 1) = new_uid_ret->low;

    /* Validate the handle (mode 2 = write) */
    DIR_$VALIDATE_HANDLE(handle, 2, status_ret);
    if (*status_ret != status_$ok) {
        goto release_handle;
    }

    /* Map page 0 of the new directory */
    page0 = (uint8_t *)dir_$map_page(handle, 0);

    /* Initialize directory structure */

    /* Set subtype to 5 (directory) in low 6 bits */
    page0[1] &= 0xC0;
    page0[1] |= 5;
    /* Clear type bits (upper 2 bits of byte 0) */
    page0[0] &= 0x3F;

    /* Entry 0 link index = 0xFFFF (no overflow chain) */
    page0[0x0C] = 0xFF;
    page0[0x0D] = 0xFF;

    /* Entry count = 1 */
    page0[0x12] = 1;

    /* Base data offset = 0x68 */
    *(uint16_t *)(page0 + 0x16) = 0x68;

    /* Header size = base_data_offset + 8 */
    *(uint16_t *)(page0 + 0x14) = *(uint16_t *)(page0 + 0x16) + 8;

    /* Free entry table pointer = base_data_offset + 0x1A (= 0x82) */
    {
        int16_t free_ptr = *(int16_t *)(page0 + 0x16) + 0x1A;
        *(int16_t *)(page0 + 0x0E) = free_ptr;
    }

    /* Free space = 0x400 (one page) */
    page0[0x10] = 4;
    page0[0x11] = 0;

    /* Compute actual free space: (0x400 - 0x0E) & ~3 */
    {
        uint16_t free_space = (*(uint16_t *)(page0 + 0x10) - 0x0E) & 0xFFFC;
        *(uint16_t *)(page0 + 0x10) = free_space;

        /* Write free_space value at the free entry table pointer location */
        int16_t free_ptr = *(int16_t *)(page0 + 0x0E);
        *(uint16_t *)(page0 + free_ptr) = free_space;
        *(int16_t *)(page0 + 0x0E) = free_ptr + 2;

        /* Initialize the stub entry at the computed offset */
        uint8_t *stub = page0 + (int16_t)free_space;
        *stub &= 0xF8;     /* Clear type bits */
        *stub |= 4;        /* Set type = 4 (link stub) */
        stub[1] = 1;       /* Name length = 1 */
        stub[4] = 0xFF;    /* Link page = 0xFFFF (inline/none) */
        stub[5] = 0xFF;
    }

    /* Copy DIR ACL protection data from parent page0+0x1A (44 bytes = 11 uint32_t) */
    {
        int16_t i;
        uint32_t *src = (uint32_t *)((uint8_t *)page0_data + 0x1A);
        uint32_t *dst = (uint32_t *)(page0 + 0x1A);
        for (i = 10; i >= 0; i--) {
            *dst++ = *src++;
        }
    }

    /* Copy FILE ACL protection data from parent page0+0x4E (44 bytes = 11 uint32_t) */
    {
        int16_t i;
        uint32_t *src = (uint32_t *)((uint8_t *)page0_data + 0x4E);
        uint32_t *dst = (uint32_t *)(page0 + 0x4E);
        for (i = 10; i >= 0; i--) {
            *dst++ = *src++;
        }
    }

    /* Set DIR ACL UID at page0+0x46 */
    *(uint32_t *)(page0 + 0x46) = dir_acl_uid->high;
    *(uint32_t *)(page0 + 0x4A) = dir_acl_uid->low;

    /* Set FILE ACL UID at page0+0x7A */
    *(uint32_t *)(page0 + 0x7A) = file_acl_uid->high;
    *(uint32_t *)(page0 + 0x7E) = file_acl_uid->low;

    /* Increment refcount on ACL UIDs */
    attr_val = 1;
    dir_acl_byte = (char)(dir_acl_uid->high >> 24);

    /* Increment DIR ACL refcount if non-NIL */
    if (dir_acl_byte != 0) {
        AST_$SET_ATTRIBUTE(dir_acl_uid, 6, &attr_val, status_ret);
        if (*status_ret != status_$ok) {
            goto release_handle;
        }
    }

    /* Increment FILE ACL refcount if non-NIL */
    if ((char)(file_acl_uid->high >> 24) != 0) {
        AST_$SET_ATTRIBUTE(file_acl_uid, 6, &attr_val, status_ret);
        if (*status_ret != status_$ok) {
            /* Rollback DIR ACL refcount if we incremented it */
            if (dir_acl_byte != 0) {
                AST_$SET_ATTRIBUTE(dir_acl_uid, 7, &attr_val, status_ret);
            }
            goto release_handle;
        }
    } else {
        /* FILE ACL is NIL - just write the page */
        FILE_$FW_PARTIAL(new_uid_ret, &DAT_00e4b33c, &DAT_00e52040_val, &local_status);
    }

release_handle:
    /* Store handle into local for release */
    {
        uint32_t handle_val = (uint32_t)(uintptr_t)handle;
        dir_$release_handle(&handle_val);
    }

    /* If creation failed, delete the file */
    if (*status_ret != status_$ok) {
        FILE_$DELETE_OBJ(new_uid_ret, 0xFF, delete_buf, &local_status);
    }
}
