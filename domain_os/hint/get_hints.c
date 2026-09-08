/*
 * HINT_$GET_HINTS - Get hints for a remote file
 *
 * Looks the low 20 bits of a UID up in the hint file's hash table and copies
 * the matching slot's (flags, node_id) pairs into the caller's buffer, then
 * appends the object's own node (unless one of the hints already named it)
 * and finally this node.  Returns the number of pairs written.
 *
 * Both walks ASCEND.  The image holds the slot pointer biased by +0x10 (the
 * key is read at (-0x10,A1)) and the address pointer biased by +0x10 as well,
 * and steps them forward with "lea (0x1c,A0),A0" (0x00E499E6) and
 * "addq.l #0x8,A2" (0x00E499DE), so slot 0 is examined first and addrs[0] is
 * emitted first.
 *
 * Address ranges (SR10.2 SAU2 image):
 *   0x00E49966-0x00E4998C  prologue, key extraction, the two early exits
 *   0x00E4998E-0x00E499A6  bucket address, biased slot pointer
 *   0x00E499A8-0x00E499EA  ascending slot scan (moveq #2 / dbf: 3 slots)
 *   0x00E499B0-0x00E499E4  ascending address copy (moveq #2 / dbf: 3 pairs)
 *   0x00E499EE-0x00E49A0A  optional self entry when key > 4
 *   0x00E49A0C-0x00E49A1C  the trailing NODE_$ME entry
 *   0x00E49A20-0x00E49A2A  return the count in D0.w
 *
 * Original address: 0x00E49966 (198 bytes)
 */

#include "hint/hint_internal.h"

int16_t HINT_$GET_HINTS(uid_t *lookup_uid, uint32_t *addresses)
{
    uint32_t uid_key;           /* D0 */
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    uint32_t *out_ptr;          /* A1, biased by +8 in the image */
    int16_t count;              /* D1w */
    int16_t i;
    int16_t j;
    int8_t found_self;          /* D2b */

    /* 0x00E49972-0x00E49974 */
    count = 1;
    found_self = 0;

    /* 0x00E4997A-0x00E49984: the key is the low longword masked to 20 bits */
    uid_key = lookup_uid->low & HINT_UID_MASK;

    if (uid_key == 0) {
        goto finish;
    }

    /* 0x00E49986-0x00E4998C */
    if (HINT_$HINTFILE_PTR == NULL) {
        goto finish;
    }

    hintfile = HINT_$HINTFILE_PTR;

    /*
     * 0x00E4998E-0x00E499A2: "moveq #0x3f,D3 / and.w D0w,D3w" then
     * "moveq #0x54,D5 / muls.w D3w,D5" - the bucket stride is 0x54 and the
     * hash is the low 6 bits of the key.
     */
    bucket = &hintfile->buckets[uid_key & HINT_HASH_MASK];

    /*
     * 0x00E499A8-0x00E499EA: "moveq #0x2,D3" with "dbf D3w" walks the three
     * slots, and "lea (0x1c,A0),A0" steps FORWARD, so slot 0 is tried first.
     */
    for (i = 0; i < HINT_SLOTS_PER_BUCKET; i++) {
        slot = &bucket->slots[i];

        /* 0x00E499AA "cmp.l (-0x10,A1),D0" */
        if (uid_key == slot->uid_low_masked) {
            /* 0x00E499B0-0x00E499B8 */
            out_ptr = addresses;

            /*
             * 0x00E499BC-0x00E499E0: "moveq #0x2,D3" with "dbf D3w" again,
             * and "addq.l #0x8,A2" steps FORWARD, so addrs[0] comes first.
             */
            for (j = 0; j < HINT_ADDRS_PER_SLOT; j++) {
                /* 0x00E499BE "tst.l (-0x10,A0)": an empty node ends the walk
                 * by branching straight to the tail at 0x00E499EE. */
                if (slot->addrs[j].node_id == 0) {
                    goto finish;
                }

                /* 0x00E499C4-0x00E499CC: one "lea" and two "(A4)+" moves copy
                 * the flags and the node id in that order. */
                *out_ptr++ = slot->addrs[j].flags;
                *out_ptr++ = slot->addrs[j].node_id;

                /*
                 * 0x00E499D0-0x00E499D8: "tst.b D2b / bmi" then "seq D2b".
                 * Once the flag is set the test is skipped, so the seq only
                 * ever runs while the flag is still clear - clearing it again
                 * on a mismatch is a no-op.
                 */
                if (found_self >= 0) {
                    found_self = (uid_key == slot->addrs[j].node_id)
                                     ? (int8_t)0xFF : 0;
                }

                /* 0x00E499DA */
                count++;
            }

            /* 0x00E499E4 */
            goto finish;
        }
    }

finish:
    /*
     * 0x00E499EE-0x00E49A0A.  "cmpi.l #0x4,D0 / bls" is an UNSIGNED compare,
     * so keys 0..4 never get an entry of their own.  The pair goes at index
     * count-1 (the image forms count*8 and writes at -8 and -4 from it).
     */
    if (found_self >= 0 && uid_key > 4) {
        addresses[(count - 1) * 2] = 0;
        addresses[(count - 1) * 2 + 1] = uid_key;
        count++;
    }

    /*
     * 0x00E49A0C-0x00E49A1C: the trailing local-node entry, written at index
     * count-1 as well.  It does NOT bump the count, so the returned value is
     * the index of that final entry plus one.
     */
    addresses[(count - 1) * 2] = 0;
    addresses[(count - 1) * 2 + 1] = NODE_$ME;

    /* 0x00E49A20 "move.w D1w,D0w" */
    return count;
}
