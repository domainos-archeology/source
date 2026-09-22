/*
 * ast_$process_aote - Deactivate an object: free its ASTEs, purify it,
 *                     take it off the hash chain
 *
 * Refuses (status "segment is not deactivatable") an AOTE that is in
 * transition or still referenced, and - unless `keep` is TRUE - a type-2
 * object with attribute-flags bit 1 set that is local.  Otherwise, with
 * the AOTE marked in transition, every ASTE on its list is deactivated
 * with AST_$DEACTIVATE_SEGMENT(purge, keep) and freed; an ASTE that is
 * itself in transition is waited for when `wait` is TRUE (else
 * DEACTIVATE_SEGMENT gets it anyway).  Unless `purge` is TRUE the AOTE is
 * then purified; finally it is unlinked from its hash bucket.  On a
 * failure the status gets bit 31 (except "not deactivatable", which is
 * passed through), the in-transition bit is cleared and waiters woken.
 *
 * Parameters (frame at 0x00E01AD2, `link.w A6,-0x10`):
 *   aote   (0x08,A6)
 *   purge  (0x0C,A6)  BOOLEAN byte (D2b): skip the purify pass; also
 *                     DEACTIVATE_SEGMENT's `purge`
 *   keep   (0x0E,A6)  BOOLEAN byte (D3b): deactivate even a protected
 *                     type-2 object; also DEACTIVATE_SEGMENT's `keep`
 *   wait   (0x10,A6)  BOOLEAN byte (D4b): wait for an in-transition ASTE
 *   status (0x12,A6)  (A2)
 *
 * Returns D0w: on the refusal exit its low byte is the busy/in-transition
 * byte computed at 0x00E01AF0..0x00E01AFC; on the other exits D0 is
 * whatever the last callee left.  Every caller ignores it.
 *
 * Original address: 0x00E01AD2 (282 bytes).  A5 is inherited (every
 * caller is AST code; 0xE1DC80): (0x0,A5,D1w) is the AOTH and (0x428,A5)
 * AST_$AST_IN_TRANS_EC.
 */

#include "ast/ast_internal.h"

/* The AOTE hash table, `AOTH` in the SAU2 map. */
#if defined(ARCH_M68K)
#define AST_AOTH_BASE ((aote_t **)0xE1DC80)
#else
#define AST_AOTH_BASE ast_aoth_base
#endif

/*
 * UID_$HASH's table-size word: `pea (0x5a,PC)` at 0x00E01B90 ->
 * 0x00E01BEC, image bytes 00 FB (251 buckets).  Shared with
 * ast_$lookup_aote_by_uid, ast_$force_activate_segment and AST_$LOAD_AOTE.
 */
static const uint16_t ast_$aoth_hash_size_00e01bec = 0x00FB;

uint16_t ast_$process_aote(aote_t *aote, boolean purge, boolean keep,
                           boolean wait, status_$t *status)
{
    uint8_t busy;               /* D0b: smi(flags) | sne(ref_count) */
    aste_t *aste;               /* A3 */
    uint16_t hash_index;        /* D0w -> D1w */
    aote_t *prev;               /* A0 */

    /* 0x00E01AEA */
    *status = status_$ok;

    /* 0x00E01AF0..0x00E01AFE: 0xFF when in transition or referenced */
    busy = (uint8_t)(((int8_t)aote->flags < 0 ? 0xFF : 0x00) |
                     (aote->ref_count != 0 ? 0xFF : 0x00));
    if ((int8_t)busy < 0) {
        goto not_deactivatable;                     /* 0x00E01B1E */
    }

    /* 0x00E01B00..0x00E01B1C: a protected local type-2 object, unless
     * `keep` says otherwise */
    if (keep >= 0 && aote->sub_type == 2 && (aote->attr_flags_lo & 0x02) &&
        aote->remote_flag >= 0) {
        goto not_deactivatable;
    }

    /* 0x00E01B28 */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* 0x00E01B6E..0x00E01B76 with 0x00E01B30..0x00E01B6C: drain the list */
    while (aote->aste_list != NULL) {
        aste = aote->aste_list;
        /* 0x00E01B38..0x00E01B46: in transition and told to wait */
        if ((int16_t)aste->flags < 0 && wait < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            continue;
        }
        /* 0x00E01B48..0x00E01B54: `move.b D3b` / `move.b D2b` - two
         * single-byte arguments */
        AST_$DEACTIVATE_SEGMENT(aste, purge, keep, status);
        /* 0x00E01B58..0x00E01B64 */
        if (*status != status_$ok) {
            if (*status == status_$ast_segment_not_deactivatable) {
                goto clear_in_trans;                /* 0x00E01BCE */
            }
            goto fail;                              /* 0x00E01BCA */
        }
        /* 0x00E01B66..0x00E01B6C */
        AST_$FREE_ASTE(aste);
    }

    /* 0x00E01B78..0x00E01B8E: `clr.w -(SP)` is purify_aote's byte flag */
    if (purge >= 0) {
        ast_$purify_aote(aote, 0, status);
        if (*status != status_$ok) {
            goto fail;
        }
    }

    /* 0x00E01B90..0x00E01BC8: hash on obj_loc.uid (aote+0xA4) and unlink;
     * the in-transition bit is NOT cleared on this exit */
    hash_index = (uint16_t)UID_$HASH(&aote->obj_loc_uid,
                                     (uint16_t *)&ast_$aoth_hash_size_00e01bec);
    prev = AST_AOTH_BASE[hash_index];
    if (prev == aote) {
        AST_AOTH_BASE[hash_index] = aote->hash_next;
    } else {
        while (prev->hash_next != aote) {
            prev = prev->hash_next;
        }
        prev->hash_next = aote->hash_next;
    }
    return busy;

not_deactivatable:
    /* 0x00E01B1E..0x00E01B24 */
    *status = status_$ast_segment_not_deactivatable;
    return busy;

fail:
    /* 0x00E01BCA: bset.b #0x7,(A2) = bit 31 of the status */
    *status |= (status_$t)0x80000000u;

clear_in_trans:
    /* 0x00E01BCE..0x00E01BDC */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    return busy;
}
