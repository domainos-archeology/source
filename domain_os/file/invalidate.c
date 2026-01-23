/*
 * FILE_$INVALIDATE - Invalidate cached pages of a file
 *
 * Original address: 0x00E75158
 * Size: 144 bytes
 *
 * Invalidates cached pages for a file, forcing them to be re-read
 * from disk on next access. This is typically used to ensure cache
 * coherency after external modifications to a file.
 *
 * Assembly analysis:
 *   - link.w A6,-0x18         ; Stack frame for local vars
 *   - Saves A5, A2 to stack
 *   - Loads A5 with 0xE86068 (global base)
 *   - Copies file_uid to local buffer
 *   - Dereferences start_page, page_count, and flags pointers
 *   - Calls ACL_$RIGHTS to check permission
 *   - If status_$ok, calls AST_$INVALIDATE
 *   - Otherwise calls OS_PROC_SHUTWIRED
 *
 * Data constants (from PC-relative addressing):
 *   DAT_00e751e8: 0x0000       (option flags = 0)
 *   DAT_00e751ea: 0x0000       (additional flags = 0)
 *   DAT_00e751ec: 0x00000006   (rights mask)
 */

#include "file/file_internal.h"

/* Constants for ACL_$RIGHTS call */
static const uint32_t invalidate_rights_mask = 0x00000006;  /* Read + write rights */
static const int16_t invalidate_option_flags = 0;

/*
 * FILE_$INVALIDATE - Invalidate cached pages of a file
 *
 * Forces cached pages for a file to be discarded, ensuring that
 * subsequent reads will fetch fresh data from disk. This is useful
 * for maintaining cache coherency in distributed file systems or
 * after external modifications.
 *
 * Parameters:
 *   file_uid    - UID of file to invalidate
 *   start_page  - Pointer to starting page number
 *   page_count  - Pointer to number of pages to invalidate
 *   flags       - Pointer to flags byte (controls invalidation behavior)
 *   status_ret  - Output status code
 *
 * Required rights:
 *   Read or write permission (0x06 mask) must be granted.
 *
 * Status codes:
 *   status_$ok                  - Invalidation succeeded
 *   status_$insufficient_rights - No permission
 *   (other status from AST_$INVALIDATE)
 */
void FILE_$INVALIDATE(uid_t *file_uid, uint32_t *start_page,
                      uint32_t *page_count, uint8_t *flags,
                      status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t start_val;
    uint32_t count_val;
    uint8_t flags_val;
    status_$t status;

    /* Copy UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Dereference parameters */
    start_val = *start_page;
    count_val = *page_count;
    flags_val = *flags;

    /*
     * Check permission using ACL_$RIGHTS
     * Rights mask 0x06 = read (0x02) + write (0x04)
     */
    ACL_$RIGHTS(&local_uid, (void *)&invalidate_rights_mask,
                (uint32_t *)&invalidate_rights_mask, (int16_t *)&invalidate_option_flags,
                &status);

    if (status == status_$ok) {
        /*
         * Permission granted - invalidate the pages
         * AST_$INVALIDATE signature:
         *   AST_$INVALIDATE(uid, start_page, count, flags, status)
         *
         * The flags parameter is passed as a 16-bit value with:
         *   - Low byte: 0xE7 (constant from assembly)
         *   - High byte: flags_val from parameter
         */
        int16_t combined_flags = (int16_t)((flags_val << 8) | 0xE7);
        AST_$INVALIDATE(&local_uid, start_val, count_val, combined_flags, &status);
    } else {
        /*
         * Permission denied - release wired pages
         */
        OS_PROC_SHUTWIRED(&status);
    }

    *status_ret = status;
}
