/*
 * FILE_$CHECK_PROT - check a caller's rights on a file
 *
 * Original address: 0x00E5D172, 208 bytes.
 *
 * A fast path in front of ACL_$RIGHTS: if the caller already holds a lock on
 * the object, the rights it was granted when the lock was taken are cached in
 * the lock-object table (LOT) entry and no ACL evaluation is needed.
 *
 * Frame (A6+):
 *   0x08 file_uid     (long) -> A3
 *   0x0C access_mask  (word) -> D2w
 *   0x0E slot_num     (long) the caller's per-process lock slot, 1-based
 *   0x12 ignore_super (byte) ACL_$RIGHTS' boolean; its ADDRESS is passed on
 *                            (`pea (0x12,A6)` at 0x00E5D226)
 *   0x14 option_flags (word) ACL_$RIGHTS' option word; likewise by address
 *                            (`pea (0x14,A6)` at 0x00E5D216)
 *   0x16 rights_out   (long)
 *   0x1A status_ret   (long) -> A2
 *
 * Locals (A6-):
 *   -0x0C rights_mask long   access_mask zero-extended, ACL_$RIGHTS' third
 *                            argument (passed by reference)
 *
 * Result: the cache paths leave the `moveq #0x1,D0` of the UID compare in D0;
 * the ACL path leaves ACL_$RIGHTS' own D0.
 */

#include "file/file_internal.h"
#include "acl/acl.h"

/*
 * The two ACL statuses this routine raises directly rather than through
 * ACL_$RIGHTS (`move.l #0x230001` at 0x00E5D1FE, `#0x230002` at 0x00E5D20C).
 */
/* Module-0x23 status codes come from acl/acl.h (included above). */

/*
 * A LOT entry's rights byte carries bit 4 as "this entry does not speak for
 * the object's protection" (`btst.l #0x4,D4` at 0x00E5D1F4) - the same bit
 * acl/acl_internal.h calls ACL_RIGHT_IGNORE.
 */
#define FILE_LOT_RIGHTS_IGNORE      0x10

/*
 * Slot numbers run 1..0x95 here.  `cmpi.l #0x96,D0` + `bcc` (0x00E5D192) is an
 * UNSIGNED test, so 0x96 itself falls through to the ACL path - unlike
 * FILE_$EXPORT_LK, whose `bls` at 0x00E74152 accepts 0x96.
 */
#define FILE_CHECK_PROT_MAX_SLOT    0x96

int16_t FILE_$CHECK_PROT(uid_t *file_uid, uint16_t access_mask, uint32_t slot_num,
                         boolean ignore_super, int16_t option_flags,
                         uint16_t *rights_out, status_$t *status_ret)
{
    int16_t   entry_index;              /* D1w */
    uint16_t  rights;                   /* D4w */
    uint32_t  rights_mask;              /* A6-0x0C */
    file_lock_entry_detail_t *entry;

    *status_ret = status_$ok;                           /* 0x00E5D18A */

    /* 0x00E5D18C-0x00E5D198 */
    if (slot_num != 0 && slot_num < FILE_CHECK_PROT_MAX_SLOT) {

        /*
         * 0x00E5D19A-0x00E5D1BA: the caller's per-process lock table.  The
         * slot holds an index into the global LOT; zero means "not held".
         */
        entry_index = (int16_t)FILE_$PROC_LOT_SLOT(PROC1_$AS_ID, slot_num);

        if (entry_index != 0) {
            /*
             * 0x00E5D1BC-0x00E5D1D6: `lsl.l #0x2` / `neg` / `lsl.l #0x3` /
             * `add` is index*0x1C, added to 0xE935CC - which addresses the END
             * of entry `index`, so the fields are read at negative
             * displacements.  In the C model that is FILE_$LOT_ENTRY(index)
             * (0xE935CC + (index-1)*0x1C) with ordinary field offsets.
             */
            entry = FILE_$LOT_ENTRY(entry_index);

            /* 0x00E5D1D6: `move.b (-0x2,A1,D0)` is entry->rights (+0x1A);
             * 0x00E5D1DE zero-extends it into the caller's word.  Note this
             * happens BEFORE the UID check, so a foreign entry still
             * overwrites *rights_out. */
            rights = (uint16_t)entry->rights;
            *rights_out = rights;

            /* 0x00E5D1E4-0x00E5D1F8: the entry must be this file's, and must
             * not be flagged "ignore". */
            if (entry->uid_high == file_uid->high &&
                entry->uid_low  == file_uid->low &&
                (rights & FILE_LOT_RIGHTS_IGNORE) == 0) {

                if (rights == 0) {                      /* 0x00E5D1FA */
                    *status_ret = status_$no_right_to_perform_operation;
                    return 1;
                }
                if ((rights & access_mask) == access_mask) {
                    return 1;                           /* 0x00E5D20A */
                }
                *status_ret = status_$insufficient_rights_to_perform_operation;
                return 1;                               /* 0x00E5D20C */
            }
        }
    }

    /*
     * 0x00E5D214-0x00E5D236: the full ACL evaluation.  All four of
     * ACL_$RIGHTS' non-status arguments are passed by reference, so the two
     * that are this routine's own parameters have their addresses taken here.
     */
    rights_mask = (uint32_t)access_mask;                /* 0x00E5D21C */
    *rights_out = (uint16_t)ACL_$RIGHTS(file_uid, &ignore_super, &rights_mask,
                                        &option_flags, status_ret);

    /* `move.w D0w,(A1)`: only the low word reaches *rights_out, and
     * ACL_$RIGHTS' D0 is also this function's own result. */
    return (int16_t)*rights_out;
}
