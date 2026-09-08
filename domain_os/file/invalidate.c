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
 *   flags       - Pointer to a Domain BOOLEAN byte, passed straight through
 *                 to AST_$INVALIDATE (0x00E75186 `move.b (A2),(-0x16,A6)`,
 *                 0x00E751B4 `move.b (-0x16,A6),-(SP)`)
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
                      uint32_t *page_count, boolean *flags,
                      status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t start_val;
    uint32_t count_val;
    boolean flags_val;
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
         * Permission granted - invalidate the pages.
         *
         * 0x00E751AE-0x00E751C4:
         *   subq.l #0x2,SP              ; discarded word result slot
         *   pea (-0xc,A6)               ; status
         *   move.b (-0x16,A6),-(SP)     ; flags, ONE byte
         *   move.l (-0x10,A6),-(SP)     ; page_count
         *   move.l (-0x14,A6),-(SP)     ; start_page
         *   pea (-0x8,A6)               ; &local_uid
         *   jsr 0x00e0662e.l            ; AST_$INVALIDATE
         * The byte is pushed unchanged; there is no 0xE7 low half.
         * (source-fan2)
         */
        AST_$INVALIDATE(&local_uid, start_val, count_val, flags_val, &status);
    } else {
        /*
         * Permission denied - release wired pages
         */
        OS_PROC_SHUTWIRED(&status);
    }

    *status_ret = status;
}
