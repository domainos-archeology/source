/*
 * acl_$find_acl_slot - locate (or load) an ACL image in the ACL image cache
 *
 * Original address: 0x00E45E8E (was FUN_00e45e8e), 234 bytes.
 *
 * Called from six sites: acl_$eval_rights (0x00E467E2), ACL_$RIGHTS' image
 * path (0x00E4698C, 0x00E46C2A), ACL_$SET_ACL_CHECK (0x00E47292, 0x00E472EE)
 * and acl_$image_internal (0x00E47C94).  All of them run with ML lock 10 held.
 *
 * Frame (A6+):
 *   0x08 acl_uid         (long) -> A3
 *   0x0C cached_flag_ret (long) -> D3
 *   0x10 prot            (long) -> D4
 *   0x14 status_ret      (long) -> D5
 *
 * Locals (A6-):
 *   -0x10 def_acl_uid    8 bytes, ACL_$DEF_ACLDATA's second out-parameter
 *
 * A5 for this module is 0xE7CF54, so the `(0x800,A2)` / `(0xa70,A1)` /
 * `(0xaf0,A0)` / `(0x9f0,A5)` / `(0xb76,A5)` operands are the cache directory,
 * the hash chain links, the hash buckets and the LRU list - see
 * acl/acl_internal.h.
 *
 * Result: the 0-based slot index into ACL_$DATA.acl_cache, or ACL_CACHE_NO_SLOT.
 */

#include "acl/acl_internal.h"
#include "uid/uid.h"

/*
 * 0x00E45E8C: the operand of the `pea (-0x20,PC)` at 0x00E45EAA - UID_$HASH's
 * modulus, passed by reference the way Pascal passes a constant.  The raw
 * bytes there are 00 3D.
 */
static const uint16_t acl_$find_acl_slot_hash_mod_00e45e8c = ACL_CACHE_HASH_MOD;

int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret)
{
    int16_t            slot;            /* D2w */
    int16_t            hash;            /* D0w / D1 */
    acl_$cache_dir_t  *dir;             /* A2 + 0x800 */
    uid_t              def_acl_uid;     /* A6-0x10 */

    *status_ret = status_$ok;                               /* 0x00E45EA8 */

    /* 0x00E45EAA-0x00E45EC2.  Only the remainder (D0's low word after the
     * `swap`) is used; `ext.l` then `add.l D1,D1` is the word index. */
    hash = (int16_t)UID_$HASH(acl_uid,
                              (uint16_t *)&acl_$find_acl_slot_hash_mod_00e45e8c);
    slot = ACL_$UNWIRED_DATA.cache_hash_buckets[hash];

    for (;;) {
        if (slot == ACL_CACHE_NO_SLOT) {                    /* 0x00E45EC8 */
            /* 0x00E45ECE-0x00E45EF0: miss - load the image. */
            slot = acl_$load_acl_image(acl_uid, cached_flag_ret, prot,
                                       status_ret);
            /* `tst.w (0x2,A0)` is the LOW word of the status longword. */
            if (((uint32_t)*status_ret & 0xFFFFu) != 0) {   /* 0x00E45EE2 */
                return slot;
            }
            if (slot == ACL_CACHE_NO_SLOT) {                /* 0x00E45EEA */
                return slot;
            }
            /* A freshly loaded slot is only linked, never unlinked first. */
            goto link_lru;                                  /* 0x00E45EF0 */
        }

        dir = &ACL_$UNWIRED_DATA.cache_dir[slot];                        /* 0x00E45EF2 */

        /* 0x00E45F02-0x00E45F0C: the two-longword UID compare. */
        if (dir->acl_uid.high == acl_uid->high &&
            dir->acl_uid.low  == acl_uid->low) {

            /* 0x00E45F0E: the flag byte is handed back unconditionally; the
             * `bpl` at 0x00E45F14 tests the byte the `move.b` just stored. */
            *cached_flag_ret = dir->cached_flag;
            if (dir->cached_flag < 0) {
                /* 0x00E45F16-0x00E45F2A: the cached image is a default ACL,
                 * so rebuild the default protection record and then restore
                 * the two rights bytes the image really carries. */
                ACL_$DEF_ACLDATA(prot, &def_acl_uid);
                /* The directory holds these as words (acl_$load_acl_image
                 * widens them at 0x00E45E40/0x00E45E4A); the `move.b
                 * (0x80d,A2)` / `(0x80f,A2)` here read only the low byte. */
                prot->world_rights  = (uint8_t)dir->world_rights;
                prot->subsys_rights = (uint8_t)dir->subsys_rights;
            }

            /* 0x00E45F48: a hit moves the slot to the front of the LRU list -
             * remove, then fall into the insert below. */
            acl_$cache_list_remove(&ACL_$UNWIRED_DATA.cache_lru_head, ACL_$UNWIRED_DATA.cache_lru_links,
                                   slot);
            goto link_lru;
        }

        /* 0x00E45F32-0x00E45F46: follow the circular hash chain; coming back
         * to the bucket head means the UID is not cached. */
        slot = ACL_$UNWIRED_DATA.cache_hash_links[slot].next;
        if (slot == ACL_$UNWIRED_DATA.cache_hash_buckets[hash]) {
            slot = ACL_CACHE_NO_SLOT;
        }
    }

link_lru:                                                   /* 0x00E45F5C */
    acl_$cache_list_insert(&ACL_$UNWIRED_DATA.cache_lru_head, ACL_$UNWIRED_DATA.cache_lru_links, slot);
    return slot;                                            /* 0x00E45F6C */
}
