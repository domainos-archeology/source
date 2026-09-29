/*
 * AST_$RESERVE - Make sure a run of an object's pages has disk blocks
 *
 * A remote object's request is forwarded to its home node with
 * REM_FILE_$RESERVE.  For a local object the segments holding pages
 * start_page .. start_page+page_count-1 are visited from the LAST one
 * down: the segment's ASTE is found or created, marked in transition and
 * locked (bits 15 and 14 - only bit 15 is cleared afterwards), and under
 * the PMAP lock its map entries for the pages in range are walked
 * upwards.  A run of consecutive entries with neither a page nor a disk
 * address is marked in transition and handed to ast_$setup_page_read
 * (flags 0x40), which allocates their blocks; the transition bits are
 * then cleared.  A setup failure ends the whole call.
 *
 * Parameters (frame at 0x00E0677E, `link.w A6,-0x2c`):
 *   uid        (0x08,A6)  (A2)
 *   start_page (0x0C,A6)  longword (D6) - a PAGE number (`lsr.l #5`
 *                         yields the segment), despite the old names
 *   page_count (0x10,A6)  longword (D2)
 *   status     (0x14,A6)
 * Locals: (-0x8) the aote+0xAC/+0xB0 pair for REM_FILE, (-0x18) the
 * zero location, (-0x1C) aote, (-0x26) the segment being visited.
 *
 * Original address: 0x00E0677E (588 bytes), A5 = 0xE1DC80 (AST_ block;
 * (0x428,A5) = AST_$AST_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"

void AST_$RESERVE(uid_t *uid, uint32_t start_page, uint32_t page_count,
                  status_$t *status)
{
    aote_t *aote;               /* (-0x1C,A6) */
    aste_t *aste;               /* A3 */
    uint32_t location;          /* (-0x18,A6) */
    uint32_t net_node[2];       /* (-0x8,A6) */
    uint32_t page;              /* D4: first page still to do in the segment */
    uint16_t seg;               /* (-0x26,A6) */
    uint16_t last_in_seg;       /* D5w */
    uint16_t cur;               /* D3w: page within the segment */
    uint16_t run;               /* D2w */
    uint32_t *entry;            /* A2 */
    uint32_t *ent;              /* A0 */
    int16_t i;

    /* 0x00E06798..0x00E067B0 */
    *status = status_$ok;
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E067B2..0x00E067E6 */
    aote = ast_$lookup_aote_by_uid(uid);
    if (aote == NULL) {
        location = 0;
        aote = ast_$force_activate_segment(uid, location, status, 0);
        if (aote == NULL) {
            goto unlock;                                /* 0x00E06856 */
        }
    } else {
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E067E8 */
    if (aote->remote_flag < 0) {
        /* 0x00E06988..0x00E069B6: pushes status, count, start, uid,
         * &net_node */
        net_node[0] = aote->obj_loc_net;
        net_node[1] = aote->obj_loc_node;
        ML_$UNLOCK(AST_LOCK_ID);
        REM_FILE_$RESERVE((uid_t *)net_node, uid, start_page, page_count,
                          status);
        goto inhibit_end;                               /* 0x00E069BA */
    }

    /* 0x00E067F0..0x00E06806: the last page, its segment and offset */
    aote->flags |= AOTE_FLAG_IN_TRANS;
    page = start_page + page_count - 1;
    seg = (uint16_t)(page >> 5);
    last_in_seg = (uint16_t)(page & 0x1F);

    for (;;) {
        /* 0x00E06808..0x00E0683E */
        aste = ast_$lookup_aste(aote, (int16_t)seg);
        if (aste == NULL) {
            aste = ast_$lookup_or_create_aste(aote, seg, status);
            if (aste == NULL) {
                goto aote_done;                         /* 0x00E06840 */
            }
        }

        /* 0x00E06868..0x00E06872: the segment's first page, or the start
         * page when that is higher (signed compare) */
        page -= last_in_seg;
        if ((int32_t)page < (int32_t)start_page) {
            page = start_page;
        }

        /* 0x00E06874..0x00E0689A: bits 15 and 14, swap the locks */
        aste->flags |= ASTE_FLAG_IN_TRANS;
        aste->flags |= ASTE_FLAG_LOCKED;
        ML_$UNLOCK(AST_LOCK_ID);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E0689C..0x00E068B6 */
        cur = (uint16_t)(page & 0x1F);
        entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                             + (uint16_t)(cur << 2));

        /* 0x00E068C0..0x00E0693C: pages cur .. last_in_seg.  The body is
         * entered unconditionally (`bra.b 0x00e068c0` at 0x00E068BA); the
         * `cmp.w D5w,D3w / bls` test at 0x00E0693A only runs after it, so
         * this is a do-while. */
        do {
            while ((int32_t)*entry < 0) {
                ast_$wait_for_page_transition();
            }
            /* 0x00E068C4..0x00E068DA: an installed page or one with a
             * disk address needs nothing */
            if ((*entry & SEGMAP_VALID) || (*entry & 0x7FFFFF) != 0) {
                entry++;
                cur++;
                continue;
            }

            /*
             * 0x00E068DC..0x00E06904: mark the run in transition.  The
             * count stops at last_in_seg - cur + 1 entries (cmp.w D2,D0 /
             * bcs), at an entry in transition, an installed one, or one
             * with a disk address.
             */
            run = 0;
            ent = entry;
            do {
                *ent |= SEGMAP_IN_TRANS;                /* bset.b #7,(A0)+ */
                ent++;
                run++;
                if ((uint16_t)(last_in_seg - cur) < run) {
                    break;
                }
                if ((int32_t)*ent < 0) {
                    break;
                }
                if (*ent & SEGMAP_VALID) {
                    break;
                }
            } while ((*ent & 0x7FFFFF) == 0);

            /* 0x00E06906..0x00E0691C: pushes status, #0x40, run, cur,
             * entry, aste */
            ast_$setup_page_read(aste, entry, cur, run, 0x40, status);

            /* 0x00E06920..0x00E0692C: clear bit 31 on the run; A2 is left
             * past it */
            for (i = 0; i < (int16_t)run; i++) {
                *entry &= ~SEGMAP_IN_TRANS;
                entry++;
            }

            /* 0x00E06930..0x00E06938 */
            if (*status != status_$ok) {
                break;
            }
            cur = (uint16_t)(cur + run);
        } while (cur <= last_in_seg);                   /* 0x00E0693A */

        /* 0x00E0693E..0x00E0696A: locks back, bit 15 only, wake waiters */
        ML_$UNLOCK(PMAP_LOCK_ID);
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

        /* 0x00E0696C..0x00E06984: stop on error or once the start page
         * is reached (unsigned); else the previous segment, all of it */
        if (*status != status_$ok) {
            break;
        }
        if (page <= start_page) {
            break;
        }
        page -= 1;
        last_in_seg = 0x1F;
        seg--;
    }

aote_done:
    /* 0x00E06840..0x00E06854 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

unlock:
    /* 0x00E06856..0x00E06862 */
    ML_$UNLOCK(AST_LOCK_ID);

inhibit_end:
    /* 0x00E069BA */
    PROC1_$INHIBIT_END();
}
