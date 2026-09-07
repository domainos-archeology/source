/*
 * acl_$cache_list_insert (0x00E44C3C, was FUN_00e44c3c, 86 bytes)
 * acl_$cache_list_remove (0x00E44C92, was FUN_00e44c92, 86 bytes)
 *
 * The two circular doubly-linked-list primitives the ACL image cache is built
 * on.  Both take the address of the list head word, the base of a link array
 * indexed by slot number, and the slot to operate on; the head holds -1 for
 * the empty list.
 *
 * Two lists share the link array at A5+0xA70 (the free list, head A5+0xB74,
 * and the 61 hash chains headed by A5+0xAF0[h]); the LRU list uses the array
 * at A5+0x9F0 with head A5+0xB76.
 *
 * Both are ordinary Pascal procedures - `link.w A6,-0x8` and arguments at
 * A6+0x08 (head), A6+0x0C (array) and A6+0x10 (slot, a word).  Callers push a
 * spare word below the word argument to keep SP longword aligned
 * (`subq.l #0x2,SP` at 0x00E45F48 / 0x00E45F5C); that slot is never read.
 */

#include "acl/acl_internal.h"

/*
 * acl_$cache_list_insert - make `slot` the new head of the list.
 *
 * 0x00E44C4E: an empty list (head == -1) gets a one-element ring.  Otherwise
 * `slot` is spliced in just before the current head - i.e. at the ring's tail -
 * and then becomes the head itself (0x00E44C88).
 */
void acl_$cache_list_insert(int16_t *head, acl_$cache_link_t *links, int16_t slot)
{
    int16_t tail;

    if (*head != ACL_CACHE_NO_SLOT) {               /* 0x00E44C4E */
        tail = links[*head].prev;                   /* 0x00E44C58 */
        links[*head].prev = slot;                   /* 0x00E44C5C */
        links[tail].next  = slot;                   /* 0x00E44C66 */
        links[slot].next  = *head;                  /* 0x00E44C70 */
        links[slot].prev  = tail;                   /* 0x00E44C74 */
    } else {
        links[slot].next = slot;                    /* 0x00E44C80 */
        links[slot].prev = slot;                    /* 0x00E44C84 */
    }
    *head = slot;                                   /* 0x00E44C88 */
}

/*
 * acl_$cache_list_remove - unlink `slot` from the list.
 *
 * 0x00E44CA6: a `head == -1` list is left alone; note that this is the ONLY
 * guard - the routine does not check that `slot` is actually a member, and it
 * does not clear the removed node's links.
 */
void acl_$cache_list_remove(int16_t *head, acl_$cache_link_t *links, int16_t slot)
{
    int16_t next;
    int16_t prev;

    if (*head == ACL_CACHE_NO_SLOT) {               /* 0x00E44CA6 */
        return;
    }

    next = links[slot].next;                        /* 0x00E44CB2 */
    prev = links[slot].prev;                        /* 0x00E44CB6 */
    links[next].prev = prev;                        /* 0x00E44CC0 */
    links[prev].next = next;                        /* 0x00E44CCA */

    if (slot == *head) {                            /* 0x00E44CCE */
        if (slot == next) {
            *head = ACL_CACHE_NO_SLOT;              /* 0x00E44CD6 */
        } else {
            *head = next;                           /* 0x00E44CDC */
        }
    }
}
