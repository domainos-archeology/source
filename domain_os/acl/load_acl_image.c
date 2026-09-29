/*
 * acl_$load_acl_image - acl_$find_acl_slot's cache-miss path
 *
 * Original address: 0x00E45A60 (was FUN_00e45a60), 1068 bytes.
 *
 * Takes a cache slot (free-list head, or the LRU victim), maps the ACL object
 * into the address space, copies its 0x400-byte image into
 * ACL_$DATA.acl_cache[slot], upgrades pre-version-5 images in place, registers the
 * slot in the cache directory and hash bucket, and returns the slot index.
 * Returns ACL_CACHE_NO_SLOT when the UID turned out to describe a default ACL
 * (nothing needs caching) or when the map/unmap failed.
 *
 * The only caller is acl_$find_acl_slot (0x00E45ED6), which runs with ML lock
 * 10 held.
 *
 * Frame (A6+):
 *   0x08 acl_uid
 *   0x0C cached_flag_ret
 *   0x10 prot
 *   0x14 status_ret
 *
 * Locals (A6-):
 *   -0x2E result       word, the value the epilogue returns
 *   -0x2C conv_len     word, acl_$convert_image's length-out
 *   -0x2A slot         word, the slot acl_$alloc_cache_slot handed out
 *   -0x28 map_out      longword, MST_$MAPS' out parameter
 *   -0x24 image        longword, the mapped VA, later &ACL_$UNWIRED_DATA.image_buf
 *   -0x1C saved_status longword, the status FIM_$CLEANUP came back with
 *   -0x18 cleanup      0x18 bytes, the FIM cleanup record
 *
 * Both helpers it calls (acl_$expand_default_acl 0x00E45984 and
 * acl_$alloc_cache_slot 0x00E458E4) are reached by `bsr.w` with no static
 * link - neither executes `movea.l (A6),An`, and both reach their globals
 * through A5 - so they are module-level Pascal procedures, emitted as
 * acl/expand_default_acl.c and acl/alloc_cache_slot.c rather than as statics
 * here.
 */

#include "acl/acl_internal.h"
#include "uid/uid.h"
#include "mst/mst.h"
#include "fim/fim.h"
#include "proc1/proc1.h"

/*
 * 0x00E45E8C: the operand of the `pea (0x3c,PC)` at 0x00E45E4E - UID_$HASH's
 * modulus, passed by reference the way Pascal passes a constant.  Raw bytes
 * 00 3D.  This is the same physical cell acl/find_acl_slot.c names
 * acl_$find_acl_slot_hash_mod_00e45e8c; the two routines share it because
 * they sit next to each other in the module.
 */
static const uint16_t acl_$load_acl_image_hash_mod_00e45e8c = ACL_CACHE_HASH_MOD;

/* The 0x18 bytes of FIM cleanup record the frame reserves at A6-0x18. */
#define ACL_FIM_CLEANUP_REC_SIZE    0x18

/* `bset.b #0x7,(A0)` on the first byte of the status longword (0x00E45BCC). */
#define STATUS_FATAL_BIT            0x80000000UL

/* MST_$MAPS' protection word (`move.w #0x12,-(SP)`, 0x00E45AC6).  Its
 * argument 2 is a Pascal BOOLEAN byte, pushed by `st -(SP)` at 0x00E45AD4
 * into the even half of a word slot, which is the half MST_$MAPS reads with
 * `move.b (0xa,A6)`; it is passed as plain `true`. */
#define ACL_MAPS_PROT               0x12

/* MST_$UNMAP_PRIVI's mode word (`move.w #0x1,-(SP)`, 0x00E45B7A). */
#define ACL_UNMAP_MODE              1

int16_t acl_$load_acl_image(uid_t *acl_uid, int8_t *cached_flag_ret,
                            acl_$prot_data_t *prot, status_$t *status_ret)
{
    /*
     * A6-0x2E is not written on the `bne.w 0x00E45E7E` path at 0x00E45AA0, so
     * the original returns an uninitialised word there.  That path is dead:
     * acl_$alloc_cache_slot clears *status_ret and never sets it (it crashes
     * instead), so the test can never be true.  Initialising it here keeps the
     * host build deterministic without changing any reachable behaviour.
     */
    int16_t   result = ACL_CACHE_NO_SLOT;   /* A6-0x2E */
    uint16_t  conv_len;                     /* A6-0x2C */
    int16_t   slot;                         /* A6-0x2A */
    void     *map_out;                      /* A6-0x28 */
    void     *image;                        /* A6-0x24 */
    status_$t saved_status;                 /* A6-0x1C */
    uint8_t   cleanup[ACL_FIM_CLEANUP_REC_SIZE];    /* A6-0x18 */

    acl_$cache_slot_t *cs;
    acl_$cache_dir_t  *dir;
    uint32_t          *src;
    uint32_t          *dst;
    int16_t            i;

    /*
     * 0x00E45A68-0x00E45A86.  A default ACL is described entirely by its UID,
     * so there is nothing to map and nothing to cache.  Note that
     * acl_$expand_default_acl also leaves UID_$NIL in *acl_uid (it passes
     * acl_uid as ACL_$DEF_ACLDATA's out parameter).
     */
    if (acl_$expand_default_acl(acl_uid, prot) < 0) {
        *cached_flag_ret = (int8_t)true;    /* `st (A0)` */
        result = ACL_CACHE_NO_SLOT;
        goto ret;
    }

    /* 0x00E45A8A-0x00E45AA4 */
    slot = acl_$alloc_cache_slot(status_ret);
    /* `tst.w (0x2,A0)` is the LOW word of the status longword. */
    if (((uint32_t)*status_ret & 0xFFFFu) != 0) {
        goto ret;                           /* dead - see `result` above */
    }
    result = slot;

    /*
     * 0x00E45AAA-0x00E45AB6 / 0x00E45AEA-0x00E45AF6.  The map runs with the
     * calling process counted as a super-user so that the ACL object's own
     * ACL cannot lock the kernel out of it.  Same array and same indexing as
     * ACL_$ENTER_SUPER / ACL_$EXIT_SUPER (`lea (0x0,A5,D0w*0x1),A0` +
     * `(0xb76,A0)` with D0 = PROC1_$CURRENT * 2).
     */
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]++;

    /* 0x00E45ABA-0x00E45AE6 */
    image = MST_$MAPS((int16_t)PROC1_$AS_ID, true, acl_uid, 0,
                      ACL_CACHE_SLOT_SIZE, ACL_MAPS_PROT, 0, 0,
                      &map_out, status_ret);

    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]--;

    /* 0x00E45AFA-0x00E45B16 */
    if (((uint32_t)*status_ret & 0xFFFFu) != 0) {
        if (*status_ret != status_$mst_object_not_found) {
            goto bad_status;
        }
        *status_ret = status_$acl_object_not_found;
        goto free_relink;
    }

    /*
     * 0x00E45B18-0x00E45B28.  FIM_$CLEANUP returns
     * status_$cleanup_handler_set the first time through; any other value
     * means the copy below faulted and control came back here.
     */
    saved_status = FIM_$CLEANUP(cleanup);
    if (saved_status != status_$cleanup_handler_set) {
        /* 0x00E45B64-0x00E45B9C: the fault path.  Unmap, pop the signal and
         * hand the fault status back to the caller. */
        MST_$UNMAP_PRIVI(ACL_UNMAP_MODE, acl_uid, (uint32_t)(uintptr_t)image,
                         ACL_CACHE_SLOT_SIZE, PROC1_$AS_ID, status_ret);
        FIM_$POP_SIGNAL(cleanup);
        *status_ret = saved_status;
        goto free_relink;
    }

    /* 0x00E45B30-0x00E45B60: 0x100 longwords = one whole slot. */
    src = (uint32_t *)image;
    dst = (uint32_t *)&ACL_$DATA.acl_cache[slot];
    for (i = 0xFF; i != -1; i--) {
        *dst++ = *src++;
    }
    FIM_$RLS_CLEANUP(cleanup);

    /* 0x00E45B9E-0x00E45BCA */
    MST_$UNMAP_PRIVI(ACL_UNMAP_MODE, acl_uid, (uint32_t)(uintptr_t)image,
                     ACL_CACHE_SLOT_SIZE, PROC1_$AS_ID, status_ret);
    if (((uint32_t)*status_ret & 0xFFFFu) != 0) {
        goto bad_status;
    }

    /* 0x00E45BE2-0x00E45BF4 */
    *cached_flag_ret = (int8_t)false;
    cs = &ACL_$DATA.acl_cache[slot];

    if (cs->version == 3) {
        /*
         * 0x00E45BFC-0x00E45C50: promote a version-3 image to version 4 by
         * filling in the header fields version 4 added.
         */
        cs->version      = 4;
        cs->subsys_uid   = cs->type_uid;    /* 0x00E45C02-0x00E45C0A */
        cs->required_uid = UID_$NIL;        /* 0x00E45C0E-0x00E45C18 */
        cs->reserved_22  = 0;
        cs->reserved_26  = 0;
        /* 0x00E45C24-0x00E45C38: a directory ACL keeps whatever
         * world_entry_present held.
         * Compared field-by-field rather than through acl_$uid_eq: type_uid
         * sits at slot+0x02, so its address is an unaligned pointer. */
        if (!(cs->type_uid.high == ACL_$DIR_ACL.high &&
              cs->type_uid.low  == ACL_$DIR_ACL.low)) {
            cs->world_entry_present = 0;
        }
        cs->unused_29 = 0;
        /* 0x00E45C40-0x00E45C4E: five words at +0x2A..+0x33. */
        for (i = 4; i != -1; i--) {
            /* `clr.w (0x28,A0)` with A0 walking cs+0x02 upwards by 2. */
            cs->reserved_2a[(4 - i) * 2]     = 0;
            cs->reserved_2a[(4 - i) * 2 + 1] = 0;
        }
    }

    /* 0x00E45C52-0x00E45C56: a version-5 image needs no conversion. */
    if (cs->version < 5) {
        /* 0x00E45C5A-0x00E45C72 */
        if (cs->type_uid.high == ACL_$DIR_ACL.high &&
            cs->type_uid.low  == ACL_$DIR_ACL.low) {

            /*
             * 0x00E45C76-0x00E45C9C: every directory entry that has not been
             * through this fixup gets the default rights forced into it.
             */
            for (i = 1; i <= (int16_t)cs->entry_count; i++) {
                acl_$v4_entry_t *e = ACL_$V4_ENTRY(cs, i);

                if ((e->rights & ACL_V4_RIGHTS_CONVERTED) == 0) {
                    e->rights |= ACL_V4_RIGHTS_FIXUP;
                }
            }

            /* 0x00E45CA0-0x00E45CA4 */
            if (cs->world_entry_present >= 0) {
                /* 0x00E45CA8-0x00E45CBA: `A0 = cs + entry_count * 0x2C` is
                 * the LAST entry (entries are 1-based). */
                acl_$v4_entry_t *last = ACL_$V4_ENTRY(cs, cs->entry_count);

                if ((int16_t)cs->entry_count < ACL_V4_MAX_ENTRIES) {
                    /*
                     * 0x00E45CBE-0x00E45CFE: if the last entry is already the
                     * all-nil "required" entry there is nothing to append.
                     */
                    if (last->reserved_20 != 0 ||
                        acl_$uid_eq(&last->org, &UID_$NIL) >= 0 ||
                        acl_$uid_eq(&last->person, &UID_$NIL) >= 0 ||
                        acl_$uid_eq(&last->group, &UID_$NIL) >= 0) {

                        /* 0x00E45D00-0x00E45D5A: append it. */
                        acl_$v4_entry_t *ne;

                        cs->entry_count++;
                        ne = ACL_$V4_ENTRY(cs, cs->entry_count);
                        ne->person       = UID_$NIL;
                        ne->group        = UID_$NIL;
                        ne->org          = UID_$NIL;
                        ne->subsys       = UID_$NIL;
                        ne->reserved_20  = 0;
                        ne->reserved_24  = 0;
                        ne->rights       = ACL_V4_RIGHTS_DEFAULT;
                    }
                }
            }
        }

        /* 0x00E45D5C-0x00E45D86 */
        acl_$convert_image(cs, prot, &ACL_$UNWIRED_DATA.image_buf, &conv_len, status_ret);
        image = &ACL_$UNWIRED_DATA.image_buf;
        *cached_flag_ret = (int8_t)true;

        /*
         * 0x00E45D88-0x00E45DCE: a converted image with no entries, no
         * required UID and a plain file/directory subsystem UID carries no
         * information, so it is not worth a cache slot.
         */
        if (conv_len == 0x34 &&
            ACL_$UNWIRED_DATA.image_buf.required_uid.high == UID_$NIL.high &&
            ACL_$UNWIRED_DATA.image_buf.required_uid.low  == UID_$NIL.low &&
            ((ACL_$UNWIRED_DATA.image_buf.subsys_uid.high == ACL_$FILE_ACL.high &&
              ACL_$UNWIRED_DATA.image_buf.subsys_uid.low  == ACL_$FILE_ACL.low) ||
             (ACL_$UNWIRED_DATA.image_buf.subsys_uid.high == ACL_$DIR_ACL.high &&
              ACL_$UNWIRED_DATA.image_buf.subsys_uid.low  == ACL_$DIR_ACL.low))) {

            /* 0x00E45DEC-0x00E45E14 */
            acl_$cache_list_insert(&ACL_$UNWIRED_DATA.cache_free_head,
                                   ACL_$UNWIRED_DATA.cache_hash_links, slot);
            result   = ACL_CACHE_NO_SLOT;
            *acl_uid = UID_$NIL;
            goto ret;
        }

        /* 0x00E45DD0-0x00E45DEA: install the converted image. */
        src = (uint32_t *)image;
        dst = (uint32_t *)&ACL_$DATA.acl_cache[slot];
        for (i = 0xFF; i != -1; i--) {
            *dst++ = *src++;
        }
    }

    /* 0x00E45E16-0x00E45E4C: register the slot in the cache directory. */
    dir          = &ACL_$UNWIRED_DATA.cache_dir[slot];
    dir->acl_uid = *acl_uid;
    dir->cached_flag = *cached_flag_ret;
    if (dir->cached_flag < 0) {
        /* Widened to words on the way in; acl_$find_acl_slot reads the low
         * byte of each back out. */
        dir->world_rights  = (uint16_t)prot->world_rights;
        dir->subsys_rights = (uint16_t)prot->subsys_rights;
    }

    /* 0x00E45E4E-0x00E45E5E: only the low word of UID_$HASH's result is used. */
    dir->hash_bucket =
        (uint16_t)UID_$HASH(acl_uid,
                            (uint16_t *)&acl_$load_acl_image_hash_mod_00e45e8c);

    /* 0x00E45E62-0x00E45E7A */
    acl_$cache_list_insert(&ACL_$UNWIRED_DATA.cache_hash_buckets[dir->hash_bucket],
                           ACL_$UNWIRED_DATA.cache_hash_links, slot);
    goto ret;

bad_status:                                             /* 0x00E45BCC */
    *status_ret |= STATUS_FATAL_BIT;
    /* fall through */

free_relink:                                            /* 0x00E45BD0 */
    /*
     * The slot never made it into a hash bucket, so it goes straight back on
     * the free list - which is threaded through the hash-link array.
     */
    acl_$cache_list_insert(&ACL_$UNWIRED_DATA.cache_free_head, ACL_$UNWIRED_DATA.cache_hash_links, slot);

ret:                                                    /* 0x00E45E7E */
    return result;
}
