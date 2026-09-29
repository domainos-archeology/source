/*
 * acl_$convert_image - rewrite a version-3/4 ACL image as a version-5 image
 *
 * Original address: 0x00E44E68 (was FUN_00e44e68), 478 bytes.
 *
 * The only caller is acl_$load_acl_image (0x00E45D6E), which runs it on the
 * slot it has just filled from the mapped ACL object and hands it
 * ACL_$UNWIRED_DATA.image_buf as the destination.  Argument order from the pushes at
 * 0x00E45D5C-0x00E45D6C; frame slots A6+0x08 src, +0x0C prot, +0x10 dst,
 * +0x14 length_ret, +0x18 status_ret.
 *
 * What it does:
 *   - stamps a fresh version-5 header into `dst`, carrying over only the
 *     type, required and subsystem UIDs;
 *   - resets `prot` to the system default (ACL_$DEF_ACLDATA) and clears its
 *     world rights;
 *   - walks the 0x2C-byte version-3/4 entries.  The all-nil entry (person,
 *     group and org all UID_$NIL) is not copied: its rights become
 *     prot->world_rights.  Every other entry is copied down to a 0x20-byte
 *     version-5 entry with its rights run through acl_$convert_rights, and
 *     for file and directory ACLs its rights bits 0, 1, 2, 3 and 6 are
 *     accumulated into prot->subsys_rights;
 *   - stores 0x34 + 0x20 * dst->entry_count through `length_ret`.
 *
 * Module-level Pascal procedure, not a nested subprocedure: `link.w A6,-0x3c`
 * with no `movea.l (A6),An` static-link load, and it reaches UID_$NIL,
 * ACL_$FILE_ACL and ACL_$DIR_ACL through absolute addresses rather than
 * through A5.
 *
 * *status_ret is cleared on entry (0x00E44E78) and never set again - the
 * routine cannot fail.
 */

#include "acl/acl_internal.h"
#include "uid/uid.h"

void acl_$convert_image(acl_$cache_slot_t *src, acl_$prot_data_t *prot,
                        acl_$cache_slot_t *dst, uint16_t *length_ret,
                        status_$t *status_ret)
{
    uint8_t          *dst_bytes = (uint8_t *)dst;
    acl_$v4_entry_t  *src_entry;    /* (-0x3c,A6), held biased by -8 in A2 */
    acl_$acl_entry_t *dst_entry;    /* (-0x14,A6) / A3 */
    uid_t            *src_type_uid; /* `pea (0x2,A0)`, 0x00E44F46 / 0x00E44F74 */
    uid_t             def_acl_uid;  /* A6-0x10: ACL_$DEF_ACLDATA's second out */
    int16_t           entry_index;  /* D2w */
    int16_t           remaining;    /* D3w, the dbf counter */
    uint8_t           new_rights;   /* D0b out of acl_$convert_rights */
    int               i;

    /* 0x00E44E74-0x00E44E78 */
    *status_ret = status_$ok;

    /*
     * 0x00E44E86-0x00E44EA4: the version-5 header.  The 16-byte byte copy at
     * 0x00E44EA0 spans src+0x12..+0x21, i.e. required_uid then subsys_uid.
     */
    dst->version      = 5;
    dst->type_uid     = src->type_uid;
    dst->required_uid = src->required_uid;
    dst->subsys_uid   = src->subsys_uid;

    /*
     * 0x00E44EA6-0x00E44EB4: `moveq #0xb` + dbf with A0 walking dst+0x02
     * upwards by two and clearing (0x28,A0) - twelve words starting at
     * dst+0x2A.  That covers all of reserved_2a AND the first 14 bytes of
     * entry 1, which is why it is spelled out in bytes here.
     */
    for (i = 0; i <= 11; i++) {
        dst_bytes[0x2A + i * 2]     = 0;
        dst_bytes[0x2A + i * 2 + 1] = 0;
    }

    /* 0x00E44EB6-0x00E44ECA.  entry_count (dst+0x0E) is deliberately left
     * alone here; the epilogue sets it. */
    dst->reserved_0a         = 0;
    dst->reserved_10         = 0;
    dst->reserved_22         = 0;
    dst->reserved_26         = 0;
    dst->world_entry_present = 0;
    dst->unused_29           = 0;

    /* 0x00E44ECE-0x00E44EDE.  def_acl_uid is filled with UID_$NIL and
     * dropped on the floor - only `prot` matters here. */
    ACL_$DEF_ACLDATA(prot, &def_acl_uid);
    prot->world_rights = 0;

    /* 0x00E44EE2-0x00E44EFC.  type_uid sits at src+0x02, an odd multiple of
     * two inside the packed image, so its address is formed by hand. */
    src_type_uid = (uid_t *)(((uint8_t *)src) + 0x02);
    entry_index  = 1;
    dst_entry    = ACL_$CACHE_ENTRY(dst, 1);
    src_entry    = ACL_$V4_ENTRY(src, 1);

    remaining = (int16_t)(src->entry_count - 1);
    if (remaining >= 0) {
        do {
            /*
             * 0x00E44F08-0x00E44F42: person, group and org all UID_$NIL -
             * this is the image's "world" entry.  It is not copied.
             */
            if (acl_$uid_eq(&src_entry->person, &UID_$NIL) < 0 &&
                acl_$uid_eq(&src_entry->group,  &UID_$NIL) < 0 &&
                acl_$uid_eq(&src_entry->org,    &UID_$NIL) < 0) {

                /* 0x00E44F44-0x00E44F56 */
                prot->world_rights =
                    acl_$convert_rights(src_entry->rights, src_type_uid);
            } else {
                /*
                 * 0x00E44F5E-0x00E44F70: `moveq #0x17` + dbf = the 24 bytes
                 * of person, group and org.  The version-4 subsys UID at
                 * +0x18 has no version-5 counterpart and is dropped.
                 */
                dst_entry->person = src_entry->person;
                dst_entry->group  = src_entry->group;
                dst_entry->org    = src_entry->org;

                /*
                 * 0x00E44F72-0x00E44F86: the converted rights are zero
                 * extended to a longword and stored at dst_entry+0x18, so
                 * reserved_18 is zeroed and rights takes the value.
                 */
                new_rights = acl_$convert_rights(src_entry->rights,
                                                 src_type_uid);
                dst_entry->reserved_18 = 0;
                dst_entry->rights      = new_rights;

                /*
                 * 0x00E44F8A-0x00E44FB2: re-tested on every entry, exactly as
                 * the image does.  Field-by-field again - see src_type_uid.
                 */
                if ((src->type_uid.high == ACL_$FILE_ACL.high &&
                     src->type_uid.low  == ACL_$FILE_ACL.low) ||
                    (src->type_uid.high == ACL_$DIR_ACL.high &&
                     src->type_uid.low  == ACL_$DIR_ACL.low)) {

                    /*
                     * 0x00E44FB4-0x00E45002: five `btst.b`/`bset.b` pairs on
                     * the low byte of the entry rights (dst_entry+0x1B) and
                     * prot->subsys_rights, in the image's own bit order.
                     */
                    if ((dst_entry->rights & ACL_V5_RIGHT_2) != 0) {
                        prot->subsys_rights |= ACL_V5_RIGHT_2;
                    }
                    if ((dst_entry->rights & ACL_V5_RIGHT_1) != 0) {
                        prot->subsys_rights |= ACL_V5_RIGHT_1;
                    }
                    if ((dst_entry->rights & ACL_V5_RIGHT_0) != 0) {
                        prot->subsys_rights |= ACL_V5_RIGHT_0;
                    }
                    if ((dst_entry->rights & ACL_V5_RIGHT_3) != 0) {
                        prot->subsys_rights |= ACL_V5_RIGHT_3;
                    }
                    if ((dst_entry->rights & ACL_V5_RIGHT_6) != 0) {
                        prot->subsys_rights |= ACL_V5_RIGHT_6;
                    }
                }

                /* 0x00E45004-0x00E45008 */
                entry_index = (int16_t)(entry_index + 1);
                dst_entry   = (acl_$acl_entry_t *)
                              ((uint8_t *)dst_entry + 0x20);
            }

            /* 0x00E4500C-0x00E45012 */
            src_entry = (acl_$v4_entry_t *)((uint8_t *)src_entry + 0x2C);
            remaining = (int16_t)(remaining - 1);
        } while (remaining != -1);
    }

    /* 0x00E45016-0x00E4502A */
    if (entry_index <= 1) {
        dst->entry_count = 0;
    } else {
        dst->entry_count = (uint16_t)(entry_index - 1);
    }

    /* 0x00E4502C-0x00E4503A: word arithmetic - `lsl.w #0x5` then
     * `addi.w #0x34`. */
    *length_ret = (uint16_t)((uint16_t)(dst->entry_count << 5) + 0x34);
}
