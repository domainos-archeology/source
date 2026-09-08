/*
 * HINT_$add_internal - Add a hint to the hint file (module-local)
 *
 * Inserts (or refreshes) the (flags, node_id) pair for a UID in the hint
 * file's hash table.  Callers hold HINT_$EXCLUSION_LOCK; every one of them
 * reaches this routine with "bsr.w", so A5 (and therefore HINT_$BUCKET_INDEX
 * at globals+0x26) is inherited from the public entry point - this function
 * never loads A5 itself.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E49A2C-0x00E49A4A  prologue and the two early exits
 *   0x00E49A4E-0x00E49A76  key, bucket address, biased slot pointer
 *   0x00E49A7C-0x00E49AE0  ascending slot scan (moveq #2 / dbf: 3 slots),
 *                          with the match body at 0x00E49A90-0x00E49ACA and
 *                          the free-slot bookkeeping at 0x00E49ACE
 *   0x00E49AE4-0x00E49AF4  the self-hint rejection
 *   0x00E49AF6-0x00E49B10  round-robin victim when no slot was free
 *   0x00E49B12-0x00E49B32  slot address (0x1C * index) and the first pair
 *   0x00E49B34-0x00E49B60  the second pair, and the recursive reverse hint
 *   0x00E49B62-0x00E49B72  clear the third pair, epilogue
 *
 * Original address: 0x00E49A2C (328 bytes)
 */

#include "hint/hint_internal.h"

/*
 * The slot layout the offsets above pin down.  The image holds the slot
 * pointer biased by +0x10 (the key is read at (-0x10,A0)) and reaches the
 * three address pairs at (-0xc,A0)/(-0x8,A0), (-0x4,A0)/(A0) and
 * (0x4,A0)/(0x8,A0), i.e. slot+0x04/0x08, +0x0C/0x10 and +0x14/0x18.
 */
_Static_assert(__builtin_offsetof(hint_slot_t, uid_low_masked) == 0x00,
               "hint_slot_t.uid_low_masked at +0x00");
_Static_assert(__builtin_offsetof(hint_slot_t, addrs[0]) == 0x04,
               "hint_slot_t.addrs[0] at +0x04");
_Static_assert(__builtin_offsetof(hint_slot_t, addrs[1]) == 0x0C,
               "hint_slot_t.addrs[1] at +0x0C");
_Static_assert(__builtin_offsetof(hint_slot_t, addrs[2]) == 0x14,
               "hint_slot_t.addrs[2] at +0x14");
_Static_assert(sizeof(hint_slot_t) == 0x1C,
               "slot stride: lea (0x1c,A1),A1 at 0x00E49ADC");
_Static_assert(sizeof(hint_bucket_t) == 0x54,
               "bucket stride: moveq #0x54,D2 / muls.w at 0x00E49A5C");

void HINT_$add_internal(uid_t *uid_ptr, hint_addr_t *addresses)
{
    uint32_t uid_key;           /* D3, recomputed inside the scan */
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    int16_t i;
    int16_t free_slot;          /* D1w, Pascal 1-based; 0 = none seen */
    int16_t search_idx;         /* D2w */

    /* 0x00E49A3C-0x00E49A4A */
    if (addresses->node_id == 0) {
        return;
    }
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    hintfile = HINT_$HINTFILE_PTR;

    /* 0x00E49A50-0x00E49A6C */
    uid_key = uid_ptr->low & HINT_UID_MASK;
    bucket = &hintfile->buckets[uid_key & HINT_HASH_MASK];

    /* 0x00E49A56 / 0x00E49A70 */
    free_slot = 0;
    search_idx = 1;

    /*
     * 0x00E49A7C-0x00E49AE0: "moveq #0x2,D0" with "dbf D0w" walks the three
     * slots and "lea (0x1c,A1),A1" (0x00E49ADC) steps FORWARD, so slot 0 is
     * examined first.
     */
    for (i = 0; i < HINT_SLOTS_PER_BUCKET; i++) {
        slot = &bucket->slots[i];

        /* 0x00E49A80-0x00E49A8A: the key is re-masked on every pass. */
        uid_key = uid_ptr->low & HINT_UID_MASK;

        if (uid_key == slot->uid_low_masked) {
            /*
             * 0x00E49A90-0x00E49A9E: the pair is already first - refresh its
             * flags and stop.
             */
            if (slot->addrs[0].node_id == addresses->node_id) {
                slot->addrs[0].flags = addresses->flags;
                return;
            }

            /*
             * 0x00E49AA2-0x00E49AB4: "cmp.l (0x4,A2),D6 / beq.b 0x00E49AB6"
             * SKIPS the copy when addrs[1] already names the new node, so the
             * shift down to addrs[2] happens only when they DIFFER.  (If
             * addrs[1] is the node being promoted there is nothing to keep.)
             */
            if (slot->addrs[1].node_id != addresses->node_id) {
                slot->addrs[2].flags = slot->addrs[1].flags;
                slot->addrs[2].node_id = slot->addrs[1].node_id;
            }

            /* 0x00E49AB6-0x00E49AC8: shift addrs[0] down and insert. */
            slot->addrs[1].flags = slot->addrs[0].flags;
            slot->addrs[1].node_id = slot->addrs[0].node_id;
            slot->addrs[0].flags = addresses->flags;
            slot->addrs[0].node_id = addresses->node_id;
            return;
        }

        /* 0x00E49ACE-0x00E49AD8: remember the FIRST empty slot only. */
        if (free_slot == 0 && slot->uid_low_masked == 0) {
            free_slot = search_idx;
        }

        /* 0x00E49ADA */
        search_idx++;
    }

    /*
     * 0x00E49AE4-0x00E49AF4: a hint that just says "the object lives on its
     * own node" carries nothing, so it is dropped when the flags word is zero
     * or is this node's own network port.
     */
    if (uid_key == addresses->node_id) {
        if (addresses->flags == 0 || addresses->flags == ROUTE_$PORT) {
            return;
        }
    }

    /* 0x00E49AF6-0x00E49B10: round-robin victim, index cycling 1..3 */
    if (free_slot == 0) {
        free_slot = HINT_$BUCKET_INDEX;
        if (HINT_$BUCKET_INDEX == HINT_SLOTS_PER_BUCKET) {
            HINT_$BUCKET_INDEX = 1;
        } else {
            HINT_$BUCKET_INDEX++;
        }
    }

    /*
     * 0x00E49B12-0x00E49B22: the image forms 0x1C * free_slot with
     * "lsl.l #0x2 / lsl.l #0x3 / neg.l / add.l" (32n - 4n) and adds it to the
     * bucket base, then reads the slot through a -0x10 bias - i.e. the
     * 1-based index selects slots[free_slot - 1].
     */
    slot = &bucket->slots[free_slot - 1];

    /* 0x00E49B26-0x00E49B32 */
    slot->uid_low_masked = uid_key;
    slot->addrs[0].flags = addresses->flags;
    slot->addrs[0].node_id = addresses->node_id;

    /* 0x00E49B34-0x00E49B38 */
    if (uid_key == addresses->node_id) {
        /* 0x00E49B5C-0x00E49B60 */
        slot->addrs[1].flags = 0;
        slot->addrs[1].node_id = 0;
    } else {
        /*
         * 0x00E49B3A-0x00E49B3E: the object's own node becomes the second
         * hint, carrying the same flags.
         */
        slot->addrs[1].node_id = uid_key;
        slot->addrs[1].flags = addresses->flags;

        /*
         * 0x00E49B40-0x00E49B58: the reverse hint.  The 8-byte UID the image
         * hands to the recursive call lives at A6-0x10 and is NEVER fully
         * initialised:
         *
         *   0x00E49B40  andi.l #-0x100000,(-0xc,A6)   ; low &= 0xFFF00000
         *   0x00E49B48  move.l (0x4,A2),D7
         *   0x00E49B4C  or.l D7,(-0xc,A6)             ; low |= node_id
         *   0x00E49B52  pea (-0x10,A6)                ; &temp uid
         *
         * The high longword at A6-0x10 is never written at all, and the low
         * longword is built out of whatever the frame slot already held.
         * Reproduced as found: only the low 20 bits are ever consumed (the
         * callee masks with 0xFFFFF at 0x00E49A58 and 0x00E49A86), so the
         * stale bits do not change the outcome.
         */
        {
            uid_t recursive_uid;    /* A6-0x10, deliberately uninitialised */

            recursive_uid.low =
                (recursive_uid.low & ~(uint32_t)HINT_UID_MASK) |
                addresses->node_id;

            HINT_$add_internal(&recursive_uid, addresses);
        }
    }

    /* 0x00E49B62-0x00E49B66 */
    slot->addrs[2].flags = 0;
    slot->addrs[2].node_id = 0;
}
