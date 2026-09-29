/*
 * AST_$PURIFY - Write an object's modified pages back
 *
 * With the object active, every ASTE selected by `flags` (all, the one
 * for `segment`, or those named by `page_list`) is marked in transition
 * and, with the AST lock released:
 *
 *   - for a remote object, or when flags bit 1 is set, its pages (all
 *     32, or the run of consecutive descending list entries in this
 *     segment) go to PMAP_$FLUSH (flag 4 when flags bit 15 is set); the
 *     count of pages written accumulates; "0x3000C" from FLUSH is not an
 *     error; with bit 1 the ASTE is then handed to ast_$update_aste.  The
 *     walk stops early on bit 0, on bit 3 once 0x40 pages are written, or
 *     when the page list is exhausted;
 *   - otherwise (local, bit 1 clear) each installed page whose PFT word
 *     says MODIFIED has that bit moved into its MMAPE and the object is
 *     remembered as needing new clocks.
 *
 * Afterwards the DTM (TIME_$CLOCK), DTA (= DTM) and DTV (TIME_$ABS_CLOCK)
 * are stamped when any page was modified, ast_$purify_aote writes the
 * attributes, and the AOTE leaves transition.  An object that is not
 * active is activated only when flags bit 1 is set.  Then, with the AST
 * lock released and bit 1 set and no error so far: a local object's
 * volume buffers are flushed with DBUF_$UPDATE_VOL; a remote one (unless
 * bit 2) is purified at its home node with REM_FILE_$PURIFY - once for a
 * single segment, else in 64KB chunks with flag 8 until the node stops
 * answering 0x3EFFF, with one final un-flagged call when the chunk count
 * runs out or the node says "incompatible request".  Finally bit 3 turns
 * a clean status into 0x3EFFF when pages were written.
 *
 * Parameters (frame at 0x00E0567A, `link.w A6,-0xe8`):
 *   uid        (0x08,A6)  copied to (-0xA0,A6)
 *   flags      (0x0C,A6)  word (D5); the cell itself is rewritten at
 *                         0x00E05B28 and its address passed to REM_FILE
 *   segment    (0x0E,A6)  word
 *   page_list  (0x10,A6)  longwords, each segment << 5 | page
 *   list_count (0x14,A6)  word
 *   status     (0x16,A6)
 * Locals: (-0xC8) pages written, (-0xBC) status, (-0xD8) clocks needed,
 * (-0xB8) aote, (-0xC2) 1-based list index, (-0xBE) FLUSH flags, (-0xCE)
 * volume index, (-0x98) the net/node pair, (-0xE6) REM_FILE flags word,
 * (-0xA8) the zero location; D4 = byte offset of the next list entry + 4.
 *
 * Returns D0w: `flags & 0x7FE0` on the refusal exit; undefined on every
 * other exit (the callers ignore it).  The C returns 0 there.
 *
 * Original address: 0x00E0567A (1314 bytes), A5 = 0xE1DC80 (AST_ block;
 * (0x428,A5) = AST_$AST_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "time/time.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "dbuf/dbuf.h"
#include "rem_file/rem_file.h"

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *page_list, uint16_t list_count,
                     status_$t *status)
{
    uid_t local_uid;            /* (-0xA0,A6) */
    uint16_t written;           /* (-0xC8,A6) */
    status_$t local_status;     /* (-0xBC,A6) */
    int8_t need_clocks;         /* (-0xD8,A6) */
    aote_t *aote;               /* (-0xB8,A6) */
    aste_t *aste;               /* A4 */
    uint16_t list_idx;          /* (-0xC2,A6), 1-based */
    uint32_t list_pos;          /* D4: byte offset + 4 of the current entry */
    uint16_t flush_flags;       /* (-0xBE,A6) */
    uint16_t vol_idx;           /* (-0xCE,A6) */
    uint32_t net_node[2];       /* (-0x98,A6) */
    uint16_t rem_flags;         /* (-0xE6,A6) */
    uint32_t location;          /* (-0xA8,A6) */
    int8_t is_remote;           /* D3b */
    uint32_t length;            /* D2 */
    int8_t match;               /* D0b */
    uint16_t first_page;        /* D2w */
    uint16_t run;               /* D3w */
    int16_t flushed;            /* D0w */
    uint32_t *row;
    uint32_t *ent;
    int8_t any_modified;        /* D6b */
    uint32_t *pft;
    mmape_t *mmape;
    uint16_t chunks;            /* D3w */
    int16_t i;
    int16_t n;

    /* 0x00E05688..0x00E05694 */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    /* 0x00E05698..0x00E056AA: bits 5..14 are not understood */
    if ((flags & 0x7FE0) != 0) {
        *status = status_$ast_incompatible_request;
        return (uint16_t)(flags & 0x7FE0);
    }

    /* 0x00E056AE..0x00E056CC */
    written = 0;
    local_status = status_$ok;
    need_clocks = 0;
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E056CE..0x00E056DC */
    aote = ast_$lookup_aote_by_uid(&local_uid);
    if (aote == NULL) {
        goto not_active;                            /* 0x00E05A1C */
    }

    /* 0x00E056E0..0x00E056F8 */
    aote->flags |= AOTE_FLAG_IN_TRANS;
    aote->flags |= AOTE_FLAG_BUSY;
    list_idx = 1;
    list_pos = 4;
    aste = aote->aste_list;

    while (aste != NULL) {
        /* 0x00E05700..0x00E05730: does this ASTE take part? */
        if (flags & 0x10) {
            match = ((uint32_t)aste->segment ==
                     (page_list[list_pos / 4 - 1] >> 5)) ? -1 : 0;
        } else if (flags & 0x01) {
            match = (aste->segment == (uint16_t)segment) ? -1 : 0;
        } else {
            match = -1;
        }
        if (match >= 0) {
            goto next_aste;                         /* 0x00E05980 */
        }

        /* 0x00E05734..0x00E0573E: in transition - wait and restart the
         * list (the list index is NOT reset) */
        if ((int16_t)aste->flags < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            aste = aote->aste_list;
            continue;
        }

        /* 0x00E05740..0x00E05752 */
        aste->flags |= ASTE_FLAG_IN_TRANS;
        ML_$UNLOCK(AST_LOCK_ID);

        /* 0x00E05754..0x00E05762 */
        if (aote->remote_flag >= 0 && (flags & 0x02) == 0) {
            goto mark_modified;                     /* 0x00E058BA */
        }

        /* 0x00E05766..0x00E057CA: which pages of the segment */
        if (flags & 0x10) {
            /* the list entry's page, then as many consecutive lower
             * pages of the same segment as follow it in the list */
            first_page = (uint16_t)(page_list[list_pos / 4 - 1] & 0x1F);
            list_idx++;
            list_pos += 4;
            run = 1;
            n = (int16_t)(list_count - list_idx);
            if (n >= 0) {
                for (i = 0; i <= n; i++) {
                    uint32_t e = page_list[list_idx - 1];
                    if ((e >> 5) != (uint32_t)aste->segment) {
                        break;
                    }
                    if ((e & 0x1F) != (uint32_t)(uint16_t)(first_page - 1)) {
                        break;
                    }
                    run++;
                    first_page--;
                    list_idx++;
                    list_pos += 4;
                }
            }
        } else {
            first_page = 0;
            run = 0x20;
        }

        /* 0x00E057CC..0x00E05802: flag 4 when flags bit 15 is set */
        flush_flags = ((int16_t)flags < 0) ? 4 : 0;
        row = (uint32_t *)PMAP_SEGMAP_ROW(aste->seg_index);
        flushed = PMAP_$FLUSH(aste, row, first_page, (int16_t)run,
                              flush_flags, &local_status);

        /* 0x00E05806..0x00E05822: 0x3000C (with or without bit 31) is
         * not an error; anything else is */
        if (local_status != status_$ok) {
            if ((local_status & 0x7FFFFFFF) != 0x3000C) {
                goto flush_failed;                  /* 0x00E05856 */
            }
            local_status = status_$ok;
        }
        written = (uint16_t)(written + flushed);

        /* 0x00E05826..0x00E05854 */
        if (flags & 0x02) {
            ast_$update_aste(aste, (segmap_entry_t *)row, 0, &local_status);
            if (local_status != status_$ok) {
                goto flush_failed;
            }
        }

        /* 0x00E0586E..0x00E05892: stop conditions */
        if (flags & 0x01) {
            goto stop_walk;                         /* 0x00E05896 */
        }
        if ((flags & 0x08) && written >= 0x40) {
            goto stop_walk;
        }
        if ((flags & 0x10) && list_idx > list_count) {
            goto stop_walk;
        }
        goto leave_transition;                      /* 0x00E0595A */

flush_failed:
        /* 0x00E05856..0x00E0586A: no eventcount advance for the ASTE */
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        goto aote_done;                             /* 0x00E059F6 */

stop_walk:
        /* 0x00E05896..0x00E058B6 */
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        goto walk_done;                             /* 0x00E0598A */

mark_modified:
        /* 0x00E058BA..0x00E05958: move each installed page's PFT
         * MODIFIED bit into its MMAPE */
        ML_$LOCK(PMAP_LOCK_ID);
        if (aste->page_count != 0) {
            any_modified = 0;
            ent = (uint32_t *)PMAP_SEGMAP_ROW(aste->seg_index);
            for (i = 0x1F; i >= 0; i--) {
                while ((int32_t)*ent < 0) {
                    ast_$wait_for_page_transition();
                }
                if (*ent & SEGMAP_VALID) {
                    pft = PFT_FOR_PPN((uint16_t)(*ent & 0xFFFF));
                    if (*pft & PFT_FLAG_MODIFIED) {
                        *pft &= ~(uint32_t)PFT_FLAG_MODIFIED;
                        mmape = &MMAPE_BASE[*ent & 0xFFFF];
                        mmape->flags2 |= MMAPE_FLAG2_MODIFIED;  /* bset.b #6,+0x09 */
                        any_modified = -1;
                    }
                }
                ent++;
            }
            if (any_modified < 0) {
                need_clocks = -1;
            }
        }
        ML_$UNLOCK(PMAP_LOCK_ID);

leave_transition:
        /* 0x00E0595A..0x00E0597E: in list mode the same ASTE is tested
         * against the next entry */
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        if (flags & 0x10) {
            continue;
        }
next_aste:
        /* 0x00E05980 */
        aste = aste->next;
    }

walk_done:
    /* 0x00E0598A..0x00E059E0 */
    if (need_clocks < 0) {
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((clock_t *)&aote->dtm_high);            /* pea (0x28,A0) */
        aote->dta_high = aote->dtm_high;
        aote->dta_low = aote->dtm_low;
        TIME_$ABS_CLOCK((clock_t *)&aote->dtv_high);        /* pea (0x38,A0) */
        ML_$UNLOCK(PMAP_LOCK_ID);
        aote->flags |= AOTE_FLAG_DIRTY;
    }

    /* 0x00E059E2..0x00E059F2: `clr.w -(SP)` is the byte flag */
    ast_$purify_aote(aote, 0, &local_status);

aote_done:
    /* 0x00E059F6..0x00E05A16 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    vol_idx = aote->vol_index;
    goto snapshot;                                  /* 0x00E05A66 */

not_active:
    /* 0x00E05A1C..0x00E05A62: only bit 1 activates the object */
    if ((flags & 0x02) == 0) {
        goto unlock;                                /* 0x00E05A7C */
    }
    location = 0;
    aote = ast_$force_activate_segment(&local_uid, location, &local_status, 0);
    if (aote == NULL) {
        ML_$UNLOCK(AST_LOCK_ID);
        PROC1_$INHIBIT_END();
        goto store_status;                          /* 0x00E05B8A */
    }
    vol_idx = aote->vol_index;

snapshot:
    /* 0x00E05A66..0x00E05A78: aote+0xAC/+0xB0, remote flag, length */
    net_node[0] = aote->obj_loc_net;
    net_node[1] = aote->obj_loc_node;
    is_remote = (aote->remote_flag < 0) ? -1 : 0;
    length = aote->length;

unlock:
    /* 0x00E05A7C..0x00E05A96 */
    ML_$UNLOCK(AST_LOCK_ID);
    if ((flags & 0x02) == 0 || local_status != status_$ok) {
        goto inhibit_end;                           /* 0x00E05B6A */
    }
    /* 0x00E05A9A: is_remote / length are only meaningful here, where the
     * object was active or just activated */
    if (is_remote >= 0) {
        /* 0x00E05B58..0x00E05B68 */
        DBUF_$UPDATE_VOL(vol_idx, &local_uid);
        goto inhibit_end;
    }
    /* 0x00E05AA0..0x00E05AA8 */
    if (flags & 0x04) {
        goto inhibit_end;
    }
    flags &= 0x7FFF;
    if (flags & 0x01) {
        /* 0x00E05AB2..0x00E05ADC: one segment */
        rem_flags = (uint16_t)(flags & 0x7FFF);
        REM_FILE_$PURIFY((uid_t *)net_node, &local_uid, &rem_flags, segment,
                         &local_status);
        goto inhibit_end;
    }
    /* 0x00E05AE0..0x00E05AEC: (length >> 16) + 1 chunks, flag 8 */
    chunks = (uint16_t)((length >> 16) + 1);
    rem_flags = (uint16_t)(flags | 0x08);
    for (;;) {
        /* 0x00E05AF0..0x00E05B0C */
        REM_FILE_$PURIFY((uid_t *)net_node, &local_uid, &rem_flags, segment,
                         &local_status);
        /* 0x00E05B10..0x00E05B26 */
        chunks--;
        if ((chunks == 0 && local_status == 0x3EFFF) ||
            local_status == status_$ast_incompatible_request) {
            /* 0x00E05B28..0x00E05B48: the final call uses the argument
             * cell itself, rewritten from D5 */
            REM_FILE_$PURIFY((uid_t *)net_node, &local_uid, &flags, segment,
                             &local_status);
        }
        /* 0x00E05B4C..0x00E05B56 */
        if (local_status != 0x3EFFF) {
            break;
        }
    }

inhibit_end:
    /* 0x00E05B6A..0x00E05B82 */
    PROC1_$INHIBIT_END();
    if ((flags & 0x08) && local_status == status_$ok && written != 0) {
        local_status = 0x3EFFF;
    }

store_status:
    /* 0x00E05B8A..0x00E05B8E */
    *status = local_status;
    return 0;
}
