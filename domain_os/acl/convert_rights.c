/*
 * acl_$convert_rights - map a pre-version-5 ACL rights word onto the
 * version-5 rights byte.
 *
 * Original address: 0x00E44DBE (was FUN_00e44dbe), 170 bytes.
 *
 * Two callers:
 *   acl_$convert_image      (0x00E44F4E, 0x00E44F7C) - once per version-3/4
 *                           entry, and once for the all-nil "world" entry.
 *   acl_$expand_default_acl (0x00E45A44) - on the rights bits encoded in a
 *                           default-ACL UID, with ACL_CONVERT_RIGHTS_DEFAULT
 *                           (bit 25) forced on.
 *
 * Module-level Pascal function, not a nested subprocedure: `link.w A6,-0x8`
 * with no `movea.l (A6),An` static-link load, and it reaches ACL_$FILE_ACL and
 * ACL_$DIR_ACL through absolute addresses (`movea.l #0xe17444,A2` at
 * 0x00E44DD2, `movea.l #0xe1744c,A2` at 0x00E44E0A) rather than through A5.
 *
 * The eight bytes the prologue reserves (`link.w A6,-0x8`) are never touched,
 * and the `moveq #0x1,D2` at 0x00E44DD8 / 0x00E44E10 is the dead "assume the
 * UIDs are equal" half of the Pascal 8-byte compare idiom - D2 is never read.
 *
 * The result is built in D0's low word: `clr.w D0w` at 0x00E44DCE leaves the
 * high half of D0 undefined, and every caller consumes it as a byte
 * (`move.b D0b,(0x1b,A0)`) or a word (`move.w D0w,D1w`), so the return type is
 * the byte the version-5 entry rights field holds.
 */

#include "acl/acl_internal.h"

uint8_t acl_$convert_rights(uint32_t old_rights, uid_t *acl_type_uid)
{
    uint16_t new_rights = 0;            /* D0w; `clr.w D0w` 0x00E44DCE */

    /*
     * 0x00E44DD0-0x00E44E06: the file-ACL bit assignment.  Compared
     * field-by-field rather than through acl_$uid_eq because the caller's
     * type_uid sits at slot+0x02, i.e. on an odd multiple of two.
     */
    if (acl_type_uid->high == ACL_$FILE_ACL.high &&
        acl_type_uid->low  == ACL_$FILE_ACL.low) {

        /* 0x00E44DE2-0x00E44DE8: `moveq #0x4,D0` ASSIGNS - it is not an OR.
         * D0w is still zero here, so the two are equivalent. */
        if ((old_rights & ACL_V4_RIGHT_1) != 0) {
            new_rights = ACL_V5_RIGHT_2;
        }
        /* 0x00E44DEA-0x00E44DF0 */
        if ((old_rights & ACL_V4_RIGHT_2) != 0) {
            new_rights |= ACL_V5_RIGHT_1;
        }
        /* 0x00E44DF4-0x00E44DFA */
        if ((old_rights & ACL_V4_RIGHT_0) != 0) {
            new_rights |= ACL_V5_RIGHT_0;
        }
        /* 0x00E44DFE-0x00E44E04: `bne` - the ABSENCE of old bit 3 sets it. */
        if ((old_rights & ACL_V4_RIGHT_3) == 0) {
            new_rights |= ACL_V5_RIGHT_6;
        }
    }

    /*
     * 0x00E44E08-0x00E44E52: the directory-ACL bit assignment.  A second
     * independent `if`, not an `else` - the image re-loads the type UID at
     * 0x00E44E08 and falls through from the file case.
     */
    if (acl_type_uid->high == ACL_$DIR_ACL.high &&
        acl_type_uid->low  == ACL_$DIR_ACL.low) {

        /* 0x00E44E1A-0x00E44E20 */
        if ((old_rights & ACL_V4_RIGHT_0) != 0) {
            new_rights |= ACL_V5_RIGHT_2;
        }
        /* 0x00E44E24-0x00E44E3C: all four of old bits 3, 1, 2 and 6. */
        if ((old_rights & ACL_V4_RIGHT_3) != 0 &&
            (old_rights & ACL_V4_RIGHT_1) != 0 &&
            (old_rights & ACL_V4_RIGHT_2) != 0 &&
            (old_rights & ACL_V4_RIGHT_6) != 0) {
            new_rights |= ACL_V5_RIGHT_1;
        }
        /* 0x00E44E40-0x00E44E46 */
        if ((old_rights & ACL_V4_RIGHT_5) != 0) {
            new_rights |= ACL_V5_RIGHT_0;
        }
        /* 0x00E44E4A-0x00E44E50: again the ABSENCE of the old bit. */
        if ((old_rights & ACL_V4_RIGHT_4) == 0) {
            new_rights |= ACL_V5_RIGHT_6;
        }
    }

    /* 0x00E44E54-0x00E44E5A: independent of the ACL type. */
    if ((old_rights & ACL_V4_RIGHT_25) != 0) {
        new_rights |= ACL_V5_RIGHT_3;
    }

    return (uint8_t)new_rights;
}
