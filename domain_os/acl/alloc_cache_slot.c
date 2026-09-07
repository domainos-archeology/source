/*
 * acl_$alloc_cache_slot - hand out an ACL image cache slot
 *
 * Original address: 0x00E458E4 (was FUN_00e458e4), 156 bytes.
 *
 * Takes the head of the free list when there is one, otherwise evicts the
 * least-recently-used slot.  The free list is threaded through the SAME link
 * array as the hash chains (ACL_$CACHE_HASH_LINKS, A5+0xA70) - only the head
 * differs (ACL_$CACHE_FREE_HEAD, A5+0xB74) - which is what lets
 * acl_$load_acl_image put a slot back on the free list with a single
 * acl_$cache_list_insert at 0x00E45BD0.
 *
 * The returned slot is a member of no list: the caller links it.
 *
 * The only caller is acl_$load_acl_image (0x00E45A8E), a plain `bsr.w` with no
 * static link, so this is a module-level Pascal procedure and gets its own
 * file.
 *
 * Frame (A6+):
 *   0x08 status_ret -> A0
 *
 * *status_ret is cleared on entry and never set again - the routine either
 * returns a slot or crashes the system - so the caller's status test at
 * 0x00E45A9C can never fire.
 */

#include "acl/acl_internal.h"
#include "misc/crash_system.h"

/*
 * 0x00E45980: the operand of the `pea (0x64,PC)` at 0x00E4591A - the status
 * CRASH_SYSTEM is given when both the free list and the LRU list are empty.
 * Raw bytes there are 00 23 00 00: the ACL manager's subsystem code with
 * module and code 0, which has no entry of its own in the status database.
 */
static const status_$t acl_$alloc_cache_slot_crash_00e45980 = 0x00230000;

int16_t acl_$alloc_cache_slot(status_$t *status_ret)
{
    int16_t slot;       /* D2w */
    int16_t victim;     /* D3w */

    *status_ret = status_$ok;                           /* 0x00E458F0 */

    /* 0x00E458F2-0x00E45910: the free list. */
    if (ACL_$CACHE_FREE_HEAD != ACL_CACHE_NO_SLOT) {
        slot = ACL_$CACHE_FREE_HEAD;
        /* The `move.w (0xb74,A5),-(SP)` at 0x00E45900 re-reads the head
         * rather than reusing D2; the two are the same value. */
        acl_$cache_list_remove(&ACL_$CACHE_FREE_HEAD, ACL_$CACHE_HASH_LINKS,
                               ACL_$CACHE_FREE_HEAD);
        return slot;                                    /* 0x00E45974 */
    }

    /* 0x00E45912-0x00E45924 */
    if (ACL_$CACHE_LRU_HEAD == ACL_CACHE_NO_SLOT) {
        CRASH_SYSTEM(&acl_$alloc_cache_slot_crash_00e45980);
    }

    /*
     * 0x00E45926-0x00E45932: the LRU list is circular, so the victim is the
     * predecessor of the head - `(0x9f2,A5 + head*4)` is
     * ACL_$CACHE_LRU_LINKS[head].prev.
     */
    victim = ACL_$CACHE_LRU_LINKS[ACL_$CACHE_LRU_HEAD].prev;

    /* 0x00E45936-0x00E45946 */
    acl_$cache_list_remove(&ACL_$CACHE_LRU_HEAD, ACL_$CACHE_LRU_LINKS, victim);

    /* 0x00E4594A-0x00E4596E: unlink it from the hash bucket it was chained
     * in.  The bucket number is remembered in the directory entry. */
    slot = (int16_t)ACL_$CACHE_DIR[victim].hash_bucket;
    acl_$cache_list_remove(&ACL_$CACHE_HASH_BUCKETS_TAB[slot],
                           ACL_$CACHE_HASH_LINKS, victim);

    slot = victim;                                      /* 0x00E45972 */
    return slot;                                        /* 0x00E45974 */
}
