/*
 * AST_$TRUNCATE - Cut an object back to a new length (or delete it)
 *
 * flags bit 0 = DELETE (the length becomes 0), bit 1 = "force" for a
 * delete.  With the object active (activated with the zero location when
 * needed):
 *
 *   - an OS-only local object cannot be touched by a type-9 process
 *     ("only local access allowed");
 *   - deleting a local object: with `force` a referenced object
 *     (refcount > 1) is instead handed to AST_$SET_ATTRIBUTE attribute 7
 *     with the value 1 and the call ends; without it a referenced object
 *     is left alone; an object whose access byte has bit 6 set cannot be
 *     deleted ("system object cannot be deleted"); otherwise the result
 *     byte is set TRUE and the object's ACL UID (aote+0x94) is kept
 *     for a retry;
 *   - a read-only volume (attribute bit 1) refuses with "volume has been
 *     mounted read-only"; a referenced AOTE with "not deactivatable".
 *
 * Then, in transition, every ASTE at or above the new end segment is
 * visited from the head of the list: one wholly beyond the end (or at
 * the end when the length is segment-aligned) is deactivated and freed;
 * the one holding the end has, under the PMAP lock, its pages from the
 * first page past the new length dropped (a wired MMAPE fails with
 * "pages wired"; an MMAPE that names another page crashes), the blocks
 * of every entry with a disk address collected, the map written back
 * with ast_$update_aste(TRUE), and - for a local object - the blocks
 * subtracted from aote+0x24 and given to BAT_$FREE.  The length is then
 * stored (stamping DTA and DTM) when it changed and this is not a
 * delete, and a local object's VTOCE truncated with VTOCE_$TRUNCATE
 * (the blocks it frees also leave aote+0x24).  A delete finishes with
 * ast_$process_aote and ast_$release_aote and, when a second UID was
 * kept, the whole procedure repeats for that UID (a "not found" on that
 * second pass is not an error).  A remote object is truncated at its
 * home node with REM_FILE_$TRUNCATE, whose reply clock becomes the DTA
 * and DTM of the (re-looked-up) AOTE unless this was a delete.
 *
 * Parameters (frame at 0x00E05C40, `link.w A6,-0x128`):
 *   uid      (0x08,A6)  copied to (-0xD8,A6); bit 24 of the low longword
 *                       (bit 0 of its first byte) is cleared in the
 *                       working copy at (-0x8,A6)
 *   new_size (0x0C,A6)  longword; rewritten to 0 for a delete
 *   flags    (0x10,A6)  word (D0); bit 0 -> D7 (delete), bit 1 -> D2
 *   result   (0x12,A6)  a BOOLEAN byte, cleared on entry, set on a local
 *                       delete
 *   status   (0x16,A6)  written from (-0xE8,A6), except that the remote
 *                       call writes it directly
 * Locals: (-0x40) the remote reply clock, (-0xC0) the 32-block array,
 * (-0xC8) net/node pair, (-0xD0) the second UID, (-0xE0) VTOCE_$TRUNCATE's
 * freed count, (-0xE4) the inner status, (-0xEC) the rounded-up length
 * for VTOCE_$TRUNCATE, (-0xF0) the byte offset within the end segment,
 * (-0xF8) the ASTE's row, (-0x100) aote, D3 = "second pass", D6 = the end
 * segment.
 *
 * Original address: 0x00E05C40 (1722 bytes), A5 = 0xE1DC80 (AST_ block;
 * (0x428,A5) = AST_$AST_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "bat/bat.h"
#include "vtoc/vtoc.h"
#include "rem_file/rem_file.h"

/*
 * 0x00E05F1C: pea (-0x5072,PC) -> 0x00E00EAC, jsr CRASH_SYSTEM at
 * 0x00E05F20.  Image bytes 00 05 00 03 (pmap "mismatch"): an installed
 * frame whose MMAPE names a different page.  Shared with AST_$TOUCH,
 * AST_$PMAP_ASSOC, ast_$allocate_pages and AST_$ASSOC_AREA.
 */
static const status_$t pmap_$mismatch_00e00eac = 0x00050003;

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    uid_t work_uid;             /* (-0xD8,A6): the UID this pass works on */
    uid_t local_uid;            /* (-0x8,A6): with bit 0 of the low word off */
    uid_t second_uid;           /* (-0xD0,A6) */
    uint32_t net_node[2];       /* (-0xC8,A6) */
    uint32_t blocks[32];        /* (-0xC0,A6) */
    clock_t remote_dtm;         /* (-0x40,A6) */
    status_$t local_status;     /* (-0xE8,A6) */
    status_$t inner_status;     /* (-0xE4,A6) */
    uint32_t freed;             /* (-0xE0,A6) / D2 */
    uint32_t rounded_size;      /* (-0xEC,A6) */
    uint32_t seg_offset;        /* (-0xF0,A6): new_size & 0x7FFF */
    uint32_t location;          /* (-0xF4,A6) */
    uint32_t *row;              /* (-0xF8,A6) */
    aote_t *aote;               /* (-0x100,A6) */
    aste_t *aste;               /* A4 */
    uint32_t *entry;            /* A2 */
    mmape_t *mmape;             /* A3 - 0x2000 */
    int8_t is_delete;           /* D7b */
    int8_t force;               /* D2b */
    int8_t second_pass;         /* D3b */
    uint32_t end_seg;           /* D6 */
    uint16_t nblocks;           /* D4w */
    uint16_t first_page;        /* D3w */
    int16_t pages_left;         /* D5w */
    uint32_t ppn;               /* D2 */
    uint16_t attr_value;        /* (-0x40,A6) on the attribute path */
    int16_t i;

    /* 0x00E05C4E..0x00E05C5A */
    work_uid.high = uid->high;
    work_uid.low = uid->low;

    /* 0x00E05C5E..0x00E05C70: the two flag bits; result := FALSE */
    is_delete = (flags & 0x01) ? -1 : 0;
    force = (flags & 0x02) ? -1 : 0;
    second_pass = 0;
    *result = 0;

    /* 0x00E05C72..0x00E05C8E */
    local_status = status_$ok;
    if (is_delete < 0) {
        new_size = 0;
    }
    end_seg = new_size >> 15;
    seg_offset = new_size & 0x7FFF;

    /* 0x00E05C92 */
    PROC1_$INHIBIT_BEGIN();

retry:
    /* 0x00E05C98..0x00E05CAA: rounded length cell, working UID with bit 0
     * of its low word cleared */
    rounded_size = new_size;
    local_uid.high = work_uid.high;
    /* bclr.b #0,(-0x4,A6): bit 0 of the BYTE at -0x4, which is the most
     * significant byte of the big-endian low longword = bit 24 */
    local_uid.low = work_uid.low & ~0x01000000u;

    /* 0x00E05CB0..0x00E05CBC */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E05CBE..0x00E05D1E */
    aote = ast_$lookup_aote_by_uid(&local_uid);
    if (aote == NULL) {
        location = 0;
        aote = ast_$force_activate_segment(&local_uid, location,
                                           &local_status, 0);
        if (aote == NULL) {
            /* 0x00E05CF0..0x00E05D14: on the second pass "not found" is
             * fine */
            ML_$UNLOCK(AST_LOCK_ID);
            if (second_pass < 0 && local_status == file_$object_not_found) {
                local_status = status_$ok;
            }
            goto done;
        }
    } else {
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E05D1E..0x00E05D48: OS-only local object, type-9 process */
    if (aote->access_flags < 0 && aote->remote_flag >= 0 &&
        PROC1_$TYPE[PROC1_$CURRENT] == 9) {
        local_status = status_$ast_only_local_access_allowed;   /* 0x3000A */
        goto unlock_done;                                       /* 0x00E0620E */
    }

    /* 0x00E05D4C..0x00E05DC0: deleting a local object */
    if (is_delete < 0 && aote->remote_flag >= 0) {
        if (force < 0) {
            /* 0x00E05D5A..0x00E05D92: still referenced elsewhere - drop
             * one reference instead (attribute 7, value 1) and finish;
             * that call's status goes straight to the caller */
            if (aote->refcount > 1) {
                ML_$UNLOCK(AST_LOCK_ID);
                attr_value = 1;
                PROC1_$INHIBIT_END();
                AST_$SET_ATTRIBUTE(&local_uid, 7, &attr_value, &local_status);
                goto store_status;                              /* 0x00E062E8 */
            }
        } else {
            /* 0x00E05D96: a referenced object is left alone */
            if (aote->refcount != 0) {
                goto unlock_done;
            }
        }
        /* 0x00E05D9E..0x00E05DC0 */
        if (aote->access_flags & 0x40) {
            local_status = 0x0003000B;      /* system object cannot be deleted */
            goto unlock_done;
        }
        *result = -1;                                           /* st (A1) */
        second_uid = aote->acl_uid;                             /* aote+0x94 */
    }

    /* 0x00E05DC4..0x00E05DE6 */
    if (aote->attr_flags_lo & 0x02) {
        local_status = status_$file_volume_has_been_mounted_read_only;  /* 0xF0016 */
        goto unlock_done;
    }
    if (aote->ref_count != 0) {
        local_status = status_$ast_segment_not_deactivatable;   /* 0x30004 */
        goto unlock_done;
    }

    /* 0x00E05DEA */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* 0x00E06068..0x00E06070 with 0x00E05DF4..0x00E06064: the ASTE list
     * is re-read from its head after every change */
    while (aote->aste_list != NULL) {
        aste = aote->aste_list;

        /* 0x00E05DFC..0x00E05E00: the list is descending - below the end
         * segment nothing more is affected */
        if ((uint16_t)end_seg > aste->segment) {
            break;
        }
        /* 0x00E05E04..0x00E05E0E */
        if ((int16_t)aste->flags < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            continue;
        }

        /* 0x00E05E12..0x00E05E22: wholly beyond the end, or exactly at
         * the end with nothing kept in it -> deactivate and free */
        if (aste->segment > (uint16_t)end_seg ||
            (aste->segment == (uint16_t)end_seg && seg_offset == 0)) {
            /* 0x00E05E24..0x00E05E52: `clr.w` (keep = FALSE), `st`
             * (purge = TRUE) */
            AST_$DEACTIVATE_SEGMENT(aste, -1, 0, &local_status);
            if (local_status != status_$ok) {
                goto clear_in_trans_fail;                       /* 0x00E05E3C */
            }
            AST_$FREE_ASTE(aste);
            continue;
        }

        /* 0x00E05E56..0x00E05E82: the segment holding the new end.  The
         * VTOCE length is rounded up to the next segment; nothing to do
         * when the length is unchanged */
        row = (uint32_t *)((char *)SEGMAP_BASE +
                           ((uint32_t)aste->seg_index << 7) - 0x80);
        rounded_size = ((uint32_t)(uint16_t)end_seg + 1) << 15;
        if (aote->length == new_size) {
            break;                                              /* 0x00E06074 */
        }

        /* 0x00E05E86..0x00E05EA6 */
        aste->flags |= ASTE_FLAG_IN_TRANS;
        ML_$UNLOCK(AST_LOCK_ID);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E05EA8..0x00E05EC8: the first page past the new length,
         * pages first..31 */
        nblocks = 0;
        first_page = (uint16_t)((seg_offset + 0x3FF) >> 10);
        pages_left = (int16_t)(0x1F - first_page);
        if (pages_left >= 0) {
            entry = &row[first_page];
            for (i = 0; i <= pages_left; i++) {
                /* 0x00E05EDC..0x00E05EE2 */
                while ((int32_t)*entry < 0) {
                    ast_$wait_for_page_transition();
                }
                /* 0x00E05EE4..0x00E05F5C: an installed page is dropped */
                if (*entry & SEGMAP_VALID) {
                    ppn = *entry & 0xFFFF;
                    mmape = &MMAPE_BASE[ppn];
                    if (mmape->wire_count != 0) {
                        local_status = status_$pmap_pages_wired;    /* 0x50007 */
                        break;                                      /* 0x00E05F96 */
                    }
                    if (mmape->seg_offset != (uint8_t)(first_page + i)) {
                        CRASH_SYSTEM(&pmap_$mismatch_00e00eac);
                    }
                    if (*entry & SEGMAP_WIRED) {
                        *entry &= ~SEGMAP_WIRED;
                        MMU_$REMOVE(ppn);
                    }
                    *entry &= ~SEGMAP_VALID;
                    *entry &= 0xFF800000u;
                    *entry |= mmape->disk_addr;
                    MMAP_$FREE_REMOVE(mmape, ppn);
                    aste->page_count--;
                }
                /* 0x00E05F60..0x00E05F8A: a disk address is collected and
                 * the entry emptied; the ASTE is dirty */
                if ((*entry & 0x7FFFFF) != 0) {
                    blocks[nblocks] = *entry & 0x3FFFFF;
                    nblocks++;
                    *entry &= 0xFF800000u;
                    aste->flags |= ASTE_FLAG_DIRTY;
                }
                entry++;
            }
        }

        /* 0x00E05F96..0x00E05FC4: write the map back (write_now TRUE);
         * its status replaces a clean one */
        ML_$UNLOCK(PMAP_LOCK_ID);
        ast_$update_aste(aste, (segmap_entry_t *)row, -1, &inner_status);
        if (local_status == status_$ok) {
            local_status = inner_status;
        }

        /* 0x00E05FC6..0x00E06038: a local object gives the blocks back */
        if (nblocks != 0 && aote->remote_flag >= 0) {
            ML_$LOCK(PMAP_LOCK_ID);
            aote->unknown_24 -= nblocks;
            aote->flags |= AOTE_FLAG_DIRTY;
            ML_$UNLOCK(PMAP_LOCK_ID);
            BAT_$FREE(blocks, (int16_t)nblocks, (int16_t)aote->vol_index, 0,
                      &inner_status);
            if (local_status == status_$ok) {
                if (inner_status != status_$ok) {
                    inner_status |= (status_$t)0x80000000u;
                }
                local_status = inner_status;
            }
        }

        /* 0x00E0603E..0x00E06064 */
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        if (local_status != status_$ok) {
            goto clear_in_trans_fail;
        }
        break;                                                  /* 0x00E06074 */
    }

    /* 0x00E06074..0x00E060CC: store the new length (not for a delete,
     * not when unchanged), stamping DTA and DTM */
    if (is_delete >= 0 && aote->length != new_size) {
        ML_$LOCK(PMAP_LOCK_ID);
        aote->length = new_size;
        TIME_$CLOCK((clock_t *)&aote->dta_high);
        aote->dtm_high = aote->dta_high;
        aote->dtm_low = aote->dta_low;
        aote->flags |= AOTE_FLAG_DIRTY;
        ML_$UNLOCK(PMAP_LOCK_ID);
    }

    /* 0x00E060CE */
    if (aote->remote_flag < 0) {
        goto remote;                                            /* 0x00E061D2 */
    }

    /* 0x00E060DA..0x00E0611E: VTOCE_$TRUNCATE(&obj_loc, new_size,
     * rounded_size, is_delete byte, &freed, &status) with the AST lock
     * released.  `move.b D7b,-(SP)` at 0x00E060F2 pushes ONE byte in a
     * word slot (&freed sits at 0x16), the boolean delete_it argument. */
    ML_$UNLOCK(AST_LOCK_ID);
    VTOCE_$TRUNCATE((vtoc_$lookup_req_t *)(void *)&aote->obj_uid, new_size,
                    (int32_t)rounded_size, is_delete, &freed, &local_status);
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E06120 */
    if (local_status != status_$ok) {
        goto clear_in_trans_fail;
    }

    /* 0x00E06128..0x00E06194 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    if (is_delete < 0) {
        /* `st` (wait), `clr.w` (keep), `st` (purge) */
        ast_$process_aote(aote, -1, 0, -1, &local_status);
        if (local_status != status_$ok) {
            goto unlock_aote;                                   /* 0x00E06196 */
        }
        ast_$release_aote(aote);
    } else {
        if (freed != 0) {
            ML_$LOCK(PMAP_LOCK_ID);
            aote->unknown_24 -= freed;
            aote->flags |= AOTE_FLAG_DIRTY;
            ML_$UNLOCK(PMAP_LOCK_ID);
        }
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    }

unlock_aote:
    /* 0x00E06196..0x00E061A2 */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E061A4..0x00E061CE: a delete with a second UID runs again for
     * it; the first byte of that UID zero means there is none */
    if (is_delete < 0 && local_status == status_$ok &&
        (second_uid.high & 0xFF000000u) != 0) {
        work_uid = second_uid;
        force = -1;
        second_pass = -1;
        goto retry;                                             /* 0x00E05C98 */
    }
    goto done;                                                  /* 0x00E062E2 */

remote:
    /* 0x00E061D2..0x00E06200: a delete deactivates the AOTE first */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    if (is_delete < 0) {
        ast_$process_aote(aote, -1, 0, -1, &local_status);
        if (local_status != status_$ok) {
            goto clear_advance_unlock;                          /* 0x00E06202 */
        }
        ast_$release_aote(aote);
    } else {
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);                     /* 0x00E06220 */
    }

    /* 0x00E0622C..0x00E06268: REM_FILE_$TRUNCATE(&net_node, &local_uid,
     * new_size, is_delete byte, &remote_dtm, THE CALLER'S status) */
    net_node[0] = aote->obj_loc_net;
    net_node[1] = aote->obj_loc_node;
    ML_$UNLOCK(AST_LOCK_ID);
    REM_FILE_$TRUNCATE((uid_t *)net_node, &local_uid, new_size,
                       (uint8_t)is_delete, &remote_dtm, status);

    /* 0x00E0626C..0x00E062D8: unless deleting, and when the node was
     * happy, the reply clock becomes the (re-looked-up) object's DTA
     * and DTM */
    if (is_delete >= 0 && *status == status_$ok) {
        ML_$LOCK(AST_LOCK_ID);
        aote = ast_$lookup_aote_by_uid(&local_uid);
        if (aote != NULL) {
            ML_$LOCK(PMAP_LOCK_ID);
            aote->dta_high = remote_dtm.high;
            aote->dta_low = remote_dtm.low;
            aote->dtm_high = remote_dtm.high;
            aote->dtm_low = remote_dtm.low;
            ML_$UNLOCK(PMAP_LOCK_ID);
        }
        ML_$UNLOCK(AST_LOCK_ID);
    }
    /* 0x00E062DA..0x00E062E0: the status cell is NOT rewritten here */
    PROC1_$INHIBIT_END();
    return;

clear_in_trans_fail:
    /* 0x00E05E3C..0x00E05E46 -> 0x00E06202 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
clear_advance_unlock:
    /* 0x00E06202..0x00E0620C */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
unlock_done:
    /* 0x00E0620E..0x00E0621C */
    ML_$UNLOCK(AST_LOCK_ID);

done:
    /* 0x00E062E2 */
    PROC1_$INHIBIT_END();
store_status:
    /* 0x00E062E8..0x00E062EC */
    *status = local_status;
}
