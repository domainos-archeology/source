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
 * Data constants (from PC-relative addressing; PC = instruction address + 2):
 *   0x00E751E8: word 0x0000       ACL_$RIGHTS option_flags  (pea (0x58,PC) at 0x00E7518E)
 *   0x00E751EA: byte 0x00         ACL_$RIGHTS ignore_super  (pea (0x52,PC) at 0x00E75196)
 *   0x00E751EC: longword 0x06     ACL_$RIGHTS required_mask (pea (0x58,PC) at 0x00E75192)
 */

#include "file/file_internal.h"

/* Constant cells for the ACL_$RIGHTS call, pooled just past FILE_$INVALIDATE */

/* 0x00E751EC: required rights mask (read 0x02 + write 0x04) */
static const uint32_t file_$invalidate_rights_00e751ec = 0x00000006;

/* 0x00E751EA: ACL_$RIGHTS' ignore_super argument, FALSE - the super-user
 * bypass applies. */
static const boolean file_$invalidate_ignore_super_00e751ea = false;

/* 0x00E751E8: ACL_$RIGHTS' option-flags word */
static const int16_t file_$invalidate_acl_opts_00e751e8 = 0;

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
    ACL_$RIGHTS(&local_uid,
                (boolean *)&file_$invalidate_ignore_super_00e751ea,
                (uint32_t *)&file_$invalidate_rights_00e751ec,
                (int16_t *)&file_$invalidate_acl_opts_00e751e8,
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
