/*
 * ast_$lookup_or_create_aste - Find, or build, the ASTE for a segment
 *
 * Called with the AST lock held.  For a local object on a dismounting
 * volume the call is refused through ast_$validate_uid; otherwise the
 * volume's activation count and the AOTE's reference count are held
 * while a fresh ASTE is taken, initialised (in transition + locked,
 * remote bit from the AOTE, the R/L counter bumped, segment stored) and
 * inserted into the AOTE's descending-by-segment ASTE list.  If an ASTE
 * for the segment already exists the fresh one is given back (after
 * waiting out a transition on the existing one) and the existing one
 * returned.  A new ASTE's segment map is then filled: zeroed for a remote
 * object; for a local one VTOCE_$LOOKUP_FM finds the segment's file-map
 * block (growing the AOTE's block count), FM_$READ loads the 32 entries,
 * and each entry is normalised (bit 31 becomes bit 22, bits 28..23, 30
 * and 29 cleared).  Failure unlinks and frees the ASTE; "UID not found"
 * is re-judged by ast_$validate_uid.
 *
 * Parameters (frame at 0x00E0255C, `link.w A6,-0x2c`):
 *   aote    (0x8,A6)
 *   segment (0xC,A6)  word (D3)
 *   status  (0xE,A6)  (D5)
 * (-0x14,A6) is VTOCE_$LOOKUP_FM's alloc-count output.
 *
 * Returns the ASTE in A0, or NULL.
 *
 * Original address: 0x00E0255C (736 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x408,A5) AST_$DISM_EC       (0x412,A5) ast_$vol_indices (word array)
 *   (0x420,A5) ast_$vol_info_count (0x428,A5) AST_$AST_IN_TRANS_EC
 *   (0x474,A5) AST_$ASTE_R_CNT     (0x476,A5) AST_$ASTE_L_CNT
 */

#include "ast/ast_internal.h"

aste_t *ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment,
                                    status_$t *status)
{
    aste_t *aste;               /* A2 */
    aste_t *cur;                /* A3 */
    aste_t *nxt;                /* A1 */
    aste_t *prev;               /* A0 in the unlink loop */
    uint32_t *segmap;           /* A3 */
    uint32_t *ent;              /* A0 */
    uint16_t vol_idx;           /* D2w */
    int32_t alloc_count;        /* (-0x14,A6) / D4 */
    int16_t i;

    /* 0x00E02572..0x00E025B6: a local object's volume must not be
     * dismounting; hold its activation count */
    vol_idx = 0;
    if (aote->remote_flag >= 0) {
        vol_idx = aote->vol_index;
        /* moveq #0xf / cmp / bcs, then btst.l D2,D0 / bls: bit set -> refuse */
        if (vol_idx <= 0xF && (ast_$vol_info_count & (1u << vol_idx)) != 0) {
            *status = ast_$validate_uid(&aote->uid, 0x30F00);
            return NULL;                                    /* 0x00E025A2 */
        }
        ast_$vol_indices[vol_idx]++;                        /* (0x412,A0) */
    }

    /* 0x00E025BA..0x00E025C6 */
    aote->ref_count++;
    aste = AST_$ALLOCATE_ASTE();

    /* 0x00E025C8..0x00E025DA: bset.b #7 / bclr.b #6,#5,#4 on the HIGH
     * byte of the flags word = bits 15, 14, 13, 12 */
    aste->flags |= ASTE_FLAG_IN_TRANS;
    aste->flags &= (uint16_t)~ASTE_FLAG_LOCKED;
    aste->flags &= (uint16_t)~ASTE_FLAG_DIRTY;
    aste->flags &= (uint16_t)~ASTE_FLAG_AREA;

    /* 0x00E025E0..0x00E02606: smi / lsr #7 / andi.b #~8 / lsl #3 / or.b
     * on the high byte = bit 11 := the AOTE's remote flag; count it */
    aste->flags &= (uint16_t)~ASTE_FLAG_REMOTE;
    if (aote->remote_flag < 0) {
        aste->flags |= ASTE_FLAG_REMOTE;
        AST_$ASTE_R_CNT++;
    } else {
        AST_$ASTE_L_CNT++;
    }

    /* 0x00E02608..0x00E02614 */
    aste->page_count = 0;
    aste->wire_count = 0;
    aste->aote = aote;
    aste->segment = segment;                                /* (0xc,A2) */

    /* 0x00E02618..0x00E0263A: NETLOG_$LOG_IT(0, &uid, segment, 0,
     * seg_index, 0, 0, 0) - the `clr.l` is parameters 7 and 8 */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(0, (uint32_t *)&aote->uid, segment, 0,
                       aste->seg_index, 0, 0, 0);
    }

retry_insert:
    /* 0x00E0263E..0x00E02658: an empty list, or a head below the new
     * segment (bls = segment <= head's means "not here"), takes it at
     * the head */
    cur = aote->aste_list;
    if (cur == NULL || segment > cur->segment) {
        aote->aste_list = aste;
        aste->next = cur;
        goto inserted;
    }
    for (;;) {
        /* 0x00E0265A..0x00E0265E */
        if (segment == cur->segment) {
            /* 0x00E02676..0x00E02690: an ASTE for this segment exists */
            if ((int16_t)cur->flags < 0) {
                AST_$WAIT_FOR_AST_INTRANS();
                goto retry_insert;
            }
            AST_$FREE_ASTE(aste);
            *status = status_$ok;
            aste = cur;
            goto release_holds;                             /* 0x00E027EE */
        }
        /* 0x00E02660..0x00E0266E: insert after `cur` when the list ends
         * or the next entry is below the new segment (bhi) */
        nxt = cur->next;
        if (nxt == NULL || segment > nxt->segment) {
            break;
        }
        cur = nxt;
    }
    /* 0x00E02670..0x00E02672 */
    aste->next = cur->next;
    cur->next = aste;

inserted:
    /* 0x00E02694..0x00E026A6 */
    aote->status_flags++;
    segmap = (uint32_t *)((char *)SEGMAP_BASE +
                          ((uint32_t)aste->seg_index << 7) - 0x80);

    /* 0x00E026AA */
    if (aote->remote_flag < 0) {
        /* 0x00E0277A..0x00E02786: moveq #0x1f / dbf = 32 longwords */
        ent = segmap;
        for (i = 0x1F; i >= 0; i--) {
            *ent++ = 0;
        }
        *status = status_$ok;
    } else {
        /* 0x00E026B2..0x00E026DC: drop the AST lock for the I/O.  The
         * third argument is pushed as a single `st` byte; VTOCE_$LOOKUP_FM
         * never reads that slot. */
        ML_$UNLOCK(AST_LOCK_ID);
        VTOCE_$LOOKUP_FM(&aote->obj_uid, segment, (uint16_t)-1,
                         &aste->fm_block, (uint32_t *)&alloc_count, status);

        /* 0x00E026E0..0x00E02732 */
        if (*status == status_$ok) {
            if (alloc_count != 0) {
                /* 0x00E026EE..0x00E02716: newly allocated blocks are
                 * added to aote+0x24 under the PMAP lock, AOTE dirty */
                ML_$LOCK(PMAP_LOCK_ID);
                aote->unknown_24 += (uint32_t)alloc_count;
                aote->flags |= AOTE_FLAG_DIRTY;             /* bset.b #5 */
                ML_$UNLOCK(PMAP_LOCK_ID);
            }
            /* 0x00E02718..0x00E02732: FM_$READ(&obj_loc, fm_block,
             * segment, segmap, status) */
            FM_$READ((fm_$file_ref_t *)(void *)&aote->obj_uid, aste->fm_block,
                     segment, (fm_$entry_t *)(void *)segmap, status);
        }

        /* 0x00E02736..0x00E02742 */
        ML_$LOCK(AST_LOCK_ID);

        /* 0x00E02744..0x00E02778: normalise the 32 entries.  `tst.w` on
         * the high word = bit 31 -> clear it and set bit 22; then clear
         * bits 29, 31, 30 and, with `andi.w #0xE07F` on the high word,
         * bits 28..23. */
        if (*status == status_$ok) {
            ent = segmap;
            for (i = 0x1F; i >= 0; i--) {
                if ((int32_t)*ent < 0) {
                    *ent &= ~SEGMAP_IN_TRANS;
                    *ent |= 0x00400000u;
                }
                *ent &= ~SEGMAP_WIRED;
                *ent &= ~SEGMAP_IN_TRANS;
                *ent &= ~SEGMAP_VALID;
                *ent &= 0xE07FFFFFu;
                ent++;
            }
        }
    }

    /* 0x00E02788..0x00E0278C */
    if (*status == status_$ok) {
        /* 0x00E027DC..0x00E027EC: bit 15 off, wake waiters */
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        goto release_holds;
    }

    /* 0x00E0278E..0x00E027A8: "UID not found" is re-judged */
    if (*status == 0x20006) {
        *status = ast_$validate_uid(&aote->uid, *status);
    }

    /* 0x00E027AA..0x00E027C6: unlink */
    if (aote->aste_list == aste) {
        aote->aste_list = aste->next;
    } else {
        prev = aote->aste_list;
        while (prev->next != aste) {
            prev = prev->next;
        }
        prev->next = aste->next;
    }

    /* 0x00E027C8..0x00E027D8 */
    aote->status_flags--;
    AST_$FREE_ASTE(aste);
    aste = NULL;

release_holds:
    /* 0x00E027EE..0x00E02826: drop the volume hold; the last one out of
     * a dismounting volume wakes AST_$DISMOUNT */
    if (aote->remote_flag >= 0) {
        ast_$vol_indices[vol_idx]--;
        if (ast_$vol_indices[vol_idx] == 0 && vol_idx <= 0xF &&
            (ast_$vol_info_count & (1u << vol_idx)) != 0) {
            EC_$ADVANCE(&AST_$DISM_EC);
        }
    }

    /* 0x00E02828..0x00E02830 */
    aote->ref_count--;
    return aste;
}
