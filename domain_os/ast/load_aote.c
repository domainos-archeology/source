/*
 * AST_$LOAD_AOTE - Refresh or create an AOTE from a fetched attribute set
 *
 * If the object is already active, its attribute block (aote+0x0C, 36
 * longwords) is overwritten from `attrs` with the DTV (0x38/0x3C)
 * preserved and the TOUCHED flag cleared.  Otherwise a fresh AOTE is
 * built: after allocating, a change of AST_$AOTE_SEQN triggers a re-check
 * of the hash chain (a matching entry wins and the fresh one is
 * released, in-transition or not); a dismounting volume also gives the
 * AOTE back.  The obj_loc record is copied to aote+0x9C, the object is
 * remote when its node differs from NODE_$ME (in which case the network
 * is installed and the location word built from it, else the location is
 * the block hint), the attributes are copied and the AOTE is put on the
 * hash chain.  Nothing here sets the in-transition bit - the entry is
 * visible as soon as it is linked.
 *
 * Parameters (frame at 0x00E0238C, `link.w A6,-0x1c`):
 *   attrs    (0x8,A6)  36 longwords (D4)
 *   obj_info (0xC,A6)  a file_$obj_loc_t (A3): uid +0x08, block_hint +0x04,
 *                      loc_info +0x10, node +0x14, rights_bits +0x1C (the
 *                      volume index); the public prototype keeps the
 *                      callers' uint32_t * shape
 * Locals: (-0x4) NETWORK_$INSTALL_NET's status, (-0x10)/(-0xC) saved DTV.
 *
 * Original address: 0x00E0238C (384 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x0,A5,D0w) AOTH  (0x420,A5) ast_$vol_info_count  (0x434,A5) AST_$AOTE_SEQN
 */

#include "ast/ast_internal.h"
#include "node/node.h"
#include "network/network.h"

/* The AOTE hash table, `AOTH` in the SAU2 map. */
/* TODO(source-gmxj): the AST_ segment and the AST/AOT tables are still absolute on the target (tools/check_guards.py exemption). */
#if defined(ARCH_M68K)
#define AST_AOTH_BASE ((aote_t **)0xE1DC80)
#else
#define AST_AOTH_BASE ast_aoth_base
#endif

/*
 * UID_$HASH's table-size word: `pea (-0x810,PC)` at 0x00E023FA ->
 * 0x00E01BEC, image bytes 00 FB (251 buckets).  Shared with
 * ast_$lookup_aote_by_uid and ast_$force_activate_segment.
 */
static const uint16_t ast_$aoth_hash_size_00e01bec = 0x00FB;

void AST_$LOAD_AOTE(uint32_t *attrs, uint32_t *obj_info)
{
    file_$obj_loc_t *loc = (file_$obj_loc_t *)(void *)obj_info;    /* A3 */
    aote_t *aote;               /* A2 */
    aote_t *existing;           /* A0 */
    uint32_t seqn_before;       /* D2 */
    uint16_t hash_index;        /* D3w */
    uint32_t saved_dtv_high;    /* (-0x10,A6) */
    uint16_t saved_dtv_low;     /* (-0xC,A6) */
    uint32_t node;              /* D2 */
    uint16_t vol_idx;           /* D0w */
    status_$t status;           /* (-0x4,A6) */
    uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* 0x00E0239E..0x00E023AE */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E023B0..0x00E023BE: by the UID at obj_info+0x08 */
    existing = ast_$lookup_aote_by_uid(&loc->uid);
    if (existing != NULL) {
        /* 0x00E023C0..0x00E023EC: refresh in place, keeping the DTV */
        saved_dtv_high = existing->dtv_high;
        saved_dtv_low = existing->dtv_low;
        src = attrs;
        dst = (uint32_t *)&existing->obj_type;
        for (i = 0x23; i >= 0; i--) {
            *dst++ = *src++;
        }
        existing->dtv_high = saved_dtv_high;
        existing->dtv_low = saved_dtv_low;
        existing->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;     /* bclr.b #4 */
        goto unlock;
    }

    /* 0x00E023F0..0x00E0240A */
    seqn_before = AST_$AOTE_SEQN;
    aote = ast_$allocate_aote();
    hash_index = (uint16_t)UID_$HASH(&loc->uid,
                                     (uint16_t *)&ast_$aoth_hash_size_00e01bec);

    /* 0x00E0240C..0x00E02440: another activation ran meanwhile - is the
     * object on the chain now?  (No wait for an in-transition match.) */
    if (seqn_before != AST_$AOTE_SEQN) {
        for (existing = AST_AOTH_BASE[hash_index]; existing != NULL;
             existing = existing->hash_next) {
            if (existing->uid.high == loc->uid.high &&
                existing->uid.low == loc->uid.low) {
                goto release;                               /* 0x00E0242E */
            }
        }
    }

    /* 0x00E02442..0x00E02454: a volume index of at most 15 whose bit is
     * set in ast_$vol_info_count is dismounting (btst / bhi) */
    vol_idx = (uint8_t)loc->rights_bits;
    if (vol_idx <= 0xF && (ast_$vol_info_count & (1u << vol_idx)) != 0) {
        goto release;
    }

    /* 0x00E02456..0x00E02476: three bclr (bits 7, 6, 5 - bit 4 is kept) */
    AST_$AOTE_SEQN++;
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;
    aote->flags &= (uint8_t)~AOTE_FLAG_DIRTY;
    aote->ref_count = 0;
    aote->status_flags = 0;
    aote->hash_next = NULL;
    aote->aste_list = NULL;

    /* 0x00E0247A..0x00E02484: moveq #0x7 / dbf = 8 longwords to aote+0x9C */
    src = obj_info;
    dst = (uint32_t *)&aote->obj_uid;
    for (i = 0x7; i >= 0; i--) {
        *dst++ = *src++;
    }

    /*
     * 0x00E02488..0x00E024A2: remote iff obj_loc.node != NODE_$ME; the
     * verdict becomes bit 7 of obj_loc.flags (aote+0xB9), other bits kept.
     * The `bpl` at 0x00E024A2 tests the result of the or.b.
     */
    node = loc->node;
    aote->remote_flag = (int8_t)((aote->remote_flag & 0x7F) |
                                 ((node != NODE_$ME) ? 0x80 : 0x00));
    if (aote->remote_flag < 0) {
        /* 0x00E024A4..0x00E024BE: install the network; failure gives the
         * AOTE back (the caller never learns why) */
        NETWORK_$INSTALL_NET(loc->loc_info, &aote->location, &status);
        if (status != status_$ok) {
            goto release;
        }
        /* 0x00E024C2..0x00E024CE: keep the network bits INSTALL_NET
         * wrote, add the node, set bit 31 */
        aote->location &= 0xFFF00000u;
        aote->location |= node;
        aote->location |= 0x80000000u;
    } else {
        /* 0x00E024D6: local - the block hint is the location */
        aote->location = loc->block_hint;
    }

    /* 0x00E024DC..0x00E024E6: 36 longwords to aote+0x0C */
    src = attrs;
    dst = (uint32_t *)&aote->obj_type;
    for (i = 0x23; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E024EA..0x00E024F2: onto the head of the bucket */
    aote->hash_next = AST_AOTH_BASE[hash_index];
    AST_AOTH_BASE[hash_index] = aote;
    goto unlock;

release:
    /* 0x00E0242E..0x00E02436 */
    ast_$release_aote(aote);

unlock:
    /* 0x00E024F6..0x00E024FC */
    ML_$UNLOCK(AST_LOCK_ID);
}
